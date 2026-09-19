# Tests will be added as backend classes are implemented

add_executable(tst_configmanager tst_configmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/services/configmanager.cpp
)
target_include_directories(tst_configmanager PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/third_party
)
target_link_libraries(tst_configmanager PRIVATE Qt6::Test Qt6::Core Qt6::Gui)
add_test(NAME tst_configmanager COMMAND tst_configmanager)

add_executable(tst_themeloader tst_themeloader.cpp
    ${CMAKE_SOURCE_DIR}/src/services/themeloader.cpp
)
target_include_directories(tst_themeloader PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/third_party
)
target_compile_definitions(tst_themeloader PRIVATE
    THEMES_DIR="${CMAKE_SOURCE_DIR}/themes"
)
target_link_libraries(tst_themeloader PRIVATE Qt6::Test Qt6::Core Qt6::Gui)
add_test(NAME tst_themeloader COMMAND tst_themeloader)

add_executable(tst_tabmodel tst_tabmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/models/tabmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/models/tablistmodel.cpp
)
target_include_directories(tst_tabmodel PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tst_tabmodel PRIVATE Qt6::Test Qt6::Core)
add_test(NAME tst_tabmodel COMMAND tst_tabmodel)

add_executable(tst_clipboardmanager tst_clipboardmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/services/clipboardmanager.cpp
)
target_include_directories(tst_clipboardmanager PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tst_clipboardmanager PRIVATE Qt6::Test Qt6::Core Qt6::Gui)
add_test(NAME tst_clipboardmanager COMMAND tst_clipboardmanager)

add_executable(tst_bookmarkmodel tst_bookmarkmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/models/bookmarkmodel.cpp
)
target_include_directories(tst_bookmarkmodel PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tst_bookmarkmodel PRIVATE Qt6::Test Qt6::Core)
add_test(NAME tst_bookmarkmodel COMMAND tst_bookmarkmodel)

add_executable(tst_fileoperations tst_fileoperations.cpp
    ${CMAKE_SOURCE_DIR}/src/services/fileoperations.cpp
    ${CMAKE_SOURCE_DIR}/src/services/xdgtrash.cpp
    ${CMAKE_SOURCE_DIR}/src/services/giotransferworker.cpp
)
target_include_directories(tst_fileoperations PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tst_fileoperations PRIVATE Qt6::Test Qt6::Core Qt6::Gui PkgConfig::GIO)
add_test(NAME tst_fileoperations COMMAND tst_fileoperations)
# Shares gio trash state with tst_giotransferworker; serialize to avoid flakes.
set_tests_properties(tst_fileoperations PROPERTIES
    RUN_SERIAL TRUE
    TIMEOUT 120
)

add_executable(tst_undomanager tst_undomanager.cpp
    ${CMAKE_SOURCE_DIR}/src/services/undomanager.cpp
    ${CMAKE_SOURCE_DIR}/src/services/xdgtrash.cpp
    ${CMAKE_SOURCE_DIR}/src/services/fileoperations.cpp
    ${CMAKE_SOURCE_DIR}/src/services/xdgtrash.cpp
    ${CMAKE_SOURCE_DIR}/src/services/giotransferworker.cpp
)
target_include_directories(tst_undomanager PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tst_undomanager PRIVATE Qt6::Test Qt6::Core Qt6::Gui PkgConfig::GIO)
add_test(NAME tst_undomanager COMMAND tst_undomanager)

add_executable(tst_previewservice tst_previewservice.cpp
    ${CMAKE_SOURCE_DIR}/src/services/previewservice.cpp
    ${CMAKE_SOURCE_DIR}/src/services/metadataextractor.cpp
)
target_include_directories(tst_previewservice PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tst_previewservice PRIVATE Qt6::Test Qt6::Core Qt6::Gui)
add_test(NAME tst_previewservice COMMAND tst_previewservice)

add_executable(tst_rcloneservice tst_rcloneservice.cpp
    ${CMAKE_SOURCE_DIR}/src/services/rcloneservice.cpp
)
target_include_directories(tst_rcloneservice PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tst_rcloneservice PRIVATE Qt6::Test Qt6::Core)
add_test(NAME tst_rcloneservice COMMAND tst_rcloneservice)

add_executable(tst_remoteaccessservice tst_remoteaccessservice.cpp
    ${CMAKE_SOURCE_DIR}/src/services/remoteaccessservice.cpp
)
target_include_directories(tst_remoteaccessservice PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tst_remoteaccessservice PRIVATE Qt6::Test Qt6::Core)
add_test(NAME tst_remoteaccessservice COMMAND tst_remoteaccessservice)

