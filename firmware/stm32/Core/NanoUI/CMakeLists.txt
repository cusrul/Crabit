cmake_minimum_required(VERSION 3.12)
project(NanoUI LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==============================================================================
# 🌟 NanoUI Core Library (中间件核心)
# ==============================================================================
file(GLOB_RECURSE NANOUI_SOURCES "src/*.cpp")
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME OR NANOUI_BUILD_DESKTOP)
    # Desktop App text has its own subset; embedded targets retain product assets.
    list(REMOVE_ITEM NANOUI_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/nanoui_assets.cpp")
    list(APPEND NANOUI_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/assets/generated/desktop/nanoui_assets.cpp")
endif()
add_library(nanoui_core STATIC ${NANOUI_SOURCES})

# Portable library owns the original declarative Widget/RenderObject core.
# Embedded builds do not bring host tests or observation executables along.
set(NANOUI_DYNAMIC_STORAGE OFF)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../../../libraries/NanoUI"
                 "${CMAKE_CURRENT_BINARY_DIR}/shared")
target_link_libraries(nanoui_core PUBLIC NanoUI::core)
# Match the existing UI archive policy; this toolchain's plain ar/ranlib do not
# index slim LTO objects correctly. Do not alter the installed compiler/SDK.
target_compile_options(nanoui_product PRIVATE "-fno-lto")

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME OR NANOUI_BUILD_DESKTOP)
    target_compile_definitions(nanoui_core PUBLIC NANOUI_SIMULATOR)
    target_include_directories(nanoui_core BEFORE PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/assets/generated/desktop")
endif()

set_target_properties(nanoui_core PROPERTIES INTERPROCEDURAL_OPTIMIZATION FALSE)
target_compile_options(nanoui_core PRIVATE "-fno-lto")

target_include_directories(nanoui_core 
    PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

# ==============================================================================
# 🖥️ Desktop Simulator
# ==============================================================================
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME OR NANOUI_BUILD_DESKTOP)
    message(STATUS "NanoUI: 桌面编译模式 (Tracy 集成版)...")

    # --- Tracy Profiler 配置 ---
    option(TRACY_ENABLE "Enable Tracy profiling" ON)
    include(FetchContent)
    FetchContent_Declare(
        tracy
        GIT_REPOSITORY https://github.com/wolfpld/tracy.git
        GIT_TAG        v0.13.1
    )
    FetchContent_MakeAvailable(tracy)

    # SDL2
    FetchContent_Declare(
        SDL2
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-2.30.2
    )
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(SDL2)

    file(GLOB_RECURSE SIM_SOURCES "platforms/desktop/*.cpp")
    add_executable(NanoUI_Sim ${SIM_SOURCES})
    option(NANOUI_DESKTOP_LAYOUT_CHECK "Build host-only observation of unchanged desktop Apps" OFF)
    if(NANOUI_DESKTOP_LAYOUT_CHECK)
        add_executable(NanoUI_DesktopLayoutCheck
            "${CMAKE_CURRENT_SOURCE_DIR}/../../../../tools/tests/nanoui_desktop_layout.cpp")
        target_link_libraries(NanoUI_DesktopLayoutCheck PRIVATE nanoui_core SDL2::SDL2-static)
        target_compile_definitions(NanoUI_DesktopLayoutCheck PRIVATE SDL_MAIN_HANDLED NANOUI_DESKTOP_SDL_INPUT)
        target_include_directories(NanoUI_DesktopLayoutCheck PRIVATE platforms/desktop app_desktop)
    endif()
    option(NANOUI_DESKTOP_FONT_CHECK "Build host-only desktop glyph/render check" OFF)
    if(NANOUI_DESKTOP_FONT_CHECK)
        add_executable(NanoUI_DesktopFontCheck
            "${CMAKE_CURRENT_SOURCE_DIR}/../../../../tools/tests/nanoui_desktop_fonts.cpp"
            src/font_engine.cpp assets/generated/desktop/nanoui_assets.cpp)
        target_link_libraries(NanoUI_DesktopFontCheck PRIVATE NanoUI::core)
        target_include_directories(NanoUI_DesktopFontCheck PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/generated/desktop"
            "${CMAKE_CURRENT_SOURCE_DIR}/include")
    endif()

    # 🌟 关键：开启 TRACY_ENABLE 宏
    target_link_libraries(NanoUI_Sim PRIVATE nanoui_core SDL2::SDL2main SDL2::SDL2-static Tracy::TracyClient)
    
    target_compile_definitions(NanoUI_Sim PRIVATE NANOUI_SIMULATOR)
    if(TRACY_ENABLE)
        target_compile_definitions(NanoUI_Sim PUBLIC TRACY_ENABLE)
    endif()

    target_include_directories(NanoUI_Sim PRIVATE 
        platforms/desktop
        ${CMAKE_CURRENT_SOURCE_DIR}/app_desktop
    )

    if(MSVC)
        target_compile_options(NanoUI_Sim PRIVATE /utf-8)
        target_compile_definitions(NanoUI_Sim PRIVATE _CRT_SECURE_NO_WARNINGS SDL_MAIN_HANDLED)
    endif()
else()
    message(STATUS "NanoUI: 🔌 已作为嵌入式子模块挂载。")
    target_include_directories(nanoui_core PUBLIC 
        ${CMAKE_CURRENT_SOURCE_DIR}/app_real
    )
endif()
