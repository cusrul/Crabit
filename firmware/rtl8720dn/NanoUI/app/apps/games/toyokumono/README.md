# Toyokumono BW16 port

Actual source/assets from [obono/ArduboyWorks](https://github.com/obono/ArduboyWorks),
commit `d4b1f041789dcd1d71907654e4025d613b4ab420`, path `toyokumono`
(OBN-Y18, v0.10, April 2026, "for Cloud Jam"). The MIT root licence is
unchanged; `LICENSE` is the upstream text, SHA-256
`62a5db2ff173434f94d055a9432f5655267e92f4db92a042b832884e8a48f6bb`.

This is the complete original action game: logo, animated goddess title,
instruction pages, player movement and wind, cloud summon/rain, wet grounds,
sprout/growth/decay/wither, flower scatter scoring, cloud elements, pause/
restart/quit menu, game over, and the original top-10 record/play-count/play-
time screens. It is not a template or a remake.

## Play

- Tilt moves the player continuously through the original LEFT/RIGHT/UP/DOWN
  `buttonPressed` path. No directional hold remapping is needed.
- PAT single-B summons one cloud (the original edge-triggered `buttonDown(B)`
  path); the original automatic summon when `endureFrames` expires is kept.
- PAT double-A pauses to CONTINUE/RESTART GAME/BACK TO TITLE. In menus B
  selects and A toggles sound, matching the original layout.
- On the instruction pages B advances, A leaves; left/right tilt pages.
- After game over, B retries and A quits (after the original one-second gate);
  the original eight-second automatic return to the title remains.
- PAT hold is the shared adapter exit gesture.

No core path requires a continuous A or B hold: A (pause/confirm) and B
(cloud/menu) are edge-triggered in the original source, and continuous input is
tilt only. No new unaudited API was needed.

## Port changes and limits

- Original `common.h`/`data.h` and `common.cpp`/`game.cpp`/`title.cpp`/
  `logo.cpp`/`menu.cpp`/`MyArduboy2.cpp` bodies are kept private under
  `ToyokumonoSandbox`. `port.h` hides and restores the Castle Boy `FPS`/
  `STATE_*` macros.
- The original `pgm_read_ptr` handler/drawer/module tables and
  `instructionTextList` are indexed directly with their typed pointers. Realtek
  `pgm_read_ptr`/`pgm_read_word` are only 16 bits and would truncate pointers.
- `strnlen_P` became `strnlen` and `memcpy_P` became `memcpy`; PROGMEM strings/
  tables are directly readable on this target.
- The original `MyArduboy2` 6x6 compact font and text metrics are preserved.
  Audio (`playTone`/`playScore`/channel init) is silent through local stubs; the
  original sound-score tables ship as data but produce no sound.
- Records (top-10 scores, play count, play frames) use the private RAM EEPROM
  (`ab.eeprom`), not AVR `EEPROM` and never Flash. Base 568, signature + record
  + byte-safe checksum, checked to stay inside the 1024-byte engine buffer. The
  checksum is byte-wise because the 32-bit target has no 16-bit aliasing
  guarantee. `sizeof(RECORD_T)` follows the target ABI (28 bytes here rather
  than the AVR's 26), and both read and write use that same size.
- The logo Morse shift is clamped non-negative (the original could shift by a
  negative count).
- Mutable state (mode, logo counter, title menu/dots, clouds, grounds, flowers,
  player, wind, score, elements, counters and LED) resets on app entry and
  unmount; the RAM EEPROM record survives visits and is re-read by the original
  `readRecord()`.
- 128x64 monochrome, nominal 60 Hz, frame-driven; no heap, blocking loops,
  compressed bitmaps, AVR asm, timers or SDK services.

## Verification

`tools/tests/test_arduboy30_toyokumono.py` compiles the real `app.h` adapter,
the real `Arduboy2Compat`, and the actual port sources on the host with
Realtek's 16-bit `pgm_read_ptr`/`pgm_read_word` shape and the Castle Boy
macros. It asserts those macros are restored and Toyokumono macros do not leak,
then covers the real title, tilt menu selection, game start, tilt movement,
one B-summoned cloud with element decrement, rain wetting the grounds, a legal
plant growth/scatter score change, the A pause menu and CONTINUE, game over,
B retry, A quit to title, unmount/reentry with the RAM record re-read, the PAT
exit gesture and non-empty framebuffers.

Two states are injected to reach original rules deterministically: a legal
wet/power/life-one plant (to exercise the growth/scatter-score branch) and
legal exhaustion (no elements, clouds or plants) to reach game over. All other
transitions are driven through the original handlers and input. Deterministic
17 ms fake frames and taps are used. Set
`CRABIT_ARDUBOY30_TOYOKUMONO_FRAMES=<dir>` for PBMs.

Host evidence only; no BW16 embedded build, Flash/FTL fit, device run or
hardware acceptance is claimed.
