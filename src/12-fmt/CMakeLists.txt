project(12-fmt)

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/build/bin)

# 这里原本硬编码了 D:/tools/vcpkg 的 toolchain、fmt_DIR 和 x64-mingw-dynamic
# 的 DLL 路径。三个问题：
#   1. 那个路径在别的机器上不存在（本机是 D:/vcpkg）；
#   2. CMAKE_TOOLCHAIN_FILE 必须在顶层 project() 之前生效，在子目录里 set 根本
#      不起作用，真正生效的是 CMakePresets.json 里那份；
#   3. triplet 写死成 mingw，用 msvc preset 构建时找的是不存在的库。
# 一律删掉，交给 preset 的 toolchain + VCPKG_TARGET_TRIPLET 决定。
find_package(fmt CONFIG REQUIRED)

add_executable(${PROJECT_NAME} main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE fmt::fmt)

# 运行期 DLL 跟着 exe 走。TARGET_RUNTIME_DLLS 由 CMake 从链接的 imported target
# 上自己算出来，不用手写文件名，也不用区分 Debug 的 libfmtd / Release 的 libfmt。
# 空列表时 copy_if_different 会报参数不足，所以套一层 $<$<BOOL:...>> 判空。
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E $<IF:$<BOOL:$<TARGET_RUNTIME_DLLS:${PROJECT_NAME}>>,copy_if_different,true>
            $<TARGET_RUNTIME_DLLS:${PROJECT_NAME}> $<TARGET_FILE_DIR:${PROJECT_NAME}>
    COMMAND_EXPAND_LISTS
    COMMENT "Copying runtime DLLs next to ${PROJECT_NAME}"
)
