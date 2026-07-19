#include "audio.hpp"

#include <cstdint>
#include <cstring>
#include <mutex>

#include <spdlog/spdlog.h>

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/audio/raw.h>
#include <spa/pod/builder.h>

namespace hyprdictate {

    namespace {

        // Wire up the process callback to the instance method via a
        // thin trampoline. Kept in a free function so the pw_stream
        // events table below can take its address without dealing with
        // C++ pointer-to-member complications.
        void audio_on_process(void* userdata) {
            audio_on_process_impl(*static_cast<AudioCapture*>(userdata));
        }

        void audio_on_state_changed(void* userdata,
                                    enum pw_stream_state /*old*/,
                                    enum pw_stream_state state,
                                    const char*          error) {
            if (error) {
                spdlog::warn("audio: stream state {}: {}",
                             pw_stream_state_as_string(state), error);
            } else {
                spdlog::debug("audio: stream state {}",
                              pw_stream_state_as_string(state));
            }
            (void)userdata;
        }

        void audio_on_param_changed(void*                userdata,
                                    uint32_t             id,
                                    const struct spa_pod* param) {
            (void)userdata;
            if (param == nullptr || id != SPA_PARAM_Format)
                return;

            uint32_t media_type    = 0;
            uint32_t media_subtype = 0;
            if (spa_format_parse(param, &media_type, &media_subtype) < 0)
                return;
            if (media_type    != SPA_MEDIA_TYPE_audio ||
                media_subtype != SPA_MEDIA_SUBTYPE_raw)
                return;

            spa_audio_info_raw info{};
            spa_format_audio_raw_parse(param, &info);
            spdlog::info("audio: negotiated format rate={} channels={} fmt={}",
                         info.rate, info.channels,
                         static_cast<int>(info.format));
        }

        constexpr pw_stream_events kStreamEvents = {
            .version       = PW_VERSION_STREAM_EVENTS,
            .state_changed = &audio_on_state_changed,
            .param_changed = &audio_on_param_changed,
            .process       = &audio_on_process,
        };

    } // namespace

    void audio_on_process_impl(AudioCapture& self) noexcept {
        pw_buffer* b = pw_stream_dequeue_buffer(self.m_stream);
        if (!b) {
            spdlog::warn("audio: out of buffers on capture stream");
            return;
        }

        spa_buffer* buf = b->buffer;
        // datas[0].data is only non-null after a successful buffer
        // map; if the graph returns an empty tick, requeue and move
        // on without touching m_pcm.
        if (buf->datas[0].data && buf->datas[0].chunk) {
            const auto* chunk = buf->datas[0].chunk;
            const auto  bytes = chunk->size;
            const auto  off   = chunk->offset;

            if (bytes > 0) {
                const auto* base   = static_cast<const std::uint8_t*>(buf->datas[0].data);
                const auto* frames = reinterpret_cast<const float*>(base + off);
                const auto  count  = bytes / sizeof(float);

                std::lock_guard<std::mutex> guard(self.m_bufMutex);
                self.m_pcm.insert(self.m_pcm.end(), frames, frames + count);
            }
        }

        pw_stream_queue_buffer(self.m_stream, b);
    }

    AudioCapture::AudioCapture() {
        // pw_init is refcounted internally; calling twice is harmless
        // and lets a future test binary bring the daemon up and down
        // repeatedly without leaking state.
        pw_init(nullptr, nullptr);

        m_loop = pw_thread_loop_new("hyprdictate-audio", nullptr);
        if (!m_loop)
            throw AudioError("pw_thread_loop_new failed");

        if (pw_thread_loop_start(m_loop) < 0) {
            pw_thread_loop_destroy(m_loop);
            m_loop = nullptr;
            throw AudioError("pw_thread_loop_start failed");
        }

        // Everything below touches PipeWire structures; take the loop
        // lock so the thread_loop's own runner doesn't concurrently
        // observe half-built state.
        pw_thread_loop_lock(m_loop);

        m_context = pw_context_new(pw_thread_loop_get_loop(m_loop), nullptr, 0);
        if (!m_context) {
            pw_thread_loop_unlock(m_loop);
            throw AudioError("pw_context_new failed");
        }

        m_core = pw_context_connect(m_context, nullptr, 0);
        if (!m_core) {
            pw_context_destroy(m_context);
            m_context = nullptr;
            pw_thread_loop_unlock(m_loop);
            throw AudioError("pw_context_connect failed (is PipeWire running?)");
        }

        pw_thread_loop_unlock(m_loop);

        spdlog::info("audio: PipeWire connected");
    }

