project(14-io)
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/build/bin)

# 每完成一题，把对应的 qN 从注释里挪进 foreach 的列表。
# 不能一次全挂上——q2..q8 现在是空文件，没有 main，链接会直接失败。
foreach(q q1) #  q2 q3 q4 q5 q6 q7 q8
    add_executable(14-io-${q} ${q}.cc)
endforeach()
