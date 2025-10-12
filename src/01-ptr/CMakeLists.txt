project(01-ptr)
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/build/bin)
add_executable(${PROJECT_NAME} 
    weak_ptr.cpp
)