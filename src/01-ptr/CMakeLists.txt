project(01-ptr)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/build/bin)

# 一个智能指针一个可执行文件。写完一个就从注释里挪进列表。
#   shared_ptr.cpp  —— 还是 0 字节空文件
#   unique_ptr.cpp  —— 只有一个 test()，还没写 main()，挂上去链接期报
#                      "unresolved external symbol main"
foreach(demo weak_ptr) # shared_ptr unique_ptr
    add_executable(${PROJECT_NAME}-${demo} ${demo}.cpp)
    target_compile_features(${PROJECT_NAME}-${demo} PRIVATE cxx_std_17)
endforeach()
