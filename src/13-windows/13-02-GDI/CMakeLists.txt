project(13-02-GDI)

# MSVC 专用。用到 <wrl/client.h> 的 ComPtr、以及 d2d1/dwrite 的 COM 接口，
# MinGW 的头文件覆盖不全，mingw-* 预设下直接跳过而不是让整个仓库 configure 失败。
if(NOT MSVC)
    message(STATUS "13-02-GDI: skipped (MSVC only -- needs WRL + Direct2D/DirectWrite)")
    return()
endif()

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/build/bin)

# WIN32 = /SUBSYSTEM:WINDOWS，入口是 wWinMain，不弹控制台窗口。
add_executable(${PROJECT_NAME} WIN32
    candemo.cpp
)

# 源码里有 #pragma comment(lib, ...)，这里再显式写一遍：
# 一是让依赖关系在构建脚本里可见，二是 pragma 只对 MSVC 有效。
target_link_libraries(${PROJECT_NAME} PRIVATE
    user32      # 窗口、消息、UpdateLayeredWindow
    gdi32       # 内存 DC、位图
    gdiplus     # 圆角路径、投影位图
    d2d1        # ID2D1DCRenderTarget
    dwrite      # 文字排版与测量
    shcore      # GetDpiForMonitor
    imm32       # ImmAssociateContext，关掉宿主窗口的系统输入法
)

# WIN32_LEAN_AND_MEAN / NOMINMAX 由 candemo.cpp 自己 #define（它要先于 windows.h
# 生效），这里再定义一遍只会换来 C4005 重定义警告。
target_compile_definitions(${PROJECT_NAME} PRIVATE
    UNICODE
    _UNICODE
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)
