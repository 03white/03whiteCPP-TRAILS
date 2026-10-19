project(04-template)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/build/bin)

# .hpp 不往 add_executable 里列：头文件不参与编译，列出来只影响 IDE 的
# 文件树分组。test01.hpp / test02.hpp / tmp.hpp 由 main.cpp 自己 include。
foreach(demo main multiPara)
    add_executable(${PROJECT_NAME}-${demo} ${demo}.cpp)
    target_compile_features(${PROJECT_NAME}-${demo} PRIVATE cxx_std_17)
endforeach()

add_subdirectory(04-01-CRTP)
