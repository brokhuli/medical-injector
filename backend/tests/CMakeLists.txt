find_package(GTest CONFIG REQUIRED)

# ── Unit tests ───────────────────────────────────────────────
add_executable(injector-unit-tests
    test_main.cpp
    test_placeholder.cpp
    test_motor_model.cpp
    test_pressure_model.cpp
    test_valve_model.cpp
    test_syringe_model.cpp
    test_air_detector.cpp
    test_simulated_hal_integration.cpp
    test_config.cpp
    test_ring_buffer.cpp
    test_pid_controller.cpp
    test_control_loop.cpp
    test_data_logger.cpp
    test_command_queue.cpp
    test_state_machine.cpp
    test_safety_monitor.cpp
)
target_include_directories(injector-unit-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures
)
target_link_libraries(injector-unit-tests PRIVATE
    injector-backend-lib
    GTest::gtest
    GTest::gmock
)

add_test(NAME unit-tests COMMAND injector-unit-tests)
set_tests_properties(unit-tests PROPERTIES TIMEOUT 120)

# ── Integration tests ────────────────────────────────────────
add_executable(injector-integration-tests
    test_main.cpp
    test_injection_lifecycle.cpp
    test_fault_end_to_end.cpp
    test_safety_integration.cpp
)
target_include_directories(injector-integration-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures
)
target_link_libraries(injector-integration-tests PRIVATE
    injector-backend-lib
    GTest::gtest
    GTest::gmock
)

add_test(NAME integration-tests COMMAND injector-integration-tests)
set_tests_properties(integration-tests PROPERTIES TIMEOUT 300)
