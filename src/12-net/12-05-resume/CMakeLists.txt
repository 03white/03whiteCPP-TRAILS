project(12-05-resume)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/build/bin)
find_package(CURL CONFIG REQUIRED)
add_executable(${PROJECT_NAME}
    main.cpp
    downloader.cpp
    CurlEasy.cpp
    # 新的实现文件加在这里
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)
target_link_libraries(${PROJECT_NAME} PRIVATE CURL::libcurl)

