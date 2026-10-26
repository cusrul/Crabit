cmake_minimum_required(VERSION 3.20)
project(NanoUIFirstApp LANGUAGES CXX)

option(NANOUI_BUILD_TESTS "Do not build the library contract tests for this example" OFF)
option(NANOUI_DYNAMIC_STORAGE "Use NanoUI's standard dynamic storage backend" ON)
set(NANOUI_LIBRARY_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." CACHE PATH "NanoUI library directory")

add_subdirectory("${NANOUI_LIBRARY_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/nanoui-library" EXCLUDE_FROM_ALL)
add_executable(NanoUIFirstApp windows/windows_main.cpp
    "${CMAKE_CURRENT_LIST_DIR}/src/generated/nanoui_assets.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/src/generated/nanoui_font_provider.cpp")
target_link_libraries(NanoUIFirstApp PRIVATE NanoUI::core user32 gdi32)
target_include_directories(NanoUIFirstApp PRIVATE "${CMAKE_CURRENT_LIST_DIR}/src")
target_compile_features(NanoUIFirstApp PRIVATE cxx_std_11)
target_compile_definitions(NanoUIFirstApp PRIVATE NOMINMAX)
include(CTest)
if(BUILD_TESTING)
    add_executable(NanoUIResourceSmoke windows/resource_smoke.cpp
        src/generated/nanoui_assets.cpp src/generated/nanoui_font_provider.cpp)
    target_link_libraries(NanoUIResourceSmoke PRIVATE NanoUI::core)
    target_compile_features(NanoUIResourceSmoke PRIVATE cxx_std_11)
    add_test(NAME nanoui_real_resources COMMAND NanoUIResourceSmoke)
endif()
