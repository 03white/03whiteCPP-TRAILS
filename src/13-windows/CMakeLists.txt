project(13-windows)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/build/bin)

add_executable(${PROJECT_NAME}
    main.cpp
    system_snapshot.cpp
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)