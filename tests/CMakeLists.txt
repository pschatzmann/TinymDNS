cmake_minimum_required(VERSION 3.10)
project(tinymdns_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# stubs/ provides minimal Arduino.h, IPAddress.h and Udp.h for the host.
# src/ is the library itself, compiled unchanged.
add_executable(tinymdns_tests test_main.cpp)
target_include_directories(tinymdns_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${CMAKE_CURRENT_SOURCE_DIR}/../src)

enable_testing()
add_test(NAME tinymdns COMMAND tinymdns_tests)
