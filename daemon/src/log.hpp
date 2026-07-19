#pragma once

// Process-wide log setup for hyprdictated.
//
// spdlog is initialised once at the top of main(), before any other
// subsystem starts logging. The default sink is stderr, which systemd
// --user picks up into the journal (visible via `journalctl --user
// -u hyprdictate.service`). Ad-hoc runs from a terminal write to the
// terminal's stderr unchanged.
//
// The daemon does not open a log file. Rotating a file from a
// long-running daemon adds a moving-part we don't need when systemd
// (or the user's terminal) already owns log persistence.

#include <cstdlib>

#include <spdlog/spdlog.h>

namespace hyprdictate::log {

    // Initialise the default logger. Sets the format pattern and
    // reads HYPRDICTATE_LOG_LEVEL (trace|debug|info|warn|err|critical)
    // if set, otherwise defaults to info. Keeping this in a header
    // trades a small compile-time hit for eliminating a whole
    // translation unit dedicated to two lines of logic.
    inline void init() {
        spdlog::set_pattern("[%Y-%m-%d %T.%e] [%^%l%$] %v");

        if (const char* env = std::getenv("HYPRDICTATE_LOG_LEVEL"); env && *env) {
            spdlog::set_level(spdlog::level::from_str(env));
        } else {
            spdlog::set_level(spdlog::level::info);
        }
    }

}
