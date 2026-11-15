# Our own WAV decoder (`okay::wav`)

Status: **side by side, off by default.** Part of `docs/own_code_roadmap.md` (the SDL2 row: `SDL_LoadWAV` in the launcher is the last WAV use of a library; the engine already had its own, narrower, WAV reader).

## How to try it

Full steps: `docs/try_it.md`, section 9. Short form: the whole-file path is tried with `examples\own_wav\own_wav_test.okayscene` (mu-law), the **streaming** path with `examples\own_wav\own_wav_stream_test.okayscene` (an IMA ADPCM file, `ima_tone.wav`, played with the Stream box on): without `OKAY_OWN_WAV=1` it is silent (the old streaming reader cannot read ADPCM), with it you hear a clean 440 Hz tone. `own_wav_sample` checks the committed file and that behaviour on Linux, and `own_wav_default` checks that `-DOKAY_OWN_WAV=ON` really makes the own decoder the default (it fails if the option stops reaching the engine); not heard on Windows.

## Why this one

It is small, it has a public specification, it is fully testable on Linux, and it is not crypto, networking or a model reader. It does two jobs: it is the own replacement for `SDL_LoadWAV` (which also reads MS and IMA ADPCM and A-law / mu-law), and it makes the engine's `AudioClip::LoadWAV` safe against hostile files (budgets) and wider (ADPCM, A-law, mu-law, 64-bit float, WAVE_FORMAT_EXTENSIBLE, odd bit depths).

## What it is

| File | What |
|---|---|
| `engine/include/okay/Audio/WavDecoder.hpp`, `engine/src/Audio/WavDecoder.cpp` | `okay::wav::DecodeWav` (bytes in, interleaved float out, plus warnings), `ToS16`, `Limits`, `OwnWavEnabled` |
| (same files) `okay::wav::WavStream` | The streaming decoder: opens a byte source (a file read on demand or memory), decodes block by block, seeks by block. `DecodeWav` and `WavStream` share one header reader and one set of block decoders, so they give the same samples |
| `engine/src/Audio/AudioClip.cpp` | The seam: `AudioClip::LoadWAV` calls the own decoder when `OwnWavEnabled()` is true; otherwise the old code runs unchanged |
| `engine/src/Audio/AudioStream.cpp` | The second seam: `AudioStream` (streamed playback) uses `WavStream` for RIFF, RF64 and BW64 files when `OwnWavEnabled()` is true; otherwise the old header parser runs unchanged |
| `tests/test_own_wav.cpp`, `tests/test_own_wav_stream.cpp` | Tests `own_wav` and `own_wav_stream` |
| `tests/test_own_wav_sample.cpp`, `tests/test_own_wav_default.cpp`, `examples/own_wav/ima_tone.wav`, `own_wav_stream_test.okayscene`, `scripts/gen_own_wav_samples.py` | The shipped IMA ADPCM sample and the scene that streams it (`own_wav_sample`: decodes to the tone, streamed == whole-file, old reader refuses it); `-DOKAY_OWN_WAV=ON` makes the own decoder the default (`own_wav_default`) |

## Turning it on

- CMake: `-DOKAY_OWN_WAV=ON` makes it the default for that build (default `OFF`).
- Run time: `OKAY_OWN_WAV=1` (or `0`) overrides the build default.
- Not part of `OKAY_OWN_STACK` yet (not added on purpose: see "Not done").

## What it reads

PCM 8 (unsigned), 16, 24, 32 bit and any bit depth from 1 to 32 stored left-justified in its byte container; IEEE float 32 and 64 (NaN and infinity become 0); A-law and mu-law (ITU-T G.711, 16-bit linear scale: A-law +-32256, mu-law +-32124); Microsoft ADPCM (tag 0x0002, one or two channels, the coefficient table from the fmt chunk or the standard seven); IMA / DVI ADPCM (tag 0x0011, 1 to 32 channels); `WAVE_FORMAT_EXTENSIBLE` (the standard sub-format GUID base is required; PCM, float, A-law, mu-law, both ADPCMs as sub-formats). `fmt ` and `data` chunks in either order, unknown chunks skipped, odd chunk padding honoured. Output is float in -1 to 1; `ToS16` rounds to nearest and clamps.

