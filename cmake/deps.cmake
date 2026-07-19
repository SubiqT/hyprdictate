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

include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

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
