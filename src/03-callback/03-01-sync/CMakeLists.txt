project(03-01-sync)
add_executable(${PROJECT_NAME}-Lesson01 )
add_executable(${PROJECT_NAME}-Expend01 )
add_executable(${PROJECT_NAME}-Expend02 )
target_sources(${PROJECT_NAME}-Lesson01 PRIVATE
	Lesson01.cpp
)
target_sources(${PROJECT_NAME}-Expend01 PRIVATE
	Expend01.cpp
)
target_sources(${PROJECT_NAME}-Expend02 PRIVATE
	Expend02.cpp
)
set_target_properties(${PROJECT_NAME}-Lesson01 PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build/bin
)
set_target_properties(${PROJECT_NAME}-Expend01 PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build/bin
)
set_target_properties(${PROJECT_NAME}-Expend02 PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build/bin
)

