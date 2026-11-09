set(HYPRFM_SOURCES
    main.cpp
    services/filepicker.cpp
    services/configmanager.cpp
    services/themeloader.cpp
    services/clipboardmanager.cpp
    services/fileoperations.cpp
    services/xdgtrash.cpp
    services/giotransferworker.cpp
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
    services/rcloneservice.cpp
    services/runtimefeaturesservice.cpp
    services/dependencychecker.cpp
    services/gitstatusservice.cpp
    services/sessionstate.cpp
    services/metadataextractor.cpp
    providers/thumbnailprovider.cpp
    providers/pdfpreviewprovider.cpp
)

qt_add_executable(hyprfm ${HYPRFM_SOURCES})

if(HYPRFM_TERMINAL)
    target_sources(hyprfm PRIVATE terminal/terminalview.cpp terminal/terminalview.h)
    target_link_libraries(hyprfm PRIVATE ghostty-vt-static util)
    target_compile_definitions(hyprfm PRIVATE HYPRFM_TERMINAL)
    # It undefines Qt's emit macro (Ghostty has an `emit` field); a unity
    # batch would carry that into the next file.
    set_source_files_properties(terminal/terminalview.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
endif()

option(HYPRFM_ENABLE_PCH "Enable precompiled headers (faster builds)" ON)
option(HYPRFM_ENABLE_UNITY_BUILD "Enable unity builds (faster clean builds)" ON)

if(HYPRFM_ENABLE_PCH)
    target_precompile_headers(hyprfm PRIVATE
        <QtCore/QObject>
        <QtCore/QString>
        <QtCore/QStringList>
        <QtCore/QVariant>
        <QtCore/QDebug>
        <QtCore/QAbstractListModel>
        <QtCore/QHash>
        <QtCore/QFileInfo>
        <QtCore/QDir>
    )
endif()

if(HYPRFM_ENABLE_UNITY_BUILD)
    set_target_properties(hyprfm PROPERTIES
        UNITY_BUILD ON
        UNITY_BUILD_BATCH_SIZE 8
    )
    # Exclude files with anonymous-namespace symbol collisions from unity batches
    # (multiple files define local helpers like isRemoteUri/parentLocation)
    set_source_files_properties(
        models/bookmarkmodel.cpp
        models/tabmodel.cpp
        models/filesystemmodel.cpp
        services/fileoperations.cpp
        PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON
    )
endif()

set_source_files_properties(qml/theme/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

# Compiling QML ahead of time costs build time, so development builds skip it and
# rely on Qt's on-disk cache instead. A packaged build cannot: that cache is keyed
# by the source file's path and mtime, so it is empty on the first launch after
# every install or update, and a Flatpak whose runtime remounts /app pays ~400 ms
# to recompile 86 QML files. Release-type builds therefore default to ON.
set(hyprfm_cachegen_default OFF)
if(CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo|MinSizeRel)$")
    set(hyprfm_cachegen_default ON)
endif()
option(HYPRFM_ENABLE_QML_CACHEGEN "Enable Qt QML cache compilation (faster startup, slower builds)"
       ${hyprfm_cachegen_default})

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
        qml/FilePicker.qml
        qml/theme/Theme.qml
        qml/components/StatusBar.qml
        qml/components/OperationsBar.qml
        qml/components/Sidebar.qml
        qml/components/Breadcrumb.qml
        qml/components/Toolbar.qml
        qml/components/HoverRect.qml
        qml/views/FileViewContainer.qml
        qml/views/FileGridView.qml
        qml/views/FileDetailedView.qml
        qml/views/FileMillerView.qml
        qml/views/KineticWheelScroller.qml
        qml/components/ContextMenu.qml
        qml/components/BulkRenameDialog.qml
        qml/components/ArchivePasswordDialog.qml
        qml/components/ConfirmDialog.qml
        qml/components/QuillThemeBinding.qml
        qml/components/RemoteConnectDialog.qml
        qml/components/PreviewLoader.qml
        qml/components/QuickPreview.qml
        qml/components/CsvTable.qml
        qml/components/Toast.qml
        qml/components/RubberBand.qml
        qml/icons/IconCheck.qml
        qml/icons/IconChevronRight.qml
        qml/icons/IconChevronDown.qml
        qml/icons/IconChevronLeft.qml
        qml/icons/IconChevronUp.qml
        qml/icons/IconX.qml
        qml/icons/IconMinus.qml
        qml/icons/IconSquare.qml
        qml/icons/IconPause.qml
        qml/icons/IconPlay.qml
        qml/icons/IconGrid.qml
        qml/icons/IconList.qml
        qml/icons/IconAlignJustify.qml
        qml/icons/IconColumns.qml
        qml/icons/IconColumns3.qml
        qml/icons/IconHome.qml
        qml/icons/IconSearch.qml
        qml/icons/IconPanelLeft.qml
        qml/icons/IconSquareSplitHorizontal.qml
        qml/icons/IconBookmark.qml
        qml/icons/IconPlus.qml
        qml/icons/IconClock.qml
        qml/icons/IconTrash.qml
        qml/icons/IconImage.qml
        qml/icons/IconKeyboard.qml
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
        qml/icons/IconUsb.qml
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
        qml/components/KeyboardShortcutsDialog.qml
        qml/components/MissingDependenciesDialog.qml
        qml/components/SettingsPanel.qml
        qml/components/ShortcutTip.qml
)

if(HYPRFM_TERMINAL)
    list(APPEND hyprfm_qml_module_args qml/components/TerminalPanel.qml)
endif()

qt_add_qml_module(hyprfm ${hyprfm_qml_module_args})

# The app logo, for the About page; the installed copy is only in the icon
# theme once HyprFM is installed.
qt_add_resources(hyprfm "logo"
    PREFIX "/HyprFM"
    BASE "${CMAKE_SOURCE_DIR}/dist"
    FILES "${CMAKE_SOURCE_DIR}/dist/io.github.soyeb_jim285.HyprFM.svg")

# Quill (the src/qml/Quill submodule) is compiled in as its own QML module,
# like HyprFM, rather than found on disk through import paths. From disk it
# was parsed at runtime, cached by path (so an AppImage, mounted somewhere new
# each launch, recompiled it every time), and resolved through import paths
# that included the build tree, which an installed binary preferred over its
# own install whenever that tree still existed.
qt_add_library(quill STATIC)
set_source_files_properties(qml/Quill/Theme.qml qml/Quill/components/Format.qml
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
# /qt/qml is on the engine's default import path, so the compiled module is
# found without any qmldir on disk.
set(quill_qml_module_args
    URI Quill
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/Quill
)
if(NOT HYPRFM_ENABLE_QML_CACHEGEN)
    list(APPEND quill_qml_module_args NO_CACHEGEN)
endif()
qt_add_qml_module(quill ${quill_qml_module_args}
    QML_FILES
        qml/Quill/Theme.qml
        qml/Quill/components/Format.qml
        qml/Quill/components/Button.qml
        qml/Quill/components/IconButton.qml
        qml/Quill/components/Toggle.qml
        qml/Quill/components/Slider.qml
        qml/Quill/components/TextField.qml
        qml/Quill/components/Dropdown.qml
        qml/Quill/components/Checkbox.qml
        qml/Quill/components/RadioButton.qml
        qml/Quill/components/RadioGroup.qml
        qml/Quill/components/Card.qml
        qml/Quill/components/Dialog.qml
        qml/Quill/components/Separator.qml
        qml/Quill/components/Tabs.qml
        qml/Quill/components/Collapsible.qml
        qml/Quill/components/ScrollableList.qml
        qml/Quill/components/Tooltip.qml
        qml/Quill/components/Badge.qml
        qml/Quill/components/ProgressBar.qml
        qml/Quill/components/Spinner.qml
        qml/Quill/components/Icon.qml
        qml/Quill/components/Avatar.qml
        qml/Quill/components/Label.qml
)
target_link_libraries(quill PRIVATE Qt6::Quick)
target_link_libraries(hyprfm PRIVATE quillplugin)

target_link_libraries(hyprfm PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::DBus
    Qt6::Svg
    Qt6::Network
    Qt6::Concurrent
    PkgConfig::GIO
)

if(KF6WindowSystem_FOUND)
    target_link_libraries(hyprfm PRIVATE KF6::WindowSystem)
endif()

target_include_directories(hyprfm PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

set(HYPRFM_DATA_DIR "${CMAKE_SOURCE_DIR}" CACHE STRING "Runtime data directory for themes and QML")

# Builds from git (the AUR -git package, local builds) say which commit
# they are; a release tarball has no .git and keeps the project version.
# ponytail: read at configure time, so a local build tree only picks up new
# commits on its next cmake run.
set(HYPRFM_VERSION_STRING "${PROJECT_VERSION}")
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" describe --tags --dirty
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE _hyprfm_git_version
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _hyprfm_git_result)
    # "v0.6.1-307-g1d5600c" -> "0.6.1-307-g1d5600c"; a clone without tags
    # fails and keeps the project version.
    if(_hyprfm_git_result EQUAL 0 AND _hyprfm_git_version)
        string(REGEX REPLACE "^v" "" HYPRFM_VERSION_STRING "${_hyprfm_git_version}")
    endif()
endif()

target_compile_definitions(hyprfm PRIVATE
    HYPRFM_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    HYPRFM_DATA_DIR="${HYPRFM_DATA_DIR}"
    HYPRFM_VERSION="${HYPRFM_VERSION_STRING}"
)

if(KF6WindowSystem_FOUND)
    target_compile_definitions(hyprfm PRIVATE HYPRFM_HAS_KWINDOWSYSTEM=1)
endif()

install(TARGETS hyprfm DESTINATION bin)
install(FILES "${CMAKE_SOURCE_DIR}/dist/io.github.soyeb_jim285.HyprFM.desktop"
    DESTINATION share/applications)
install(FILES "${CMAKE_SOURCE_DIR}/dist/io.github.soyeb_jim285.HyprFM.svg"
    DESTINATION share/icons/hicolor/scalable/apps)
install(FILES "${CMAKE_SOURCE_DIR}/dist/io.github.soyeb_jim285.HyprFM.metainfo.xml"
    DESTINATION share/metainfo)

add_executable(xdg-desktop-portal-hyprfm portal/main.cpp)
target_link_libraries(xdg-desktop-portal-hyprfm PRIVATE Qt6::Core Qt6::DBus)
install(TARGETS xdg-desktop-portal-hyprfm DESTINATION bin)

# Register the backend with D-Bus and the portal broker. Choosing it remains
# opt-in through the user's portals.conf; do not rewrite desktop preferences.
configure_file(${CMAKE_SOURCE_DIR}/dist/portal/org.freedesktop.impl.portal.desktop.hyprfm.service.in
    org.freedesktop.impl.portal.desktop.hyprfm.service @ONLY)
configure_file(${CMAKE_SOURCE_DIR}/dist/portal/xdg-desktop-portal-hyprfm.service.in
    xdg-desktop-portal-hyprfm.service @ONLY)
install(FILES ${CMAKE_SOURCE_DIR}/dist/portal/hyprfm.portal
    DESTINATION ${CMAKE_INSTALL_DATADIR}/xdg-desktop-portal/portals)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.freedesktop.impl.portal.desktop.hyprfm.service
    DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/xdg-desktop-portal-hyprfm.service
    DESTINATION ${CMAKE_INSTALL_DATADIR}/systemd/user)
