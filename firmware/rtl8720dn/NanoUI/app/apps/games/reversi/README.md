# Reversi BW16 port

Actual source/assets from [obono/ArduboyWorks, `reversi/`](https://github.com/obono/ArduboyWorks/tree/d4b1f041789dcd1d71907654e4025d613b4ab420/reversi),
commit `d4b1f041789dcd1d71907654e4025d613b4ab420`, game version 0.11
(November 2018), upstream library dependence Arduboy 1.1.1. `LICENSE` is the
unchanged repository-root MIT license, Copyright (c) 2016–2026 OBONO. Logo,
title, credit and stone/player bitmaps, sound score bytes, the board rules, CPU
search and level/menu code are the original assets; this is not a remake.

## Play

- Tilt: move the cursor on the board; up/down selects an item in menus.
- B (watch single tap): place a stone on the highlighted legal square; confirm
  a menu item; in the result screen, restore the finished board.
- A (watch double tap): open the in-game menu (CONTINUE, CANCEL LAST MOVE,
  SUSPEND, QUIT); in the result state, open RETRY GAME / BACK TO TITLE.
- Hold PAT for the shared exit gesture.
- Goal: finish an 8×8 game with more stones than the opponent. Title choices are
  RESUME (after a suspend), AS BLACK, AS WHITE, 2 PLAYERS, SETTINGS and CREDIT;
  CPU levels 1–5 unlock as the stored win count grows. Settings are THINKING
  LED, STONES COUNTER, SHOW PLACEABLE and SHOW FIXED.

## Port changes and limits

- Original logo/title/game/menu code, legal-move and fixed-stone analysis, CPU
  alphabeta levels, flipping animation, pass/double-pass handling, win/lose
  labels, counters and bitmaps are retained. `.cpp`/`.ino` bodies are renamed
  `.inc.h` for Arduino header staging. They share one translation unit, so
  `logo.cpp` and `title.cpp` each live in a nested `Logo`/`Title` namespace
  (both declare `enum STATE_T`, `state` and `counter`); game/menu stay at
  `ReversiSandbox` scope.
- `MyArduboy` derives from the shared `Arduboy2Compat` instead of Arduboy 1.1.1.
  Its compact font, clipped line/rect helpers and text cursor behavior are kept.
  AVR timers, bootloader/OLED panic paths and score playback are not compiled;
  audio calls are silent no-ops.
- Module dispatch uses typed native function pointers instead of
  `pgm_read_word` casts. `strlen_PF((uint_farptr_t) ...)` became `strlen` for
  the normal const result label. The sound table's 260 ms delay is written
  `(byte) 260`. `memcpy_P` is mapped to `memcpy` locally in `port.h`.
- EEPROM is the engine's private RAM-only 1 KiB emulation: the record at address
  928 (game mode, CPU level, settings, play frames/count and win/lose counters)
  survives app reentry only. It is not Flash-persistent and never touches BW16
  settings/FTL. The checksum reads record bytes through `memcpy`, not a
  `uint16_t*` alias.
- `MyArduboy.h` uses the unique guard `REVERSI_MYARDBUOY_H` instead of only
  `#pragma once`: on the MinGW host, byte-identical helper headers from two
  ports can be mistaken for the same file, which would silently hide the class
  in a combined translation unit. The other former translation units keep their
  upstream pragma-once/guard behavior; port-level macros are undefined at the
  end of `port.h`.
- `common.h` already declared `eepRead*`/`eepWrite*` externally while
  `common.cpp` repeated them as `static`; that only built with `-fpermissive`.
  The redundant `static` is removed. `title.cpp`'s `STATE_TITLE` enumerator is
  shielded from the launcher's `STATE_TITLE` macro with a local
  push/undef/pop in `app.h` (the macro is restored afterwards for other code).
- 128×64 monochrome, nominal 60 Hz. Tilt directions, A/B tap semantics, pacing
  and the exit hold come from the shared `ArduboyCompatApp`.
- No compressed bitmap format is used. Host tests and builds are not watch
  acceptance; timing, controls and embedded behavior still need main-AI build
  and user hardware validation.

## Test

`tools/tests/test_arduboy30_reversi.py` copies the real game and compat headers
into a temporary tree with stub Arduino/NanoUI/AmebaStore headers (and the real
NanoUI `geometry.h`), then compiles and runs
`tools/tests/fixtures/arduboy30_reversi.cpp`. The fixture drives title →
AS BLACK → LEVEL 2, waits for the original four-stone opening animation, tilts
the cursor, makes a real legal placement and lets the CPU reply, checks that an
occupied square is refused, opens/continues the in-game menu, then seeds a
full-board double pass to exercise the real `finalizeGame`/`BLACK WINS`/record
path. It exits with the shared hold and remounts to verify the RAM record
(play count/frames) survived. The full board is a synthetic terminal position
used only to reach the real finalize/win/record path; it is not a complete
input-driven game. It also writes PBM frames when
`CRABIT_ARDUBOY_FRAMES=<dir>` is set. It passed locally; it is not an embedded or
hardware test.
