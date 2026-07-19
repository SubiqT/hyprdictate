#pragma once

// Session coordinates the daemon's stateful pieces (audio capture,
// whisper inference, injection hook) around the wire commands and
// emits protocol events to subscribers.
//
// Threading model:
//   - Commands arrive from the IPC server, running on the asio
//     io_context thread. They mutate state under m_mutex and either
//     return synchronously (Status, Cancel), or hand off to a worker.
//   - Transcription runs on a detached std::thread per utterance.
//     Whisper inference is CPU-bound and would block the IPC loop
//     otherwise. On completion the worker calls back via
//     completeTranscription(), which reacquires the mutex, emits the
//     transcript event, and transitions back to Idle.
//   - The event emitter callback is expected to be thread-safe (the
//     IPC server implementation posts writes through its io_context).

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <string>

#include "hyprdictate/protocol.hpp"
#include "hyprdictate/state.hpp"

namespace hyprdictate {

    class AudioCapture;
    class WhisperEngine;

    class Session {
    public:
        using EventEmitter = std::function<void(const Event&)>;
        using Injector    = std::function<void(const std::string&, const std::optional<WindowContext>&)>;

        // audio and whisper are borrowed; the caller (main.cpp) owns
        // their lifetime and outlives Session. emitter is invoked
        // whenever the session broadcasts an event (state change,
        // transcript, error); injector receives the final transcript
        // and the recording's start-time window context.
        Session(AudioCapture&  audio,
                WhisperEngine& whisper,
                EventEmitter   emitter,
                Injector       injector);
        ~Session();

        Session(const Session&)            = delete;
        Session& operator=(const Session&) = delete;

        // Dispatch a wire command. Returns any per-command reply that
        // should be unicast to the requester (Status). Events that
        // fan out to every subscriber are pushed through the emitter
        // rather than returned here.
        std::optional<Event> handle(const Command& cmd);

        State state() const noexcept { return m_state.load(std::memory_order_acquire); }

    private:
        // Command handlers. All run under m_mutex.
        std::optional<Event> handleToggle(const std::optional<WindowContext>& window);
        std::optional<Event> handleStart(const std::optional<WindowContext>& window);
        std::optional<Event> handleStop();
        std::optional<Event> handleCancel();
        std::optional<Event> handleStatus();
        std::optional<Event> handleReload();

        // Kick off whisper on a worker thread with the current PCM
        // and window context. The worker calls completeTranscription
        // on the way out.
        void startTranscription(std::vector<float> pcm);
        void completeTranscription(std::string text);
        void failTranscription(std::string reason);

        void setState(State s);
        void emitStateEvent();

        AudioCapture&  m_audio;
        WhisperEngine& m_whisper;
        EventEmitter   m_emitter;
        Injector       m_injector;

        mutable std::mutex m_mutex;
        std::atomic<State> m_state{State::Idle};

        // Window context captured at start time. Preserved across the
        // recording so the design-doc's `inject_focus = "start"` path
        // uses the original window even if focus has drifted by the
        // time transcription completes.
        std::optional<WindowContext> m_window;
    };

}
