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
# - whisper.cpp for inference
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

pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)

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
    src/session.cpp
    src/ipc.cpp
    src/inject.cpp
    src/vocabulary.cpp
)

target_include_directories(hyprdictated PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(hyprdictated
    PRIVATE
        hyprdictate::shared
        hyprdictate::asio
        tomlplusplus::tomlplusplus
        spdlog::spdlog
        CLI11::CLI11
        whisper
        PkgConfig::PIPEWIRE
)

# Threads::Threads is a transitive dep of both asio (for std::thread
# in signal handling) and whisper (Threads::Threads is exported in
# its cmake config on some builds); declaring it here explicitly
# keeps link-time errors out of the "why doesn't asio compile" pile.
find_package(Threads REQUIRED)
target_link_libraries(hyprdictated PRIVATE Threads::Threads)

install(TARGETS hyprdictated RUNTIME)
