# 7. Audio

Software stereo mixer, WAV/OGG/MP3 clips (mono or stereo) and streamed long sounds, audio sources with 3D falloff, doppler, occlusion and distance muffling, reverb zones, a music player and saved volume settings. Code is
`engine/include/okay/Audio/*` (7 headers), `engine/src/Audio/*` and two components; the host programs
(`player/main.cpp`, `editor/main.cpp`) open the sound device. Doc: `docs/audio.md`. Nothing is in flight; #574 added a captions
model (`Core/Subtitles`, see 10.1 and 32.2) that audio does not feed yet.

## 7. Audio

### 7.1 Mixer and Audio Mixer
- **What:** The mixer sums every playing audio source into stereo, applies group volumes, master volume, mute, music ducking under voice, distance panning, and (when asked) doppler, occlusion, distance muffling and a reverb zone, and reports peak levels for the editor meters.
- **Parts:**
  - 7.1.1 Mixer core - `Audio/AudioMixer` - `RenderStereo` and `Render`: voices, resampling to the device rate, stereo voices, pan gains (balance for stereo 2D, mono-then-pan for 3D), one-pole low-pass per channel, streaming voices, clamping
  - 7.1.2 Groups, master and mute - `Audio/AudioMixer` - SFX, Music, UI and Voice groups each with volume and mute under `masterVolume`; `focusMuted` for a background window
  - 7.1.3 Ducking - `Audio/AudioMixer` - Music dips while a Voice source plays (level, attack, release)
  - 7.1.4 Spatial listener - `Audio/AudioMixer` - `SetListener` set from the main camera each audio refill; `Attenuation` and `PanGains`; doppler from the listener and source motion between refills
  - 7.1.7 Reverb bus - `Audio/SpatialFx` (`SchroederReverb`) - eight damped combs into four all-passes, fed by 3D sources while the listener is inside a Reverb volume; skipped entirely when dry so an old mix is unchanged
  - 7.1.5 Audio Mixer panel - `editor/main.cpp` - master and group sliders, mutes, output meters, ducking controls
  - 7.1.6 Device feed - `player/main.cpp`, `editor/main.cpp` - open the audio device through `okay::platform` (SDL2 by default, our own ALSA backend with `-DOKAY_PLATFORM_BACKEND=own`) and keep about 70 ms queued from `RenderStereo`
- **Code:** `Audio/AudioMixer`, `editor/main.cpp` (Audio Mixer panel), `player/main.cpp` (audio device loop)
- **API:** `AudioMixer::masterVolume`, `AudioMixer::groupVolume`, `AudioMixer::groupMuted`, `AudioMixer::duckMusicUnderVoice`, `AudioMixer::SetListener`, `AudioMixer::peak`, script `Audio.SetGroupVolume`, `set_group_volume`, `set_ducking`
- **Editor:** View > Panels > Audio Mixer
- **Docs:** `docs/audio.md`
- **Tests:** `audio_mixer`, `master_volume`, `audio`, `spatial_audio`, `volumes`
- **Status:** Works - AudioMixer.hpp and the mixing code in AudioMixer.cpp read (ducking ramp, low-pass, spatial pan, doppler, occlusion, distance low-pass, reverb send); player and editor feed it; covered by `audio_mixer`, `master_volume` and `spatial_audio`
- **Open:** only four fixed groups (no user-made buses, effect chain or snapshots); levels set in the editor panel are for the editor session only (players save their own, 7.4); one reverb bus for the listener (not a send per source, and the tail is not added once the listener has left the zone)
- **Related:** 7.3, 7.4, 4.7, 37.1
- **Words:** mixer, Audio Mixer, master volume, group volume, ducking, bus, SFX, Music, UI, Voice, mute, meters, peak, AudioMixer
- **Seen as:** no sound at all; music too loud under dialogue; everything the same volume; the sound pans the wrong way or stays in the centre; sound stops when the window loses focus
- **Ask me like:** "make the music quieter while someone is talking"; "why is the game silent in the build?"

