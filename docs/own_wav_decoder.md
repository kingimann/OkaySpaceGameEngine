# Our own WAV decoder (`okay::wav`)

Status: **the default since the owner's approval of 2026-11-16** ("Switch, but keep improving it"). The old path (`SDL_LoadWAV` in the launcher, the old reader in `AudioClip::LoadWAV`) stays selectable for one release (`OKAY_OWN_WAV=0`) and is then deleted (roadmap step 5). Part of `docs/own_code_roadmap.md` (the SDL2 row: `SDL_LoadWAV` in the launcher is the last WAV use of a library; the engine already had its own, narrower, WAV reader).

## Why this one

It is small, it has a public specification, it is fully testable on Linux, and it is not crypto, networking or a model reader. It does two jobs: it is the own replacement for `SDL_LoadWAV` (which also reads MS and IMA ADPCM and A-law / mu-law), and it makes the engine's `AudioClip::LoadWAV` safe against hostile files (budgets) and wider (ADPCM, A-law, mu-law, 64-bit float, WAVE_FORMAT_EXTENSIBLE, odd bit depths).

## What it is

| File | What |
|---|---|
| `engine/include/okay/Audio/WavDecoder.hpp`, `engine/src/Audio/WavDecoder.cpp` | `okay::wav::DecodeWav` (bytes in, interleaved float out, plus warnings), `ToS16`, `Limits`, `OwnWavEnabled` |
| `engine/src/Audio/AudioClip.cpp` | The seam: `AudioClip::LoadWAV` calls the own decoder when `OwnWavEnabled()` is true (the default); with `OKAY_OWN_WAV=0` the old code runs unchanged. It logs once per run which reader is in use |
| `tests/test_own_wav.cpp` | Test `own_wav` |
| `tests/bench_wav_decoder.cpp` | Not a test: speed and peak memory against `SDL_LoadWAV` (below, "Speed and memory against SDL") |

## The default, and going back

**Owner's approval, 2026-11-16, in chat: "Switch, but keep improving it".** Our decoder is now the default everywhere, with no variable set: `AudioClip::LoadWAV` (the engine, editor and player) and the launcher's voice clips (`shared/WavPlayback.hpp`). This is step 4 of "How a library is removed" in `docs/own_code_roadmap.md`.

- Going back, at run time: `OKAY_OWN_WAV=0` selects the old path (`SDL_LoadWAV` in the launcher, the old reader in `AudioClip::LoadWAV`). `OKAY_OWN_WAV=1` forces ours. Only the first character counts, and only `0` or `1`; anything else means the build default.
- Going back, at build time: `-DOKAY_OWN_WAV=OFF` (default `ON`) makes the old path the build default again (it defines `OKAY_OWN_WAV_DEFAULT_OFF` on the engine). Both paths are compiled in either way.
- Which one ran: the first WAV load of a run logs `audio: WAV files load with our own reader (set OKAY_OWN_WAV=0 for the old one)` (or `... with the OLD reader ...`), in the log and the editor's Console. The launcher's voice-clip path logs nothing.
- Step 5, **due one release after the flip**: delete the `SDL_LoadWAV` call in `shared/WavPlayback.hpp`, the old reader branch in `AudioClip::LoadWAV`, the `OKAY_OWN_WAV` option and variable, and `tests/check_wav_seam_wiring.cmake`'s "off path" check.
- Not part of `OKAY_OWN_STACK` (that switch is for features that are still off by default; this one is on).
- Tests for both settings: `own_wav` and `wav_playback` check the unset variable against the build default and set `0` and `1` explicitly; `own_wav_default_on` and `own_wav_default_off` compile the decoder both ways in every build and check unset, `0`, `1` and other values.

## What it reads

PCM 8 (unsigned), 16, 24, 32 bit and any bit depth from 1 to 32 stored left-justified in its byte container; IEEE float 32 and 64 (NaN and infinity become 0); A-law and mu-law (ITU-T G.711, 16-bit linear scale: A-law +-32256, mu-law +-32124); Microsoft ADPCM (tag 0x0002, one or two channels, the coefficient table from the fmt chunk or the standard seven); IMA / DVI ADPCM (tag 0x0011, 1 to 32 channels); `WAVE_FORMAT_EXTENSIBLE` (the standard sub-format GUID base is required; PCM, float, A-law, mu-law, both ADPCMs as sub-formats). `fmt ` and `data` chunks in either order, unknown chunks skipped, odd chunk padding honoured. Output is float in -1 to 1; `ToS16` rounds to nearest and clamps.

