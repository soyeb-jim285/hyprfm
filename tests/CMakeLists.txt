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
