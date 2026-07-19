#pragma once

// Thin C++ RAII wrapper around whisper.cpp's C API.
//
// One WhisperEngine holds a resident whisper_context for the daemon's
// lifetime; construction loads the model, destruction whisper_free's
// it. transcribe() is safe to call repeatedly from the session
// thread, but not concurrently from multiple threads — the underlying
// whisper_context is not internally synchronised.

#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#include "config.hpp"

// Forward declare rather than pulling <whisper.h> into the header;
// callers don't need whisper.cpp's C types directly.
struct whisper_context;

namespace hyprdictate {

    struct WhisperError : std::runtime_error {
        using std::runtime_error::runtime_error;
    };

    class WhisperEngine {
    public:
        // Load a GGML model from disk. Throws WhisperError on failure
        // (missing file, unrecognised format, out of memory). The
        // config-derived params configure per-call whisper_full_params
        // for every subsequent transcribe(); only initial_prompt is
        // varied per call.
        WhisperEngine(const std::filesystem::path&  model_path,
                      const Config::WhisperParams&  params,
                      std::string                   language,
                      int                           threads);
        ~WhisperEngine();

        WhisperEngine(const WhisperEngine&)            = delete;
        WhisperEngine& operator=(const WhisperEngine&) = delete;
        WhisperEngine(WhisperEngine&&)                 = delete;
        WhisperEngine& operator=(WhisperEngine&&)      = delete;

        // Run inference on 16 kHz mono float32 PCM. Returns the
        // concatenation of every segment's text, whitespace-trimmed.
        // initial_prompt is applied through whisper's biasing hook;
        // callers pass an empty view for the plain unbiased case.
        std::string transcribe(std::span<const float> pcm,
                               std::string_view       initial_prompt = {});

        const std::filesystem::path& modelPath() const noexcept { return m_modelPath; }
        int                          threads()   const noexcept { return m_threads; }
        std::string_view             language()  const noexcept { return m_language; }

    private:
        std::filesystem::path  m_modelPath;
        Config::WhisperParams  m_params;
        std::string            m_language;
        int                    m_threads;
        whisper_context*       m_ctx = nullptr;
    };

}
