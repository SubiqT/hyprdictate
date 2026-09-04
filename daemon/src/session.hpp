#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "audio.hpp"
#include "hyprdictate/protocol.hpp"
#include "hyprdictate/state.hpp"
#include "transcription_engine.hpp"

namespace hyprdictate {

    class Session {
    public:
        using EventEmitter = std::function<void(const Event&)>;
        using Injector = std::function<void(const std::string&,
                                            const std::optional<WindowContext>&)>;
        using PromptSupplier = std::function<std::string(const std::optional<WindowContext>&)>;

        Session(AudioSource&         audio,
                TranscriptionEngine& transcription,
                EventEmitter         emitter,
                Injector             injector,
                PromptSupplier       promptSupplier);
        ~Session();

        Session(const Session&)            = delete;
        Session& operator=(const Session&) = delete;

        std::optional<Event> handle(const Command& cmd);
        State state() const noexcept { return m_state.load(std::memory_order_acquire); }

    private:
        std::optional<Event> handleToggle(const std::optional<WindowContext>& window);
        std::optional<Event> handleStart(const std::optional<WindowContext>& window);
        std::optional<Event> handleStop();
        std::optional<Event> handleCancel();
        std::optional<Event> handleStatus();
        std::optional<Event> handleReload();
        std::optional<Event> handleIdentify(const std::string& role);

        void beginRecording(const std::optional<WindowContext>& window);
        void startFinalization();
        void completeTranscription(std::uint64_t generation, std::string text);
        void failTranscription(std::uint64_t generation, std::string reason);
        void emitPartial(std::uint64_t generation, const std::string& text);
        void emitStreamingError(std::uint64_t generation, const std::string& reason);

        void setState(State state);
        void emitStateEvent();

        AudioSource&         m_audio;
        TranscriptionEngine& m_transcription;
        EventEmitter         m_emitter;
        Injector             m_injector;
        PromptSupplier       m_promptSupplier;

        mutable std::mutex m_mutex;
        std::atomic<State> m_state{State::Idle};
        std::jthread       m_finishWorker;
        std::uint64_t      m_generation = 0;

        std::optional<WindowContext> m_window;
        bool                         m_clientOwnsInjection = false;
    };

}
