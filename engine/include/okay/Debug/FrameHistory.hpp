#pragma once
// CPU frame timeline model (feature map 23.1): a ring buffer of per-frame scope
// trees fed from the existing Profiler, with per-scope min/avg/max/percentiles,
// spike detection, a flame-graph aggregate (self vs inclusive time) and text, JSON
// and Chrome trace export of any frame range. No UI here; a window draws from it.
//
//     FrameHistory history(600);
//     Prof().enabled = true;
//     ...each frame, after Prof().EndFrame(ms):
//     history.Capture(Prof());                     // copies the frames it has not seen
//     auto spikes = history.FindSpikes(2.0);       // frames slower than 2x the median
//     auto flame  = history.BuildFlameGraph();     // aggregated call tree
//
// Not thread safe: drive it from the thread that owns the Profiler.
#include "okay/Core/Profiler.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace okay {

/// One scope of one frame. Scopes are stored in pre-order (a parent always comes
/// before its children) and `parent` indexes the same vector.
struct FrameScope {
    std::string name;
    double inclusiveMs = 0.0;   // time in the scope including children
    double selfMs      = 0.0;   // time in the scope excluding children
    double startMs     = 0.0;   // start relative to the frame start
    int    calls       = 0;
    int    depth       = 0;     // 0 = top level
    int    parent      = -1;    // index into FrameRecord::scopes, -1 at the top
};

/// A snapshot of one frame: totals plus the scope tree.
struct FrameRecord {
    std::uint64_t sequence = 0;     // assigned by FrameHistory::Push, counts up from 0
    double startMs = 0.0;           // frame start, ms since the first frame
    double totalMs = 0.0;           // whole frame
    double cpuMs   = 0.0;
    double gpuMs   = 0.0;
    int objects = 0, triangles = 0, particles = 0;
    std::vector<FrameScope> scopes;

    /// Append a scope in pre-order. Its parent is the nearest earlier scope with a
    /// smaller depth. Returns the new scope's index. For synthetic frames in tests
    /// and for importing from other sources.
    int AddScope(const std::string& name, int depth, double inclusiveMs, double selfMs,
                 int calls = 1, double startMs = 0.0);

    /// Sum of self time over all scopes with this name in this frame.
    double SelfMs(const std::string& name) const;
    /// Inclusive time for this name; a scope nested inside one of the same name
    /// (recursion) is not counted twice.
    double InclusiveMs(const std::string& name) const;
    /// True if any scope has this name.
    bool Has(const std::string& name) const;

    /// Convert a Profiler frame. Profiler buckets are flat (one per section name
    /// and depth); the tree is rebuilt from start time and depth, so two scopes of
    /// the same name and depth under different parents show under one of them.
    static FrameRecord FromProfiler(const Profiler::Frame& f);
};

/// min/avg/max and percentiles of a series (percentiles interpolate linearly).
struct Distribution {
    std::size_t count = 0;
    double sum = 0.0, min = 0.0, avg = 0.0, max = 0.0;
    double p50 = 0.0, p90 = 0.0, p95 = 0.0, p99 = 0.0;
    static Distribution Of(std::vector<double> values);
};

/// A frame found by spike detection.
struct FrameSpike {
    std::size_t frame = 0;          // timeline index (0 = oldest)
    std::uint64_t sequence = 0;
    double totalMs = 0.0;
    double medianMs = 0.0;          // median frame time of the range scanned
    double ratio = 0.0;             // totalMs / medianMs
    std::string culprit;            // scope that rose most above its own median (may be empty)
    double culpritExtraMs = 0.0;    // how far above that median it ran
};

/// Per-scope statistics over a frame range.
struct ScopeStats {
    std::string name;
    Distribution dist;
    std::size_t framesPresent = 0;  // frames in the range where the scope ran
    std::size_t framesTotal   = 0;  // frames in the range
};

/// One node of the aggregated call tree.
struct FlameNode {
    std::string name;
    int parent = -1;
    int depth = -1;                 // root is -1, top level scopes 0
    double inclusiveMs = 0.0;       // summed over the range
    double selfMs = 0.0;
    long long calls = 0;
    std::vector<int> children;      // sorted by inclusive time, largest first
};

/// Aggregated call tree: scopes that share the same path of names (Update, then
/// Physics, then Broadphase) are merged across frames. Node 0 is a synthetic root
/// named "(frames)" whose inclusive time is the sum of the top level scopes.
struct FlameGraph {
    std::vector<FlameNode> nodes;
    std::size_t frames = 0;         // frames aggregated

    struct Rect { int node = 0; int depth = 0; double x0 = 0.0, x1 = 0.0; };

    bool Empty() const { return nodes.size() <= 1; }
    double TotalMs() const { return nodes.empty() ? 0.0 : nodes[0].inclusiveMs; }
    /// Self time summed over every node with this name (any path).
    double SelfMs(const std::string& name) const;
    /// Indented tree, children by inclusive time. Nodes below `minPercent` of the
    /// total or deeper than maxDepth (-1 = no limit) are left out.
    std::string ToText(int maxDepth = -1, double minPercent = 0.0) const;
    /// Folded stacks for external flame graph tools: `a;b;c <self microseconds>`.
    std::string ToFolded() const;
    std::string ToJson() const;
    /// Rectangles for drawing: x0..x1 is the node's share (0..1) of the total, laid
    /// out left to right under its parent; depth 0 is the row under the root.
    std::vector<Rect> Layout(double minFraction = 0.0) const;
};

