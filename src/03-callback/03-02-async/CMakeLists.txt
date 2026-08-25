project(03-02-async)
add_executable(${PROJECT_NAME}-Lesson01 )
add_executable(${PROJECT_NAME}-Lesson02)
target_sources(${PROJECT_NAME}-Lesson01 PRIVATE
	Lesson01.cpp
)
target_sources(${PROJECT_NAME}-Lesson02 PRIVATE
	Lesson02.cpp
)
set_target_properties(${PROJECT_NAME}-Lesson01 PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build/bin
)
set_target_properties(${PROJECT_NAME}-Lesson02 PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build/bin
)
