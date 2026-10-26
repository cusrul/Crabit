# NanoUI

NanoUI is a small C++ UI framework for embedded displays and host previews. It
lets an application describe a widget tree once, while a platform supplies the
canvas, input, clock and font implementation. The maintained implementation in
this repository is the original `Widget` / `RenderObject` framework; it is not
Flutter itself and does not promise Flutter API compatibility.

This guide describes the maintained library, not a stable Crabit firmware release.
Crabit's current product candidate is roughly usable and undergoing convergence;
its IDE-default BW16 profile includes network tools and Hopper. Building or reading
this library documentation does not establish device acceptance. Start with the
[newcomer guide](../../docs/guide/nanoui.md), then try [FirstApp](examples/FirstApp/README.md).

FirstApp, tools, integrations and `docs/runtime-contracts.md` are included in the
locally generated Arduino ZIP. Links into `../../docs/` refer to the full Crabit
repository, not files bundled in that standalone ZIP. Packaging locally is not
a license or public redistribution approval; see the repository's
release checklist.

## A small App

The framework entry for new applications is `<NanoUI.h>`. An `Application`
returns a newly composed `Widget` tree from `buildUI()`; widgets and render
objects are dynamically allocated in the original framework. State referenced
by a widget, such as the `bool` passed to `Switch`, must outlive the render
objects that use it.

```cpp
#include <NanoUI.h>

class HelloApp : public Application {
public:
    HelloApp() : enabled(false) {}
    const char* getAppName() override { return "Hello"; }

    Widget* buildUI() override {
        return new Column({
            new Text("Hello, NanoUI", 12),
            new Row({new Text("Enabled", 12), new Switch(enabled)})
        });
    }

private:
    bool enabled;
};
```

This demonstrates composition, not a complete board program. A port owns the
`Canvas`, `InputDriver`, time source and run loop. FirstApp shows the concrete
host and Arduino setup: initialize time and input, create a `WindowManager`,
push the tree with its owning `Application`, then update and draw it from the
platform loop. Keep the App alive for that entire period. See
[`libraries/NanoUI/examples/FirstApp`](examples/FirstApp/README.md) for the
complete example and its deliberately simple serial renderer.

## What belongs where

- `src/NanoUI.h` is the framework-only public entry. The active original core
  lives under `src/NanoUI/product/`.
- An App owns its UI composition, application state, product behavior and its
  generated resources. Keep board services and device-specific headers in the
  App or port that uses them.
- A platform port adapts display drawing to `Canvas`, input to `InputDriver`,
  supplies time and, where needed, memory/font adapters. Arduino, STM32 HAL,
  FreeRTOS, Windows and vendor SDK headers do not belong in the public framework
  entry.
- `experimental/` is preserved separately; it is not the default product core.

Use `#include <NanoUI.h>` for new Apps. Existing Crabit Apps can keep their
legacy include paths through forwarding headers; there is no need for a bulk
migration. `NanoUILegacy.h` is a compatibility entry, not the recommended new
App interface. The details and limits are in the
portable library contract.

## Try FirstApp on Windows

From this directory, configure the example with a Windows C++11 toolchain and
CMake:

```powershell
cmake -S examples/FirstApp -B build/nanoui-first-app
cmake --build build/nanoui-first-app --config Release
```

The default library path is relative to the example. To point the example at a
separate NanoUI checkout, configure with
`-DNANOUI_LIBRARY_DIR=C:/path/to/NanoUI`. The project has an opt-in
`NANOUI_BUILD_TESTS` option (default `OFF`) and an
`NANOUI_DYNAMIC_STORAGE` option (default `ON`). Build behavior is defined by
[`examples/FirstApp/CMakeLists.txt`](examples/FirstApp/CMakeLists.txt) and
[`CMakeLists.txt`](CMakeLists.txt). The generated font/resource `.cpp` files
are checked in with FirstApp, so compiling them does not require Python.

FirstApp's Windows port uses Win32 GDI; it does not need the product simulator's
SDL/Tracy setup. Its canvas is a 128x64 monochrome host preview. The native
window accepts arrow keys, Return and Escape. For a non-interactive render, run
`NanoUIFirstApp.exe --headless [output.bmp]` from the build directory; this is a
host check, not display hardware evidence.

## Arduino and embedded integrations

FirstApp also has an Arduino serial reference sketch. Install a library ZIP,
open `FirstApp.ino` from the NanoUI examples, and use the serial monitor at
115200 baud. The sample draws an ASCII representation over serial; it is not an
OLED driver or a board wiring recipe. Arduino IDE/library packaging details are
maintained in the FirstApp README and repository documentation.

For a product integration, separate the App from the board adapter and use the
platform's existing build system. The STM32 product tree has a CMake adapter;
the BW16 Arduino sketch has its own adapter. Their SDKs and builds are distinct.
CubeMX/Keil integration notes and required port responsibilities are described
in [`integrations/README.md`](integrations/README.md). They are recipes, not
claims that every board project has been built or accepted on hardware. Zephyr
is not an implemented or validated port in this snapshot; the fact that an
application can be structured around a platform port is not Zephyr support.

## Fonts and images

Resource generation is an application-side preparation step, not part of the
ordinary C++ build. Start with the [assets guide](../../docs/guide/assets.md)
and [`tools/README.md`](tools/README.md). Generated font/image translation
units belong to the consuming App. The tool scans configured application source
and the shared framework sources; it does not discover every runtime string.
Font inputs have separate provenance and redistribution notes in
[`assets/fonts/SOURCES.md`](assets/fonts/SOURCES.md). Choose and verify fonts
you are entitled to use; this repository makes no blanket licensing promise.

## Build the library contract tests

For storage backends, borrowed App/text lifetimes, object callback limits and
terminal cleanup order, see the retained [runtime contracts](docs/runtime-contracts.md).
Layout semantics are in the layout contract.

The independent host test build uses CMake and a C++11 compiler:

```powershell
cmake -S . -B build/nanoui-tests -DNANOUI_BUILD_TESTS=ON
cmake --build build/nanoui-tests --config Release
ctest --test-dir build/nanoui-tests -C Release --output-on-failure
```

Tests exercise selected framework contracts on the host. They do not prove
embedded SDK compatibility, product image boundaries, OLED behavior, timing or
physical hardware acceptance. For the wider repository's current product and
validation state, use the [getting-started](../../docs/guide/getting-started.md),
[build](../../docs/guide/build-and-flash.md) and [status](../../docs/guide/status.md) guides;
this library README does not promote an experimental default into a
stable firmware release.
