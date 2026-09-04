#include "session.hpp"

#include <thread>
#include <type_traits>
#include <utility>

#include <spdlog/spdlog.h>

namespace hyprdictate {

    Session::Session(AudioSource&         audio,
                     TranscriptionEngine& transcription,
                     EventEmitter         emitter,
                     Injector             injector,
                     PromptSupplier       promptSupplier)
        : m_audio(audio)
        , m_transcription(transcription)
        , m_emitter(std::move(emitter))
        , m_injector(std::move(injector))
        , m_promptSupplier(std::move(promptSupplier))
    {}

    Session::~Session() {
        m_audio.cancel();
        if (m_finishWorker.joinable())
            m_finishWorker.join();
        m_transcription.cancel();
    }

    std::optional<Event> Session::handle(const Command& cmd) {
        return std::visit([this](auto&& command) -> std::optional<Event> {
            using T = std::decay_t<decltype(command)>;
            if constexpr (std::is_same_v<T, command::Toggle>) {
                return handleToggle(std::nullopt);
            } else if constexpr (std::is_same_v<T, command::Start>) {
                return handleStart(command.window);
            } else if constexpr (std::is_same_v<T, command::Stop>) {
                return handleStop();
            } else if constexpr (std::is_same_v<T, command::Cancel>) {
                return handleCancel();
            } else if constexpr (std::is_same_v<T, command::Status>) {
                return handleStatus();
            } else if constexpr (std::is_same_v<T, command::Reload>) {
                return handleReload();
            } else if constexpr (std::is_same_v<T, command::PttDown>) {
                return handleStart(command.window);
            } else if constexpr (std::is_same_v<T, command::PttUp>) {
                return handleStop();
            } else if constexpr (std::is_same_v<T, command::Identify>) {
                return handleIdentify(command.role);
            }
        }, cmd);
    }

    std::optional<Event> Session::handleToggle(
        const std::optional<WindowContext>& window) {
        std::lock_guard lock(m_mutex);
        switch (m_state.load(std::memory_order_acquire)) {
            case State::Idle:
                beginRecording(window);
                break;
            case State::Recording:
                startFinalization();
                break;
            case State::Transcribing:
            case State::Error:
            case State::Cancelled:
                spdlog::debug("toggle ignored in state {}",
                              formatState(m_state.load(std::memory_order_acquire)));
                break;
        }
        return std::nullopt;
    }

    std::optional<Event> Session::handleStart(
        const std::optional<WindowContext>& window) {
        std::lock_guard lock(m_mutex);
        if (m_state.load(std::memory_order_acquire) == State::Idle)
            beginRecording(window);
        else
            spdlog::debug("start ignored, already {}",
                          formatState(m_state.load(std::memory_order_acquire)));
        return std::nullopt;
    }

    std::optional<Event> Session::handleStop() {
        std::lock_guard lock(m_mutex);
        if (m_state.load(std::memory_order_acquire) == State::Recording)
            startFinalization();
        else
            spdlog::debug("stop ignored in state {}",
                          formatState(m_state.load(std::memory_order_acquire)));
        return std::nullopt;
    }

    std::optional<Event> Session::handleCancel() {
        std::unique_lock lock(m_mutex);
        const State current = m_state.load(std::memory_order_acquire);
        if (current == State::Idle)
            return std::nullopt;

        if (current == State::Recording) {
            m_audio.cancel();
            ++m_generation;
            m_window.reset();
            setState(State::Cancelled);
            lock.unlock();
            m_transcription.cancel();
            lock.lock();
            setState(State::Idle);
        } else if (current == State::Transcribing) {
            // finish() may already be inside Moonshine. Mark cancellation now;
            // completeTranscription drops its result and returns to Idle.
            setState(State::Cancelled);
        }
        return std::nullopt;
    }

    std::optional<Event> Session::handleStatus() {
        return event::StatusReply{
            .state      = m_state.load(std::memory_order_acquire),
            .model_path = m_transcription.modelPath().string(),
        };
    }

    std::optional<Event> Session::handleReload() {
        return event::Error{.message = "reload not implemented yet"};
    }

