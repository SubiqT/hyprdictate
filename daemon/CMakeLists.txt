# hyprdictated: the voice-dictation daemon.
#
# Third-party deps are resolved here rather than at the top level so
# a component-restricted build (nix build .#cli, .#plugin) doesn't
# hit find_package calls for libs it will never link. Every dep uses
# the find_package → FetchContent fallback pattern so Nix and
# non-Nix consumers both build without network access at build time.
#
# Current deps:
# - CLI11 for arg parsing
# - toml++ for config
# - spdlog for logging
# - hyprdictate::asio for the socket server
# - hyprdictate::shared for the wire protocol types
# - Moonshine Voice for streaming inference
# - libpipewire-0.3 for audio capture

find_package(PkgConfig REQUIRED)

# --- toml++ ---
#
# 3.4.0 is the first release that exposes toml::parse_file with a
# std::filesystem::path overload, letting the daemon consume the
# XDG-resolved path directly.
find_package(tomlplusplus 3.3 QUIET CONFIG)
if(NOT tomlplusplus_FOUND)
    include(FetchContent)
    message(STATUS "hyprdictate/daemon: tomlplusplus not found, fetching")
    FetchContent_Declare(
        hyprdictate_tomlplusplus
        GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
        GIT_TAG        v3.4.0
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(hyprdictate_tomlplusplus)
endif()

# --- spdlog ---
#
# 1.13.0 gets us std::format-backed formatters and drops the older
# fmt-headers-only workaround. The plugin uses a stderr shim to
# match hyprwsmode; only the daemon links spdlog.
find_package(spdlog 1.12 QUIET CONFIG)
if(NOT spdlog_FOUND)
    include(FetchContent)
    message(STATUS "hyprdictate/daemon: spdlog not found, fetching")
    FetchContent_Declare(
        hyprdictate_spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG        v1.13.0
        GIT_SHALLOW    TRUE
    )
    set(SPDLOG_BUILD_EXAMPLE OFF CACHE INTERNAL "")
    set(SPDLOG_BUILD_TESTS   OFF CACHE INTERNAL "")
    FetchContent_MakeAvailable(hyprdictate_spdlog)
endif()

# --- CLI11 ---
#
# CLI11 is single-header; find_package resolves the standard
# CLI11::CLI11 target when nixpkgs' cli11 is on the prefix path.
# FetchContent falls back to a git tag rather than vendoring the
# header so bumping the pin stays a one-line change. cli/ resolves
# CLI11 independently so a component-restricted CLI-only build
# doesn't pull daemon-side deps.
find_package(CLI11 2.4 QUIET CONFIG)
if(NOT CLI11_FOUND)
    include(FetchContent)
    message(STATUS "hyprdictate/daemon: CLI11 not found, fetching")
    FetchContent_Declare(
        hyprdictate_cli11
        GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
        GIT_TAG        v2.4.2
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(hyprdictate_cli11)
endif()

pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)

# hyprdictate::asio (header-only) via the shared module; the plugin
# picks it up the same way from M2.2.
include(${CMAKE_SOURCE_DIR}/cmake/hyprdictate_asio.cmake)

find_path(MOONSHINE_INCLUDE_DIR moonshine-c-api.h REQUIRED)
find_library(MOONSHINE_LIBRARY NAMES moonshine REQUIRED)
add_library(hyprdictate_moonshine SHARED IMPORTED)
set_target_properties(hyprdictate_moonshine PROPERTIES
    IMPORTED_LOCATION "${MOONSHINE_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${MOONSHINE_INCLUDE_DIR}"
)

find_package(Threads REQUIRED)

add_library(hyprdictate_daemon_core STATIC
    src/session.cpp
    src/vocabulary.cpp
)
target_include_directories(hyprdictate_daemon_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(hyprdictate_daemon_core
    PUBLIC
        hyprdictate::shared
        spdlog::spdlog
        Threads::Threads
)

add_executable(hyprdictated
    src/main.cpp
    src/config.cpp
    src/moonshine_engine.cpp
    src/audio.cpp
    src/ipc.cpp
    src/inject.cpp
)

target_include_directories(hyprdictated PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(hyprdictated
    PRIVATE
        hyprdictate_daemon_core
        hyprdictate::asio
        tomlplusplus::tomlplusplus
        CLI11::CLI11
        hyprdictate_moonshine
        PkgConfig::PIPEWIRE
        Threads::Threads
)

install(TARGETS hyprdictated RUNTIME)
