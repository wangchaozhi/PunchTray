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
    src/updater.h src/updater.cpp
)

# Release builds pass the git tag (e.g. -DPUNCHTRAY_VERSION=v1.2.3); the updater
# compares it with the latest GitHub release.
set(PUNCHTRAY_VERSION "${PROJECT_VERSION}" CACHE STRING "Version reported by the app")
string(REGEX REPLACE "^[vV]" "" PUNCHTRAY_VERSION_NUMBER "${PUNCHTRAY_VERSION}")
target_compile_definitions(PunchTray PRIVATE PUNCHTRAY_VERSION="${PUNCHTRAY_VERSION_NUMBER}")

target_link_libraries(PunchTray PRIVATE Qt6::Widgets Qt6::Network)

if(WIN32)
    target_link_libraries(PunchTray PRIVATE user32 dwmapi)
endif()

if(APPLE)
    # Menu-bar only app: no Dock icon.
    set_target_properties(PunchTray PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER com.local.punchtray
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PUNCHTRAY_VERSION_NUMBER}
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in)
endif()
