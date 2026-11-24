---
id: 7z
row: | 7z | First-person arm: looks normal to you and to others; presets to compare | Editor: File > Examples or open `examples\fp_hand\fp_hand_test.okayscene`, Play; select Player > First Person Hands > Preset | New in this change; headless renders only, nothing run on Windows or a GPU |
---

## 7z. First-person arm: normal for you and for everyone else

**What it is.** Your own arm in first person (the player in the First Person starter). Before: no arm unless you held
something, then a floating skin block, and a left click did nothing. Now: a hand and forearm in your shirt colour, always in
view at the lower right, a left click swings it, and other players see the whole body. Before and after pictures:
a picture folder (pictures are no longer stored in the repository) and a picture folder (pictures are no longer stored in the repository) (headless renders: left your camera, middle a camera in
front of the player, right a camera at the side). Details: [fp_hand.md](../gameplay/fp_hand.md).

**How to try it (Windows release).**

1. Open the editor, open `examples\fp_hand\fp_hand_test.okayscene` (the `examples` folder next to the program) and press Play.
2. Look at the lower right of your view: you should see a hand with a forearm in a green sleeve coming up from the bottom edge. Click
   the left mouse button: the arm swings.
3. Walk up to a block (red cube, blue block, green box), press **E** to hold it. It sits in the hand. The green box is two-handed.
4. The small picture at the top right is a second camera looking back at you: you should see a whole person with two arms, the
   held block in the hand and the arm swinging when you click. No floating hand there.
5. Stop. Select **Player**, in the Inspector find **First Person Hands**, open **Preset** and pick Both arms (looks like Normal except for two-handed items, see fp_hand.md), Right hand
   only, Minimal (just the hand block) or **Old look**; press Play each time. **Use normal first-person arms** brings the normal look back (Ctrl+Z also undoes it).
   **Open Preview** shows both views live as you change a preset.

**Unverified.** Everything above is checked only by the tests (`ctest -R fp_hand`) and headless software renders on Linux.
The editor window code compiles but was not run; no Windows, GPU, or real multiplayer session was tried; how it feels in a real game is unknown.
