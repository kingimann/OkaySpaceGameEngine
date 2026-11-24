#pragma once
// Our own WAV decoder, written from the public format documents only:
//   - the RIFF / WAVE container and the "fmt " chunk layouts as Microsoft published them in the
//     multimedia registration documents (WAVEFORMAT, WAVEFORMATEX, WAVEFORMATEXTENSIBLE and its
//     sub-format GUIDs),
//   - ITU-T G.711 (A-law and mu-law companding),
//   - the Microsoft ADPCM and the IMA / DVI ADPCM block layouts as the public format descriptions
//     (the Microsoft RIFF registry notes and the IMA Digital Audio Focus and Technical Working Group
//     recommended practices) give them, with the 89-entry IMA step table from that description.
// No decoder source (SDL, libsndfile, dr_wav, ffmpeg, ...) was read, copied or followed.
//
// What it reads: PCM 8/16/24/32 bit (also in WAVE_FORMAT_EXTENSIBLE), IEEE float 32 and 64 bit,
// A-law, mu-law, Microsoft ADPCM (tag 0x0002) and IMA ADPCM (tag 0x0011), any channel count up to
// Limits::maxChannels. It is the only WAV reader: AudioClip::LoadWAV and the launcher (through
// okay/Audio/WavPlayback.hpp) use it; SDL_LoadWAV and the OKAY_OWN_WAV switch were deleted
// (docs/audio/own_wav_decoder.md).
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace okay {
namespace wav {

/// Budgets for hostile input. The defaults refuse anything that would need more than about 512 MiB.
struct Limits {
    std::size_t   maxInputBytes    = std::size_t(1) << 30;   ///< larger input is refused
    std::uint64_t maxOutputSamples = std::uint64_t(1) << 27; ///< total floats (frames * channels)
    int           maxChannels      = 32;
    std::uint32_t maxSampleRate    = 768000;
    int           maxChunks        = 4096;                   ///< chunks walked before giving up
};

enum class Encoding { Pcm, Float, ALaw, MuLaw, MsAdpcm, ImaAdpcm };

struct WavAudio {
    int sampleRate = 0;
    int channels = 0;
    Encoding encoding = Encoding::Pcm;
    int bitsPerSample = 0;                 ///< as the file states it
    std::vector<float> samples;            ///< interleaved, -1 to 1 (float files are not clamped, NaN becomes 0)
    std::vector<std::string> warnings;     ///< things that were cut or fixed, never silent
    std::size_t FrameCount() const { return channels > 0 ? samples.size() / std::size_t(channels) : 0; }
};

/// Decodes a whole in-memory WAV file. Never reads outside `data`, never allocates more than the
/// limits allow, never throws. Returns false and sets *error for anything it cannot decode.
/// A data chunk longer than the file (or 0xFFFFFFFF, as streaming writers leave it) is cut at the end
/// of the file with a warning; a half frame at the end is dropped with a warning.
bool DecodeWav(const unsigned char* data, std::size_t size, WavAudio& out,
               std::string* error = nullptr, const Limits* limits = nullptr);

/// Interleaved signed 16-bit samples (the launcher's playback format): rounds to the
/// nearest and clamps. Same channel count and rate as `a`.
std::vector<std::int16_t> ToS16(const WavAudio& a);

} // namespace wav
} // namespace okay
