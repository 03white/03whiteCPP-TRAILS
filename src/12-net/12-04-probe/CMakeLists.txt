project(12-04-probe)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/build/bin)

# CURL::libcurl 自带头文件路径、库路径和 Debug 下的 libcurl-d.lib 选择。
# 详见 12-01-curl/readme.md 第 4 节。
find_package(CURL CONFIG REQUIRED)

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/Probe.cpp
    src/ResponseHeaders.cpp
    src/ContentRange.cpp
    src/FilenameResolver.cpp
    src/CurlEasy.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)
target_link_libraries(${PROJECT_NAME} PRIVATE CURL::libcurl)

# test/ 自己再编一遍这几个 .cpp（可执行文件没法被链接，测试程序够不着 exe 里的代码）。
# 代价是同一份源码编译两次；换来的是这个专题目录不对外暴露任何 target。
add_subdirectory(test)
