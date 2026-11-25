# Our own WAV decoder (`okay::wav`)

> **What this is:** Status: **the only WAV reader (2026-11-23), streamed WAV too (2026-11-25).** `SDL_LoadWAV` / `SDL_FreeWAV`, the engine's older, narrower `AudioClip::LoadWAV` path, the `OKAY_OWN_WAV` switch and the older streaming header parser (`ParseWav`, `ConvertWavFrames`, `WavInfo`) were deleted. Part of `docs/status/own_code_roadmap.md`.
>
> **Status:** Reference page for this feature. Kept as written; not extended (rule 5).
>
> **Last checked:** 2026-11-20 (moved into `audio/`, paths and links checked; the text itself was not re-checked against the code).

Status: **the only WAV reader (2026-11-23), streamed WAV too (2026-11-25).** `SDL_LoadWAV` / `SDL_FreeWAV`, the engine's older, narrower `AudioClip::LoadWAV` path, the `OKAY_OWN_WAV` switch and the older streaming header parser (`ParseWav`, `ConvertWavFrames`, `WavInfo`) were deleted. Part of `docs/status/own_code_roadmap.md`.

## Why this one

It is small, it has a public specification, it is fully testable on Linux, and it is not crypto, networking or a model reader. It does two jobs: it is the own replacement for `SDL_LoadWAV` (which also reads MS and IMA ADPCM and A-law / mu-law), and it makes the engine's `AudioClip::LoadWAV` safe against hostile files (budgets) and wider (ADPCM, A-law, mu-law, 64-bit float, WAVE_FORMAT_EXTENSIBLE, odd bit depths).

## What it is

| File | What |
|---|---|
| `engine/include/okay/Audio/WavDecoder.hpp`, `engine/src/Audio/WavDecoder.cpp` | `okay::wav::DecodeWav` (bytes in, interleaved float out, plus warnings), `ToS16`, `Limits` |
| `engine/src/Audio/AudioClip.cpp` | `AudioClip::LoadWAV` always calls the own decoder (the older path and `OwnWavEnabled()` were deleted) |
| `engine/src/Audio/AudioStream.cpp` | `AudioStream` (streamed WAV: Music player, streamed Audio Source) reads through `okay::wav::WavStream` |
| `tests/test_own_wav.cpp` | Test `own_wav` |

## Switch

None. It was behind `OKAY_OWN_WAV` (CMake option, run-time variable, Settings > Experimental item) until 2026-11-23; that switch and the code it chose between were deleted.

## What it reads

PCM 8 (unsigned), 16, 24, 32 bit and any bit depth from 1 to 32 stored left-justified in its byte container; IEEE float 32 and 64 (NaN and infinity become 0); A-law and mu-law (ITU-T G.711, 16-bit linear scale: A-law +-32256, mu-law +-32124); Microsoft ADPCM (tag 0x0002, one or two channels, the coefficient table from the fmt chunk or the standard seven); IMA / DVI ADPCM (tag 0x0011, 1 to 32 channels); `WAVE_FORMAT_EXTENSIBLE` (the standard sub-format GUID base is required; PCM, float, A-law, mu-law, both ADPCMs as sub-formats). `fmt ` and `data` chunks in either order, unknown chunks skipped, odd chunk padding honoured. Output is float in -1 to 1; `ToS16` rounds to nearest and clamps.

## Hostile input and budgets (`wav::Limits`)

Input over 1 GiB, more than 32 channels, a rate over 768,000 Hz or 0, more than 4,096 chunks, and a decoded size over 2^27 samples (512 MiB of float) are refused with a message, before anything is allocated. A data chunk longer than the file (or 0xFFFFFFFF, as streaming writers leave it) is cut at the end of the file, a half frame at the end is dropped, and an IMA step index over 88 is clamped, each with an entry in `warnings`. ADPCM block sizes must fit their headers, an MS ADPCM predictor index outside the table is an error, positions are 64-bit so a chunk size near 2^32 cannot wrap. Nothing reads outside the input; the output size is bounded by the input size times a constant (ADPCM expands at most 4 samples per byte).

