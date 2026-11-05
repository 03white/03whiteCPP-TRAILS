project(imgpipe-tests)

# GTest 由 vcpkg 提供（x64-windows），与仓库的 msvc preset 一致。
find_package(GTest CONFIG REQUIRED)

add_executable(imgpipe_tests
    test_topology.cpp
    test_error.cpp
    test_lifecycle.cpp
    test_async_contract.cpp
)

target_link_libraries(imgpipe_tests PRIVATE
    20-01-picture
    GTest::gtest
    GTest::gtest_main
)

# 本仓库 MSVC 默认 flags 没有 /EHsc；GTest 断言和步骤函数异常都需要它。
target_compile_options(imgpipe_tests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/EHsc>)

# vcpkg 的 x64-windows triplet 给的是动态库；把 GTest 的 DLL 拷到测试可执行文件
# 同目录，否则 gtest_discover_tests 在构建期运行 exe 时会 0xc0000135（找不到 DLL）。
if(WIN32)
    add_custom_command(TARGET imgpipe_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:GTest::gtest>
            $<TARGET_FILE:GTest::gtest_main>
            $<TARGET_FILE_DIR:imgpipe_tests>
        VERBATIM
    )
endif()

# 让 ctest 自动发现每个 TEST 宏，不用手写 add_test()。
include(GoogleTest)
gtest_discover_tests(imgpipe_tests)
