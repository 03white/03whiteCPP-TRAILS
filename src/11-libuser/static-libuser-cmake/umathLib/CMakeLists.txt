project(umathlib)
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/build/bin)
# 1.包含所有头文件的目录
include_directories(${PROJECT_SOURCE_DIR}/include)  # 对应 g++ -I
add_executable(${PROJECT_NAME} main.cpp)    
# 3. 链接静态库
link_directories(${PROJECT_SOURCE_DIR}/lib)   #  对应g++ -L路径
target_link_libraries(${PROJECT_NAME} PRIVATE #  对应 g++ -lcmathlib
    cmathlib
)