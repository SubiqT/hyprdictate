# hyprdictate_asio.cmake — defines the hyprdictate::asio INTERFACE
# target for consumers that need standalone asio.
#
# asio doesn't install a CMake config; we look for the header on the
# include path (nixpkgs' `asio` package places asio.hpp there
# directly) and wrap it as an INTERFACE target so downstream code
# depends on `hyprdictate::asio` regardless of source.
# ASIO_STANDALONE selects the Boost-free build; ASIO_NO_DEPRECATED
# trims the legacy handler APIs that trip newer C++ conformance
# modes.
#
# include_guard(GLOBAL) means daemon/ and plugin/ can both
# `include(cmake/hyprdictate_asio.cmake)` — the second include is a
# no-op, but the target created by the first is visible from the
# second thanks to CMake's global target scope. Component-restricted
# builds that only add_subdirectory one of them still get the
# target because whichever CMakeLists includes this file first
# creates it.

include_guard(GLOBAL)

find_path(HYPRDICTATE_ASIO_INCLUDE_DIR asio.hpp)
if(NOT HYPRDICTATE_ASIO_INCLUDE_DIR)
    include(FetchContent)
    message(STATUS "hyprdictate/asio: asio.hpp not found, fetching")
    # 1.30.2 matches nixpkgs' current asio and provides the executor
    # traits the daemon's IpcServer and (in M2.2) the plugin socket
    # client rely on.
    FetchContent_Declare(
        hyprdictate_asio_src
        GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
        GIT_TAG        asio-1-30-2
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(hyprdictate_asio_src)
    set(HYPRDICTATE_ASIO_INCLUDE_DIR ${hyprdictate_asio_src_SOURCE_DIR}/asio/include)
endif()

add_library(hyprdictate_asio INTERFACE)
target_include_directories(hyprdictate_asio SYSTEM INTERFACE ${HYPRDICTATE_ASIO_INCLUDE_DIR})
target_compile_definitions(hyprdictate_asio INTERFACE
    ASIO_STANDALONE=1
    ASIO_NO_DEPRECATED=1
)
add_library(hyprdictate::asio ALIAS hyprdictate_asio)
