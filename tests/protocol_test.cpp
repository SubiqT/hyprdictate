#include <iostream>
#include <string>

#include "hyprdictate/protocol.hpp"

namespace {

    bool require(bool condition, const char* message) {
        if (!condition)
            std::cerr << "FAIL: " << message << '\n';
        return condition;
    }

}

int main() {
    using namespace hyprdictate;

    const Event partial = event::Transcript{.text = "hello wor", .final = false};
    const auto encoded = serialize(partial);
    if (!require(encoded["event"] == "transcript", "transcript event name") ||
        !require(encoded["text"] == "hello wor", "partial text") ||
        !require(encoded["final"] == false, "partial final marker")) {
        return 1;
    }

    const auto decoded = parseEvent(encoded.dump());
    const auto* transcript = std::get_if<event::Transcript>(&decoded);
    if (!require(transcript != nullptr, "partial round-trip type") ||
        !require(transcript->text == "hello wor", "partial round-trip text") ||
        !require(!transcript->final, "partial round-trip final marker")) {
        return 1;
    }

    // Older daemons did not send `final`; clients must continue treating
    // those events as final so mixed-version upgrades never suppress text.
    const auto legacy = parseEvent(R"({"event":"transcript","text":"done"})");
    const auto* legacyTranscript = std::get_if<event::Transcript>(&legacy);
    if (!require(legacyTranscript != nullptr, "legacy transcript type") ||
        !require(legacyTranscript->final, "legacy transcript defaults final")) {
        return 1;
    }

    return 0;
}
