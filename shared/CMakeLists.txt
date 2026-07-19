# Wire protocol and state types shared between the daemon (M1), the
# CLI (M1), and the Hyprland plugin (M2). Building this as STATIC
# means M1's daemon and CLI each link their own copy of the code
# without an installed .so lifetime to reason about. The plugin, when
# added, links the same archive with -fPIC compatible object files.

add_library(hyprdictate_shared STATIC
    src/protocol.cpp
    src/state.cpp
)

target_include_directories(hyprdictate_shared
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# The plugin half will load this archive into Hyprland's process
# space, so every object needs to be position-independent. Setting it
# unconditionally keeps daemon and plugin builds symmetric; there is
# no measurable cost on x86_64 for PIE code.
set_target_properties(hyprdictate_shared PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_link_libraries(hyprdictate_shared PUBLIC nlohmann_json::nlohmann_json)

add_library(hyprdictate::shared ALIAS hyprdictate_shared)
