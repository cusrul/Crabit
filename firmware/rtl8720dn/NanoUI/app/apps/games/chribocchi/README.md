# Chri-Bocchi Cat BW16 port

Actual source/assets from [obono/ArduboyWorks, `chribocchi/`](https://github.com/obono/ArduboyWorks/tree/d4b1f041789dcd1d71907654e4025d613b4ab420/chribocchi),
commit `d4b1f041789dcd1d71907654e4025d613b4ab420`, version 0.04
(December 2017), OBN-Y03. Retrieved from the pinned archive described in the
shared worker brief; `LICENSE` is the upstream root MIT license,
Copyright (c) 2016–2026 OBONO, unchanged. `info.json` remains upstream metadata.
The original game logic, title/menu/record/credit screens, animated logo,
bitmaps, 6×6 font, two-minute timer, box bouncing/spawning and score table are
included rather than remade.

## Play

- Tilt left/right: move the cat.
- Tilt down: meow (`DOWN`).
- PAT single tap (A) or double tap (B): select a menu item, pause/resume, or
  return from record/credit screens. The original game uses A|B identically for
  these actions.
- Hold PAT at least 800 ms: shared adapter exit gesture.
- Start moves the cat to center, plays the two-second "Ready?" countdown, then
  throws gift boxes for two minutes. Bounce them on the cat; missing boxes
  reduces the live count. Score and timer stay on the top bar.

## Port changes and limits

- `.ino` and the three `.cpp` bodies are renamed `.inc.h` so Arduino stages them
  as headers without separately compiling them. `port.h` includes them inside
  `ChribocchiSandbox`, with `Logo`, `Title` and `Game` sub-namespaces; function
  tables are typed native pointers.
- `ChribocchiApp` uses the shared frame-mode `ArduboyCompatApp` /
  `Arduboy2Compat`; pacing, Canvas handover, tilt/button translation and exit
  are adapter-owned. No blocking startup, AVR timer/speaker/power or RGB
  hardware calls remain.
- `MyArduboy` derives from `Arduboy2Compat` and keeps the upstream compact font,
  text cursor, and clipped line/rect helpers. The compatibility layer's 5×7 text
  path is not substituted.
- All AVR `eeprom_*` pointer casts and busy waits are replaced by the shared
  per-engine 1024-byte RAM EEPROM. The upstream record address (`832`),
  signature (`"OBN\x03"`), Best-10 layout, play count/frames and checksum are
  retained. Only addresses 832–863 are written; nothing touches real
  EEPROM/Flash. Records survive app visits while the engine lives, not reboot.
- `strcpy_P` is replaced by a bounded local copy; no AVR program-memory API is
  required. `ALIGN_*` macros from the shared header are hidden only while the
  game's private alignment enum is compiled, then restored.
- Sound score arrays are retained as source assets, but audio is the shared
  silent stub and the SOUND toggle only changes its own ON/OFF state. RGB logo
  colour is a no-op. The logo's negative Morse shift at the end of its countdown
  is clamped to silence. The upstream `soundOver` 260 ms value that does not fit
  a `byte` is encoded as its real big-endian bytes `1, 4` (same correction the
  Hollow port documents).
- All mutable logo/title/game state resets on app entry; the RAM record is
  re-read. Transitions stay frame-driven without delays or busy waits.

## Host verification

`tools/tests/test_arduboy30_chribocchi.py` compiles the real port and shared
compatibility layer with host-only Arduino/NanoUI/AmebaStore fakes, then checks:
the animated logo and title render, menu navigation and SOUND toggle, record
screen rendering, START, cat boundary clamping, DOWN meow, the real ready/
throw/bounce path producing a score and second box, pause/resume with the
upstream ignore counter, RAM record signature/sorted Best-10/checksum reread,
bounded EEPROM bytes, complete transient-state reset on re-entry, non-empty
actual framebuffers (optional PBM capture via `CRABIT_ARDUBOY_FRAMES`), and the
shared PAT-hold exit gesture.

No embedded build, SDK, Flash/FTL measurement or watch/hardware acceptance is
claimed by this port. Menu/profile and split-image integration are owned by the
main AI.
