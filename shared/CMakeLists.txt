# Wire protocol and state types shared between the daemon (M1), the
# CLI (M1), and the Hyprland plugin (M2). Built as STATIC so daemon,
# CLI, and plugin each link their own copy of the code without an
# installed .so lifetime to reason about.

# --- nlohmann/json ---
#
# The protocol header includes <nlohmann/json.hpp> directly, so this
# dep is part of shared/'s PUBLIC interface — every consumer inherits
# it transitively. Version 3.11.3 has the structured-binding
# decompose fixes our std::visit-based serializer relies on; bump
# only after confirming the wire test still round-trips.
#
# find_package first for Nix / distro packaging, FetchContent as the
# non-Nix fallback. Both paths expose the same
# `nlohmann_json::nlohmann_json` imported target.
find_package(nlohmann_json 3.11 QUIET CONFIG)
if(NOT nlohmann_json_FOUND)
    include(FetchContent)
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

add_library(hyprdictate_shared STATIC
    src/protocol.cpp
    src/state.cpp
)

target_include_directories(hyprdictate_shared
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# The plugin loads this archive into Hyprland's process space, so
# every object needs to be position-independent. Setting it
# unconditionally keeps daemon and plugin builds symmetric; there is
# no measurable cost on x86_64 for PIE code.
set_target_properties(hyprdictate_shared PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_link_libraries(hyprdictate_shared PUBLIC nlohmann_json::nlohmann_json)

add_library(hyprdictate::shared ALIAS hyprdictate_shared)
