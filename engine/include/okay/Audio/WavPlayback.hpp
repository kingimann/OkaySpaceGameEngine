#pragma once
// The playback side of the WAV seam: a WAV file (or its bytes) turned into samples a sound device
// plays, with no SDL in the header. Written by us from the okay::wav decoder's own output
// (docs/audio/own_wav_decoder.md); no other project's code was read or followed.
//
// Device formats: 32-bit float files give native-endian float; every other encoding (PCM 8/16/24/32,
// A-law, mu-law, MS and IMA ADPCM) gives native-endian signed 16-bit. Channel count and rate are kept.
// The conversion is ours (okay::wav::ToS16 and a plain copy); no SDL_ConvertAudio.
//
// The launcher calls this through shared/WavPlayback.hpp, which is the only file allowed to call
// SDL_LoadWAV (it does so while the own decoder is off). tests/check_wav_seam_wiring.cmake enforces that.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace okay {
namespace wav {

enum class PlaybackFormat { S16, F32 };

struct PlaybackClip {
    PlaybackFormat format = PlaybackFormat::S16;
    int channels = 0;
    int sampleRate = 0;
    std::vector<std::uint8_t> bytes;   ///< interleaved, native-endian, never empty on success
};

/// Decodes WAV file bytes with okay::wav::DecodeWav and converts them for playback.
/// Returns false (and sets *error) on any damage, on an empty result, or when over the budgets.
bool DecodeForPlayback(const unsigned char* data, std::size_t size, PlaybackClip& out,
                       std::string* error = nullptr);

/// Reads the file at `path` (refusing more than Limits::maxInputBytes) and calls DecodeForPlayback.
bool LoadForPlayback(const std::string& path, PlaybackClip& out, std::string* error = nullptr);

} // namespace wav
} // namespace okay
