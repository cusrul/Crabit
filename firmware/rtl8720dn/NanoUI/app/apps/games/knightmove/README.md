# Knight Move — BW16 source port

Original game and assets: [OBONO / ArduboyWorks, knightmove](https://github.com/obono/ArduboyWorks/tree/d4b1f041789dcd1d71907654e4025d613b4ab420/knightmove),
pinned commit `d4b1f041789dcd1d71907654e4025d613b4ab420`.
Copyright (c) 2016-2026 OBONO, MIT; `LICENSE` is copied verbatim from that
commit's repository root (no separate per-game file exists).

## Controls

- Tilt steers the knight one chess move; B (watch single tap) starts fast
  rolling (a tap, not a hold) and confirms menu items. A (watch double tap)
  opens the pause menu. No sustained A/B gameplay.
- Title menu: GAME A/B START, RECORD, CREDIT; B confirms.
- PAT hold >= 800 ms exits. RGB LED and sound remain silent no-ops.

## Port changes

`port.h` composes the upstream units as `.inc.h` under `KnightMoveSandbox`,
keeping logo/title/game in separate namespaces. `KnightMoveApp` is frame-driven
on `ArduboyCompatApp`.

- CastleBoy's `STATE_*` macros are visible in the combined launcher TU, so the
  port pushes and undefines the nine known names around its own enums, then
  restores them. This is required for `STATE_TITLE` in the title enum.
- The state handler table is called through a typed function-pointer array;
  Realtek's 16-bit `pgm_read_ptr` would truncate native pointers.
- The legacy Arduboy 1.1 `MyArduboy` API is mapped onto `Arduboy2Compat`:
  frame/button edges, text, sprites, fill/draw helpers. Audio and Playtune calls
  are silent stubs with a RAM-only sound flag.
- EEPROM helpers use bounded RAM EEPROM with byte-wise checksum reads. Records
  use the native 28-byte ARM layout, not the upstream 26-byte AVR wire layout.
  The retained fallback migration code is not validated AVR import support:
  the primary checksum at 688–689 overlaps its signature. Native primary-slot
  save/recreate/read round trips work; no Flash persistence or AVR import.
- Shared 5x7 text/clipping replaces the local font helper; field/floor bitmaps,
  heart cursor, knight animation, scoring, menus, title art and LED effects are
  retained. `strlen_P` becomes `strlen` because PROGMEM is empty on this target.
- All game/title/menu/EEPROM state and cursors reset on mount. No blocking
  waits, tasks or SDK timers were added.

Host evidence only: `tools/tests/test_arduboy30_knightmove.py` compiles the real
sources with real NanoUI geometry, Arduino bit macros, the 16-bit `pgm_read_ptr`
shape and CastleBoy's leaked macros, then checks title, start, fast-roll
scoring, pause/continue, exit, PBM frames and re-entry reset. Embedded/RAM/
hardware acceptance is main-AI/user work.
