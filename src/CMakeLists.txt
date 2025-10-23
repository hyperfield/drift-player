qt_add_executable(bvplayer
    main.cpp
    MainWindow.cpp
    VideoBackgroundWidget.cpp
    ../include/MainWindow.h
    ../include/VideoBackgroundWidget.h
)

target_include_directories(bvplayer
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(bvplayer
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::OpenGLWidgets
        PkgConfig::MPV
)

set_target_properties(bvplayer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_BINARY_DIR}
)

if (WIN32)
    set_property(TARGET bvplayer APPEND PROPERTY WIN32_EXECUTABLE TRUE)
endif()