**RF64 / BW64** (files over 4 GiB, EBU TECH 3306): the file starts with `RF64` (or `BW64`) instead of `RIFF`, and a `ds64` chunk carries 64-bit sizes. Where a chunk size field says `0xFFFFFFFF`, the real size comes from `ds64` (its `dataSize` for the `data` chunk, its size table for any other chunk). A `data` chunk with `0xFFFFFFFF` and no `ds64` before it is refused. The RIFF size and the sample count in `ds64` are not trusted. A size larger than the file (2^63, 2^64-1, anything) is cut at the end of the file with a warning; positions are 64-bit and checked so nothing wraps. A `ds64` chunk in a plain `RIFF` file is ignored.

**Loop points and cue points** are read into the result: `WavAudio::sampler` (the `smpl` chunk: manufacturer, product, sample period, MIDI unity note and pitch fraction, SMPTE fields, and every loop with id, type, start, end, fraction, play count) and `WavAudio::cues` (the `cue ` chunk: id, position, chunk id, chunk start, block start, sample offset). They may come before or after the audio. A list that declares more entries than the chunk holds (or than `Limits::maxMarkers`, 65,536) is cut with a warning. **Playback does not use them**: no loop points are wired into `AudioStream` or the mixer (not trivial: the loop would have to be a sub-range with its own play count; left for a later step). `LIST`/`adtl` labels are still not read.

## Streaming (`wav::WavStream`, and `AudioStream` with the switch on)

`WavStream` reads the header once, then decodes on request: plain formats a window of at most 1024 frames (and at most 64 KB) at a time; ADPCM one block at a time (the read buffer is one block, at most 65,535 bytes, and the decoded block, at most about 131,000 floats, so under 1 MB for the largest legal block). Memory does not grow with the length of the file (a 7.5 million frame IMA file and a 2.5 billion frame RF64 file are tested). `Seek(frame)`: PCM, float, A-law and mu-law go to the exact byte; ADPCM goes to the block that holds the frame, decodes it from its start (the predictor and step in the block header make every block independent) and drops the frames before the target. `Read` returns whole frames and 0 at the end; a source that shrinks ends the stream without a crash; an ADPCM block with a bad predictor index stops the stream and `Failed()` says so, while the blocks before it still play. The IMA "step index above 88" warning is given once per file, not once per block.

Budgets for a stream: the channel, rate, chunk and marker limits and the input size apply as for the clip decoder; the decoded size of one block is checked against `maxOutputSamples` before anything is decoded; the total decoded size is not budgeted (it is never held; that is what streaming is for). `AudioStream` passes `maxInputBytes` of 2 GB (what its file readers already allow).

`AudioStream` with the switch on: more than two channels are averaged to mono as before; the seek, loop, ring and `decodeBudget` behaviour is `AudioStream`'s own and unchanged. With the switch off nothing changed: the golden test below records hashes of what the old path produced.

## Hostile input and budgets (`wav::Limits`)

Input over 1 GiB, more than 32 channels, a rate over 768,000 Hz or 0, more than 4,096 chunks, and a decoded size over 2^27 samples (512 MiB of float) are refused with a message, before anything is allocated. A data chunk longer than the file (or 0xFFFFFFFF, as streaming writers leave it) is cut at the end of the file, a half frame at the end is dropped, and an IMA step index over 88 is clamped, each with an entry in `warnings`. ADPCM block sizes must fit their headers, an MS ADPCM predictor index outside the table is an error, positions are 64-bit so a chunk size near 2^32 cannot wrap. Nothing reads outside the input; the output size is bounded by the input size times a constant (ADPCM expands at most 4 samples per byte).

## Specifications used (rule 19a)

