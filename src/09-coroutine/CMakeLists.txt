cmake_minimum_required(VERSION 3.15)
project(09-coroutine)

# ==================== 编译选项设置 ====================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/build/bin)

# ==================== 编译选项 ====================
if(MSVC)
    add_compile_options(/EHsc)
else()
    add_compile_options(-Wall -Wextra -pedantic)
    
    # **使用 target_compile_options 更精确**
    add_executable(${PROJECT_NAME} main.cpp)
    
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Debug>:-g -O0>
        $<$<CONFIG:Release>:-O3 -DNDEBUG>
    )
endif()

# 如果没有在else中创建目标，在这里创建
if(NOT TARGET ${PROJECT_NAME})
    add_executable(${PROJECT_NAME} main.cpp)
endif()

message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID}")