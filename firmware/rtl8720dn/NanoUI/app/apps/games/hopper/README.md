# Hopper

Full upstream OBONO Hopper, ported to the shared Arduboy compatibility layer.
This is the real game, not the earlier native simplified remake.

## Source and licence

- Upstream: [OBONO/ArduboyWorks](https://github.com/OBONO/ArduboyWorks),
  pinned commit `d4b1f041789dcd1d71907654e4025d613b4ab420`, directory
  `hopper/` (`hopper.ino`, `common.h`, `logo.cpp`, `title.cpp`, `game.cpp`,
  `MyArduboy.*`, `info.json`).
- `LICENSE` is the unchanged root MIT notice, Copyright (c) 2016-2026 OBONO.
- Original game/title/logo bodies are preserved as include-only `.inc.h` files
  under `HopperSandbox`; the checkable source keeps the 50-floor levels, five
  platform types (normal, moving, vanishing, small, goal), clear/next-level,
  pause, animations/background, BEST 10 record menu, sound menu and credit page.

Local adaptation, not gameplay rewriting:

- The AVR sketch/translation units are included once instead of compiled as
  separate files. Function tables use native typed pointers; pointer-sized
  `pgm_read_word` reads and `(int)pointer % 4` addressing were removed.
- All mutable state lives in one typed `HopperSandbox::State` created on demand
  by `CrabitArduboy::createPortState()` (engine, 1 KiB framebuffer, RAM EEPROM,
  floors, chips, title/menu and animation fields). There is no large static
  game object.
- `Arduboy2Compat` supplies drawing, input edges, frame pacing and the 1 KiB RAM
  EEPROM. `MyArduboy2.h` keeps the upstream `tunes`/`audio` call surface as
  silent stubs. Text uses the shared 5x7 NanoUI font instead of upstream's 6x6
  glyphs at the original line positions, so tight 6-pixel menus look one pixel
  denser; layout, strings and menu behaviour are unchanged.
- EEPROM reads/writes use the compat RAM EEPROM at the original address 800;
  the record layout and checksum are unchanged. The runtime copies it to a
  retained session buffer on App destruction and back on the next mount. This is
  **RAM only**: reset or power loss loses the record; there is no Flash
  persistence and no AVR EEPROM/Flash import.

## Play

- Tilt: move the player. The shared adapter maps tilt to the original d-pad.
- PAT single tap (A): primary confirm/start/pause/resume.
- PAT double tap (B): the same confirm/start/pause/resume in Hopper. The game
  does not need distinct A/B actions, so no per-game shared remapping is needed.
- PAT hold: shared adapter exit gesture.
- Title menu: tilt up/down selects START GAME, SOUND, RECORD, CREDIT; a PAT tap
  confirms. The record page shows BEST 10 scores, play count and play time.
- Gameplay: land on platforms to climb. Goal platforms clear the level; the
  original 96-frame spring animation leads to the next level. Press A/B to
  pause; falling below the first platform ends the game and writes the record.

## Audio and persistence limits

- Real audio is **not implemented** in the shared compatibility backend. The
  upstream `playScore2`/`tunes` calls are preserved and compile against silent
  stubs; the sound menu only toggles a RAM flag and the ON/OFF indicator. Do not
  expect audible sound.
- Scores are RAM-session records only. They survive App exit/re-entry while the
  process lives and are lost on reset/power loss.
- 60 Hz pacing follows the upstream `setFrameRate(60)`/`nextFrame()` gate; no
  artificial extra frame limiter or alternate timing is added.

## Integration note (main AI)

The shared `ArduboyPortRuntime.h` now exposes 22 slot bits, so Hopper's reserved
`kPortId = 21` is live and the tests compile that production header unchanged.
What remains is launcher integration: every profile factory path must launch
Hopper through the prepared `CrabitArduboy::tryCreate<AppHopper>()` (or the
registry `builtInEntryFor<AppHopper>` row), never `new AppHopper()` directly,
because a raw construction leaves `port_ptr` null. This package does not modify
shared compat/runtime/launcher/builder files. `getAppName()` is `Hopper`.

## Verification

`tools/tests/test_hopper_full.py` compiles the actual port with Realtek-style
16-bit `pgm_read_word`/`pgm_read_ptr` macros and hostile Castle Boy/Hollow
macros. It covers logo -> title, sound toggle, BEST 10/record pages, game start,
the first 4,096-Z fall, tilt movement, pause/resume, goal/CLEAR/next-level, all
five platform draw paths, game over/timeout, record write and checksum bytes,
destroy/recreate RAM-save restore, same-App re-entry reset, duplicate-launch
refusal, allocation accounting and PAT-hold exit. A second compile stages the
real old-six + all catalogue header order. Set
`CRABIT_HOPPER_FULL_FRAMES=<dir>` to write PBMs.

Host evidence only. No BW16 embedded build, Flash/FTL fit, device timing,
audio, hardware input or acceptance is claimed.
