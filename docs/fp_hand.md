# The first-person arm (hand, forearm, presets)

What you see of your own body in first person, and what other players see. Code: `engine/include/okay/Components/FirstPersonHands.hpp`
(the settings and presets), `engine/src/Components/Character.cpp` (`PoseOwnerArm`, `PaintFpForearm`, the click swing),
`editor/FpHandsWindow.cpp` (Inspector section and the Fp Hands window with the live preview). Written from the existing
engine code; no outside code or assets.

## What was wrong (before, see `docs/images/fp_hand/before/`)

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

## The motion layer (breathing, bob, poses, grips, named animations)

Code: `engine/include/okay/Components/FpViewmodelMotion.hpp` (the model, no scene or input in it, so the tests can step it for
thousands of frames), hooked into `Character::StepFpMotion` and `Character::ApplyFpMotion` in `Character.cpp`. Written from this
engine's own player code and the general idea of critically damped springs; no outside code, constants or tests.

**What was there (before).** The arm pose followed the stride (a small bob), the click swing, the look lag and the raise and
lower for a sprint. There was no idle movement, no sprint pose, no crouch, jump or landing response, no strafe tilt, no aim, no
wall handling (only a near-plane guard), the same pose for every held item, and no named animations a script could play. The bob
ignored Reduce Motion.

**What it does now.** One rigid camera-space offset and rotation is laid on the whole viewmodel (arm, forearm, hand, and the held
item, so the item bobs with the hands). Each state has its own spring weight, so a change of state bends the curve and never steps it:

| Layer | What you see |
|---|---|
| Idle breathing | a slow rise and fall, fades out when you move |
| Walk / run bob | two dips per stride (every footfall), a side sway and an arm swing locked to the character's stride phase; bigger at a run, smaller crouched |
| Sprint pose | hand pulled in, down and tipped (visible when Show while running is on) |
| Crouch / prone | the hand sits lower and closer |
| Jump | a dip on take-off, a lift in the air, a dip on landing (follows the landing envelope) |
| Strafe tilt | a roll toward the side you move |
| Wall pull-back | one short ray from the lens; closer than 0.85 m lowers the hand, brings it back and tips it up; the near-plane guard still runs afterwards |
| Aim (pistol, rifle) | the hands slide to the middle |
| Grip | per held item kind: empty hand, tool (flashlight, sword, cup), one-handed, two-handed, pistol, rifle, taken from the item's `HoldableObject` style (`FpGripKindFor`) |

**Named animations.** `equip`, `unequip`, `use`, `swing`, `fire`, `reload`. Aliases: `dig`, `hit`, `attack` play `swing`; `place`,
`interact` play `use`; `shoot` plays `fire`; `draw`, `raise` play `equip`; `holster`, `lower` play `unequip`. Case does not matter.
Equip and unequip play by themselves when a held item appears or goes. From C++: `character->TriggerFpEvent("dig")`,
`character->SetFpAim(true)`; from a script: `FirstPersonHands.Event("dig")`, `FirstPersonHands.Aim(true)`. Events overlap (fire
while reloading) and starting the same one again keeps it playing. The sandbox dig and place actions can call these names.

**Keys (First Person Hands > "Pistol / rifle: click fires, right mouse aims, R reloads").** Off for a component added by hand,
on in the starter players and the motion example. With a pistol or rifle in hand the click plays `fire` instead of the arm
swing, the aim button (right mouse) aims, `R` reloads. Other items keep the click swing. Saved as a `fphands4` scene line (an
older scene has no such line and keeps the keys off).

**Settings that already existed and now scale it.** Bob amount (walk bob), Sway amount (breathing and strafe tilt), Punch (the
one-shot kicks). **Reduce motion** (Accessibility): breathing, bob, strafe tilt and the air lift go to zero and the kicks
(fire, swing, reload, landing dip) shrink to a third; the poses that tell you a state (sprint, crouch, grip, aim, wall) stay.

**Tests.** `ctest -R fp_motion`: `fp_motion` (the model: continuity through every state and event, scaling, Reduce Motion, 20000
random frames) and `fp_motion_char` (a real First Person player on a floor: the arm in the camera never jumps through walk,
sprint, jump, land and crouch, the item stays in the hand, the wall pull, Reduce Motion, nothing in third person).

**Not done.** No fingers or finger poses; the aim does not change the field of view (the viewmodel has its own Hand FOV, which
already existed); no separate left-hand animations (the second hand follows the first); the pose and events are not sent over the
network; the amounts are first guesses that need a play test on a real screen; slide has no pose of its own (it uses the crouch
pose); the wall check is one ray, so a thin pole beside the line of sight does not pull the hands back.
