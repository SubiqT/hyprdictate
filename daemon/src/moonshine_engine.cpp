#include "moonshine_engine.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <utility>

#include <moonshine-c-api.h>
#include <spdlog/spdlog.h>

namespace hyprdictate {

    namespace {

        uint32_t toMoonshineArch(Config::ModelArch arch) {
            switch (arch) {
                case Config::ModelArch::TinyStreaming:
                    return MOONSHINE_MODEL_ARCH_TINY_STREAMING;
                case Config::ModelArch::SmallStreaming:
                    return MOONSHINE_MODEL_ARCH_SMALL_STREAMING;
                case Config::ModelArch::MediumStreaming:
                    return MOONSHINE_MODEL_ARCH_MEDIUM_STREAMING;
            }
            return MOONSHINE_MODEL_ARCH_MEDIUM_STREAMING;
        }

        std::string errorMessage(int32_t code) {
            if (const char* message = moonshine_error_to_string(code); message && *message)
                return message;
            return "Moonshine error " + std::to_string(code);
        }

        void checkResult(int32_t result, std::string_view operation) {
            if (result < 0)
                throw TranscriptionError(std::string{operation} + ": " + errorMessage(result));
        }

        std::string trim(std::string text) {
            const auto isWhitespace = [](unsigned char ch) { return std::isspace(ch); };
            const auto first = std::find_if_not(text.begin(), text.end(), isWhitespace);
            const auto last  = std::find_if_not(text.rbegin(), text.rend(), isWhitespace).base();
            if (first >= last)
                return {};
            return std::string(first, last);
        }

        std::string flattenTranscript(const transcript_t* transcript) {
            if (!transcript)
                return {};

            std::string text;
            for (std::uint64_t i = 0; i < transcript->line_count; ++i) {
                const char* line = transcript->lines[i].text;
                if (!line || !*line)
                    continue;
                if (!text.empty() && !std::isspace(static_cast<unsigned char>(text.back())) &&
                    !std::isspace(static_cast<unsigned char>(*line))) {
                    text.push_back(' ');
                }
                text.append(line);
            }
            return trim(std::move(text));
        }

    }

    MoonshineEngine::MoonshineEngine(std::filesystem::path modelPath,
                                     Config::ModelArch     modelArch,
                                     int                   updateIntervalMs)
        : m_modelPath(std::move(modelPath))
        , m_updateIntervalMs(updateIntervalMs)
    {
        const moonshine_option_t options[] = {
            {.name = "decode_incomplete_lines", .value = "true"},
        };
        m_transcriber = moonshine_load_transcriber_from_files(
            m_modelPath.c_str(),
            toMoonshineArch(modelArch),
            options,
            std::size(options),
            MOONSHINE_HEADER_VERSION);
        checkResult(m_transcriber, "load transcriber");

        spdlog::info("moonshine: loaded {} (library={}, update={}ms)",
                     m_modelPath.string(), moonshine_get_version(), m_updateIntervalMs);
    }

    MoonshineEngine::~MoonshineEngine() {
        cancel();
        if (m_transcriber >= 0) {
            moonshine_free_transcriber(m_transcriber);
            m_transcriber = -1;
        }
    }

    void MoonshineEngine::start(std::string     keyterms,
                                PartialCallback onPartial,
                                ErrorCallback   onError) {
        stopWorker();
        std::lock_guard lock(m_lifecycleMutex);
        const int32_t staleStream = m_stream.load(std::memory_order_acquire);
        if (staleStream >= 0) {
            spdlog::warn("moonshine: replacing stale stream {}", staleStream);
            releaseStream();
        }

        checkResult(moonshine_transcriber_set_keyterms(
                        m_transcriber, keyterms.empty() ? nullptr : keyterms.c_str()),
                    "set keyterms");

        {
            std::lock_guard callbackLock(m_callbackMutex);
            m_onPartial  = std::move(onPartial);
            m_onError    = std::move(onError);
            m_lastPartial.clear();
        }
        m_audioError.store(0, std::memory_order_release);

        const int32_t stream = moonshine_create_stream(m_transcriber, 0);
        checkResult(stream, "create stream");
        m_stream.store(stream, std::memory_order_release);

        try {
            checkResult(moonshine_start_stream(m_transcriber, stream), "start stream");
            m_pollWorker = std::jthread([this](std::stop_token token) {
                pollLoop(token);
            });
        } catch (...) {
            releaseStream();
            throw;
        }
    }

