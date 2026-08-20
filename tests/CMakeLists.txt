add_executable(fserve_tests
    test_mock_engine.cpp
)

target_link_libraries(fserve_tests PRIVATE fserve_core GTest::gtest_main)
target_compile_options(fserve_tests PRIVATE -Wall -Wextra -Wpedantic)

include(GoogleTest)
gtest_discover_tests(fserve_tests)
