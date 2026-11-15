#pragma once
// Our own WAV decoder, written from the public format documents only:
//   - the RIFF / WAVE container and the "fmt " chunk layouts as Microsoft published them in the
//     multimedia registration documents (WAVEFORMAT, WAVEFORMATEX, WAVEFORMATEXTENSIBLE and its
//     sub-format GUIDs),
//   - ITU-T G.711 (A-law and mu-law companding),
//   - the Microsoft ADPCM and the IMA / DVI ADPCM block layouts as the public format descriptions
//     (the Microsoft RIFF registry notes and the IMA Digital Audio Focus and Technical Working Group
//     recommended practices) give them, with the 89-entry IMA step table from that description.
//   - EBU TECH 3306 (RF64) for the "ds64" chunk, and the public RIFF notes for the "smpl" and "cue " chunks.
// No decoder source (SDL, libsndfile, dr_wav, ffmpeg, ...) was read, copied or followed.
//
// What it reads: PCM 8/16/24/32 bit (also in WAVE_FORMAT_EXTENSIBLE), IEEE float 32 and 64 bit,
// A-law, mu-law, Microsoft ADPCM (tag 0x0002) and IMA ADPCM (tag 0x0011), any channel count up to
// Limits::maxChannels; RIFF and RF64 / BW64 containers (EBU TECH 3306: the "ds64" chunk carries 64-bit sizes);
// the "smpl" loop points and the "cue " points are read into the result. It also streams: WavStream decodes
// block by block in bounded memory and can seek (used by AudioStream). It is an OPT-IN replacement for SDL_LoadWAV and the older, narrower
// AudioClip::LoadWAV path: see OwnWavEnabled() and docs/own_wav_decoder.md.
#include "okay/Audio/AudioByteSource.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
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
    int           maxMarkers       = 65536;                  ///< smpl loops and cue points kept (the rest is dropped with a warning)
};

enum class Encoding { Pcm, Float, ALaw, MuLaw, MsAdpcm, ImaAdpcm };

/// One loop of the "smpl" chunk (all fields as stored; start and end are sample frames, end inclusive).
struct WavLoop {
    std::uint32_t id = 0, type = 0;        ///< type: 0 forward, 1 ping-pong, 2 backward, others as stored
    std::uint32_t start = 0, end = 0, fraction = 0, playCount = 0;   ///< playCount 0 = endless
};
/// The "smpl" (sampler) chunk. `present` is false when the file has none.
struct WavSampler {
    bool present = false;
    std::uint32_t manufacturer = 0, product = 0, samplePeriodNs = 0;
    std::uint32_t midiUnityNote = 0, midiPitchFraction = 0, smpteFormat = 0, smpteOffset = 0;
    std::vector<WavLoop> loops;
};
/// One point of the "cue " chunk (all fields as stored).
struct WavCue {
    std::uint32_t id = 0, position = 0;
    char dataChunk[4] = {'d', 'a', 't', 'a'};   ///< the chunk the point refers to ("data" or "slnt")
    std::uint32_t chunkStart = 0, blockStart = 0, sampleOffset = 0;   ///< sampleOffset is the frame inside the data
};

/// What the header says, shared by the whole-file result and the stream.
struct WavMeta {
    int sampleRate = 0;
    int channels = 0;
    Encoding encoding = Encoding::Pcm;
    int bitsPerSample = 0;                 ///< as the file states it
    bool rf64 = false;                     ///< RF64 / BW64 container (64-bit sizes from ds64)
    WavSampler sampler;                    ///< loop points ("smpl"), read only: playback does not use them
    std::vector<WavCue> cues;              ///< cue points ("cue "), read only
    std::vector<std::string> warnings;     ///< things that were cut or fixed, never silent
};

struct WavAudio : WavMeta {
    std::vector<float> samples;            ///< interleaved, -1 to 1 (float files are not clamped, NaN becomes 0)
    std::size_t FrameCount() const { return channels > 0 ? samples.size() / std::size_t(channels) : 0; }
};

/// Decodes a whole in-memory WAV file. Never reads outside `data`, never allocates more than the
/// limits allow, never throws. Returns false and sets *error for anything it cannot decode.
/// A data chunk longer than the file (or 0xFFFFFFFF, as streaming writers leave it) is cut at the end
/// of the file with a warning; a half frame at the end is dropped with a warning.
bool DecodeWav(const unsigned char* data, std::size_t size, WavAudio& out,
               std::string* error = nullptr, const Limits* limits = nullptr);

/// Streamed decode: the same formats and the same samples as DecodeWav (byte for byte), one block at a time.
/// Memory is bounded: one read window of at most about 64 KB plus, for ADPCM, one decoded block (under 1 MB
/// even for the largest legal block). Seeking is by block: an ADPCM block is decoded from its start and the
/// frames before the target are dropped; PCM seeks to the exact byte. Not thread safe: one thread per stream.
/// Budgets (Limits): channels, rate, chunk count, marker count, input size and the decoded size of ONE block are
/// enforced; the total decoded size is not (it is the point of streaming that it is never held).
class WavStream {
public:
    WavStream();
    ~WavStream();
    WavStream(const WavStream&) = delete;
    WavStream& operator=(const WavStream&) = delete;

    /// Reads and checks the header. False (with *error) for anything DecodeWav would refuse before decoding.
    bool Open(std::shared_ptr<AudioByteSource> src, std::string* error = nullptr, const Limits* limits = nullptr);
    bool IsOpen() const;
    const WavMeta& Info() const;                 ///< warnings may grow while decoding (each kind once)
    std::uint64_t TotalFrames() const;
    std::uint64_t Position() const;              ///< next frame Read will return
    /// Up to `maxFrames` interleaved frames into `dst` (needs maxFrames * channels floats). Returns the frames
    /// written: 0 at the end, or after an error (see Failed()). A source that shrinks ends the stream.
    std::size_t Read(float* dst, std::size_t maxFrames);
    /// Moves to `frame` (clamped to TotalFrames()). False if not open or the stream has failed.
    bool Seek(std::uint64_t frame);
    bool Failed() const;
    const std::string& Error() const;
    std::size_t MemoryBytes() const;

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

/// Interleaved signed 16-bit samples (what SDL_LoadWAV returns for a 16-bit result): rounds to the
/// nearest and clamps. Same channel count and rate as `a`.
std::vector<std::int16_t> ToS16(const WavAudio& a);

/// True when the own decoder is the one AudioClip::LoadWAV should use: the build default
/// (CMake option OKAY_OWN_WAV, off) overridden by the environment variable OKAY_OWN_WAV=0 or 1.
bool OwnWavEnabled();

} // namespace wav
} // namespace okay
