# WAV differential fixtures

Made by `scripts/gen_wav_ffmpeg_fixtures.py` for `tests/test_wav_ffmpeg_diff.cpp`.

- **What:** our own synthetic signal (two sines, a chirp, a click and a silent gap, 11,025 Hz, a quarter of a
  second; the right channel is a different mix) encoded into each WAV coding the own decoder reads, and each
  file decoded back to signed 16-bit little-endian samples (`<name>.s16`).
- **Tool:** ffmpeg 6.1.1 (Ubuntu 6.1.1-3ubuntu5), run as a program with `-bitexact`. ffmpeg's output is data we
  generated; no ffmpeg code is in the repository and none was read.
- **Licence:** the signal and these files are ours (made in this repository), under the project's licence.
- To remake them: `python3 -I scripts/gen_wav_ffmpeg_fixtures.py` from the repository root (needs ffmpeg). A
  different ffmpeg version may write different ADPCM bytes; the test only needs each `.wav` to match its `.s16`.
