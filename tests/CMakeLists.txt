# Tests will be added as backend classes are implemented

add_executable(tst_configmanager tst_configmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/services/configmanager.cpp
)
target_include_directories(tst_configmanager PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/third_party
)
target_link_libraries(tst_configmanager PRIVATE Qt6::Test Qt6::Core)
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
target_link_libraries(tst_clipboardmanager PRIVATE Qt6::Test Qt6::Core)
add_test(NAME tst_clipboardmanager COMMAND tst_clipboardmanager)

add_executable(tst_bookmarkmodel tst_bookmarkmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/models/bookmarkmodel.cpp
)
target_include_directories(tst_bookmarkmodel PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tst_bookmarkmodel PRIVATE Qt6::Test Qt6::Core)
add_test(NAME tst_bookmarkmodel COMMAND tst_bookmarkmodel)

add_executable(tst_fileoperations tst_fileoperations.cpp
    ${CMAKE_SOURCE_DIR}/src/services/fileoperations.cpp
)
target_include_directories(tst_fileoperations PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tst_fileoperations PRIVATE Qt6::Test Qt6::Core Qt6::Gui)
add_test(NAME tst_fileoperations COMMAND tst_fileoperations)

add_executable(tst_undomanager tst_undomanager.cpp
    ${CMAKE_SOURCE_DIR}/src/services/undomanager.cpp
    ${CMAKE_SOURCE_DIR}/src/services/fileoperations.cpp
)
target_include_directories(tst_undomanager PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tst_undomanager PRIVATE Qt6::Test Qt6::Core Qt6::Gui)
add_test(NAME tst_undomanager COMMAND tst_undomanager)

add_executable(tst_previewservice tst_previewservice.cpp
    ${CMAKE_SOURCE_DIR}/src/services/previewservice.cpp
)
target_include_directories(tst_previewservice PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tst_previewservice PRIVATE Qt6::Test Qt6::Core Qt6::Gui)
if(POPPLER_QT6_FOUND)
    target_link_libraries(tst_previewservice PRIVATE PkgConfig::POPPLER_QT6)
    target_compile_definitions(tst_previewservice PRIVATE HYPRFM_HAS_POPPLER_QT6=1)
endif()
add_test(NAME tst_previewservice COMMAND tst_previewservice)

# --- New test targets ---

add_executable(tst_filesystemmodel tst_filesystemmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/models/filesystemmodel.cpp
)
target_include_directories(tst_filesystemmodel PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/third_party
)
target_link_libraries(tst_filesystemmodel PRIVATE Qt6::Test Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg Qt6::SvgWidgets)
add_test(NAME tst_filesystemmodel COMMAND tst_filesystemmodel)

add_executable(tst_devicemodel tst_devicemodel.cpp
    ${CMAKE_SOURCE_DIR}/src/models/devicemodel.cpp
)
target_include_directories(tst_devicemodel PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tst_devicemodel PRIVATE Qt6::Test Qt6::Core Qt6::DBus)
add_test(NAME tst_devicemodel COMMAND tst_devicemodel)

add_executable(tst_iconprovider tst_iconprovider.cpp)
target_include_directories(tst_iconprovider PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tst_iconprovider PRIVATE Qt6::Test Qt6::Core Qt6::Gui Qt6::Quick Qt6::Svg)
add_test(NAME tst_iconprovider COMMAND tst_iconprovider)

add_executable(tst_thumbnailprovider tst_thumbnailprovider.cpp
    ${CMAKE_SOURCE_DIR}/src/providers/thumbnailprovider.cpp
)
target_include_directories(tst_thumbnailprovider PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tst_thumbnailprovider PRIVATE Qt6::Test Qt6::Core Qt6::Gui Qt6::Quick)
add_test(NAME tst_thumbnailprovider COMMAND tst_thumbnailprovider)

if(POPPLER_QT6_FOUND)
    add_executable(tst_pdfpreviewprovider tst_pdfpreviewprovider.cpp
        ${CMAKE_SOURCE_DIR}/src/providers/pdfpreviewprovider.cpp
    )
    target_include_directories(tst_pdfpreviewprovider PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(tst_pdfpreviewprovider PRIVATE Qt6::Test Qt6::Core Qt6::Gui Qt6::Quick PkgConfig::POPPLER_QT6)
    add_test(NAME tst_pdfpreviewprovider COMMAND tst_pdfpreviewprovider)
endif()

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
    ${CMAKE_SOURCE_DIR}/src/models/searchresultsmodel.cpp
)
target_include_directories(tst_searchproxymodel PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/third_party
)
target_link_libraries(tst_searchproxymodel PRIVATE Qt6::Test Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg Qt6::SvgWidgets)
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
