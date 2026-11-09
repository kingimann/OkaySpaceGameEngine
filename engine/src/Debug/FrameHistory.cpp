#include "okay/Debug/FrameHistory.hpp"
#include "okay/Debug/DebugUtil.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <map>
#include <unordered_map>

namespace okay {

using namespace debugutil;

// ---------------------------------------------------------------------------
// FrameRecord
// ---------------------------------------------------------------------------

int FrameRecord::AddScope(const std::string& name, int depth, double inclusiveMs, double selfMs,
                          int calls, double startMsIn) {
    FrameScope s;
    s.name = name;
    s.depth = depth < 0 ? 0 : depth;
    s.inclusiveMs = inclusiveMs;
    s.selfMs = selfMs;
    s.calls = calls;
    s.startMs = startMsIn;
    s.parent = -1;
    if (s.depth > 0) {
        for (int i = (int)scopes.size() - 1; i >= 0; --i)
            if (scopes[(std::size_t)i].depth < s.depth) { s.parent = i; break; }
    }
    scopes.push_back(std::move(s));
    return (int)scopes.size() - 1;
}

double FrameRecord::SelfMs(const std::string& name) const {
    double sum = 0.0;
    for (const FrameScope& s : scopes) if (s.name == name) sum += s.selfMs;
    return sum;
}

bool FrameRecord::Has(const std::string& name) const {
    for (const FrameScope& s : scopes) if (s.name == name) return true;
    return false;
}

// True if some ancestor of scope `i` carries the same name (recursion).
static bool AncestorHasName(const std::vector<FrameScope>& scopes, int i) {
    const std::string& nm = scopes[(std::size_t)i].name;
    for (int p = scopes[(std::size_t)i].parent; p >= 0; p = scopes[(std::size_t)p].parent)
        if (scopes[(std::size_t)p].name == nm) return true;
    return false;
}

double FrameRecord::InclusiveMs(const std::string& name) const {
    double sum = 0.0;
    for (std::size_t i = 0; i < scopes.size(); ++i)
        if (scopes[i].name == name && !AncestorHasName(scopes, (int)i)) sum += scopes[i].inclusiveMs;
    return sum;
}

FrameRecord FrameRecord::FromProfiler(const Profiler::Frame& f) {
    FrameRecord r;
    r.startMs = f.startMs;
    r.totalMs = f.total; r.cpuMs = f.cpu; r.gpuMs = f.gpu;
    r.objects = f.objects; r.triangles = f.triangles; r.particles = f.particles;
    std::vector<const Profiler::Bucket*> order;
    order.reserve(f.buckets.size());
    for (const auto& b : f.buckets) order.push_back(&b);
    std::stable_sort(order.begin(), order.end(), [](const Profiler::Bucket* a, const Profiler::Bucket* b) {
        return a->startMs != b->startMs ? a->startMs < b->startMs : a->depth < b->depth; });
    r.scopes.reserve(order.size());
    for (const Profiler::Bucket* b : order)
        r.AddScope(b->name, b->depth, b->inclusive, b->self, b->calls, b->startMs);
    return r;
}

// ---------------------------------------------------------------------------
// Distribution
// ---------------------------------------------------------------------------

Distribution Distribution::Of(std::vector<double> v) {
    Distribution d;
    d.count = v.size();
    if (v.empty()) return d;
    std::sort(v.begin(), v.end());
    for (double x : v) d.sum += x;
    d.min = v.front(); d.max = v.back();
    d.avg = d.sum / (double)v.size();
    d.p50 = PercentileSorted(v, 50.0);
    d.p90 = PercentileSorted(v, 90.0);
    d.p95 = PercentileSorted(v, 95.0);
    d.p99 = PercentileSorted(v, 99.0);
    return d;
}

// ---------------------------------------------------------------------------
// FrameHistory: ring buffer
// ---------------------------------------------------------------------------

FrameHistory::FrameHistory(std::size_t capacity) : m_capacity(capacity) {}

void FrameHistory::Clear() {
    m_ring.clear();
    m_head = 0; m_size = 0;
    // sequence numbers keep counting so exports from before and after never collide
}

const FrameRecord& FrameHistory::At(std::size_t i) const {
    static const FrameRecord kEmpty;
    if (i >= m_size) return kEmpty;
    return m_ring[(m_head + i) % m_ring.size()];
}

std::uint64_t FrameHistory::Push(FrameRecord rec) {
    rec.sequence = m_next++;
    const std::uint64_t seq = rec.sequence;
    if (m_capacity == 0) return seq;
    if (m_ring.size() < m_capacity) {
        m_ring.push_back(std::move(rec));
        ++m_size;
    } else {
        m_ring[m_head] = std::move(rec);
        m_head = (m_head + 1) % m_ring.size();
    }
    return seq;
}

void FrameHistory::SetCapacity(std::size_t capacity) {
    if (capacity == m_capacity) return;
    // Re-linearise, keeping the newest `capacity` frames.
    std::vector<FrameRecord> fresh;
    std::size_t keep = m_size < capacity ? m_size : capacity;
    fresh.reserve(keep);
    for (std::size_t i = m_size - keep; i < m_size; ++i) fresh.push_back(std::move(m_ring[(m_head + i) % m_ring.size()]));
    m_ring = std::move(fresh);
    m_head = 0;
    m_size = keep;
    m_capacity = capacity;
}

long long FrameHistory::IndexOfSequence(std::uint64_t seq) const {
    if (m_size == 0) return -1;
    std::uint64_t oldest = At(0).sequence;
    if (seq < oldest || seq >= oldest + m_size) return -1;
    return (long long)(seq - oldest);
}

std::size_t FrameHistory::Capture(const Profiler& p) {
    int n = p.Count();
    if (n <= 0) return 0;
    const double newestStart = p.FrameAt(n - 1).startMs;
    if (m_haveLast && newestStart < m_lastStartMs) m_haveLast = false;   // another Profiler: start over
    int i = n;
    while (i > 0) {
        const Profiler::Frame& f = p.FrameAt(i - 1);
        if (m_haveLast && f.startMs <= m_lastStartMs) break;
        --i;
    }
    std::size_t added = 0;
    for (; i < n; ++i) { Push(FrameRecord::FromProfiler(p.FrameAt(i))); ++added; }
    m_lastStartMs = newestStart;
    m_haveLast = true;
    return added;
}

bool FrameHistory::Range(std::size_t& first, std::size_t& last) const {
    if (m_size == 0) return false;
    if (last == kEnd || last >= m_size) last = m_size - 1;
    if (first > last) return false;
    return true;
}

// ---------------------------------------------------------------------------
// Statistics
// ---------------------------------------------------------------------------

std::vector<double> FrameHistory::SeriesValues(Series s, std::size_t first, std::size_t last) const {
    std::vector<double> v;
    if (!Range(first, last)) return v;
    v.reserve(last - first + 1);
    for (std::size_t i = first; i <= last; ++i) {
        const FrameRecord& f = At(i);
        v.push_back(s == Series::Total ? f.totalMs : s == Series::Cpu ? f.cpuMs : f.gpuMs);
    }
    return v;
}

Distribution FrameHistory::SeriesStats(Series s, std::size_t first, std::size_t last) const {
    return Distribution::Of(SeriesValues(s, first, last));
}

double FrameHistory::Fps(std::size_t first, std::size_t last) const {
    Distribution d = SeriesStats(Series::Total, first, last);
    return d.avg > 0.0 ? 1000.0 / d.avg : 0.0;
}

ScopeStats FrameHistory::ScopeStat(const std::string& name, Metric m, std::size_t first,
                                   std::size_t last, bool absentAsZero) const {
    ScopeStats r;
    r.name = name;
    if (!Range(first, last)) return r;
    r.framesTotal = last - first + 1;
    std::vector<double> v;
    for (std::size_t i = first; i <= last; ++i) {
        const FrameRecord& f = At(i);
        if (f.Has(name)) {
            v.push_back(m == Metric::Self ? f.SelfMs(name) : f.InclusiveMs(name));
            ++r.framesPresent;
        } else if (absentAsZero) {
            v.push_back(0.0);
        }
    }
    r.dist = Distribution::Of(std::move(v));
    return r;
}

std::vector<ScopeStats> FrameHistory::AllScopeStats(Metric m, std::size_t first, std::size_t last,
                                                    bool absentAsZero) const {
    std::vector<ScopeStats> out;
    if (!Range(first, last)) return out;
    const std::size_t frames = last - first + 1;

    struct Acc { std::vector<double> v; };
    std::unordered_map<std::string, std::size_t> index;
    std::vector<std::string> names;
    std::vector<Acc> accs;
    for (std::size_t fi = first; fi <= last; ++fi) {
        const FrameRecord& f = At(fi);
        // per-name totals of this frame
        std::vector<std::pair<std::size_t, double>> perName;   // acc index, value
        for (std::size_t si = 0; si < f.scopes.size(); ++si) {
            const FrameScope& s = f.scopes[si];
            double val;
            if (m == Metric::Self) val = s.selfMs;
            else val = AncestorHasName(f.scopes, (int)si) ? 0.0 : s.inclusiveMs;
            auto it = index.find(s.name);
            std::size_t ai;
            if (it == index.end()) {
                ai = accs.size();
                index.emplace(s.name, ai);
                names.push_back(s.name);
                accs.emplace_back();
            } else ai = it->second;
            bool merged = false;
            for (auto& pn : perName) if (pn.first == ai) { pn.second += val; merged = true; break; }
            if (!merged) perName.emplace_back(ai, val);
        }
        for (const auto& pn : perName) accs[pn.first].v.push_back(pn.second);
    }
    out.reserve(names.size());
    for (std::size_t i = 0; i < names.size(); ++i) {
        ScopeStats s;
        s.name = names[i];
        s.framesTotal = frames;
        s.framesPresent = accs[i].v.size();
        if (absentAsZero) accs[i].v.resize(frames, 0.0);
        s.dist = Distribution::Of(std::move(accs[i].v));
        out.push_back(std::move(s));
    }
    std::stable_sort(out.begin(), out.end(), [](const ScopeStats& a, const ScopeStats& b) {
        if (a.dist.avg != b.dist.avg) return a.dist.avg > b.dist.avg;
        return a.name < b.name; });
    return out;
}

// ---------------------------------------------------------------------------
// Spikes
// ---------------------------------------------------------------------------

std::vector<FrameSpike> FrameHistory::FindSpikes(double k, std::size_t first, std::size_t last, double minMs) const {
    std::vector<FrameSpike> spikes;
    if (!Range(first, last)) return spikes;
    std::vector<double> totals = SeriesValues(Series::Total, first, last);
    std::vector<double> sorted = totals;
    std::sort(sorted.begin(), sorted.end());
    const double median = PercentileSorted(sorted, 50.0);
    if (!(median > 0.0)) return spikes;
    const double threshold = k * median;

    bool haveMedians = false;
    std::unordered_map<std::string, double> scopeMedian;
    for (std::size_t i = first; i <= last; ++i) {
        const double total = totals[i - first];
        if (!(total > threshold) || !(total > minMs)) continue;
        if (!haveMedians) {
            for (const ScopeStats& s : AllScopeStats(Metric::Self, first, last, true)) scopeMedian[s.name] = s.dist.p50;
            haveMedians = true;
        }
        FrameSpike sp;
        sp.frame = i;
        sp.sequence = At(i).sequence;
        sp.totalMs = total;
        sp.medianMs = median;
        sp.ratio = total / median;
        const FrameRecord& f = At(i);
        std::map<std::string, double> self;
        for (const FrameScope& s : f.scopes) self[s.name] += s.selfMs;
        double best = 0.0;
        for (const auto& kv : self) {
            double extra = kv.second - scopeMedian[kv.first];
            if (extra > best) { best = extra; sp.culprit = kv.first; sp.culpritExtraMs = extra; }
        }
        spikes.push_back(std::move(sp));
    }
    return spikes;
}

// ---------------------------------------------------------------------------
// Flame graph
// ---------------------------------------------------------------------------

FlameGraph FrameHistory::BuildFlameGraph(std::size_t first, std::size_t last) const {
    FlameGraph g;
    FlameNode root;
    root.name = "(frames)";
    g.nodes.push_back(root);
    if (!Range(first, last)) return g;
    g.frames = last - first + 1;
    g.nodes[0].calls = (long long)g.frames;

    std::vector<int> map;
    for (std::size_t fi = first; fi <= last; ++fi) {
        const FrameRecord& f = At(fi);
        map.assign(f.scopes.size(), 0);
        for (std::size_t si = 0; si < f.scopes.size(); ++si) {
            const FrameScope& s = f.scopes[si];
            int parentNode = (s.parent >= 0 && (std::size_t)s.parent < si) ? map[(std::size_t)s.parent] : 0;
            int node = -1;
            for (int c : g.nodes[(std::size_t)parentNode].children)
                if (g.nodes[(std::size_t)c].name == s.name) { node = c; break; }
            if (node < 0) {
                FlameNode n;
                n.name = s.name;
                n.parent = parentNode;
                n.depth = g.nodes[(std::size_t)parentNode].depth + 1;
                g.nodes.push_back(std::move(n));
                node = (int)g.nodes.size() - 1;
                g.nodes[(std::size_t)parentNode].children.push_back(node);
            }
            FlameNode& n = g.nodes[(std::size_t)node];
            n.inclusiveMs += s.inclusiveMs;
            n.selfMs += s.selfMs;
            n.calls += s.calls;
            map[si] = node;
            if (parentNode == 0) g.nodes[0].inclusiveMs += s.inclusiveMs;
        }
    }
    for (FlameNode& n : g.nodes)
        std::stable_sort(n.children.begin(), n.children.end(), [&g](int a, int b) {
            return g.nodes[(std::size_t)a].inclusiveMs > g.nodes[(std::size_t)b].inclusiveMs; });
    return g;
}

double FlameGraph::SelfMs(const std::string& name) const {
    double sum = 0.0;
    for (std::size_t i = 1; i < nodes.size(); ++i) if (nodes[i].name == name) sum += nodes[i].selfMs;
    return sum;
}

static void FlameText(const FlameGraph& g, int node, int maxDepth, double minMs, std::string& out) {
    const FlameNode& n = g.nodes[(std::size_t)node];
    char buf[200];
    if (node != 0) {
        double pct = g.TotalMs() > 0.0 ? n.inclusiveMs / g.TotalMs() * 100.0 : 0.0;
        std::snprintf(buf, sizeof(buf), "  %.3f ms (%.1f%%)  self %.3f ms  x%lld\n", n.inclusiveMs, pct, n.selfMs, n.calls);
        out.append((std::size_t)n.depth * 2, ' ');
        out += n.name; out += buf;
    }
    if (n.depth >= maxDepth) return;
    for (int c : n.children)
        if (g.nodes[(std::size_t)c].inclusiveMs >= minMs) FlameText(g, c, maxDepth, minMs, out);
}

std::string FlameGraph::ToText(int maxDepth, double minPercent) const {
    char buf[160];
    std::snprintf(buf, sizeof(buf), "Call tree over %zu frame(s), total %.3f ms\n", frames, TotalMs());
    std::string out = buf;
    // maxDepth counts rows below the root: 1 = top level scopes only
    int depthLimit = maxDepth < 0 ? INT_MAX : maxDepth - 1;
    if (nodes.size() > 1) FlameText(*this, 0, depthLimit, TotalMs() * minPercent / 100.0, out);
    return out;
}

static void FoldedWalk(const FlameGraph& g, int node, const std::string& path, std::string& out) {
    const FlameNode& n = g.nodes[(std::size_t)node];
    std::string here = path;
    if (node != 0) {
        std::string nm = n.name;
        for (char& c : nm) if (c == ';' || c == '\n' || c == '\r') c = ':';
        if (!here.empty()) here += ';';
        here += nm;
        long long us = (long long)std::llround(n.selfMs * 1000.0);
        if (us > 0) { out += here; out += ' '; out += std::to_string(us); out += '\n'; }
    }
    for (int c : n.children) FoldedWalk(g, c, here, out);
}

std::string FlameGraph::ToFolded() const {
    std::string out;
    if (!nodes.empty()) FoldedWalk(*this, 0, std::string(), out);
    return out;
}

std::string FlameGraph::ToJson() const {
    std::string out = "{\"frames\":" + std::to_string(frames) + ",\"totalMs\":";
    AppendNum(out, TotalMs());
    out += ",\"nodes\":[";
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const FlameNode& n = nodes[i];
        if (i) out += ',';
        out += "{\"id\":" + std::to_string(i) + ",\"name\":";
        AppendJsonString(out, n.name);
        out += ",\"parent\":" + std::to_string(n.parent) + ",\"depth\":" + std::to_string(n.depth)
             + ",\"inclusiveMs\":";
        AppendNum(out, n.inclusiveMs);
        out += ",\"selfMs\":"; AppendNum(out, n.selfMs);
        out += ",\"calls\":" + std::to_string(n.calls) + ",\"children\":[";
        for (std::size_t c = 0; c < n.children.size(); ++c) { if (c) out += ','; out += std::to_string(n.children[c]); }
        out += "]}";
    }
    out += "]}";
    return out;
}

