# Quarto! BW16 port

Actual source/assets from [obono/ArduboyWorks, `quarto/`](https://github.com/obono/ArduboyWorks/tree/d4b1f041789dcd1d71907654e4025d613b4ab420/quarto),
commit `d4b1f041789dcd1d71907654e4025d613b4ab420`, game version 0.10
(October 2019), upstream library dependence Arduboy 1.1.1. `LICENSE` is the
unchanged repository-root MIT license, Copyright (c) 2016–2026 OBONO. Title,
logo, piece/player/cursor bitmaps, instruction and credit text, all 16 pieces,
4-in-line/2×2 rules, CPU alphabeta and menus are the original assets; this is
not a remake.

## Play

- Tilt: move the cursor on the board; left/right browse the offered pieces;
  up/down selects a title/menu item.
- B (watch single tap): place the held piece on the highlighted empty square;
  offer the highlighted piece to the opponent; confirm a menu item.
- A (watch double tap): cancel a placement (board returns to PLACING); open the
  in-game menu (CONTINUE, CANCEL LAST MOVE, QUIT GAME); on the result screen
  open RETRY GAME / BACK TO TITLE.
- Hold PAT for the shared exit gesture.
- Goal: be the player who places a piece completing a line (or an optional 2×2
  square) of four pieces sharing one attribute. Title choices are YOU FIRST,
  CPU FIRST, 2 PLAYERS, SETTINGS, INSTRUCTION and CREDIT; levels 1–5 unlock with
  wins. Settings are APPROVE 2², THINKING LED, INVERT SCREEN and HINT (VS CPU).

## Port changes and limits

- Original logo/title/game/menu/instruction/credit code, the full piece
  attribute set, line/2×2 detection, CPU search and level progression,
  animations, counters and bitmaps are retained. `.cpp`/`.ino` bodies are
  renamed `.inc.h` for Arduino header staging. `logo.cpp` and `title.cpp` each
  own `enum STATE_T`, `state` and `counter`, so each sits in a nested
  `Logo`/`Title` namespace; game/menu stay at `QuartoSandbox` scope.
- `MyArduboy` derives from the shared `Arduboy2Compat`. Its compact font,
  clipped helpers and text cursor behavior are kept; AVR timers, bootloader/OLED
  panic paths and score playback are not compiled. Audio is silent.
- The AVR OLED invert command is not available through the compat engine.
  `invertScreen` now records the setting, and the main loop flips the framebuffer
  to a temporary inverted copy for `display()`, then restores it. Restoring
  matters because the title/result screens redraw incrementally; flipping the
  live buffer in place would toggle on alternate frames.
- The optional UP+DOWN "hide overlay" gesture in the result/choosing screens is
  retained in the original code but is unreachable with this device's controls:
  tilt cannot produce UP and DOWN at the same time, and no button is assigned to
  it. Core Quarto play is unchanged; this is documented rather than remapped.
- Module dispatch uses typed native function pointers, not 16-bit
  `pgm_read_word` casts. The instruction page pointer is read natively
  (`instList[instPage]`), never through the launcher's bad `pgm_read_ptr`.
  `strlen_PF((uint_farptr_t) ...)` became `strlen` for the const result label;
  the sound table's 260 ms delay is `(byte) 260`; `memcpy_P` maps to `memcpy`
  locally in `port.h`.
- `MyArduboy.h` uses the unique guard `QUARTO_MYARDBUOY_H` instead of only
  `#pragma once`: on the MinGW host, byte-identical helper headers from two
  ports can be mistaken for the same file, which would silently hide the class
  in a combined translation unit. Port-level macros (EEPROM address/signature,
  pad timing, dispatch helpers) are undefined at the end of `port.h`.
- EEPROM is the engine's private RAM-only 1 KiB emulation: the record at address
  992 (game mode, CPU level/max level, settings, play frames/count) survives app
  reentry only. It is not Flash-persistent and never touches BW16 settings/FTL.
  The checksum reads record bytes through `memcpy`, not a `uint16_t*` alias.
  Redundant `static` on `eepRead*`/`eepWrite*` was removed (upstream needed
  `-fpermissive`), and `title.cpp`'s `STATE_TITLE` is shielded from the
  launcher macro with a local push/undef/pop in `app.h`.
- 128×64 monochrome, nominal 60 Hz. Tilt directions, A/B tap semantics, pacing
  and the exit hold come from the shared `ArduboyCompatApp`.
- No compressed bitmap format is used. Host tests and builds are not watch
  acceptance; timing, controls and embedded behavior still need main-AI build
  and user hardware validation.

## Test

`tools/tests/test_arduboy30_quarto.py` copies the real game and compat headers
into a temporary tree with stub Arduino/NanoUI/AmebaStore headers (and the real
NanoUI `geometry.h`), then compiles and runs
`tools/tests/fixtures/arduboy30_quarto.cpp`. The fixture drives title →
2 PLAYERS with real tilt/menu input, makes a real placement and the real offered-
piece choice (including the 16-frame move animation), checks that an occupied
square is refused, then seeds a legal row of pieces 0/2/4 and places piece 6 to
complete a real shared-attribute line, reaching `STATE_OVER` with `2P WINS` and
the real record write. It returns to the title through the result menu, persists
the original INVERT SCREEN setting through `writeRecord`, exits with the shared
hold and remounts to verify the RAM record and the inverted framebuffer/canvas.
The winning position is a seeded legal terminal position, not a complete
input-driven game. It also writes PBM frames when `CRABIT_ARDUBOY_FRAMES=<dir>`
is set. It passed locally; it is not an embedded or hardware test.
