#pragma once
// Rolling frame-time diagnostics: the last N frames' real times as plain numbers a
// Profiler window or an on-screen overlay can read, with the figures players know from
// benchmarks (average FPS, "1% low", "0.1% low", the worst frame, hitches).
//
//     FrameStats stats(240);                 // the last 240 frames
//     stats.Push(realSeconds * 1000.0);      // GameLoop does this every frame for you
//     FrameStatsSnapshot s = stats.Snapshot();
//     Draw("avg %.1f fps, 1%% low %.1f fps, worst %.1f ms", s.fps, s.low1PercentFps, s.worstMs);
//
// GameLoop owns one (GameLoop::Stats(), window from Options::statsWindow); you can also
// build your own for a different window or for another frame source.
//
// What "1% low" means here: the average frame time of the slowest 1% of the frames in the
// window (at least one frame), and its FPS. "0.1% low" the same for the slowest 0.1%.
// With a 240 frame window that is the 3 worst frames and the single worst frame. A
// hitch is a frame longer than hitchMs (default 100 ms, GameLoop's own default).
//
// Relation to Debug/FrameHistory: FrameHistory is the CPU-profiler timeline (scope trees
// fed from the Profiler). FrameStats does not duplicate it. The average / min / max /
// p50 / p95 / p99 come from FrameHistory's `Distribution`, and SyncTo() copies new frames
// into a FrameHistory (as totalMs / cpuMs per frame) so FindSpikes, SeriesStats and the
// JSON / Chrome-trace exports work on loop frames too.
//
// Push is O(1) with no allocation once the window exists. Snapshot() and the text / JSON
// forms sort a copy of the window: call them once per UI refresh, not per frame. Not thread
// safe; the loop's thread owns it. Plain C++17.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace okay {

class FrameHistory;

struct FrameStatsSnapshot {
    std::size_t frames = 0;          ///< frames in the window right now
    std::size_t window = 0;          ///< the window size
    std::uint64_t totalFrames = 0;   ///< frames pushed since Clear (not only the window)
    std::uint64_t totalHitches = 0;  ///< hitches since Clear
    double lastMs = 0.0;
    double avgMs = 0.0, minMs = 0.0, worstMs = 0.0;
    double p50Ms = 0.0, p95Ms = 0.0, p99Ms = 0.0;
    double fps = 0.0;                ///< 1000 / avgMs (0 with no frames)
    double low1PercentMs = 0.0, low1PercentFps = 0.0;
    double low01PercentMs = 0.0, low01PercentFps = 0.0;
    int    hitches = 0;              ///< frames in the window over hitchMs
    double hitchMs = 100.0;
};

class FrameStats {
public:
    static constexpr std::size_t kMaxWindow = 100000;

    /// `window` frames are kept (1..kMaxWindow; 0 turns recording off).
    explicit FrameStats(std::size_t window = 240, double hitchMs = 100.0);

    std::size_t Window() const { return m_cap; }
    /// Resize the window; the newest frames are kept. 0 stops recording and empties it.
    void SetWindow(std::size_t window);
    double HitchMs() const { return m_hitchMs; }
    /// A frame longer than this counts as a hitch (<= 0 or NaN: counting off).
    void SetHitchMs(double ms);

    /// One frame's time in milliseconds. A value that is not a finite number >= 0
    /// (NaN, infinity, negative) is stored as 0 ms: the frame still counts.
    void Push(double ms);
    void Clear();

    std::size_t Size() const { return m_size; }
    bool Empty() const { return m_size == 0; }
    std::uint64_t TotalFrames() const { return m_total; }
    std::uint64_t TotalHitches() const { return m_totalHitches; }
    /// The newest frame's time (0 with none).
    double LastMs() const;
    /// Frame times of the window, oldest first (a graph's points).
    std::vector<double> Values() const;
    /// Frames per second over the newest `frames` frames of the window: how many there were divided by how
    /// long they took. (Not the mean of each frame's own rate, which a few very short frames inflate.) It reads
    /// REAL frame times, so a stall counts as the stall it was. 0 with no frames, or when they took no time.
    /// O(frames), no allocation: cheap enough for every frame.
    double AverageFps(std::size_t frames) const;

    /// The window's figures; cheap enough for once per UI refresh.
    FrameStatsSnapshot Snapshot() const;
    /// "240 frames: 59.8 fps avg (16.72 ms), 1% low 41.2 fps, 0.1% low 22.0 fps, worst 45.31 ms, 2 hitches".
    std::string ToText() const;
    std::string ToJson() const;
    /// The window as CSV, oldest frame first, for a spreadsheet or a plot: a header line (`header`)
    /// `frame,start_ms,frame_ms,fps` and one row per frame. `frame` counts from the last Clear (the
    /// first frame ever pushed is 0), `start_ms` is where the frame began on the sum of all frame
    /// times, `fps` is 1000 / frame_ms (0 for a 0 ms frame). Numbers use a dot and no thousands
    /// separator whatever the locale. Cost is linear in the window: an export button, not a per-frame call.
    std::string ToCsv(bool header = true) const;
    /// Write ToCsv() to `path` atomically (a crash or a full disk leaves the old file whole). False,
    /// with the reason in *error, when the file cannot be written.
    bool SaveCsv(const std::string& path, std::string* error = nullptr) const;

    /// Copy the frames pushed since the last SyncTo (at most the window's worth) into a
    /// FrameHistory as FrameRecords (startMs, totalMs = cpuMs = frame time). Returns how
    /// many were added. It remembers what it already copied, so call it each frame or each
    /// UI refresh with the same history; for a different history call ResetSync() first and
    /// the next SyncTo copies the whole window again.
    std::size_t SyncTo(FrameHistory& history);
    void ResetSync() { m_synced = 0; }

private:
    struct Entry { double ms = 0.0; double startMs = 0.0; };
    const Entry& At(std::size_t i) const;   // 0 = oldest in the window

    std::vector<Entry> m_ring;
    std::size_t m_cap = 0, m_head = 0, m_size = 0;
    std::uint64_t m_total = 0, m_totalHitches = 0, m_synced = 0;
    double m_hitchMs = 100.0;
    double m_clockMs = 0.0;   // sum of every frame pushed: where the next one starts
};

}  // namespace okay
