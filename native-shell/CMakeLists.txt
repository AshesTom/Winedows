# SPDX-License-Identifier: LGPL-2.1-or-later
# The XP shell as a Linux program (native-shell/build.sh builds it into
# .local/native-shell): no Wine, no XP code; XP's files only give it resources.
cmake_minimum_required(VERSION 3.21)
project(xp-native-shell LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets)
find_package(PkgConfig REQUIRED)
pkg_check_modules(XCB REQUIRED IMPORTED_TARGET xcb xcb-xkb)

add_executable(xp-native-shell
    src/main.cpp
    src/acs.cpp src/acs.h
    src/applets.cpp src/applets.h
    src/controlpanel.cpp src/controlpanel.h
    src/desktop.cpp src/desktop.h
    src/filebrowser.cpp src/filebrowser.h
    src/fileicons.cpp src/fileicons.h
    src/fileops.cpp src/fileops.h
    src/searchcompanion.cpp src/searchcompanion.h
    src/taskpane.cpp src/taskpane.h
    src/desktopicons.cpp src/desktopicons.h
    src/displayproperties.cpp src/displayproperties.h
    src/frames.cpp src/frames.h
    src/ipcfile.h
    src/startmenu.cpp src/startmenu.h
    src/theme.cpp src/theme.h
    src/xpdialog.cpp src/xpdialog.h
    src/programs.cpp src/programs.h
    src/systemicons.cpp src/systemicons.h
    src/taskbar.cpp src/taskbar.h
    src/tray.cpp src/tray.h
    src/windowlist.cpp src/windowlist.h
    src/x11.cpp src/x11.h
    src/xpfiles.cpp src/xpfiles.h
    # The reader of XP's visual styles and of the prefix's look, shared with the Qt style.
    ../qt-style/src/msstyles.cpp ../qt-style/src/msstyles.h
    ../qt-style/src/session.cpp ../qt-style/src/session.h)
target_include_directories(xp-native-shell PRIVATE ../qt-style/src)
target_link_libraries(xp-native-shell PRIVATE Qt6::Widgets PkgConfig::XCB)
target_compile_options(xp-native-shell PRIVATE -Wall -Wextra)
# Web pages inside a folder window, as XP has; without the module they open in the desktop's browser.
find_package(Qt6 QUIET COMPONENTS WebEngineWidgets)
if(Qt6WebEngineWidgets_FOUND)
    target_link_libraries(xp-native-shell PRIVATE Qt6::WebEngineWidgets)
    target_compile_definitions(xp-native-shell PRIVATE XP_WEB_PAGES)
endif()

install(TARGETS xp-native-shell RUNTIME DESTINATION bin)
