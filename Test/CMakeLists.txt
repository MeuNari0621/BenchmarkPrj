# cpp_src/CMakeLists.txt

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.0.zip
)

if (WIN32)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
endif()
FetchContent_MakeAvailable(googletest)

# カバレッジ計測のためのオプションを追加
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --coverage -O0 -g") # -O0: 最適化を無効, -g: デバッグ情報
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage") # カバレッジ用

# LightSchedulerのテスト
# ソースファイルの変数化
set(TEST_SOURCE_FILE test_LightScheduler.cpp)
get_filename_component(target ${TEST_SOURCE_FILE} NAME_WE)
add_executable(${target} ${TEST_SOURCE_FILE})
target_link_libraries(${target} gtest_main LightSchedulerLib )
include(GoogleTest)
gtest_discover_tests(${target})