add_executable(tst_diskusageservice tst_diskusageservice.cpp
    ${CMAKE_SOURCE_DIR}/src/services/diskusageservice.cpp
)
target_include_directories(tst_diskusageservice PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tst_diskusageservice PRIVATE Qt6::Test Qt6::Core)
add_test(NAME tst_diskusageservice COMMAND tst_diskusageservice)

# --- New test targets ---

add_executable(tst_filesystemmodel tst_filesystemmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/models/filesystemmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/services/xdgtrash.cpp
    ${CMAKE_SOURCE_DIR}/src/services/gitstatusservice.cpp
)
target_include_directories(tst_filesystemmodel PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/third_party
)
target_link_libraries(tst_filesystemmodel PRIVATE Qt6::Test Qt6::Core Qt6::Gui Qt6::Svg Qt6::Concurrent)
add_test(NAME tst_filesystemmodel COMMAND tst_filesystemmodel)

add_executable(tst_detailedview tst_detailedview.cpp viewharness.h
    ${CMAKE_SOURCE_DIR}/src/services/configmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/services/themeloader.cpp
    ${CMAKE_SOURCE_DIR}/src/models/filesystemmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/services/xdgtrash.cpp
    ${CMAKE_SOURCE_DIR}/src/services/gitstatusservice.cpp
)
target_include_directories(tst_detailedview PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/third_party
)
target_compile_definitions(tst_detailedview PRIVATE
    TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    TEST_MODULE_DIR="${CMAKE_BINARY_DIR}/src"
)
target_link_libraries(tst_detailedview PRIVATE Qt6::Test Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Concurrent PkgConfig::GIO)
add_dependencies(tst_detailedview hyprfm)
add_test(NAME tst_detailedview COMMAND tst_detailedview)

add_executable(tst_millerview tst_millerview.cpp viewharness.h
    ${CMAKE_SOURCE_DIR}/src/services/configmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/services/themeloader.cpp
    ${CMAKE_SOURCE_DIR}/src/models/filesystemmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/services/xdgtrash.cpp
    ${CMAKE_SOURCE_DIR}/src/services/gitstatusservice.cpp
    ${CMAKE_SOURCE_DIR}/src/services/previewservice.cpp
    ${CMAKE_SOURCE_DIR}/src/services/metadataextractor.cpp
    ${CMAKE_SOURCE_DIR}/src/services/runtimefeaturesservice.cpp
)
target_include_directories(tst_millerview PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/third_party
)
target_compile_definitions(tst_millerview PRIVATE
    TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    TEST_MODULE_DIR="${CMAKE_BINARY_DIR}/src"
)
target_link_libraries(tst_millerview PRIVATE Qt6::Test Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Concurrent Qt6::DBus PkgConfig::GIO)
add_dependencies(tst_millerview hyprfm)
add_test(NAME tst_millerview COMMAND tst_millerview)

add_executable(tst_viewcontextmenu tst_viewcontextmenu.cpp viewharness.h
    ${CMAKE_SOURCE_DIR}/src/services/configmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/services/themeloader.cpp
    ${CMAKE_SOURCE_DIR}/src/models/filesystemmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/services/xdgtrash.cpp
    ${CMAKE_SOURCE_DIR}/src/services/gitstatusservice.cpp
    ${CMAKE_SOURCE_DIR}/src/services/previewservice.cpp
    ${CMAKE_SOURCE_DIR}/src/services/metadataextractor.cpp
    ${CMAKE_SOURCE_DIR}/src/services/runtimefeaturesservice.cpp
)
target_include_directories(tst_viewcontextmenu PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/third_party
)
target_compile_definitions(tst_viewcontextmenu PRIVATE
    TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    TEST_MODULE_DIR="${CMAKE_BINARY_DIR}/src"
)
target_link_libraries(tst_viewcontextmenu PRIVATE Qt6::Test Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Concurrent Qt6::DBus PkgConfig::GIO)
add_dependencies(tst_viewcontextmenu hyprfm)
add_test(NAME tst_viewcontextmenu COMMAND tst_viewcontextmenu)

add_executable(tst_toolbar tst_toolbar.cpp viewharness.h
    ${CMAKE_SOURCE_DIR}/src/services/configmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/services/themeloader.cpp
    ${CMAKE_SOURCE_DIR}/src/models/filesystemmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/services/xdgtrash.cpp
    ${CMAKE_SOURCE_DIR}/src/services/gitstatusservice.cpp
    ${CMAKE_SOURCE_DIR}/src/models/tablistmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/models/tabmodel.cpp
)
target_include_directories(tst_toolbar PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/third_party)
target_compile_definitions(tst_toolbar PRIVATE TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}" TEST_MODULE_DIR="${CMAKE_BINARY_DIR}/src")
target_link_libraries(tst_toolbar PRIVATE Qt6::Test Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Concurrent PkgConfig::GIO)
add_dependencies(tst_toolbar hyprfm)
add_test(NAME tst_toolbar COMMAND tst_toolbar)

