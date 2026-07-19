#pragma once

// Vocabulary composition for whisper's initial_prompt.
//
// The design doc's vocabulary section defines three layers:
//   1. global      — always included
//   2. per_class   — appended when the focused window class matches
//   3. title tokens — parsed from the current window title
//
// M1 implements layer 1 only; per_class and title tokens land in M4.
// The composer function stays layered so later commits can add the
// later layers without touching the caller in session.cpp.

#include <optional>
#include <string>

#include "config.hpp"
#include "hyprdictate/protocol.hpp"

namespace hyprdictate {

    // Build an initial_prompt string from the configured vocabulary
    // and the optional focused-window context.
    //
    // The output is capped at a conservative token budget (~200 tokens
    // ≈ 800 chars) so a large vocabulary doesn't crowd whisper's own
    // n_max_text_ctx window. Excess entries at the end are dropped.
    std::string composePrompt(const Config::Vocabulary&           voc,
                              const std::optional<WindowContext>& window);

}
