#pragma once

// PipeWire audio capture for the daemon.
//
// The capture path is deliberately narrow: request F32 mono at whisper's
// native 16 kHz, let PipeWire's audio adapter resample from whichever
// default source the session graph offers, and accumulate float samples
// into an in-memory buffer for later transcription. Streaming and VAD
// belong to a later milestone; M1's toggle flow needs one buffer per
// utterance.
//
// The class owns a pw_thread_loop that runs on its own thread. Every
// call that touches PipeWire state takes the loop lock internally so
// state transitions from the daemon's main thread don't race the
// realtime process callback.

#include <mutex>
#include <stdexcept>
#include <vector>

// Forward declarations dodge the PipeWire header cascade in .hpp
// consumers; the .cpp includes the actual headers.
struct pw_thread_loop;
struct pw_context;
struct pw_core;
struct pw_stream;

namespace hyprdictate {

    struct AudioError : std::runtime_error {
        using std::runtime_error::runtime_error;
    };

    class AudioCapture {
    public:
        // Whisper's fixed sample rate. Public so callers computing
        // buffer sizes and utterance durations don't have to guess.
        static constexpr int kSampleRate = 16000;

        // Initialises PipeWire, spins up the capture thread_loop, and
        // connects a core to the session daemon. Throws AudioError
        // on any failure; the caller treats that as a fatal daemon-
        // start condition.
        AudioCapture();
        ~AudioCapture();

        AudioCapture(const AudioCapture&)            = delete;
        AudioCapture& operator=(const AudioCapture&) = delete;
        AudioCapture(AudioCapture&&)                 = delete;
        AudioCapture& operator=(AudioCapture&&)      = delete;

        // Begin recording. Clears any residual buffer and connects a
        // fresh stream. Idempotent: calling start() while already
        // recording is a no-op.
        void start();

        // Stop recording and return the accumulated PCM (float32,
        // mono, 16 kHz). Idempotent when already stopped: returns an
        // empty vector.
        std::vector<float> stop();

        // Stop recording and discard the buffer. Used by the cancel
        // command path where the caller doesn't want a transcript.
        void cancel();

        bool isCapturing() const noexcept;

    private:
        // Trampolines and the SPA event tables live entirely in the
        // .cpp file so this header stays free of PipeWire types.
        // These friend declarations let those file-local statics
        // reach the private members below.
        friend void audio_on_process_impl(class AudioCapture&) noexcept;
        friend void audio_on_state_changed_impl(class AudioCapture&) noexcept;

        void tearDownStreamLocked();

        pw_thread_loop* m_loop    = nullptr;
        pw_context*     m_context = nullptr;
        pw_core*        m_core    = nullptr;
        pw_stream*      m_stream  = nullptr;

        // spa_hook holds the listener node PipeWire threads onto the
        // stream's internal listener list. It has to outlive every
        // callback fire, so a member (not a stack local) is required.
        // Opaque byte storage keeps the header PipeWire-free; the
        // .cpp casts to spa_hook when calling pw_stream_add_listener.
        alignas(void*) unsigned char m_streamHook[64] = {};

        // PCM buffer guard: the process callback appends under the
        // same lock the main thread reads under. std::mutex fits the
        // toggle-flow's seconds-scale timing budget; a lock-free
        // ringbuffer would matter only if we streamed partials.
        mutable std::mutex m_bufMutex;
        std::vector<float> m_pcm;

        bool m_capturing = false;
    };

}
