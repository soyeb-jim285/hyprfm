cmake_minimum_required(VERSION 3.20)
project(hyprfm VERSION 0.4.16 LANGUAGES CXX)

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
find_package(KF6WindowSystem QUIET)

add_subdirectory(src)

# Install themes
install(DIRECTORY themes/ DESTINATION share/hyprfm/themes FILES_MATCHING PATTERN "*.toml")

# Install QML sources (Quill module + icons needed at runtime)
install(DIRECTORY src/qml/ DESTINATION share/hyprfm/src/qml)

# Install the generated HyprFM QML module: qmldir + qml/ tree + qmltypes.
# Without the qml/ subtree on disk, Qt has nothing to fall back on if the
# embedded qrc bundle is missing a file (we hit exactly that with Qt 6.7.3
# in v0.4.14, where SettingsPanel.qml made it into the qmldir but not into
# qrc_hyprfm_raw_qml_0, leaving "SettingsPanel is not a type" at runtime).
install(DIRECTORY ${CMAKE_BINARY_DIR}/src/HyprFM/
    DESTINATION share/hyprfm/HyprFM
    FILES_MATCHING
        PATTERN "*.qml"
        PATTERN "qmldir"
        PATTERN "*.qmltypes")

# Qt generates `prefer :/HyprFM/` in the module qmldir. That makes deployed
# bundles keep resolving HyprFM types from the embedded qrc even when the
# installed on-disk module has the full file set, which is exactly how a
# missing qrc SettingsPanel.qml turns into "SettingsPanel is not a type".
install(CODE [[
    set(_hyprfm_qmldir "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/share/hyprfm/HyprFM/qmldir")
    if(EXISTS "${_hyprfm_qmldir}")
        file(READ "${_hyprfm_qmldir}" _hyprfm_qmldir_contents)
        string(REPLACE "prefer :/HyprFM/\n" "" _hyprfm_qmldir_contents "${_hyprfm_qmldir_contents}")
        string(REPLACE "prefer :/HyprFM/\r\n" "" _hyprfm_qmldir_contents "${_hyprfm_qmldir_contents}")
        file(WRITE "${_hyprfm_qmldir}" "${_hyprfm_qmldir_contents}")
    endif()
]])

option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()
