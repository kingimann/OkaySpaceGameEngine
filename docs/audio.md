# Audio

Sound in OkaySpace comes from **AudioSource** components, mixed by the engine into
stereo output at the device's rate. This page covers the source settings, the mixer
and its groups, music, the player's volume settings, and scripting.

## Clips

An AudioSource plays a **WAV, OGG, MP3 or FLAC** file. Set its file in the Inspector
(Browse or type a path relative to the project). Files are decoded once when Play
starts, resampled to the output rate, and shared between every source that uses
them. Files that can't be read are listed in the Console. FLAC is lossless: the
samples are the ones stored in the file. The decoder is our own, written from the
public FLAC format description.

Clips keep their **channels**: a mono file stays mono and a stereo file stays stereo
(files with more than two channels are averaged to mono). Nothing is downmixed
unless you ask: in code `AudioClip::Load(path, &error, true)` (or
`AudioClip::Downmixed()`) gives the mono version. The import setting
**Mix down to one channel** does the same thing for that file.

### Import settings

Select an audio file in the Project panel. Two settings are saved next to it
and used the next time the file is loaded (Play, or Load on an Audio Source):

| Setting | What it does |
| --- | --- |
| Loading | **Load the whole file**, or **Read it while it plays** (a small fixed amount of memory however long the file is). An Audio Source or Music Player whose own Stream box is ticked also reads while it plays, even when this says to load the whole file. |
| Mix down to one channel | Left and right become a single channel. Off keeps a stereo file stereo. |

A one-shot that needs the samples in memory still loads the whole file. It still
mixes down to one channel when that setting is on.

There is no quality setting. The file is not re-encoded, so a quality number
would not change the sound. A number left in an older settings file is kept and
ignored.

### Stereo placement

| Source | What `Pan` does |
| --- | --- |
| Mono, 2D | Places the sound: -1 left, 0 centre, 1 right. The centre leaves the clip unchanged on both sides. |
| Stereo, 2D | A **balance**: the left and right channels stay as recorded. Pan right turns the left channel down (1 mutes it), pan left turns the right channel down; the centre leaves both alone. A left-only clip stays in the left speaker. |
| Any, **3D** | A 3D source is a point in space, so a stereo clip is first averaged to mono, then placed by its distance and direction from the listener (plus `Pan`). |

Stereo one-shots and the Music Player follow the same rules; the low-pass filters each
channel on its own.

## Streaming long sounds

Tick **Stream** on an AudioSource (or leave **Stream** on in a Music Player, where it
is on by default) and the file is not loaded whole. It is opened when Play starts
and decoded in small pieces while it plays, so a ten minute track uses the same
small, fixed amount of memory as a ten second one: about 300 KB for a stereo
track, always under 2 MB (an MP3 also keeps an 8 byte offset for each 32 frames to
seek, about 0.6 KB per minute). Loop, Pitch, Pan, Low Pass, fades, volume and the 3D
settings all work on a streamed source. Use it for music and long ambience; leave
short effects off, since a stream is read from the file as it plays and a
one-shot of the source's own clip (`PlayOneShot()`) needs the clip in memory.

How it works, for the curious: each streaming source owns an `AudioStream`
(WAV is read directly; MP3 goes through `Mp3Stream`, our own decoder one frame at a
time; OGG through `VorbisStream`, our own Vorbis decoder reading Ogg pages on demand;
FLAC through `FlacStream`, our own decoder one frame at a time). Decoded audio waits in a
ring of 32,768 frames. The mixer's buffer fill starts by topping the ring up (the
decode step), so decoding always runs ahead of what is played and on the same
thread that mixes, with no locks. If the file cannot be read fast enough the ring
runs dry: the source plays **silence** for the rest of that buffer and carries on from
the same place, nothing is skipped or repeated, and nothing crashes (the stream
counts these as `Underruns()`). Damaged and truncated files play what can be read
and then end.

- **Seek** (C++): `AudioSource::Seek(seconds)` jumps to a time in a clip or a stream (clamped to
  its length); `Time()` and `Duration()` read the position and length. (Scripts do not
  have a seek call yet.) MP3 seeks are
  exact: the file is indexed when it opens and playback restarts a few frames earlier so
  the decoder is warm. Starting a source with Play always begins at 0.
- **Loop**: the end continues straight into the start with no gap (the ring keeps
  filling across the join), so a loop whose audio is itself seamless is seamless.
- **Same audio**: streamed and whole-file decoding give identical samples, so Stream
  only changes memory and when the file is read.
- **Limits**: files over 2 GB are refused; Stream is read at Play (and with Load in
  the Inspector), so a path changed while playing takes effect next Play.

In code: `AudioStream::OpenFile(path)` or `OpenMemory(bytes)`, then `Read`, `Seek`,
`loop`; `AudioSource::OpenStream(path)` / `UseStream(stream)` to attach one;
`Mp3Stream` for MP3 on its own.

## AudioSource settings

