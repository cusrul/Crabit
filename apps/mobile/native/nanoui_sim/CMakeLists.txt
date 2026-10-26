cmake_minimum_required(VERSION 3.20)
project(crabit_nanoui_sim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# This directory is apps/mobile/native/nanoui_sim. Resolve every dependency
# from the source location so callers may configure from any build directory.
get_filename_component(CRABIT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../" ABSOLUTE)
set(NANOUI_ROOT "${CRABIT_ROOT}/libraries/NanoUI")
set(CRABIT_NANOUI_ROOT "${CRABIT_ROOT}/firmware/stm32/Core/NanoUI")
set(SIM_STM32_ADAPTER "${CMAKE_CURRENT_SOURCE_DIR}/platform/stm32")

if(NOT EXISTS "${CRABIT_ROOT}/README.md" OR NOT EXISTS "${NANOUI_ROOT}/src/NanoUI.h")
    message(FATAL_ERROR "Unable to locate the Crabit repository from ${CMAKE_CURRENT_SOURCE_DIR}")
endif()

set(NANOUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(NANOUI_BUILD_EXPERIMENTAL_TESTS OFF CACHE BOOL "" FORCE)
set(NANOUI_DYNAMIC_STORAGE ON CACHE BOOL "" FORCE)
add_subdirectory("${NANOUI_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/nanoui-framework")

add_library(crabit_nanoui_sim SHARED
    simulator.cpp
    "${CRABIT_NANOUI_ROOT}/src/font_engine.cpp"
    "${CRABIT_NANOUI_ROOT}/assets/generated/desktop/nanoui_assets.cpp")
target_link_libraries(crabit_nanoui_sim PRIVATE NanoUI::core)
target_include_directories(crabit_nanoui_sim PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CRABIT_NANOUI_ROOT}/include"
    "${CRABIT_NANOUI_ROOT}"
    "${SIM_STM32_ADAPTER}"
    "${CRABIT_NANOUI_ROOT}/app_real"
    "${CRABIT_ROOT}/firmware/shared"
    "${CRABIT_NANOUI_ROOT}/assets/generated/desktop")
target_compile_definitions(crabit_nanoui_sim PRIVATE NANOUI_SIMULATOR)
set_target_properties(crabit_nanoui_sim PROPERTIES
    WINDOWS_EXPORT_ALL_SYMBOLS OFF
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES)
if(MSVC)
    target_compile_options(crabit_nanoui_sim PRIVATE /utf-8)
    target_compile_definitions(crabit_nanoui_sim PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(CTest)
endif()
if(BUILD_TESTING AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_executable(crabit_nanoui_sim_tests tests.cpp)
    target_link_libraries(crabit_nanoui_sim_tests PRIVATE crabit_nanoui_sim)
    add_test(NAME crabit_nanoui_sim_tests COMMAND crabit_nanoui_sim_tests)
endif()