## Streaming (`okay::wav::WavStream`, used by `AudioStream`)

`WavStream` reads a WAV file from an `AudioByteSource` (a file read on demand, or memory) a piece at a time. It uses `DecodeWav`'s own chunk walk (`ReadLayout`, one template over memory or a byte source), format checks (`CheckFormat`) and per-encoding decode (`DecodeFlat`, `DecodeBlock`), so a streamed file gives exactly `DecodeWav`'s samples, bit for bit, for every encoding. Flat formats read a 64 KiB window at a time; ADPCM reads and decodes one block at a time (at most 64 KiB of block and its frames, about 0.5 MB for the worst legal mono MS ADPCM block). A seek goes to the block holding the frame, decodes it and skips in; a loop is a seek to 0 (`AudioStream`). The output and input-size budgets do not apply (nothing is held whole); the channel, rate and chunk limits do. If the source ends early or an MS ADPCM block has a predictor index outside the table (which makes `DecodeWav` refuse the whole file), the stream stops there with `Error()` set. More than two channels are averaged to mono by `AudioStream`'s wrapper through the same `DownmixToMono` as `AudioClip::LoadWAV`.

## The playback seam (launcher voice clips)

The launcher's only WAV load (`VoiceClipFor`, voice clips) goes through `okay::wav::LoadWavForSdl` in `shared/WavPlayback.hpp`. It calls `okay::wav::LoadForPlayback` (`engine/include/okay/Audio/WavPlayback.hpp`, no SDL in the header): `DecodeWav`, then our own conversion to the device format the call site plays: 32-bit float stays native float, every other encoding (PCM 8/24/32, A-law, mu-law, ADPCM) becomes native 16-bit via `ToS16`; channels and rate are kept. No `SDL_ConvertAudio`. PCM16 mono and stereo come out byte for byte the same as `SDL_LoadWAV` did (checked side by side while both existed; now checked against the file's data bytes); 8-bit and 32-bit PCM arrive as 16-bit instead of U8/S32 (the call site converted them to 16-bit anyway). Failures (truncated, zero length, bad header, over budget, missing file) return false with a message and no samples. Written from the `okay::wav` output and the call site's needs only. `tests/check_wav_seam_wiring.cmake` (`ctest -R wav_seam_wiring`) fails if `SDL_LoadWAV`, `SDL_FreeWAV`, `OwnWavEnabled` or `OKAY_OWN_WAV` appears in code in launcher/, shared/, editor/, player/, engine/, tools/, okayui/, platform/ or the top CMakeLists.txt. Tests: `ctest -R wav_playback` (own path, hostile and zero-length input, and with SDL2 present the launcher seam against the file's bytes). The test WAVs are built in memory (no WAV fixtures are checked in).

## Specifications used (rule 19a)

Written from public format descriptions only: the Microsoft RIFF / WAVE format registration notes (WAVEFORMAT, WAVEFORMATEX, WAVEFORMATEXTENSIBLE, the sub-format GUID base), ITU-T G.711 (the companding laws), the Microsoft ADPCM block layout and adaptation table, and the IMA Digital Audio Focus and Technical Working Group ADPCM description (the 89-entry step table and index adjust table). **No decoder source was read, copied or followed** (not SDL, libsndfile, dr_wav, ffmpeg or any other). The two ADPCM tables and the seven standard Microsoft coefficient pairs are standard constants from those descriptions, typed in from memory of the published tables and then checked against the independent decoder in Python's `audioop` (IMA, 120 random files) and by hand (MS ADPCM; see below).

## Tests (`ctest -R own_wav`)

Streaming (`TestStreamMatchesWhole`, `TestStreamHostile`): 22 files built in the test (IMA and MS ADPCM mono, stereo and 3 channels with a short last block, A-law, mu-law, float 32 and 64, PCM 8, 12-in-16, 24 and 32, extensible PCM, float, IMA and A-law, odd-sized chunks before `fmt ` and `data` in either order) streamed through `AudioStream` and compared with `DecodeWav` sample for sample: whole reads in pieces of 1, 333 and 4096 frames, seeks at ADPCM block edges and random places, reading on after a seek, and a loop twice round. Then every truncation and 400 random mutations of seven small files: no crash, never more frames than `TotalFrames()`, equal to `DecodeWav` whenever it accepts the bytes, and never a refusal of a file `DecodeWav` accepts. `wav_seam_wiring` also fails if `ParseWav`, `ConvertWavFrames` or `WavInfo` comes back.


