project(20-01-picture)

# 物不落在 和全仓其它专题保持一致：产build/<preset> 的深层目录里。
set(LIBRARY_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/build/lib)

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
