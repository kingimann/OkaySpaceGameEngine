# 7. Audio

Software stereo mixer, WAV/OGG/MP3 clips, audio sources with 3D falloff, a music player and saved volume settings. Code is
`engine/include/okay/Audio/*` (3 headers), `engine/src/Audio/*` (about 500 lines) and two components; the host programs
(`player/main.cpp`, `editor/main.cpp`) open the sound device. Doc: `docs/audio.md`. Nothing is in flight; PR #574 adds a captions
model (see 10.1).

## 7. Audio

### 7.1 Mixer and Audio Mixer
- **What:** The mixer sums every playing audio source into stereo, applies group volumes, master volume, mute, music ducking under voice and distance panning, and reports peak levels for the editor meters.
- **Parts:**
  - 7.1.1 Mixer core - `Audio/AudioMixer` - `RenderStereo` and `Render`: voices, resampling to the device rate, pan gains, one-pole low-pass, clamping
  - 7.1.2 Groups, master and mute - `Audio/AudioMixer` - SFX, Music, UI and Voice groups each with volume and mute under `masterVolume`; `focusMuted` for a background window
  - 7.1.3 Ducking - `Audio/AudioMixer` - Music dips while a Voice source plays (level, attack, release)
  - 7.1.4 Spatial listener - `Audio/AudioMixer` - `SetListener` set from the main camera each audio refill; `Attenuation` and `PanGains`
  - 7.1.5 Audio Mixer panel - `editor/main.cpp` - master and group sliders, mutes, output meters, ducking controls
  - 7.1.6 Device feed - `player/main.cpp`, `editor/main.cpp` - open the SDL audio device and keep about 70 ms queued from `RenderStereo`
- **Code:** `Audio/AudioMixer`, `editor/main.cpp` (Audio Mixer panel), `player/main.cpp` (audio device loop)
- **API:** `AudioMixer::masterVolume`, `AudioMixer::groupVolume`, `AudioMixer::groupMuted`, `AudioMixer::duckMusicUnderVoice`, `AudioMixer::SetListener`, `AudioMixer::peak`, script `Audio.SetGroupVolume`, `set_group_volume`, `set_ducking`
- **Editor:** View > Panels > Audio Mixer
- **Docs:** `docs/audio.md`
- **Tests:** `audio_mixer`, `master_volume`, `audio`
- **Status:** Works - AudioMixer.hpp and the mixing code in AudioMixer.cpp read (ducking ramp, low-pass, spatial pan); player and editor feed it; covered by `audio_mixer` and `master_volume`
- **Open:** only four fixed groups (no user-made buses, effect chain or snapshots); low-pass is the only effect; no reverb, occlusion or doppler anywhere in the code; levels set in the editor panel are for the editor session only (players save their own, 7.4)
- **Related:** 7.3, 7.4, 4.7, 37.1
- **Words:** mixer, Audio Mixer, master volume, group volume, ducking, bus, SFX, Music, UI, Voice, mute, meters, peak, AudioMixer
- **Seen as:** no sound at all; music too loud under dialogue; everything the same volume; the sound pans the wrong way or stays in the centre; sound stops when the window loses focus
- **Ask me like:** "make the music quieter while someone is talking"; "why is the game silent in the build?"

### 7.2 Formats and clips
- **What:** Audio files (WAV, OGG, MP3) are decoded once into mono float clips at the mixer's rate and shared between sources; generators make simple test tones.
- **Parts:**
  - 7.2.1 Clip data and WAV - `Audio/AudioClip` - mono samples plus rate; load and save WAV (8/16/24/32-bit PCM and float), downmix, resample, sine/square/noise/silence generators
  - 7.2.2 OGG and MP3 decoding - `engine/src/Audio/AudioDecode.cpp` - stb_vorbis and dr_mp3 decoders and the shared decode cache (`LoadCached`)
  - 7.2.3 Path loading - `Audio/AudioLoad` - `LoadAudioFile`, `LoadSceneAudio`, the base folder used for project-relative audio paths
- **Code:** `Audio/AudioClip`, `engine/src/Audio/AudioDecode.cpp`, `Audio/AudioLoad`
- **API:** `AudioClip`, `AudioClip::Load`, `LoadWAV`, `LoadOGG`, `LoadMP3`, `LoadCached`, `Resampled`, `SaveWAV`, `LoadAudioFile`, `LoadSceneAudio`, `AudioBaseDir`
- **Editor:** Project panel Audio category and Audio Clip asset icon; AudioSource inspector file picker
- **Docs:** `docs/audio.md`
- **Tests:** `audio_formats`, `wav`
- **Status:** Works - AudioClip.hpp, the WAV loader and the AudioDecode.cpp head read; covered by `audio_formats` and `wav`
- **Open:** clips are always mono (stereo files are downmixed) and fully decoded into memory, so long music is memory heavy and there is no streaming; per-clip import settings exist only as data (`Core/ImportSettings`, area 20.4) and the loader does not read them; no FLAC
- **Related:** 7.3, 20.4, 1.7
- **Words:** wav, ogg, mp3, vorbis, audio clip, decode, resample, sample rate, LoadAudioFile, stb_vorbis, dr_mp3
- **Seen as:** a sound file will not play or is listed in the Console as unreadable; a sound plays too fast or too slow; stereo sound comes out mono; a long song uses a lot of memory
- **Ask me like:** "add a FLAC loader"; "my ogg file will not load"

