#pragma once

// hyprdictate's state machine values.
//
// One shared enum keeps three surfaces in agreement:
//
//   1. The daemon's internal state transitions.
//   2. The socket event {"event":"state","value":"..."} that CLIs,
//      plugins, and widgets subscribe to.
//   3. Hyprland's socket2 broadcast the plugin emits (`hyprdictate>>
//      state,<value>`), consumed by the Noctalia widget.
//
// The wire-facing strings are the labels formatState() returns; changing
// them is a wire-protocol break.

#include <optional>
#include <string_view>

namespace hyprdictate {

    enum class State {
        Idle,
        Recording,
        Transcribing,
        Error,
        Cancelled,
    };

    std::string_view     formatState(State s);
    std::optional<State> parseState(std::string_view s);

}
