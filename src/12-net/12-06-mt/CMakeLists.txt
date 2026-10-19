project(12-06-mt)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/build/bin)

find_package(CURL CONFIG REQUIRED)

# 多线程分块下载：每个线程一个 CURL easy 句柄。
# libcurl 全局初始化不是线程安全的，main() 里要先 curl_global_init 再开线程。
find_package(Threads REQUIRED)

# 包名必须写全大写的 OpenSSL（走 CMake 自带的 FindOpenSSL 模块，不是 CONFIG
# 模式），给出的 imported target 是 OpenSSL::SSL / OpenSSL::Crypto。
# 原来写的小写 openssl 在 Windows 上碰巧能过，纯属 NTFS 大小写不敏感，
# 换到 Linux 上就是 "Could not find a package configuration file"。
find_package(OpenSSL REQUIRED)
add_executable(${PROJECT_NAME}
    main.cpp
    Multidownloader.cpp
    singledownloader.cpp
    CurlEasy.cpp
    evp.cpp
    # 新的实现文件加在这里
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)
target_link_libraries(${PROJECT_NAME} PRIVATE CURL::libcurl Threads::Threads OpenSSL::SSL OpenSSL::Crypto)
