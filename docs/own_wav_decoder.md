# Our own WAV decoder (`okay::wav`)

Status: **side by side, off by default.** Part of `docs/own_code_roadmap.md` (the SDL2 row: `SDL_LoadWAV` in the launcher is the last WAV use of a library; the engine already had its own, narrower, WAV reader).

## Why this one

It is small, it has a public specification, it is fully testable on Linux, and it is not crypto, networking or a model reader. It does two jobs: it is the own replacement for `SDL_LoadWAV` (which also reads MS and IMA ADPCM and A-law / mu-law), and it makes the engine's `AudioClip::LoadWAV` safe against hostile files (budgets) and wider (ADPCM, A-law, mu-law, 64-bit float, WAVE_FORMAT_EXTENSIBLE, odd bit depths).

## What it is

| File | What |
|---|---|
| `engine/include/okay/Audio/WavDecoder.hpp`, `engine/src/Audio/WavDecoder.cpp` | `okay::wav::DecodeWav` (bytes in, interleaved float out, plus warnings), `ToS16`, `Limits`, `OwnWavEnabled` |
| `engine/src/Audio/AudioClip.cpp` | The seam: `AudioClip::LoadWAV` calls the own decoder when `OwnWavEnabled()` is true; otherwise the old code runs unchanged |
| `tests/test_own_wav.cpp` | Test `own_wav` |

## Turning it on

- CMake: `-DOKAY_OWN_WAV=ON` makes it the default for that build (default `OFF`).
- Run time: `OKAY_OWN_WAV=1` (or `0`) overrides the build default.
- Not part of `OKAY_OWN_STACK` yet (not added on purpose: see "Not done").

## What it reads

PCM 8 (unsigned), 16, 24, 32 bit and any bit depth from 1 to 32 stored left-justified in its byte container; IEEE float 32 and 64 (NaN and infinity become 0); A-law and mu-law (ITU-T G.711, 16-bit linear scale: A-law +-32256, mu-law +-32124); Microsoft ADPCM (tag 0x0002, one or two channels, the coefficient table from the fmt chunk or the standard seven); IMA / DVI ADPCM (tag 0x0011, 1 to 32 channels); `WAVE_FORMAT_EXTENSIBLE` (the standard sub-format GUID base is required; PCM, float, A-law, mu-law, both ADPCMs as sub-formats). `fmt ` and `data` chunks in either order, unknown chunks skipped, odd chunk padding honoured. Output is float in -1 to 1; `ToS16` rounds to nearest and clamps.

## Hostile input and budgets (`wav::Limits`)

Input over 1 GiB, more than 32 channels, a rate over 768,000 Hz or 0, more than 4,096 chunks, and a decoded size over 2^27 samples (512 MiB of float) are refused with a message, before anything is allocated. A data chunk longer than the file (or 0xFFFFFFFF, as streaming writers leave it) is cut at the end of the file, a half frame at the end is dropped, and an IMA step index over 88 is clamped, each with an entry in `warnings`. ADPCM block sizes must fit their headers, an MS ADPCM predictor index outside the table is an error, positions are 64-bit so a chunk size near 2^32 cannot wrap. Nothing reads outside the input; the output size is bounded by the input size times a constant (ADPCM expands at most 4 samples per byte).

## Specifications used (rule 19a)

Written from public format descriptions only: the Microsoft RIFF / WAVE format registration notes (WAVEFORMAT, WAVEFORMATEX, WAVEFORMATEXTENSIBLE, the sub-format GUID base), ITU-T G.711 (the companding laws), the Microsoft ADPCM block layout and adaptation table, and the IMA Digital Audio Focus and Technical Working Group ADPCM description (the 89-entry step table and index adjust table). **No decoder source was read, copied or followed** (not SDL, libsndfile, dr_wav, ffmpeg or any other). The two ADPCM tables and the seven standard Microsoft coefficient pairs are standard constants from those descriptions, typed in from memory of the published tables and then checked against the independent decoder in Python's `audioop` (IMA, 120 random files) and by hand (MS ADPCM; see below).

## Tests (`ctest -R own_wav`)

Hand-computed expected values: every PCM width, left-justified 12-in-16, stereo order, float 32 and 64, extensible, chunk order, junk chunks, streaming-size data chunks; G.711 values (+8, -8, +-32256, +-32124, +264, +132); an IMA block worked by hand nibble by nibble (mono and stereo); an MS ADPCM mono and stereo block worked by hand with the standard coefficients, plus clamping; refusals (unknown tag, bad depth, bad channels, rate, bad GUID, bad block align, bad predictor index); budgets; every truncation of seven valid files and 21,000 random byte mutations (21,685 decodes in all) (no crash, no oversize output; recorded run: the decoder and test built standalone with ASan and UBSan, 22,642 checks, 0 failures; the full engine test was only compiled, not linked or run); the switch in `AudioClip::LoadWAV` (own and old paths agree on a 6-channel PCM file, stereo and mono downmix, ADPCM loads, errors come through); and a **differential run of IMA ADPCM against Python's `audioop`** at test time only (120 random mono, stereo and 3-channel files, exact equality; SKIPs when python3 or `audioop` is missing, `OKAY_REQUIRE_DIFFERENTIAL=1` makes the skip a failure; `audioop` is removed in Python 3.13, so on a newer Python this test skips).

## Replacement criteria (from the roadmap) and honest state

1. Real-file corpus: **partly done.** Files from an encoder we did not write are now in `tests/data/audio/wav_ffmpeg/` (test `wav_ffmpeg_diff`): our own synthetic signal encoded by ffmpeg 6.1.1 into MS ADPCM (mono and stereo, default, 256 and 2048 byte blocks), IMA ADPCM (mono, stereo), A-law, mu-law, unsigned 8-bit, 24-bit and float32, each compared with ffmpeg's own decode of the same file. MS ADPCM, A-law, mu-law, 8-bit, 24-bit and float match ffmpeg **bit for bit** (16-bit output). IMA ADPCM does **not**: most samples differ by a few steps (at most 29 of 32,768 in these files); our IMA matches Python's `audioop` exactly (above), so the two reference decoders disagree with each other in their arithmetic, and the test holds IMA to a bound of 32. Not yet: telephony-tool files, extensible multichannel files, files from a second encoder. ffmpeg is used as a program only (`scripts/gen_wav_ffmpeg_fixtures.py` records the commands); none of its code was read.
2. Windows and Linux platform tests: Linux only so far; nothing was run on Windows or on GitHub CI.
3. Fuzz under sanitizers: a deterministic mutation run is in the test; a long fuzz run is not done.
4. No known bugs: none known. MS ADPCM was checked by hand, and is now also equal, sample for sample, to ffmpeg's decoder on files ffmpeg's encoder made (`wav_ffmpeg_diff`; three deliberate changes to the decoder, one adaptation-table entry, the predictor shift, the order of the two header samples, each make it fail).
5. Owner approval: not asked.

## Not done

- `launcher/main.cpp` still calls `SDL_LoadWAV` (the launcher is off limits for this change); switching that call to `wav::DecodeWav` + `ToS16` and then deleting the SDL call is the next step.
- `AudioStream` (streamed WAV playback) still uses the old header parser; only whole-file loads (`AudioClip::LoadWAV`) use the own decoder. ADPCM streams are not supported there.
- `RF64` / `BW64` (files over 4 GiB), `LIST` metadata, loop points (`smpl`) and cue points are not read. GSM, MP3-in-WAV and other tags are refused.
- Not added to `OKAY_OWN_STACK`.
- `AudioClip::SaveWAV` is unchanged (16-bit PCM).
