# Hand-written verification rows: audio and input

### V-AU-001 Play a sound from an Audio Source
- **Map:** 7
- **Where:** editor, Inspector
- **Steps:**
  1. Select an object and press **Add Component**, choose **Audio Source**.
  2. In its section press **Beep** (a generated tone) or choose an audio file; tick **Play On Awake** and **Loop**.
  3. Press Play (Ctrl+P). Press **Stop** in the section, or stop the game.
- **See:** you hear a tone when the game starts, and it loops until stopped.
- **Partial:** no log line says which decoder was used.

### V-AU-002 Audio Mixer
- **Map:** 7
- **Where:** editor, View menu
- **Steps:**
  1. Open **View > Panels > Audio Mixer**.
  2. Move the **Master** volume and **Mute** a group (**SFX**, **Music**, **Voice**) while a sound loops in Play.
  3. Try **Duck music under voice**.
- **See:** the looping sound gets quieter or silent as you move the sliders; with ducking, music dips when voice plays.
- **Partial:** none known.
- **Link:** [audio.md](../../audio.md)

### V-AU-003 Spatial sound example
- **Map:** 7
- **Where:** editor, File > Open...
- **Steps:**
  1. Open `examples/spatial_audio/spatial_audio.okayscene` (the `examples` folder next to the launcher if your release ships it).
  2. Press Play and walk around the sources.
- **See:** the sound gets louder and pans left or right as you move. Play starts without a pause, and a second Play starts at once. There is no log line that shows sharing of the decoded clip.
- **Partial:** the release may not ship `examples` yet (PR #887).
- **Link:** [../../try_it.md](../../try_it.md)

### V-AU-004 Keyboard, mouse and gamepad in Play
- **Map:** 8
- **Where:** editor, **Help** menu
- **Steps:**
  1. Open **Help > Keyboard Shortcuts** and read the list of default keys.
  2. Press Play on a Third Person scene and move with W A S D and the mouse; plug in a gamepad and use its sticks.
- **See:** the character moves with keys, mouse look and a gamepad.
- **Partial:** gamepad support was only exercised through SDL on other machines; steps beyond the menu are unverified.
- **Grounded:** no

### V-AU-005 Touch controls on screen
- **Map:** 8, 12
- **Where:** editor, GameObject menu
- **Steps:**
  1. Open `examples/touch_controls/touch_controls.okayscene`, or add **GameObject > UI > Controls > Virtual Joystick** and **Touch Button**.
  2. Press Play and drag the joystick with the mouse.
- **See:** the on-screen joystick follows the drag; real touch only works on a touch screen.
- **Partial:** untested on a touch device.
