project(umathlib)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/build/bin)

add_executable(${PROJECT_NAME} main.cpp)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)

# main.cpp 写的是 #include "include/math.h"，相对自己所在目录就能找到，
# 所以不需要 target_include_directories。

# 原来这里有一句 link_directories(${PROJECT_SOURCE_DIR}/lib)，两重无效：
#   1. link_directories 是目录级命令，只对它之后创建的 target 生效，而它写在
#      add_executable 后面 —— 对本目录唯一的 target 完全没作用；
#   2. 就算位置对了也多余：cmathlib 是同一棵构建树里的兄弟 target
#      （见 ../CMakeLists.txt 的 add_subdirectory(cmathLib)），CMake 按 target
#      解析，不会去搜 lib/ 目录。lib/libcmathlib.a 是七月的 MinGW 旧产物。
# 对应 g++ 的 -lcmathlib，但这里给的是 target 名而不是库文件名，
# 连带的头文件路径/编译选项/传递依赖都由 CMake 自己算。
target_link_libraries(${PROJECT_NAME} PRIVATE cmathlib)
