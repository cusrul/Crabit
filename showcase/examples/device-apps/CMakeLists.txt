cmake_minimum_required(VERSION 3.20)
project(CrabitDeviceAppTutorial LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "This preview uses the existing Windows desktop toolchain")
endif()
option(NANOUI_DYNAMIC_STORAGE "Use unbounded desktop storage instead of the pool profile" OFF)
set(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
set(FIRST_APP "${REPO_ROOT}/libraries/NanoUI/examples/FirstApp")
set(NANOUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${REPO_ROOT}/libraries/NanoUI" "${CMAKE_CURRENT_BINARY_DIR}/nanoui")
add_executable(DeviceAppPreview preview.cpp
    "${FIRST_APP}/src/generated/nanoui_assets.cpp"
    "${FIRST_APP}/src/generated/nanoui_font_provider.cpp")
target_compile_features(DeviceAppPreview PRIVATE cxx_std_17)
target_compile_definitions(DeviceAppPreview PRIVATE NOMINMAX)
target_include_directories(DeviceAppPreview PRIVATE
    "${FIRST_APP}/src"
    "${REPO_ROOT}/firmware/stm32/Core/Inc"
    "${REPO_ROOT}/firmware/stm32/Core/NanoUI/include"
    "${REPO_ROOT}/firmware/stm32/Core/NanoUI/app_real")
target_link_libraries(DeviceAppPreview PRIVATE NanoUI::core user32 gdi32 gdiplus)
include(CTest)
if(BUILD_TESTING)
    add_test(NAME counter_and_temperature
        COMMAND DeviceAppPreview --headless "${CMAKE_CURRENT_BINARY_DIR}")
endif()
