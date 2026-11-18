# Galaxion BW16 port

Actual source/assets from [tako2/galaxion](https://github.com/tako2/galaxion),
commit `8cfbafdf57d0dae448c64f6576de788d8105bec8`, root MIT (tako2, 2017).
`LICENSE` is the upstream MIT text unchanged. The real game loop, title mode
selection, convoy, launched aliens, boss waves, ship movement, single missile,
collision/scoring and game-over states are retained; this is not a remake or a
template.

## Play

- Directions (tilt): move the ship left/right; up/down move the title cursor
  (mode, sound, auto fire).
- A or B (PAT tap/double tap): fire the original single missile; A/B also
  start/restart and confirm title items. No button hold is required.
- Auto fire is ON by default and only fires while the ship is in the real
  battle state (`GAME_ST_PLAY`): the title, the five-second stage entry and the
  game-over screen stay manual. The title's third row, `AUTO: ON/OFF`, is
  selected with up/down and switched with left/right; the choice lasts for the
  powered session. With it off, the manual A/B tap is the only fire path.
- Hold up+down (PAT hold): shared adapter exit gesture.
- Title order is the original: left/right chooses Practice/Normal, up/down
  moves between rows, and A/B starts. Practice has the original wider hit
  window; Normal requires the original precise alignment.

## Port changes and limits

- Original `common.h`/`game.h`/`ship.h`/`enemy.h` and `.cpp` bodies are isolated
  under `GalaxionSandbox` as `.inc.h`; `port.h` hides/restores Castle Boy's
  conflicting macros at the boundary and clears Galaxion's own macros.
- All function dispatch uses direct typed calls/indexing, not Realtek's 16-bit
  `pgm_read_ptr`; there are no pointer-sized `pgm_read_word` reads. Bitmap bytes
  remain table data read with the compatible byte accessors.
- `CAPTURE_SCREEN`/`Serial` capture code is disabled; the port is frame-driven
  and has no blocking transitions. The original disabled `#if 0` credit-tone
  delay block remains unreachable.
- Audio is silent via a local `tunes` stub. The original `sound.h` was generated
  from MIDI files that are not present in the upstream tree, so **no generated
  music/tone data is imported**; one-byte silent handles preserve the call
  sites. This removes the unverified music-source rights from the port.
- Original dynamic `Position`/`Enemy`/`Explosion`/`BossScore` objects are still
  released by the original ownership paths. A new `Game::reset()` plus
  `Convoy::init()` cleanup frees the ship missile, launched enemies, explosion
  and boss-score objects; `GalaxionApp::onUnmount()` and app entry call it, so
  repeated visits cannot accumulate heap objects. Host tests use an allocation
  counter to verify zero live objects after unmount.
- No EEPROM or Flash writes and no persistence; the high score is RAM-only for
  the powered session. The port does not use compressed bitmaps, AVR asm,
  timers, SDK services or side-button remapping.
- 128x64 monochrome, nominal 60 Hz; the original float fixed-point helpers and
  `Position` scaling are retained.
- Auto fire is a port addition, not upstream behaviour. The title screen gains
  one `AUTO: ON/OFF` row between the silent sound row and the footer hint; the
  battle gate reads `GAME_ST_PLAY`, and the fire site only treats the missile
  key as pressed when that gate is open. The preference is session-scoped (the
  port has no EEPROM) and ON by default.

## Verification

`tools/tests/test_arduboy30_galaxion.py` compiles the real `app.h` adapter,
real `Arduboy2Compat` and actual game sources on the host with Realtek's 16-bit
`pgm_read_ptr`/`pgm_read_word` shape and Castle Boy macros. It asserts legacy
macro restoration and Galaxion non-leakage, then covers logo/title, Practice
selection/start, 5-second stage entry, tilt movement, real A-shot allocation and
release, a real convoy hit and score, launched-enemy trajectory, ship
destruction -> 5-second respawn -> game over -> A restart, and unmount/reentry
with zero live heap objects. PBMs cover title/play/hit/game-over; inputs are
deterministic 17 ms frames, tilt and PAT taps. Set
`CRABIT_ARDUBOY30_GALAXION_FRAMES=<dir>` to write PBMs.

Host evidence only. No BW16 embedded build, Flash/FTL fit, device run, timing or
hardware acceptance is claimed; those remain main-AI integration checks.
