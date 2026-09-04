#pragma once

#include <atomic>
#include <functional>
#include <span>
#include <stdexcept>

struct pw_thread_loop;
struct pw_context;
struct pw_core;
struct pw_stream;

namespace hyprdictate {

    struct AudioError : std::runtime_error {
        using std::runtime_error::runtime_error;
    };

    class AudioSource {
    public:
        using ChunkCallback = std::function<void(std::span<const float>)>;

        virtual ~AudioSource() = default;
        virtual void start(ChunkCallback onChunk) = 0;
        virtual void stop() = 0;
        virtual void cancel() = 0;
        virtual bool isCapturing() const noexcept = 0;
    };

    class AudioCapture final : public AudioSource {
    public:
        static constexpr int kSampleRate = 16000;

        AudioCapture();
        ~AudioCapture() override;

        AudioCapture(const AudioCapture&)            = delete;
        AudioCapture& operator=(const AudioCapture&) = delete;

        void start(ChunkCallback onChunk) override;
        void stop() override;
        void cancel() override;
        bool isCapturing() const noexcept override;

    private:
        friend void audio_on_process_impl(class AudioCapture&) noexcept;

        void tearDownStreamLocked();

        pw_thread_loop* m_loop    = nullptr;
        pw_context*     m_context = nullptr;
        pw_core*        m_core    = nullptr;
        pw_stream*      m_stream  = nullptr;

        alignas(void*) unsigned char m_streamHook[64] = {};

        // Set before the PipeWire stream connects and cleared only after the
        // loop lock has quiesced callbacks, so the process thread can invoke
        // it without taking a realtime-path mutex.
        ChunkCallback     m_onChunk;
        std::atomic<bool> m_capturing{false};
    };

}
