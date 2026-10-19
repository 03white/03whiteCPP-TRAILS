project(umathlib-share)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/build/bin)

add_executable(${PROJECT_NAME} main.cpp)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)

# main.cpp 写的是 #include "include/math.h"，相对自己所在目录就能找到，
# 所以不需要 target_include_directories。
# 这个 include/math.h 是手抄的一份副本，模拟「拿到别人的头 + 二进制库」的场景。

# 原来这里还有一句 target_link_directories(... ${CMAKE_CURRENT_SOURCE_DIR}/lib)，
# 是死代码：下面 link 的 cmathlib-share 是同一棵构建树里的兄弟 target
# （见 ../CMakeLists.txt 的 add_subdirectory(cmathlib)），CMake 直接按 target
# 解析依赖，根本不会去搜 lib/ 目录。lib/ 里躺的是七月用 MinGW 编的旧产物，
# 从来没参与过链接。
target_link_libraries(${PROJECT_NAME} PRIVATE cmathlib-share)

# exe 旁边放一份 DLL，否则运行时加载不到。
# 原来这里硬编码的是 lib/libcmathlib-share.dll —— 既是陈旧的 MinGW 产物，
# 名字也和 MSVC 构建出来的 cmathlib-share.dll 对不上（MSVC 不加 lib 前缀），
# 拷过去照样跑不起来。换成 $<TARGET_FILE:>，由 CMake 按当前配置算出真实路径。
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:cmathlib-share>
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
    COMMENT "Copying cmathlib-share DLL next to ${PROJECT_NAME}"
)
