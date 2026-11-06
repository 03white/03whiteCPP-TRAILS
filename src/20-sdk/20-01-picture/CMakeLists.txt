project(20-01-picture)

# 产物放在各自构建目录下，避免 msvc-debug / msvc-asan 等配置互相覆盖。
set(LIBRARY_OUTPUT_PATH ${CMAKE_CURRENT_BINARY_DIR}/lib)

# 业务代码全部编译进静态库，对外只暴露 include/ 下的头文件。
# PUBLIC 让使用方（example/tests）自动拿到 include 路径，不必再手写。
add_library(${PROJECT_NAME} STATIC src/pipeline_impl.cpp)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)

# 本仓库的 MSVC 默认 flags 里没有 /EHsc（见 build/msvc-debug/compile_commands.json），
# 但实现里用 try/catch 接住步骤函数的异常，缺了它会报 C4530 且异常语义不正确。
target_compile_options(${PROJECT_NAME} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/EHsc>)

# 阶段 2 的线程池使用 Boost.Asio 的 boost::asio::thread_pool（header-only）。
# Boost 只出现在实现里，公共头不暴露它，所以用 PRIVATE。
find_package(Boost CONFIG REQUIRED COMPONENTS asio system)
target_link_libraries(${PROJECT_NAME} PRIVATE Boost::asio Boost::system)

# ---- 阶段 5：C API（共享库，导出 imgpipe_* 符号）----
add_library(imgpipe_c SHARED src/c_api.cpp)
target_link_libraries(imgpipe_c PRIVATE ${PROJECT_NAME})
target_compile_features(imgpipe_c PRIVATE cxx_std_17)
target_compile_options(imgpipe_c PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/EHsc>)
set_target_properties(imgpipe_c PROPERTIES
    OUTPUT_NAME imgpipe
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lib
)

# Asio 会 include windows.h：显式给目标平台版本，避免 "Please define _WIN32_WINNT"；
# NOMINMAX/WIN32_LEAN_AND_MEAN 防止 windows.h 的 min/max 宏污染 std::min/std::max。
target_compile_definitions(${PROJECT_NAME} PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0A00
)

# tests/ 已就绪；example/ 还是空目录，等放入 CMakeLists.txt 和源文件后再打开。
add_subdirectory(tests)
# add_subdirectory(example)
