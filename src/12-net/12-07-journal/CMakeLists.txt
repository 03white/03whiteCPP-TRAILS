project(12-07-journal)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/build/bin)

# libcurl 走 CONFIG 模式（vcpkg 装出来的是 lib/cmake/CURL/*.cmake）。
# 多线程分块下载：每个线程一个 CURL easy 句柄。
# libcurl 全局初始化不是线程安全的，main() 里要先 curl_global_init 再开线程。
find_package(CURL CONFIG REQUIRED)
find_package(Threads REQUIRED)

# OpenSSL 走 CMake 自带的 FindOpenSSL 模块（不是 CONFIG 模式）。
# 包名必须写全大写的 OpenSSL，给出的 imported target 是
# OpenSSL::SSL / OpenSSL::Crypto。原来写的 find_package(openssl) 在 Windows 上
# 碰巧能过（NTFS 大小写不敏感），但 openssl::ssl / openssl::crypto 这两个
# target 根本不存在，链接期一定报错。
find_package(OpenSSL REQUIRED)

# nlohmann_json 走 CONFIG 模式，包名带下划线（vcpkg 的 port 名才是连字符的
# nlohmann-json）。它只导出一个 header-only 的 INTERFACE target。
# 原来写的 find_package(json) 不是任何包的配置名，configure 阶段就会失败。
find_package(nlohmann_json CONFIG REQUIRED)

# 源文件按目录分层，和 include/ 下的 core / entity / net / repository / util 对应。
# 原来只列了 main.cpp，其余实现文件不参与编译，一旦 main 用到就会一堆
# undefined reference（链接错误）。
add_executable(${PROJECT_NAME}
    src/main.cpp
    src/core/downloader.cpp
    src/core/worker.cpp
    src/core/piecemanager.cpp
    src/net/httpsession.cpp
    src/repository/jsondownloadrepository.cpp
    src/util/stringutil.cpp
    src/util/atomicfile.cpp
    src/core/checkpoint.cpp
    # 新的实现文件加在这里
)

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)
target_link_libraries(${PROJECT_NAME} PRIVATE
    CURL::libcurl
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
    nlohmann_json::nlohmann_json
)

# tests/CMakeLists.txt 目前是空的；根目录已经 enable_testing()，
# 等写好单测（interfaces.md 建议用 doctest）后，在那边挂 add_test() 即可。
add_subdirectory(tests)