    std::optional<Event> Session::handleIdentify(const std::string& role) {
        if (!role.empty())
            spdlog::info("client identified as role={}", role);
        return std::nullopt;
    }

    void Session::beginRecording(const std::optional<WindowContext>& window) {
        m_window              = window;
        m_clientOwnsInjection = window.has_value();
        const std::uint64_t generation = ++m_generation;

        std::string keyterms;
        if (m_promptSupplier)
            keyterms = m_promptSupplier(m_window);

        try {
            m_transcription.start(
                std::move(keyterms),
                [this, generation](const std::string& text) {
                    emitPartial(generation, text);
                },
                [this, generation](const std::string& reason) {
                    emitStreamingError(generation, reason);
                });
            m_audio.start([this](std::span<const float> pcm) {
                m_transcription.addAudio(pcm);
            });
        } catch (...) {
            m_audio.cancel();
            m_transcription.cancel();
            m_window.reset();
            m_clientOwnsInjection = false;
            throw;
        }

        setState(State::Recording);
    }

    void Session::startFinalization() {
        m_audio.stop();
        setState(State::Transcribing);
        const std::uint64_t generation = m_generation;

        m_finishWorker = std::jthread([this, generation] {
            try {
                completeTranscription(generation, m_transcription.finish());
            } catch (const std::exception& e) {
                failTranscription(generation, e.what());
            }
        });
    }

    void Session::emitPartial(std::uint64_t generation, const std::string& text) {
        {
            std::lock_guard lock(m_mutex);
            const State current = m_state.load(std::memory_order_acquire);
            if (generation != m_generation ||
                (current != State::Recording && current != State::Transcribing)) {
                return;
            }
        }
        m_emitter(event::Transcript{.text = text, .final = false});
    }

    void Session::emitStreamingError(std::uint64_t generation,
                                     const std::string& reason) {
        {
            std::lock_guard lock(m_mutex);
            if (generation != m_generation ||
                m_state.load(std::memory_order_acquire) != State::Recording) {
                return;
            }

            // This callback runs on Moonshine's polling worker, so it cannot
            // call transcription.cancel() (that would join itself). Stopping
            // capture bounds the failed stream; MoonshineEngine::start or its
            // destructor reclaims that stale handle after the worker exits.
            m_audio.cancel();
            m_window.reset();
            setState(State::Error);
            setState(State::Idle);
        }
        spdlog::error("streaming transcription failed: {}", reason);
        m_emitter(event::Error{.message = "moonshine: " + reason});
    }

    void Session::completeTranscription(std::uint64_t generation, std::string text) {
        std::optional<WindowContext> window;
        bool clientOwns = false;
        {
            std::lock_guard lock(m_mutex);
            if (generation != m_generation)
                return;
            if (m_state.load(std::memory_order_acquire) == State::Cancelled) {
                m_window.reset();
                setState(State::Idle);
                return;
            }

            window     = m_window;
            clientOwns = m_clientOwnsInjection;
            m_window.reset();
            setState(State::Idle);
        }

        if (text.empty()) {
            spdlog::info("transcript empty; nothing to inject");
            return;
        }

        m_emitter(event::Transcript{.text = text, .final = true});
        if (clientOwns) {
            spdlog::info("wtype skipped: recording owned by client");
            return;
        }
        if (m_injector)
            m_injector(text, window);
    }

    void Session::failTranscription(std::uint64_t generation, std::string reason) {
        {
            std::lock_guard lock(m_mutex);
            if (generation != m_generation)
                return;
            m_window.reset();
            if (m_state.load(std::memory_order_acquire) == State::Cancelled) {
                setState(State::Idle);
                return;
            }
            setState(State::Error);
            setState(State::Idle);
        }
        spdlog::error("transcription failed: {}", reason);
        m_emitter(event::Error{.message = "moonshine: " + std::move(reason)});
    }

    void Session::setState(State state) {
        m_state.store(state, std::memory_order_release);
        if (state == State::Idle)
            m_clientOwnsInjection = false;
        emitStateEvent();
    }

    void Session::emitStateEvent() {
        m_emitter(event::StateChanged{
            .value = m_state.load(std::memory_order_acquire),
        });
    }

}
