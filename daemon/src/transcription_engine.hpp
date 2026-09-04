#pragma once

#include <filesystem>
#include <functional>
#include <span>
#include <stdexcept>
#include <string>

namespace hyprdictate {

    struct TranscriptionError : std::runtime_error {
        using std::runtime_error::runtime_error;
    };

    class TranscriptionEngine {
    public:
        using PartialCallback = std::function<void(const std::string&)>;
        using ErrorCallback   = std::function<void(const std::string&)>;

        virtual ~TranscriptionEngine() = default;

        // Start a fresh streaming utterance. Partial callbacks contain the
        // complete replaceable preview, not an append-only delta.
        virtual void start(std::string     keyterms,
                           PartialCallback onPartial,
                           ErrorCallback   onError) = 0;

        // Called from PipeWire's process callback. Implementations must keep
        // this path non-throwing and avoid running inference synchronously.
        virtual void addAudio(std::span<const float> pcm) noexcept = 0;

        // Drain the stream and return its final transcript. May block, so the
        // Session invokes it on its worker thread.
        virtual std::string finish() = 0;

        // Discard the active stream. Called only after audio capture has
        // stopped, so no further addAudio calls can race stream teardown.
        virtual void cancel() noexcept = 0;

        virtual const std::filesystem::path& modelPath() const noexcept = 0;
    };

}
