project(cmathlib)

set(LIBRARY_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/build/lib)

add_library(${PROJECT_NAME} STATIC math.cpp)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)

# 静态库不需要 dllexport/dllimport，所以 math.h 里是光秃秃的函数声明，
# 对比 ../../share-libuser-cmake/cmathlib/math.h 的 MATH_API 看差别。
