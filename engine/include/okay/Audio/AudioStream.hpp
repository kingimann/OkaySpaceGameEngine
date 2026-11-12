#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace okay {

namespace audio_detail { class PcmDecoder; }

/// A long sound played straight from its file instead of being decoded into
/// memory: WAV, OGG Vorbis or MP3 (the format is recognised from the file's
/// first bytes). Only a ring of decoded frames, one read window and the
/// decoder's own state are ever held, a fixed couple of hundred kilobytes
/// whatever the length (an MP3 also keeps one 8-byte offset per 32 frames to
/// seek, about 0.6 KB per minute); see MemoryBytes().
///
/// Threading: an AudioStream has no locks. The engine's audio device runs in
/// queue mode, so AudioMixer::RenderStereo runs on the game thread and pumps
/// every streaming source at the start of each buffer: decoding happens
/// there, ahead of what the buffer consumes, never on a device callback. Use
/// one stream from one thread, and one stream per playing source.
///
/// Frames are addressed by an absolute, "unrolled" index that starts at the
/// last Seek (or 0) and keeps counting through loops, so the sound a looping
/// stream plays is one unbroken timeline.
class AudioStream {
public:
    static constexpr std::size_t kRingFrames = 32768;   ///< decoded frames held ahead (0.74 s at 44.1 kHz)
    static constexpr std::size_t kChunkFrames = 2048;   ///< frames decoded per step

    /// Opens `path` for streaming. Null (with `*error` set) when it cannot be
    /// read or is not WAV, OGG or MP3. Files over 2 GB are refused.
    static std::shared_ptr<AudioStream> OpenFile(const std::string& path, std::string* error = nullptr);
    /// Streams from a block of memory (a file already loaded, an asset pack entry).
    static std::shared_ptr<AudioStream> OpenMemory(std::shared_ptr<const std::vector<unsigned char>> bytes,
                                                   std::string* error = nullptr);
    ~AudioStream();
    AudioStream(const AudioStream&) = delete;
    AudioStream& operator=(const AudioStream&) = delete;

    int SampleRate() const { return m_rate; }
    /// 1 or 2 (a file with more channels is averaged to mono).
    int Channels() const { return m_channels; }
    /// Length in sample frames, or -1 when the format could not tell.
    std::int64_t TotalFrames() const { return m_total; }
    float Duration() const { return (m_total >= 0 && m_rate > 0) ? static_cast<float>(m_total) / static_cast<float>(m_rate) : 0.0f; }

    /// When set, the end of the file continues from its start with no gap
    /// (the ring keeps filling across the join). Set it before frames run out.
    bool loop = false;
    /// Decode at most this many frames per Pump (0 = fill the ring). For slow
    /// media; whatever the mixer needs beyond it is silence, counted in Underruns().
    std::size_t decodeBudget = 0;

    // --- the decode step and the ring -------------------------------------------------------------
    /// Decodes ahead until the ring is full (or the budget or the end of a
    /// non-looping file is reached); returns the frames decoded.
    std::size_t Pump();
    /// First absolute frame still held, and one past the last.
    std::uint64_t HeadFrame() const { return m_head; }
    std::uint64_t EndFrame() const { return m_next; }
    bool Has(std::uint64_t frame) const { return frame >= m_head && frame < m_next; }
    /// Copies the `Channels()` samples of absolute frame `frame` to `out`; false if it is not in the ring.
    bool Frame(std::uint64_t frame, float* out) const;
    /// Drops every frame below `frame` so the ring has room again.
    void Release(std::uint64_t frame);
    /// True once the decoder has nothing more to add (never for a healthy looping file).
    bool Ended() const { return m_ended; }

    /// Moves to source frame `frame` (clamped to the length; wrapped when looping).
    /// The ring is emptied and the timeline restarts there; the next Pump refills.
    /// Returns false if the format could not seek (the stream then starts over at 0).
    bool Seek(std::int64_t frame);
    /// Pop up to `frames` frames from the head into `dst` (Pump first): the plain sequential
    /// read, used by tests and tools. Returns the frames produced (less only at the end).
    std::size_t Read(float* dst, std::size_t frames);
    /// Where the next frame to play is, in source frames (wrapped when looping).
    std::int64_t Position() const;

    /// Times a reader found the ring empty while the file had more (silence was played).
    std::uint64_t Underruns() const { return m_underruns; }
    void NoteUnderrun() { ++m_underruns; }
    /// Bytes this stream holds right now: the ring, the decoder's buffers and tables.
    std::size_t MemoryBytes() const;

private:
    AudioStream();
    bool Init(std::unique_ptr<audio_detail::PcmDecoder> dec);

    std::unique_ptr<audio_detail::PcmDecoder> m_dec;
    int m_rate = 0, m_channels = 1;
    std::int64_t m_total = -1;
    std::vector<float> m_ring;
    std::size_t m_readIdx = 0, m_count = 0;
    std::uint64_t m_head = 0, m_next = 0;
    std::int64_t m_origin = 0;          // source frame at the last Seek
    std::uint64_t m_sinceRewind = 0;
    bool m_ended = false;
    std::uint64_t m_underruns = 0;
};

} // namespace okay