add_executable(tst_mainwindow tst_mainwindow.cpp
    ${CMAKE_SOURCE_DIR}/src/services/configmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/services/themeloader.cpp
    ${CMAKE_SOURCE_DIR}/src/services/clipboardmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/services/fileoperations.cpp
    ${CMAKE_SOURCE_DIR}/src/services/xdgtrash.cpp
    ${CMAKE_SOURCE_DIR}/src/services/giotransferworker.cpp
    ${CMAKE_SOURCE_DIR}/src/services/draghelper.cpp
    ${CMAKE_SOURCE_DIR}/src/models/tabmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/models/tablistmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/models/bookmarkmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/models/filesystemmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/services/xdgtrash.cpp
    ${CMAKE_SOURCE_DIR}/src/models/devicemodel.cpp
    ${CMAKE_SOURCE_DIR}/src/models/recentfilesmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/models/searchresultsmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/models/searchproxymodel.cpp
    ${CMAKE_SOURCE_DIR}/src/services/searchservice.cpp
    ${CMAKE_SOURCE_DIR}/src/services/undomanager.cpp
    ${CMAKE_SOURCE_DIR}/src/services/xdgtrash.cpp
    ${CMAKE_SOURCE_DIR}/src/services/previewservice.cpp
    ${CMAKE_SOURCE_DIR}/src/services/diskusageservice.cpp
    ${CMAKE_SOURCE_DIR}/src/services/remoteaccessservice.cpp
    ${CMAKE_SOURCE_DIR}/src/services/rcloneservice.cpp
    ${CMAKE_SOURCE_DIR}/src/services/runtimefeaturesservice.cpp
    ${CMAKE_SOURCE_DIR}/src/services/dependencychecker.cpp
    ${CMAKE_SOURCE_DIR}/src/services/gitstatusservice.cpp
    ${CMAKE_SOURCE_DIR}/src/services/sessionstate.cpp
    ${CMAKE_SOURCE_DIR}/src/services/metadataextractor.cpp
    ${CMAKE_SOURCE_DIR}/src/providers/thumbnailprovider.cpp
    ${CMAKE_SOURCE_DIR}/src/providers/pdfpreviewprovider.cpp
)
target_include_directories(tst_mainwindow PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/third_party)
target_compile_definitions(tst_mainwindow PRIVATE
    TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    TEST_MODULE_DIR="${CMAKE_BINARY_DIR}/src"
    HYPRFM_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    HYPRFM_DATA_DIR="${CMAKE_SOURCE_DIR}"
    HYPRFM_VERSION="test"
)
target_link_libraries(tst_mainwindow PRIVATE Qt6::Test Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::DBus Qt6::Svg Qt6::Network Qt6::Concurrent PkgConfig::GIO)
add_dependencies(tst_mainwindow hyprfm)
add_test(NAME tst_mainwindow COMMAND tst_mainwindow)

# The view harnesses assert pixel geometry of a 900x600 window; a tiling
# compositor would resize a real window, so keep them on the offscreen QPA.
set_tests_properties(tst_detailedview tst_millerview tst_viewcontextmenu tst_toolbar tst_mainwindow PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_devicemodel tst_devicemodel.cpp
    ${CMAKE_SOURCE_DIR}/src/models/devicemodel.cpp
)
target_include_directories(tst_devicemodel PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tst_devicemodel PRIVATE Qt6::Test Qt6::Core Qt6::DBus PkgConfig::GIO)
add_test(NAME tst_devicemodel COMMAND tst_devicemodel)

add_executable(tst_iconprovider tst_iconprovider.cpp)
target_include_directories(tst_iconprovider PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tst_iconprovider PRIVATE Qt6::Test Qt6::Core Qt6::Gui Qt6::Quick Qt6::Svg)
add_test(NAME tst_iconprovider COMMAND tst_iconprovider)

add_executable(tst_thumbnailprovider tst_thumbnailprovider.cpp
    ${CMAKE_SOURCE_DIR}/src/providers/thumbnailprovider.cpp
)
target_include_directories(tst_thumbnailprovider PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tst_thumbnailprovider PRIVATE Qt6::Test Qt6::Core Qt6::Gui Qt6::Quick Qt6::Svg)
add_test(NAME tst_thumbnailprovider COMMAND tst_thumbnailprovider)

