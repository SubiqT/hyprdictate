# hyprdictated: the voice-dictation daemon.
#
# The daemon's CMakeLists.txt grows through the M1 commit series: each
# feature commit adds its own source files and pkg-config lookups
# rather than declaring the full dep graph up front. That keeps
# individual commits reviewable and each intermediate state buildable
# on its own.
#
# Baseline (this commit): CLI11 for arg parsing, toml++ for the
# config file, spdlog for logging, and the shared protocol library
# for the wire types used later by the IPC server.

add_executable(hyprdictated
    src/main.cpp
    src/config.cpp
)

target_include_directories(hyprdictated PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(hyprdictated
    PRIVATE
        hyprdictate::shared
        tomlplusplus::tomlplusplus
        spdlog::spdlog
        CLI11::CLI11
)

install(TARGETS hyprdictated RUNTIME)