## Hostile input and budgets (`wav::Limits`)

Input over 1 GiB, more than 32 channels, a rate over 768,000 Hz or 0, more than 4,096 chunks, and a decoded size over 2^27 samples (512 MiB of float) are refused with a message, before anything is allocated. A data chunk longer than the file (or 0xFFFFFFFF, as streaming writers leave it) is cut at the end of the file, a half frame at the end is dropped, and an IMA step index over 88 is clamped, each with an entry in `warnings`. ADPCM block sizes must fit their headers, an MS ADPCM predictor index outside the table is an error, positions are 64-bit so a chunk size near 2^32 cannot wrap. Nothing reads outside the input; the output size is bounded by the input size times a constant (ADPCM expands at most 4 samples per byte).

## The playback seam (launcher voice clips)

The launcher's only WAV load (`VoiceClipFor`, voice clips) goes through `okay::wav::LoadWavForSdl` in `shared/WavPlayback.hpp`. With `OKAY_OWN_WAV=0` (or a `-DOKAY_OWN_WAV=OFF` build) it calls `SDL_LoadWAV` exactly as before and copies the samples out. By default (since 2026-11-16) it calls `okay::wav::LoadForPlayback` (`engine/include/okay/Audio/WavPlayback.hpp`, no SDL in the header) and moves the result into the caller's buffer (no copy). `LoadForPlayback` decodes straight to the device format the call site plays, with the same values `DecodeWav` and `ToS16` give (the in-place layout is described under "Speed and memory against SDL"): 32-bit float stays native float, every other encoding (PCM 8/24/32, A-law, mu-law, ADPCM) becomes native 16-bit via `ToS16`; channels and rate are kept. No `SDL_ConvertAudio`. PCM16 mono and stereo come out byte for byte the same as `SDL_LoadWAV`; 8-bit and 32-bit PCM arrive as 16-bit instead of U8/S32 (the call site converted them to 16-bit anyway). Failures (truncated, zero length, bad header, over budget, missing file) return false with a message and no samples. Written from the `okay::wav` output and the call site's needs only. `tests/check_wav_seam_wiring.cmake` (`ctest -R wav_seam_wiring`) fails if `SDL_LoadWAV` is called anywhere in launcher/, shared/, editor/, player/, engine/ or tools/ except that header. Tests: `ctest -R wav_playback` (own path, hostile and zero-length input, and with SDL2 present the option-off versus option-on equivalence). The test WAVs are built in memory (no WAV fixtures are checked in).

## Speed and memory against SDL

Owner rule (2026-11-16): "We only switch when it's faster and up to par": our decoder may only become the default when it is at least as fast as `SDL_LoadWAV`, uses no more memory on the same files, and is at least as good.

**How it was measured.** `tests/bench_wav_decoder.cpp` (built in the test build when SDL2 is found, not run by ctest; how to run it is in `docs/try_it.md`). Per file: the median wall time of at least 9 runs, and the peak heap above the start of the call, counted by a malloc / free / realloc hook in the program (glibc), the same way for SDL's C allocations and our C++ ones. Two comparisons:

- **Final** (what the launcher really does, `VoiceClipFor`): `LoadWavForSdl` with our decoder on (`LoadForPlayback`) against it with our decoder off (`SDL_LoadWAV`, copy, `SDL_FreeWAV`), each followed by the launcher's own `VoiceClipToPlayable` (SDL hands back 8-bit as U8 and 24/32-bit as S32, which that function turns into 16-bit; ours already is). Both end with native 16-bit samples, or float for float32 files. This is the comparison the rule is about.
- **Raw**: reading the file and `DecodeWav` (float out, the form `AudioClip::LoadWAV` wants) against `SDL_LoadWAV` alone (SDL's own output: for PCM and float that is the file's samples untouched, so SDL does almost no work and uses half or a third of the memory of a float result). Not like for like: shown because it was asked for.