static void LayoutWalk(const FlameGraph& g, int node, double x0, double x1, double minFraction,
                       std::vector<FlameGraph::Rect>& out) {
    const double total = g.TotalMs();
    double x = x0;
    for (int c : g.nodes[(std::size_t)node].children) {
        const FlameNode& cn = g.nodes[(std::size_t)c];
        double w = total > 0.0 ? cn.inclusiveMs / total : 0.0;
        if (w < minFraction || w <= 0.0) continue;
        double cx1 = x + w;
        if (cx1 > x1) cx1 = x1;
        FlameGraph::Rect r;
        r.node = c; r.depth = cn.depth; r.x0 = x; r.x1 = cx1;
        out.push_back(r);
        LayoutWalk(g, c, x, cx1, minFraction, out);
        x = cx1;
    }
}

std::vector<FlameGraph::Rect> FlameGraph::Layout(double minFraction) const {
    std::vector<Rect> out;
    if (nodes.size() > 1 && TotalMs() > 0.0) LayoutWalk(*this, 0, 0.0, 1.0, minFraction, out);
    return out;
}

// ---------------------------------------------------------------------------
// Export
// ---------------------------------------------------------------------------

static void AppendDistJson(std::string& out, const Distribution& d) {
    out += "{\"count\":" + std::to_string(d.count) + ",\"min\":"; AppendNum(out, d.min);
    out += ",\"avg\":"; AppendNum(out, d.avg);
    out += ",\"max\":"; AppendNum(out, d.max);
    out += ",\"p50\":"; AppendNum(out, d.p50);
    out += ",\"p90\":"; AppendNum(out, d.p90);
    out += ",\"p95\":"; AppendNum(out, d.p95);
    out += ",\"p99\":"; AppendNum(out, d.p99);
    out += '}';
}

