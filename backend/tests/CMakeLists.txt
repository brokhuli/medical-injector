find_package(GTest CONFIG REQUIRED)

# ── Unit tests ───────────────────────────────────────────────
add_executable(injector-unit-tests
    test_placeholder.cpp
)
target_link_libraries(injector-unit-tests PRIVATE
    injector-backend-lib
    GTest::gtest_main
    GTest::gmock
)
add_test(NAME unit-tests COMMAND injector-unit-tests)
