# hyprdictated: the voice-dictation daemon.
#
# The daemon's CMakeLists.txt grows through the M1 commit series: each
# feature commit adds its own source files and pkg-config lookups
# rather than declaring the full dep graph up front. That keeps
# individual commits reviewable and each intermediate state buildable
# on its own.
#
# Current deps:
# - CLI11 for arg parsing
# - toml++ for config
# - spdlog for logging
# - hyprdictate::shared for the wire protocol types
# - whisper.cpp for inference
# - libpipewire-0.3 for audio capture

find_package(PkgConfig REQUIRED)

# PipeWire ships pkg-config only; no CMake config. libpipewire-0.3 is
# the ABI Ubuntu/Debian, Fedora, and NixOS all agree on today; if the
# repository sees a 1.x-era libpipewire-1.0 we'll bump the module
# name here.
pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)

# whisper.cpp installs both a CMake config (`whisper-config.cmake`) and
# a pkg-config file (whisper.pc), depending on how it's built.
# find_package covers the standard install and Nix path; the
# pkg_check_modules fallback rescues distro packages that only ship
# the .pc.
find_package(whisper QUIET CONFIG)
if(NOT whisper_FOUND)
    pkg_check_modules(WHISPER REQUIRED IMPORTED_TARGET whisper)
    add_library(hyprdictate_whisper INTERFACE)
    target_link_libraries(hyprdictate_whisper INTERFACE PkgConfig::WHISPER)
    add_library(whisper ALIAS hyprdictate_whisper)
endif()

add_executable(hyprdictated
    src/main.cpp
    src/config.cpp
    src/whisper_engine.cpp
    src/audio.cpp
)

target_include_directories(hyprdictated PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(hyprdictated
    PRIVATE
        hyprdictate::shared
        tomlplusplus::tomlplusplus
        spdlog::spdlog
        CLI11::CLI11
        whisper
        PkgConfig::PIPEWIRE
)

install(TARGETS hyprdictated RUNTIME)