std::string FrameHistory::ExportText(std::size_t first, std::size_t last) const {
    if (!Range(first, last)) return "(no frames)\n";
    std::string out;
    char buf[256];
    for (std::size_t i = first; i <= last; ++i) {
        const FrameRecord& f = At(i);
        std::snprintf(buf, sizeof(buf), "Frame #%llu (index %zu): total %.3f ms, cpu %.3f ms, gpu %.3f ms\n",
                      (unsigned long long)f.sequence, i, f.totalMs, f.cpuMs, f.gpuMs);
        out += buf;
        for (const FrameScope& s : f.scopes) {
            out.append((std::size_t)s.depth * 2 + 2, ' ');
            std::snprintf(buf, sizeof(buf), "  %.3f ms (self %.3f) x%d\n", s.inclusiveMs, s.selfMs, s.calls);
            out += s.name; out += buf;
        }
    }
    Distribution d = SeriesStats(Series::Total, first, last);
    std::snprintf(buf, sizeof(buf), "Summary over %zu frame(s): frame min %.3f avg %.3f p95 %.3f p99 %.3f max %.3f ms (%.1f fps)\n",
                  last - first + 1, d.min, d.avg, d.p95, d.p99, d.max, d.avg > 0.0 ? 1000.0 / d.avg : 0.0);
    out += buf;
    std::vector<ScopeStats> all = AllScopeStats(Metric::Self, first, last);
    std::size_t shown = 0;
    for (const ScopeStats& s : all) {
        if (shown++ >= 20) break;
        std::snprintf(buf, sizeof(buf), "  self avg %8.3f  p95 %8.3f  max %8.3f ms  (%zu/%zu frames)  ",
                      s.dist.avg, s.dist.p95, s.dist.max, s.framesPresent, s.framesTotal);
        out += buf; out += s.name; out += '\n';
    }
    return out;
}

