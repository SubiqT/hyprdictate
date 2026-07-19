#define WLR_USE_UNSTABLE

#include <stdexcept>
#include <string>

#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include "globals.hpp"
#include "log.hpp"

// PLUGIN_API_VERSION returns the ABI tag baked into the Hyprland
// headers at build time. Do NOT change. Hyprland compares this
// against its own hash before running any plugin code; a mismatch
// prevents load.
APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    // Compare the Hyprland commit hash we compiled against to the one
    // currently running. If they diverge, the ABI can differ even
    // when the API tag matches, and any HyprlandAPI call becomes
    // undefined behaviour. Refuse to load rather than crash later.
    // Same pattern hyprwsmode uses.
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

    // Later M2 commits register dispatchers, config values, the
    // daemon socket client, the virtual keyboard for injection, and
    // the border-indicator listeners here. This skeleton commit only
    // proves the plugin loads and unloads cleanly.

    HyprlandAPI::addNotification(
        PHANDLE,
        "[hyprdictate] loaded",
        CHyprColor{0.2, 1.0, 0.2, 1.0},
        3000);

    hyprdictate::log::info("plugin loaded (skeleton)");

    return {"hyprdictate",
            "Voice dictation integration for Hyprland",
            "Subi",
            "0.1"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    hyprdictate::log::info("plugin unloaded");
}
