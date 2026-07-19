#include "whisper_engine.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <thread>

#include <spdlog/spdlog.h>
#include <whisper.h>

namespace hyprdictate {

    WhisperEngine::WhisperEngine(const std::filesystem::path& model_path,
                                 const Config::WhisperParams& params,
                                 std::string                  language,
                                 int                          threads)
        : m_modelPath(model_path)
        , m_params(params)
        , m_language(std::move(language))
        , m_threads(threads)
    {
        // Resolve "auto" (0) threads once at load time. Capping at 4 by
        // default matches whisper.cpp's own example CLI defaults and
        // stays inside a sensible cache-friendly range on desktop
        // hardware. Users needing more can crank `threads` in config.
        if (m_threads <= 0) {
            const unsigned hw = std::thread::hardware_concurrency();
            m_threads = static_cast<int>(std::min(hw == 0 ? 4u : hw, 4u));
        }

        auto cparams = whisper_context_default_params();
        // CPU inference for M1. GPU (CUDA/ROCm/Metal) is a whisper.cpp
        // build-time flag and requires the daemon's linked libwhisper
        // to have been compiled with GPU support; enabling this here
        // without checking would silently fail on a CPU-only build.
        // A config option lives in the deferred backlog.
        cparams.use_gpu = false;

        m_ctx = whisper_init_from_file_with_params(
            m_modelPath.string().c_str(), cparams);
        if (!m_ctx) {
            throw WhisperError(
                "whisper_init_from_file_with_params returned null for "
                + m_modelPath.string());
        }

        spdlog::info("whisper: loaded {} (threads={}, lang={})",
                     m_modelPath.string(), m_threads, m_language);
    }

    WhisperEngine::~WhisperEngine() {
        if (m_ctx)
            whisper_free(m_ctx);
    }

    std::string WhisperEngine::transcribe(std::span<const float> pcm,
                                          std::string_view       initial_prompt) {
        // A zero-length buffer produces zero segments; short-circuit
        // so callers don't need to guard.
        if (pcm.empty())
            return {};

        auto wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);

        wparams.print_realtime   = false;
        wparams.print_progress   = false;
        wparams.print_timestamps = false;
        wparams.print_special    = false;
        wparams.translate        = false;
        wparams.language         = m_language.c_str();
        wparams.detect_language  = false;
        wparams.n_threads        = m_threads;
        wparams.temperature      = m_params.temperature;
        wparams.no_speech_thold  = m_params.no_speech_thold;
        wparams.suppress_blank   = m_params.suppress_blank;
        wparams.suppress_nst     = m_params.suppress_non_speech_tokens;

        // whisper's initial_prompt is a const char*; the pointer must
        // stay valid across the whisper_full call. Copying into a
        // scope-local std::string ensures that: the caller can pass a
        // string_view over any storage without worrying about
        // lifetime, and the copy is one small allocation per
        // transcription (negligible compared to inference cost).
        std::string prompt_storage;
        if (!initial_prompt.empty()) {
            prompt_storage.assign(initial_prompt);
            wparams.initial_prompt = prompt_storage.c_str();
        }

        if (whisper_full(m_ctx, wparams, pcm.data(),
                         static_cast<int>(pcm.size())) != 0) {
            throw WhisperError("whisper_full failed");
        }

        std::string text;
        const int n = whisper_full_n_segments(m_ctx);
        for (int i = 0; i < n; ++i) {
            if (const char* seg = whisper_full_get_segment_text(m_ctx, i);
                seg && *seg) {
                text.append(seg);
            }
        }

        // Whisper prefixes each segment with a leading space; trim so
        // the wire-visible transcript doesn't carry it. Also strip a
        // trailing newline if whisper emits one at end-of-stream.
        auto is_ws = [](unsigned char ch) { return std::isspace(ch); };
        const auto first = std::find_if_not(text.begin(), text.end(), is_ws);
        const auto last  = std::find_if_not(text.rbegin(), text.rend(), is_ws).base();
        if (first < last)
            return std::string(first, last);
        return {};
    }

}