Files: the repository's 13 WAV files (`tests/data/audio/wav_ffmpeg`, encoded by ffmpeg, and two in `examples/`) and 13 generated ones (the bench writes them; 44.1 kHz stereo unless named: PCM and float carry a tone plus noise, the A-law, mu-law and ADPCM payloads are pseudo-random bytes behind valid headers). Release build, GCC 13.3, one core of an Intel Xeon at 2.1 GHz, Linux, 2026-11-16. Ratio = ours / SDL; below 1 is better for us. Times under 0.1 ms are a few microseconds and noisy.

| File | MiB | Final: ms ours / SDL | Final: MiB ours / SDL | Final met? | Raw: ms ours / SDL | Raw: MiB ours / SDL | Raw met? |
|---|---|---|---|---|---|---|---|
| `alaw_stereo.wav` | 0.005 | 0.006 / 0.013 (0.48) | 0.011 / 0.021 (0.54) | met | 0.007 / 0.020 (0.36) | 0.035 / 0.015 (2.32) | not met (more memory) |
| `f32_mono.wav` | 0.011 | 0.004 / 0.005 (0.84) | 0.011 / 0.021 (0.54) | met | 0.007 / 0.005 (1.52) | 0.029 / 0.015 (1.97) | not met (slower, more memory) |
| `ima_mono.wav` | 0.002 | 0.013 / 0.021 (0.63) | 0.010 / 0.016 (0.65) | met | 0.012 / 0.019 (0.62) | 0.026 / 0.014 (1.82) | not met (more memory) |
| `ima_stereo.wav` | 0.003 | 0.018 / 0.028 (0.64) | 0.015 / 0.023 (0.65) | met | 0.019 / 0.028 (0.68) | 0.035 / 0.019 (1.81) | not met (more memory) |
| `ms_mono.wav` | 0.002 | 0.020 / 0.029 (0.67) | 0.010 / 0.016 (0.66) | met | 0.025 / 0.029 (0.89) | 0.026 / 0.014 (1.82) | not met (more memory) |
| `ms_mono_b256.wav` | 0.002 | 0.016 / 0.024 (0.67) | 0.008 / 0.012 (0.65) | met | 0.019 / 0.022 (0.86) | 0.021 / 0.012 (1.82) | not met (more memory) |
| `ms_stereo.wav` | 0.003 | 0.019 / 0.025 (0.75) | 0.015 / 0.023 (0.65) | met | 0.021 / 0.024 (0.89) | 0.034 / 0.019 (1.81) | not met (more memory) |
| `ms_stereo_b2048.wav` | 0.004 | 0.022 / 0.032 (0.68) | 0.020 / 0.031 (0.64) | met | 0.028 / 0.031 (0.91) | 0.043 / 0.024 (1.81) | not met (more memory) |
| `mulaw_mono.wav` | 0.003 | 0.005 / 0.010 (0.52) | 0.006 / 0.011 (0.59) | met | 0.003 / 0.009 (0.37) | 0.021 / 0.010 (2.22) | not met (more memory) |
| `s24_stereo.wav` | 0.016 | 0.011 / 0.038 (0.30) | 0.017 / 0.042 (0.40) | met | 0.011 / 0.009 (1.24) | 0.045 / 0.025 (1.77) | not met (slower, more memory) |
| `u8_mono.wav` | 0.003 | 0.005 / 0.010 (0.53) | 0.006 / 0.008 (0.79) | met | 0.003 / 0.004 (0.75) | 0.021 / 0.007 (3.04) | not met (more memory) |
| `mulaw_tone.wav` | 0.042 | 0.032 / 0.076 (0.42) | 0.085 / 0.168 (0.51) | met | 0.021 / 0.079 (0.27) | 0.219 / 0.089 (2.47) | not met (more memory) |
| `tone.wav` | 0.168 | 0.012 / 0.015 (0.81) | 0.169 / 0.336 (0.50) | met | 0.025 / 0.009 (2.91) | 0.513 / 0.173 (2.97) | not met (slower, more memory) |
| `gen_pcm16_stereo_1s.wav` | 0.168 | 0.010 / 0.013 (0.80) | 0.169 / 0.336 (0.50) | met | 0.025 / 0.009 (2.72) | 0.513 / 0.173 (2.97) | not met (slower, more memory) |
| `gen_pcm16_stereo_60s.wav` | 10.094 | 1.191 / 1.603 (0.74) | 10.095 / 20.187 (0.50) | met | 3.332 / 0.807 (4.13) | 30.289 / 10.098 (3.00) | not met (slower, more memory) |
| `gen_pcm16_stereo_10min.wav` | 100.937 | 67.2 / 115.1 (0.58) | 100.938 / 201.875 (0.50) | met | 217.3 / 48.1 (4.52) | 302.821 / 100.942 (3.00) | not met (slower, more memory) |
| `gen_pcm8_stereo_60s.wav` | 5.047 | 4.147 / 9.013 (0.46) | 10.095 / 15.141 (0.67) | met | 2.265 / 0.382 (5.94) | 25.243 / 5.051 (5.00) | not met (slower, more memory) |
| `gen_pcm24_stereo_60s.wav` | 15.141 | 40.1 / 47.1 (0.85) | 15.141 / 40.375 (0.38) | met | 9.126 / 5.247 (1.74) | 35.336 / 20.192 (1.75) | not met (slower, more memory) |
| `gen_pcm32_stereo_60s.wav` | 20.187 | 12.8 / 45.6 (0.28) | 20.188 / 40.375 (0.50) | met | 21.6 / 1.629 (13.26) | 40.383 / 20.192 (2.00) | not met (slower, more memory) |
| `gen_f32_stereo_60s.wav` | 20.187 | 3.862 / 15.4 (0.25) | 20.188 / 40.375 (0.50) | met | 20.4 / 1.443 (14.16) | 40.383 / 20.192 (2.00) | not met (slower, more memory) |
| `gen_f64_stereo_60s.wav` | 40.375 | 28.3 / fail (-) | 40.376 / - (-) | met (SDL cannot read it) | 23.1 / fail (-) | 60.571 / - (-) | met (SDL cannot read it) |
| `gen_alaw_stereo_60s.wav` | 5.047 | 4.022 / 22.1 (0.18) | 10.095 / 20.187 (0.50) | met | 2.790 / 20.6 (0.14) | 25.243 / 10.098 (2.50) | not met (more memory) |
| `gen_mulaw_stereo_60s.wav` | 5.047 | 3.997 / 9.462 (0.42) | 10.095 / 20.187 (0.50) | met | 3.069 / 8.876 (0.35) | 25.243 / 10.098 (2.50) | not met (more memory) |
| `gen_msadpcm_stereo_60s.wav` | 2.539 | 14.3 / 19.5 (0.73) | 12.636 / 20.193 (0.63) | met | 16.6 / 16.7 (1.00) | 22.741 / 12.640 (1.80) | not met (more memory) |
| `gen_ima_stereo_60s.wav` | 2.533 | 16.6 / 20.6 (0.81) | 12.632 / 20.196 (0.63) | met | 16.4 / 19.9 (0.82) | 22.738 / 12.636 (1.80) | not met (more memory) |
| `gen_ext_pcm16_6ch_48k_60s.wav` | 32.959 | 14.7 / 25.9 (0.57) | 32.962 / 65.922 (0.50) | met | 62.1 / 13.2 (4.70) | 98.891 / 32.965 (3.00) | not met (slower, more memory) |

