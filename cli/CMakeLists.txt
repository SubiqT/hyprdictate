# hyprdictate: the thin CLI over the daemon's unix socket.
#
# Uses raw POSIX socket calls rather than asio; the CLI is
# short-lived and single-threaded, so pulling asio in for one
# connect/send/close would add build time and a linker dep for no
# functional gain. The wire types come from hyprdictate::shared so
# the CLI and the daemon compile against the same JSON schema.

# --- CLI11 ---
#
# Same version pin and pattern as daemon/CMakeLists.txt. Resolved
# independently here so a component-restricted CLI-only Nix build
# (which never enters daemon/) still finds its argument parser.
find_package(CLI11 2.4 QUIET CONFIG)
if(NOT CLI11_FOUND)
    include(FetchContent)
    message(STATUS "hyprdictate/cli: CLI11 not found, fetching")
    FetchContent_Declare(
        hyprdictate_cli11
        GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
        GIT_TAG        v2.4.2
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(hyprdictate_cli11)
endif()

add_executable(hyprdictate
    src/main.cpp
)

target_include_directories(hyprdictate PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(hyprdictate
    PRIVATE
        hyprdictate::shared
        CLI11::CLI11
)

install(TARGETS hyprdictate RUNTIME)
