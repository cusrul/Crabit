# Chie Magari Ita BW16 port

Actual source/assets from [obono/ArduboyWorks, `chiemagari/`](https://github.com/obono/ArduboyWorks/tree/d4b1f041789dcd1d71907654e4025d613b4ab420/chiemagari),
commit `d4b1f041789dcd1d71907654e4025d613b4ab420`, game version 0.11
(March 2018), upstream library dependence Arduboy 1.1.1. `LICENSE` is the
upstream repository-root MIT license, Copyright (c) 2016–2026 OBONO, unchanged.
All ten pieces, pattern data, title/logo/gallery bitmaps, credits and menu code
are the original assets; this is not a remake.

## Play

- Directions (tilt): move the cursor on the board; in menus, up/down select an
  item.
- B (watch single tap): pick the piece under the cursor; while holding a
  piece, release it and test the arrangement; in menus, confirm an item.
- A (watch double tap): while holding a piece, enter rotate mode — a
  horizontal tilt rotates it, an up/down tilt flips it, A again continues
  moving; on the board, A opens the menu; in menus, A is CONTINUE.
- Rotate/flip is a held A modifier upstream, but the PAT adapter has no held
  gesture. Each A double tap reaches the game as the same three-frame pulse the
  engine already received, which rotates or flips once while a tilt direction
  is held during that window. Continuous or repeated rotation by holding is not
  claimed.
- Hold PAT for the shared exit gesture.
- Goal: place all ten pieces into the frame without overlapping them. A new
  arrangement is added to the gallery; repeating a known arrangement is shown
  as already found. The menu also has SOUND, SHOW HELP, CREDIT and (on the
  board) RESET; SOUND and SHOW HELP are persisted settings.

## Port changes and limits

- Original logo, menu, puzzle, gallery, credit, piece shapes/rotations, frame
  rules, move/put/rotate/flip logic, solved-pattern codec (10 one-byte codes)
  and scoring/gallery counters are retained.
- `.cpp`/`.ino` bodies are renamed `.inc.h` for Arduino header staging; each
  former translation unit keeps its file-scope names inside
  `ChiemagariSandbox`. Only `menu.inc.h` needs a nested `Menu` namespace because
  `menu.cpp` and `puzzle.cpp` both declare `enum STATE_T`; the gallery's
  `toDrawAll` flag is renamed `galleryToDrawAll` for the same merged-TU reason.
- `MyArduboy` derives from the shared `Arduboy2Compat` instead of Arduboy 1.1.1.
  The original 6×6 compact font, clipped buffer line/rect helpers and text
  cursor behavior are kept. No AVR timers, bootloader/OLED panic path, direct
  UART, assembly or 16-bit pointer assumptions remain; module dispatch uses
  typed native function pointers.
- EEPROM is the engine's private RAM-only 1 KiB emulation. Record, gallery,
  play time, SOUND/SHOW HELP flags survive app reentry only; they are not
  Flash-persistent and never touch BW16 settings/FTL. Patterns are stored in
  the original 10-byte slots at address 32 and the record at 864. Upstream had
  no capacity guard, so filling 83 slots would overwrite the record; this port
  stops registering new patterns at that point (existing arrangements still
  match) instead of corrupting the record.
- Sound is silent: no speaker/score playback. The menu's ON/OFF state and its
  save/restore path are retained so the original UI/record behavior stays
  honest; score byte arrays remain as source assets. RGB LED writes are no-ops.
- Upstream logo shifted a 32-bit constant by counts up to 35 and, near the end,
  by negative counts (AVR happened to mask them). The port makes the same 5-bit
  repeat explicit and shows no signal for the last 15 frames.
- `puzzle.cpp` declared `static void handleDPad(void);` after `common.h` had
  already declared it externally. That only compiled because Arduino uses
  `-fpermissive`; the `static` is removed here. The checksum reads no longer
  alias the piece/code arrays through `uint16_t*`, and the play-time string uses
  `snprintf` with the correct conversion.
- The vertical-line/fill helper advances the buffer row only while more rows
  remain (upstream formed an out-of-range pointer after the last row; no write
  changed).
- 128×64 monochrome, nominal 60 Hz. Tilt directions, A/B tap semantics, pacing
  and the exit hold come from the shared `ArduboyCompatApp`; the port uses no
  shared geometry types (no Point/Rect).
- No compressed bitmap format is used. Host tests and builds are not watch
  acceptance; timing, controls and embedded behavior still need main-AI build
  and user hardware validation.

## Test

`tools/tests/test_arduboy30_chiemagari.py` copies the real game and the real
compat headers into a temporary tree with stub Arduino/NanoUI/AmebaStore
headers, then compiles and runs `tools/tests/fixtures/arduboy30_chiemagari.cpp`.
The fixture drives logo→title, starts the puzzle, moves with tilt, picks,
rotates (A + tilt), moves and releases a held piece, navigates/continues the
menu, toggles SOUND, then seeds the non-overlapping ten-piece packing found
with the game's own board rules and lets the real release path register it as a
new pattern, checks the duplicate-arrangement branch, round-trips the stored
pattern through the original codec, enters the gallery, exits with the shared
hold gesture and remounts to check the RAM-only record/gallery restore. Set
`CRABIT_ARDUBOY_FRAMES=<dir>` to also write PBM frames. It passed locally; it is
not an embedded or hardware test.
