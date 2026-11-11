#pragma once
#include <cstddef>
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

} // namespace okay
