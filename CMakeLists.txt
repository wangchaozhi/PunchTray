cmake_minimum_required(VERSION 3.21)
project(PunchTray VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
qt_standard_project_setup()

qt_add_executable(PunchTray WIN32 MACOSX_BUNDLE
    src/main.cpp
    src/config.h src/config.cpp
    src/autostart.h src/autostart.cpp
    src/attendanceclient.h src/attendanceclient.cpp
    src/glasspanel.h src/glasspanel.cpp
    src/trayapp.h src/trayapp.cpp
)

target_link_libraries(PunchTray PRIVATE Qt6::Widgets Qt6::Network)

if(WIN32)
    target_link_libraries(PunchTray PRIVATE user32 dwmapi)
endif()

if(APPLE)
    # Menu-bar only app: no Dock icon.
    set_target_properties(PunchTray PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER com.local.punchtray
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in)
endif()
