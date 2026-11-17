---
id: 7zr
row: | 7zr | First-person hands: alive motion, a grip per held item, named animations, no hand in the wall | Editor: File > Open `examples\fp_hand\fp_hands_motion.okayscene`, Play | New in this change; headless renders and tests only, nothing run on Windows or in a window |
---
## 7zr. First-person hands: they move like they belong to you

**What it is.** The arm you see in first person (see 7z) now moves. It breathes when you stand still, bobs and swings with
your steps, tips into a sprint pose, lowers when you crouch, kicks when you jump and land, tilts when you strafe, pulls back
when you walk into a wall, and holds a pistol, a rifle, a flashlight and a crate in four different ways. Everything eases
from one state to the next, so nothing pops. Pictures (headless software renders, not a window): `docs/images/fp_hands/states.png`
(idle, walk, sprint, crouch, air, land, strafe, wall), `grips.png` (empty, flashlight, crate, pistol, rifle, rifle aiming) and
`events.png` (swing, use, fire, reload, equip). Details: [fp_hand.md](fp_hand.md).

**How to try it (Windows release).**

1. Open the editor, **File > Open** `examples\fp_hand\fp_hands_motion.okayscene` (the `examples` folder next to the program), press Play.
2. Stand still and look at the hand at the lower right: it should rise and fall very slowly (breathing).
3. Hold **W**: the hand bobs in time with your steps and swings a little. Hold **Shift** as well: the hand tips in and down (the
   sprint pose; by default a sprint hides the hand, so select **Player > First Person Hands** and tick **Show while running** first).
   Let go: it eases back, with no jump.
4. Press **Ctrl** (crouch): the hand sits a little lower. Press **Space**: a small dip when you take off, a lift in the air, a bigger dip when you land.
5. Press **A** or **D** while walking: the hand tilts a little toward the side you move.
6. Walk straight at the grey wall: when you are about half a metre away the hand comes back toward you and down, and it
   never goes through the wall.
7. Press **E** at the **Pistol**, the **Rifle**, the **Flashlight** and the **Crate** in turn (and **G** or **E** to put it down). Each
   sits in the hand differently, and the hand dips as it takes the item. With the pistol or rifle: **left click** fires (a
   small recoil kick), hold the **right mouse button** to aim (the hands slide to the middle), **R** reloads (the hands drop and come back).
8. The small picture at the top right is a camera looking back at you: other players see the normal body, not these kicks.
9. Reduce motion: in the Settings screen (or Accessibility settings) turn **Reduce motion** on. The breathing, bob and tilt go
   away and the fire, swing and reload kicks get much smaller; the sprint, crouch, grip and wall poses stay (they show what
   you are doing, they do not shake).
10. For scripts (and the sandbox dig and place actions): `FirstPersonHands.Event("swing")` (also "dig", "hit", "use", "place",
    "fire", "reload", "equip", "unequip"), `FirstPersonHands.Aim(true)`. In C++: `Character::TriggerFpEvent("dig")`, `SetFpAim(true)`.

**Unverified.** Checked only by `ctest -R fp_motion` (the pure motion model for thousands of frames and the real player stepped
frame by frame) and headless software renders on Linux. Not tried in a window, on Windows, with a gamepad or in multiplayer; how
it feels (the amounts are first guesses) is unknown. The pose is not sent over the network: other players do not see these kicks.