std::string FrameHistory::ExportJson(std::size_t first, std::size_t last) const {
    std::string out;
    if (!Range(first, last)) return "{\"frameCount\":0,\"frames\":[],\"summary\":{}}";
    out = "{\"frameCount\":" + std::to_string(last - first + 1) + ",\"frames\":[";
    for (std::size_t i = first; i <= last; ++i) {
        const FrameRecord& f = At(i);
        if (i > first) out += ',';
        out += "{\"index\":" + std::to_string(i) + ",\"sequence\":" + std::to_string(f.sequence) + ",\"startMs\":";
        AppendNum(out, f.startMs);
        out += ",\"totalMs\":"; AppendNum(out, f.totalMs);
        out += ",\"cpuMs\":"; AppendNum(out, f.cpuMs);
        out += ",\"gpuMs\":"; AppendNum(out, f.gpuMs);
        out += ",\"objects\":" + std::to_string(f.objects) + ",\"triangles\":" + std::to_string(f.triangles)
             + ",\"particles\":" + std::to_string(f.particles) + ",\"scopes\":[";
        for (std::size_t si = 0; si < f.scopes.size(); ++si) {
            const FrameScope& s = f.scopes[si];
            if (si) out += ',';
            out += "{\"name\":"; AppendJsonString(out, s.name);
            out += ",\"depth\":" + std::to_string(s.depth) + ",\"parent\":" + std::to_string(s.parent)
                 + ",\"calls\":" + std::to_string(s.calls) + ",\"inclusiveMs\":";
            AppendNum(out, s.inclusiveMs);
            out += ",\"selfMs\":"; AppendNum(out, s.selfMs);
            out += ",\"startMs\":"; AppendNum(out, s.startMs);
            out += '}';
        }
        out += "]}";
    }
    out += "],\"summary\":{\"frame\":";
    AppendDistJson(out, SeriesStats(Series::Total, first, last));
    out += ",\"scopes\":[";
    bool firstScope = true;
    for (const ScopeStats& s : AllScopeStats(Metric::Self, first, last)) {
        if (!firstScope) out += ',';
        firstScope = false;
        out += "{\"name\":"; AppendJsonString(out, s.name);
        out += ",\"framesPresent\":" + std::to_string(s.framesPresent) + ",\"self\":";
        AppendDistJson(out, s.dist);
        out += '}';
    }
    out += "]}}";
    return out;
}

