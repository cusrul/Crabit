# Windows-only watch UI review

Run `python tools/build_watch_ui_review.py` at the repository root, then open
`build/watch-ui-review/watch-ui-motion-review.exe`. Existing host g++ and Pillow are
required; the script does not install SDKs, system fonts or libraries.

This is a **design candidate**, not the current firmware UI or hardware evidence.
It uses the actual shared NanoUI Widget/RenderObject/Canvas renderer at128x64.
The existing Windows review viewer shows native pixels and4x nearest-neighbor
enlargement. The dropdown/previous/next buttons choose pre-rendered simulated
states; they are not buttons controlling a live watch. No radio, serial, BLE,
power/flash hardware or protected product path is accessed.

## Candidates

- Settings: mechanically staged from the real STM32 SettingsApp and its controls,
  with host-only service fakes, Chinese label replacements and8/10px font choices.
  Header/list/18px row/controls remain the real page's composition. The picker
  preview measures UTF-8 glyph width instead of counting bytes, and centers text
  vertically for each size. Action-button labels use the comparison size.
- Four new face compositions: information, minimalist digits, weather and analog.
- Two daily-forecast compositions at8/10/12px, plus stale and empty states.

The Settings comparison frames are layout references under the **same Fusion font
provider**, not byte-identical screenshots of the current firmware font assets.
Time/date/battery/forecast values are fixed sample data, not live weather/current
temperature. Candidates have no persistence or production event integration.
The three-day candidate uses simulated18–26/17–25/16–22 degree ranges; these are
layout examples, not device measurements or a fetched forecast.

## Fonts and reproducibility

Fusion Pixel Font monospaced zh-Hans BDF release2026.10.16:
https://github.com/TakWolf/fusion-pixel-font/releases/tag/2026.10.16

The script downloads archives into ignored `build/ui-review-fonts/` and checks
pinned SHA256 values before use. The extracted OFL.txt and upstream LICENSES stay
with the local font files. Full fonts are not added to Git or product firmware.
Only needed character bitmaps are generated in the review output using the
existing NanoUI BDF parser, with center alignment.8/10/12px require native glyphs;
missing characters stop the build, with no silent TTF fallback.24px digits use
the12px native digits enlarged exactly2x, with no grayscale interpolation.

## Selected8px direction and animation candidates

The user selected native8px for regular App text. Keep large time/primary numeric
information larger. Product App typography and fonts now follow that selection;
the comparison renderer below still contains older10/12px alternatives.
Additional tool-page typography candidates keep the existing split-panel/menu
row proportions (static folder artwork substitute) and use no device callbacks.
They are not byte-identical copies of product pages or evidence of enabled features.

The updated executable is `build/watch-ui-review/watch-ui-motion-review.exe`.
Its first two entries loop actual NanoUI-rendered80-frame sequences at70ms/frame:

- Dot-matrix time: native12px glyph masks on a3px grid,2px dots; common strokes
  stay still, changed dots travel to nearby destinations along a shallow ripple.
- Minimal time: only the time, bespoke5x7 digit masks enlarged3x, with changed
  digits rolling through a tightly clipped slot. Not a grayscale font crossfade.

Simulated12:59→13:00 only, not a live RTC. Each endpoint holds20frames, transitions
use20frames, then reverse for comparison. The viewer plays pre-rendered frames;
this proves rendered states, not MCU frame time, RAM, power or target integration.
The primitive uses bounded paint scratch arrays and no paint-time allocation;
future firmware work still needs task-stack and redraw-cost checks.
`motion-0.gif`, `motion-1.gif` and `motion-strip.png` provide offline review too.

`font-coverage.json` records the selected sources and subset. `manifest.tsv`
contains all selectable frames, and `contact-sheet.png` allows visual review
without running the Windows executable. Rebuilding after the user selects a
design does not by itself integrate it into firmware: product assets, settings,
size budgets, embedded builds and hardware acceptance are separate steps.