    void MoonshineEngine::addAudio(std::span<const float> pcm) noexcept {
        const int32_t stream = m_stream.load(std::memory_order_acquire);
        if (stream < 0 || pcm.empty())
            return;

        const int32_t result = moonshine_transcribe_add_audio_to_stream(
            m_transcriber, stream, pcm.data(), pcm.size(), 16000, 0);
        if (result < 0) {
            int32_t expected = 0;
            if (m_audioError.compare_exchange_strong(expected, result,
                                                     std::memory_order_acq_rel)) {
                m_pollWakeup.notify_all();
            }
        }
    }

    std::string MoonshineEngine::finish() {
        stopWorker();
        std::lock_guard lock(m_lifecycleMutex);
        const int32_t stream = m_stream.load(std::memory_order_acquire);
        if (stream < 0)
            return {};

        const int32_t audioError = m_audioError.exchange(0, std::memory_order_acq_rel);
        if (audioError < 0) {
            releaseStream();
            throw TranscriptionError("add audio: " + errorMessage(audioError));
        }

        try {
            checkResult(moonshine_stop_stream(m_transcriber, stream), "stop stream");
            std::string finalText = pullTranscript(0);
            releaseStream();
            {
                std::lock_guard callbackLock(m_callbackMutex);
                m_onPartial = {};
                m_onError   = {};
                m_lastPartial.clear();
            }
            return finalText;
        } catch (...) {
            releaseStream();
            throw;
        }
    }

    void MoonshineEngine::cancel() noexcept {
        stopWorker();
        std::lock_guard lock(m_lifecycleMutex);
        releaseStream();
        m_audioError.store(0, std::memory_order_release);
        std::lock_guard callbackLock(m_callbackMutex);
        m_onPartial = {};
        m_onError   = {};
        m_lastPartial.clear();
    }

    void MoonshineEngine::stopWorker() {
        if (!m_pollWorker.joinable())
            return;
        m_pollWorker.request_stop();
        m_pollWakeup.notify_all();
        m_pollWorker.join();
    }

    std::string MoonshineEngine::pullTranscript(std::uint32_t flags) {
        const int32_t stream = m_stream.load(std::memory_order_acquire);
        if (stream < 0)
            return {};

        transcript_t* transcript = nullptr;
        checkResult(moonshine_transcribe_stream(m_transcriber, stream, flags, &transcript),
                    "transcribe stream");
        return flattenTranscript(transcript);
    }

    void MoonshineEngine::pollLoop(std::stop_token stopToken) {
        std::mutex waitMutex;
        std::unique_lock waitLock(waitMutex);
        while (!stopToken.stop_requested()) {
            m_pollWakeup.wait_for(waitLock,
                                  std::chrono::milliseconds(m_updateIntervalMs));
            if (stopToken.stop_requested())
                return;

            if (const int32_t audioError = m_audioError.load(std::memory_order_acquire);
                audioError < 0) {
                reportAsyncError("add audio: " + errorMessage(audioError));
                return;
            }

            try {
                std::string text = pullTranscript(0);
                PartialCallback callback;
                {
                    std::lock_guard callbackLock(m_callbackMutex);
                    if (text.empty() || text == m_lastPartial)
                        continue;
                    m_lastPartial = text;
                    callback = m_onPartial;
                }
                if (callback)
                    callback(text);
            } catch (const std::exception& e) {
                reportAsyncError(e.what());
                return;
            }
        }
    }

    void MoonshineEngine::releaseStream() noexcept {
        const int32_t stream = m_stream.exchange(-1, std::memory_order_acq_rel);
        if (stream >= 0) {
            const int32_t result = moonshine_free_stream(m_transcriber, stream);
            if (result < 0)
                spdlog::warn("moonshine: free stream failed: {}", errorMessage(result));
        }
    }

    void MoonshineEngine::reportAsyncError(std::string message) {
        ErrorCallback callback;
        {
            std::lock_guard callbackLock(m_callbackMutex);
            callback = m_onError;
        }
        if (callback)
            callback(std::move(message));
    }

}