int FrameHistory::ExportChromeTrace(std::string& out, std::size_t first, std::size_t last) const {
    out = "{\"displayTimeUnit\":\"ms\",\"traceEvents\":[";
    if (!Range(first, last)) { out += "]}"; return 0; }
    int events = 0;
    for (std::size_t i = first; i <= last; ++i) {
        const FrameRecord& f = At(i);
        if (events++) out += ',';
        out += "{\"name\":\"Frame " + std::to_string(f.sequence) + "\",\"cat\":\"okay.frame\",\"ph\":\"X\",\"ts\":";
        AppendNum(out, f.startMs * 1000.0);
        out += ",\"dur\":"; AppendNum(out, f.totalMs * 1000.0);
        out += ",\"pid\":1,\"tid\":0,\"args\":{\"frame\":" + std::to_string(f.sequence) + ",\"cpu_ms\":";
        AppendNum(out, f.cpuMs);
        out += ",\"gpu_ms\":"; AppendNum(out, f.gpuMs);
        out += "}}";
        for (const FrameScope& s : f.scopes) {
            out += ",{\"name\":"; AppendJsonString(out, s.name);
            out += ",\"cat\":\"okay\",\"ph\":\"X\",\"ts\":"; AppendNum(out, (f.startMs + s.startMs) * 1000.0);
            out += ",\"dur\":"; AppendNum(out, s.inclusiveMs * 1000.0);
            out += ",\"pid\":1,\"tid\":1,\"args\":{\"frame\":" + std::to_string(f.sequence)
                 + ",\"calls\":" + std::to_string(s.calls) + ",\"depth\":" + std::to_string(s.depth) + ",\"self_ms\":";
            AppendNum(out, s.selfMs);
            out += "}}";
            ++events;
        }
    }
    out += "]}";
    return events;
}

bool FrameHistory::ExportChromeTraceToFile(const std::string& path, std::size_t first, std::size_t last) const {
    std::string s; ExportChromeTrace(s, first, last);
    return WriteTextFile(path, s);
}
bool FrameHistory::ExportJsonToFile(const std::string& path, std::size_t first, std::size_t last) const {
    return WriteTextFile(path, ExportJson(first, last));
}
bool FrameHistory::ExportTextToFile(const std::string& path, std::size_t first, std::size_t last) const {
    return WriteTextFile(path, ExportText(first, last));
}

} // namespace okay
