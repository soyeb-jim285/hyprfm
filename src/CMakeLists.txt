qt_add_executable(hyprfm
    main.cpp
    services/configmanager.cpp
    services/themeloader.cpp
    services/clipboardmanager.cpp
    services/fileoperations.cpp
    models/tabmodel.cpp
    models/tablistmodel.cpp
    models/bookmarkmodel.cpp
    models/filesystemmodel.cpp
    models/devicemodel.cpp
    providers/thumbnailprovider.cpp
)

set_source_files_properties(qml/theme/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(hyprfm
    URI HyprFM
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/theme/Theme.qml
        qml/components/StatusBar.qml
        qml/components/OperationsBar.qml
        qml/components/Sidebar.qml
        qml/components/FileTabBar.qml
        qml/components/Breadcrumb.qml
        qml/components/Toolbar.qml
        qml/views/FileViewContainer.qml
        qml/views/FileGridView.qml
        qml/views/FileListView.qml
        qml/views/FileDetailedView.qml
        qml/components/ContextMenu.qml
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
        qml/icons/IconPlus.qml
)

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

target_include_directories(hyprfm PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

set(HYPRFM_DATA_DIR "${CMAKE_SOURCE_DIR}" CACHE STRING "Runtime data directory for themes and QML")

target_compile_definitions(hyprfm PRIVATE
    HYPRFM_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    HYPRFM_DATA_DIR="${HYPRFM_DATA_DIR}"
)

install(TARGETS hyprfm DESTINATION bin)
install(FILES "${CMAKE_SOURCE_DIR}/dist/hyprfm.desktop" DESTINATION share/applications)
install(FILES "${CMAKE_SOURCE_DIR}/dist/hyprfm.svg" DESTINATION share/icons/hicolor/scalable/apps)
