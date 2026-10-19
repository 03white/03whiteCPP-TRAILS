project(02-02-stl_using)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/build/bin)

# 一个知识点一个可执行文件。新加 .cpp 记得往列表里挂——
# 不挂的话 clangd 在 compile_commands.json 里查无此文件，会退回去"推断"
# 邻近文件的编译命令，编辑器里的报错就跟真实构建对不上了。
# （invoke.cpp 之前就是漏挂的。）
foreach(demo hash invoke string stringview)
    add_executable(${PROJECT_NAME}-${demo} ${demo}.cpp)
    target_compile_features(${PROJECT_NAME}-${demo} PRIVATE cxx_std_17)
endforeach()
