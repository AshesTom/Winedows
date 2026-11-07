# SPDX-License-Identifier: LGPL-2.1-or-later
# The XP shell as a Linux program (native-shell/build.sh builds it into
# .local/native-shell): no Wine, no XP code; XP's files only give it resources.
cmake_minimum_required(VERSION 3.21)
project(explorer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets DBus)
find_package(PkgConfig REQUIRED)
pkg_check_modules(XCB REQUIRED IMPORTED_TARGET xcb xcb-xkb xcb-screensaver)

# Everything the shell's programs share; each program is a main() of its own over it, as XP's
# explorer.exe, notepad.exe, calc.exe and cmd.exe are files of their own.
add_library(xpshell STATIC
    src/xpapp.cpp src/xpapp.h
    src/acs.cpp src/acs.h
    src/applets.cpp src/applets.h
    src/balloon.cpp src/balloon.h
    src/calculator.cpp src/calculator.h
    src/console.cpp src/console.h
    src/contexthelp.cpp src/contexthelp.h
    src/editmenu.cpp src/editmenu.h
    src/controlpanel.cpp src/controlpanel.h
    src/desktop.cpp src/desktop.h
    src/filebrowser.cpp src/filebrowser.h
    src/folderoptions.cpp src/folderoptions.h
    src/shellmenus.cpp src/shellmenus.h
    src/fileicons.cpp src/fileicons.h
    src/fileops.cpp src/fileops.h
    src/fileproperties.cpp src/fileproperties.h
    src/foldercustomize.cpp src/foldercustomize.h
    src/notepad.cpp src/notepad.h
    src/pictures.cpp src/pictures.h
    src/searchcompanion.cpp src/searchcompanion.h
    src/sessiondialog.cpp src/sessiondialog.h
    src/sounds.cpp src/sounds.h
    src/taskpane.cpp src/taskpane.h
    src/desktopicons.cpp src/desktopicons.h
    src/displayproperties.cpp src/displayproperties.h
    src/frames.cpp src/frames.h
    src/menushadow.cpp src/menushadow.h
    src/captionanimation.cpp src/captionanimation.h
    src/ipcfile.h
    src/startmenu.cpp src/startmenu.h
    src/theme.cpp src/theme.h
    src/xpdialog.cpp src/xpdialog.h
    src/notifications.cpp src/notifications.h
    src/programs.cpp src/programs.h
    src/securitycenter.cpp src/securitycenter.h src/securitywindow.cpp
    src/systemicons.cpp src/systemicons.h
    src/taskbar.cpp src/taskbar.h
    src/taskbarproperties.cpp src/taskbarproperties.h
    src/arrange.cpp src/arrange.h
    src/tray.cpp src/tray.h
    src/windowlist.cpp src/windowlist.h
    src/x11.cpp src/x11.h
    src/xpfiles.cpp src/xpfiles.h
    src/xpprogram.cpp src/xpprogram.h
    # The reader of XP's visual styles and of the prefix's look, shared with the Qt style.
    ../qt-style/src/msstyles.cpp ../qt-style/src/msstyles.h
    ../qt-style/src/session.cpp ../qt-style/src/session.h)
target_include_directories(xpshell PUBLIC src ../qt-style/src)
target_link_libraries(xpshell PUBLIC Qt6::Widgets Qt6::DBus PkgConfig::XCB)
target_compile_options(xpshell PRIVATE -Wall -Wextra)
# Web pages inside a folder window, as XP has; without the module they open in the desktop's browser.
find_package(Qt6 QUIET COMPONENTS WebEngineWidgets)
if(Qt6WebEngineWidgets_FOUND)
    target_link_libraries(xpshell PUBLIC Qt6::WebEngineWidgets)
    target_compile_definitions(xpshell PUBLIC XP_WEB_PAGES)
endif()

# Notepad's Page Setup and Print; without the module they are greyed.
find_package(Qt6 QUIET COMPONENTS PrintSupport)
if(Qt6PrintSupport_FOUND)
    target_link_libraries(xpshell PUBLIC Qt6::PrintSupport)
    target_compile_definitions(xpshell PUBLIC XP_PRINTING)
endif()
# The Calculator's 32 digits, as XP's; without libquadmath, long double's 18.
include(CheckIncludeFileCXX)
check_include_file_cxx(quadmath.h HAVE_QUADMATH_H)
if(HAVE_QUADMATH_H)
    target_link_libraries(xpshell PUBLIC quadmath)
    target_compile_definitions(xpshell PUBLIC XP_QUAD)
endif()

# Windows Media Player, a program of its own as on XP: Qt Multimedia plays, Qt's JavaScript engine
# runs the skins' scripts. The shell starts it from All Programs and for music and videos.
find_package(Qt6 QUIET COMPONENTS Multimedia Qml)
if(Qt6Multimedia_FOUND AND Qt6Qml_FOUND)
    add_executable(wmplayer
        src/wmplayer.cpp
        src/menushadow.cpp src/menushadow.h
        src/x11.cpp src/x11.h
        src/mediaplayer.cpp src/mediaplayer.h
        src/wmpengine.cpp src/wmpengine.h
        src/wmpskin.cpp src/wmpskin.h
        src/xpfiles.cpp src/xpfiles.h
        src/xpprogram.cpp src/xpprogram.h
        src/xpdialog.cpp src/xpdialog.h
        src/theme.cpp src/theme.h
        ../qt-style/src/msstyles.cpp ../qt-style/src/msstyles.h
        ../qt-style/src/session.cpp ../qt-style/src/session.h)
    target_include_directories(wmplayer PRIVATE ../qt-style/src)
    target_link_libraries(wmplayer PRIVATE Qt6::Widgets Qt6::Multimedia Qt6::Qml PkgConfig::XCB)
    target_compile_options(wmplayer PRIVATE -Wall -Wextra)
    target_compile_definitions(xpshell PUBLIC XP_MEDIA_PLAYER)
    install(TARGETS wmplayer RUNTIME DESTINATION bin)
endif()

# The shell, and XP's programs that are files of their own beside it
add_executable(explorer src/main.cpp)
add_executable(notepad apps/notepad.cpp)
add_executable(calc apps/calc.cpp)
add_executable(cmd apps/cmd.cpp)
add_executable(shimgvw apps/shimgvw.cpp)
foreach(program explorer notepad calc cmd shimgvw)
    target_link_libraries(${program} PRIVATE xpshell)
    target_compile_options(${program} PRIVATE -Wall -Wextra)
endforeach()
install(TARGETS explorer notepad calc cmd shimgvw RUNTIME DESTINATION bin)
install(FILES xp-cmd.bashrc DESTINATION share/win4wine)
