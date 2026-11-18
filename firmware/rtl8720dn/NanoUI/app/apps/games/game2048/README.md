# 2048 BW16 port

Actual source from [awisse/2048-Arduboy](https://github.com/awisse/2048-Arduboy),
commit `038b34bf165f4fedee6d82f303651ce8e6eec7f2`, 2024. `LICENSE` is the
upstream MIT license (Copyright (c) 2024 awisse), unchanged;
`UPSTREAM_README.md` is the original repository README. The complete
board/merge/reward/EEPROM implementation is retained; the desktop SDL harness
in the upstream repository is not part of this port.

## Play

- Tilt: move tiles left/right/up/down (one move per press/release; the original
  controller debounces held directions).
- PAT single tap (A): start from the frame-driven start screen; in play, the
  original A event starts a new game when one is not running.
- PAT double tap (B): save the current game/high score to the engine's private
  RAM EEPROM.
- Hold PAT at least 800 ms: shared adapter exit gesture.
- The original A-long-press save and B-long-press reset paths require a held
  button and are therefore unavailable under the fixed PAT mapping. B's normal
  save is retained; the original A/B semantics are otherwise unchanged.

## Port changes and limits

- `.ino/.cpp` bodies are included as `.inc.h` inside `Game2048Sandbox`; the
  original board, merge, scoring, reward animation, stars, controller and
  EEPROM record layout are retained.
- The original blocking `while (!pressed(A))` setup wait is now a frame state:
  `2048 / PRESS A` is drawn without blocking, and A seeds and starts the game.
- `Arduboy2Base` maps to `Arduboy2Compat`; `EEPROM` is a game-local view over
  the active compat engine's bounded 1024-byte RAM EEPROM. Only offsets 16.. are
  written; no real EEPROM/Flash is touched. Records survive app re-entry while
  the engine lives, not power loss.
- Input masks were wired to the shared `LEFT/RIGHT/UP/DOWN/A/B` constants; the
  upstream `INPUT_*` names remain. `board_full`/controller transient state and
  the game board reset on entry. `pgm_read_ptr` is not used for data/function
  pointers (the real Realtek macro is 16-bit); no compressed bitmap or FX path
  is used.
- Audio is silent compatibility; no shared API was added. The game-local
  `EEPROM` view and `Arduboy2Base` shim are contained in `port.h`.

## Host verification

`tools/tests/test_arduboy30_game2048.py` compiles the real port and compat layer
with host-only Canvas/InputDriver/AmebaStore fakes and real NanoUI geometry
included first. It covers the frame-driven start, initial two tiles, real merge
(`2+2` → one tile, score 4), four-equal no-chain merge, full-board game over,
new game, RAM EEPROM signature and high-score reload after exit/re-entry, B save
through the adapter, real non-empty PBM frames and the PAT-hold exit. No embedded
build or watch hardware acceptance is claimed.

Main integration owns menu/profile and split-image wiring.
