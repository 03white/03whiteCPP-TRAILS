project(cmathlib)
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/build/lib)
add_library(${PROJECT_NAME} STATIC math.cpp)
