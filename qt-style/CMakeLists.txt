# SPDX-License-Identifier: LGPL-2.1-or-later
# The Qt 6 style that draws Linux applications with the XP shell's visual
# style (qt-style/build.sh builds it into .local/qt-style).
cmake_minimum_required(VERSION 3.21)
project(win4wine-qt-style LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets)

add_library(win4wine-xp-core STATIC
    src/msstyles.cpp src/msstyles.h
    src/session.cpp src/session.h
    src/xpstyle.cpp src/xpstyle.h)
target_link_libraries(win4wine-xp-core PUBLIC Qt6::Widgets)
target_compile_options(win4wine-xp-core PRIVATE -Wall -Wextra)

add_library(win4wine-xp MODULE src/plugin.cpp src/win4wine-xp.json)
target_link_libraries(win4wine-xp PRIVATE win4wine-xp-core)
set_target_properties(win4wine-xp PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/styles
    PREFIX "lib")

find_package(PkgConfig REQUIRED)
pkg_check_modules(XCB REQUIRED IMPORTED_TARGET xcb)
add_executable(xpstyle-gtk tools/gtktheme.cpp)
target_link_libraries(xpstyle-gtk PRIVATE win4wine-xp-core PkgConfig::XCB)

# The GTK 3 module that lays XP's parts over the applications' own CSS
pkg_check_modules(GTK3 IMPORTED_TARGET gtk+-3.0)
if(GTK3_FOUND)
    enable_language(C)
    add_library(win4wine-xp-gtk MODULE gtk-module/win4wine-xp-gtk.c)
    target_link_libraries(win4wine-xp-gtk PRIVATE PkgConfig::GTK3)
    target_compile_options(win4wine-xp-gtk PRIVATE -Wall -Wextra)
    install(TARGETS win4wine-xp-gtk LIBRARY DESTINATION gtk-3.0/modules)
endif()

add_executable(xpstyle-gallery tools/gallery.cpp)
target_link_libraries(xpstyle-gallery PRIVATE win4wine-xp-core)

install(TARGETS win4wine-xp LIBRARY DESTINATION styles)
# Qt Quick's controls style of the same name (QML_IMPORT_PATH)
install(DIRECTORY qml/Win4WineXP DESTINATION qml)
install(TARGETS xpstyle-gallery xpstyle-gtk RUNTIME DESTINATION bin)
