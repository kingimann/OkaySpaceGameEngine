#pragma once
#include <cstddef>
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

} // namespace okay
