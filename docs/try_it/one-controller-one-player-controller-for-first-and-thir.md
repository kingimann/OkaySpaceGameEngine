---
id: one-controller
row: | one-controller | One Player Controller for first and third person; no Humanoid or separate First / Third Person entries | Editor: GameObject > Player, Add Component > Gameplay, File > New Project (First / Third Person) | Tests run on Linux only; not run in a real editor or on Windows |
---
## one-controller. One Player Controller for first and third person (no Humanoid)

**What it is.** A walking first- or third-person character is made with one thing, the **Player Controller**. Its **View**
setting is First Person, Third Person or Both. The separate First Person, Third Person and Humanoid entries are gone from the
menus and template lists. Third Person Shooter, Top Down, Vehicle, 2.5D, Click To Move and Free Roam stay (they are other
kinds of game). Older scenes still open.

**Steps.**
1. Open the editor. Open **GameObject > Player**. You see **Player Controller (first / third person)** as the first entry, then
   Third Person Shooter, Top Down and the others. There is no First Person, Third Person or Humanoid entry.
2. Click **Player Controller (first / third person)**. A player and a camera appear. In the Inspector the **Player Controller**
   panel has **View** (First Person only / Third Person only / Both). Press Play: you start behind the player; press **V** to
   go to the eyes. Stop, set **View** to First Person only, press Play: eyes only, V does nothing.
3. Select any object, **Add Component > Gameplay**. There is one **Player Controller (first / third person)** entry, and no
   separate First Person or Third Person controller. (Foot IK: Add Component > Foot IK on the player; it wires itself.)
4. **File > New Project...**: the template list has **First / Third Person** and no separate First Person, Third Person or
   Humanoid template. The launcher's quick list shows the same one card.
5. Old scenes: open a scene saved before this change that has a Humanoid player or a First Person or Third Person player
   (for example any older project of yours). It opens, the player has a **Player Controller** with the same speeds and
   settings, and Foot IK and Root Motion are still on it.
6. Old launchers or scripts that pass `--template "First Person"` or `"Third Person"` to the editor still get a scene.

**What you should see.** One controller entry in both menus, one New Project template, and old scenes loading with their
settings. The Console says "Added Player Controller (View: Both ...)" when you use the menu entry.

**Status.** Unverified in a real editor and on Windows. Tests run on Linux: `one_controller` (an old-format Humanoid scene and
an old First plus Third scene load as one Player Controller and keep their settings), `templates_work` (one template, old titles
still build, Humanoid gone), `scene_cameras`, `player_controller`, `view_upgrade`.