    AudioCapture::~AudioCapture() {
        if (m_loop) {
            pw_thread_loop_lock(m_loop);
            tearDownStreamLocked();
            if (m_core) {
                pw_core_disconnect(m_core);
                m_core = nullptr;
            }
            if (m_context) {
                pw_context_destroy(m_context);
                m_context = nullptr;
            }
            pw_thread_loop_unlock(m_loop);

            pw_thread_loop_stop(m_loop);
            pw_thread_loop_destroy(m_loop);
            m_loop = nullptr;
        }

        pw_deinit();
    }

    bool AudioCapture::isCapturing() const noexcept {
        return m_capturing;
    }

    void AudioCapture::start() {
        if (m_capturing)
            return;

        pw_thread_loop_lock(m_loop);

        // Reset the PCM buffer under the same lock the process
        // callback would append under, so a start() racing a stale
        // process tick can't pick up leftovers from the previous run.
        {
            std::lock_guard<std::mutex> guard(m_bufMutex);
            m_pcm.clear();
        }

        pw_properties* props = pw_properties_new(
            PW_KEY_MEDIA_TYPE,     "Audio",
            PW_KEY_MEDIA_CATEGORY, "Capture",
            // "Communication" hints the PipeWire graph that this is a
            // voice-capture consumer; some session managers apply echo
            // cancellation to Communication streams. That matches how
            // users actually run dictation.
            PW_KEY_MEDIA_ROLE,     "Communication",
            PW_KEY_APP_NAME,       "hyprdictate",
            nullptr);

        m_stream = pw_stream_new(m_core, "hyprdictate-capture", props);
        if (!m_stream) {
            pw_thread_loop_unlock(m_loop);
            throw AudioError("pw_stream_new failed");
        }

        // pw_stream_new takes ownership of props.
        // The listener hook lives in m_streamHook; casting to
        // spa_hook* is safe because the buffer is aligned and sized
        // for the type (checked with a static_assert in the impl).
        static_assert(sizeof(spa_hook) <= sizeof(m_streamHook),
                      "m_streamHook buffer too small for spa_hook");
        auto* hook = reinterpret_cast<spa_hook*>(m_streamHook);
        std::memset(hook, 0, sizeof(spa_hook));
        pw_stream_add_listener(m_stream, hook, &kStreamEvents, this);

        // Build the format spec: F32 mono at whisper's native 16 kHz.
        // Setting rate and channels here (rather than leaving them
        // empty as the audio-capture example does) asks PipeWire's
        // audio adapter to resample from the source to what we need.
        uint8_t buffer[1024];
        spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));

        spa_audio_info_raw info{};
        info.format   = SPA_AUDIO_FORMAT_F32;
        info.rate     = static_cast<uint32_t>(kSampleRate);
        info.channels = 1;

        const spa_pod* params[1];
        params[0] = spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info);

        const auto flags = static_cast<pw_stream_flags>(
            PW_STREAM_FLAG_AUTOCONNECT |
            PW_STREAM_FLAG_MAP_BUFFERS |
            PW_STREAM_FLAG_RT_PROCESS);

        if (pw_stream_connect(m_stream, PW_DIRECTION_INPUT, PW_ID_ANY,
                              flags, params, 1) < 0) {
            pw_stream_destroy(m_stream);
            m_stream = nullptr;
            pw_thread_loop_unlock(m_loop);
            throw AudioError("pw_stream_connect failed");
        }

        m_capturing = true;
        pw_thread_loop_unlock(m_loop);

        spdlog::info("audio: capture started");
    }

    std::vector<float> AudioCapture::stop() {
        if (!m_capturing)
            return {};

        pw_thread_loop_lock(m_loop);
        tearDownStreamLocked();
        m_capturing = false;

        std::vector<float> out;
        {
            std::lock_guard<std::mutex> guard(m_bufMutex);
            out = std::move(m_pcm);
            m_pcm.clear();
        }
        pw_thread_loop_unlock(m_loop);

        spdlog::info("audio: capture stopped ({} samples, ~{:.2f}s)",
                     out.size(),
                     static_cast<double>(out.size()) / kSampleRate);
        return out;
    }

    void AudioCapture::cancel() {
        if (!m_capturing) {
            std::lock_guard<std::mutex> guard(m_bufMutex);
            m_pcm.clear();
            return;
        }

        pw_thread_loop_lock(m_loop);
        tearDownStreamLocked();
        m_capturing = false;
        {
            std::lock_guard<std::mutex> guard(m_bufMutex);
            m_pcm.clear();
        }
        pw_thread_loop_unlock(m_loop);

        spdlog::info("audio: capture cancelled");
    }

    void AudioCapture::tearDownStreamLocked() {
        if (m_stream) {
            pw_stream_disconnect(m_stream);
            pw_stream_destroy(m_stream);
            m_stream = nullptr;
        }
    }

}
