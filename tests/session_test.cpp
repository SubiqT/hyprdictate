#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "session.hpp"

namespace {

    using namespace hyprdictate;
    using namespace std::chrono_literals;

    struct FakeAudio final : AudioSource {
        void start(ChunkCallback callback) override {
            onChunk = std::move(callback);
            capturing = true;
        }
        void stop() override { capturing = false; onChunk = {}; }
        void cancel() override { cancelled = true; capturing = false; onChunk = {}; }
        bool isCapturing() const noexcept override { return capturing; }

        ChunkCallback onChunk;
        bool capturing = false;
        bool cancelled = false;
    };

    struct FakeEngine final : TranscriptionEngine {
        void start(std::string keytermsValue,
                   PartialCallback partial,
                   ErrorCallback error) override {
            keyterms = std::move(keytermsValue);
            onPartial = std::move(partial);
            onError = std::move(error);
            active = true;
        }

        void addAudio(std::span<const float> pcm) noexcept override {
            samples += pcm.size();
        }

        std::string finish() override {
            {
                std::lock_guard lock(mutex);
                finishStarted = true;
            }
            cv.notify_all();
            std::unique_lock lock(mutex);
            cv.wait(lock, [this] { return !blockFinish || allowFinish; });
            active = false;
            if (failFinish)
                throw TranscriptionError("synthetic failure");
            return finalText;
        }

        void cancel() noexcept override {
            active = false;
            cancelled = true;
            onPartial = {};
            onError = {};
        }

        const std::filesystem::path& modelPath() const noexcept override {
            return path;
        }

        void streamingError(const std::string& message) {
            if (onError) onError(message);
        }

        void partial(const std::string& text) {
            if (onPartial) onPartial(text);
        }

        void releaseFinish() {
            {
                std::lock_guard lock(mutex);
                allowFinish = true;
            }
            cv.notify_all();
        }

        bool waitForFinishStart() {
            std::unique_lock lock(mutex);
            return cv.wait_for(lock, 1s, [this] { return finishStarted; });
        }

        std::filesystem::path path{"/models/moonshine"};
        PartialCallback onPartial;
        ErrorCallback onError;
        std::string keyterms;
        std::string finalText{"hello world"};
        std::size_t samples = 0;
        std::atomic<bool> active{false};
        bool cancelled = false;
        bool blockFinish = false;
        bool allowFinish = false;
        bool finishStarted = false;
        bool failFinish = false;
        std::mutex mutex;
        std::condition_variable cv;
    };

    struct Harness {
        FakeAudio audio;
        FakeEngine engine;
        std::mutex mutex;
        std::vector<Event> events;
        std::vector<std::string> injections;
        Session session;

        Harness()
            : session(audio, engine,
                      [this](const Event& event) {
                          std::lock_guard lock(mutex);
                          events.push_back(event);
                      },
                      [this](const std::string& text,
                             const std::optional<WindowContext>&) {
                          std::lock_guard lock(mutex);
                          injections.push_back(text);
                      },
                      [](const std::optional<WindowContext>&) {
                          return std::string{"Hyprland,NixOS"};
                      })
        {}

        bool waitForState(State state) {
            for (int i = 0; i < 200; ++i) {
                if (session.state() == state) return true;
                std::this_thread::sleep_for(5ms);
            }
            return false;
        }

        std::vector<event::Transcript> transcripts() {
            std::lock_guard lock(mutex);
            std::vector<event::Transcript> result;
            for (const Event& event : events) {
                if (const auto* transcript = std::get_if<event::Transcript>(&event))
                    result.push_back(*transcript);
            }
            return result;
        }

        std::size_t injectionCount() {
            std::lock_guard lock(mutex);
            return injections.size();
        }

        bool hasError() {
            std::lock_guard lock(mutex);
            for (const Event& event : events) {
                if (std::holds_alternative<event::Error>(event)) return true;
            }
            return false;
        }
    };

    bool require(bool condition, const char* message) {
        if (!condition)
            std::cerr << "FAIL: " << message << '\n';
        return condition;
    }

    bool testPartialAndFinal() {
        Harness h;
        h.session.handle(command::Start{});
        if (!require(h.session.state() == State::Recording, "start enters recording") ||
            !require(h.engine.keyterms == "Hyprland,NixOS", "vocabulary becomes keyterms"))
            return false;

        h.engine.partial("hello wor");
        auto partials = h.transcripts();
        if (!require(partials.size() == 1, "one partial event") ||
            !require(!partials[0].final, "partial is not final") ||
            !require(h.injectionCount() == 0, "partial is never injected"))
            return false;

        h.session.handle(command::Stop{});
        if (!require(h.waitForState(State::Idle), "finalization returns idle"))
            return false;

        auto transcripts = h.transcripts();
        return require(transcripts.size() == 2, "partial plus final events") &&
               require(transcripts.back().final, "final event marked final") &&
               require(transcripts.back().text == "hello world", "final text") &&
               require(h.injectionCount() == 1, "final injected exactly once");
    }

    bool testCancelRecording() {
        Harness h;
        h.session.handle(command::Start{});
        h.engine.partial("discard me");
        h.session.handle(command::Cancel{});
        if (!require(h.session.state() == State::Idle, "recording cancel returns idle") ||
            !require(h.audio.cancelled, "recording cancel stops audio") ||
            !require(h.engine.cancelled, "recording cancel stops engine") ||
            !require(h.injectionCount() == 0, "cancel injects nothing"))
            return false;
        const auto transcripts = h.transcripts();
        return require(transcripts.size() == 1 && !transcripts[0].final,
                       "cancel emits no final transcript");
    }

    bool testCancelDuringFinalization() {
        Harness h;
        h.engine.blockFinish = true;
        h.session.handle(command::Start{});
        h.session.handle(command::Stop{});
        if (!require(h.engine.waitForFinishStart(), "finish worker started"))
            return false;
        h.session.handle(command::Cancel{});
        if (!require(h.session.state() == State::Cancelled,
                     "cancel marks in-flight finalization"))
            return false;
        h.engine.releaseFinish();
        return require(h.waitForState(State::Idle), "cancelled finish returns idle") &&
               require(h.transcripts().empty(), "cancelled finish emits no transcript") &&
               require(h.injectionCount() == 0, "cancelled finish injects nothing");
    }

    bool testStreamingFailure() {
        Harness h;
        h.session.handle(command::Start{});
        h.engine.streamingError("poll failed");
        return require(h.session.state() == State::Idle,
                       "streaming failure returns idle") &&
               require(h.audio.cancelled, "streaming failure stops audio") &&
               require(h.hasError(), "streaming failure emits structured error") &&
               require(h.injectionCount() == 0, "streaming failure injects nothing");
    }

    bool testFinalizationFailure() {
        Harness h;
        h.engine.failFinish = true;
        h.session.handle(command::Start{});
        h.session.handle(command::Stop{});
        return require(h.waitForState(State::Idle), "failure returns idle") &&
               require(h.hasError(), "failure emits structured error") &&
               require(h.injectionCount() == 0, "failure injects nothing");
    }

}

int main() {
    if (!testPartialAndFinal()) return 1;
    if (!testCancelRecording()) return 1;
    if (!testCancelDuringFinalization()) return 1;
    if (!testStreamingFailure()) return 1;
    if (!testFinalizationFailure()) return 1;
    return 0;
}
