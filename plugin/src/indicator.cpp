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

    namespace {

        // Read the plugin's config values via HyprlandAPI. Returns
        // {enabled, colour} — colour is packed ARGB per Hyprland's
        // conventional 0xAARRGGBB form.
        struct SIndicatorConfig {
            bool     enabled = false;
            uint64_t argb    = 0xffff5555;
        };

        SIndicatorConfig readConfig() {
            SIndicatorConfig out;

            // HyprlandAPI::getConfigValue returns a shared_ptr to the
            // registered SConfigValue. The plugin registered these in
            // PLUGIN_INIT (see main.cpp).
            auto pEnabled = HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprdictate:indicator_border");
            auto pColor   = HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprdictate:indicator_border_color");

            if (pEnabled) {
                if (auto* bv = dynamic_cast<Config::Values::CBoolValue*>(pEnabled.get()))
                    out.enabled = bv->value();
            }
            if (pColor) {
                if (auto* cv = dynamic_cast<Config::Values::CColorValue*>(pColor.get()))
                    out.argb = static_cast<uint64_t>(cv->value());
            }

            return out;
        }

    }

    void Indicator::startRecording(PHLWINDOWREF target) {
        const auto cfg = readConfig();
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
