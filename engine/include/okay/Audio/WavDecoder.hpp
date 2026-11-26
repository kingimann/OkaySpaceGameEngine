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
// okay/Audio/WavPlayback.hpp) use DecodeWav, streamed playback (AudioStream) uses WavStream; SDL_LoadWAV,
// the OKAY_OWN_WAV switch and the older streaming header parser were deleted (docs/audio/own_wav_decoder.md).
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace okay {

class AudioByteSource;

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

/// Reads a WAV file a piece at a time from a byte source (a file read on demand, or memory), for long
/// sounds that are streamed instead of loaded whole (AudioStream). It uses DecodeWav's own chunk walk,
/// format checks and per-encoding decode, so its samples equal DecodeWav's bit for bit, with every
/// channel of the file interleaved. It holds one read window (64 KiB) or one ADPCM block and its decoded
/// frames, never the file. ADPCM decodes a block at a time, so a seek decodes the block holding the
/// frame and skips to it. The output budget (Limits::maxOutputSamples) and the input size budget do not
/// apply here (nothing is held whole); the channel, rate and chunk-count limits do.
/// When the source ends early or an ADPCM block is damaged (a Microsoft ADPCM predictor index outside
/// the table, which makes DecodeWav refuse the whole file), Read stops there and Error() says why;
/// a Seek clears that and tries again from the new place.
class WavStream {
public:
    WavStream();
    ~WavStream();
    WavStream(WavStream&&) noexcept;
    WavStream& operator=(WavStream&&) noexcept;
    WavStream(const WavStream&) = delete;
    WavStream& operator=(const WavStream&) = delete;

    /// Reads the header. False (with *error set, the same messages as DecodeWav) when it cannot be streamed.
    bool Open(std::shared_ptr<AudioByteSource> src, std::string* error = nullptr, const Limits* limits = nullptr);
    bool IsOpen() const;
    int SampleRate() const;
    int Channels() const;                 ///< as the file states it (1 to Limits::maxChannels)
    Encoding GetEncoding() const;
    int BitsPerSample() const;
    std::uint64_t TotalFrames() const;    ///< exact, for every encoding
    /// Up to `maxFrames` frames (Channels() floats each) into `dst`; fewer only at the end or when stopped.
    std::size_t Read(float* dst, std::size_t maxFrames);
    /// Moves to `frame` (clamped to TotalFrames()).
    bool Seek(std::uint64_t frame);
    std::uint64_t Position() const;
    /// Why Read stopped before the end, or empty.
    const std::string& Error() const;
    /// The same notes DecodeWav gives (cut chunks, ignored fields); a clamped IMA step index is noted once.
    const std::vector<std::string>& Warnings() const;
    std::size_t MemoryBytes() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_;
};

/// The IMA / DVI ADPCM tables from the IMA recommended practice (the public format description): the 89-entry step
/// table and the step-index change for each 3-bit code magnitude. The engine's one copy: the WAV reader and the
/// launcher's voice chat (launcher/VoiceChat.hpp) both read these.
const int* ImaAdpcmSteps();         // 89 entries, 7 .. 32767
const int* ImaAdpcmIndexAdjust();   // 8 entries, indexed by (code & 7)

} // namespace wav
} // namespace okay
