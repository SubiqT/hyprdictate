#include "hyprdictate/state.hpp"

namespace hyprdictate {

    std::string_view formatState(State s) {
        switch (s) {
            case State::Idle:         return "idle";
            case State::Recording:    return "recording";
            case State::Transcribing: return "transcribing";
            case State::Error:        return "error";
            case State::Cancelled:    return "cancelled";
        }
        // Enum-covering switch above; falling through means a value was
        // cast in from outside the enum, which is a bug in the caller
        // rather than a wire-side possibility. Return a sentinel that
        // makes the mistake visible in logs and event payloads without
        // crashing.
        return "unknown";
    }

    std::optional<State> parseState(std::string_view s) {
        if (s == "idle")         return State::Idle;
        if (s == "recording")    return State::Recording;
        if (s == "transcribing") return State::Transcribing;
        if (s == "error")        return State::Error;
        if (s == "cancelled")    return State::Cancelled;
        return {};
    }

}
