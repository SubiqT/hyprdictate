#pragma once

// Contextual vocabulary for Moonshine's streaming decoder. Global terms are
// joined into the comma-separated keyterm syntax accepted by
// moonshine_transcriber_set_keyterms. The window argument keeps the seam for
// future per-class and title-derived terms.

#include <optional>
#include <string>

#include "config.hpp"
#include "hyprdictate/protocol.hpp"

namespace hyprdictate {

    std::string composeKeyterms(const Config::Vocabulary&           vocabulary,
                                const std::optional<WindowContext>& window);

}