Written from public format descriptions only: the Microsoft RIFF / WAVE format registration notes (WAVEFORMAT, WAVEFORMATEX, WAVEFORMATEXTENSIBLE, the sub-format GUID base, and the layouts of the `smpl` and `cue ` chunks), ITU-T G.711 (the companding laws), the Microsoft ADPCM block layout and adaptation table, the IMA Digital Audio Focus and Technical Working Group ADPCM description (the 89-entry step table and index adjust table), and EBU TECH 3306 (the RF64 container: the `RF64` / `BW64` id, the `ds64` chunk and its size table, the `0xFFFFFFFF` size rule). **No decoder source was read, copied or followed** (not SDL, libsndfile, dr_wav, ffmpeg or any other). The two ADPCM tables and the seven standard Microsoft coefficient pairs are standard constants from those descriptions, typed in from memory of the published tables and then checked against the independent decoder in Python's `audioop` (IMA, 120 random files) and by hand (MS ADPCM; see below).

## Tests (`ctest -R own_wav`)

Hand-computed expected values: every PCM width, left-justified 12-in-16, stereo order, float 32 and 64, extensible, chunk order, junk chunks, streaming-size data chunks; G.711 values (+8, -8, +-32256, +-32124, +264, +132); an IMA block worked by hand nibble by nibble (mono and stereo); an MS ADPCM mono and stereo block worked by hand with the standard coefficients, plus clamping; refusals (unknown tag, bad depth, bad channels, rate, bad GUID, bad block align, bad predictor index); budgets; every truncation of seven valid files and 21,000 random byte mutations (21,685 decodes in all) (no crash, no oversize output; recorded run: the decoder and test built standalone with ASan and UBSan, 22,642 checks, 0 failures; the full engine test was only compiled, not linked or run); the switch in `AudioClip::LoadWAV` (own and old paths agree on a 6-channel PCM file, stereo and mono downmix, ADPCM loads, errors come through); and a **differential run of IMA ADPCM against Python's `audioop`** at test time only (120 random mono, stereo and 3-channel files, exact equality; SKIPs when python3 or `audioop` is missing, `OKAY_REQUIRE_DIFFERENTIAL=1` makes the skip a failure; `audioop` is removed in Python 3.13, so on a newer Python this test skips).

## Streaming and RF64 tests (`ctest -R own_wav_stream`)

`tests/test_own_wav_stream.cpp`, 25,764 checks (standalone build with ASan and UBSan: 0 failures; see the mutation list below). It checks: IMA and MS ADPCM blocks worked by hand, including the step index at the top (88) and bottom (0) of the table, a second block reached by seeking, and a refused block; **streamed output equal to the whole-clip decode byte for byte** for 26 files (PCM 8, 12-in-16, 16, 24, 32 bit, float 32 and 64, A-law, mu-law, mono to 6 channels, a wide block align, IMA with 1 to 3 channels and partial last blocks, MS ADPCM mono and stereo with the standard and a custom coefficient table, an RF64 file) at read sizes 1, 3, 64, 1000, 4096 and 1,048,576 frames and at about 70 seeks per file (block edges, the end, past the end, random), each followed by a read; every truncation length of ten valid files (1,032 truncated files: both decoders give the same verdict, the same refusal text at open and the same samples); 6,000 random byte mutations (same verdict and samples); RF64 and BW64 (ds64 first, the `0xFFFFFFFF` rule, a table entry for a non-data chunk, a data chunk followed by `smpl`, no ds64, a short ds64, ds64 sizes of 2^63, 2^63-1, 2^64-1 and 2^32, a huge table length, a chunk hidden in the audio of a `2^64-1` data chunk), `smpl` and `cue ` (hand-given fields, before and after the data, declared counts of a billion, cut chunks, `maxMarkers`); chunk sizes past the end of the file; the channel, rate, chunk, input and block budgets and that the stream, unlike the clip decoder, plays a file over the total budget; a 1.07 billion frame and a 2.5 billion frame (5 GB RF64) file through a made-up byte source (seeks past 2^31, memory under 200 KB); a source that shrinks; `AudioStream` on the same 26 files (read in odd sizes, seek, three laps of loop equal to three copies, memory), a 7.5 million frame IMA stream, an RF64 file opened from disk, error messages; and **the off-by-default path**: golden hashes (FNV-1a over the float bits) of `AudioStream` output for 7 PCM and float files, recorded by building the test against the unchanged sources of commit 9a258154, must match with `OKAY_OWN_WAV=0`; the old reader must still refuse ADPCM and RF64; with the switch on, 8, 16, 24 bit and float files equal the old output exactly, and 32-bit and 3-channel files agree to 2e-7 (the old code rounds differently).

