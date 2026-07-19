#pragma once

// The Hyprland-provided Log::logger in src/debug/log/Logger.hpp is
// declared `inline UP<CLogger> logger = makeUnique<CLogger>();` in a
// header. Because the plugin is loaded as a separate DSO with its own
// copy of that header, our Log::logger is a distinct, uninitialised
// CLogger instance whose output goes nowhere. Writing to it makes the
// plugin appear silent in Hyprland's log.
//
// Workaround, same as hyprwsmode: write to stderr directly. Hyprland
// captures fd 2 for its own log output (systemd journal or a
// redirected log file), so plugin messages appear alongside compositor
// messages there.

#include <cstdio>
#include <format>
#include <string>
#include <utility>

namespace hyprdictate::log {

    template <typename... Args>
    void write(const char* level, std::format_string<Args...> fmt, Args&&... args) {
        // Format into a std::string first so a malformed format's
        // exception does not fire inside fprintf.
        const auto msg = std::format(fmt, std::forward<Args>(args)...);
        std::fprintf(stderr, "[hyprdictate] %s: %s\n", level, msg.c_str());
    }

    template <typename... Args>
    void info(std::format_string<Args...> fmt, Args&&... args) {
        write("info", fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void warn(std::format_string<Args...> fmt, Args&&... args) {
        write("warn", fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void debug(std::format_string<Args...> fmt, Args&&... args) {
        write("debug", fmt, std::forward<Args>(args)...);
    }

}  // namespace hyprdictate::log
