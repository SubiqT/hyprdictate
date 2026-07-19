#include "config.hpp"

#include <cstdlib>
#include <string>
#include <toml++/toml.hpp>

namespace hyprdictate {

    namespace fs = std::filesystem;

    fs::path expandPath(std::string_view p) {
        if (p.empty() || p.front() != '~')
            return fs::path{p};

        const char* home = std::getenv("HOME");
        if (!home || !*home)
            return fs::path{p};

        // Skip the leading '~'; the remainder is either empty, "/",
        // or "/rest". Concatenating with $HOME preserves that suffix.
        return fs::path{std::string{home} + std::string{p.substr(1)}};
    }

    namespace {

        Config::InjectFocus parseInjectFocus(std::string_view s) {
            if (s == "end")   return Config::InjectFocus::End;
            // Everything else (including "start" and typos) treated as
            // Start. Typos are caller error; logging that here would
            // require a logger dependency in a config module. The
            // daemon logs the effective value on load anyway.
            return Config::InjectFocus::Start;
        }

        Config::InjectMethod parseInjectMethod(std::string_view s) {
            if (s == "wtype") return Config::InjectMethod::Wtype;
            return Config::InjectMethod::WlrKeyboard;
        }

        fs::path resolveConfigPath(std::optional<fs::path> explicit_path) {
            if (explicit_path)
                return *explicit_path;

            if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
                return fs::path{xdg} / "hyprdictate" / "config.toml";

            if (const char* home = std::getenv("HOME"); home && *home)
                return fs::path{home} / ".config" / "hyprdictate" / "config.toml";

            throw ConfigError(
                "cannot resolve config path: neither XDG_CONFIG_HOME nor HOME is set");
        }

    }

    Config Config::load(std::optional<fs::path> explicit_path, fs::path* resolved) {
        const auto path = resolveConfigPath(explicit_path);

        if (!fs::exists(path)) {
            throw ConfigError(
                std::string{"config file not found: "} + path.string()
                + " (create one or pass --config)");
        }

        if (resolved) *resolved = path;

        toml::table tbl;
        try {
            tbl = toml::parse_file(path.string());
        } catch (const toml::parse_error& e) {
            // toml++ 3.4+ returns description() as std::string_view;
            // materialise it before concatenation so operator+ picks
            // the char-owning std::string path instead of the deleted
            // string+string_view overload.
            throw ConfigError(std::string{"config parse error at "} + path.string()
                              + ": " + std::string{e.description()});
        }

        Config c;

        if (auto v = tbl["model_path"].value<std::string>())
            c.model_path = expandPath(*v);

        if (auto v = tbl["language"].value<std::string>())
            c.language = *v;

        if (auto v = tbl["threads"].value<int64_t>())
            c.threads = static_cast<int>(*v);

        if (auto v = tbl["inject_focus"].value<std::string>())
            c.inject_focus = parseInjectFocus(*v);

        if (auto v = tbl["inject_method"].value<std::string>())
            c.inject_method = parseInjectMethod(*v);

        if (auto ind = tbl["indicator"].as_table()) {
            if (auto v = (*ind)["border"].value<bool>())
                c.indicator.border = *v;
            if (auto v = (*ind)["border_color"].value<std::string>())
                c.indicator.border_color = *v;
        }

        if (auto voc = tbl["vocabulary"].as_table()) {
            if (auto arr = (*voc)["global"].as_array()) {
                for (auto&& el : *arr) {
                    if (auto s = el.value<std::string>())
                        c.vocabulary.global.push_back(std::move(*s));
                }
            }
            if (auto v = (*voc)["include_title_tokens"].value<bool>())
                c.vocabulary.include_title_tokens = *v;
        }

        if (auto w = tbl["whisper"].as_table()) {
            if (auto v = (*w)["temperature"].value<double>())
                c.whisper.temperature = static_cast<float>(*v);
            if (auto v = (*w)["no_speech_thold"].value<double>())
                c.whisper.no_speech_thold = static_cast<float>(*v);
            if (auto v = (*w)["suppress_blank"].value<bool>())
                c.whisper.suppress_blank = *v;
            if (auto v = (*w)["suppress_non_speech_tokens"].value<bool>())
                c.whisper.suppress_non_speech_tokens = *v;
        }

        if (c.model_path.empty()) {
            throw ConfigError(
                std::string{"config at "} + path.string()
                + " is missing the required 'model_path' key");
        }

        return c;
    }

}
