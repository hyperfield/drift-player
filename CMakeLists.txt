cmake_minimum_required(VERSION 3.21)

project(DriftPlayer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGLWidgets Concurrent)

find_package(PkgConfig QUIET)

if (PkgConfig_FOUND)
    pkg_check_modules(MPV IMPORTED_TARGET QUIET mpv)
    if (NOT TARGET PkgConfig::MPV)
        pkg_check_modules(MPV IMPORTED_TARGET QUIET libmpv)
    endif()
    if (TARGET PkgConfig::MPV AND NOT TARGET MPV::MPV)
        add_library(MPV::MPV ALIAS PkgConfig::MPV)
    endif()
endif()

if (NOT TARGET MPV::MPV)
    set(_MPV_INCLUDE_HINTS)
    set(_MPV_LIBRARY_HINTS)

    if (DEFINED MPV_ROOT)
        list(APPEND _MPV_INCLUDE_HINTS "${MPV_ROOT}/include")
        list(APPEND _MPV_LIBRARY_HINTS "${MPV_ROOT}/lib")
    endif()

    list(APPEND _MPV_INCLUDE_HINTS
        /opt/homebrew/include
        /usr/local/include
        /opt/local/include
    )

    list(APPEND _MPV_LIBRARY_HINTS
        /opt/homebrew/lib
        /usr/local/lib
        /opt/local/lib
    )

    find_path(MPV_INCLUDE_DIR
        NAMES mpv/client.h
        HINTS ${_MPV_INCLUDE_HINTS}
    )

    find_library(MPV_LIBRARY
        NAMES mpv libmpv
        HINTS ${_MPV_LIBRARY_HINTS}
    )

    if (MPV_INCLUDE_DIR AND MPV_LIBRARY)
        add_library(MPV::MPV UNKNOWN IMPORTED)
        set_target_properties(MPV::MPV PROPERTIES
            IMPORTED_LOCATION "${MPV_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${MPV_INCLUDE_DIR}"
        )
    endif()
endif()

if (NOT TARGET MPV::MPV)
    message(FATAL_ERROR
        "libmpv was not found. Install libmpv (e.g. brew install libmpv) "
        "or set MPV_ROOT to the installation prefix."
    )
endif()

add_subdirectory(src)
