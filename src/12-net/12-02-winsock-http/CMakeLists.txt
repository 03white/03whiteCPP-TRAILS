project(12-02-winsock-http)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/build/bin)

add_executable(${PROJECT_NAME}
    main.cpp
    ChunkDecoder.cpp
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)

# Winsock 2 的导入库。MSVC 也可以用 #pragma comment(lib, "ws2_32.lib")，
# 但那是编译器私有扩展，MinGW 不认；写在 CMake 里两边通吃。
target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32)

# WIN32_LEAN_AND_MEAN: 让 <windows.h> 不要自动拉进 winsock.h(v1)，
#                      否则和 <winsock2.h> 撞一堆重定义。
# NOMINMAX:            挡掉 windows.h 里的 min/max 宏，它们会破坏 <algorithm>。
target_compile_definitions(${PROJECT_NAME} PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)
