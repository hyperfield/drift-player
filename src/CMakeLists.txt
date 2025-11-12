qt_add_executable(drift_player
    MACOSX_BUNDLE
    main.cpp
    MainWindow.cpp
    VideoBackgroundWidget.cpp
    BassVisualizerWidget.cpp
    ../include/MainWindow.h
    ../include/VideoBackgroundWidget.h
    ../include/BassVisualizerWidget.h
)

qt_add_resources(drift_player_resources
    ../resources/icons.qrc
)

target_sources(drift_player PRIVATE ${drift_player_resources})

target_include_directories(drift_player
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_SOURCE_DIR}/third_party/spdlog/include
)

target_link_libraries(drift_player
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::OpenGLWidgets
        Qt6::Concurrent
        MPV::MPV
)

set_target_properties(drift_player PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_BINARY_DIR}
    OUTPUT_NAME "driftplayer"
)

if (WIN32)
    set_property(TARGET drift_player APPEND PROPERTY WIN32_EXECUTABLE TRUE)
    set(APP_ICON_PATH "${CMAKE_SOURCE_DIR}/assets/icons/logo.ico")
    configure_file(${CMAKE_SOURCE_DIR}/resources/appicon.rc.in
                   ${CMAKE_BINARY_DIR}/appicon.rc @ONLY)
    target_sources(drift_player PRIVATE ${CMAKE_BINARY_DIR}/appicon.rc)
endif()

if (APPLE)
    set_source_files_properties(${CMAKE_SOURCE_DIR}/assets/icons/driftplayer.icns PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(drift_player PRIVATE ${CMAKE_SOURCE_DIR}/assets/icons/driftplayer.icns)
    set_target_properties(drift_player PROPERTIES MACOSX_BUNDLE_ICON_FILE driftplayer.icns)
endif()

qt_add_executable(drift_dialog_helper
    AddMediaHelper.cpp
)

target_link_libraries(drift_dialog_helper
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
)

set_target_properties(drift_dialog_helper PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_BINARY_DIR}
)

install(TARGETS drift_player
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(TARGETS drift_dialog_helper
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
