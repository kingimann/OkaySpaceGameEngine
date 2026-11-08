#pragma once
#include <chrono>
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <fstream>

namespace okay {

/// An advanced, frame profiler. Scoped CPU timers group into named
/// sections (self + inclusive time, call counts, nesting depth). Every frame is
/// snapshotted into a ring buffer so the editor can PAUSE and SCRUB back through
/// recent frames, graph the frame/CPU/GPU timeline, draw a stacked CPU-category
/// area, and show per-section min/avg/max over the window. Off by default — the
/// editor's Profiler window turns it on, so it costs nothing when not viewed.
///
/// Usage:  { OKAY_PROFILE("Physics"); ...work... }   // RAII begin/end (nestable)
/// Frame:  Prof().BeginFrame();  ...  Prof().EndFrame(frameMs);
class Profiler {
public:
    static constexpr int kHistory = 240;   // frames kept (~4s at 60fps)

    struct Bucket {
        std::string name;
        double self = 0.0;       // ms in this scope, excluding child scopes
        double inclusive = 0.0;  // ms including children
        int    calls = 0;
        int    depth = 0;        // nesting depth (for the call tree indent)
        double startMs = 0.0;    // first call's start, ms after the frame began (for traces)
    };
    struct Frame {
        std::vector<Bucket> buckets;
        double total = 0.0, cpu = 0.0, gpu = 0.0;
        double startMs = 0.0;    // frame start, ms since the profiler's first frame
        int objects = 0, triangles = 0, particles = 0;
    };
    struct Stat { double mn = 0, avg = 0, mx = 0; int frames = 0; };  // per-section over the window

    bool enabled = false;        // gate: the editor window flips this on
    bool paused  = false;        // freeze + scrub
    int  selected = -1;          // scrubbed frame index into the ring, or -1 = latest

    // ---- live-frame render stats (set by the editor each frame) ----
    int drawObjects = 0, drawTriangles = 0, particles = 0;
    void Stat3(int objects, int tris, int parts) { drawObjects = objects; drawTriangles = tris; particles = parts; }

    double gpuMs = 0.0;          // most recent synchronous render/read-back time (GPU proxy)
    void Gpu(double ms) { gpuMs = ms; }

    int    histPos = 0;          // next write slot in the ring
    double lastFrameMs = 0.0, cpuMs = 0.0;

    // ---- frame ring access -------------------------------------------------
    int Count() const { return m_count < kHistory ? m_count : kHistory; }
    /// Map a 0..Count()-1 timeline index (oldest..newest) to a ring slot.
    int RingSlot(int timelineIdx) const {
        int n = Count();
        int start = (histPos - n + kHistory * 2) % kHistory;
        return (start + timelineIdx) % kHistory;
    }
    const Frame& FrameAt(int timelineIdx) const { return m_ring[RingSlot(timelineIdx)]; }
    /// The frame the window should display: the scrubbed one, else the newest.
    const Frame& Displayed() const {
        int n = Count();
        if (n <= 0) return m_empty;
        int idx = (selected >= 0 && selected < n) ? selected : n - 1;
        return m_ring[RingSlot(idx)];
    }

    // history series for plotting (oldest -> newest), filled by the caller's loop
    double SeriesTotal(int i) const { return m_ring[RingSlot(i)].total; }
    double SeriesGpu(int i)   const { return m_ring[RingSlot(i)].gpu; }
    double SeriesCpu(int i)   const { return m_ring[RingSlot(i)].cpu; }

    /// Sum of self time over a category prefix (e.g. "Scripts") for a frame — used
    /// for the stacked CPU graph. "" matches everything not otherwise categorised.
    static double CategorySelf(const Frame& f, const char* prefix) {
        double s = 0.0; std::size_t pl = std::strlen(prefix);
        for (const auto& b : f.buckets)
            if (b.name.compare(0, pl, prefix) == 0) s += b.self;
        return s;
    }

    /// Per-section min/avg/max self-ms across the whole ring ("Overview").
    Stat SectionStat(const std::string& name) const {
        Stat r; double sum = 0; bool first = true; int n = Count();
        for (int i = 0; i < n; ++i)
            for (const auto& b : m_ring[RingSlot(i)].buckets)
                if (b.name == name) {
                    if (first) { r.mn = r.mx = b.self; first = false; }
                    else { if (b.self < r.mn) r.mn = b.self; if (b.self > r.mx) r.mx = b.self; }
                    sum += b.self; r.frames++;
                }
        if (r.frames > 0) r.avg = sum / r.frames;
        return r;
    }