add_executable(tst_pdfpreviewprovider tst_pdfpreviewprovider.cpp
    ${CMAKE_SOURCE_DIR}/src/providers/pdfpreviewprovider.cpp
)
target_include_directories(tst_pdfpreviewprovider PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tst_pdfpreviewprovider PRIVATE Qt6::Test Qt6::Core Qt6::Gui Qt6::Quick)
add_test(NAME tst_pdfpreviewprovider COMMAND tst_pdfpreviewprovider)

add_executable(tst_searchresultsmodel tst_searchresultsmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/models/searchresultsmodel.cpp
)
target_include_directories(tst_searchresultsmodel PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/third_party
)
target_link_libraries(tst_searchresultsmodel PRIVATE Qt6::Test Qt6::Core Qt6::Gui)
add_test(NAME tst_searchresultsmodel COMMAND tst_searchresultsmodel)

add_executable(tst_searchproxymodel tst_searchproxymodel.cpp
    ${CMAKE_SOURCE_DIR}/src/models/searchproxymodel.cpp
    ${CMAKE_SOURCE_DIR}/src/models/filesystemmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/services/xdgtrash.cpp
    ${CMAKE_SOURCE_DIR}/src/models/searchresultsmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/services/gitstatusservice.cpp
)
target_include_directories(tst_searchproxymodel PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/third_party
)
target_link_libraries(tst_searchproxymodel PRIVATE Qt6::Test Qt6::Core Qt6::Gui Qt6::Svg Qt6::Concurrent)
add_test(NAME tst_searchproxymodel COMMAND tst_searchproxymodel)

add_executable(tst_searchservice tst_searchservice.cpp
    ${CMAKE_SOURCE_DIR}/src/services/searchservice.cpp
    ${CMAKE_SOURCE_DIR}/src/models/searchresultsmodel.cpp
)
target_include_directories(tst_searchservice PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/third_party
)
target_link_libraries(tst_searchservice PRIVATE Qt6::Test Qt6::Core Qt6::Gui)
add_test(NAME tst_searchservice COMMAND tst_searchservice)

add_executable(tst_giotransferworker tst_giotransferworker.cpp
    ${CMAKE_SOURCE_DIR}/src/services/giotransferworker.cpp
)
target_include_directories(tst_giotransferworker PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tst_giotransferworker PRIVATE Qt6::Test Qt6::Core PkgConfig::GIO)
add_test(NAME tst_giotransferworker COMMAND tst_giotransferworker)
# gio spawns child processes and shares trash/mount state with other tests;
# running serially prevents flaky timeouts under `ctest -j`.
set_tests_properties(tst_giotransferworker PROPERTIES
    RUN_SERIAL TRUE
    TIMEOUT 120
)

# Multi-window / single-instance arbitration lives in main(), so it is covered
# by driving the real binary. Skips itself (exit 77) without a Wayland session.
add_test(NAME tst_instance_launch
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tst_instance_launch.sh $<TARGET_FILE:hyprfm>
)
set_tests_properties(tst_instance_launch PROPERTIES
    SKIP_RETURN_CODE 77
    RUN_SERIAL TRUE
    TIMEOUT 120
)

# --help / --version answer before any Qt GUI setup, so unlike the launch test
# these run anywhere, including headless CI.
add_test(NAME tst_cli_help COMMAND hyprfm --help)
set_tests_properties(tst_cli_help PROPERTIES
    PASS_REGULAR_EXPRESSION "-n, --new-window"
)
add_test(NAME tst_cli_version COMMAND hyprfm --version)
set_tests_properties(tst_cli_version PROPERTIES
    PASS_REGULAR_EXPRESSION "^hyprfm [0-9]+\\.[0-9]+\\.[0-9]+"
)

add_executable(tst_xdgtrash tst_xdgtrash.cpp
    ${CMAKE_SOURCE_DIR}/src/services/xdgtrash.cpp
)
target_include_directories(tst_xdgtrash PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tst_xdgtrash PRIVATE Qt6::Test Qt6::Core)
add_test(NAME tst_xdgtrash COMMAND tst_xdgtrash)

add_executable(tst_recentfilesmodel tst_recentfilesmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/models/recentfilesmodel.cpp
)
target_include_directories(tst_recentfilesmodel PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tst_recentfilesmodel PRIVATE Qt6::Test Qt6::Core Qt6::Concurrent)
add_test(NAME tst_recentfilesmodel COMMAND tst_recentfilesmodel)
