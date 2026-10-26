# FirstApp

A small original-NanoUI `Application::buildUI()` example for a 128x64 display.
The same app header is used by a native Windows GDI preview and a serial-text
Arduino sketch. The app owns its title and switch state; its declarative tree is
one `Column` containing the title and a `Row` with a `Text` and `Switch`.

For a new App, keep the shared `src/apps/hello_app.h` entry as `<NanoUI.h>`.
Arduino/Windows/board SDK dependencies belong in ports, not that App header or
the library's public entry. Existing Crabit legacy include paths need not be
rewritten when adopting this example.

## Windows preview

Configure and build with a Windows C++ toolchain and CMake:

```powershell
cmake -S . -B build
cmake --build build --config Release
```

The library is included from `../../` by default. To use an exported example
with a separately installed NanoUI source tree, pass
`-DNANOUI_LIBRARY_DIR=C:/path/to/NanoUI`. Generated font/resource translation
units are checked in under `src/generated/`; Python is not required to build.
Regeneration is separate from compiling the checked-in assets. The default
configuration does not select a TTF font. The current scanner detects the
framework's 8px Toast as well as this App's Chinese text; bundled `5x8.bdf` lacks
those Chinese glyphs, so regeneration with only the default inputs fails.
Before running `python ../../tools/font_builder.py --config nanoui-assets.json`,
explicitly add `"ttf_font": "Fonts/your-chinese-font.ttf"` to the configuration
and supply that chosen file, or configure `fonts_dir` with adequate BDF coverage.
Font paths resolve relative to `nanoui-assets.json`; no system font is selected
automatically. See the [resource tool instructions](../../tools/README.md).
The window displays the 128x64
canvas at 4x scale. Arrow keys move focus, Return toggles the switch, and Escape
dispatches Back. The normal GUI remains open until the user closes it.

`NanoUIFirstApp.exe --headless [output.bmp]` renders through the same NanoUI
WindowManager and Canvas, sends a simulated key, prints the switch state, and
writes the actual canvas pixels as a 24-bit BMP (default `FirstApp.bmp`). This
mode is for deterministic host checks; its BMP is a generated test artifact.

## Arduino serial reference port

Install the NanoUI library ZIP and open `FirstApp.ino` with the Arduino IDE.
Its `src` folder and generated sources compile automatically as sketch sources.
The sketch uses only `Arduino.h` outside NanoUI and prints a
64-row ASCII representation when the canvas changes. Use `j`/`k` to move focus
and `o` to toggle. This is a serial reference renderer/input adapter, not OLED
support or physical-display acceptance; no board pins or flashing are included.

This bounded example does not validate Keil/CubeMX integration, embedded SDK
builds, or hardware behavior.
