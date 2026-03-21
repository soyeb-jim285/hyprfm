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
)

target_link_libraries(hyprfm PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::DBus
    Qt6::Widgets
)

target_include_directories(hyprfm PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

target_compile_definitions(hyprfm PRIVATE
    HYPRFM_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
