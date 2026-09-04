#include "vocabulary.hpp"

namespace hyprdictate {

    namespace {

        // Moonshine defaults to a maximum of 200 contextual terms. Keep the
        // daemon-side list within that bound so an unexpectedly large config
        // cannot dilute decoder accuracy.
        constexpr std::size_t kMaxKeyterms = 200;

        void append(std::string& out, std::string_view term) {
            if (term.empty()) return;
            if (!out.empty()) out.push_back(',');
            out.append(term);
        }

    }

    std::string composeKeyterms(const Config::Vocabulary&           voc,
                                const std::optional<WindowContext>& /*window*/) {
        std::string keyterms;

        for (std::size_t i = 0; i < voc.global.size() && i < kMaxKeyterms; ++i)
            append(keyterms, voc.global[i]);

        // Per-class and title-derived keyterms can be layered here later.
        return keyterms;
    }

}
