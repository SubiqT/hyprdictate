#pragma once

#include <memory>

#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include "hyprdictate/state.hpp"
#include "injector.hpp"
#include "socket_client.hpp"

// PHANDLE is the handle Hyprland gives us at PLUGIN_INIT. Required
// by every HyprlandAPI:: call. Stored inline so any translation
// unit in the plugin can reach it via #include "globals.hpp".
inline HANDLE PHANDLE = nullptr;

namespace hyprdictate {

    // Plugin-wide state, mirroring hyprwsmode's g_config pattern.
    // Members are constructed at PLUGIN_INIT time by main.cpp and
    // destroyed at PLUGIN_EXIT so their lifetime brackets every
    // callback fired on Hyprland's event loop.
    //
    // Everything here is single-threaded: every touch happens on the
    // compositor's main thread (dispatcher fires, socket_client
    // callbacks fired by wl_event_loop). No mutexes required.
    struct SPluginState {
        // Socket client to the daemon. Nullable: a plugin loaded
        // before the daemon starts still comes up, dispatchers just
        // surface the disconnect.
        std::unique_ptr<SocketClient> socket;

        // Mirror of the daemon's state, updated on every state
        // event. Local mirror lets dispatchers make edge-vs-level
        // decisions (toggle → start or stop depending on state)
        // without a round-trip to the daemon.
        State daemonState = State::Idle;

        // Weak reference to the window focused at record-start time.
        // Injection (M2.5) locks this and types into the underlying
        // surface even if focus has since drifted. Cleared when the
        // recording completes or cancels.
        PHLWINDOWREF targetWindow;

        // Deterministic-target injector. Owned by the plugin state
        // so its lifetime is tied to PLUGIN_INIT/EXIT and any
        // in-flight wtype gets detached rather than orphaned.
        std::unique_ptr<Injector> injector;
    };

    inline SPluginState g_plugin = {};

}
