#pragma once

// Typed mirror of the config schema laid out in design.md. Every
// section the daemon might one day consume is parsed here so that a
// user's forward-compatible config doesn't trip parse errors on an
// older daemon binary; fields the current milestone doesn't yet touch
// remain harmless data.

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace hyprdictate {

    struct Config {
        enum class InjectFocus  { Start, End };
        enum class InjectMethod { WlrKeyboard, Wtype };
        enum class ModelArch    { TinyStreaming, SmallStreaming, MediumStreaming };

        // Moonshine streaming models are directories containing the ORT model
        // components and tokenizer, rather than one GGML file.
        std::filesystem::path model_path;
        std::string           language           = "en";
        ModelArch             model_arch         = ModelArch::MediumStreaming;
        int                   update_interval_ms = 300;
        InjectFocus           inject_focus       = InjectFocus::Start;
        // Design doc default is wlr_keyboard, but that path only exists
        // once the M2 plugin is loaded. On the standalone daemon (M1),
        // main() falls back to Wtype at runtime and logs the divergence
        // rather than refusing to start; the config value is preserved
        // untouched so M2 doesn't need a config migration.
        InjectMethod          inject_method = InjectMethod::WlrKeyboard;

        struct Indicator {
            bool        border       = false;
            std::string border_color = "0xffff5555";
        } indicator;

        struct Vocabulary {
            std::vector<std::string> global;
            bool                     include_title_tokens = false;
            // per_class is deferred to M4; parsed but not stored here
            // until the map is actually consumed to avoid a dead field.
        } vocabulary;

        // Load a config from disk.
        //
        // Precedence:
        //   1. explicit_path if set (must exist).
        //   2. $XDG_CONFIG_HOME/hyprdictate/config.toml.
        //   3. ~/.config/hyprdictate/config.toml.
        //
        // The chosen path is written back into resolved_path when the
        // pointer is non-null. Missing model_path in the loaded config
        // throws ConfigError, since the daemon has nothing sensible to
        // fall back to for that field.
        static Config load(std::optional<std::filesystem::path> explicit_path,
                           std::filesystem::path*               resolved_path = nullptr);
    };

    struct ConfigError : std::runtime_error {
        using std::runtime_error::runtime_error;
    };

    // Expand a leading `~` in a config path against $HOME. Anything
    // else is passed through unchanged. Kept deliberately narrow: a
    // full shell-style expansion isn't worth the surface area for a
    // config file where users usually just want tilde expansion.
    std::filesystem::path expandPath(std::string_view p);

}
