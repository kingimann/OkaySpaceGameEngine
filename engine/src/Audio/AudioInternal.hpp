// Shared by AudioClip.cpp, AudioDecode.cpp and AudioStream.cpp: the WAV header
// and sample conversion (so whole-file and streamed WAV decode are the same
// code) and the PcmDecoder interface the streaming decoders implement.
#pragma once
#include "okay/Audio/AudioByteSource.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace okay {
namespace audio_detail {

struct WavInfo {
    std::uint16_t format = 1, channels = 1, bits = 16;
    std::uint32_t rate = 44100;
    std::uint64_t dataOffset = 0, dataLen = 0;
    int bytesPerSample = 2;
    std::size_t BlockBytes() const { return static_cast<std::size_t>(bytesPerSample) * channels; }
    std::uint64_t Frames() const { return BlockBytes() ? dataLen / BlockBytes() : 0; }
};

/// Reads the RIFF header and chunk list. Returns null on success or a message.
const char* ParseWav(AudioByteSource& src, WavInfo& out);

/// Channels a decoded clip keeps: 1 or 2 stay as they are, more than two are
/// averaged to mono; `downmix` forces mono.
inline int KeptChannels(int fileChannels, bool downmix) { return (downmix || fileChannels > 2) ? 1 : (fileChannels < 1 ? 1 : fileChannels); }

/// `frames` frames of `raw` (WAV sample bytes) to floats, `outCh` interleaved
/// channels each (1 averages every file channel).
void ConvertWavFrames(const WavInfo& w, const unsigned char* raw, std::size_t frames, float* out, int outCh);

/// Averages `in` (interleaved, `channels` per frame) to mono.
void DownmixToMono(const float* in, std::size_t frames, int channels, float* out);

/// A decoder that produces interleaved float PCM a piece at a time.
class PcmDecoder {
public:
    virtual ~PcmDecoder() = default;
    int sampleRate = 0;
    int channels = 0;                 ///< 1 or 2
    std::int64_t totalFrames = -1;    ///< -1 when unknown
    /// Up to `maxFrames` frames into `dst`; 0 at the end.
    virtual std::size_t Read(float* dst, std::size_t maxFrames) = 0;
    virtual bool Seek(std::int64_t frame) = 0;
    virtual std::size_t MemoryBytes() const = 0;
};

/// OGG Vorbis through VorbisStream (in AudioDecode.cpp).
std::unique_ptr<PcmDecoder> MakeOggDecoder(std::shared_ptr<AudioByteSource> src, std::string* error);

} // namespace audio_detail
} // namespace okay
