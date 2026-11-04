cmake_minimum_required(VERSION 3.20)
project(hyprfm VERSION 0.6.1 LANGUAGES CXX)

include(GNUInstallDirs)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0 gio-unix-2.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 DBus Svg Network Concurrent)
find_package(KF6WindowSystem QUIET)

# Built-in terminal on libghostty-vt, Ghostty's terminal core (MIT). Its C API
# is only on Ghostty's main branch so far, hence the pinned commit; building it
# needs Zig 0.17 on PATH, so the terminal is left out where there is no zig.
find_program(HYPRFM_ZIG zig)
if(HYPRFM_ZIG)
    set(hyprfm_terminal_default ON)
else()
    set(hyprfm_terminal_default OFF)
endif()
option(HYPRFM_TERMINAL "Built-in terminal (libghostty-vt, needs zig)" ${hyprfm_terminal_default})
if(HYPRFM_TERMINAL)
    enable_language(C)
    include(FetchContent)
    set(ZIG_EXECUTABLE "${HYPRFM_ZIG}" CACHE FILEPATH "Zig compiler for libghostty-vt")
    # The wrapper only optimises release build types; an unoptimised terminal
    # core is too slow to type into, whatever HyprFM itself is built as.
    if(NOT CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo|MinSizeRel)$")
        set(GHOSTTY_ZIG_BUILD_FLAGS "-Doptimize=ReleaseFast" CACHE STRING "" FORCE)
    endif()
    FetchContent_Declare(ghostty
        URL https://github.com/ghostty-org/ghostty/archive/da5936c2287cd7f466acdad3cc000de5ce35ccaa.tar.gz
        URL_HASH SHA256=aba3c7bd4488cf2915edc8abe03c06e0057c2a8d820dbb34ee424d527bc88855
        # Only the static library HyprFM links; none of Ghostty's install rules.
        EXCLUDE_FROM_ALL)
    FetchContent_MakeAvailable(ghostty)
endif()

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
