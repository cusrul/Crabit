# Arduboy 3D

Pinned upstream source and attribution are recorded in `LICENSE` and
`../../../THIRD_PARTY_NOTICES.md` (repository root). The port runs on the shared
`ArduboyCompatApp`/`Arduboy2Compat` layer; upstream in-tree `.cpp` files are
included by `app.h` inside one namespace, so do not add new `.cpp` files under
`src/`.

## Controls

- Tilt supplies UP/DOWN/LEFT/RIGHT.
- B (watch single tap) fires. It is the primary in-game action.
- A (watch double tap) is the original strafe modifier, which upstream is held
  together with left/right. The PAT adapter only supplies taps: A arrives as
  the same three-frame pulse the engine already received before this policy
  change, so strafe is a short nudge rather than a sustained sidestep. This
  limitation predates the action policy and is not worked around here.
- Menus advance with A or B; UP/DOWN changes the selected item.
- The shared wrapper's PAT hold exits.

No held A or B gesture is promised by this port.
