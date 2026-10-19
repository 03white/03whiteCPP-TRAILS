project(05-01-Observer)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/build/bin)

# 每写完一课，把对应的 LessonNN 从注释里挪进列表。
# Lesson01/02.cpp 现在是 0 字节空文件，挂上去没有 main()，链接期必炸。
foreach(lesson Lesson03) # Lesson01 Lesson02
    add_executable(${PROJECT_NAME}-${lesson} ${lesson}.cpp)
    target_compile_features(${PROJECT_NAME}-${lesson} PRIVATE cxx_std_17)
endforeach()
