#include "vocabulary.hpp"

namespace hyprdictate {

    namespace {

        // Whisper's practical n_max_text_ctx for base-family models is
        // around 224 tokens; leaving headroom for the initial padding
        // and the segment being decoded, ~200 tokens is a safe cap.
        // A crude 4-chars-per-token estimate gives ~800 characters.
        constexpr std::size_t kMaxPromptChars = 800;

        // Append one vocabulary term to the running prompt, separated
        // from the previous term by a comma+space. Returns true when
        // the term fit; false means the cap was reached and callers
        // should stop appending.
        bool tryAppend(std::string& out, std::string_view term) {
            if (term.empty()) return true;

            // "Term, " overhead — 2 extra chars unless this is the
            // first entry. The cap is a soft ceiling; a slightly-
            // over-budget prompt is still healthier for whisper than
            // a mid-term truncation.
            const std::size_t joiner = out.empty() ? 0 : 2;
            if (out.size() + joiner + term.size() > kMaxPromptChars)
                return false;

            if (!out.empty())
                out.append(", ");
            out.append(term);
            return true;
        }

    }

    std::string composePrompt(const Config::Vocabulary&           voc,
                              const std::optional<WindowContext>& /*window*/) {
        std::string prompt;

        // Layer 1: global vocabulary.
        for (const auto& term : voc.global) {
            if (!tryAppend(prompt, term))
                break;
        }

        // Layer 2 (per_class) and Layer 3 (title tokens) land in M4.
        // The window context parameter is threaded through today so
        // the caller in session.cpp doesn't need to change when those
        // layers arrive.

        return prompt;
    }

}
