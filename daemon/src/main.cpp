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
#include <optional>
#include <string>

#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>

#include "config.hpp"
#include "log.hpp"

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

    // M1 skeleton exit: later commits install the whisper engine,
    // audio thread, IPC server, and the toggle state machine here,
    // then block until a signal handler flips the shutdown flag.
    spdlog::info("skeleton run complete; runtime wiring lands in later commits");
    return 0;
}
