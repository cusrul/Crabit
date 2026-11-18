# LongCat BW16 port

Actual source/assets from [jaguile6/arduboy-longcat](https://github.com/jaguile6/arduboy-longcat),
commit `5894110c825f1c351c3f1055779b9b109f917cd3`, `longcat/` (the pinned
repository also contains a level editor and `.hex` builds, which are not part
of this port). The upstream root `LICENSE` is the MIT text unchanged and names
only "Copyright (c) 2016-2018 OBONO"; SHA-256
`64571d2da850a26b4e92b0082ab2c571fe48468ed9c07109c4bb7f5d30081b8a`
(LF-normalised; raw checkout with CRLF is
`00d644f67d0604220193fd88f17d1e90c1ba172c7c0aabea226e7a40ae90b984`).
Upstream credits the game to **jaguile6** and **Dreamer2345**; both are
retained here as attribution (`NOTICE`). This port does not claim a broader
MIT scope than the upstream repository.

This is the complete original game: splash, main/campaign/random/options/
credits menus, seed entry, 100 packed levels, deterministic random generation,
cat-body simulation, stuck and win overlays, end-screen animation and the
`SaveUtil` campaign progress save. It is not a template or a remake.

## Play

- Tilt is the d-pad. The original rule applies: one direction command slides
  the cat until the next cell is blocked, laying its body along the way.
- PAT single-A confirms/restarts; PAT double-B opens the current menu (as in
  the original `pressed(B_BUTTON)` path). Splash A enters the main menu.
- Select Level uses continuous UP/DOWN in the original; the upstream
  `delayShort(250)` repeat gate is now 15 frames.
- Campaign -> Continue resumes the saved stage; Start resets it to stage 0.
- PAT hold is the shared adapter exit gesture.
- Silent build: in-code tones and the options switch remain, but produce no
  audio through the compatibility stubs.

## Port changes and limits

- The upstream `src/` tree is kept with its directory layout. `.cpp` bodies are
  renamed `.inc.h` (so Arduino staging does not compile them separately) and
  all are included privately under `LongCatSandbox` by `port.h`.
- Local shims restore the static-style APIs the source uses: `Sprites::draw*`,
  `ArduboyTones::tone` and `EEPROM.get/put`. `Arduboy2` is the shared
  compatibility engine. No `drawCompressed`, FX, timers or SDK services.
- `SaveUtil` now reads/writes the engine's private **RAM** EEPROM at address
  100 (4-byte `Save`), never AVR EEPROM/Flash. The upstream `hash()` was
  missing `return runner;`, so no save could ever verify; that audited defect
  is fixed, making real `lastStage` resume work.
- The two `arduboy.delayShort(250)` scroll waits became a 15-frame repeat
  counter. `LevelRenderer::renderWin`'s blocking `display()+delayShort(2000)`
  became a 120-frame win hold in `GameStateSimulate`, which also checks the
  win before the blocked move can return to GamePlay.
- `arduboy.fillScreen()` became `fillScreen(WHITE)` (the port engine has no
  default argument). `pgm_read_byte_near` became `pgm_read_byte`; `byte` and
  `constrain` come from the Arduino/compat headers.
- A PAT tap is held for several adapter frames, so `Game::justPressed()` is a
  one-frame edge over `buttonsState()` instead of the stretched engine pulse.
  This keeps one tap to one menu action / restart, as on the AVR build.
- Audited deterministic-reset and initialization fixes: `RandKey::fromCatKey`
  accumulator, `GameStateSelectSeed` key/selection, `Map`/`Hero`/`GameContext`
  defaults, and `Game::resetState()` for app entry/unmount. The RAM save is
  deliberately kept across visits and re-read by the original `setup()`.
- 128x64 monochrome, 60 Hz frame-driven; no blocking waits, heap, AVR asm or
  compressed bitmaps. The original random-level generator is the bounded
  8x8 loop from upstream.

## Verification

`tools/tests/test_arduboy30_longcat.py` compiles the real `app.h` adapter, the
real `Arduboy2Compat` and the actual `src/` tree on the host with Realtek's
16-bit `pgm_read_ptr`/`pgm_read_word` shape and preceding Castle macros. It
checks macro restoration and that `CatKeyLength` does not leak, then covers:
splash, one-tap A to the main menu, tilt menu navigation to Credits, Campaign
Continue, real tilt cat movement and body placement, the real level-0 win by
five original direction commands (thumbs-up overlay), the two-second frame
hold, stage increment and RAM save verification, reentry restoring the save
while clearing transients, real blocked movement, the end-screen transition,
the PAT exit gesture, and non-empty framebuffers/PBMs.

One state is injected to reach the end screen deterministically: a legal
almost-complete board (one empty cell) in Fixed mode, noted in the fixture as
not a full-input completion. All other transitions use the original handlers.

Deterministic 17 ms fake frames are used. Set
`CRABIT_ARDUBOY30_LONGCAT_FRAMES=<dir>` for PBMs.

Host evidence only; no BW16 embedded build, Flash/FTL fit, device run or
hardware acceptance is claimed.
