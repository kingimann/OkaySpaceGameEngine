#pragma once
// FLAC decoder written from the public FLAC format specification
// (https://xiph.org/flac/format.html). It is our own code: nothing here is
// copied from another decoder. Native FLAC files only (not FLAC packed in Ogg).
#include "okay/Audio/AudioByteSource.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace okay {

/// Decoded FLAC audio: interleaved float samples (the integer sample divided by
/// 2^(bits-1), so a full-scale 16-bit file lands on [-1, 1]), the file's channel
/// count and its sample rate.
struct FlacAudio {
    int sampleRate = 0;
    int channels = 0;
    std::vector<float> samples;   ///< interleaved, FrameCount() * channels floats

    std::size_t FrameCount() const {
        return channels > 0 ? samples.size() / static_cast<std::size_t>(channels) : 0;
    }
};

/// Decodes a whole in-memory native FLAC file into PCM.
///
/// Metadata blocks other than STREAMINFO are skipped. Frames use a fixed or
/// linear predictor, rice or escaped residuals, and left/side, right/side or
/// mid/side stereo. A frame whose CRC does not match is skipped and the next
/// sync is tried. It never reads outside `data`. Returns false (with `*error`
/// set) when the file has no FLAC marker or no decodable audio. The MD5 in
/// STREAMINFO is not checked.
bool DecodeFlac(const unsigned char* data, std::size_t size, FlacAudio& out,
                std::string* error = nullptr);

/// The same decoder as DecodeFlac, pulling bytes from an AudioByteSource so a
/// long file is not held as PCM. Read() output matches DecodeFlac sample for
/// sample. Seek decodes forward from the nearest frame already visited (the
/// first seek walks from the start). Not thread-safe.
class FlacStream {
public:
    FlacStream();
    ~FlacStream();
    FlacStream(const FlacStream&) = delete;
    FlacStream& operator=(const FlacStream&) = delete;

    /// False (with `*error` set) when the source is not a FLAC file with at
    /// least one decodable frame.
    bool Open(std::shared_ptr<AudioByteSource> source, std::string* error = nullptr);
    bool IsOpen() const;
    int SampleRate() const;
    /// Channels in the file (1 to 8). A caller that only wants mono or stereo
    /// averages the rest itself.
    int Channels() const;
    /// Length in sample frames from STREAMINFO, or -1 when the file does not say.
    std::int64_t TotalFrames() const;
    std::size_t Read(float* dst, std::size_t maxFrames);
    bool Seek(std::int64_t frame);
    std::size_t MemoryBytes() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

} // namespace okay
