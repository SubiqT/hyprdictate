#define WLR_USE_UNSTABLE

#include <memory>
#include <stdexcept>
#include <string>

#include <hyprland/src/config/values/ConfigValues.hpp>
#include <hyprland/src/config/values/types/BoolValue.hpp>
#include <hyprland/src/config/values/types/ColorValue.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/managers/EventManager.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include "dispatcher.hpp"
#include "globals.hpp"
#include "hyprdictate/state.hpp"
#include "indicator.hpp"
#include "injector.hpp"
#include "log.hpp"
#include "socket_client.hpp"

namespace {

    // Track the Recording edge so the border override fires exactly
    // once per record cycle. Falling into Recording again without an
    // intervening Idle would apply the override twice; explicit edge
    // detection avoids that.
    void onDaemonState(hyprdictate::State s) {
        const auto prev = hyprdictate::g_plugin.daemonState;
        hyprdictate::g_plugin.daemonState = s;

        const bool wasRecording = (prev == hyprdictate::State::Recording);
        const bool nowRecording = (s == hyprdictate::State::Recording);

        if (!wasRecording && nowRecording && hyprdictate::g_plugin.indicator) {
            hyprdictate::g_plugin.indicator->startRecording(
                hyprdictate::g_plugin.targetWindow);
        } else if (wasRecording && !nowRecording && hyprdictate::g_plugin.indicator) {
            hyprdictate::g_plugin.indicator->stopRecording();
        }

        // Clear the captured window on any terminal transition so a
        // stale PHLWINDOWREF doesn't drift into the next recording.
        // The Recording→Transcribing edge keeps it so the injector
        // still has the target when the transcript arrives.
        if (s == hyprdictate::State::Idle ||
            s == hyprdictate::State::Error ||
            s == hyprdictate::State::Cancelled) {
            hyprdictate::g_plugin.targetWindow.reset();
        }

        // Fan out onto Hyprland's socket2 so the M3 Noctalia widget
        // and any other socket2 subscriber sees state changes. Format
        // matches design.md: `hyprdictate>>state,<value>`. Redundant
        // emits (same state twice) are already avoided upstream —
        // the daemon only publishes StateChanged on genuine
        // transitions — but the prev == s guard here keeps the wire
        // clean even if a future daemon rework replays state.
        if (prev != s && g_pEventManager) {
            g_pEventManager->postEvent(SHyprIPCEvent{
                .event = "hyprdictate",
                .data  = std::string{"state,"}
                       + std::string{hyprdictate::formatState(s)},
            });
        }

        hyprdictate::log::info("daemon state = {}",
                               hyprdictate::formatState(s));
    }

    void onDaemonTranscript(const std::string& text) {
        if (!hyprdictate::g_plugin.injector) {
            hyprdictate::log::warn(
                "transcript arrived but injector not initialised");
            return;
        }

        if (!hyprdictate::g_plugin.injector->startInject(
                hyprdictate::g_plugin.targetWindow, text)) {
            hyprdictate::log::warn(
                "transcript ({} chars) not injected — see prior warning",
                text.size());
        }
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

    // Register Hyprland config values before reloading so user
    // hyprland.conf values are honoured on first parse. Same order
    // hyprwsmode uses. Indicator owns its own config values.
    hyprdictate::g_plugin.indicator = std::make_unique<hyprdictate::Indicator>();
    hyprdictate::g_plugin.indicator->registerConfig();
    HyprlandAPI::reloadConfig();

    hyprdictate::g_plugin.injector  = std::make_unique<hyprdictate::Injector>();

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
    // one.
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
    // Explicit reset in dependency order so an in-flight injector
    // doesn't outlive the compositor's wl_event_loop, and the
    // socket client's event source tears down before Hyprland
    // reclaims the fd table.
    hyprdictate::g_plugin.indicator.reset();
    hyprdictate::g_plugin.injector.reset();
    hyprdictate::g_plugin.socket.reset();
    hyprdictate::log::info("plugin unloaded");
}
