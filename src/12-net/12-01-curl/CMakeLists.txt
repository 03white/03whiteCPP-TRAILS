project(12-01-curl)
find_package(CURL CONFIG REQUIRED)
add_executable(${PROJECT_NAME} main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE CURL::libcurl)