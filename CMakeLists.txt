cmake_minimum_required(VERSION 3.16)
project(EmbeddedAutoHeadlampBenchmark LANGUAGES C CXX)

enable_testing()

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(auto_headlamp_controller STATIC
    src/auto_headlamp_controller.c
)

target_include_directories(auto_headlamp_controller PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.0.zip
)
if(WIN32)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
endif()
FetchContent_MakeAvailable(googletest)

add_executable(test_auto_headlamp_controller
    test/test_auto_headlamp_controller.cpp
)

target_link_libraries(test_auto_headlamp_controller
    PRIVATE
    auto_headlamp_controller
    gtest_main
)

target_include_directories(test_auto_headlamp_controller PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

include(GoogleTest)
gtest_discover_tests(test_auto_headlamp_controller)
