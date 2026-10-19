project(05-02-Plugin)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/build/bin)

# 这个 demo 的全部意义在于「宿主编译期完全不认识插件」：
# host 只 #include IPlugin.h 这个纯虚接口，运行期用 LoadLibrary 扫目录、
# GetProcAddress 取 CreatePlugin/DestroyPlugin。所以插件必须是独立的 DLL，
# 绝不能和 host 编进同一个 target —— 一旦编进去，符号在链接期就定死了，
# 「可插拔」就无从谈起。

add_executable(${PROJECT_NAME}-host host.cpp)
target_compile_features(${PROJECT_NAME}-host PRIVATE cxx_std_17)

# host.cpp 用了 <filesystem>。GCC 9 之前要额外链 stdc++fs，MSVC 和新版 GCC
# 都已并入标准库，这里不用管。
#
# 插件导出入口靠 IPlugin.h 里的 __declspec(dllexport)，不是 CMake 的
# WINDOWS_EXPORT_ALL_SYMBOLS，所以不需要额外开关。
foreach(p add mul)
    add_library(${PROJECT_NAME}-${p} SHARED plugin_${p}.cpp)
    target_compile_features(${PROJECT_NAME}-${p} PRIVATE cxx_std_17)

    set_target_properties(${PROJECT_NAME}-${p} PROPERTIES
        # host.cpp 不带参数时默认扫当前工作目录下的 plugins/，
        # 所以 DLL 直接产在 host.exe 旁边的 plugins/ 里，cd 过去就能跑。
        # 注意：DLL 在 CMake 眼里是 RUNTIME 产物（.lib 导入库才是 ARCHIVE），
        # 所以这里设 RUNTIME_OUTPUT_DIRECTORY 而不是 LIBRARY_OUTPUT_DIRECTORY。
        RUNTIME_OUTPUT_DIRECTORY ${EXECUTABLE_OUTPUT_PATH}/plugins
        # 去掉 target 名前缀，让产物叫 plugin_add.dll 而不是 05-02-Plugin-add.dll。
        # PREFIX "" 顺带压掉 MinGW 默认加的 lib 前缀，两个编译器产出同名。
        PREFIX ""
        OUTPUT_NAME plugin_${p}
    )
endforeach()
