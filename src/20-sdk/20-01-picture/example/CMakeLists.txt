project(imgpipe-examples)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_BINARY_DIR}/bin)

foreach(demo basic cancel)
    add_executable(imgpipe-example-${demo} ${demo}.cpp)
    target_link_libraries(imgpipe-example-${demo} PRIVATE 20-01-picture)
    target_compile_features(imgpipe-example-${demo} PRIVATE cxx_std_17)
    target_compile_options(imgpipe-example-${demo} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/EHsc>)
endforeach()
