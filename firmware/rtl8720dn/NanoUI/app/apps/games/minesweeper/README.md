# Minesweeper BW16 port

Actual source from [Hexagenic/minesweeper-arduboy](https://github.com/Hexagenic/minesweeper-arduboy),
commit `1bd5e95a1d4ff20c510045b1d9d7c89218275ee8`, 2017. `LICENSE` is the
upstream MIT license (Copyright (c) 2017 Mattias Hermansson), unchanged;
`UPSTREAM_README.md` is the original repository README. The complete 9×9
Minesweeper game (first-click mine placement, neighbour flood reveal, flags,
win/lose result screens, reset) is retained.

## Play

- Tilt: move the 9×9 cursor; the view follows at the board edges.
- PAT single tap (A): reveal the cell under the cursor, or start a new game on
  the win/lose screen.
- PAT double tap (B): place/remove a flag. Because B is the adapter's
  double-tap gesture, flagging has a short deliberate delay.
- Hold PAT at least 800 ms: shared adapter exit gesture.
- No title screen is invented; the original game begins at the board. The app
  name shown by NanoUI is `Minesweeper`.

## Port changes and limits

- The single upstream `.ino` is included as `minesweeper.inc.h` inside
  `MinesweeperSandbox`; the real game/board/result logic is unchanged.
- `Arduboy2` is a game-local subclass of `Arduboy2Compat` that provides the
  upstream `setTextBackground()` call (the shared class exposes
  `setTextColor(color, background)` only). `ArduboyPlaytune` is a thin
  constructor wrapper over the shared silent Playtune stub; `PIN_SPEAKER_*`
  constants are local. No shared compatibility API was changed.
- Frame pacing, canvas, input edges and the PAT-hold exit belong to
  `ArduboyCompatApp`. The game uses no EEPROM, no Flash writes, no compressed
  bitmaps, no FX, no greyscale and no multiplayer.
- All mutable grid/cursor/result state resets on app re-entry.
- Audio is silent compatibility; the original win/lose score arrays remain
  source data. Host tests/builds do not imply watch hardware acceptance.

## Host verification

`tools/tests/test_arduboy30_minesweeper.py` compiles the real port and shared
compat layer with host-only Canvas/InputDriver/AmebaStore fakes and real NanoUI
geometry included first. It covers a real first click (exactly ten mines and a
revealed cell), cursor movement, flag/unflag, A-on-flagged, real mine loss via
`clickA`, result-pause reset, the `countHidden()==10` win path, non-empty actual
frames (PBM capture via `CRABIT_ARDUBOY_FRAMES`), clean re-entry and the shared
exit gesture. No embedded build or watch acceptance is claimed.

Main integration owns menu/profile and split-image wiring.
