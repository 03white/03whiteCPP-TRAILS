# 被测源码在这里再编一遍（不含 main.cpp——那是命令行程序的入口，
# 这个 target 有自己的 main.cpp）。
add_executable(12-04-probe-test
    main.cpp
    test_content_range.cpp
    test_response_headers.cpp
    test_filename_resolver.cpp
    test_decide_mode.cpp
    test_probe_interpret.cpp

    ../src/Probe.cpp
    ../src/ResponseHeaders.cpp
    ../src/ContentRange.cpp
    ../src/FilenameResolver.cpp
    ../src/CurlEasy.cpp
)

target_include_directories(12-04-probe-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_compile_features(12-04-probe-test PRIVATE cxx_std_17)
target_link_libraries(12-04-probe-test PRIVATE CURL::libcurl)

# 挂到 CTest 上。顶层 CMakeLists.txt 里要有 enable_testing()。
#   ctest --test-dir build/msvc-debug --output-on-failure
add_test(NAME 12-04-probe COMMAND 12-04-probe-test)
