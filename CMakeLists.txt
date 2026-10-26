cmake_minimum_required(VERSION 3.20)
project(hyprfm VERSION 0.6.1 LANGUAGES CXX)

# Windows uses Win32 directly: no GLib, no D-Bus, nothing to ship beside Qt.
if(NOT WIN32)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Svg Network Concurrent)
if(NOT WIN32)
    find_package(Qt6 REQUIRED COMPONENTS DBus)
    find_package(KF6WindowSystem QUIET)
endif()

if(MSVC)
    add_compile_options(/utf-8 /Zc:__cplusplus /permissive-)
endif()
if(WIN32)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)

    # SFTP on Windows (Linux has GVFS): libssh2 on Windows' own CNG crypto,
    # linked statically, so the package gains no OpenSSL or other DLLs.
    enable_language(C)
    include(FetchContent)
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)   # let the set()s below win over its option()s
    # A cache entry: libssh2's own CACHE declaration would otherwise replace
    # a plain set() with its empty default, and it then auto-detects any
    # OpenSSL it finds (the CI runner has one), making hyprfm.exe need
    # libcrypto-3-x64.dll.
    set(CRYPTO_BACKEND WinCNG CACHE STRING "" FORCE)
    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_STATIC_LIBS ON)
    set(BUILD_EXAMPLES OFF)
    set(BUILD_TESTING OFF)
    set(ENABLE_ZLIB_COMPRESSION OFF)
    FetchContent_Declare(libssh2
        URL https://github.com/libssh2/libssh2/releases/download/libssh2-1.11.1/libssh2-1.11.1.tar.gz
        URL_HASH SHA256=d9ec76cbe34db98eec3539fe2c899d26b0c837cb3eb466a56b0f109cabf658f7
        EXCLUDE_FROM_ALL)
    FetchContent_MakeAvailable(libssh2)
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
