#include "okay/Debug/DebugStatsOverlay.hpp"
#include "okay/Debug/DebugUtil.hpp"
#include "okay/Debug/FrameHistory.hpp"
#include "okay/Debug/MemoryTracker.hpp"
#include "okay/Debug/RenderStats.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace okay {

using namespace debugutil;

static std::string Fmt(const char* fmt, double a) {
    char b[96]; std::snprintf(b, sizeof(b), fmt, a); return b;
}

std::string DebugStatsOverlay::FormatCount(unsigned long long n) {
    char b[32];
    if (n >= 1000000ull) std::snprintf(b, sizeof(b), "%.1fM", (double)n / 1000000.0);
    else if (n >= 10000ull) std::snprintf(b, sizeof(b), "%.1fk", (double)n / 1000.0);
    else std::snprintf(b, sizeof(b), "%llu", n);
    return b;
}

int DebugStatsOverlay::AddLine(std::function<std::string()> provider) {
    if (!provider) return 0;
    Extra e; e.id = m_nextId++; e.fn = std::move(provider);
    m_extra.push_back(std::move(e));
    return m_extra.back().id;
}

void DebugStatsOverlay::RemoveLine(int id) {
    for (std::size_t i = 0; i < m_extra.size(); ++i)
        if (m_extra[i].id == id) { m_extra.erase(m_extra.begin() + (std::ptrdiff_t)i); return; }
}

std::vector<std::string> DebugStatsOverlay::BuildLines(bool detailed) const {
    std::vector<std::string> lines;
    const Options& o = m_opt;

    if (m_frames && !m_frames->Empty()) {
        const std::size_t size = m_frames->Size();
        std::size_t want = o.smoothFrames < 1 ? 1 : (std::size_t)o.smoothFrames;
        std::size_t first = size > want ? size - want : 0;
        Distribution d = m_frames->SeriesStats(FrameHistory::Series::Total, first, FrameHistory::kEnd);
        double fps = d.avg > 0.0 ? 1000.0 / d.avg : 0.0;
        if (o.showFps && o.showFrameTime) lines.push_back("FPS: " + Fmt("%.1f", fps) + " (" + Fmt("%.1f", d.avg) + " ms)");
        else if (o.showFps) lines.push_back("FPS: " + Fmt("%.1f", fps));
        else if (o.showFrameTime) lines.push_back("Frame: " + Fmt("%.1f", d.avg) + " ms");
        if (detailed && o.showFrameTime) {
            char b[160];
            std::snprintf(b, sizeof(b), "Frame ms: avg %.2f  p95 %.2f  p99 %.2f  max %.2f", d.avg, d.p95, d.p99, d.max);
            lines.push_back(b);
        }
        if (o.showCpuGpu) {
            const FrameRecord& last = *m_frames->Latest();
            double gpu = last.gpuMs;
            if (gpu <= 0.0 && m_render) gpu = m_render->Last().gpuMs;
            if (gpu > 0.0) lines.push_back("CPU: " + Fmt("%.1f", last.cpuMs) + " ms  GPU: " + Fmt("%.1f", gpu) + " ms");
            else if (detailed && last.cpuMs > 0.0) lines.push_back("CPU: " + Fmt("%.1f", last.cpuMs) + " ms");
        }
        if (o.showSpikes) {
            std::size_t gfirst = size > o.graphPoints ? size - o.graphPoints : 0;
            std::size_t n = m_frames->FindSpikes(o.spikeFactor, gfirst, FrameHistory::kEnd).size();
            lines.push_back("Spikes: " + std::to_string(n) + " in last " + std::to_string(size - gfirst) + " frames");
        }
    }

    if (m_memory && o.showMemory && (m_memory->TotalBytes() > 0 || m_memory->PeakTotalBytes() > 0)) {
        lines.push_back("Memory: " + FormatBytes(m_memory->TotalBytes()) + " (peak " + FormatBytes(m_memory->PeakTotalBytes()) + ")");
        MemorySnapshot snap = m_memory->Snapshot();
        std::vector<const MemoryCategoryInfo*> cats;
        for (const MemoryCategoryInfo& c : snap.categories) if (c.bytes > 0) cats.push_back(&c);
        std::stable_sort(cats.begin(), cats.end(), [](const MemoryCategoryInfo* a, const MemoryCategoryInfo* b) { return a->bytes > b->bytes; });
        std::size_t limit = detailed ? cats.size() : (o.topCategories < 0 ? 0 : (std::size_t)o.topCategories);
        if (limit > cats.size()) limit = cats.size();
        if (detailed) {
            for (std::size_t i = 0; i < limit; ++i)
                lines.push_back("  " + cats[i]->name + ": " + FormatBytes(cats[i]->bytes) + " x" + std::to_string(cats[i]->count));
        } else if (limit > 0) {
            std::string l = "  ";
            for (std::size_t i = 0; i < limit; ++i) { if (i) l += ", "; l += cats[i]->name + " " + FormatBytes(cats[i]->bytes); }
            lines.push_back(l);
        }
        if (o.showLeakWarning) {
            for (const MemoryLeakSuspect& s : m_memory->LeakSuspects())
                lines.push_back("Possible leak: " + s.name + " +" + FormatBytes(s.growthBytes) + " over " + std::to_string(s.windowFrames) + " frames");
        }
    }

    if (m_render && o.showRender) {
        GpuFrameStats s = m_render->Last();
        if (s.frame > 0 || s.drawCalls > 0) {
            std::string l = "Draw calls: " + std::to_string(s.drawCalls) + "  Tris: " + FormatCount(s.triangles)
                          + "  Mat switches: " + std::to_string(s.materialSwitches);
            lines.push_back(l);
            if (s.textureBytes > 0) lines.push_back("Textures: ~" + FormatBytes((long long)s.textureBytes));
            if (detailed) {
                lines.push_back("Verts: " + FormatCount(s.vertices) + "  Instances: " + FormatCount(s.instances)
                                + "  Dispatches: " + std::to_string(s.dispatches));
                lines.push_back("Switches: shader " + std::to_string(s.shaderSwitches) + ", texture " + std::to_string(s.textureSwitches)
                                + ", target " + std::to_string(s.renderTargetSwitches));
                if (s.meshBytes > 0) lines.push_back("Meshes: ~" + FormatBytes((long long)s.meshBytes));
            }
        }
    }

    for (const Extra& e : m_extra) {
        std::string s = e.fn();
        if (!s.empty()) lines.push_back(std::move(s));
    }
    return lines;
}

