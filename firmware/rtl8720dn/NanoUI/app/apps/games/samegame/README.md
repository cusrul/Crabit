# SameGame BW16 port

Actual source/assets from [obono/ArduboyWorks, `samegame/`](https://github.com/obono/ArduboyWorks/tree/d4b1f041789dcd1d71907654e4025d613b4ab420/samegame),
commit `d4b1f041789dcd1d71907654e4025d613b4ab420`, version 0.10 (December 2019).
Retrieved via GitHub REST directory listing and pinned raw URLs. `LICENSE` is
the upstream root MIT license, Copyright (c) 2016–2026 OBONO, unchanged.

## Play

- Directions: move cursor; in menus, up/down selects an item.
- B (watch single tap): confirm a menu item / remove the highlighted connected
  group of at least two matching tiles. Larger groups score more; clearing all
  tiles earns the original perfect bonus.
- A (watch double tap): open the in-game menu (continue, one-move undo, retry,
  quit).
- Hold up+down for the shared wrapper's exit gesture.
- Original pattern editor retained: unlock with a score of at least 1000 or
  one hour of accumulated play during this powered session. Select a tile with
  directions, B to design, B to draw/erase dots, A to select/menu. Editor menus
  retain discard, restore-default, and quit behavior.

## Port changes and limits

- Original game, menu, title, animated logo, graphics/font, matching, scoring,
  one-move undo and editor code retained; not a gameplay remake.
- `.cpp` and `.ino` bodies renamed `.inc.h` for Arduino header staging without auto compilation.
  `port.h` isolates former translation units in nested namespaces inside
  `SameGameSandbox` and clears their macros after inclusion.
- `SameGameApp` uses the shared frame-mode `ArduboyCompatApp` / `Arduboy2Compat`;
  no blocking startup, AVR hardware/audio library, assembly or pointer-width
  assumptions. Function/score dispatch uses typed native pointers.
- `MyArduboy` retains the upstream compact font and clipped buffer helpers;
  local text/cursor handling avoids dependence on AVR Print internals.
- All mutable game/UI/animation state resets on app reentry. EEPROM is only
  the engine's private RAM emulation: records/custom patterns survive visits
  only when saved by upstream game/editor paths, not power loss or reset.
  There are no actual EEPROM/Flash writes and no persistent saves.
- Sound and RGB output are absent; original score arrays are retained as
  source assets, but the sound toggle remains off and playback is a no-op.
- Corrected the upstream negative/out-of-range Morse logo shift and an
  unnecessary next-page buffer write for tiles ending at the bottom edge;
  checksum reads avoid strict-aliasing assumptions.
- Flood fill rejects column `FIELD_W` (the first out-of-range column), including
  groups reaching the right edge of the board.
- 128×64 monochrome, nominal 60 Hz; physical buttons only (no tilt), pacing and
  app exit are supplied by the shared adapter. Host tests/builds are not watch acceptance.

Main integration owns menu/profile wiring and combined native/embedded checks.

`native_smoke.cpp.inc.h` is a standalone host logic test (compile with `g++
-std=c++11 -x c++ native_smoke.cpp.inc.h -o <temporary executable>`). It uses an
explicit engine stub, not the actual NanoUI adapter, and checks title/start,
original flood fill/removal/undo, editor unlock, complete reentry resets,
RAM-only records and custom text rendering. It passed locally; actual adapter,
embedded build, timing, controls and device acceptance are separate checks.
