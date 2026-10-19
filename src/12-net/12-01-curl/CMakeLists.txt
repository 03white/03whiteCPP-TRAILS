project(12-01-curl)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/build/bin)

# CURL::libcurl 这个 imported target 自带头文件路径、库路径，
# 以及 Debug 配置下的 libcurl-d.lib 选择。详见 ../curl.md 第 4 节。
find_package(CURL CONFIG REQUIRED)

add_executable(${PROJECT_NAME} main.cpp)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)
target_link_libraries(${PROJECT_NAME} PRIVATE CURL::libcurl)