Hand-computed expected values: every PCM width, left-justified 12-in-16, stereo order, float 32 and 64, extensible, chunk order, junk chunks, streaming-size data chunks; G.711 values (+8, -8, +-32256, +-32124, +264, +132); an IMA block worked by hand nibble by nibble (mono and stereo); an MS ADPCM mono and stereo block worked by hand with the standard coefficients, plus clamping; refusals (unknown tag, bad depth, bad channels, rate, bad GUID, bad block align, bad predictor index); budgets; every truncation of seven valid files and 21,000 random byte mutations (21,685 decodes in all) (no crash, no oversize output; recorded run: the decoder and test built standalone with ASan and UBSan, 22,642 checks, 0 failures; the full engine test was only compiled, not linked or run); `AudioClip::LoadWAV` (a 6-channel PCM file gives the fixed mono averages the old and own paths both gave when they were compared, stereo and mono downmix, ADPCM loads, errors come through, a leftover `OKAY_OWN_WAV=0` changes nothing); and a **differential run of IMA ADPCM against Python's `audioop`** at test time only (120 random mono, stereo and 3-channel files, exact equality; SKIPs when python3 or `audioop` is missing, `OKAY_REQUIRE_DIFFERENTIAL=1` makes the skip a failure; `audioop` is removed in Python 3.13, so on a newer Python this test skips).

## Replacement criteria (from the roadmap) and honest state

1. Real-file corpus: **partly done.** Files from an encoder we did not write are now in `tests/data/audio/wav_ffmpeg/` (test `wav_ffmpeg_diff`): our own synthetic signal encoded by ffmpeg 6.1.1 into MS ADPCM (mono and stereo, default, 256 and 2048 byte blocks), IMA ADPCM (mono, stereo), A-law, mu-law, unsigned 8-bit, 24-bit and float32, each compared with ffmpeg's own decode of the same file. MS ADPCM, A-law, mu-law, 8-bit, 24-bit and float match ffmpeg **bit for bit** (16-bit output). IMA ADPCM does **not**: most samples differ by a few steps (at most 29 of 32,768 in these files); our IMA matches Python's `audioop` exactly (above), so the two reference decoders disagree with each other in their arithmetic, and the test holds IMA to a bound of 32. Not yet: telephony-tool files, extensible multichannel files, files from a second encoder. ffmpeg is used as a program only (`scripts/gen_wav_ffmpeg_fixtures.py` records the commands); none of its code was read.
2. Windows and Linux platform tests: Linux only so far. The tests (`own_wav`, `wav_ffmpeg_diff`, `wav_playback`, `wav_seam_wiring`) run on GitHub in job "Build & test (Ubuntu)". Nothing ran on Windows (the Windows CI jobs do not include these tests).
3. Fuzz under sanitizers: a deterministic mutation run is in the test; a long fuzz run is not done.
4. No known bugs: none known. MS ADPCM was checked by hand, and is now also equal, sample for sample, to ffmpeg's decoder on files ffmpeg's encoder made (`wav_ffmpeg_diff`; three deliberate changes to the decoder, one adaptation-table entry, the predictor shift, the order of the two header samples, each make it fail).
5. Owner approval: not asked.

## Not done

- `RF64` / `BW64` (files over 4 GiB), `LIST` metadata, loop points (`smpl`) and cue points are not read. GSM, MP3-in-WAV and other tags are refused.
- Not added to `OKAY_OWN_STACK`, so the startup line does not show it and no CI job runs with it on (Step 0.3 of `docs/status/own_flip_proposal.md` adds it). The default loader it would replace plays A-law and mu-law files as loud noise and 64-bit float as silence (Grok finding 19, issue #888).
- `AudioClip::SaveWAV` is unchanged (16-bit PCM).
