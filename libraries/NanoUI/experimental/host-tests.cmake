add_library(nanoui_foundation STATIC
    experimental/src/NanoUI/foundation/allocator.cpp
    experimental/src/NanoUI/foundation/owned_text.cpp
    experimental/src/NanoUI/framework/build_owner.cpp
    experimental/src/NanoUI/framework/focus.cpp
    experimental/src/NanoUI/framework/element.cpp
    experimental/src/NanoUI/framework/internal_element.cpp
    experimental/src/NanoUI/framework/reconcile_children.cpp
    experimental/src/NanoUI/navigation/navigator.cpp
    experimental/src/NanoUI/rendering/render_object.cpp
    experimental/src/NanoUI/scheduler/pump.cpp
    experimental/src/NanoUI/scheduler/frame.cpp
    experimental/src/NanoUI/widgets/widgets.cpp
    experimental/src/NanoUI/ports/ports.cpp
)
add_library(NanoUI::foundation ALIAS nanoui_foundation)
# STM32's existing forced compiler toolchain has no CMake feature table.
# Set the language standard directly without changing that board toolchain.
set_target_properties(nanoui_foundation PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)
target_include_directories(nanoui_foundation PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/experimental/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/ports"
)
if(MSVC)
    target_compile_options(nanoui_foundation PRIVATE /W4 /WX)
    target_compile_options(nanoui_foundation PUBLIC "$<$<COMPILE_LANGUAGE:CXX>:/GR-;/EHs-c->")