## Mutation checks (break the code, see red, restore)

Each was applied to the source, the two tests rebuilt and run, and the source restored (compared byte for byte afterwards). Counts are failed checks, `own_wav_stream` / `own_wav`.

| Break | Red |
|---|---|
| IMA step-index clamp `> 88 ? 88` changed to `> 89 ? 89` (the check the previous step did not re-run) | 14 / 88 |
| IMA lower clamp `< 0 ? 0` changed to `< -1 ? -1` | 7 / 7 |
| RF64 data size from ds64 halved | 15 / 0 |
| `smpl` loop end read from the start field | 6 / 0 |
| `cue ` sample offset read from the wrong field | 4 / 0 |
| ds64 size table lookup disabled | 1 / 0 |
| chunk past the end of the file no longer stops the walk (a hidden `smpl` in the audio is then parsed) | 2 / 0 |
| seek: block index off by one frame | 83 / 0 |
| seek: offset inside the block off by one | 903 / 0 |
| MS ADPCM last partial block: one frame too many | 978 / 0 |
| ADPCM block budget removed | 1 / 0 |
| IMA clamp warning given for every block, not once | 1 / 0 |
| `AudioStream` never uses the own decoder (`own = false`) | 107 / 0 |
| `AudioStream` always uses the own decoder (`own = true`: the off-by-default golden test) | 4 / 0 |

One mutation survived: removing `s.ended = true` when a PCM read returns nothing (the stream then asks the source again on the next `Read` and gets nothing again, so the observable behaviour is the same: an equivalent change, not a missing test).

## Replacement criteria (from the roadmap) and honest state

1. Real-file corpus: **partly done.** Files from an encoder we did not write are now in `tests/data/audio/wav_ffmpeg/` (test `wav_ffmpeg_diff`): our own synthetic signal encoded by ffmpeg 6.1.1 into MS ADPCM (mono and stereo, default, 256 and 2048 byte blocks), IMA ADPCM (mono, stereo), A-law, mu-law, unsigned 8-bit, 24-bit and float32, each compared with ffmpeg's own decode of the same file. MS ADPCM, A-law, mu-law, 8-bit, 24-bit and float match ffmpeg **bit for bit** (16-bit output). IMA ADPCM does **not**: most samples differ by a few steps (at most 29 of 32,768 in these files); our IMA matches Python's `audioop` exactly (above), so the two reference decoders disagree with each other in their arithmetic, and the test holds IMA to a bound of 32. Not yet: telephony-tool files, extensible multichannel files, files from a second encoder. ffmpeg is used as a program only (`scripts/gen_wav_ffmpeg_fixtures.py` records the commands); none of its code was read.
2. Windows and Linux platform tests: Linux only so far; nothing was run on Windows or on GitHub CI.
3. Fuzz under sanitizers: a deterministic mutation run is in the test; a long fuzz run is not done.
4. No known bugs: none known. MS ADPCM was checked by hand, and is now also equal, sample for sample, to ffmpeg's decoder on files ffmpeg's encoder made (`wav_ffmpeg_diff`; three deliberate changes to the decoder, one adaptation-table entry, the predictor shift, the order of the two header samples, each make it fail).
5. Owner approval: not asked.

## Not done

- `launcher/main.cpp` still calls `SDL_LoadWAV` (the launcher is off limits for this change); switching that call to `wav::DecodeWav` + `ToS16` and then deleting the SDL call is the next step.
- Loop points and cue points are read but not used by playback; `LIST` / `adtl` labels are not read. GSM, MP3-in-WAV and other tags are refused.
- Streaming and RF64 have only been run on Linux, only on files built by the tests (no real RF64 or BW64 file from a broadcast tool, no real file over 4 GiB on disk; the 5 GB case uses a made-up byte source).
- The old `AudioStream` WAV reader is not deleted (rule 17: one release after the default is flipped, with the owner's approval).
- Not added to `OKAY_OWN_STACK`.
- `AudioClip::SaveWAV` is unchanged (16-bit PCM).
