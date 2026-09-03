project(02-02-stl_using)
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/build/bin)
# 一个知识点一个可执行文件。新加 .cc/.cpp 记得往这里挂 target——
# 不挂的话 clangd 在 compile_commands.json 里查无此文件，会退回去"推断"
# 邻近文件的编译命令，编辑器里的报错就跟真实构建对不上了。
add_executable(${PROJECT_NAME} hash.cpp)
add_executable(${PROJECT_NAME}-string string.cpp)
add_executable(${PROJECT_NAME}-stringview stringview.cpp)
