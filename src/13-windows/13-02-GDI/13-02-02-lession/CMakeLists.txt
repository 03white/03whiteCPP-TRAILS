project(13-02-02-lession)
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/build/bin)
add_executable(${PROJECT_NAME} WIN32
	Lession01.cpp
)
target_link_libraries(${PROJECT_NAME} PRIVATE
    user32      # 窗口、消息、UpdateLayeredWindow
    gdi32       # 内存 DC、位图
    gdiplus     # 圆角路径、投影位图
)
# WIN32_LEAN_AND_MEAN / NOMINMAX 由 candemo.cpp 自己 #define（它要先于 windows.h
# 生效），这里再定义一遍只会换来 C4005 重定义警告。
target_compile_definitions(${PROJECT_NAME} PRIVATE
    UNICODE
    _UNICODE
)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)