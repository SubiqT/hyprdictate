add_executable(hyprdictate_protocol_test protocol_test.cpp)
target_link_libraries(hyprdictate_protocol_test PRIVATE hyprdictate::shared)
add_test(NAME hyprdictate_protocol_test COMMAND hyprdictate_protocol_test)

add_executable(hyprdictate_session_test session_test.cpp)
target_link_libraries(hyprdictate_session_test PRIVATE hyprdictate_daemon_core)
add_test(NAME hyprdictate_session_test COMMAND hyprdictate_session_test)

add_executable(hyprdictate_injection_gate_test injection_gate_test.cpp)
target_include_directories(hyprdictate_injection_gate_test
    PRIVATE ${CMAKE_SOURCE_DIR}/plugin/src)
add_test(NAME hyprdictate_injection_gate_test
         COMMAND hyprdictate_injection_gate_test)
