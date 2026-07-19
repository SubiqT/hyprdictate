# hyprdictate: the thin CLI over the daemon's unix socket.
#
# Uses raw POSIX socket calls rather than asio; the CLI is
# short-lived and single-threaded, so pulling asio in for one
# connect/send/close would add build time and a linker dep for no
# functional gain. The wire types come from hyprdictate::shared so
# the CLI and the daemon compile against the same JSON schema.

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
