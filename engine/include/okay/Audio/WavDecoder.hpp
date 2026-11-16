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
// Limits::maxChannels. It is an OPT-IN replacement for SDL_LoadWAV and the older, narrower
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

/// Interleaved signed 16-bit samples (what SDL_LoadWAV returns for a 16-bit result): rounds to the
/// nearest and clamps. Same channel count and rate as `a`.
std::vector<std::int16_t> ToS16(const WavAudio& a);

/// True when the own decoder is the one AudioClip::LoadWAV (and shared/WavPlayback.hpp) should use: the build
/// default (CMake option OKAY_OWN_WAV, ON since 2026-11-16) overridden by the environment variable OKAY_OWN_WAV=0
/// (the old path, kept for one release) or 1. Anything else in the variable is ignored.
bool OwnWavEnabled();

/// The one line a program logs, once per run, to say which WAV reader is in use: the engine (AudioClip::LoadWAV and
/// streamed WAV) and the launcher (voice clips) print the same text.
const char* ReaderLogLine(bool own);

/// Limits for WavStream when none are given: as Limits(), but with no input or output size budget (a stream never
/// holds the whole file or its whole output; AudioStream refuses files over 2 GB itself).
Limits StreamLimits();

/// Our decoder, a piece at a time: for AudioStream (streamed music and long sounds). Open reads the header once with
/// the same chunk walk and checks as DecodeWav, then Read decodes any number of frames from any position, using the
/// same per-format code as DecodeWav on the bytes it reads. A streamed decode gives exactly DecodeWav's samples, bit for
/// bit, whatever the piece sizes and seeks (test own_wav_stream): PCM, float, A-law and mu-law are read frame by frame
/// at frame * block align; MS and IMA ADPCM a block at a time (a seek decodes from the start of its block and skips to
/// the frame). Only one read window (up to 16 KiB, or one ADPCM block) and, for ADPCM, one decoded block are held.
///
/// Damage: a file DecodeWav refuses is refused by Open with the same message, except that the input and output size
/// budgets are lifted (StreamLimits). An MS ADPCM block with a bad predictor index (DecodeWav: an error after the
/// samples before it) ends the stream at the start of that block; Failed() is then true. A source that returns fewer
/// bytes than its Size() promised (a file cut while it plays) ends the stream after the last whole frame or block read.
/// Never reads outside what the source gives, never throws.
class WavStream {
public:
    WavStream();
    ~WavStream();
    WavStream(const WavStream&) = delete;
    WavStream& operator=(const WavStream&) = delete;

    /// Reads and checks the header. False (with *error) for anything DecodeWav would refuse.
    bool Open(std::shared_ptr<AudioByteSource> src, std::string* error = nullptr, const Limits* limits = nullptr);
    bool IsOpen() const;
    int SampleRate() const;
    int Channels() const;                       ///< the file's channels (every one is kept: interleaved as DecodeWav)
    Encoding GetEncoding() const;
    int BitsPerSample() const;
    std::uint64_t TotalFrames() const;          ///< what DecodeWav's FrameCount() gives for the file
    const std::vector<std::string>& Warnings() const;   ///< the header's warnings (as DecodeWav's, in the same order)

    /// Up to `maxFrames` frames (Channels() floats each) from Position() into `dst`; fewer only at the end. 0 at the end.
    std::size_t Read(float* dst, std::size_t maxFrames);
    /// Moves to `frame` (clamped to TotalFrames()). The next Read starts there.
    bool Seek(std::uint64_t frame);
    std::uint64_t Position() const;
    /// True once damage found while reading (a bad MS ADPCM block, a short read) ended the stream early.
    bool Failed() const;
    const std::string& Error() const;
    /// Bytes held: the read window, one decoded ADPCM block, the header plan.
    std::size_t MemoryBytes() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_;
};

} // namespace wav
} // namespace okay
