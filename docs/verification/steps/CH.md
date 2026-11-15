# Hand-written verification rows: characters, cameras and animation

### V-CH-001 First person player
- **Map:** 4.1, 4.2
- **Where:** editor
- **Steps:**
  1. Choose **File > New Project...** and the **First Person** template (or **GameObject > Player > First Person**).
  2. Press Play (Ctrl+P). Walk with W A S D and look with the mouse.
  3. Press Ctrl to crouch and Z to go prone (crouch toggles with a tap by default; prone key is Z).
- **See:** you walk and look in first person; the camera lowers when you crouch and lowers more when prone; running is off while crouched or prone.
- **Partial:** map 4.2: prone on first and third person is listed as "verify"; hands-on-ledge IK is not wired.
- **Link:** [../../try_it.md](../../try_it.md)

### V-CH-002 Third person player
- **Map:** 4.1, 4.2
- **Where:** editor
- **Steps:**
  1. Choose the **Third Person** template (or **GameObject > Player > Third Person**).
  2. Press Play. Move with W A S D, look with the mouse, jump with Space.
  3. Press Z for prone and Ctrl for crouch.
- **See:** the character walks, the camera orbits behind it, and the crouch and prone stances change its height.
- **Partial:** none known beyond map 4.2.

### V-CH-003 Reduce Motion stops the head bob
- **Map:** 4.1, 8, 12
- **Where:** editor, in a First Person scene
- **Steps:**
  1. In a First Person scene press Play, walk, and watch the camera bob. Press Stop.
  2. Choose **GameObject > UI > Prebuilt Screens > Settings** to add a Settings panel.
  3. Press Play, click **Reduce motion** on the panel, click **Done**, and walk again.
- **See:** with Reduce motion on the bob and sway are gone and there is no dip on landing; off, they come back.
- **Partial:** the editor itself has no accessibility window; the switch lives on the game's Settings screen.
- **Link:** [../../try_it.md](../../try_it.md)

### V-CH-004 Turn interpolation
- **Map:** 4.1, 6
- **Where:** editor
- **Steps:**
  1. In a **Third Person** scene choose **Edit > Project Settings...**, open the Physics tab, set **Steps per second** to 10 and tick **Smooth the motion**.
  2. Press Play and walk in a tight circle.
  3. Untick **Smooth the motion** and compare.
- **See:** with smoothing the motion is even; without it, at 10 steps a second, it visibly steps.
- **Partial:** the "before the fix" look needs an older release to compare.
- **Link:** [../../try_it.md](../../try_it.md)

### V-CH-005 Character from a model file
- **Map:** 3, 4
- **Where:** editor, Project panel
- **Steps:**
  1. Import a model with **File > Import Model...**.
  2. In the **Project** panel right-click the model and choose **Make Playable Character**.
  3. Press Play.
- **See:** the model is made into a character you can walk.
- **Partial:** needs a rigged model file of your own; none ships with the release. Steps beyond the menu names are unverified.
- **Grounded:** no

### V-CH-006 Animation window and Animator graph
- **Map:** 5
- **Where:** editor, View menu
- **Steps:**
  1. Open **View > Editors & Tools > Animation** and **Animator (state graph)**.
  2. Select a character with an animation and press the Play button in the Animation window (tooltip "Play (Space)").
- **See:** the clip plays on the selected object in the Scene view.
- **Partial:** none known; you need an object with clips.

### V-CH-007 Humanoid with foot IK
- **Map:** 5, 4
- **Where:** editor, GameObject menu
- **Steps:**
  1. Open **GameObject > Player** and choose **Humanoid (foot IK)**.
  2. Press Play and walk over stairs (**GameObject > 3D Object > Stairs**) or a slope.
- **See:** the feet are placed on the steps instead of floating or sinking.
- **Partial:** "feet slide on slopes" is a listed symptom in map 5; report if seen.
