project(03-01-sync)

# 原来是每个 target 各写一遍 set_target_properties(RUNTIME_OUTPUT_DIRECTORY ...)，
# 一行目录级变量就能覆盖本目录所有 target，和全仓其它专题的写法也一致。
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/build/bin)

# 一课一个可执行文件。target 名沿用原来的 03-01-sync-XXX，没有改。
foreach(demo Lesson01 Expend01 Expend02)
    add_executable(${PROJECT_NAME}-${demo} ${demo}.cpp)
    target_compile_features(${PROJECT_NAME}-${demo} PRIVATE cxx_std_17)
endforeach()
