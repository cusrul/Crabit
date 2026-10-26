# Include AFTER CubeMX defines the firmware target. Keep outside generated CMake.
# Caller defines NANOUI_DIR, NANOUI_APP_DIR, NANOUI_PORT_DIR, NANOUI_ASSETS_DIR.
function(nanoui_attach_application firmware)
    foreach(variable NANOUI_DIR NANOUI_APP_DIR NANOUI_PORT_DIR NANOUI_ASSETS_DIR)
        if(NOT DEFINED ${variable})
            message(FATAL_ERROR "Set ${variable} before attaching NanoUI")
        endif()
    endforeach()
    set(NANOUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory("${NANOUI_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/nanoui")
    # Enumerate user-owned sources explicitly: no generated CubeMX source globbing.
    target_sources(${firmware} PRIVATE
        "${NANOUI_PORT_DIR}/ui_entry.cpp"
        "${NANOUI_PORT_DIR}/board_port.cpp"
        "${NANOUI_ASSETS_DIR}/nanoui_assets.cpp"
        "${NANOUI_ASSETS_DIR}/nanoui_font_provider.cpp")
    target_include_directories(${firmware} PRIVATE "${NANOUI_APP_DIR}" "${NANOUI_PORT_DIR}" "${NANOUI_ASSETS_DIR}")
    target_link_libraries(${firmware} PRIVATE NanoUI::core)
endfunction()
