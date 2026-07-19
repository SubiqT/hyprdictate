#define WLR_USE_UNSTABLE

#include <memory>
#include <stdexcept>
#include <string>

#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include "dispatcher.hpp"
#include "globals.hpp"
#include "hyprdictate/state.hpp"
#include "log.hpp"
#include "socket_client.hpp"

namespace {

    void onDaemonState(hyprdictate::State s) {
        hyprdictate::g_plugin.daemonState = s;

        // Clear the captured window on any terminal transition so a
        // stale PHLWINDOWREF doesn't drift into the next recording.
        // The Recording→Transcribing edge keeps it, since M2.5's
        // injector still needs to type into the same window when
        // the transcript arrives.
        if (s == hyprdictate::State::Idle ||
            s == hyprdictate::State::Error ||
            s == hyprdictate::State::Cancelled) {
            hyprdictate::g_plugin.targetWindow.reset();
        }

        // M2.7 fans this out onto Hyprland's socket2 so the M3 widget
        // can subscribe.
        hyprdictate::log::info("daemon state = {}",
                               hyprdictate::formatState(s));
    }

    void onDaemonTranscript(const std::string& text) {
        // M2.5 pipes this into the virtual-keyboard injector. Log
        // for now so the plumbing is observable end-to-end.
        hyprdictate::log::info("daemon transcript ({} chars): {}",
                               text.size(), text);
    }

    void onDaemonError(const std::string& msg) {
        hyprdictate::log::warn("daemon error: {}", msg);
    }

}

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    // Compare the Hyprland commit hash we compiled against to the one
    // currently running. If they diverge, the ABI can differ even
    // when the API tag matches, and any HyprlandAPI call becomes
    // undefined behaviour. Refuse to load rather than crash later.
    const std::string HASH        = __hyprland_api_get_hash();
    const std::string CLIENT_HASH = __hyprland_api_get_client_hash();
    if (HASH != CLIENT_HASH) {
        HyprlandAPI::addNotification(
            PHANDLE,
            "[hyprdictate] version mismatch with running Hyprland, refusing to load",
            CHyprColor{1.0, 0.2, 0.2, 1.0},
            5000);
        throw std::runtime_error("[hyprdictate] version mismatch");
    }

    // Bring up the daemon socket client. A connect failure is not
    // fatal — the plugin still loads, dispatchers still register,
    // they just report the disconnected state when the user
    // triggers one. This lets the compositor come up before the
    // daemon in scripted startups.
    hyprdictate::g_plugin.socket = std::make_unique<hyprdictate::SocketClient>(
        hyprdictate::SocketClient::Callbacks{
            .onState      = &onDaemonState,
            .onTranscript = &onDaemonTranscript,
            .onError      = &onDaemonError,
        });

    const bool connected = hyprdictate::g_plugin.socket->connect();

    // Register dispatchers regardless of connect success: users still
    // want `hyprctl dispatch hyprdictate:toggle` to give a
    // "daemon not connected" error rather than a "unknown dispatcher"
    // one, which is much less helpful.
    hyprdictate::registerDispatchers();

    if (connected) {
        HyprlandAPI::addNotification(
            PHANDLE,
            "[hyprdictate] loaded and connected to daemon",
            CHyprColor{0.2, 1.0, 0.2, 1.0},
            3000);
        hyprdictate::log::info("plugin loaded, daemon connection open");
    } else {
        HyprlandAPI::addNotification(
            PHANDLE,
            "[hyprdictate] loaded (daemon unreachable, dispatch to reconnect)",
            CHyprColor{1.0, 0.7, 0.2, 1.0},
            4000);
        hyprdictate::log::warn("plugin loaded but daemon connection failed");
    }

    return {"hyprdictate",
            "Voice dictation integration for Hyprland",
            "Subi",
            "0.1"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    // Explicit reset so the SocketClient destructor unregisters from
    // Hyprland's wl_event_loop before the compositor tears the loop
    // down; a late-teardown source removal would poke a dangling
    // event_loop pointer.
    hyprdictate::g_plugin.socket.reset();
    hyprdictate::log::info("plugin unloaded");
}
