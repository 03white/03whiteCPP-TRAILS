cmake_minimum_required(VERSION 3.15)
project(12-fmt)

# 显示当前构建类型
message(STATUS "=== Build Configuration ===")
message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")

# 如果没有指定构建类型，默认为Debug
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
    message(STATUS "CMAKE_BUILD_TYPE not set, defaulting to Debug")
endif()

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/build/bin)
set(CMAKE_TOOLCHAIN_FILE "D:/tools/vcpkg/scripts/buildsystems/vcpkg.cmake")
set(fmt_DIR "D:/tools/vcpkg/installed/x64-mingw-dynamic/share/fmt")

find_package(fmt CONFIG REQUIRED)
add_executable(${PROJECT_NAME} main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE fmt::fmt)

# 根据构建类型选择DLL
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(FMT_DLL_PATH "D:/tools/vcpkg/installed/x64-mingw-dynamic/debug/bin/libfmtd.dll")
    message(STATUS "Debug mode: using ${FMT_DLL_PATH}")
else()
    set(FMT_DLL_PATH "D:/tools/vcpkg/installed/x64-mingw-dynamic/bin/libfmt.dll")
    message(STATUS "Release mode: using ${FMT_DLL_PATH}")
endif()

# 检查DLL文件是否存在
if(EXISTS ${FMT_DLL_PATH})
    message(STATUS "✅ Found fmt DLL at: ${FMT_DLL_PATH}")
    
    # 复制到exe目录
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${FMT_DLL_PATH}
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
        COMMENT "Copying fmt DLL to executable directory"
    )
    
    # 复制到bin目录
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${FMT_DLL_PATH}
            ${EXECUTABLE_OUTPUT_PATH}/
        COMMENT "Copying fmt DLL to bin directory"
    )
else()
    message(WARNING "❌ fmt DLL not found at: ${FMT_DLL_PATH}")
endif()