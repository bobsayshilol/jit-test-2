cmake_minimum_required(VERSION 3.26)
project(jit_test_2)
enable_testing()

set(CMAKE_CXX_STANDARD 23)

add_subdirectory(lib)
add_subdirectory(test)
add_subdirectory(examples)
