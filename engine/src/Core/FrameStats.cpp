#include "okay/Core/FrameStats.hpp"
#include "okay/Core/SaveData.hpp"      // savefmt::AtomicWrite
#include "okay/Debug/FrameHistory.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace okay {

namespace {

// Mean of the largest `count` values of an ascending-sorted vector (count >= 1).
double MeanOfWorst(const std::vector<double>& sorted, std::size_t count) {
    if (sorted.empty() || count == 0) return 0.0;
    if (count > sorted.size()) count = sorted.size();
    double sum = 0.0;
    for (std::size_t i = sorted.size() - count; i < sorted.size(); ++i) sum += sorted[i];
    return sum / (double)count;
}

// Number of frames in the slowest `percent` of `n` frames: at least one.
std::size_t WorstCount(std::size_t n, double percent) {
    const double c = std::ceil((double)n * percent / 100.0 - 1e-9);
    return c < 1.0 ? 1 : (std::size_t)c;
}

std::string Fmt(const char* f, double a) { char b[64]; std::snprintf(b, sizeof b, f, a); return b; }

}  // namespace

FrameStats::FrameStats(std::size_t window, double hitchMs) {
    SetHitchMs(hitchMs);
    SetWindow(window);
}

void FrameStats::SetHitchMs(double ms) {
    m_hitchMs = (ms > 0.0 && std::isfinite(ms)) ? ms : 0.0;   // 0 = counting off
}

void FrameStats::SetWindow(std::size_t window) {
    if (window > kMaxWindow) window = kMaxWindow;
    if (window == m_cap) return;
    // Re-linearise, keeping the newest frames.
    std::vector<Entry> fresh;
    const std::size_t keep = std::min(m_size, window);
    fresh.reserve(window);
    for (std::size_t i = m_size - keep; i < m_size; ++i) fresh.push_back(At(i));
    m_ring.swap(fresh);
    m_cap = window;
    m_size = keep;
    m_head = 0;   // the oldest frame is slot 0
}

void FrameStats::Clear() {
    m_ring.clear();
    m_ring.reserve(m_cap);
    m_head = m_size = 0;
    m_total = m_totalHitches = m_synced = 0;
    m_clockMs = 0.0;
}

const FrameStats::Entry& FrameStats::At(std::size_t i) const {
    static const Entry kNone;
    if (i >= m_size || m_ring.empty()) return kNone;
    return m_ring[(m_head + i) % m_ring.size()];
}

void FrameStats::Push(double ms) {
    if (!(ms > 0.0) || !std::isfinite(ms)) ms = 0.0;
    ++m_total;
    if (m_hitchMs > 0.0 && ms > m_hitchMs) ++m_totalHitches;
    const Entry e{ms, m_clockMs};
    m_clockMs += ms;
    if (m_cap == 0) return;
    if (m_ring.size() < m_cap) {
        m_ring.push_back(e);
        ++m_size;
    } else {
        m_ring[m_head] = e;
        m_head = (m_head + 1) % m_ring.size();
    }
}

double FrameStats::LastMs() const { return m_size ? At(m_size - 1).ms : 0.0; }

std::vector<double> FrameStats::Values() const {
    std::vector<double> v;
    v.reserve(m_size);
    for (std::size_t i = 0; i < m_size; ++i) v.push_back(At(i).ms);
    return v;
}

double FrameStats::AverageFps(std::size_t frames) const {
    const std::size_t take = std::min(frames, m_size);
    if (take == 0) return 0.0;
    double ms = 0.0;
    for (std::size_t i = m_size - take; i < m_size; ++i) ms += At(i).ms;
    return ms > 0.0 ? (double)take * 1000.0 / ms : 0.0;
}

FrameStatsSnapshot FrameStats::Snapshot() const {
    FrameStatsSnapshot s;
    s.frames = m_size;
    s.window = m_cap;
    s.totalFrames = m_total;
    s.totalHitches = m_totalHitches;
    s.hitchMs = m_hitchMs;
    s.lastMs = LastMs();
    if (m_size == 0) return s;
    std::vector<double> v = Values();
    for (double x : v) if (m_hitchMs > 0.0 && x > m_hitchMs) ++s.hitches;
    const Distribution d = Distribution::Of(v);   // FrameHistory's min/avg/max/percentiles
    s.avgMs = d.avg; s.minMs = d.min; s.worstMs = d.max;
    s.p50Ms = d.p50; s.p95Ms = d.p95; s.p99Ms = d.p99;
    s.fps = d.avg > 0.0 ? 1000.0 / d.avg : 0.0;
    std::sort(v.begin(), v.end());
    s.low1PercentMs = MeanOfWorst(v, WorstCount(v.size(), 1.0));
    s.low01PercentMs = MeanOfWorst(v, WorstCount(v.size(), 0.1));
    s.low1PercentFps = s.low1PercentMs > 0.0 ? 1000.0 / s.low1PercentMs : 0.0;
    s.low01PercentFps = s.low01PercentMs > 0.0 ? 1000.0 / s.low01PercentMs : 0.0;
    return s;
}

