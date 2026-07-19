#pragma once

// Border-colour indicator for the recording target window.
//
// While recording, the target window's border adopts the colour
// configured via `plugin:hyprdictate:indicator_border_color` if
// `plugin:hyprdictate:indicator_border` is on. On stop / cancel /
// error, the previous border-colour gradient is restored.
//
// Known caveat: Hyprland's own updateWindowData path recomputes
// m_realBorderColor from window rules on focus changes, layout
// ticks, and workspace switches. The override survives most cases
// but a rapid focus change during recording can revert it. A more
// robust fix hooks setBorderColor via createFunctionHook; deferred
// until we see that flake in real use.

#include <hyprland/src/config/shared/complex/ComplexDataTypes.hpp>
#include <hyprland/src/config/values/ConfigValues.hpp>
#include <hyprland/src/config/values/types/BoolValue.hpp>
#include <hyprland/src/config/values/types/ColorValue.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/helpers/memory/Memory.hpp>

namespace hyprdictate {

    class Indicator {
    public:
        Indicator();
        ~Indicator() = default;

        Indicator(const Indicator&)            = delete;
        Indicator& operator=(const Indicator&) = delete;

        // Called at PLUGIN_INIT time. Registers the plugin's config
        // values with Hyprland and stashes the returned shared_ptrs
        // so startRecording can read them via ->value() without
        // going through HyprlandAPI::getConfigValue (whose raw-
        // pointer return type is not polymorphic and can't be
        // dynamic_cast to the typed value).
        void registerConfig();

        // Called on the Idle→Recording transition. If the plugin
        // config's `indicator_border` is enabled, snapshots the
        // current m_realBorderColor of `target` and swaps in the
        // configured colour. No-op if disabled or target expired.
        void startRecording(PHLWINDOWREF target);

        // Called on the Recording→(anything-else) transition. Puts
        // back whatever colour was snapshotted at start, so long as
        // the window still exists. Clears the saved state either
        // way so a subsequent start captures a fresh baseline.
        void stopRecording();

    private:
        SP<Config::Values::CBoolValue>  m_borderEnabled;
        SP<Config::Values::CColorValue> m_borderColor;

        PHLWINDOWREF                m_target;
        Config::CGradientValueData  m_savedBorder;
        bool                        m_saved = false;
    };

}
