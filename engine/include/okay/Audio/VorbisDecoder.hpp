#pragma once
#include "okay/Audio/AudioByteSource.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace okay {

/// Decoded Ogg Vorbis audio: interleaved float samples (nominally in [-1, 1],
/// not clipped), the channel count and the sample rate of the stream. Channels
/// are in Vorbis stream order (mono; L R; L C R; FL FR BL BR; and so on), no
/// remapping to another layout is done.
struct VorbisAudio {
    int sampleRate = 0;
    int channels = 0;
    std::vector<float> samples;   ///< interleaved, FrameCount() * channels floats
    int streamsFound = 0;         ///< Vorbis logical streams (chained or multiplexed) found in the file
    int streamsDecoded = 0;       ///< streams whose audio ended up in `samples`
    bool truncated = false;       ///< the stream has no end-of-stream page (file cut off) or the sample cap was hit

    std::size_t FrameCount() const {
        return channels > 0 ? samples.size() / static_cast<std::size_t>(channels) : 0;
    }
};

struct VorbisOptions {
    /// Chained files hold several logical streams one after another. false (the
    /// default, what the previous vendored decoder did) decodes the first one
    /// only; true appends the following ones as long as they have the same
    /// channel count and sample rate.
    bool allStreams = false;
    /// Upper bound on channels * frames of the result. A hostile stream can
    /// claim far more audio than its size suggests; decoding stops (with
    /// `truncated` set) when the cap is reached.
    std::size_t maxSamples = std::size_t(1) << 28;
};

/// Decodes a whole in-memory Ogg Vorbis I file into PCM.
///
/// Pages are checked against their CRC (damaged pages are skipped and the next
/// page found by resynchronising), packets are reassembled across pages, and
/// the stream is decoded with floor type 1, residue types 0, 1 and 2, channel
/// coupling and both block sizes, trimmed to the end-of-stream granule
/// position. Truncated or corrupt input yields whatever decoded before the
/// damage. It never reads outside `data`, bounds every allocation by the input
/// and `options->maxSamples`, and never throws. Returns false (with `*error`
/// set) when no Vorbis stream with valid headers and at least one decoded
/// sample is found. Floor type 0 (obsolete, no known encoder emits it) is
/// rejected as unsupported, as are more than 32 channels.
bool DecodeVorbis(const unsigned char* data, std::size_t size, VorbisAudio& out,
                  std::string* error = nullptr, const VorbisOptions* options = nullptr);

/// Incremental Ogg Vorbis decoder: the same decoder as DecodeVorbis, but it
/// reads Ogg pages from an AudioByteSource as they are needed and keeps the
/// overlap-add state between calls, so a long file is never held in memory (the
/// codebooks and a few block buffers, a few hundred KB, plus a one-page read
/// buffer). Open reads the file once, without decoding audio, to find the
/// length (from the audio packets' block sizes, trimmed to the final granule
/// position) and an index of every 16th page for seeking. Read() output is
/// identical, sample for sample, to DecodeVorbis(first stream) up to its
/// maxSamples cap, which streaming does not have. Seek restarts decoding at the
/// nearest indexed page at or before the target (the first packet only primes
/// the overlap) and is exact. Damaged pages are skipped like DecodeVorbis does.
/// Not thread-safe. Channels are in Vorbis stream order, up to 32.
class VorbisStream {
public:
    VorbisStream();
    ~VorbisStream();
    VorbisStream(const VorbisStream&) = delete;
    VorbisStream& operator=(const VorbisStream&) = delete;

    /// False (with `*error` set) when no Vorbis stream with valid headers and
    /// at least one sample is found.
    bool Open(std::shared_ptr<AudioByteSource> source, std::string* error = nullptr);
    bool IsOpen() const;
    int SampleRate() const;
    int Channels() const;
    /// Length in sample frames.
    std::int64_t TotalFrames() const;
    /// Up to `maxFrames` interleaved frames into `dst`; 0 at the end.
    std::size_t Read(float* dst, std::size_t maxFrames);
    /// The next Read starts at `frame` (clamped to the length).
    bool Seek(std::int64_t frame);
    /// Bytes held: setup tables, block buffers, page buffer, seek index.
    std::size_t MemoryBytes() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

} // namespace okay
