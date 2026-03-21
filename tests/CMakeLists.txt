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
