#pragma once
// Frame recorder and player: keep the real frame times of a session, save them, and feed
// them back into GameLoop::BeginFrame so a hitchy session or a bug report plays again
// with identical timing. No sleeping, no real clock: a test replays a recording and gets
// the same Time values every run.
//
//     FrameRecorder rec;                       // records until Stop()
//     loop.AttachRecorder(&rec);               // GameLoop adds every frame (and its flags)
//     ...play...
//     rec.Save("session.okayframes");          // atomic write
//
//     FrameRecording r;  std::string why;
//     if (!FrameRecording::Load("session.okayframes", r, &why)) Log::Warning(why);
//     FramePlayer player(r);
//     loop.AttachPlayer(&player);              // BeginFrame now ignores the real clock
//     while (loop.BeginFrame(0.0)) { ... }     // stops after the last recorded frame
//     Log::Info(r.Summary().ToText());
//
// What is recorded per frame: the real frame time in whole microseconds (what GameLoop
// measured, or the capture / step time that replaced it: it is the time the frame
// really used, before the clamp), a few flags (paused, hitch, stepped, capture) and an
// integer marker the host sets with GameLoop::SetFrameMarker (an input-batch number, a
// scene id: anything it wants to line up with its own input log). Whole microseconds
// are the reason GameLoop rounds the measured time to them while a recorder is attached:
// the live run and the replay then step Time by the very same numbers.
//
// What is not recorded: GameLoop's options, the debug time scale, the game's own inputs.
// Replay with the same options and the same inputs to get the same simulation.
//
// The text form ("okayframes 1"), one recording per file:
//     okayframes 1
//     frames 124
//     micros 2066667
//     16667*120 16666 250000/h 16667/p@7*3 ...          <- runs of identical frames
//     checksum 9f3a0c1d2b4e5f60                         <- FNV-1a of every byte above it
// A token is `micros[/flags][@marker][*count]`; flags are letters p (paused), h (hitch),
// s (stepped), c (capture). A capture session of a million frames at one rate is one
// token. Loading is strict: the version, the totals and the checksum must agree, every
// number is range checked, the size and the frame count are capped, and garbage is an
// error message, never a crash. Plain C++17, no threads.
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace okay {

/// Per-frame flags (FrameSample::flags).
enum FrameFlag : std::uint8_t {
    kFramePaused    = 1,   ///< Game::Paused() when the frame began
    kFrameHitch     = 2,   ///< the real time was over the loop's hitch threshold
    kFrameStepped   = 4,   ///< a debug step (GameLoop::RequestStep) while paused
    kFrameSynthetic = 8,   ///< a capture-mode frame: its time was set, not measured
};

struct FrameSample {
    std::uint32_t micros = 0;     ///< real frame time, whole microseconds
    std::int32_t  marker = 0;     ///< the host's own number for this frame (0 = none)
    std::uint8_t  flags = 0;      ///< FrameFlag bits

    double Seconds() const { return (double)micros / 1e6; }
    bool operator==(const FrameSample& o) const { return micros == o.micros && marker == o.marker && flags == o.flags; }
    bool operator!=(const FrameSample& o) const { return !(*this == o); }
};

/// Figures of a recording (FrameRecording::Summary).
struct FrameSummary {
    std::uint64_t frames = 0;
    double seconds = 0.0;                       ///< sum of the frame times
    double avgMs = 0.0, minMs = 0.0, maxMs = 0.0;
    double p50Ms = 0.0, p95Ms = 0.0, p99Ms = 0.0;
    double avgFps = 0.0;                        ///< frames / seconds
    double hitchMs = 100.0;                     ///< a frame longer than this is a hitch
    std::uint64_t hitches = 0;
    double longestHitchMs = 0.0;                ///< 0 with no hitch
    std::uint64_t pausedFrames = 0, steppedFrames = 0, syntheticFrames = 0, markedFrames = 0;

    /// A few readable lines.
    std::string ToText() const;
    /// {"frames":...,"seconds":...,"avgMs":...,...} (plain numbers, no spaces).
    std::string ToJson() const;
};

/// The frames of one session, stored as runs of identical frames.
class FrameRecording {
public:
    /// Most frames one recording holds (about 4.6 hours at 60 fps), a longest frame of one
    /// hour, and the largest text the loader reads.
    static constexpr std::uint64_t kMaxFrames = 1000000;
    static constexpr std::uint32_t kMaxFrameMicros = 3600u * 1000000u;
    static constexpr std::size_t   kMaxFileBytes = 64u * 1024u * 1024u;

    /// Seconds -> whole microseconds (rounded; NaN, infinity and negatives are 0; capped at
    /// kMaxFrameMicros). ToSeconds is the one conversion everything uses, so a time that
    /// went through Quantize reads back bit for bit.
    static std::uint32_t ToMicros(double seconds);
    static double ToSeconds(std::uint32_t micros) { return (double)micros / 1e6; }
    static double Quantize(double seconds) { return ToSeconds(ToMicros(seconds)); }

    /// Append one frame (equal neighbours merge into a run). False, and nothing added,
    /// when the recording already holds kMaxFrames.
    bool Add(const FrameSample& sample) { return AddRun(sample, 1); }
    bool Add(double seconds, std::uint8_t flags = 0, std::int32_t marker = 0);
    /// Append `count` identical frames at once (false if that would pass kMaxFrames).
    bool AddRun(const FrameSample& sample, std::uint64_t count);
    void Clear();

    std::uint64_t Frames() const { return m_frames; }
    bool Empty() const { return m_frames == 0; }
    std::uint64_t TotalMicros() const { return m_micros; }
    double TotalSeconds() const { return (double)m_micros / 1e6; }
    /// Distinct runs (the size of the text form, roughly).
    std::size_t Runs() const { return m_runs.size(); }

    /// Frame `index` (0 = first); false when out of range.
    bool At(std::uint64_t index, FrameSample& out) const;
    /// Run `i`: its sample and how many frames it covers; false when out of range.
    bool RunAt(std::size_t i, FrameSample& sample, std::uint64_t& count) const;
    /// Every frame (bounded by kMaxFrames); for tests and exports.
    std::vector<FrameSample> Expand() const;
    bool operator==(const FrameRecording& o) const;
    bool operator!=(const FrameRecording& o) const { return !(*this == o); }

    FrameSummary Summary(double hitchMs = 100.0) const;

    /// The text form (see the top of this file).
    std::string ToText() const;
    /// Read the text form. False, with the reason in *error, for anything that is not a
    /// well-formed, checksummed recording; `out` is only replaced on success.
    static bool FromText(const std::string& text, FrameRecording& out, std::string* error = nullptr);
    /// Write atomically (a crash or a full disk leaves the old file whole).
    bool Save(const std::string& path, std::string* error = nullptr) const;
    static bool Load(const std::string& path, FrameRecording& out, std::string* error = nullptr);

private:
    struct Run {
        std::uint32_t first = 0;     // index of its first frame
        std::uint32_t count = 0;
        std::uint32_t micros = 0;
        std::int32_t  marker = 0;
        std::uint8_t  flags = 0;
    };
    std::vector<Run> m_runs;
    std::uint64_t m_frames = 0, m_micros = 0;
};

/// Records what GameLoop::BeginFrame ran. Recording is on from the start; Stop() pauses it.
class FrameRecorder {
public:
    explicit FrameRecorder(std::uint64_t maxFrames = FrameRecording::kMaxFrames)
        : m_max(maxFrames < FrameRecording::kMaxFrames ? maxFrames : FrameRecording::kMaxFrames) {}

    void Start() { m_recording = true; }
    void Stop() { m_recording = false; }
    bool IsRecording() const { return m_recording; }

    /// Add a frame (GameLoop calls this). The time is rounded to whole microseconds.
    /// False when not recording or when the recorder is full (Truncated() then says so).
    bool Add(double seconds, std::uint8_t flags = 0, std::int32_t marker = 0);
    bool Full() const { return m_rec.Frames() >= m_max; }
    /// A frame was turned away because the recorder was full.
    bool Truncated() const { return m_truncated; }
    std::uint64_t MaxFrames() const { return m_max; }

    const FrameRecording& Recording() const { return m_rec; }
    FrameRecording& Recording() { return m_rec; }
    void Clear() { m_rec.Clear(); m_truncated = false; }
    bool Save(const std::string& path, std::string* error = nullptr) const { return m_rec.Save(path, error); }

private:
    FrameRecording m_rec;
    std::uint64_t m_max;
    bool m_recording = true;
    bool m_truncated = false;
};

/// Hands a recording's frames back one at a time (GameLoop::AttachPlayer).
class FramePlayer {
public:
    FramePlayer() = default;
    explicit FramePlayer(FrameRecording recording) { Load(std::move(recording)); }

    /// Take a recording and start from its first frame.
    void Load(FrameRecording recording);
    const FrameRecording& Recording() const { return m_rec; }

    /// The next frame; false (and `out` untouched) after the last one, unless looping.
    bool Next(FrameSample& out);
    bool HasNext() const { return m_rec.Frames() > 0 && (m_looping || m_pos < m_rec.Frames()); }
    bool Finished() const { return !HasNext(); }
    std::uint64_t Position() const { return m_pos; }
    std::uint64_t Remaining() const { return m_pos < m_rec.Frames() ? m_rec.Frames() - m_pos : 0; }
    std::uint64_t Frames() const { return m_rec.Frames(); }
    void Rewind() { m_pos = 0; m_run = 0; m_inRun = 0; }
    /// Jump so the next frame is `index` (false when out of range).
    bool Seek(std::uint64_t index);
    /// Start over at the end instead of finishing (a benchmark loop).
    void SetLooping(bool on) { m_looping = on; }
    bool Looping() const { return m_looping; }
    /// Times the end was passed while looping.
    std::uint64_t Loops() const { return m_loops; }

private:
    FrameRecording m_rec;
    std::uint64_t m_pos = 0, m_inRun = 0, m_loops = 0;
    std::size_t m_run = 0;
    bool m_looping = false;
};

}  // namespace okay
