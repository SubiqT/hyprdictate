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
# - hyprdictate::asio for the socket server
# - hyprdictate::shared for the wire protocol types
# - whisper.cpp for inference
# - libpipewire-0.3 for audio capture

find_package(PkgConfig REQUIRED)

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
