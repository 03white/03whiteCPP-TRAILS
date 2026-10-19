project(13-03-01-IPC)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/build/bin)

# 这个 demo 是「一份协议、两个进程」：service 常驻建管道，client 轻量连上去。
# 所以必须是两个独立的可执行文件，不能合成一个 target。
# 两边没有共享的 .cpp（协议目前就是字符串约定），各自单文件编译即可；
# 等 doc/扩展.md 第 1 条把协议换成 struct KeyEvent，再抽一个 protocol.h 出来，
# 用 add_library(ipc-protocol INTERFACE) 挂给两个 target。
foreach(app service client)
    add_executable(ipc-${app} ${app}/${app}.cpp)

    # target 名要全局唯一（CMake 的硬性约束），但落盘的文件名不受此限。
    # 这里改回 service.exe / client.exe，和 readme 第 4 节的运行命令对齐。
    set_target_properties(ipc-${app} PROPERTIES OUTPUT_NAME ${app})

    target_compile_features(ipc-${app} PRIVATE cxx_std_17)

    # UNICODE/_UNICODE 是这个 demo 的必需项，不是风格偏好：
    # CreateNamedPipe / CreateFile 都是宏，不定义 UNICODE 时展开成 ...A 版本，
    # 形参是 LPCSTR；而源码里的 PIPE_NAME 是 L"..." 宽字符串，直接编译不过
    # （cannot convert 'const wchar_t*' to 'LPCSTR'）。
    # CMake 默认不给 MSVC 加这两个宏（VS IDE 新建项目才默认 Unicode），得手动写。
    #
    # WIN32_LEAN_AND_MEAN: 砍掉 <windows.h> 里用不到的一大堆子头文件，编译更快。
    # NOMINMAX:            挡掉 min/max 宏，否则和 <algorithm> 打架。
    target_compile_definitions(ipc-${app} PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endforeach()

# 不用 target_link_libraries：命名管道那套 API（CreateNamedPipe/ReadFile/...）
# 全在 kernel32 里，MSVC 和 MinGW 都默认链接它，不像 Winsock 要显式挂 ws2_32。
# _getch() 来自 <conio.h>，在 MSVC 的 CRT 里，同样不需要额外库。
