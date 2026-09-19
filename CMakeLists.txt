cmake_minimum_required(VERSION 3.20)
project(hyprfm VERSION 0.6.1 LANGUAGES CXX)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0 gio-unix-2.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 DBus Svg Network Concurrent)
find_package(KF6WindowSystem QUIET)

add_subdirectory(src)

# Install themes
install(DIRECTORY themes/ DESTINATION share/hyprfm/themes FILES_MATCHING PATTERN "*.toml")

# Install the generated HyprFM QML module: qmldir + qml/ tree + qmltypes.
# The qmldir keeps Qt's `prefer :/HyprFM/`, so at runtime the module resolves
# to the qmlcachegen-compiled qrc payload and nothing is recompiled on launch
# (a random AppImage mount path defeats the on-disk .qmlc cache every start).
# The on-disk tree is only the fallback main.cpp uses when the qrc load fails,
# as it did with Qt 6.7.3 NO_CACHEGEN in v0.4.14 ("SettingsPanel is not a
# type"); release builds force HYPRFM_ENABLE_QML_CACHEGEN=ON for that reason.
install(DIRECTORY ${CMAKE_BINARY_DIR}/src/HyprFM/
    DESTINATION share/hyprfm/HyprFM
    FILES_MATCHING
        PATTERN "*.qml"
        PATTERN "qmldir"
        PATTERN "*.qmltypes")


option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()
