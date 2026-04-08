cmake_minimum_required(VERSION 3.20)
project(hyprfm VERSION 0.4.10 LANGUAGES CXX)

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(POPPLER_QT6 QUIET IMPORTED_TARGET poppler-qt6)
    pkg_check_modules(LIBEXIF QUIET IMPORTED_TARGET libexif)
    pkg_check_modules(TAGLIB QUIET IMPORTED_TARGET taglib)
    pkg_check_modules(LIBAVFORMAT QUIET IMPORTED_TARGET libavformat libavcodec libavutil)
endif()
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0 gio-unix-2.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 DBus Widgets SvgWidgets Svg)

add_subdirectory(src)

# Install themes
install(DIRECTORY themes/ DESTINATION share/hyprfm/themes FILES_MATCHING PATTERN "*.toml")

# Install QML sources (Quill module + icons needed at runtime)
install(DIRECTORY src/qml/ DESTINATION share/hyprfm/src/qml)

# Install QML module metadata (generated at build time)
install(FILES ${CMAKE_BINARY_DIR}/src/HyprFM/qmldir DESTINATION share/hyprfm/HyprFM OPTIONAL)

option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()

# CPack .deb packaging
set(CPACK_PACKAGE_NAME "hyprfm")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A lightweight Qt6/QML file manager for Hyprland")
set(CPACK_PACKAGE_CONTACT "Soyeb Pervez Jim <soyeb.jim@gmail.com>")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "libqt6core6t64, libqt6gui6t64, libqt6qml6, libqt6quick6, libqt6dbus6t64, libqt6widgets6t64, libqt6svg6, libqt6svgwidgets6, qml6-module-qtquick, qml6-module-qtquick-controls, qml6-module-qtquick-layouts, qml6-module-qtquick-shapes, qt6-wayland, libglib2.0-0, fd-find, xdg-utils")
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS
    "wl-clipboard, gvfs, rsync")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/soyeb-jim285/hyprfm")
set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
include(CPack)
