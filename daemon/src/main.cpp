// hyprdictated — voice dictation daemon for Hyprland.
//
// Wires:
//   config      — TOML at $XDG_CONFIG_HOME/hyprdictate/config.toml
//   whisper     — resident model loaded on startup
//   audio       — PipeWire capture into an in-memory buffer
//   session     — state machine over the wire commands
//   ipc         — asio unix-socket server carrying Commands and Events
//
// Text injection lands in the next commit; for this milestone the
// transcript flows out over the socket only.

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

#include <CLI/CLI.hpp>
#include <asio.hpp>
#include <spdlog/spdlog.h>

#include "audio.hpp"
#include "config.hpp"
#include "inject.hpp"
#include "ipc.hpp"
#include "log.hpp"
#include "session.hpp"
#include "vocabulary.hpp"
#include "whisper_engine.hpp"

namespace {

    constexpr const char* kVersion = "0.1";

    // Resolve the daemon's socket path. The design doc places it at
    // $XDG_RUNTIME_DIR/hyprdictate.sock; if XDG_RUNTIME_DIR isn't set
    // (unusual outside a real session), fall back to /tmp so the
    // daemon at least starts and complains via logs.
    std::filesystem::path defaultSocketPath() {
        if (const char* xdg = std::getenv("XDG_RUNTIME_DIR"); xdg && *xdg)
            return std::filesystem::path{xdg} / "hyprdictate.sock";
        return std::filesystem::path{"/tmp"} / "hyprdictate.sock";
    }

}

int main(int argc, char** argv) {
    hyprdictate::log::init();

    CLI::App app{"hyprdictated - voice dictation daemon for Hyprland"};

    std::string config_arg;
    app.add_option("-c,--config", config_arg,
                   "Path to config.toml "
                   "(default: $XDG_CONFIG_HOME/hyprdictate/config.toml)");

    std::string socket_arg;
    app.add_option("-s,--socket", socket_arg,
                   "Path to the daemon's unix socket "
                   "(default: $XDG_RUNTIME_DIR/hyprdictate.sock)");

    bool show_version = false;
    app.add_flag("--version", show_version, "Print version and exit");

    CLI11_PARSE(app, argc, argv);

    if (show_version) {
        std::cout << "hyprdictate " << kVersion << "\n";
        return 0;
    }

    std::optional<std::filesystem::path> config_path;
    if (!config_arg.empty())
        config_path = std::filesystem::path{config_arg};

    hyprdictate::Config      config;
    std::filesystem::path    resolved;
    try {
        config = hyprdictate::Config::load(config_path, &resolved);
    } catch (const hyprdictate::ConfigError& e) {
        spdlog::error("config load failed: {}", e.what());
        return 2;
    } catch (const std::exception& e) {
        spdlog::error("unexpected error loading config: {}", e.what());
        return 2;
    }

    spdlog::info("hyprdictated {} starting", kVersion);
    spdlog::info("config     = {}", resolved.string());
    spdlog::info("model_path = {}", config.model_path.string());
    spdlog::info("language   = {}", config.language);

    // Load the whisper model up front. Failing here is fatal for the
    // daemon: without an engine there's nothing useful to run.
    std::unique_ptr<hyprdictate::WhisperEngine> engine;
    try {
        engine = std::make_unique<hyprdictate::WhisperEngine>(
            config.model_path,
            config.whisper,
            config.language,
            config.threads);
    } catch (const hyprdictate::WhisperError& e) {
        spdlog::error("whisper engine failed to load: {}", e.what());
        return 3;
    } catch (const std::exception& e) {
        spdlog::error("unexpected error initialising whisper: {}", e.what());
        return 3;
    }

    // PipeWire connect happens next so a missing session-daemon or a
    // permission issue on the audio graph produces its diagnostic
    // before the socket listener opens and starts accepting toggle
    // commands the daemon can't fulfil.
    std::unique_ptr<hyprdictate::AudioCapture> audio;
    try {
        audio = std::make_unique<hyprdictate::AudioCapture>();
    } catch (const hyprdictate::AudioError& e) {
        spdlog::error("audio capture setup failed: {}", e.what());
        return 4;
    } catch (const std::exception& e) {
        spdlog::error("unexpected error initialising audio: {}", e.what());
        return 4;
    }

    // The daemon's event loop is a single-threaded asio io_context.
    // All socket I/O and command dispatch runs on this thread; the
    // only off-thread work is the PipeWire process callback (on its
    // own thread_loop) and per-utterance whisper worker threads
    // launched from Session::startTranscription. Both hand back to
    // the io_context via asio::post before touching connection state.
    asio::io_context io;

    // The IPC server is constructed before the Session so the Session
    // can capture a pointer to it in its emitter lambda; the pointer
    // is dereferenced only after start(), so use before initialisation
    // isn't a hazard here.
    std::unique_ptr<hyprdictate::IpcServer> ipc;

    hyprdictate::WtypeInjector injector;

    hyprdictate::Session session(
        *audio,
        *engine,
        [&ipc](const hyprdictate::Event& e) {
            if (ipc) ipc->broadcast(e);
        },
        // Text injection via wtype. Runs on the worker thread that
        // Session launched for transcription, so a slow wtype exec
        // does not block the IPC loop.
        //
        // The suppression decision (skip wtype when a client owns
        // injection) lives inside Session::completeTranscription
        // and is gated per-recording rather than per-connection. By
        // the time this lambda is called, the daemon has already
        // decided this recording needs wtype.
        [&injector](const std::string&                                  text,
                    const std::optional<hyprdictate::WindowContext>&    window) {
            injector.inject(text, window);
        },
        // Initial-prompt supplier: layer 1 (global vocabulary) only
        // for M1. Layers 2 and 3 (per-class and title-token) plug in
        // through the same callback signature in M4.
        [&config](const std::optional<hyprdictate::WindowContext>& window) {
            return hyprdictate::composePrompt(config.vocabulary, window);
        });

    const std::filesystem::path socket_path =
        !socket_arg.empty() ? std::filesystem::path{socket_arg}
                            : defaultSocketPath();

    try {
        ipc = std::make_unique<hyprdictate::IpcServer>(io, socket_path, session);
        ipc->start();
    } catch (const std::exception& e) {
        spdlog::error("ipc server failed to start on {}: {}",
                      socket_path.string(), e.what());
        return 5;
    }

    // Level meter fan-out: AudioCapture fires the callback from the
    // PipeWire thread every ~50 ms while a stream is connected.
    // ipc->broadcast is thread-safe (posts onto the io_context), so
    // no additional marshalling is required. Only fires during
    // Recording — the stream is torn down on stop/cancel.
    audio->setLevelCallback([&ipc](float level) {
        if (ipc)
            ipc->broadcast(hyprdictate::event::Level{ .value = level });
    });

    // Graceful shutdown on SIGINT/SIGTERM: cancel any in-flight
    // accept and let io.run() unwind. asio::signal_set is a
    // cross-platform wrapper over sigaction/sigprocmask.
    asio::signal_set signals(io, SIGINT, SIGTERM);
    signals.async_wait([&](const std::error_code&, int signo) {
        spdlog::info("received signal {}, shutting down", signo);
        ipc->stop();
        io.stop();
    });

    spdlog::info("daemon ready");
    try {
        io.run();
    } catch (const std::exception& e) {
        spdlog::error("event loop terminated: {}", e.what());
        return 6;
    }

    spdlog::info("daemon exited cleanly");
    return 0;
}
