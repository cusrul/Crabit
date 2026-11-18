# Wireless Apps

Current Wi-Fi and BLE apps remain single-header files in this category directory.
The shared wireless services live in `../../core/`:

- `wifi_hack_core.h`, `wifi_hack_sniffer.h`, `wifi_sniff_diagnostics.h`
- `webserver.h`, `debug.h`

This is a migration state, not the final package layout. New wireless apps must
use `apps/wireless/<id>/` with `app.h` + `README.md` + `LICENSE`, register once
in `app/arduboy30_registry.h`, and declare `wifi`/`ble`/`radio-owner` permissions
in the README. Existing apps will move to per-app directories only with their own
compile and hardware regression, not as part of a cosmetic sweep.

Do not treat the presence of a header here as proof of radio coexistence,
authorization or hardware acceptance. Follow the existing radio-owner rules and
use only owned or explicitly authorized networks/devices.