### 7.3 Audio sources and music player
- **What:** The AudioSource component plays a clip with volume, pitch, pan, loop, 3D falloff, low-pass, fades and overlapping one-shots; the Music Player plays a playlist with crossfades.
- **Parts:**
  - 7.3.1 Audio source - `Components/AudioSource` - volume, pitch, pan, group, loop, play on awake, rolloff (linear, inverse, logarithmic), `lowPass`
  - 7.3.2 Fades and one-shots - `Components/AudioSource` - `FadeIn`, `FadeOut`, `FadeTo`; `PlayOneShot` up to 16 voices per source
  - 7.3.3 Music player - `Components/MusicPlayer` - playlist, shuffle, loop playlist, crossfade between two child sources on the Music group
- **Code:** `Components/AudioSource`, `Components/MusicPlayer`, `editor/main.cpp` (AudioSource and Music Player inspectors)
- **API:** `AudioSource`, `AudioGroup`, `AudioRolloff`, `PlayOneShot`, `FadeIn`, `FadeOut`, `MusicPlayer`, script `play_sound`, `music_play`, `music_next`, `music_stop`
- **Editor:** Add Component > Audio > Audio Source and Music Player; Inspector with file picker
- **Docs:** `docs/audio.md`
- **Tests:** `audio`, `audio_music`, `wav`, `vs_media`
- **Status:** Works - AudioSource.hpp and MusicPlayer.hpp read (crossfade start and fades are in the headers); covered by `audio_music`, `audio` and `vs_media` (visual-script music nodes)
- **Open:** one listener (the main camera); no streaming sources; the editor Add Component menu labels and script bindings for audio live in areas 15 and 13
- **Related:** 7.1, 7.2, 10.1, 9.4
- **Words:** AudioSource, MusicPlayer, music, playlist, crossfade, one-shot, footsteps, 3D sound, spatial, rolloff, min distance, max distance, fade in, fade out, pitch, pan
- **Seen as:** music cuts instead of crossfading; a sound does not get quieter with distance; a gunshot cuts off the previous one; sound plays from the start every time it should resume
- **Ask me like:** "add footstep sounds that overlap"; "crossfade between two music tracks"

### 7.4 Saved volume settings
- **What:** The player's master, group volumes and mutes are saved to Prefs (`audio.*` keys) and loaded on the next start; the Pause Menu and Settings screen sliders drive them.
- **Parts:**
  - 7.4.1 Settings load and save - `Audio/AudioMixer` - `LoadSettings` and `SaveSettings` under `audio.*` Prefs keys
  - 7.4.2 Settings screen sliders - `Components/SettingsScreen` - Music and Effects sliders write the groups and save
  - 7.4.3 Mute when unfocused - `Audio/AudioMixer`, `player/main.cpp` - Build Settings option sets `focusMuted` while the window is in the background (never saved)
- **Code:** `Audio/AudioMixer`, `Components/SettingsScreen`, `Components/PauseMenu`, `player/main.cpp`
- **API:** `AudioMixer::LoadSettings`, `AudioMixer::SaveSettings`, `Audio.SaveSettings`, `save_audio_settings`, Build Settings "Mute audio when unfocused"
- **Editor:** Build Settings (mute when unfocused); Settings Screen and Pause Menu inspectors
- **Docs:** `docs/audio.md`
- **Tests:** `master_volume`, `audio_mixer`
- **Status:** Works - LoadSettings and SaveSettings call sites and the focus-mute line in player/main.cpp read; the save and load round trip is exercised in `audio_mixer`
- **Open:** the volume sliders belong to the pause and settings screens (area 12.8); no subtitle or caption link yet (in flight, 10.1)
- **Related:** 12.8, 1.7, 32.2, 7.1
- **Words:** volume slider, master volume, music volume, effects volume, mute, saved settings, prefs, audio.* keys, mute when unfocused, settings screen
- **Seen as:** the volume the player chose is forgotten next launch; the volume slider changes nothing; the game is loud again after restart
- **Ask me like:** "save the player's music volume"; "mute the game when I alt-tab"