class FrameHistory {
public:
    enum class Metric { Self, Inclusive };
    enum class Series { Total, Cpu, Gpu };
    /// Pass as `last` for "up to the newest frame".
    static constexpr std::size_t kEnd = static_cast<std::size_t>(-1);

    explicit FrameHistory(std::size_t capacity = 600);

    // ---- ring buffer ------------------------------------------------------
    std::size_t Capacity() const { return m_capacity; }
    /// Resize; if the history is larger than the new capacity the oldest frames go.
    void SetCapacity(std::size_t capacity);
    std::size_t Size() const { return m_size; }
    bool Empty() const { return m_size == 0; }
    void Clear();
    /// Add a frame (assigns its sequence number); the oldest is dropped when full.
    std::uint64_t Push(FrameRecord rec);
    /// Frame by timeline index, 0 = oldest. Out of range returns an empty record.
    const FrameRecord& At(std::size_t i) const;
    const FrameRecord* Latest() const { return m_size ? &At(m_size - 1) : nullptr; }
    std::uint64_t TotalPushed() const { return m_next; }
    /// Timeline index of a sequence number, or -1 if it has been dropped.
    long long IndexOfSequence(std::uint64_t seq) const;

    // ---- Profiler adapter -------------------------------------------------
    /// Copy frames the Profiler recorded since the last call (a frame is "seen"
    /// when its startMs is not newer than the last one copied). Reads only the
    /// Profiler's public ring, so Profiler.hpp stays as it is. If the Profiler is
    /// paused or disabled nothing new arrives. A different Profiler, whose time
    /// runs behind the last frame seen, restarts the capture. Returns frames added.
    std::size_t Capture(const Profiler& p);
    /// Forget which Profiler frames were already copied (the next Capture takes the whole ring).
    void ResetCapture() { m_haveLast = false; m_lastStartMs = 0.0; }

    // ---- statistics -------------------------------------------------------
    /// Frame time, CPU time or GPU time over the range first..last (inclusive timeline indices).
    Distribution SeriesStats(Series s = Series::Total, std::size_t first = 0, std::size_t last = kEnd) const;
    double Fps(std::size_t first = 0, std::size_t last = kEnd) const;   // 1000 / average frame ms
    /// Per-frame values of the range, oldest first (a graph's points).
    std::vector<double> SeriesValues(Series s = Series::Total, std::size_t first = 0, std::size_t last = kEnd) const;
    /// One scope by name. By default only frames where it ran count; with
    /// absentAsZero the frames without it count as 0 ms (right for scopes that run
    /// every few frames).
    ScopeStats ScopeStat(const std::string& name, Metric m = Metric::Self, std::size_t first = 0,
                         std::size_t last = kEnd, bool absentAsZero = false) const;
    /// Every scope seen in the range, the highest average first.
    std::vector<ScopeStats> AllScopeStats(Metric m = Metric::Self, std::size_t first = 0,
                                          std::size_t last = kEnd, bool absentAsZero = false) const;

    // ---- spikes -----------------------------------------------------------
    /// Frames of the range whose total time is above k times the median of the range
    /// (and above minMs). Oldest first.
    std::vector<FrameSpike> FindSpikes(double k = 2.0, std::size_t first = 0, std::size_t last = kEnd,
                                       double minMs = 0.0) const;

    // ---- call tree --------------------------------------------------------
    FlameGraph BuildFlameGraph(std::size_t first = 0, std::size_t last = kEnd) const;

    // ---- export -----------------------------------------------------------
    /// Readable report: per-frame trees, then a summary table of the range.
    std::string ExportText(std::size_t first = 0, std::size_t last = kEnd) const;
    /// {"frames":[{...,"scopes":[...]}],"summary":{...}} for tools and bug reports.
    std::string ExportJson(std::size_t first = 0, std::size_t last = kEnd) const;
    /// Chrome trace (chrome://tracing, Perfetto): a "Frame" event per frame on
    /// thread 0 and one event per scope on thread 1, ts/dur in microseconds.
    /// Returns the number of events.
    int ExportChromeTrace(std::string& out, std::size_t first = 0, std::size_t last = kEnd) const;
    bool ExportChromeTraceToFile(const std::string& path, std::size_t first = 0, std::size_t last = kEnd) const;
    bool ExportJsonToFile(const std::string& path, std::size_t first = 0, std::size_t last = kEnd) const;
    bool ExportTextToFile(const std::string& path, std::size_t first = 0, std::size_t last = kEnd) const;

private:
    bool Range(std::size_t& first, std::size_t& last) const;   // clamp; false if empty

    std::vector<FrameRecord> m_ring;
    std::size_t m_capacity = 0;
    std::size_t m_head = 0;        // ring slot of the oldest frame (only when full)
    std::size_t m_size = 0;
    std::uint64_t m_next = 0;      // next sequence number
    bool   m_haveLast = false;
    double m_lastStartMs = 0.0;
};

} // namespace okay
