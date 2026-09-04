#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

#include "config.hpp"
#include "transcription_engine.hpp"

namespace hyprdictate {

    class MoonshineEngine final : public TranscriptionEngine {
    public:
        MoonshineEngine(std::filesystem::path modelPath,
                        Config::ModelArch     modelArch,
                        int                   updateIntervalMs);
        ~MoonshineEngine() override;

        MoonshineEngine(const MoonshineEngine&)            = delete;
        MoonshineEngine& operator=(const MoonshineEngine&) = delete;

        void start(std::string     keyterms,
                   PartialCallback onPartial,
                   ErrorCallback   onError) override;
        void addAudio(std::span<const float> pcm) noexcept override;
        std::string finish() override;
        void cancel() noexcept override;

        const std::filesystem::path& modelPath() const noexcept override {
            return m_modelPath;
        }

    private:
        void stopWorker();
        std::string pullTranscript(std::uint32_t flags);
        void pollLoop(std::stop_token stopToken);
        void releaseStream() noexcept;
        void reportAsyncError(std::string message);

        std::filesystem::path m_modelPath;
        int                   m_updateIntervalMs;
        int32_t               m_transcriber = -1;
        std::atomic<int32_t>  m_stream{-1};
        std::atomic<int32_t>  m_audioError{0};

        std::mutex              m_lifecycleMutex;
        std::mutex              m_callbackMutex;
        std::condition_variable m_pollWakeup;
        std::jthread            m_pollWorker;
        PartialCallback         m_onPartial;
        ErrorCallback           m_onError;
        std::string             m_lastPartial;
    };

}
