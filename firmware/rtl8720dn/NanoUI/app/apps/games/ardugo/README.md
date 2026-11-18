# ArduGo BW16 port

Actual source/assets from [ModusCreateOrg/ardugo](https://github.com/ModusCreateOrg/ardugo),
commit `2343b903c36234c302efaaa5c3aca80010d388ee`, MIT
(Copyright (c) 2018 Modus Create). `LICENSE` is the upstream root license,
unchanged. The 9×9 board background in `assets.h` is the repository's own 64×64
PNG asset; text uses the shared engine font. No music or font files are imported.

## Play

- Directions (tilt): move the blinking cursor on the 9×9 board.
- A (watch OK, single tap): place a black stone. The original simple-play AI then
  answers with one white stone on a random empty legal point and moves the cursor
  there. An occupied point is rejected with the original silent error beep.
- B (watch BACK, double tap): restart the game (clear board and counters).
- Hold PAT for the shared exit gesture.
- Sidebar shows the original title/statistics pages, including living stones and
  prisoner counts (Black "capt" = white stones Black captured and vice versa).

This is the upstream "simple rules" Go variant, not a complete Go engine: only
empty-point legality is checked, so suicide is not rejected (the stone is
immediately removed by the original capture pass and counted against its
owner), and there is no ko rule, handicap, pass, komi or resignation. The upstream README sentence about
playing "until one player wins" is not implemented in the source either; the
game has no win/terminal screen and continues until B restarts it. The AI only
selects uniformly among empty legal points (no search). These are reported as
original limitations, not replaced with an invented ruleset or AI.

## Port changes and limits

- `.cpp`/`.ino` bodies are `.inc.h` include-only files inside `ArduGoSandbox`.
  File-scope include guards were made unique (`ARDUGO_PORT_*`) because Castle
  Boy's earlier `ASSETS_H` and similar launcher-order macros would otherwise
  skip these headers in the combined translation unit.
- Old `Arduboy`/`ArduboyTunes` are replaced with a thin `Arduboy2Compat`
  subclass (`start()` → `beginNoLogo()`, `println()` helper) and a silent
  `tunes` stub. The two original 150/2000 Hz beeps are silent on this build.
- The debug serial Shell and `MemoryFree` files are not compiled (they have no
  explicit license headers and are not gameplay). No `Serial`, timer, AVR
  register, EEPROM or Flash-persistence dependency remains. The game never had
  EEPROM saved state, so no save was invented.
- The original `.ino` loop had no frame pacing and button sampling; the port adds
  `nextFrame()` so the shared adapter supplies 60 Hz pacing/button polling
  without blocking NanoUI or link services. App reentry resets the board, the
  blink phase, counters and sidebar; B restart keeps the original `clear()` path.
- The bitmap helper takes a const pointer (upstream only compiled with
  `-fpermissive`); the upstream `srand(time(NULL))` seed is replaced with
  `srand(millis())` because no RTC is assumed. `Board` gained read-only
  `pointAt`/cursor accessors and a `playMove` wrapper used by the host test; they
  call the original move path and add no gameplay behavior.
- 128×64 monochrome, tilt directions and A/B tap semantics are supplied by the
  shared `ArduboyCompatApp`. No shared geometry types are used.

## Test

`tools/tests/test_arduboy30_ardugo.py` copies the real game and compat headers
into a temp tree, includes the real NanoUI `geometry.h` and the launcher's
inherited Castle Boy/Arduboy 3D macros first, then compiles and runs
`tools/tests/fixtures/arduboy30_ardugo.cpp`. It covers title rendering, tilt
cursor movement, black placement, occupied-point rejection, the real
liberty/capture/score path, a deterministic legal AI reply, B restart, the
shared hold exit, canvas detach and clean reentry. Set
`CRABIT_ARDUBOY_FRAMES=<dir>` for PBM frames. Host-only; not embedded or
hardware acceptance.
