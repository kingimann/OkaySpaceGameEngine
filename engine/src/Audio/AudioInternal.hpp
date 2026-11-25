// Shared by AudioClip.cpp, AudioDecode.cpp and AudioStream.cpp: the channel
// rules, the mono downmix and the PcmDecoder interface the streaming decoders
// implement. (WAV, whole and streamed, is okay::wav in WavDecoder.hpp.)
#pragma once
#include "okay/Audio/AudioByteSource.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace okay {
namespace audio_detail {

/// Channels a decoded clip keeps: 1 or 2 stay as they are, more than two are
/// averaged to mono; `downmix` forces mono.
inline int KeptChannels(int fileChannels, bool downmix) { return (downmix || fileChannels > 2) ? 1 : (fileChannels < 1 ? 1 : fileChannels); }

/// Averages `in` (interleaved, `channels` per frame) to mono.
void DownmixToMono(const float* in, std::size_t frames, int channels, float* out);

/// A decoder that produces interleaved float PCM a piece at a time.
class PcmDecoder {
public:
    virtual ~PcmDecoder() = default;
    int sampleRate = 0;
    int channels = 0;                 ///< 1 or 2 once MakeDecoder has averaged wider files to mono
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
