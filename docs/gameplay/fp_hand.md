# The first-person arm (hand, forearm, presets)

> **What this is:** What you see of your own body in first person, and what other players see.
>
> **Status:** Reference page for this feature. Kept as written; not extended (rule 5).
>
> **Last checked:** 2026-11-20 (moved into `gameplay/`, paths and links checked; the text itself was not re-checked against the code).

What you see of your own body in first person, and what other players see. Code: `engine/include/okay/Components/FirstPersonHands.hpp`
(the settings and presets), `engine/src/Components/Character.cpp` (`PoseOwnerArm`, `PaintFpForearm`, the click swing),
`editor/FpHandsWindow.cpp` (Inspector section and the Fp Hands window with the live preview). Written from the existing
engine code; no outside code or assets.

## What was wrong (before, see a picture folder (pictures are no longer stored in the repository))

Pictures are headless software renders (`tests/fp_hand_shot.cpp`), left to right: your own camera, a camera in front of the
player, a camera at the side.

- With nothing in your hands you saw **no arm at all**. The arm only appeared while holding an item.
- When you did hold something, the "hand" was a big **floating skin-coloured block** at the lower right, with no forearm and no
  sleeve, so it looked like a lump of flesh, not an arm.
- A **left click did nothing** in the starter player: the swing only existed on the older single `FirstPersonHand`.
- Other players already saw a complete body with two arms, and nothing of the viewmodel leaked to them (checked in the tests).
- There was no way to compare looks.

## What it is now

A new First Person Hands component is the **Normal** preset: the arm is always in view at the lower right; a forearm in the
character's shirt colour runs from the hand off the bottom edge of the screen (so it is connected to you); the hand is
smaller; left click swings the real arm (so others see the swing too); a two-handed item uses both hands.

| Preset | What it does |
|---|---|
| Normal (default) | Arm always in view, forearm in the shirt colour, click swings, both hands for two-handed items |
| Both arms | Normal, but asks for both hands on a held item. A one-handed item still shows one hand and empty hands show one arm, so it looks like Normal for those; **a real two-arm stance is not built yet** |
| Right hand only | Normal, but only the right arm ever shows |
| Minimal (hand only) | Just a small hand block, no forearm |
| Old look | The behaviour before presets, for comparing: floating block, only while holding, no click swing |

A preset sets every player-view number (it does not touch the Others View numbers). The numbers stay editable; the preset is
only the name of the last one applied. **Use normal first-person arms** applies Normal. Ctrl+Z undoes any of them.

Old scenes: a saved scene without the new `fphands3` line loads as the Old look, so nothing changes until you pick a preset.

## Try it

See `docs/try_it.md`, 7z.
