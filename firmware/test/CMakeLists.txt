cmake_minimum_required(VERSION 3.16)
project(pc_power_host_tests LANGUAGES CXX)

add_executable(control_policy_test control_policy_test.cpp)
target_include_directories(control_policy_test PRIVATE ../main)
target_compile_features(control_policy_test PRIVATE cxx_std_17)
enable_testing()
add_test(NAME control_policy_test COMMAND control_policy_test)
