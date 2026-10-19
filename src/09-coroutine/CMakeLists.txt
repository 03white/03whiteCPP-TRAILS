project(09-coroutine)

# 原来写的是 ${CMAKE_SOURCE_DIR}（仓库根），exe 会掉到 <repo>/build/bin。
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/build/bin)

add_executable(${PROJECT_NAME} main.cpp)

# 协程是 C++20 的东西，而根 CMakeLists 兜底的是 17，这里单独提上去。
# 原来是在目录作用域 set(CMAKE_CXX_STANDARD 20)，能生效但会悄悄影响整个子树；
# 挂在 target 上更精确，也和 17-json 的做法一致。
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)

# 原来这段是 if(MSVC) 加 /EHsc / else() 加 -Wall 并且**只在 else 分支里**
# add_executable，后面再用 if(NOT TARGET) 补一次 —— 绕且容易看漏。
# 改成先无条件建 target，再用生成器表达式按编译器分流。
# MSVC 的 /EHsc 不用写，CMake 默认就带。
target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall;-Wextra;-pedantic>
)
