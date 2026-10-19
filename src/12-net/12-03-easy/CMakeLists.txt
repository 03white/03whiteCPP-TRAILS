project(12-03-easy)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/build/bin)

# CURL::libcurl 这个 imported target 会自带头文件路径、库路径，
# 以及 Debug 配置下的 libcurl-d.lib 选择。详见 ../curl.md 第 4 节。
# 放在 add_executable 之前，后面每个 target 都能直接 link。
find_package(CURL CONFIG REQUIRED)

add_executable(${PROJECT_NAME}          main.cpp)
add_executable(${PROJECT_NAME}-lesson01 lesson01.cpp)
add_executable(${PROJECT_NAME}-lesson02 lesson02.cpp)
add_executable(${PROJECT_NAME}-lesson03 lesson03.cpp)

# 四个 target 的编译/链接要求完全一样，统一挂一遍，不用每个重复三行。
foreach(tgt
    ${PROJECT_NAME}
    ${PROJECT_NAME}-lesson01
    ${PROJECT_NAME}-lesson02
    ${PROJECT_NAME}-lesson03
)
    target_compile_features(${tgt} PRIVATE cxx_std_17)
    target_link_libraries(${tgt} PRIVATE CURL::libcurl)
endforeach()
