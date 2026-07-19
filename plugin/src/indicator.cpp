#include "indicator.hpp"

#include <hyprland/src/config/values/ConfigValues.hpp>
#include <hyprland/src/config/values/types/BoolValue.hpp>
#include <hyprland/src/config/values/types/ColorValue.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include "globals.hpp"
#include "log.hpp"

namespace hyprdictate {

    Indicator::Indicator() = default;

    void Indicator::registerConfig() {
        // Match hyprwsmode's config-registration pattern: keep the
        // shared_ptr returned by makeShared so we can read via
        // ->value() at runtime. HyprlandAPI::getConfigValue's raw-
        // pointer return isn't polymorphic and can't be safely
        // dynamic_cast to the typed value.
        m_borderEnabled = makeShared<Config::Values::CBoolValue>(
            "plugin:hyprdictate:indicator_border",
            "Highlight the recording target window's border while dictating",
            false);
        HyprlandAPI::addConfigValueV2(PHANDLE, m_borderEnabled);

        // ARGB integer; hyprland.conf accepts 0xAARRGGBB literals for
        // this type. Default is a muted red so a mis-configured
        // enabler doesn't disappear against typical themes.
        m_borderColor = makeShared<Config::Values::CColorValue>(
            "plugin:hyprdictate:indicator_border_color",
            "Border colour applied while dictating (0xAARRGGBB)",
            Config::INTEGER{0xffff5555ULL});
        HyprlandAPI::addConfigValueV2(PHANDLE, m_borderColor);
    }

    namespace {

        // Read the plugin's config values via HyprlandAPI. Returns
        // {enabled, colour} — colour is packed ARGB per Hyprland's
        // conventional 0xAARRGGBB form.
        struct SIndicatorConfig {
            bool     enabled = false;
            uint64_t argb    = 0xffff5555;
        };

    }

    void Indicator::startRecording(PHLWINDOWREF target) {
        SIndicatorConfig cfg;
        if (m_borderEnabled) cfg.enabled = m_borderEnabled->value();
        if (m_borderColor)   cfg.argb    = static_cast<uint64_t>(m_borderColor->value());

        if (!cfg.enabled)
            return;

        const auto window = target.lock();
        if (!window) {
            log::debug("indicator: target window expired at record-start");
            return;
        }

        // Snapshot the current border gradient so stopRecording can
        // put it back. Reads a copy — the assignment operator on
        // CGradientValueData deep-copies m_colors + m_colorsOkLabA.
        m_savedBorder = window->m_realBorderColor;
        m_saved       = true;
        m_target      = target;

        // Assign a solid-colour gradient in the configured hue. Warp
        // the animation progress so the change is instant rather
        // than fading through the previous colour, which would
        // muddle the visual signal.
        window->m_realBorderColorPrevious = window->m_realBorderColor;
        window->m_realBorderColor         = Config::CGradientValueData(CHyprColor{cfg.argb});
        if (window->m_borderFadeAnimationProgress) {
            window->m_borderFadeAnimationProgress->setValueAndWarp(1.f);
        }

        log::info("indicator: recording border applied on window '{}'",
                  window->m_class);
    }

    void Indicator::stopRecording() {
        if (!m_saved)
            return;

        const auto window = m_target.lock();
        if (window) {
            window->m_realBorderColorPrevious = window->m_realBorderColor;
            window->m_realBorderColor         = m_savedBorder;
            if (window->m_borderFadeAnimationProgress) {
                window->m_borderFadeAnimationProgress->setValueAndWarp(1.f);
            }
            log::info("indicator: recording border restored on window '{}'",
                      window->m_class);
        }

        m_target.reset();
        m_savedBorder = {};
        m_saved       = false;
    }

}
