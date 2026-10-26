# Native NanoUI host simulator

`crabit_nanoui_sim` renders the existing STM32 `app_real` NanoUI tree for host previews. Startup remains the legacy Launcher; after `crabit_sim_start()`, call `crabit_sim_show_clock()` to reset the app tree and make the product `ClockApp` its root. The ClockApp's existing menu opens Launcher, and long BACK returns to the ClockApp. A further BACK at the ClockApp root cannot expose the discarded startup Launcher. Showing the clock preserves the host and injected clock/weather adapters.

## Host injection ABI

All calls are serialized on the simulator thread. `crabit_sim_set_clock(local_epoch_seconds)` accepts UTC-epoch seconds already shifted by the host's local UTC offset (thus UTC conversion routines render local wall time). Its supported range is 2000-01-01 through 2099-12-31. The injected clock advances with `crabit_sim_step(now_ms)` and updates both RTC time and date across day/month/year boundaries. The product RTC SetTime/SetDate adapter calls remain honored and advance from their explicitly set values.

`crabit_sim_set_weather(date, wmo_code, low_tenths, high_tenths, rain_percent, fetched_epoch_seconds)` injects one dated forecast day. Valid dates are Gregorian 2000–2099, WMO codes are 0–99, tenths-degrees are -1000..1000 with low <= high, rain is 0..100, and fetched epoch uses the same 2000–2099 bounds. Date zero clears weather (the other arguments are ignored). There is no implicit weather forecast, location, or sunny fallback. The ClockApp matches the injected date to today's local date; unmatched forecast dates are labeled as cached by the existing product UI. The generic simulator phone label is “预览”, and without weather the count is zero and label is “未获取”. WMO conditions follow the product `network_weather.cpp` mapping.

Invalid values and calls made while stopped return -1 without changing adapter state. Setters mark the NanoUI window dirty but do not render or allocate a frame synchronously. Stop/start clears the injected clock/weather; legacy startup still displays Launcher.

## Build and test

```powershell
cmake -S apps/mobile/native/nanoui_sim -B build/clock-preview -G "Visual Studio 17 2022" -A x64
cmake --build build/clock-preview --config Debug
ctest --test-dir build/clock-preview -C Debug --output-on-failure
```

This is a host-only simulator target; it does not build embedded SDKs or prove device behavior.
