---
id: 9b
group: appendix
---
## WAV decoder speed and memory (developer bench, not part of the Windows release)

Compares our own WAV decoder (`okay::wav`) with `SDL_LoadWAV` on the same files: time and peak memory of the decode alone
and of the launcher's whole voice-clip load to the format it plays. It is a program in the test build, not something in
`OkaySpace.exe`, so it cannot be tried from the release. Linux (or any build with tests on and SDL2 found; the memory
columns need glibc, elsewhere they print `-`):

1. Build it: `cmake -B build -DOKAY_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release`, then `ninja -C build bench_wav_decoder`.
2. Run it: `build/tests/bench_wav_decoder` (about 30 seconds). With no argument it reads the repository's WAV files and
   writes a generated set of 13 files (about 260 MB, 16-bit stereo from 1 s to 10 min, 8/24/32-bit, float32/64, A-law,
   mu-law, MS and IMA ADPCM, a 6-channel extensible file) to `<temp>/okay_wav_bench` the first time; `--gen DIR` puts
   them elsewhere. Or give it your own `.wav` files or folders.
3. You should see a table, one row per file: milliseconds and peak MiB for our decoder and SDL, and the ratio (below 1
   means ours is faster or smaller), first for the decode alone ("raw"), then for the load to the playback format
   ("final"). Expect every "final" ratio below 1, and SDL to print `fail` on the float64 file (it does not read it).
4. `--digest` prints a hash of our output per file; run it before and after a change to the decoder: the lines must
   match.

The measured table is in [own_wav_decoder.md](own_wav_decoder.md), "Speed and memory against SDL".