**Result.** Final (the launcher's load): **met on all 26 files**, faster (ratio 0.18 to 0.85) and smaller (0.38 to 0.79 of SDL's peak); the float64 file only we read (SDL refuses it). Raw: **not met** on 25 of 26 files for memory (a float result is 2 to 5 times the size of SDL's untouched or 16-bit samples) and on the PCM and float files for time (SDL does not convert them at all); raw is faster than SDL for A-law, mu-law, MS and IMA ADPCM. "Up to par": our decoder reads everything SDL reads here plus float64, and the 16-bit result of PCM16 is byte for byte SDL's (`wav_playback`). Not equal to SDL's path: 8-bit and 24/32-bit, where the launcher's conversion after SDL scales by 32,767 and cuts toward zero, and ours scales by 32,768 and rounds to the nearest; ours is the exact one (`ToS16`).

**Before this work** the same bench showed our final load slower than SDL on 22 of 26 files (PCM16 60 s: 58.6 ms against 1.5 ms) and bigger on all 26 (2.1 to 4 times SDL's peak): it decoded to a float buffer, made a 16-bit copy, then a byte copy, then the launcher's seam copied it once more.

**What changed (output identical).**
- One chunk walk and format check (a "plan"), then a loop per format that writes straight into the target: float for `DecodeWav`, native 16-bit or float bytes for playback. No float buffer and no extra copies on the playback path.
- `LoadForPlayback` reads the first 512 bytes (up to 64 KiB if needed) to find the data chunk, then reads the samples into their own buffer and decodes inside it: PCM 16/24/32 and float in place front to back (an output sample is never wider than its input sample, and each is read before it is written); 8-bit PCM, A-law and mu-law from the buffer's second half into its first (output sample k ends before input sample k + 1 starts). ADPCM decodes into a new buffer. Unbuffered C stdio, so no stream buffer is allocated. `LoadWavForSdl` moves the bytes instead of copying them.
- PCM16 to 16-bit is one block move; float32 to float is a move plus a scan that zeroes NaN and infinity. G.711 decodes through two 256-entry tables made at start-up from the same functions; IMA through an 89 x 8 table of differences made from the same shifts and adds.
- 24-bit to 16-bit rounds with integer arithmetic, and float to 16-bit (also `ToS16`) adds and takes away 1.5 x 2^23 instead of calling `std::nearbyint`. Both were checked against the old `std::nearbyint` code on **every** input: all 16,777,216 24-bit values and all 4,278,190,082 non-NaN float values, 0 differences (GCC 13 `-O2`, x86-64). They need strict IEEE float (no `-ffast-math`, SSE not x87), which our builds use.

**How "output identical" was checked.** `bench_wav_decoder --digest` hashes, per file, `DecodeWav`'s whole result (samples bit for bit, warnings, error, header fields), `DecodeForPlayback`'s and `LoadForPlayback`'s bytes; for each of the 13 small files it also hashes every truncation and 2,000 random byte mutations (both decoders, results and error messages). The 40 hash lines are the same before and after, also in a build with AddressSanitizer and UndefinedBehaviorSanitizer (no report). `wav_playback` gained a check that `LoadForPlayback` (two buffers, in place) equals `DecodeForPlayback` (one buffer) and `ToS16(DecodeWav)` on files over 64 KiB with every chunk layout, width and widening format, odd sizes and refusals (245 checks); two deliberate breakages of the in-place layout each make it fail (one with a wrong value, one with a heap error). `own_wav` (22,163 checks; the `audioop` differential skipped, no `audioop` on this Python), `wav_ffmpeg_diff`, `audio_formats`, `wav` and `wav_seam_wiring` pass unchanged.

**Second speed pass, after the flip (2026-11-16, output identical).** Same machine, Release build, 60 s stereo files at 44.1 kHz (5.3 million samples). Times are the fastest of 41 runs (the machine is shared and noisy; the median moves by up to 30%).

| Path | File | Before | After |
|---|---|---|---|
| `DecodeWav` alone (float out, file already in memory) | PCM 32-bit | 7.3 ms | 2.1 ms |
| | float32 | 5.1 ms | 2.1 ms |
| | PCM 24-bit | 7.3 ms | 3.9 ms |
| | float64 | 5.5 ms | 4.1 ms |
| | PCM 16-bit, 8-bit, A-law, mu-law | 1.6 to 2.2 ms | unchanged (already one plain loop) |
| `DecodeForPlayback` (16-bit out, the launcher's path) | PCM 24-bit | 36.3 ms | 6.6 ms |
| | PCM 32-bit | 11.2 ms | 6.9 ms |
| `AudioClip::LoadWAV` (the engine's whole load: read the file, decode) | PCM 16-bit | 36 to 41 ms | 3.0 ms |
| | PCM 24-bit / 32-bit / float32 | 48 / 54 to 58 / 53 ms | 5.7 / 8.2 / 16.3 ms |
| | A-law / IMA ADPCM | 11 / 18 ms | 2.9 / 14 ms |

What changed:
- `DecodeWav`, packed files (block align = one frame): one plain loop per format with no per-sample function call, which the compiler turns into vector code: 16 and 32-bit integers are loaded with one `memcpy` and multiplied by the inverse power of two, float32 is copied with its non-finite values made 0 in the same pass, 24-bit samples are loaded as 4 bytes and shifted (the last one byte by byte, so nothing past the data is read). Multiplying by 2^-15, 2^-23 or 2^-31 is exact, and `float(x) * 2^-31` equals `float(double(x) / 2^31)` (scaling by a power of two commutes with rounding to float): checked on **every** 32-bit and 24-bit integer, 0 differences.
- The 16-bit playback output for 24 and 32-bit PCM and float64 got the same kind of loop, and the 24-bit rounding lost its branch (the low byte of real audio is noise, so the branch was mispredicted half the time: that was the 36 ms). The branch-free rounding and the min/max clamp were checked against the old code on all 2^24 24-bit values and all 4,278,190,082 non-NaN floats, 0 differences.
- Float32 to float32 playback: one pass (copy and clean together) instead of a move and then a scan.
- `AudioClip::LoadWAV`, and the OGG, MP3 and FLAC whole-file loads, read the file with one sized read instead of a byte-at-a-time stream iterator (that alone was 51 ms of a 20 MB file against 3 ms).
- Tried and dropped: a float lookup table for A-law and mu-law (slower than the 16-bit table and a scale).

`bench_wav_decoder`'s "own-raw" column moves less (32-bit PCM 26.5 to 21.1 ms, float32 22.4 to 17.9 ms) because most of it is the bench's own file read into a fresh 20 MB buffer, not the decoder. The "final" column (the launcher's load): 24-bit 40.6 to 8.4 ms (SDL 57.9), 32-bit 13.5 to 8.5 ms (SDL 52.7), 8-bit 4.1 to 2.6 ms, float32 4.7 to 3.2 ms; peaks unchanged. The `--digest` lines are the same as before the change; `own_wav`, `wav_playback`, `audio_stream` and the rest of the audio tests pass, and the decoder ran under AddressSanitizer and UndefinedBehaviorSanitizer on every generated and ffmpeg file and in `own_wav` and `wav_playback` (no report).

**Not verified.** Windows: nothing was built or run there (the bench needs glibc for memory; time alone would work). Only one machine. Real voice clips from the launcher's chat were not in the corpus (they are 16-bit PCM, which the generated 16-bit files cover). `AudioClip::LoadWAV` (the engine's float path) was not compared with the engine's older reader.

## Specifications used (rule 19a)

Written from public format descriptions only: the Microsoft RIFF / WAVE format registration notes (WAVEFORMAT, WAVEFORMATEX, WAVEFORMATEXTENSIBLE, the sub-format GUID base), ITU-T G.711 (the companding laws), the Microsoft ADPCM block layout and adaptation table, and the IMA Digital Audio Focus and Technical Working Group ADPCM description (the 89-entry step table and index adjust table). **No decoder source was read, copied or followed** (not SDL, libsndfile, dr_wav, ffmpeg or any other). The two ADPCM tables and the seven standard Microsoft coefficient pairs are standard constants from those descriptions, typed in from memory of the published tables and then checked against the independent decoder in Python's `audioop` (IMA, 120 random files) and by hand (MS ADPCM; see below).

## Tests (`ctest -R own_wav`)

Hand-computed expected values: every PCM width, left-justified 12-in-16, stereo order, float 32 and 64, extensible, chunk order, junk chunks, streaming-size data chunks; G.711 values (+8, -8, +-32256, +-32124, +264, +132); an IMA block worked by hand nibble by nibble (mono and stereo); an MS ADPCM mono and stereo block worked by hand with the standard coefficients, plus clamping; refusals (unknown tag, bad depth, bad channels, rate, bad GUID, bad block align, bad predictor index); budgets; every truncation of seven valid files and 21,000 random byte mutations (21,685 decodes in all) (no crash, no oversize output; recorded run: the decoder and test built standalone with ASan and UBSan, 22,642 checks, 0 failures; the full engine test was only compiled, not linked or run); the switch in `AudioClip::LoadWAV` (own and old paths agree on a 6-channel PCM file, stereo and mono downmix, ADPCM loads, errors come through); and a **differential run of IMA ADPCM against Python's `audioop`** at test time only (120 random mono, stereo and 3-channel files, exact equality; SKIPs when python3 or `audioop` is missing, `OKAY_REQUIRE_DIFFERENTIAL=1` makes the skip a failure; `audioop` is removed in Python 3.13, so on a newer Python this test skips).

## Replacement criteria (from the roadmap) and honest state

1. Real-file corpus: **partly done.** Files from an encoder we did not write are now in `tests/data/audio/wav_ffmpeg/` (test `wav_ffmpeg_diff`): our own synthetic signal encoded by ffmpeg 6.1.1 into MS ADPCM (mono and stereo, default, 256 and 2048 byte blocks), IMA ADPCM (mono, stereo), A-law, mu-law, unsigned 8-bit, 24-bit and float32, each compared with ffmpeg's own decode of the same file. MS ADPCM, A-law, mu-law, 8-bit, 24-bit and float match ffmpeg **bit for bit** (16-bit output). IMA ADPCM does **not**: most samples differ by a few steps (at most 29 of 32,768 in these files); our IMA matches Python's `audioop` exactly (above), so the two reference decoders disagree with each other in their arithmetic, and the test holds IMA to a bound of 32. Not yet: telephony-tool files, extensible multichannel files, files from a second encoder. ffmpeg is used as a program only (`scripts/gen_wav_ffmpeg_fixtures.py` records the commands); none of its code was read.
2. Windows and Linux platform tests: Linux only so far. The tests (`own_wav`, `own_wav_default_on`, `own_wav_default_off`, `wav_ffmpeg_diff`, `wav_playback`, `wav_seam_wiring`) run on GitHub in job "Build & test (Ubuntu)", now with our decoder as the default. Nothing ran on Windows (the Windows CI jobs do not include these tests).
3. Fuzz under sanitizers: a deterministic mutation run is in the test; a long fuzz run is not done.
4. No known bugs: none known. MS ADPCM was checked by hand, and is now also equal, sample for sample, to ffmpeg's decoder on files ffmpeg's encoder made (`wav_ffmpeg_diff`; three deliberate changes to the decoder, one adaptation-table entry, the predictor shift, the order of the two header samples, each make it fail).
5. Owner approval: **given, 2026-11-16, in chat: "Switch, but keep improving it".** Criteria 1 to 3 were not all fully met when it was given (one outside encoder, no Windows run, no long fuzz run); the owner approved the switch with those listed, and the work on them goes on.

## Not done

- Deleting the `SDL_LoadWAV` call itself (in `shared/WavPlayback.hpp`) and the old engine reader: due one release after the flip (step 5).
- `AudioStream` (streamed WAV playback) still uses the old header parser; only whole-file loads (`AudioClip::LoadWAV`) use the own decoder. ADPCM, A-law and mu-law streams are not supported there (a streamed A-law file plays as noise, as before). A streamed and a whole-file load of a PCM or float file give the same samples bit for bit (`audio_stream`); for that the whole-file mono downmix of 3 or more channels sums then divides, as the stream does.
- `RF64` / `BW64` (files over 4 GiB), `LIST` metadata, loop points (`smpl`) and cue points are not read. GSM, MP3-in-WAV and other tags are refused.
- Not in `OKAY_OWN_STACK` or the startup line (it is the default now; the log line above says which reader ran). Every CI job now runs the WAV tests with it on (the default). The old default loader played A-law and mu-law files as loud noise and 64-bit float as silence (Grok finding 19, issue #888); with the flip they play correctly.
- `AudioClip::SaveWAV` is unchanged (16-bit PCM).
