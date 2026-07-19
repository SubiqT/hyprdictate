// hyprdictated — voice dictation daemon for Hyprland.
//
// This is the M1 skeleton: parses CLI arguments, initialises logging,
// loads the config, and exits cleanly. Later commits in the M1 series
// layer on whisper.cpp model loading, PipeWire audio capture, the
// unix-socket IPC server, and text injection.
//
// Rationale for the layered approach lives in the individual feature
// commits' messages; this file's job is to be the wiring diagram.

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>

#include "audio.hpp"
#include "config.hpp"
#include "log.hpp"
#include "whisper_engine.hpp"

namespace {

    constexpr const char* kVersion = "0.1";

}

int main(int argc, char** argv) {
    hyprdictate::log::init();

    CLI::App app{"hyprdictated - voice dictation daemon for Hyprland"};

    std::string config_arg;
    app.add_option("-c,--config", config_arg,
                   "Path to config.toml "
                   "(default: $XDG_CONFIG_HOME/hyprdictate/config.toml)");

    bool show_version = false;
    app.add_flag("--version", show_version, "Print version and exit");

    // CLI11_PARSE prints its own error messages and returns a non-zero
    // exit code on argv parse failure; nothing to do in that branch.
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
    spdlog::info("config loaded from {}", resolved.string());
    spdlog::info("model_path = {}", config.model_path.string());
    spdlog::info("language   = {}", config.language);
    spdlog::info("threads    = {} ({})",
                 config.threads,
                 config.threads == 0 ? "auto" : "explicit");

    // Load the whisper model up front. Failing here (missing model
    // file, unrecognised format) is fatal for the daemon: without an
    // engine there is nothing useful to run. This matches the design
    // doc's "Load whisper model at startup, keep it resident" line
    // and gives the systemd unit a clean signal to restart against.
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

    // M1 skeleton exit: later commits install the IPC server and
    // toggle state machine here, then block until a signal handler
    // flips the shutdown flag.
    spdlog::info("audio + model resident; runtime wiring lands in later commits");
    return 0;
}
