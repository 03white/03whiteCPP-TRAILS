cmake_minimum_required(VERSION 3.5)
project(cpp_trail)

# 源文件统一 UTF-8 无 BOM。MSVC 默认按系统代码页(936/GBK)读源文件，
# 中文注释的 UTF-8 字节会被当成 GBK 双字节字符，行尾那个 lead byte
# 会把换行符一起吞掉 —— 相邻两行粘成一行，下一行代码变成注释的一部分。
# /utf-8 = /source-charset:utf-8 + /execution-charset:utf-8。
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

add_subdirectory(src)
