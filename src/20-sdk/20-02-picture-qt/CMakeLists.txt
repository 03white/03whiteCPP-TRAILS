project(20-02-picture-qt LANGUAGES CXX)

# 本机 Qt 6.5.3（MSVC x64，只有 release 版）。换机器时改这里或用 -DQt6_DIR 指定。
if(EXISTS "D:/Qt/6.5.3-msvc-x64/lib/cmake/Qt6")
    list(APPEND CMAKE_PREFIX_PATH "D:/Qt/6.5.3-msvc-x64")
endif()

find_package(Qt6 QUIET COMPONENTS Widgets)
if(NOT Qt6_FOUND)
    message(STATUS "20-02-picture-qt: skipped (Qt6 not found)")
    return()
endif()

qt_standard_project_setup()

qt_add_executable(picture_qt_demo
    main.cpp
    mainwindow.cpp
    mainwindow.h
)

target_link_libraries(picture_qt_demo PRIVATE 20-01-picture Qt6::Widgets)
target_compile_features(picture_qt_demo PRIVATE cxx_std_17)
target_compile_options(picture_qt_demo PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/EHsc>
    /utf-8
)

# 自动把 Qt 运行库/插件拷到 exe 旁边，双击即可运行
if(WIN32)
    get_filename_component(_qt_bin "${Qt6_DIR}/../../../bin" ABSOLUTE)
    find_program(WINDEPLOYQT_EXE windeployqt HINTS "${_qt_bin}")
    if(WINDEPLOYQT_EXE)
        add_custom_command(TARGET picture_qt_demo POST_BUILD
            COMMAND "${WINDEPLOYQT_EXE}" --no-translations --no-opengl-sw
                    "$<TARGET_FILE:picture_qt_demo>"
            COMMENT "Deploying Qt runtime"
        )
    endif()
endif()
