#pragma once

#include <hyprland/src/plugins/PluginAPI.hpp>

// PHANDLE is the handle Hyprland gives us at PLUGIN_INIT. Required
// by every HyprlandAPI:: call. Stored inline so any translation unit
// in the plugin can reach it via #include "globals.hpp" without a
// separate globals.cpp.
inline HANDLE PHANDLE = nullptr;