DebugStatsOverlay::Graph DebugStatsOverlay::BuildGraph(const std::vector<double>& values, double target,
                                                       double fixedMax, const std::string& unit) {
    Graph g;
    g.unit = unit;
    g.target = target > 0.0 ? target : 0.0;
    if (values.empty()) { g.maxValue = fixedMax > 0.0 ? fixedMax : (g.target > 0.0 ? g.target : 1.0); return g; }
    double mn = values[0], mx = values[0], sum = 0.0;
    for (double v : values) { if (v < mn) mn = v; if (v > mx) mx = v; sum += v; }
    g.minValue = mn;
    g.avgValue = sum / (double)values.size();
    double top = fixedMax;
    if (!(top > 0.0)) {
        top = mx > g.target ? mx : g.target;
        if (g.target > 0.0) {
            double step = g.target / 2.0;
            top = std::ceil(top / step) * step;
        }
        if (!(top > 0.0)) top = 1.0;
    }
    g.maxValue = top;
    g.targetY = g.target > 0.0 ? (float)(g.target / top) : 0.0f;
    g.points.reserve(values.size());
    for (std::size_t i = 0; i < values.size(); ++i) {
        GraphPoint p;
        p.x = values.size() == 1 ? 1.0f : (float)i / (float)(values.size() - 1);
        double y = values[i] / top;
        p.y = (float)(y < 0.0 ? 0.0 : (y > 1.0 ? 1.0 : y));
        p.value = values[i];
        p.overTarget = g.target > 0.0 && values[i] > g.target;
        g.points.push_back(p);
    }
    return g;
}

DebugStatsOverlay::Graph DebugStatsOverlay::BuildFrameGraph() const {
    std::vector<double> v;
    if (m_frames && !m_frames->Empty()) {
        std::size_t size = m_frames->Size();
        std::size_t first = size > m_opt.graphPoints ? size - m_opt.graphPoints : 0;
        v = m_frames->SeriesValues(FrameHistory::Series::Total, first, FrameHistory::kEnd);
    }
    return BuildGraph(v, m_opt.targetMs, m_opt.graphMaxMs, "ms");
}

DebugStatsOverlay::Graph DebugStatsOverlay::BuildMemoryGraph() const {
    std::vector<double> v;
    if (m_memory) {
        for (std::int64_t b : m_memory->TotalHistory(m_opt.graphPoints)) v.push_back((double)b / (1024.0 * 1024.0));
    }
    return BuildGraph(v, 0.0, 0.0, "MB");
}

} // namespace okay
