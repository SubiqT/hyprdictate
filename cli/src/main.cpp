// hyprdictate — thin CLI over the daemon's unix socket.
//
// Every subcommand maps to a single Command variant on the wire.
// `status` reads one reply from the daemon and prints it as JSON so
// scripts can consume it; every other subcommand sends and exits.
//
// The CLI keeps its own connect + write + read loop rather than
// pulling asio into a 100-line binary. sockets, iostreams, and a
// small parse routine are enough.

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>

#include "hyprdictate/protocol.hpp"

namespace {

    constexpr const char* kVersion = "0.1";

    std::filesystem::path defaultSocketPath() {
        if (const char* xdg = std::getenv("XDG_RUNTIME_DIR"); xdg && *xdg)
            return std::filesystem::path{xdg} / "hyprdictate.sock";
        return std::filesystem::path{"/tmp"} / "hyprdictate.sock";
    }

    // Connect to the daemon's unix socket. Prints a diagnostic to
    // stderr on failure and returns -1. Success returns the connected
    // socket file descriptor; caller is responsible for closing it.
    int connectDaemon(const std::filesystem::path& socket_path) {
        const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) {
            std::cerr << "hyprdictate: socket(): "
                      << std::strerror(errno) << "\n";
            return -1;
        }

        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        const auto path_str = socket_path.string();
        if (path_str.size() >= sizeof(addr.sun_path)) {
            std::cerr << "hyprdictate: socket path too long: " << path_str << "\n";
            ::close(fd);
            return -1;
        }
        std::memcpy(addr.sun_path, path_str.data(), path_str.size());

        if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            std::cerr << "hyprdictate: connect(" << path_str << "): "
                      << std::strerror(errno) << "\n"
                      << "  (is hyprdictated running?)\n";
            ::close(fd);
            return -1;
        }

        return fd;
    }

    // Send one command line. Returns true on complete write.
    bool sendCommand(int fd, const hyprdictate::Command& cmd) {
        const auto payload = hyprdictate::serialize(cmd).dump() + "\n";
        std::size_t written = 0;
        while (written < payload.size()) {
            const ssize_t n = ::write(fd, payload.data() + written,
                                      payload.size() - written);
            if (n < 0) {
                if (errno == EINTR) continue;
                std::cerr << "hyprdictate: write(): "
                          << std::strerror(errno) << "\n";
                return false;
            }
            written += static_cast<std::size_t>(n);
        }
        return true;
    }

    // Read one \n-terminated line from the socket. Returns nullopt on
    // EOF or error. Blocks until the line completes.
    std::optional<std::string> readLine(int fd) {
        std::string buf;
        char        c;
        while (true) {
            const ssize_t n = ::read(fd, &c, 1);
            if (n == 0) return std::nullopt;
            if (n < 0) {
                if (errno == EINTR) continue;
                return std::nullopt;
            }
            if (c == '\n') return buf;
            buf.push_back(c);
        }
    }

    // For `status`: read until a status event arrives (skipping any
    // stray state events that raced the request). Prints it as JSON.
    int printStatus(int fd) {
        while (auto line = readLine(fd)) {
            try {
                const auto ev = hyprdictate::parseEvent(*line);
                if (std::holds_alternative<hyprdictate::event::StatusReply>(ev)) {
                    // Round-trip through serialize() so the output
                    // stays canonical rather than reflecting the
                    // exact wire ordering we received.
                    std::cout << hyprdictate::serialize(ev).dump() << "\n";
                    return 0;
                }
                if (std::holds_alternative<hyprdictate::event::Error>(ev)) {
                    const auto& err = std::get<hyprdictate::event::Error>(ev);
                    std::cerr << "hyprdictate: daemon error: "
                              << err.message << "\n";
                    return 1;
                }
                // Ignore state broadcasts that arrive ahead of our reply.
            } catch (const hyprdictate::ProtocolError& e) {
                std::cerr << "hyprdictate: bad event from daemon: "
                          << e.what() << "\n";
                return 1;
            }
        }
        std::cerr << "hyprdictate: daemon closed connection before replying\n";
        return 1;
    }

}

int main(int argc, char** argv) {
    CLI::App app{"hyprdictate - CLI for the voice dictation daemon"};

    std::string socket_arg;
    app.add_option("-s,--socket", socket_arg,
                   "Path to the daemon's unix socket "
                   "(default: $XDG_RUNTIME_DIR/hyprdictate.sock)");

    bool show_version = false;
    app.add_flag("--version", show_version, "Print version and exit");

    // Each subcommand corresponds 1:1 with a Command variant. The
    // `start` / `stop` pair is not exposed on the CLI: those are
    // level-triggered messages the plugin will send, not typical
    // shell-invocation shapes. If a scripting use case appears, we
    // can wire them in later without breaking the daemon wire.
    auto* c_toggle    = app.add_subcommand("toggle",    "Start or stop dictation");
    auto* c_ptt_down  = app.add_subcommand("ptt_down",  "PTT: begin recording");
    auto* c_ptt_up    = app.add_subcommand("ptt_up",    "PTT: end recording");
    auto* c_cancel    = app.add_subcommand("cancel",    "Cancel any in-flight recording");
    auto* c_status    = app.add_subcommand("status",    "Print daemon state as JSON");
    auto* c_reload    = app.add_subcommand("reload",    "Reload config (arrives in M4)");
    app.require_subcommand(0, 1);

    CLI11_PARSE(app, argc, argv);

    if (show_version) {
        std::cout << "hyprdictate " << kVersion << "\n";
        return 0;
    }

    if (app.get_subcommands().empty()) {
        std::cerr << app.help();
        return 1;
    }

    std::optional<hyprdictate::Command> cmd;
    bool wants_reply = false;

    if (c_toggle->parsed())         cmd = hyprdictate::command::Toggle{};
    else if (c_ptt_down->parsed())  cmd = hyprdictate::command::PttDown{};
    else if (c_ptt_up->parsed())    cmd = hyprdictate::command::PttUp{};
    else if (c_cancel->parsed())    cmd = hyprdictate::command::Cancel{};
    else if (c_reload->parsed())    cmd = hyprdictate::command::Reload{};
    else if (c_status->parsed()) {
        cmd = hyprdictate::command::Status{};
        wants_reply = true;
    }

    if (!cmd) {
        std::cerr << "hyprdictate: no subcommand recognised\n";
        return 1;
    }

    const std::filesystem::path socket_path =
        !socket_arg.empty() ? std::filesystem::path{socket_arg}
                            : defaultSocketPath();

    const int fd = connectDaemon(socket_path);
    if (fd < 0) return 2;

    if (!sendCommand(fd, *cmd)) {
        ::close(fd);
        return 3;
    }

    int rc = 0;
    if (wants_reply)
        rc = printStatus(fd);

    ::close(fd);
    return rc;
}
