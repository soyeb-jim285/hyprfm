set(HYPRFM_SOURCES
    main.cpp
    services/configmanager.cpp
    services/themeloader.cpp
    services/clipboardmanager.cpp
    services/fileoperations.cpp
    services/draghelper.cpp
    models/tabmodel.cpp
    models/tablistmodel.cpp
    models/bookmarkmodel.cpp
    models/filesystemmodel.cpp
    models/devicemodel.cpp
    models/recentfilesmodel.cpp
    models/searchresultsmodel.cpp
    models/searchproxymodel.cpp
    services/searchservice.cpp
    services/undomanager.cpp
    services/previewservice.cpp
    services/diskusageservice.cpp
    services/remoteaccessservice.cpp
    services/runtimefeaturesservice.cpp
    services/gitstatusservice.cpp
    providers/thumbnailprovider.cpp
)

if(POPPLER_QT6_FOUND)
    list(APPEND HYPRFM_SOURCES providers/pdfpreviewprovider.cpp)
endif()

qt_add_executable(hyprfm ${HYPRFM_SOURCES})

set_source_files_properties(qml/theme/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

option(HYPRFM_ENABLE_QML_CACHEGEN "Enable Qt QML cache compilation (faster startup, slower builds)" OFF)

set(hyprfm_qml_module_args
    URI HyprFM
    VERSION 1.0
)

if(NOT HYPRFM_ENABLE_QML_CACHEGEN)
    list(APPEND hyprfm_qml_module_args NO_CACHEGEN)
endif()

list(APPEND hyprfm_qml_module_args
    QML_FILES
        qml/Main.qml
        qml/theme/Theme.qml
        qml/components/StatusBar.qml
        qml/components/OperationsBar.qml
        qml/components/Sidebar.qml
        qml/components/FileTabBar.qml
        qml/components/Breadcrumb.qml
        qml/components/Toolbar.qml
        qml/components/HoverRect.qml
        qml/views/FileViewContainer.qml
        qml/views/FileGridView.qml
        qml/views/FileListView.qml
        qml/views/FileDetailedView.qml
        qml/views/KineticWheelScroller.qml
        qml/components/ContextMenu.qml
        qml/components/BulkRenameDialog.qml
        qml/components/RemoteConnectDialog.qml
        qml/components/QuickPreview.qml
        qml/components/Toast.qml
        qml/components/RubberBand.qml
        qml/icons/IconCheck.qml
        qml/icons/IconChevronRight.qml
        qml/icons/IconChevronDown.qml
        qml/icons/IconChevronLeft.qml
        qml/icons/IconChevronUp.qml
        qml/icons/IconX.qml
        qml/icons/IconGrid.qml
        qml/icons/IconList.qml
        qml/icons/IconAlignJustify.qml
        qml/icons/IconHome.qml
        qml/icons/IconSearch.qml
        qml/icons/IconPanelLeft.qml
        qml/icons/IconSquareSplitHorizontal.qml
        qml/icons/IconPlus.qml
        qml/icons/IconClock.qml
        qml/icons/IconTrash.qml
        qml/icons/IconImage.qml
        qml/icons/IconDownload.qml
        qml/icons/IconFolder.qml
        qml/icons/IconFileText.qml
        qml/icons/IconMusic.qml
        qml/icons/IconVideo.qml
        qml/icons/IconMonitor.qml
        qml/icons/IconGlobe.qml
        qml/icons/IconRocket.qml
        qml/icons/IconSettings.qml
        qml/icons/IconFilter.qml
        qml/icons/IconHardDrive.qml
        qml/icons/IconHardDriveOff.qml
        qml/icons/IconCopy.qml
        qml/icons/IconCopyPath.qml
        qml/icons/IconExternalLink.qml
        qml/icons/IconFolderArchive.qml
        qml/icons/IconFolderPen.qml
        qml/icons/IconInfo.qml
        qml/icons/IconPackageOpen.qml
        qml/icons/IconPin.qml
        qml/icons/IconPinOff.qml
        qml/icons/IconScissors.qml
        qml/icons/IconTerminal.qml
        qml/icons/IconClipboard.qml
        qml/icons/IconEye.qml
        qml/icons/IconSlidersH.qml
        qml/icons/IconUndo.qml
        qml/icons/IconGitModified.qml
        qml/icons/IconGitStaged.qml
        qml/icons/IconGitUntracked.qml
        qml/icons/IconGitDeleted.qml
        qml/icons/IconGitRenamed.qml
        qml/icons/IconGitConflicted.qml
        qml/icons/IconGitIgnored.qml
        qml/icons/IconGitDirty.qml
        qml/components/SearchBar.qml
        qml/components/FilterPanel.qml
)

qt_add_qml_module(hyprfm ${hyprfm_qml_module_args})

# Make Quill module discoverable at runtime
set_target_properties(hyprfm PROPERTIES
    QT_QML_IMPORT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/qml"
)

target_link_libraries(hyprfm PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::DBus
    Qt6::Widgets
    Qt6::Svg
)

if(POPPLER_QT6_FOUND)
    target_link_libraries(hyprfm PRIVATE PkgConfig::POPPLER_QT6)
endif()

target_include_directories(hyprfm PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

set(HYPRFM_DATA_DIR "${CMAKE_SOURCE_DIR}" CACHE STRING "Runtime data directory for themes and QML")

target_compile_definitions(hyprfm PRIVATE
    HYPRFM_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    HYPRFM_DATA_DIR="${HYPRFM_DATA_DIR}"
)

if(POPPLER_QT6_FOUND)
    target_compile_definitions(hyprfm PRIVATE HYPRFM_HAS_POPPLER_QT6=1)
endif()

install(TARGETS hyprfm DESTINATION bin)
install(FILES "${CMAKE_SOURCE_DIR}/dist/hyprfm.desktop" DESTINATION share/applications)
install(FILES "${CMAKE_SOURCE_DIR}/dist/hyprfm.svg" DESTINATION share/icons/hicolor/scalable/apps)
