project(12-06-mt)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/build/bin)

find_package(CURL CONFIG REQUIRED)

# 多线程分块下载：每个线程一个 CURL easy 句柄。
# libcurl 全局初始化不是线程安全的，main() 里要先 curl_global_init 再开线程。
find_package(Threads REQUIRED)

add_executable(${PROJECT_NAME}
    main.cpp
    Multidownloader.cpp
    singledownloader.cpp
    CurlEasy.cpp
    # 新的实现文件加在这里
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)
target_link_libraries(${PROJECT_NAME} PRIVATE CURL::libcurl Threads::Threads)
