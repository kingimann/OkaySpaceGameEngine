---
id: 9
group: switches
row: | 9 | Own WAV decoder (off by default) | `OKAY_OWN_WAV=1`, `examples\own_wav` | Switch works; no log line |
---
### 9. Own WAV decoder

* **Variable.** `OKAY_OWN_WAV=1` (and `0` to force the old one). It is **not** part of `OKAY_OWN_STACK=1` and **not** in
  the startup line; it is only in the engine's `AudioClip::LoadWAV` (whole-file loads; streamed large files still use
  the old parser).
* **Sample file.** `examples\own_wav\mulaw_tone.wav` (2 s of a 440 Hz sine, 8-bit mu-law, 22,050 Hz; we generated it
  from our own signal, no outside source) and the scene `own_wav_test.okayscene` that plays it.
* **Steps.**
  1. Start the editor with no variable. **File > Open...** `examples\own_wav\own_wav_test.okayscene`, Play. The old
     decoder does not read the format tag: it treats the bytes as plain 8-bit samples, so you should hear the same
     note but **distorted and buzzy**, at a different loudness (worked out by computing what the old code does to
     these bytes: its output is nowhere near the sine, error 1.09 on a 0.5 amplitude signal; not heard).
  2. Close the editor, `set OKAY_OWN_WAV=1`, start it again, open the same scene, Play. You should hear a **clean, soft
     steady tone** (about A, 440 Hz). That difference is the proof the own decoder is in use.
* **Verified here:** `okay::wav::DecodeWav` (compiled on its own) reads this file as 22,050 Hz mono, 44,100 frames,
  mu-law, peak 0.512, and every sample within 0.016 of an ideal 440 Hz sine of amplitude 0.5. The
  `own_wav` test covers mu-law. **Unverified:** what you hear on Windows.
