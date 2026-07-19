#include "session.hpp"

#include <thread>
#include <utility>

#include <spdlog/spdlog.h>

#include "audio.hpp"
#include "whisper_engine.hpp"

namespace hyprdictate {

    Session::Session(AudioCapture&    audio,
                     WhisperEngine&   whisper,
                     EventEmitter     emitter,
                     Injector         injector,
                     PromptSupplier   promptSupplier)
        : m_audio(audio)
        , m_whisper(whisper)
        , m_emitter(std::move(emitter))
        , m_injector(std::move(injector))
        , m_promptSupplier(std::move(promptSupplier))
    {}

    Session::~Session() = default;

    std::optional<Event> Session::handle(const Command& cmd) {
        return std::visit([this](auto&& c) -> std::optional<Event> {
            using T = std::decay_t<decltype(c)>;

            if constexpr (std::is_same_v<T, command::Toggle>) {
                return handleToggle(std::nullopt);
            } else if constexpr (std::is_same_v<T, command::Start>) {
                return handleStart(c.window);
            } else if constexpr (std::is_same_v<T, command::Stop>) {
                return handleStop();
            } else if constexpr (std::is_same_v<T, command::Cancel>) {
                return handleCancel();
            } else if constexpr (std::is_same_v<T, command::Status>) {
                return handleStatus();
            } else if constexpr (std::is_same_v<T, command::Reload>) {
                return handleReload();
            } else if constexpr (std::is_same_v<T, command::PttDown>) {
                // PTT lands in M4; treat as `start` for now so PTT-
                // configured keybinds don't break silently once the
                // dispatcher is registered client-side.
                return handleStart(c.window);
            } else if constexpr (std::is_same_v<T, command::PttUp>) {
                return handleStop();
            }
        }, cmd);
    }

    std::optional<Event> Session::handleToggle(const std::optional<WindowContext>& window) {
        std::lock_guard<std::mutex> guard(m_mutex);

        switch (m_state.load(std::memory_order_acquire)) {
            case State::Idle:
                m_window = window;
                m_audio.start();
                setState(State::Recording);
                return std::nullopt;

            case State::Recording: {
                auto pcm = m_audio.stop();
                setState(State::Transcribing);
                startTranscription(std::move(pcm));
                return std::nullopt;
            }

            case State::Transcribing:
            case State::Error:
            case State::Cancelled:
                // Toggle during transcribe / error / cancelled is a
                // no-op. The user will see the state settle back to
                // Idle in a beat and can toggle again then.
                spdlog::debug("toggle ignored in state {}",
                              formatState(m_state.load(std::memory_order_acquire)));
                return std::nullopt;
        }
        return std::nullopt;
    }

    std::optional<Event> Session::handleStart(const std::optional<WindowContext>& window) {
        std::lock_guard<std::mutex> guard(m_mutex);

        if (m_state.load(std::memory_order_acquire) != State::Idle) {
            spdlog::debug("start ignored, already {}",
                          formatState(m_state.load(std::memory_order_acquire)));
            return std::nullopt;
        }

        m_window = window;
        m_audio.start();
        setState(State::Recording);
        return std::nullopt;
    }

    std::optional<Event> Session::handleStop() {
        std::lock_guard<std::mutex> guard(m_mutex);

        if (m_state.load(std::memory_order_acquire) != State::Recording) {
            spdlog::debug("stop ignored in state {}",
                          formatState(m_state.load(std::memory_order_acquire)));
            return std::nullopt;
        }

        auto pcm = m_audio.stop();
        setState(State::Transcribing);
        startTranscription(std::move(pcm));
        return std::nullopt;
    }

