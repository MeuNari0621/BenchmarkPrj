# c_src/CMakeLists.txt

#テスト対象ライブラリを生成
# ソースファイルを指定
set(C_SOURCES
    APP/SRC/LightScheduler.c
    ../Test/SPY/LightControllerSpy.c
)
# C言語のライブラリを生成
add_library(LightSchedulerLib STATIC ${C_SOURCES})
# インクルードディレクトリを設定
target_include_directories(LightSchedulerLib PUBLIC
    APP/INC
    DRV/INC
    RTOS
)
target_compile_options(LightSchedulerLib PRIVATE --coverage -O0)
target_link_libraries(LightSchedulerLib PRIVATE --coverage)
