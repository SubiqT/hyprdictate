cmake_minimum_required(VERSION 3.27)

# Compile flags for the plugin match hyprwsmode's, which in turn
# match Hyprland's own build. Mismatched flags between the plugin
# and the compositor surface as obscure symbol-resolution errors at
# plugin load rather than at build time.
add_compile_options(-fPIC -Wall -Wextra
    -Wno-unused-parameter -Wno-unused-value
    -Wno-missing-field-initializers -Wno-narrowing -Wno-pointer-arith)

# --no-gnu-unique keeps type_info addresses per-DSO, matching the
# flag Hyprland compiles with. Required for reliable function-hook
# and typeid-based plugin behaviour.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(--no-gnu-unique)
endif()

# Baseline plugin sources. Later M2 commits add dispatcher.cpp,
# injector.cpp, and indicator.cpp alongside.
add_library(hyprdictate_plugin SHARED
    src/main.cpp
    src/socket_client.cpp
)

# Match hyprwsmode's dep list: hyprland's headers, wlroots protocol
# support headers, and the X-adjacent libs Hyprland's public headers
# transitively include. Locked to the same set that hyprwsmode uses
# so plugin load doesn't surface a missing-symbol only at runtime.
find_package(PkgConfig REQUIRED)
pkg_check_modules(HYPRLAND_DEPS REQUIRED IMPORTED_TARGET
    hyprland
    libdrm
    pixman-1
    wayland-server
    xkbcommon
)

# Hyprland 0.55+ routes hyprctl dispatch through Lua bindings, so a
# dispatcher that wants to be reachable via `hyprctl dispatch
# 'hl.plugin.hyprdictate.toggle()'` needs the plugin to link Lua
# itself. hyprland.pc does not chain it in. The version search list
# mirrors hyprwsmode so distros' varying Lua .pc naming is handled.
pkg_search_module(LUA REQUIRED IMPORTED_TARGET
    lua55 lua5.5 lua-55 lua-5.5 lua>=5.5 lua<5.6)

target_include_directories(hyprdictate_plugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(hyprdictate_plugin
    PRIVATE
        hyprdictate::shared
        PkgConfig::HYPRLAND_DEPS
        PkgConfig::LUA
)

# Install as libhyprdictate.so under lib/ so Hyprland's plugin loader
# picks it up via its usual path lookup. The Nix module points
# Hyprland at ${plugin}/lib/libhyprdictate.so directly, matching the
# hyprwsmode integration pattern.
set_target_properties(hyprdictate_plugin PROPERTIES
    OUTPUT_NAME hyprdictate
    PREFIX      lib
)

install(TARGETS hyprdictate_plugin LIBRARY)
