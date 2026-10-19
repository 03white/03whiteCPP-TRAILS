project(cmathlib-share)

set(LIBRARY_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/build/lib)

add_library(${PROJECT_NAME} SHARED math.cpp)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)

# 构建 DLL 时定义 BUILD_DLL，让 math.h 里的 MATH_API 展开成 __declspec(dllexport)；
# 使用方没有这个宏，展开成 dllimport。PRIVATE 是关键：这个宏只能在编译这个库
# 自己时生效，传播给使用方就把导入写成导出了。
target_compile_definitions(${PROJECT_NAME} PRIVATE BUILD_DLL)

# 这里没有 target_include_directories：umathlib 手抄了一份 include/math.h，
# 刻意模拟「拿到别人的头 + 二进制库」的场景，不走 CMake 的依赖传播。