else()
    target_compile_options(nanoui_foundation PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(nanoui_foundation PUBLIC "$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions;-fno-rtti>")
endif()

option(NANOUI_BUILD_EXAMPLES "Build headless allocation observation tool" ${NANOUI_STANDALONE})
if(NANOUI_BUILD_EXAMPLES)
    add_executable(nanoui_memory_probe examples/foundation_memory_probe.cpp)
    target_link_libraries(nanoui_memory_probe PRIVATE NanoUI::foundation)
    add_executable(nanoui_lifecycle_updates examples/lifecycle_updates.cpp)
    target_link_libraries(nanoui_lifecycle_updates PRIVATE NanoUI::foundation)
    set_target_properties(nanoui_lifecycle_updates PROPERTIES
        OUTPUT_NAME nanoui_lifecycle_workload
    )
endif()

if(NANOUI_BUILD_TESTS)
    add_executable(nanoui_pixel_canvas_tests tests/pixel_canvas_tests.cpp)
    target_link_libraries(nanoui_pixel_canvas_tests PRIVATE NanoUI::foundation)
    add_test(NAME nanoui_pixel_canvas COMMAND nanoui_pixel_canvas_tests)
    add_executable(nanoui_legacy_bitmap_font_tests tests/legacy_bitmap_font_tests.cpp)
    target_link_libraries(nanoui_legacy_bitmap_font_tests PRIVATE NanoUI::foundation)
    add_test(NAME nanoui_legacy_bitmap_font COMMAND nanoui_legacy_bitmap_font_tests)
    add_executable(nanoui_focus_tests tests/focus_tests.cpp)
    target_link_libraries(nanoui_focus_tests PRIVATE NanoUI::foundation)
    add_test(NAME nanoui_focus COMMAND nanoui_focus_tests)
    add_executable(nanoui_scroll_controller_tests tests/scroll_controller_tests.cpp)
    target_link_libraries(nanoui_scroll_controller_tests PRIVATE NanoUI::foundation)
    add_test(NAME nanoui_scroll_controller COMMAND nanoui_scroll_controller_tests)
    add_executable(nanoui_frame_tests tests/frame_tests.cpp)
    target_link_libraries(nanoui_frame_tests PRIVATE NanoUI::foundation)
    add_test(NAME nanoui_frame COMMAND nanoui_frame_tests)
    add_executable(nanoui_raster_canvas_tests tests/raster_canvas_tests.cpp)
    target_link_libraries(nanoui_raster_canvas_tests PRIVATE NanoUI::foundation)
    add_test(NAME nanoui_raster_canvas COMMAND nanoui_raster_canvas_tests)
    add_executable(nanoui_ui_gallery examples/ui_gallery.cpp)
    target_link_libraries(nanoui_ui_gallery PRIVATE NanoUI::foundation)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/ui-gallery")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/ui-gallery/interaction")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/ui-gallery/focus")
    add_test(NAME nanoui_ui_gallery COMMAND nanoui_ui_gallery "${CMAKE_CURRENT_BINARY_DIR}/ui-gallery")
    add_executable(nanoui_rendering_tests tests/rendering_tests.cpp)
    target_link_libraries(nanoui_rendering_tests PRIVATE NanoUI::foundation)
    add_test(NAME nanoui_rendering COMMAND nanoui_rendering_tests)
    add_executable(nanoui_children_tests tests/children_tests.cpp)
    target_link_libraries(nanoui_children_tests PRIVATE NanoUI::foundation)
    add_test(NAME nanoui_children COMMAND nanoui_children_tests)
    add_executable(nanoui_navigation_tests tests/navigation_tests.cpp)
    target_link_libraries(nanoui_navigation_tests PRIVATE NanoUI::foundation)
    add_test(NAME nanoui_navigation COMMAND nanoui_navigation_tests)
    add_executable(nanoui_portability_tests tests/portability_tests.cpp)
    target_link_libraries(nanoui_portability_tests PRIVATE NanoUI::foundation)
    add_test(NAME nanoui_portability COMMAND nanoui_portability_tests)

    add_executable(nanoui_foundation_tests
        tests/foundation_tests.cpp
        tests/type_id_peer.cpp
    )
    target_link_libraries(nanoui_foundation_tests PRIVATE NanoUI::foundation)
    add_test(NAME nanoui_foundation COMMAND nanoui_foundation_tests)

    add_executable(nanoui_lifecycle_tests tests/lifecycle_tests.cpp)
    target_link_libraries(nanoui_lifecycle_tests PRIVATE NanoUI::foundation)
    add_test(NAME nanoui_lifecycle COMMAND nanoui_lifecycle_tests)

    set(STM32_NANOUI_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/../../firmware/stm32/Core/NanoUI/include/NanoUI")
    set(BW16_NANOUI_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/../../firmware/rtl8720dn/NanoUI/src")
    foreach(legacy IN ITEMS stm32 bw16)
        if(legacy STREQUAL "stm32")
            set(legacy_include "${STM32_NANOUI_INCLUDE}")
        else()
            set(legacy_include "${BW16_NANOUI_INCLUDE}")
        endif()
        add_executable(nanoui_legacy_${legacy}_probe tests/legacy_probe.cpp)
        target_include_directories(nanoui_legacy_${legacy}_probe PRIVATE "${legacy_include}"
            "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_compile_features(nanoui_legacy_${legacy}_probe PRIVATE cxx_std_17)
        add_test(NAME nanoui_legacy_${legacy} COMMAND nanoui_legacy_${legacy}_probe)
        add_executable(nanoui_animation_${legacy}_tests tests/legacy_animation_tests.cpp)
        target_include_directories(nanoui_animation_${legacy}_tests PRIVATE "${legacy_include}"
            "${CMAKE_CURRENT_SOURCE_DIR}/src")
        add_test(NAME nanoui_animation_${legacy} COMMAND nanoui_animation_${legacy}_tests)
        if(legacy STREQUAL "stm32")
            set(legacy_sources "${STM32_NANOUI_INCLUDE}/../../src")
            set(legacy_root "${STM32_NANOUI_INCLUDE}/..")
        else()
            set(legacy_sources "${BW16_NANOUI_INCLUDE}")
            set(legacy_root "${BW16_NANOUI_INCLUDE}")
        endif()
        add_executable(nanoui_product_${legacy}_port_tests tests/product_canvas_font_tests.cpp
            "${legacy_sources}/font_engine.cpp" "${legacy_sources}/nanoui_assets.cpp")
        target_link_libraries(nanoui_product_${legacy}_port_tests PRIVATE NanoUI::foundation)
        target_include_directories(nanoui_product_${legacy}_port_tests BEFORE PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src" "${legacy_include}" "${legacy_root}")
        target_compile_definitions(nanoui_product_${legacy}_port_tests PRIVATE NANOUI_LEGACY_NAME="${legacy}")
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/product-port-gallery")
        add_test(NAME nanoui_product_${legacy}_port COMMAND nanoui_product_${legacy}_port_tests "${CMAKE_CURRENT_BINARY_DIR}/product-port-gallery")
        add_executable(nanoui_settings_${legacy}_tests tests/settings_hub_tests.cpp
            "${legacy_sources}/font_engine.cpp" "${legacy_sources}/nanoui_assets.cpp")
        target_link_libraries(nanoui_settings_${legacy}_tests PRIVATE NanoUI::foundation)
        target_include_directories(nanoui_settings_${legacy}_tests BEFORE PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}/examples"
            "${legacy_include}" "${legacy_root}")
        target_compile_definitions(nanoui_settings_${legacy}_tests PRIVATE NANOUI_LEGACY_NAME="${legacy}")
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/settings-gallery")
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/settings-gallery/session")
        add_test(NAME nanoui_settings_${legacy} COMMAND nanoui_settings_${legacy}_tests "${CMAKE_CURRENT_BINARY_DIR}/settings-gallery")
        add_executable(nanoui_settings_${legacy}_memory_probe examples/settings_memory_probe.cpp
            "${legacy_sources}/font_engine.cpp" "${legacy_sources}/nanoui_assets.cpp")
        target_link_libraries(nanoui_settings_${legacy}_memory_probe PRIVATE NanoUI::foundation)
        target_include_directories(nanoui_settings_${legacy}_memory_probe BEFORE PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}/examples"
            "${legacy_include}" "${legacy_root}")
        target_compile_definitions(nanoui_settings_${legacy}_memory_probe PRIVATE NANOUI_LEGACY_NAME="${legacy}")
        add_test(NAME nanoui_settings_${legacy}_memory COMMAND nanoui_settings_${legacy}_memory_probe)
    endforeach()
    if(NANOUI_BUILD_EXAMPLES)
        add_test(NAME nanoui_memory_workload COMMAND nanoui_memory_probe)
        add_test(NAME nanoui_lifecycle_workload COMMAND nanoui_lifecycle_updates)
    endif()
endif()
