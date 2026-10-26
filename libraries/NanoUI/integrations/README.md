# Platform integration

Keep the library read-only and user files outside generator-owned directories:

New Apps use `#include <NanoUI.h>` on every platform. SDK/driver and product-service
headers stay in the consuming App or port, not the public framework header.
Existing Crabit Apps may retain their old include paths through compatibility
forwarders; there is no bulk migration or second core. Board-level umbrella
headers that also aggregate services are legacy product entries, not templates
for new integrations. See the
entry policy.

```text
YourProject/
  third_party/NanoUI/       # this complete library
  App/                     # Application subclasses; buildUI returns Widget trees
  Ports/                   # Canvas, InputDriver, time and C entry point
  Assets/generated/        # application-specific generated resources
  nanoui-assets.json       # app_dirs and output_dir, relative to this file
  CMakeLists.txt / *.uvprojx / YourSketch.ino
```

Arduino: install the library ZIP, put App/Ports/generated below the sketch's
`src/` (Arduino compiles it recursively), and keep Windows sources outside that
directory. FirstApp is the complete reference, with a serial Canvas rather
than assumptions about a particular display/pinout. Include `<NanoUI.h>`.

Windows: FirstApp uses the same App and generated resources with a Win32 Canvas
and keyboard InputDriver. It needs only a C++11 toolchain and CMake, not SDL,
Python or a Crabit checkout. Resource generation is an optional separate step.

CubeMX CMake: keep App, Ports, Assets and integration scripts outside generated
`Core`/`cmake/stm32cubemx`. Copy `cubemx/ui_entry.*` and `board_port.h` into Ports,
copy the FirstApp App to App, implement board_port.cpp for your existing drivers.
From USER CODE sections in main.c call `nanoui_init()` once and `nanoui_tick()`
periodically. Add the C header's include path to the generated target through
your top-level CMake, not a regenerated file. Example top-level addition:

```cmake
set(NANOUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/NanoUI")
set(NANOUI_APP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/App")
set(NANOUI_PORT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Ports")
set(NANOUI_ASSETS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Assets/generated")
include("${NANOUI_DIR}/integrations/cubemx/nanoui-app.cmake")
nanoui_attach_application(${CMAKE_PROJECT_NAME})
```

Enable CXX in your root project. The library defaults to dynamic storage; if an
existing pool adapter is required, set NANOUI_DYNAMIC_STORAGE=OFF before adding
the library and inject the adapter before UI initialization. Do not mix settings
between translation units. Fonts require exactly one provider implementation.

Keil/Arm Compiler 6: use the same App/Ports/Assets layout and C entry points. Add
one NanoUI source group containing `src/NanoUI/product/runtime.cpp`; add Ports
and the two generated resource `.cpp` files in separate groups. Include paths:
NanoUI/src, App, Ports, Assets/generated. Enable C++11 and a consistent
NANOUI_DYNAMIC_STORAGE definition for every C++ source. Configure heap/stack
for the target application's real needs. Do not modify CubeMX generated drivers
or automatically rewrite your .uvprojx. This is an integration recipe, not a
validated native Keil project; Arm Compiler 5 is not claimed supported.

FreeRTOS/Zephyr: keep SDK headers and scheduling in Ports. Run the original UI
in one owning task/context; inputs from other tasks need a port-owned queue.
The library has no new thread-safety guarantee or vendor/RTOS dependency.

These recipes do not configure power, OLED handover, UART switching or flashing.
FirstApp Windows and Arduino compilation can be checked independently; native
CubeMX/Keil and physical hardware still require their own validation.
