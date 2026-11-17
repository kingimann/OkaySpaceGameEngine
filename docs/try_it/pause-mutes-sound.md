---
id: pause-mutes-sound
---
## Pausing the game pauses game sounds (bug fix, branch `claude2/pause-mutes-sound`, #888 finding 64)

Before: with the Pause Menu open, gunshots, voice lines and engine loops kept playing, and a short sound played to its end
during the pause. Now the SFX and Voice groups stop where they are while the game is paused, and carry on from the same
spot when it resumes. Music and UI keep playing (menu music and button clicks).

1. Open `Tools\OkayEngine.exe`, then **File > Open...** `examples\spatial_audio\spatial_audio.okayscene`.
2. Add a pause menu: **GameObject > UI > Prebuilt Screens > Pause Menu**.
3. Press **Play**. You hear the speakers' tone.
4. Press **Esc**. The menu opens and every speaker goes silent at once.
5. Press **Esc** again (or click Resume). The tones come back, continuing where they stopped.
6. Stop Play. Select **Near Speaker** and set its Audio Source **Group** to **Music**. Play and press Esc: that speaker
   keeps playing over the menu, the others stop.

Also: `Time.timeScale = 0` from a script pauses game sounds the same way. Slow motion (a scale above 0) and a hit stop
do not. A game can choose per group in code with `AudioMixer::pausesWithGame` (see [audio.md](audio.md#the-mixer)).

**Status.** Mixer tested headless (`test_pause_sound`, run on Linux). The editor and player were not built or run here,
so the Windows steps above are unverified. The stop is instant, with no short fade, so a loud sound may click as it
stops.
