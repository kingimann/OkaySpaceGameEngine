#pragma once
// Text lines and graph points for an in-game or editor stats overlay (feature map
// 23.1 to 23.3 in one place): FPS and frame time, CPU/GPU time, memory, draw calls
// and a frame time graph. It only builds data; the UI draws the strings and points
// wherever it likes. (The Core console work has its own DebugOverlay line builder;
// this one is DebugStatsOverlay and reads the okay/Debug models instead.)
//
//     DebugStatsOverlay overlay;
//     overlay.SetSources(&history, &MemoryTracker::Global(), &RenderStatsCollector::Global());
//     for (const std::string& line : overlay.BuildLines()) DrawLabel(line);
//     DebugStatsOverlay::Graph g = overlay.BuildFrameGraph();
//     for (const GraphPoint& p : g.points) PlotVertex(p.x, p.y);
//
// Any source may be null; its lines are simply left out. Main thread only.
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace okay {

class FrameHistory;
class MemoryTracker;
class RenderStatsCollector;

/// One point of a graph. x runs 0 (oldest) to 1 (newest); y is 0 at the bottom and 1
/// at Graph::maxValue (flip it if your y axis points down).
struct GraphPoint {
    float x = 0.0f;
    float y = 0.0f;
    double value = 0.0;             // the real number (ms or MB)
    bool overTarget = false;        // above Graph::target
};

class DebugStatsOverlay {
public:
    struct Options {
        bool showFps = true;
        bool showFrameTime = true;      // frame ms with average, 95th percentile and worst
        bool showCpuGpu = true;         // when a GPU time is known
        bool showMemory = true;         // total, peak and the biggest categories
        bool showLeakWarning = true;    // a line when MemoryTracker::LeakSuspects finds something
        bool showRender = true;         // draw calls, triangles, switches, texture memory
        bool showSpikes = false;        // frames in the window slower than spikeFactor x median
        int  smoothFrames = 30;         // FPS and averages cover this many recent frames
        int  topCategories = 3;         // memory categories listed
        double spikeFactor = 2.0;
        std::size_t graphPoints = 120;  // frames in the frame graph
        double graphMaxMs = 0.0;        // 0 = fit: the worst frame rounded up to a multiple of targetMs/2
        double targetMs = 1000.0 / 60.0;   // the reference line (a 60 fps budget)
    };

    struct Graph {
        std::vector<GraphPoint> points;
        double minValue = 0.0, avgValue = 0.0, maxValue = 0.0;   // maxValue is the top of the graph (y = 1)
        double target = 0.0;
        float targetY = 0.0f;           // where the target line sits (0..1), may be above 1 when clipped
        std::string unit;               // "ms" or "MB"
    };

    DebugStatsOverlay() = default;
    void SetSources(const FrameHistory* frames, const MemoryTracker* memory, const RenderStatsCollector* render) {
        m_frames = frames; m_memory = memory; m_render = render;
    }
    Options& GetOptions() { return m_opt; }
    const Options& GetOptions() const { return m_opt; }

    /// The lines to draw, top to bottom. `detailed` adds percentiles, per-category
    /// memory and the extra render counters.
    std::vector<std::string> BuildLines(bool detailed = false) const;
    /// Frame time of the last graphPoints frames.
    Graph BuildFrameGraph() const;
    /// Total live memory (MB) at each EndFrame of the MemoryTracker.
    Graph BuildMemoryGraph() const;
    /// A graph of any series (oldest first); `target` <= 0 means no reference line.
    static Graph BuildGraph(const std::vector<double>& values, double target, double fixedMax, const std::string& unit);

    /// A line produced each time BuildLines runs ("Bullets: 12"). Returns an id for RemoveLine.
    int AddLine(std::function<std::string()> provider);
    void RemoveLine(int id);
    void ClearLines() { m_extra.clear(); }

    /// "1234" -> "1234", "45600" -> "45.6k", "1200000" -> "1.2M".
    static std::string FormatCount(unsigned long long n);

private:
    const FrameHistory* m_frames = nullptr;
    const MemoryTracker* m_memory = nullptr;
    const RenderStatsCollector* m_render = nullptr;
    Options m_opt;
    struct Extra { int id; std::function<std::string()> fn; };
    std::vector<Extra> m_extra;
    int m_nextId = 1;
};

} // namespace okay
