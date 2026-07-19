# hyprdictate third-party dependency resolution.
#
# Two paths per dep:
#
#   1. find_package (or pkg_check_modules) first, so a Nix build
#      always hits the sandboxed store paths; FetchContent's network
#      access is never triggered on Nix and reproducibility rides on
#      flake.lock.
#
#   2. FetchContent fallback for header-only libraries a distro may
#      not ship. The GIT_TAG lines are the source of truth for the
#      non-Nix build; bump here to update a dep.
#
# Deps that always come from the system (whisper.cpp, PipeWire,
# Hyprland) are declared in the target CMakeLists that need them so
# missing them produces a build error only for the affected target.
# nlohmann/json is the one dep required at this top level because
# shared/ links it publicly.

include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

# --- nlohmann/json ---
#
# 3.11.3 is the release with the fixes to structured binding decompose
# that our std::visit-based serializer relies on. Bump only after
# confirming the wire test still round-trips.
find_package(nlohmann_json 3.11 QUIET CONFIG)
if(NOT nlohmann_json_FOUND)
    message(STATUS "hyprdictate: nlohmann_json not found, fetching")
    FetchContent_Declare(
        hyprdictate_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG        v3.11.3
        GIT_SHALLOW    TRUE
    )
    set(JSON_BuildTests OFF CACHE INTERNAL "")
    FetchContent_MakeAvailable(hyprdictate_json)
endif()

# --- tomlplusplus (daemon only) ---
#
# 3.4.0 is the first release that exposes toml::parse_file with a
# std::filesystem::path overload, letting the daemon consume the
# XDG-resolved path directly. Gated by the daemon build option so
# a CLI-only Nix build doesn't require this dep in its buildInputs.
if(HYPRDICTATE_BUILD_DAEMON)
    find_package(tomlplusplus 3.3 QUIET CONFIG)
    if(NOT tomlplusplus_FOUND)
        message(STATUS "hyprdictate: tomlplusplus not found, fetching")
        FetchContent_Declare(
            hyprdictate_tomlplusplus
            GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
            GIT_TAG        v3.4.0
            GIT_SHALLOW    TRUE
        )
        FetchContent_MakeAvailable(hyprdictate_tomlplusplus)
    endif()
endif()

# --- spdlog (daemon only) ---
#
# 1.13.0 gets us std::format-backed formatters and drops the older
# fmt-headers-only workaround. spdlog::spdlog remains the canonical
# target regardless of source. The plugin uses a stderr shim to
# match hyprwsmode; only the daemon links spdlog.
if(HYPRDICTATE_BUILD_DAEMON)
    find_package(spdlog 1.12 QUIET CONFIG)
    if(NOT spdlog_FOUND)
        message(STATUS "hyprdictate: spdlog not found, fetching")
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
endif()

# --- CLI11 (daemon + CLI) ---
#
# CLI11 is single-header; find_package resolves the standard
# `CLI11::CLI11` target when nixpkgs' cli11 is on the prefix path.
# FetchContent falls back to a git tag rather than vendoring the
# header so bumping the pin stays a one-line change here.
if(HYPRDICTATE_BUILD_DAEMON OR HYPRDICTATE_BUILD_CLI)
    find_package(CLI11 2.4 QUIET CONFIG)
    if(NOT CLI11_FOUND)
        message(STATUS "hyprdictate: CLI11 not found, fetching")
        FetchContent_Declare(
            hyprdictate_cli11
            GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
            GIT_TAG        v2.4.2
            GIT_SHALLOW    TRUE
        )
        FetchContent_MakeAvailable(hyprdictate_cli11)
    endif()
endif()

# --- asio (standalone, daemon only until M2.2) ---
#
# asio doesn't install a CMake config; we look for the header on the
# include path (nixpkgs' `asio` package places asio.hpp there directly)
# and wrap it as an INTERFACE target so downstream code depends on
# `hyprdictate::asio` regardless of source. ASIO_STANDALONE selects
# the Boost-free build; ASIO_NO_DEPRECATED trims the legacy handler
# APIs that trip newer C++ conformance modes. Only the daemon consumes
# this today; the M2.2 plugin socket client will opt in by widening
# this gate.
if(HYPRDICTATE_BUILD_DAEMON)
    find_path(HYPRDICTATE_ASIO_INCLUDE_DIR asio.hpp)
    if(NOT HYPRDICTATE_ASIO_INCLUDE_DIR)
        message(STATUS "hyprdictate: asio.hpp not found, fetching")
        FetchContent_Declare(
            hyprdictate_asio
            GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
            GIT_TAG        asio-1-30-2
            GIT_SHALLOW    TRUE
        )
        FetchContent_MakeAvailable(hyprdictate_asio)
        set(HYPRDICTATE_ASIO_INCLUDE_DIR ${hyprdictate_asio_SOURCE_DIR}/asio/include)
    endif()

    add_library(hyprdictate_asio INTERFACE)
    target_include_directories(hyprdictate_asio SYSTEM INTERFACE ${HYPRDICTATE_ASIO_INCLUDE_DIR})
    target_compile_definitions(hyprdictate_asio INTERFACE
        ASIO_STANDALONE=1
        ASIO_NO_DEPRECATED=1
    )
    add_library(hyprdictate::asio ALIAS hyprdictate_asio)
endif()
