cmake_minimum_required(VERSION 3.5)
project(cpp_trail)

# 源文件统一 UTF-8 无 BOM。MSVC 默认按系统代码页(936/GBK)读源文件，
# 中文注释的 UTF-8 字节会被当成 GBK 双字节字符，行尾那个 lead byte
# 会把换行符一起吞掉 —— 相邻两行粘成一行，下一行代码变成注释的一部分。
# /utf-8 = /source-charset:utf-8 + /execution-charset:utf-8。
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

# MSVC 不给 /std 就按 C++14 编译。之前只有 12-net/13-windows 自己写了
# target_compile_features(... cxx_std_17)，别的专题全在 C++14 上——写个
# std::string_view 就报 "no member named 'string_view' in namespace 'std'"，
# clangd 从 compile_commands.json 里读到的也是同一条缺 /std 的命令，
# 于是编辑器里跟着一起飘红。在这里兜底，子目录里那些 cxx_std_17 留着当显式声明。
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 必须在 add_subdirectory 之前调用，否则子目录里的 add_test() 不生效。
#   ctest --test-dir build/msvc-debug --output-on-failure
enable_testing()

add_subdirectory(src)
