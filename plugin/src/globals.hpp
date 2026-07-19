#pragma once

#include <memory>

#include <hyprland/src/plugins/PluginAPI.hpp>

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
    struct SPluginState {
        // Socket client to the daemon. Nullable: a plugin loaded
        // before the daemon starts still comes up, dispatchers just
        // surface the disconnect. See socket_client.hpp for the
        // reconnect story.
        std::unique_ptr<SocketClient> socket;
    };

    inline SPluginState g_plugin = {};

}
