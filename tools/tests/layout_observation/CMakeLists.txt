cmake_minimum_required(VERSION 3.20)
project(NanoUILayoutObservation LANGUAGES CXX)
# Deliberately independent of SDK/SDL builds. Existing SDL headers are required
# only by the unchanged desktop mock header; this harness opens no SDL window.
set(NANOUI_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." CACHE PATH "Checkout to observe")
set(NANOUI_SDL_HEADERS "" CACHE PATH "Existing directory containing SDL2/SDL.h")
set(NANOUI_SDL_CONFIG_HEADERS "" CACHE PATH "Existing directory containing SDL_config.h")
if(NOT EXISTS "${NANOUI_SDL_HEADERS}/SDL2/SDL.h")
    message(FATAL_ERROR "Supply NANOUI_SDL_HEADERS from an existing desktop SDL build")
endif()
set(NANOUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(NANOUI_DYNAMIC_STORAGE OFF CACHE BOOL "" FORCE)
add_subdirectory("${NANOUI_REPO_ROOT}/libraries/NanoUI" shared)
set(ui "${NANOUI_REPO_ROOT}/firmware/stm32/Core/NanoUI")
add_executable(NanoUILayoutObservation
    "${CMAKE_CURRENT_LIST_DIR}/../nanoui_desktop_layout.cpp"
    "${ui}/src/font_engine.cpp"
    "${ui}/src/nanoui_dummy.cpp"
    "${ui}/assets/generated/desktop/nanoui_assets.cpp")
target_link_libraries(NanoUILayoutObservation PRIVATE NanoUI::core)
target_include_directories(NanoUILayoutObservation PRIVATE
    "${ui}/assets/generated/desktop" "${ui}/include" "${ui}/app_desktop"
    "${ui}/platforms/desktop" "${NANOUI_SDL_HEADERS}" "${NANOUI_SDL_HEADERS}/SDL2"
    "${NANOUI_SDL_CONFIG_HEADERS}")
target_compile_definitions(NanoUILayoutObservation PRIVATE SDL_MAIN_HANDLED NANOUI_SIMULATOR)
set_target_properties(NanoUILayoutObservation PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)
