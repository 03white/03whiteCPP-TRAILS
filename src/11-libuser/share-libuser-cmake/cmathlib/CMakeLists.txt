project(cmathlib-share)
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/build/lib)
add_library(${PROJECT_NAME} SHARED math.cpp)
# 当构建 DLL 时在编译器预处理器中定义 BUILD_DLL，使 MATH_API 成为 __declspec(dllexport)
target_compile_definitions(${PROJECT_NAME} PRIVATE BUILD_DLL)
# 可选：导出头文件搜索路径
# target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})