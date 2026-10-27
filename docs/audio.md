# Audio

Sound in OkaySpace comes from **AudioSource** components, mixed by the engine into
stereo output at the device's rate. This page covers the source settings, the mixer
and its groups, music, the player's volume settings, and scripting.

## Clips

An AudioSource plays a **WAV, OGG or MP3** file. Set its file in the Inspector
(Browse or type a path relative to the project). Files are decoded once when Play
starts, resampled to the output rate, and shared between every source that uses
them. Files that can't be read are listed in the Console.

## AudioSource settings

| Setting | What it does |
| --- | --- |
| Volume | 0 to 1. |
| Pitch | Speed and pitch together: 2 is an octave up and twice as fast. |
| Pan | -1 left, 0 centre, 1 right. Adds to the 3D pan. |
| Group | The mixer group it plays through: SFX, Music, UI or Voice. |
| Loop / Play On Awake | Repeat the clip; start when the scene starts. |
| Low Pass | Cutoff in Hz; lower sounds more muffled (behind a wall, underwater). |
| 3D | Fade with distance from the listener (the camera) and pan to the side the sound is on. |
| Min / Max Distance | Full volume inside Min, silent past Max. |
| Rolloff | Linear, Inverse (like real sound) or Logarithmic (loud close up, long quiet tail). |

Sources can also play **one-shots** on top of their main clip (footsteps, gunshots):
each call gets its own voice, up to 16 at once per source. Fades (`FadeIn`,
`FadeOut`, `FadeTo`) change the level smoothly, sample by sample.

## The mixer

Every source plays through one **group**, under the **master** volume:

- **Music**, **SFX**, **UI** and **Voice** each have a volume (0 to 1) and a mute.
- **Ducking**: when it's on, the Music group dips while anything in the Voice group
  plays (level, attack and release are adjustable), so dialogue stays clear.

Open **Window > Panels > Audio Mixer** in the editor to set the levels, watch the
output meters while playing, and try ducking. Levels set there are for the editor
session; players set their own (below).

## Music

Add **Audio > Music Player** to an object for a playlist: list the tracks, then
choose Play On Start, Shuffle, Loop Playlist and the **crossfade** time between
tracks. It plays through the Music group.

## The player's volume settings

The **Pause Menu** component has **Volume** (master), **Music** and **Effects**
sliders (Effects covers the SFX and UI groups). Whatever the player sets is saved
with their prefs when the game closes and loaded the next time it starts, so their
settings stick between sessions. The game's default master volume comes from
Build Settings; the player's own setting wins once they've saved one.

If **Mute audio when unfocused** is on in Build Settings, the game goes quiet while its
window is in the background without changing the player's settings.

## Scripting

C#-style scripts and visual scripts share the `Audio` bindings:

```csharp
Audio.masterVolume = 0.8f;
Audio.SetGroupVolume("Music", 0.5f);
float fx = Audio.GetGroupVolume("SFX");
Audio.MuteGroup("Voice", true);
Audio.SaveSettings();   // keep them as the player's settings now
```

In a visual script these are the **Audio** nodes in the node search (for example
`Audio.SetGroupVolume(group, volume)`).

OkayScript has the same in function form:

```
set_volume(0.8)
set_group_volume("music", 0.5)
mute_group("voice", true)
set_ducking(true, 0.35)
save_audio_settings()

music_play(0)        # or music_play("Assets/Music/theme.ogg")
music_next()
music_stop(2)        # fade out over 2 seconds

play_sound(0.7)      # this object's clip as a one-shot at 70%
```

On an AudioSource (`GetComponent<AudioSource>()`): `pitch`, `pan`, `lowPass`, `isPlaying`,
`Play()`, `Stop()`, `Pause()`, `Resume()`, `PlayOneShot(volume)`,
`FadeIn(seconds)`, `FadeOut(seconds)`.
