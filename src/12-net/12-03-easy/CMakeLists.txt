project(12-03-easy)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/build/bin)

# CURL::libcurl 这个 imported target 会自带头文件路径、库路径，
# 以及 Debug 配置下的 libcurl-d.lib 选择。详见 ../curl.md 第 4 节。
# 放在 add_executable 之前，后面每个 target 都能直接 link。
find_package(CURL CONFIG REQUIRED)

add_executable(${PROJECT_NAME} main.cpp)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)
target_link_libraries(${PROJECT_NAME} PRIVATE CURL::libcurl)

add_executable(${PROJECT_NAME}-lesson01 lesson01.cpp)
target_compile_features(${PROJECT_NAME}-lesson01 PRIVATE cxx_std_17)
target_link_libraries(${PROJECT_NAME}-lesson01 PRIVATE CURL::libcurl)

add_executable(${PROJECT_NAME}-lesson02 lesson02.cpp)
target_compile_features(${PROJECT_NAME}-lesson02 PRIVATE cxx_std_17)
target_link_libraries(${PROJECT_NAME}-lesson02 PRIVATE CURL::libcurl)

add_executable(${PROJECT_NAME}-lesson03 lesson03.cpp)
target_compile_features(${PROJECT_NAME}-lesson03 PRIVATE cxx_std_17)
target_link_libraries(${PROJECT_NAME}-lesson03 PRIVATE CURL::libcurl)