    std::optional<Event> Session::handleCancel() {
        std::lock_guard<std::mutex> guard(m_mutex);

        const auto cur = m_state.load(std::memory_order_acquire);
        if (cur == State::Recording) {
            m_audio.cancel();
        } else if (cur == State::Idle) {
            // Nothing to cancel; a stray `cancel` command is common
            // from a keybind pressed at the wrong time and should
            // not surface an error.
            return std::nullopt;
        } else if (cur == State::Transcribing) {
            // Whisper doesn't expose a cancellation hook to abort
            // whisper_full mid-inference; the utterance completes
            // and its result is dropped by completeTranscription
            // when it sees state == Cancelled.
            setState(State::Cancelled);
            return std::nullopt;
        }

        m_window.reset();
        setState(State::Cancelled);

        // Auto-return to Idle so the widget's cancelled glow doesn't
        // stick. Doing this inline (rather than after a delay) keeps
        // the daemon single-threaded and predictable; the widget
        // handles the transient state visualisation.
        setState(State::Idle);
        return std::nullopt;
    }

    std::optional<Event> Session::handleStatus() {
        return event::StatusReply{
            .state      = m_state.load(std::memory_order_acquire),
            .model_path = m_whisper.modelPath().string(),
        };
    }

    std::optional<Event> Session::handleReload() {
        // Config reload arrives in M4; for M1 return an informative
        // error rather than silently doing nothing, so users who wire
        // this to a keybind can see the response in their logs.
        return event::Error{
            .message = "reload not implemented yet (arrives in M4)",
        };
    }

    void Session::startTranscription(std::vector<float> pcm) {
        emitStateEvent();  // Transcribing

        // Compose the initial_prompt on the current thread so the
        // supplier sees the same m_window value the caller just
        // captured. Passing an owned std::string into the worker
        // lambda avoids lifetime issues with the config's storage.
        std::string prompt;
        if (m_promptSupplier)
            prompt = m_promptSupplier(m_window);

        // Launch a detached worker per utterance. Whisper inference
        // is CPU-heavy (hundreds of milliseconds even for short
        // clips) and running it on the IPC thread would freeze
        // incoming state queries. A per-call thread is fine because
        // toggle-flow has one utterance in flight at a time, gated by
        // the state machine.
        std::thread([this, pcm = std::move(pcm), prompt = std::move(prompt)]() mutable {
            try {
                auto text = m_whisper.transcribe(pcm, prompt);
                completeTranscription(std::move(text));
            } catch (const std::exception& e) {
                failTranscription(std::string{"whisper: "} + e.what());
            }
        }).detach();
    }

    void Session::completeTranscription(std::string text) {
        std::optional<WindowContext> window;
        {
            std::lock_guard<std::mutex> guard(m_mutex);

            // If the user cancelled during transcribe, drop the text
            // on the floor and reset to Idle. The emitter already
            // published Cancelled from handleCancel.
            if (m_state.load(std::memory_order_acquire) == State::Cancelled) {
                m_window.reset();
                setState(State::Idle);
                return;
            }

            window = m_window;
            m_window.reset();
            setState(State::Idle);
        }

        if (text.empty()) {
            spdlog::info("transcript empty; nothing to inject");
            return;
        }

        m_emitter(event::Transcript{ .text = text });

        // Injection happens outside the mutex: it may block on a
        // subprocess spawn and shouldn't hold up incoming commands.
        if (m_injector)
            m_injector(text, window);
    }

    void Session::failTranscription(std::string reason) {
        {
            std::lock_guard<std::mutex> guard(m_mutex);
            m_window.reset();
            setState(State::Error);
            setState(State::Idle);
        }
        spdlog::error("transcription failed: {}", reason);
        m_emitter(event::Error{ .message = std::move(reason) });
    }

    void Session::setState(State s) {
        // Callers hold m_mutex; the atomic store publishes to
        // observers that read state() lock-free.
        m_state.store(s, std::memory_order_release);
        emitStateEvent();
    }

    void Session::emitStateEvent() {
        m_emitter(event::StateChanged{
            .value = m_state.load(std::memory_order_acquire),
        });
    }

}
