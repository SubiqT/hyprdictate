#pragma once

// Wire protocol between the daemon and its clients (CLI in M1, plugin
// in M2, widget in M3). Line-delimited JSON on a unix stream socket
// (design.md, "IPC protocol"). The types below are the typed mirror
// of the wire schema; parseCommand / serialize sit at the JSON boundary
// so the rest of the codebase deals in std::variant, not raw json.

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

#include <nlohmann/json.hpp>

#include "hyprdictate/state.hpp"

namespace hyprdictate {

    // Focused-window context. Sent by the Hyprland plugin with `start`
    // so the daemon can compose per-class vocabulary into whisper's
    // initial_prompt. CLI clients omit this field; the daemon then
    // uses global vocabulary alone.
    //
    // `class` is a reserved C++ keyword, so the struct field is `cls`
    // and the JSON key remains `class` on the wire.
    struct WindowContext {
        std::string cls;
        std::string title;
    };

    // Commands flow from client to daemon. `start`/`stop` are the
    // level-triggered pair the plugin uses when it holds the wire
    // authority; `toggle` is the higher-level pair-of-both the CLI
    // uses. The daemon accepts both so a mixed setup (CLI + plugin)
    // works without a coordinator.
    namespace command {
        struct Toggle  {};
        struct Start   { std::optional<WindowContext> window; };
        struct Stop    {};
        struct Cancel  {};
        struct Status  {};
        struct Reload  {};
        // PTT lands in M4; the wire types live here so serialising a
        // future ptt_down/ptt_up client doesn't require a shared/
        // header bump.
        struct PttDown { std::optional<WindowContext> window; };
        struct PttUp   {};
    }

    using Command = std::variant<
        command::Toggle,
        command::Start,
        command::Stop,
        command::Cancel,
        command::Status,
        command::Reload,
        command::PttDown,
        command::PttUp
    >;

    // Events flow from daemon to subscribed clients. StateChanged is
    // broadcast; Transcript is broadcast (the plugin uses it, the CLI
    // ignores it); StatusReply is unicast to whoever asked for status;
    // Error carries structured failure to the requesting client.
    namespace event {
        struct StateChanged { State value; };
        struct Transcript   { std::string text; };
        struct StatusReply  {
            State                       state;
            std::optional<std::string>  model_path;
        };
        struct Error        { std::string message; };
    }

    using Event = std::variant<
        event::StateChanged,
        event::Transcript,
        event::StatusReply,
        event::Error
    >;

    // Thrown by parseCommand/parseEvent on malformed input. Callers
    // treat this as a protocol-layer failure and either return an
    // error event to the client (recoverable) or drop the connection
    // (unrecoverable), based on context.
    struct ProtocolError : std::runtime_error {
        using std::runtime_error::runtime_error;
    };

    nlohmann::json serialize(const Command& c);
    nlohmann::json serialize(const Event& e);

    Command parseCommand(const nlohmann::json& j);
    Command parseCommand(std::string_view line);
    Event   parseEvent(const nlohmann::json& j);
    Event   parseEvent(std::string_view line);

}
