# RYSK BW16 port

Actual source/assets from [obono/ArduboyWorks, `rysk/`](https://github.com/obono/ArduboyWorks/tree/d4b1f041789dcd1d71907654e4025d613b4ab420/rysk),
commit `d4b1f041789dcd1d71907654e4025d613b4ab420`, game version 0.10
(March 2020), upstream library dependence Arduboy2 5.2.1. RYSK is a single
`.ino` sketch and has no per-game license file; `LICENSE` is the unchanged
repository-root MIT license, Copyright (c) 2016–2026 OBONO. Title/start/over
bitmaps, figure/ten/clock assets, cookie behavior, timer, scoring and LED score
tables are the original assets; this is not a remake.

## Play

- Tilt: move the player around the 128×64 field.
- A or B (single tap): start the 60-second game; at game over, start again.
- Hold PAT for the shared exit gesture.
- Goal: collect as many cookies as possible before the timer ends. Each cookie
  respawns away from the player; the clock, figure counter and (on hardware)
  the RGB LED encode the score.

## Port changes and limits

- Original sketch, start/over screen logic, `movePlayer`, cookie hit/respawn,
  score/time update, LED score encoding, animations and all bitmaps are retained.
  `.ino` and `MyArduboy2.cpp` bodies are renamed `rysk.inc.h` and
  `MyArduboy2.inc.h` for Arduino header staging; the whole sketch lives in
  `RyskSandbox` and keeps its file-scope names.
- `MyArduboy2` derives from the shared `Arduboy2Compat` (its own copy of the
  compact font, belt-line helpers and text cursor behavior are kept). AVR
  timers, bootloader/OLED panic code and the Playtune implementation are not
  compiled: `MyArduboyPlaytune` is excluded and every `playTone`/`playScore`
  call is a silent no-op, so no speaker output exists.
- The line/fill helper advances the buffer row only while rows remain; upstream
  formed an out-of-range pointer after the last row (no write changed).
- RYSK uses `ALIGN_LEFT/CENTER/RIGHT` as enum values while compat defines the
  same names as macros for text drawing. `port.h` pushes, undefines and pops
  those macros locally so the sketch's own values are used without changing the
  shared header.
- No EEPROM/record: the game keeps no persistent data and never touches BW16
  settings, FTL or the engine's RAM record.
- 128×64 monochrome, nominal 60 Hz. Tilt directions, A/B tap semantics, pacing
  and the exit hold come from the shared `ArduboyCompatApp`; the port uses no
  shared geometry types (no Point/Rect).
- No compressed bitmap format is used. Host tests and builds are not watch
  acceptance; timing, controls and embedded behavior still need main-AI build
  and user hardware validation.

## Test

`tools/tests/test_arduboy30_rysk.py` copies the real game and compat headers
into a temporary tree with stub Arduino/NanoUI/AmebaStore headers (and the real
NanoUI `geometry.h`), then compiles and runs
`tools/tests/fixtures/arduboy30_rysk.cpp`. The fixture drives the original
title, starts with A, moves the real player with tilt, seeds one original cookie
onto the player to exercise the real pickup/respawn path, ends the session with
the original timer, restarts with A, exits through the shared hold and remounts
to check the fresh state (and that B also starts, as upstream accepts A|B). It
also writes PBM frames when `CRABIT_ARDUBOY_FRAMES=<dir>` is set. It passed
locally using the actual source and compat tree; it is not an embedded or
hardware test.