### 7.2 Formats and clips
- **What:** Audio files (WAV, OGG, MP3) are decoded once into float clips that keep their channels (mono or stereo; downmix only on request) at the mixer's rate and shared between sources; long sounds can instead be streamed from disk in a fixed small amount of memory; generators make simple test tones.
- **Parts:**
  - 7.2.1 Clip data and WAV - `Audio/AudioClip` - interleaved samples, channels (1 or 2) and rate; load and save WAV (8/16/24/32-bit PCM and float), `Downmixed`, per-channel resample, sine/square/noise/silence generators
  - 7.2.2 OGG and MP3 decoding - `engine/src/Audio/AudioDecode.cpp` - the engine's own Ogg Vorbis and MP3 decoders and the shared decode cache (`LoadCached`)
  - 7.2.3 Path loading - `Audio/AudioLoad` - `LoadAudioFile`, `LoadSceneAudio`, the base folder used for project-relative audio paths
  - 7.2.5 Streaming - `Audio/AudioStream`, `Audio/AudioByteSource`, `Mp3Stream` in `Audio/Mp3Decoder` - `AudioStream::OpenFile` / `OpenMemory` (WAV direct, MP3 through the incremental `Mp3Stream`, OGG through the incremental `VorbisStream`), a 32,768-frame ring filled by `Pump` ahead of the mixer on the mixing thread (no locks), `Seek` (exact), seamless `loop`, `Underruns` (silence, no skipping), `MemoryBytes` under 2 MB for any length
  - 7.2.6 Vorbis decoder - `Audio/VorbisDecoder` - Ogg Vorbis I written in-house: Ogg pages with CRC and packet reassembly, the three headers, Huffman codebooks, floors 0 and 1, residue 0/1/2, channel coupling, block-size switching windows, IMDCT, overlap-add, trim to the final granule position; skips damaged pages, decodes the first of chained streams by default. `VorbisStream` is the page-by-page form (reads pages on demand, keeps the overlap state, indexes every 16th page for exact seeks); `DecodeVorbis` and `VorbisStream` share one packet decoder, so whole-file and streamed output are identical
  - 7.2.4 MP3 decoder - `Audio/Mp3Decoder` - MPEG-1/2/2.5 Layer III written in-house: frame sync, side info, bit reservoir, Huffman, requantisation, MS/intensity stereo, short/mixed blocks, IMDCT, polyphase synthesis; skips ID3v2 and the Xing/Info/VBRI frame, trims LAME encoder delay and padding, resynchronises past damage; `DecodeMp3` and `Mp3Stream` share one frame decoder, so whole-file and streamed output are identical
- **Code:** `Audio/AudioClip`, `engine/src/Audio/AudioDecode.cpp`, `Audio/AudioLoad`, `Audio/Mp3Decoder` (`engine/src/Audio/Mp3Decoder.cpp`, tables in `Mp3Tables.hpp`), `Audio/AudioStream` (`engine/src/Audio/AudioStream.cpp`), `Audio/AudioByteSource`, `Audio/VorbisDecoder` (`engine/src/Audio/VorbisDecoder.cpp`)
- **API:** `AudioClip`, `AudioClip::Load`, `LoadWAV`, `LoadOGG`, `LoadMP3`, `LoadCached`, `Resampled`, `SaveWAV`, `LoadAudioFile`, `LoadSceneAudio`, `AudioBaseDir`, `DecodeMp3`, `Mp3Audio`, `Mp3Options`, `AudioClip::Downmixed`, `AudioClip::FromChannels`, `AudioStream`, `AudioByteSource`, `Mp3Stream`, `OpenAudioStream`, `DecodeVorbis`, `VorbisAudio`, `VorbisOptions`, `VorbisStream`
- **Editor:** Project panel Audio category and Audio Clip asset icon; AudioSource inspector file picker
- **Docs:** `docs/audio.md`
- **Tests:** `audio_formats`, `wav`, `mp3_decoder`, `vorbis_decoder`, `audio_stereo`, `audio_stream`
- **Status:** Works - AudioClip.hpp, the WAV loader and the AudioDecode.cpp head read; covered by `audio_formats` and `wav`; the MP3 decoder is checked against ffmpeg reference decodes (about 86 dB SNR on LAME files, 75-83 dB on synthetic intensity/mixed-block streams, limited by the 16-bit references) and the Vorbis decoder against libvorbis reference decodes (129-140 dB against the float output, 65-94 dB against the 16-bit fixtures); both are fuzzed under ASan/UBSan by `mp3_decoder` and `vorbis_decoder`; `audio_stream` checks streamed output equals whole-file decode sample for sample on every WAV, MP3 and OGG fixture (all 30 Ogg decoder fixtures included), exact seeks, seamless loops, a ten-minute file under 2 MB, underruns and damaged files
- **Open:** the Vorbis decoder rejects more than 32 channels and codebooks that claim more than 2M entries, and floor type 0 (written by no current encoder) is only checked against libvorbis on generated streams; streaming has no sample cap (whole-file `DecodeVorbis` stops at `maxSamples`); streams are not resampled to the device rate in advance (the mixer interpolates); files over 2 GB are refused; more than two channels are averaged to mono; per-clip import settings exist only as data (`Core/ImportSettings`, area 20.4) and the loader does not read them; no FLAC; the MP3 decoder is Layer III only (no Layer I/II, no free-format bitrates)
- **Related:** 7.3, 20.4, 1.7
- **Words:** wav, ogg, mp3, vorbis, audio clip, decode, resample, sample rate, LoadAudioFile, Mp3Decoder, DecodeMp3, id3, xing, lame, gapless, VorbisDecoder, DecodeVorbis, VorbisStream, ogg page, crc, floor, residue, mdct, streaming
- **Seen as:** a sound file will not play or is listed in the Console as unreadable; a sound plays too fast or too slow; a long song uses a lot of memory (tick Stream); stereo music sounds off-centre or one-sided (Pan on a stereo clip is a balance)
- **Ask me like:** "add a FLAC loader"; "my ogg file will not load"

