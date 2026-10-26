# Standalone BW16 scan/receive bench

For a separate Ai-Thinker BW16 with USB UART, **not the integrated watch**.
No OLED access, STM32 protocol, BLE initialization, association, credentials,
packet injection or persistent settings writes. SDK native discovery may send
normal scan probes. This is not a full watch-runtime replica.

Run `build.ps1` from PowerShell. It serializes builds with the product's existing
SDK mutex, copies six current product headers byte-for-byte into a generated
Arduino IDE sketch, compiles with AmebaD, then checks the image/FTL boundary.
No global SDK configuration or installed Arduino library is changed.

The generated sketch is
`build/bw16-wifi-sniff-bench-ide/BW16WifiSniffBench/BW16WifiSniffBench.ino`.
Open that generated file (keep its entire directory) for ordinary Arduino IDE
compilation/upload. The source sketch in this directory needs staging first.
Re-run the script after changing product headers; do not edit the generated copy.
Original `../BW16WifiUartScan/` is retained as the native-scan control.

V2 boots at 115200 8N1. Its first cycle skips native scanning entirely and tests
passive receive directly on known nearby AP channels 9/40, followed by the same
sweep. This distinguishes cold PROMISC initialization from a receiver previously
primed by a native scan. Starting at the second cycle, it executes:

1. Native SDK STA scan, waiting for explicit completion, at most 30 seconds.
   Reports count, duration, SSID/channel/RSSI; chooses the strongest discovered
   2.4 GHz and 5 GHz channels (fallbacks 1/40 if a band has no results).
2. Watch-style `off -> on(PROMISC) -> enter -> enable -> channel` startup.
3. Eight-second fixed-channel receive trials on the two selected channels.
4. Continuous receive with the watch's complete channel table and 800/500 ms
   dwell times, then prints the AP list and repeats after five seconds.

The actual `master_promisc_callback`, `WifiHackContext`, channel table and receive
counters come from the watch sources, not a rewritten substitute parser. The
bench adds a callback-admission/in-flight guard and never reads the callback's
vector/String contents while receiving. It does not allocate the EAPOL queue or
process credential material. It does not reproduce the watch's UI concurrency,
BLE coexistence, power/boot handover, heap pressure or exclusive-App loop.

`cb/rx/beacon/probe_response/added` locate the first missing receive stage.
`set/get/enabled` distinguish channel or capture failure from parser failure.
Counters are per fixed-channel stage and cumulative within the sweep; AP list
accumulates for the entire passive phase. Channel labeling during a hop retains
the original parser's limitation and is not proof of a frame's physical channel.

On rejected native scan, missing native completion, failed radio initialization,
or failed receiver shutdown, the bench prints `HALT` and does not retry a radio
teardown of uncertain state. SDK calls themselves can block; a missing stage-end
message is meaningful. Fixed channel/set/enable errors are logged even when no
frames arrive. Do not infer hardware success from compilation or this document.

Only one process owns the serial port. Close the IDE monitor before assistant
capture. Opening serial for capture must leave DTR/RTS disabled; a reset/boot-mode
operation requires the user's acknowledgement.
