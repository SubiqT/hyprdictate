cmake_minimum_required(VERSION 3.27)

project(hyprdictate
    DESCRIPTION "Voice dictation daemon and Hyprland integration"
    VERSION 0.1
    LANGUAGES CXX
)

# C++23 matches hyprwsmode's baseline and the plugin half shares
# code with the daemon via shared/. std::variant, std::format,
# std::span, and std::string_view all get used across the codebase,
# so the standard is set once at the top rather than per-target.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json so clangd works out of the box across
# every subproject without per-directory configuration.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Baseline warnings, matched to hyprwsmode's own build. The two
# suppressions are the ones the Hyprland codebase itself silences;
# turning them on here would drown daemon output in third-party
# noise from headers we don't own.
add_compile_options(
    -Wall
    -Wextra
    -Wno-unused-parameter
    -Wno-missing-field-initializers
)

# Per-target build gates. The three components have disjoint runtime
# deps (daemon needs PipeWire+whisper, CLI needs almost nothing,
# plugin needs Hyprland), so each Nix derivation flips one on and
# the others off to keep its closure minimal. Defaults preserve the
# existing single-command `cmake -B build` flow for developers who
# want everything built.
option(HYPRDICTATE_BUILD_DAEMON "Build the hyprdictated voice daemon" ON)
option(HYPRDICTATE_BUILD_CLI    "Build the hyprdictate CLI client"    ON)
option(HYPRDICTATE_BUILD_PLUGIN "Build the Hyprland compositor plugin" OFF)

# shared/ is always built: daemon, CLI, and plugin all link it. It's
# a small static archive so the always-on cost is negligible. Every
# third-party dep resolution lives with the target that links it
# (see the individual CMakeLists.txt files); the top level here has
# no per-dep responsibility beyond enumerating subdirectories.
add_subdirectory(shared)

if(HYPRDICTATE_BUILD_DAEMON)
    add_subdirectory(daemon)
endif()

if(HYPRDICTATE_BUILD_CLI)
    add_subdirectory(cli)
endif()

if(HYPRDICTATE_BUILD_PLUGIN)
    add_subdirectory(plugin)
endif()

# The systemd user unit is a daemon-side artefact; install it only
# when the daemon is being built. configure_file substitutes the
# absolute installed daemon path so `cmake --install` on non-Nix
# produces a unit non-Nix installers can activate without hand-
# editing ExecStart.
if(HYPRDICTATE_BUILD_DAEMON)
    include(GNUInstallDirs)
    set(HYPRDICTATE_DAEMON_PATH "${CMAKE_INSTALL_FULL_BINDIR}/hyprdictated")
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/contrib/systemd/hyprdictate.service.in
        ${CMAKE_CURRENT_BINARY_DIR}/contrib/systemd/hyprdictate.service
        @ONLY
    )
    install(
        FILES ${CMAKE_CURRENT_BINARY_DIR}/contrib/systemd/hyprdictate.service
        DESTINATION lib/systemd/user
    )
endif()
