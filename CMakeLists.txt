cmake_minimum_required(VERSION 3.27)

project(hyprdictate
    DESCRIPTION "Voice dictation daemon and Hyprland integration"
    VERSION 0.1
    LANGUAGES CXX
)

# C++23 matches hyprwsmode's baseline and the plugin half will share
# code with the daemon via shared/. std::variant, std::format,
# std::span, and std::string_view all get used across the codebase, so
# the standard is set once at the top rather than per-target.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json so clangd works out of the box across
# every subproject without per-directory configuration.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Baseline warnings, matched to hyprwsmode's own build. The two
# suppressions are the ones the Hyprland codebase itself silences;
# turning them on here would drown daemon output in third-party noise
# from headers we don't own.
add_compile_options(
    -Wall
    -Wextra
    -Wno-unused-parameter
    -Wno-missing-field-initializers
)

# Milestone gating. M1 ships only the daemon and CLI; the plugin
# target is folded in at M2, where it needs Hyprland's headers on
# PKG_CONFIG_PATH. Keeping it off by default lets Nix build M1 in
# isolation without an unfulfilled hyprland pkg-config lookup.
option(HYPRDICTATE_BUILD_PLUGIN "Build the Hyprland compositor plugin" OFF)

include(cmake/deps.cmake)

add_subdirectory(shared)
add_subdirectory(daemon)
add_subdirectory(cli)