std::string FrameStats::ToText() const {
    const FrameStatsSnapshot s = Snapshot();
    if (s.frames == 0) return "no frames";
    std::string out = std::to_string(s.frames) + " frames: " + Fmt("%.1f", s.fps) + " fps avg (" + Fmt("%.2f", s.avgMs) +
                      " ms), 1% low " + Fmt("%.1f", s.low1PercentFps) + " fps, 0.1% low " + Fmt("%.1f", s.low01PercentFps) +
                      " fps, worst " + Fmt("%.2f", s.worstMs) + " ms, " + std::to_string(s.hitches) +
                      (s.hitches == 1 ? " hitch" : " hitches");
    return out;
}

std::string FrameStats::ToJson() const {
    const FrameStatsSnapshot s = Snapshot();
    auto n = [](double v) { char b[48]; std::snprintf(b, sizeof b, "%.4f", v); return std::string(b); };
    std::string j = "{";
    j += "\"frames\":" + std::to_string(s.frames) + ",\"window\":" + std::to_string(s.window);
    j += ",\"totalFrames\":" + std::to_string(s.totalFrames) + ",\"totalHitches\":" + std::to_string(s.totalHitches);
    j += ",\"lastMs\":" + n(s.lastMs) + ",\"avgMs\":" + n(s.avgMs) + ",\"minMs\":" + n(s.minMs) + ",\"worstMs\":" + n(s.worstMs);
    j += ",\"p50Ms\":" + n(s.p50Ms) + ",\"p95Ms\":" + n(s.p95Ms) + ",\"p99Ms\":" + n(s.p99Ms) + ",\"fps\":" + n(s.fps);
    j += ",\"low1PercentMs\":" + n(s.low1PercentMs) + ",\"low1PercentFps\":" + n(s.low1PercentFps);
    j += ",\"low01PercentMs\":" + n(s.low01PercentMs) + ",\"low01PercentFps\":" + n(s.low01PercentFps);
    j += ",\"hitches\":" + std::to_string(s.hitches) + ",\"hitchMs\":" + n(s.hitchMs) + "}";
    return j;
}

std::string FrameStats::ToCsv(bool header) const {
    std::string out;
    if (header) out = "frame,start_ms,frame_ms,fps\n";
    out.reserve(out.size() + m_size * 36);
    const std::uint64_t oldest = m_total - (std::uint64_t)m_size;
    char buf[128];
    for (std::size_t i = 0; i < m_size; ++i) {
        const Entry& e = At(i);
        const double fps = e.ms > 0.0 ? 1000.0 / e.ms : 0.0;
        std::snprintf(buf, sizeof buf, "%llu,%.4f,%.4f,%.2f\n", (unsigned long long)(oldest + (std::uint64_t)i), e.startMs, e.ms, fps);
        out += buf;
    }
    return out;
}

bool FrameStats::SaveCsv(const std::string& path, std::string* error) const {
    if (path.empty()) { if (error) *error = "no file name given"; return false; }
    if (!savefmt::AtomicWrite(path, ToCsv())) { if (error) *error = "could not write " + path; return false; }
    return true;
}

std::size_t FrameStats::SyncTo(FrameHistory& history) {
    // Frames still in the window that this history has not seen.
    const std::uint64_t oldest = m_total - (std::uint64_t)m_size;
    std::uint64_t from = std::max(m_synced, oldest);
    std::size_t added = 0;
    for (std::uint64_t k = from; k < m_total; ++k) {
        const Entry& e = At((std::size_t)(k - oldest));
        FrameRecord r;
        r.startMs = e.startMs;
        r.totalMs = e.ms;
        r.cpuMs = e.ms;
        history.Push(std::move(r));
        ++added;
    }
    m_synced = m_total;
    return added;
}

}  // namespace okay