### 7.3 Audio sources and music player
- **What:** The AudioSource component plays a clip or, with Stream, a file read as it plays, with volume, pitch, pan or balance, loop, seek, 3D falloff, low-pass, fades and overlapping one-shots; the Music Player plays a playlist (streamed by default) with crossfades.
- **Parts:**
  - 7.3.1 Audio source - `Components/AudioSource` - volume, pitch, pan (balance for stereo 2D clips), group, loop, play on awake, `stream` (Inspector checkbox), rolloff (linear, inverse, logarithmic), `lowPass`; doppler, occlusion and distance low-pass (each off by default, 3D only); C++ `Seek`, `Time`, `Duration`
  - 7.3.2 Fades and one-shots - `Components/AudioSource` - `FadeIn`, `FadeOut`, `FadeTo`; `PlayOneShot` up to 16 voices per source
  - 7.3.3 Music player - `Components/MusicPlayer` - playlist, shuffle, loop playlist, `stream` (on by default), crossfade between two child sources on the Music group
- **Code:** `Components/AudioSource`, `Components/MusicPlayer`, `editor/main.cpp` (AudioSource and Music Player inspectors)
- **API:** `AudioSource`, `AudioGroup`, `AudioRolloff`, `PlayOneShot`, `FadeIn`, `FadeOut`, `MusicPlayer`, script `play_sound`, `music_play`, `music_next`, `music_stop`
- **Editor:** Add Component > Audio > Audio Source and Music Player; Inspector with file picker
- **Docs:** `docs/audio.md`
- **Tests:** `audio`, `audio_music`, `audio_stereo`, `audio_stream`, `wav`, `vs_media`, `spatial_audio`
- **Status:** Works - AudioSource.hpp and MusicPlayer.hpp read (crossfade start and fades are in the headers); covered by `audio_music`, `audio`, `audio_stereo` (placement), `audio_stream` (the Stream option, Music Player streaming, save and load of the flag), `spatial_audio` (doppler ratio, occlusion, distance curve, stereo and stream still correct) and `vs_media` (visual-script music nodes)
- **Open:** one listener (the main camera); Seek is C++ only (no script or visual-script call); the editor Add Component menu labels and script bindings for audio live in areas 15 and 13
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
- **Open:** the volume sliders belong to the pause and settings screens (area 12.8). An Audio Source `caption` is shown by the caption widget when sound captions are on (32.2)
- **Related:** 12.8, 1.7, 32.2, 7.1
- **Words:** volume slider, master volume, music volume, effects volume, mute, saved settings, prefs, audio.* keys, mute when unfocused, settings screen
- **Seen as:** the volume the player chose is forgotten next launch; the volume slider changes nothing; the game is loud again after restart
- **Ask me like:** "save the player's music volume"; "mute the game when I alt-tab"
