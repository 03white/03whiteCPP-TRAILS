project(04-template)
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/build/bin)
add_executable(${PROJECT_NAME} main.cpp test01.hpp)
add_subdirectory(04-01-CRTP)
