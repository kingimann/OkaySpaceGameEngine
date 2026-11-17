---
id: 9
group: switches
row: | 9 | Own WAV decoder (**the default** since 2026-11-16), whole and streamed | nothing to set; `OKAY_OWN_WAV=0` for the old one; `examples\own_wav` (`own_wav_test`, and `stream_test` for streaming) | Console line says which reader ran (the launcher writes it to `okay_launcher.log`); Windows sound unverified |
---
### 9. Own WAV decoder (the default now)

* **What changed.** Since the owner's approval of 2026-11-16 ("Switch, but keep improving it") every WAV file loads with
  our own reader by default: sounds in the editor and in exported games (`AudioClip::LoadWAV`, whole-file loads),
  streamed sounds (a Music Player, or an Audio Source with **Stream** ticked: since 2026-11-16 these use our reader too)
  and the launcher's voice clips. Nothing to set.
* **Going back (this release only).** `set OKAY_OWN_WAV=0` in a Command Prompt, then start the editor (or launcher)
  from that same window. `OKAY_OWN_WAV=1` forces ours. The next release deletes the old one.
* **How to tell which one ran.** The first WAV load of a run writes one line to the log and the editor's **Console**:
  `audio: WAV files load with our own reader (set OKAY_OWN_WAV=0 for the old one)`, or
  `audio: WAV files load with the OLD reader (...)`. The launcher writes the same line once per run, at the first voice
  clip it plays, to `okay_launcher.log` in the launcher's folder (next to `OkaySpace.exe`).
* **Sample file.** `examples\own_wav\mulaw_tone.wav` (2 s of a 440 Hz sine, 8-bit mu-law, 22,050 Hz; we generated it
  from our own signal, no outside source) and the scene `own_wav_test.okayscene` that plays it.
* **Compare the two.**
  1. Start the editor normally (no variable). **File > Open...** `examples\own_wav\own_wav_test.okayscene`, Play. You
     should hear a **clean, soft steady tone** (about A, 440 Hz), and the Console shows the "our own reader" line.
  2. Close the editor. In a Command Prompt: `set OKAY_OWN_WAV=0`, then start `OkayEngine.exe` from that window, open
     the same scene, Play. The old reader does not read the format tag: it treats the bytes as plain 8-bit samples, so
     you should hear the same note but **distorted and buzzy**, at a different loudness (worked out by computing what
     the old code does to these bytes: error 1.09 on a 0.5 amplitude signal; not heard), and the Console shows the
     "OLD reader" line.
  3. For an ordinary 16-bit WAV of your own the two sound the same (the samples are equal bit for bit). For 8, 24 and
     32-bit files ours rounds to the nearest 16-bit step where the old launcher path cut toward zero: at most 2 steps
     out of 32,768 apart, not audible.
* **Streamed (new).** `examples\own_wav\alaw_tone.wav` (2 s of a 440 Hz sine, 8-bit A-law, 22,050 Hz; made by
  `scripts/gen_own_wav_alaw_tone.py`, our own signal and encoder) and the scene `stream_test.okayscene`, whose **Music
  Player** streams it (Stream is on).
  1. Start the editor normally. **File > Open...** `examples\own_wav\stream_test.okayscene`, Play. You should hear a
     **clean, soft steady tone** (440 Hz), repeating every 2 s, and the Console shows the "our own reader" line.
  2. With `set OKAY_OWN_WAV=0` (as above) the same scene plays the old streamed reader, which reads A-law bytes as plain
     8-bit samples: **loud, buzzy noise** around the note (worked out from the bytes: about 1.6 times as loud as the tone,
     samples up to 0.97 away from it; not heard).
  3. Any Audio Source works the same way: put `alaw_tone.wav` on it, tick **Stream**, Play.
* **Verified here (Linux):** streaming `alaw_tone.wav` through `AudioStream` gives the sine to within 0.0079 (one A-law
  step), and the scene loads with its Music Player set to stream that file (test `own_wav_stream`).
  `okay::wav::DecodeWav` reads `mulaw_tone.wav` as 22,050 Hz mono, 44,100 frames, mu-law, peak
  0.512, every sample within 0.016 of an ideal 440 Hz sine of amplitude 0.5; the WAV and audio tests pass with the new
  default and with `OKAY_OWN_WAV=0`. **Unverified:** what you hear on Windows, and the Console line on Windows.
