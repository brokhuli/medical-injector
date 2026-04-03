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
)
target_include_directories(injector-unit-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(injector-unit-tests PRIVATE
    injector-backend-lib
    GTest::gtest
    GTest::gmock
)

add_test(NAME unit-tests COMMAND injector-unit-tests)
