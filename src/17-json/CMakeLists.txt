project(17-json)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/build/bin)

# nlohmann_json 走 CONFIG 模式（vcpkg 装出来的是 share/nlohmann_json/*.cmake），
# 注意包名带下划线，而 vcpkg 的 port 名是带连字符的 nlohmann-json —— 装的时候写
# 连字符，find_package 里写下划线，写错就是 "Could not find a package configuration file"。
# 给出的 imported target 只有一个：
#   nlohmann_json::nlohmann_json —— INTERFACE 库，header-only，只带 include 路径
find_package(nlohmann_json CONFIG REQUIRED)

# reflect-cpp 走 CONFIG 模式。三处名字别搞混：
#   vcpkg port 名     ：reflectcpp        （vcpkg install reflectcpp）
#   find_package 名   ：reflectcpp        （和 port 名一致，不带下划线/连字符之分）
#   imported target 名：reflectcpp::reflectcpp
#   include 目录/命名空间：rfl            （#include <rfl/json.hpp>，rfl::json）
# 它自己会 find_dependency(ctre)、find_dependency(yyjson)，所以只要 vcpkg 装好了
# reflectcpp，这两个也会跟着进来，不用单独 find_package。
# 重要：reflect-cpp 大量使用 C++20 concepts/consteval，必须用 /std:c++20 编译；
# 下面每个 lesson 的 cxx_std 因此从 17 提到 20（nlohmann 在 C++20 下照常可用）。
find_package(reflectcpp CONFIG REQUIRED)

# 每写完一课，把对应的 lessonNN 从注释里挪出来。
# 这里额外加了 EXISTS 判断：文件还没建时静默跳过，不至于 configure 就炸。
set(LESSONS
    lesson01        # 创建与访问对象
    lesson02        # 解析与序列化
    lesson03        # 与 STL 互转：vector/map/optional 的隐式转换，以及数组/对象的迭代
    lesson04        # 修改Json值    
    lesson05        # 自定义类型：to_json/from_json + ADL，NLOHMANN_DEFINE_TYPE_* 宏
    lesson06        # reflect-cpp：编译期反射自动序列化/反序列化，对比 lesson05 的手写方式
    # lesson07      # 错误处理：parse_error/type_error/out_of_range，以及无异常的 parse(..., nullptr, false)
    # lesson08      # 流式：SAX 接口 sax_parse，大文件边读边解析，不把整棵树塞进内存
    # lesson09      # 寻址与打补丁：JSON Pointer、JSON Patch(RFC 6902)、Merge Patch(RFC 7386)
    # lesson10      # 二进制格式：to_cbor / to_msgpack / to_bson 与对应的 from_*，比一比体积
)

foreach(lesson IN LISTS LESSONS)
    if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${lesson}.cpp)
        message(STATUS "17-json: 跳过 ${lesson}.cpp（文件还不存在）")
        continue()
    endif()

    set(tgt ${PROJECT_NAME}-${lesson})
    add_executable(${tgt} ${lesson}.cpp)
    target_compile_features(${tgt} PRIVATE cxx_std_20)
    target_link_libraries(${tgt} PRIVATE nlohmann_json::nlohmann_json reflectcpp::reflectcpp)
endforeach()

# 这里不用自己写 TARGET_RUNTIME_DLLS 的 POST_BUILD 拷贝：nlohmann_json / ctre 是
# header-only，reflect-cpp 在 x64-windows 下会带一个 yyjson.dll，vcpkg 工具链的
# applocal 机制会自动把它拷到 exe 旁边（16-openssl / 12-fmt 里那段是手动版）。

# nlohmann_json 的版本号变量叫 nlohmann_json_VERSION（由它的 ConfigVersion 文件写入），
# include 路径 header-only 库不导出成变量，要看就从 imported target 上取。
get_target_property(_njson_inc nlohmann_json::nlohmann_json INTERFACE_INCLUDE_DIRECTORIES)
message(STATUS "17-json: nlohmann_json ${nlohmann_json_VERSION} @ ${_njson_inc}")

# reflectcpp 只有 reflectcpp-version.cmake，没有 config-version 文件，所以拿不到
# reflectcpp_VERSION 变量，这里只打印 include 路径，确认找到的是 rfl 那个包。
get_target_property(_rfl_inc reflectcpp::reflectcpp INTERFACE_INCLUDE_DIRECTORIES)
message(STATUS "17-json: reflectcpp @ ${_rfl_inc}")
