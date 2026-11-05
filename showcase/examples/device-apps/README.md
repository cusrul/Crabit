# Device App tutorial examples

These examples are not registered in the product launchers. Follow
the [STM32 tutorial](../../docs/stm32-first-app.md) and
[BW16 tutorial](../../docs/bw16-first-app.md) in your own working copy.

- `app_counter.h`: portable cooperative application, RAM state only.
- `app_temperature.h`: STM32 snapshot consumer, no direct peripheral access.
- `preview.cpp`: Windows-only adapter, simulated sensor source and headless checks.

From the repository root in a Visual Studio Developer PowerShell:

```powershell
cmake -S showcase/examples/device-apps -B build/device-apps -DBUILD_TESTING=ON
cmake --build build/device-apps --config Release
ctest --test-dir build/device-apps -C Release --output-on-failure
```

CTest renders `counter.png` and `temperature-demo.png` into the build directory.
These are real NanoUI pixels, but temperature input is synthetic. The checks cover
increment/reset, snapshot absence, invalid/stale/nonfinite input, Back, repeated
visits and tracked UI allocation release. They do not prove hardware behavior.

In the interactive preview: Enter confirms, arrows move focus, T opens a simulated
temperature page, Esc returns. Close the window to exit at the root Counter page.

The preview uses the existing generated FirstApp font resources and needs no font
generation. It does not save settings, write Flash, initialize a radio or control
the watch. Persistent product settings require their own versioned contract;
these examples deliberately do not extend SystemConfig.