## Interactive product clock/settings review

Run `python tools/build_watch_live_review.py`, then open
`build/watch-live-review/crabit-watch-review.exe`. This compiles the actual STM32
ClockApp and SettingsApp with shared NanoUI and the product's generated fonts.
It does not stage/rewrite these two page sources. Windows-only service fakes
supply fixed date, advancing time, battery and local preference-file storage;
BLE is unavailable. No device, radio SDK, Flash driver or power path is linked.

Click Open Menu, enter System Settings then Settings using the real product menu
callbacks. Use Up/Down to reach the watch-face picker, Enter to edit,
Left/Right to choose Classic/Liquid/Minimal, Enter to leave editing, then Apply
and Escape to return. Enter on a time-only face opens the real product launcher.
Other hardware App bodies are unavailable-page fakes, never linked board services.
Next Minute advances the
fake RTC by60seconds to inspect the420ms digit transition without waiting.

Desktop preferences are stored beside the executable in `ui-preferences.txt`.
`--self-test` uses a separate self-test preference file and drives real page
events for draft cancellation, save/reload, switching/return, idle redraw and
minute transitions. It also walks the actual menu entry path20times and checks
pooled UI bytes return to baseline. Tests save product-*.pgm snapshots in the working directory.
This proves desktop behavior, not the watch Flash driver or animation frame
budget; only user-operated watch validation establishes those.

Product persistence reuses the old SystemConfig's six tail-padding bytes with
a five-byte marker; total48bytes, magic, earlier field offsets, Flash page/address
and save driver are unchanged. Old padding without the marker selects Classic.
Settings apply writes once; animation/RTC updates never save preferences.
The shared ModernMenu supports an optional title size (default12 unchanged).
Both product launchers now use the original12px titles; wide system/tool folder
lists also use12px, while narrow split-art lists and regular App data stay8px.
Framework Text defaults are
unchanged, so unrelated NanoUI users are not forced into the product typography.

### Settings-entry regression guard

The live preview now uses `NANOUI_DYNAMIC_STORAGE=0` and the exact12KiB allocator
class staged from nanoui_port.cpp, matching the STM32 storage policy. Earlier
malloc/profile1 tests missed a pool-exhaustion regression when opening Settings.
No production allocator/profile/capacity change is part of the fix: six label
slots use ConstrainedBox directly instead of the redundant undecorated Container
render pipeline, preserving their60x18geometry and Flutter-style composition.

`python tools/build_watch_live_review.py --verify-regression` additionally builds
a negative control that reintroduces only those wrappers in a generated header.
It must fail with a pool-exhaustion diagnostic while the real current page passes.
The mutation does not modify product sources or require old Git history.
The test-only allocator adapter exits visibly on allocation failure rather than
silently following a null pointer. This is still host evidence, not watch acceptance.

### Classic status face

The product Classic face now composes a thin94x28seven-segment time primitive
with8px date, battery percentage, phone-cached forecast range and BMP280 device
temperature. It replaces the old24px general-purpose font digits and debug
RAM/uptime labels. Top-level/wide-folder menu typography stays12px.

ClockApp reads the coherent UI sensor snapshot, never I2C or a driver. Battery
requires valid0..100percent data no older than5minutes; temperature requires
valid finite-40..85C data no older than30seconds. Invalid data displays--, not
100percent. Device temperature is labelled separately from the outdoor forecast.
Current hardware/snapshot/BLE forecast contract has no humidity field, so no
humidity value is invented. Today is selected from cached forecasts when present;
non-today fallback is labelled cache. No network request or radio activation.

The live window uses simulated76percent/24.6C/sunny18..26C data, explicitly
labelled as simulated. Its tests inspect actual ClockApp render bindings and
layout for ready, unavailable, stale/out-of-range, charging, negative temperature
and matching-today-in-second-cache-slot cases. `--classic` opens Classic for
review without overwriting the saved desktop face preference.
