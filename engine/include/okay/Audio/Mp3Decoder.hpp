#pragma once
#include "okay/Audio/AudioByteSource.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace okay {

/// Decoded MP3 audio: interleaved float samples (nominally in [-1, 1], not
/// clipped), the channel count (1 or 2) and the sample rate of the stream.
struct Mp3Audio {
    int sampleRate = 0;
    int channels = 0;
    std::vector<float> samples;    ///< interleaved, FrameCount() * channels floats
    int framesDecoded = 0;         ///< MP3 frames that produced audio
    int framesSkipped = 0;         ///< frames whose data was unusable (decoded as silence)
    int encoderDelay = 0;          ///< from the LAME/Xing tag, 0 when there is none
    int encoderPadding = 0;        ///< from the LAME/Xing tag, 0 when there is none
    bool hasLameTag = false;

    std::size_t FrameCount() const {
        return channels > 0 ? samples.size() / static_cast<std::size_t>(channels) : 0;
    }
};

struct Mp3Options {
    /// Drop the encoder delay and padding that a LAME/Xing header records, so
    /// the result has exactly the length of the source audio (what players and
    /// the previous vendored decoder do). Without such a header nothing is
    /// trimmed.
    bool trimGapless = true;
};

/// Decodes a whole in-memory MP3 file (MPEG-1/2/2.5 Layer III) into PCM.
///
/// Leading ID3v2 tags and a Xing/Info/VBRI header frame are skipped, damaged or
/// missing data is bridged by resynchronising on the next valid frame, and
/// frames that cannot be decoded come out as silence of the right length. It
/// never reads outside `data` and never throws. Returns false (with `*error`
/// set) only when no audio frame could be found at all. Layer I/II streams and
/// free-format bitrates are not supported.
bool DecodeMp3(const unsigned char* data, std::size_t size, Mp3Audio& out,
               std::string* error = nullptr, const Mp3Options* options = nullptr);

/// Incremental MP3 decoder: the same decoder as DecodeMp3, but it pulls bytes
/// from an AudioByteSource and hands back PCM a frame at a time, so a long
/// file is never held in memory. Seek is exact: Open scans the frame headers
/// once (no decoding) and keeps one byte offset per 32 frames; Seek jumps to
/// the nearest one and decodes forward, discarding PCM, so the decoder's
/// bit reservoir and filter state are warm at the target. Output of
/// Read() is identical, sample for sample, to DecodeMp3 with the same options.
/// Not thread-safe; use one stream from one thread.
class Mp3Stream {
public:
    Mp3Stream();
    ~Mp3Stream();
    Mp3Stream(const Mp3Stream&) = delete;
    Mp3Stream& operator=(const Mp3Stream&) = delete;

    /// Finds the first audio frame and reads the stream parameters. With
    /// `buildIndex` (needed for Read, Seek and TotalFrames) it also walks the
    /// rest of the source once to count frames and index them. False (with
    /// `*error` set) when no audio frame exists.
    bool Open(std::shared_ptr<AudioByteSource> source, std::string* error = nullptr,
              const Mp3Options* options = nullptr, bool buildIndex = true);
    bool IsOpen() const;

    int SampleRate() const;
    int Channels() const;
    /// Length in sample frames after gapless trimming; -1 when Open did not index.
    std::int64_t TotalFrames() const;

    /// Up to `maxFrames` frames of trimmed, interleaved PCM into `dst`; 0 at the
    /// end. Requires an indexed Open. Damaged frames come out as silence.
    std::size_t Read(float* dst, std::size_t maxFrames);
    /// Moves so the next Read starts at output frame `frame` (clamped to the
    /// length). Requires an indexed Open.
    bool Seek(std::int64_t frame);
    /// Seek(0) that also works on a stream opened without an index.
    void Rewind();

    /// Appends one whole frame's PCM, untrimmed (what DecodeMp3 does in a loop);
    /// false at the end of the stream.
    bool DecodeFrame(std::vector<float>& out);

    int FramesDecoded() const;
    int FramesSkipped() const;
    bool HasLameTag() const;
    int EncoderDelay() const;
    int EncoderPadding() const;
    std::uint32_t XingFrames() const;
    int SamplesPerFrame() const;
    /// Bytes this stream holds: read window, PCM buffer and the frame index.
    std::size_t MemoryBytes() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

} // namespace okay
