project(03-02-async)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/build/bin)

# 一课一个可执行文件。target 名沿用原来的 03-02-async-XXX，没有改。
foreach(demo Lesson01 Lesson02)
    add_executable(${PROJECT_NAME}-${demo} ${demo}.cpp)
    target_compile_features(${PROJECT_NAME}-${demo} PRIVATE cxx_std_17)
endforeach()
