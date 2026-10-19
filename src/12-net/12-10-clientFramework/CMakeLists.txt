project(12-10-clientFramework)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/build/bin)

find_package(CURL CONFIG REQUIRED)

# client.hpp 是 header-only，不往源文件列表里列（头文件不参与编译，
# 列出来只影响 IDE 的文件树分组）。
add_executable(${PROJECT_NAME}
    main.cpp
    # 新的实现文件加在这里
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)
target_link_libraries(${PROJECT_NAME} PRIVATE CURL::libcurl)
