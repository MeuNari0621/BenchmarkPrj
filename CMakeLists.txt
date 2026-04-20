cmake_minimum_required(VERSION 3.16)
project(EmbeddedACCBenchmark LANGUAGES C CXX)

enable_testing()

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ACC Controller library
add_library(acc_controller STATIC
    src/acc_controller.c
)

target_include_directories(acc_controller PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(acc_controller PRIVATE m)

# Google Test
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.0.zip
)
if(WIN32)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
endif()
FetchContent_MakeAvailable(googletest)

# Test executable
add_executable(test_acc_controller
    test/test_acc_controller.cpp
)

target_link_libraries(test_acc_controller
    PRIVATE
    acc_controller
    gtest_main
)

target_include_directories(test_acc_controller PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

include(GoogleTest)
gtest_discover_tests(test_acc_controller)