    // ---- collection --------------------------------------------------------
    void BeginFrame() {
        if (!enabled) return;
        m_build.clear();
        m_stack.clear();
        m_frameStart = Now();
        if (!m_haveEpoch) { m_epoch = m_frameStart; m_haveEpoch = true; }
    }
    void Begin(const char* name) {
        if (!enabled) return;
        Open o; o.name = name; o.start = Now(); o.childMs = 0.0; o.depth = (int)m_stack.size();
        m_stack.push_back(o);
    }
    void End() {
        if (!enabled || m_stack.empty()) return;
        Open o = m_stack.back(); m_stack.pop_back();
        double inclusive = Ms(Now() - o.start);
        double self = inclusive - o.childMs; if (self < 0.0) self = 0.0;
        if (!m_stack.empty()) m_stack.back().childMs += inclusive;
        Bucket* b = Find(o.name, o.depth);
        double startMs = o.start > m_frameStart ? Ms(o.start - m_frameStart) : 0.0;
        if (!b) { m_build.push_back({o.name, 0, 0, 0, o.depth, startMs}); b = &m_build.back(); }
        else if (startMs < b->startMs) b->startMs = startMs;
        b->self += self; b->inclusive += inclusive; b->calls++;
    }
    void EndFrame(double frameMs) {
        if (!enabled) return;
        lastFrameMs = frameMs;
        double cpu = 0.0;
        for (const auto& b : m_build) if (b.depth == 0) cpu += b.inclusive;
        cpuMs = cpu;
        if (paused) return;   // frozen: keep the ring as-is for scrubbing
        Frame& f = m_ring[histPos];
        f.buckets = m_build;
        f.total = frameMs; f.cpu = cpu; f.gpu = gpuMs;
        f.startMs = m_haveEpoch ? Ms(m_frameStart - m_epoch) : 0.0;
        f.objects = drawObjects; f.triangles = drawTriangles; f.particles = particles;
        histPos = (histPos + 1) % kHistory;
        if (m_count < kHistory * 2) ++m_count;
    }

    void Range(double& outAvg, double& outPeak) const {
        double sum = 0, peak = 0; int n = Count();
        for (int i = 0; i < n; ++i) { double v = m_ring[RingSlot(i)].total;
            sum += v; if (v > peak) peak = v; }
        outAvg = n > 0 ? sum / n : 0.0; outPeak = peak;
    }

