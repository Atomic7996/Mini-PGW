include(GoogleTest)
add_executable(mini_pgw_tests tests.cpp)
target_link_libraries(mini_pgw_tests PRIVATE gtest_main config)
add_test(NAME mini_pgw_tests COMMAND mini_pgw_tests)
