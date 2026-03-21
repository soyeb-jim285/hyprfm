qt_add_executable(hyprfm
    main.cpp
    services/configmanager.cpp
)

qt_add_qml_module(hyprfm
    URI HyprFM
    VERSION 1.0
    QML_FILES
        qml/main.qml
)

target_link_libraries(hyprfm PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::DBus
)

target_include_directories(hyprfm PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
