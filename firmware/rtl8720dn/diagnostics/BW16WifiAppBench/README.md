# Actual Wi-Fi App on a standalone BW16

Independent BW16 with USB UART only. **Do not flash this onto the watch.**
The production App, parser and shared NanoUI are unchanged. There is no STM32,
OLED driver, BLE initialization, credentials or product boot/power path here.

## Build and upload

Run `build.ps1`. It takes the shared SDK build mutex, stages byte-identical
production App headers, forwarding headers, real font provider and generated
assets; links the repository's shared NanoUI; compiles and checks FTL boundaries.
It does not modify the SDK or installed Arduino libraries.

Generated IDE entry:
`build/bw16-wifi-app-bench-ide/BW16WifiAppBench/BW16WifiAppBench.ino`.
Keep the whole generated sketch directory. IDE compilation requires the current
Crabit NanoUI library used by the watch; the script explicitly selects the repo
library, so the CLI build does not prove an older installed library is compatible.
The source entry in this directory needs staging first; don't edit generated files.

User flashes/restarts, closes other serial monitors, then assistant captures COM7
at1152008N1 with DTR/RTS disabled. A reset requires user acknowledgement.

## Automatic cases

1. Passive entry, diagnostic page, scripted channel selection until driver40,
   capture8s, OK to the list, observe25s, Back to mode selector, Back to exit.
2. Default native system scan, actual progress loop/completion, list25s, same exit.
3. Repeat passive case to test re-entry after native scan and App destruction.

`AppManager::launch` uses a dummy cooperative root and the real exclusive App
entry. Actual `Canvas` raster/text/font operations write a1024B RAM screen rather
than OLED pins. Input decisions depend on the real rendered page header, not
private App state or a duplicated App state machine. Timing comes from millis.

Only mode/diagnostic pages can receive OK. The list gets no OK, so the task menu
and packet-injection tasks are not entered. Unexpected task-menu rendering aborts
the harness. It emits no AP identifiers: it logs the count actually formatted by
the App list header, callback counters, mode/channel and display-call counts.
Native scan can send standard SDK probes. The App's real internal EAPOL queue
allocation/processing and callback containers are retained; BLE notification is a
local rejecting stub with no BLE SDK calls or payload logging.

The App's existing callback/UI vector concurrency is deliberately NOT fixed or
hidden by this harness. This can reproduce instability. It remains a diagnostic,
not a safe replacement for the product implementation. The fixed-channel parser
bench remains available separately for comparison.

## Reading results and limitations

`APP_TICK`: `page=1` mode selector,2 diagnostic,3 list,4 forbidden task page.
`cb/beacon/added` are current diagnostic counts; tuning resets them as in the App.
`shown=-1` means no list count has been observed yet, not zero APs.
`draws` counts completed Canvas display calls, not physical OLED performance.
Compare callback/addition counts with the App-formatted `shown` count and their
progress over a full channel sweep. Counters may reset on each App entry.

`CASE_RETURN scripted_exit=1` only proves the scripted route returned through
the real App's exit path. It is NOT a radio test PASS. `CASE_ABORT` stops input and
clears the local display-active flag so the existing App cleanup can run. A70s
budget applies while poll runs; blocked SDK/render calls cannot be interrupted by
this same-task timer. A missing heartbeat/return is therefore meaningful.

It does not reproduce BLE coexistence, full watch heap/stack consumption, UART
input timing, OLED transfer latency, hardware power or the launcher wrapper.
Neither successful compilation nor standalone success is watch acceptance.