    // ---- export / text reports (allocate only when called) -----------------
    /// (File versions are named ...ToFile so a std::string path can never pick the string-out overload.)
    /// Chrome trace JSON ("traceEvents", ph "X", ts/dur in microseconds) for the
    /// timeline frames firstFrame..lastFrame inclusive (0 = oldest; clamped).
    /// Open the result in chrome://tracing or Perfetto. A bucket merges repeated
    /// calls of one section, so it is one event spanning its summed time (calls in args).
    /// Returns the number of events written.
    int ExportChromeTrace(std::string& out, int firstFrame, int lastFrame) const {
        out = "{\"displayTimeUnit\":\"ms\",\"traceEvents\":[";
        int events = 0, n = Count();
        if (firstFrame < 0) firstFrame = 0;
        if (lastFrame > n - 1) lastFrame = n - 1;
        for (int fi = firstFrame; fi <= lastFrame; ++fi) {
            const Frame& f = FrameAt(fi);
            for (const auto& b : f.buckets) {
                if (events++) out += ',';
                out += "{\"name\":\""; AppendJsonEscaped(out, b.name);
                out += "\",\"cat\":\"okay\",\"ph\":\"X\",\"ts\":"; AppendNum(out, (f.startMs + b.startMs) * 1000.0);
                out += ",\"dur\":"; AppendNum(out, b.inclusive * 1000.0);
                out += ",\"pid\":1,\"tid\":1,\"args\":{\"frame\":" + std::to_string(fi)
                     + ",\"calls\":" + std::to_string(b.calls)
                     + ",\"depth\":" + std::to_string(b.depth) + ",\"self_ms\":";
                AppendNum(out, b.self);
                out += "}}";
            }
        }
        out += "]}";
        return events;
    }
    bool ExportChromeTraceToFile(const std::string& path, int firstFrame, int lastFrame) const {
        std::string s; ExportChromeTrace(s, firstFrame, lastFrame);
        return WriteText(path, s);
    }
    /// CSV: frame,section,ms,calls,depth,self_ms (ms = inclusive). One row per
    /// bucket for frames firstFrame..lastFrame (default: the whole ring).
    int ExportCsv(std::string& out, int firstFrame = 0, int lastFrame = kHistory) const {
        out = "frame,section,ms,calls,depth,self_ms\n";
        int rows = 0, n = Count();
        if (firstFrame < 0) firstFrame = 0;
        if (lastFrame > n - 1) lastFrame = n - 1;
        for (int fi = firstFrame; fi <= lastFrame; ++fi)
            for (const auto& b : FrameAt(fi).buckets) {
                out += std::to_string(fi) + ',';
                bool q = b.name.find_first_of(",\"\n\r") != std::string::npos;
                if (q) {
                    out += '"';
                    for (char c : b.name) { if (c == '"') out += '"'; out += c; }
                    out += '"';
                } else out += b.name;
                out += ','; AppendNum(out, b.inclusive);
                out += ',' + std::to_string(b.calls) + ',' + std::to_string(b.depth) + ',';
                AppendNum(out, b.self); out += '\n';
                ++rows;
            }
        return rows;
    }
    bool ExportCsvToFile(const std::string& path, int firstFrame = 0, int lastFrame = kHistory) const {
        std::string s; ExportCsv(s, firstFrame, lastFrame);
        return WriteText(path, s);
    }
    /// One frame (timeline index, 0 = oldest) as an indented tree, parents first.
    std::string DumpFrame(int idx) const {
        if (idx < 0 || idx >= Count()) return "(no such frame)\n";
        const Frame& f = FrameAt(idx);
        char buf[160];
        std::snprintf(buf, sizeof(buf), "Frame %d: total %.3f ms, cpu %.3f ms, gpu %.3f ms\n", idx, f.total, f.cpu, f.gpu);
        std::string out = buf;
        std::vector<const Bucket*> order;
        for (const auto& b : f.buckets) order.push_back(&b);
        std::stable_sort(order.begin(), order.end(), [](const Bucket* a, const Bucket* b) {
            return a->startMs != b->startMs ? a->startMs < b->startMs : a->depth < b->depth; });
        for (const Bucket* b : order) {
            out.append((std::size_t)b->depth * 2 + 2, ' ');
            std::snprintf(buf, sizeof(buf), "  %.3f ms (self %.3f) x%d\n", b->inclusive, b->self, b->calls);
            out += b->name; out += buf;
        }
        return out;
    }
    /// Per-section self-ms min/avg/max over the whole ring, as a text table.
    std::string Summary() const {
        std::vector<std::string> names;
        for (int i = 0, n = Count(); i < n; ++i)
            for (const auto& b : FrameAt(i).buckets)
                if (std::find(names.begin(), names.end(), b.name) == names.end()) names.push_back(b.name);
        char buf[200];
        std::snprintf(buf, sizeof(buf), "Profiler summary over %d frame(s)\n", Count());
        std::string out = buf;
        for (const auto& nm : names) {
            Stat s = SectionStat(nm);
            std::snprintf(buf, sizeof(buf), "  min %8.3f  avg %8.3f  max %8.3f ms  (%d frames)  ", s.mn, s.avg, s.mx, s.frames);
            out += buf; out += nm; out += '\n';
        }
        return out;
    }

private:
    static void AppendNum(std::string& out, double v) {
        char b[48]; std::snprintf(b, sizeof(b), "%.3f", v); out += b;
    }
    static void AppendJsonEscaped(std::string& out, const std::string& s) {
        for (unsigned char c : s) {
            switch (c) {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (c < 0x20) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", c); out += b; }
                    else out += (char)c;
            }
        }
    }
    static bool WriteText(const std::string& path, const std::string& s) {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(s.data(), (std::streamsize)s.size());
        return (bool)f;
    }

    struct Open { const char* name; std::chrono::steady_clock::time_point start; double childMs; int depth; };
    std::vector<Open>   m_stack;
    std::vector<Bucket> m_build;
    Frame   m_ring[kHistory];
    Frame   m_empty;
    int     m_count = 0;
    std::chrono::steady_clock::time_point m_frameStart{}, m_epoch{};
    bool    m_haveEpoch = false;

    static std::chrono::steady_clock::time_point Now() { return std::chrono::steady_clock::now(); }
    static double Ms(std::chrono::steady_clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); }
    Bucket* Find(const char* name, int depth) {
        for (auto& b : m_build) if (b.depth == depth && b.name == name) return &b;
        return nullptr;
    }
};

inline Profiler& Prof() { static Profiler p; return p; }

struct ProfScope {
    explicit ProfScope(const char* name) { Prof().Begin(name); }
    ~ProfScope() { Prof().End(); }
    ProfScope(const ProfScope&) = delete;
    ProfScope& operator=(const ProfScope&) = delete;
};

#define OKAY_PROF_CONCAT2(a, b) a##b
#define OKAY_PROF_CONCAT(a, b) OKAY_PROF_CONCAT2(a, b)
#define OKAY_PROFILE(name) ::okay::ProfScope OKAY_PROF_CONCAT(_okprof_, __LINE__)(name)

} // namespace okay
