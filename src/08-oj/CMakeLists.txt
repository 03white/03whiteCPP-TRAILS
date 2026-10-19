project(08-oj)

# 原来写的是 ${CMAKE_SOURCE_DIR}，那是**仓库根目录**，exe 会掉到
# <repo>/build/bin 里去，和其它专题不在一个地方。三个变量的区别：
#   CMAKE_CURRENT_SOURCE_DIR  当前这个 CMakeLists 所在目录  <- 全仓统一用这个
#   PROJECT_SOURCE_DIR        最近一次 project() 所在目录
#   CMAKE_SOURCE_DIR          顶层 CMakeLists 所在目录（仓库根）
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/build/bin)

# 一题一个可执行文件。新开一题就往列表里加一个名字。
foreach(q 01Test Q1 Q2 Q3)
    add_executable(${PROJECT_NAME}-${q} ${q}.cpp)
    target_compile_features(${PROJECT_NAME}-${q} PRIVATE cxx_std_17)
endforeach()
