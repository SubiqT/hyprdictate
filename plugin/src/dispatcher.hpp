#pragma once

namespace hyprdictate {

    // Register the plugin's Hyprland dispatchers.
    //
    // Two registration paths per dispatcher, mirroring hyprwsmode's
    // approach:
    //
    //   1. HyprlandAPI::addDispatcherV2 for classic keybind syntax,
    //      e.g. `bind = SUPER, H, hyprdictate, toggle`.
    //   2. HyprlandAPI::addLuaFunction for the hl.plugin.hyprdictate.<fn>
    //      namespace exposed to hyprland.lua and reachable via
    //      `hyprctl dispatch 'hl.plugin.hyprdictate.toggle()'`.
    //
    // Registered dispatchers (see design.md's "Toggle dictation" and
    // "Push-to-talk"):
    //
    //   hyprdictate toggle
    //   hyprdictate ptt_down
    //   hyprdictate ptt_up
    //   hyprdictate cancel
    //
    // Each dispatcher sends the matching Command variant to the
    // daemon via g_plugin.socket. Failure paths (daemon disconnected,
    // wire error) return a non-success SDispatchResult so hyprctl
    // dispatch reports the reason.
    void registerDispatchers();

}