| Setting | What it does |
| --- | --- |
| Volume | 0 to 1. |
| Pitch | Speed and pitch together: 2 is an octave up and twice as fast. |
| Pan | -1 left, 0 centre, 1 right. Adds to the 3D pan. For stereo clips it is a balance (see below). |
| Group | The mixer group it plays through: SFX, Music, UI or Voice. |
| Loop / Play On Awake | Repeat the clip; start when the scene starts. |
| Stream | Decode the file in small pieces while it plays instead of loading it whole (long music). |
| Low Pass | Cutoff in Hz; lower sounds more muffled (underwater, a radio). All the way open leaves the tone alone. |
| 3D | Fade with distance from the listener (the camera) and pan to the side the sound is on. The effects below apply only when this is on. |
| Min / Max Distance | Full volume inside Min, silent past Max. |
| Rolloff | Linear, Inverse (like real sound) or Logarithmic (loud close up, long quiet tail). |
| Pitch follows speed | Off by default. On, the pitch rises when this sound or the listener is moving closer and falls when they move apart. Speed amount 1 matches real speed, 0 is no shift, higher exaggerates it. A sudden jump (faster than most of the speed of sound) is ignored, so a scene change does not shriek. |
| Quieter behind walls | Off by default. On, a solid wall between this and the listener lowers the volume (Loudness behind a wall) and muffles it (Muffle behind a wall, in Hz). Triggers are not walls. It eases in over a fraction of a second so it does not click. |
| Muffle with distance | Off by default. On, the sound stays bright up close and gets duller toward Max Distance, along a curve that stays brighter than a straight line through the middle. Muffle at the far edge is the cutoff there, in Hz. |

Every one of those is off unless you turn it on, and the open low-pass is unchanged, so a scene you already have sounds the same until you ask for the new behaviour.

Streaming and stereo are unchanged by this. A stereo 2D clip is still a balance (the centre leaves both channels alone) and is not sent to a reverb zone. A 3D source, streamed or not, is still averaged to one point in space first; pitch-follows-speed, walls, distance muffling and reverb then apply to that point. A stream still decodes ahead of the mix on the same thread.

Sources can also play **one-shots** on top of their main clip (footsteps, gunshots):
each call gets its own voice, up to 16 at once per source. Fades (`FadeIn`,
`FadeOut`, `FadeTo`) change the level smoothly, sample by sample.

## The mixer

Every source plays through one **group**, under the **master** volume:

- **Music**, **SFX**, **UI** and **Voice** each have a volume (0 to 1) and a mute.
- **Ducking**: when it's on, the Music group dips while anything in the Voice group
  plays (level, attack and release are adjustable), so dialogue stays clear.

Open **View > Panels > Audio Mixer** in the editor to set the levels, watch the
output meters while playing, and try ducking. Levels set there are for the editor
session; players set their own (below).

## Reverb zones

A **Reverb** volume (GameObject > Volume, or Add Component > Volume: Reverb) puts an echo on **3D** sounds while the listener is inside it. 2D sounds, including music and UI, stay dry. There is no echo at all until a Reverb volume exists, so an old scene stays dry.

The echo is a Schroeder reverberator: eight comb filters in parallel, each with a little damping in the feedback, then four all-pass filters in series. The right channel uses a slightly longer set of delays so the tail spreads in stereo. It is our own code, written from that public description.

| Echo | What you hear |
| --- | --- |
| Room | Small and short. |
| Hall | Larger, longer. |
| Cave | A long ring, the wettest of the four. |
| Outdoor | A little air, almost dry. |

**Strength** scales that echo (0 is dry, 1 is the preset). **Edge blend** is how far in from the surface the echo takes to reach full strength: 0 is full the moment you cross the edge, 1 only reaches full at the middle. Several overlapping zones blend by that weight (the room character is an average, the strength adds up and stops at full). Walking out fades the echo off; it is not added once you are outside.

The listener is the main camera, the same one 3D panning uses. `examples/spatial_audio/spatial_audio.okayscene` is a walk-through: a bright speaker, a dulled far speaker, a speaker behind a wall, a flyby whose pitch rises as it approaches, and Room, Hall, Cave and Outdoor zones further down the floor. Move the Main Camera into a zone and press Play. The tone is `examples/spatial_audio/tone.wav` (a buzzy loop, so the muffling is easy to hear).

## Music

Add **Audio > Music Player** to an object for a playlist: list the tracks, then
choose Play On Start, Shuffle, Loop Playlist, **Stream** and the **crossfade** time
between tracks. It plays through the Music group. Tracks keep their stereo, and with
Stream on (the default) each track is read from its file as it plays.

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

On an AudioSource (`GetComponent<AudioSource>()`): `pitch`, `pan`, `lowPass`, `doppler`,
`dopplerScale`, `occlude`, `occlusionVolume`, `occlusionLowPass`, `distanceLowPass`,
`distanceLowPassHz`, `isPlaying`, `Play()`, `Stop()`, `Pause()`, `Resume()`,
`PlayOneShot(volume)`, `FadeIn(seconds)`, `FadeOut(seconds)`.
