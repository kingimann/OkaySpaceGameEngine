#include "okay/Graphics/TtfRaster.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace okay {
namespace ttf {

// ---- Path plumbing -------------------------------------------------------------
namespace detail {

// Where an outline goes: the rasterizer, or a bounds collector. Coordinates are
// font units, y up.
class PathSink {
public:
    virtual ~PathSink() = default;
    virtual void Move(float x, float y) = 0;
    virtual void Line(float x, float y) = 0;
    virtual void Quad(float cx, float cy, float x, float y) = 0;
    virtual void Cubic(float c1x, float c1y, float c2x, float c2y, float x, float y) = 0;
    virtual void Close() = 0;
};

// A glyf glyph as points: contours end at ends[i] (indices into pts).
struct PointList {
    struct P { float x, y; bool on; };
    std::vector<P> pts;
    std::vector<int> ends;
};

// Type 2 charstring interpreter state.
struct CsState {
    double st[96];
    int sp = 0;
    double x = 0, y = 0;
    bool open = false, done = false;
    int nStems = 0;
    int steps = 0;
    double trans[32];
    PathSink* sink = nullptr;
};

} // namespace detail

namespace {

using detail::PathSink;
using detail::PointList;
using detail::CsState;

constexpr int kMaxBox = 4096;          // a glyph bitmap is never wider / taller than this
constexpr int kPointBudget = 200000;   // points (and components) one glyph may expand to
constexpr int kMaxCompositeDepth = 8;
constexpr int kMaxSubrDepth = 10;
constexpr int kMaxCsSteps = 400000;

bool Finite(float v) { return v > -1e7f && v < 1e7f; }   // false for NaN too
// Floor and ceiling to int for the small, finite bitmap coordinates the rasterizer works in
// (without a library call on CPUs where the compiler has no rounding instruction).
inline int IFloor(float v) { const int i = (int)v; return i - (v < (float)i ? 1 : 0); }
inline int ICeil(float v) { const int i = (int)v; return i + (v > (float)i ? 1 : 0); }

// ---- Exact-area coverage accumulation ---------------------------------------------
// Each line segment adds, to the cells it crosses, the signed area it sweeps to the
// right of itself; a running sum along the row then gives each pixel's covered
// fraction (|sum|, clamped to 1).
//
// That sum is exact while contours do not overlap. Where two contours of the same
// direction overlap (a composite's accent touching its letter, a font drawn with
// overlapping strokes), a pixel the edges of both cross gets the SUM of the two areas
// instead of the area of their union, so the edge of the overlap comes out too dark.
// So rows are checked for winding 2 (4 sample lines per row; one
// contour can cross itself too, as JetBrains Mono's sharp s does), and a row where the
// running sum goes past 1 or a sample line meets winding 2 is drawn again, with its neighbours, by a non-zero
// winding scanline: 16 sample lines per pixel row, each with exact horizontal coverage
// from the sorted edge crossings (the general non-zero scanline method). Every other row
// keeps the exact area.
struct Edge { float x0, y0, y1, dxdy, dir; };   // a line in bitmap space, y0 < y1
// Buffers one thread's rasterizer reuses from glyph to glyph (no allocation per glyph).
struct RasterScratch {
    std::vector<float> acc;
    std::vector<Edge> edges;
    std::vector<std::uint8_t> overlap;
    std::vector<int> at, fill;
    std::vector<std::pair<float, float>> cx;
};
RasterScratch& Scratch() { static thread_local RasterScratch s; return s; }
void ReplayInto(const detail::Recorder& path, PathSink& sink);   // (below, after Recorder)

class Raster : public PathSink {
public:
    Raster(int w, int h, float scale, int ox, int oy)
        : m_w(w), m_h(h), m_stride(w + 2), m_scale(scale), m_ox((float)ox), m_oy((float)oy),
          m_s(Scratch()), m_acc(m_s.acc), m_edges(m_s.edges) {
        m_acc.assign((std::size_t)(w + 2) * (std::size_t)h, 0.0f);
        m_edges.clear();
    }

    void Move(float x, float y) override {
        CloseSub();
        Map(x, y, m_cx, m_cy);
        m_sx = m_cx; m_sy = m_cy;
        m_open = true;
    }
    void Line(float x, float y) override {
        float px, py; Map(x, y, px, py);
        if (!m_open) { m_sx = m_cx = px; m_sy = m_cy = py; m_open = true; return; }
        Seg(m_cx, m_cy, px, py);
        PushEdge(m_cx, m_cy, px, py);
        m_cx = px; m_cy = py;
    }
    void Quad(float cx, float cy, float x, float y) override {
        float qx, qy, px, py; Map(cx, cy, qx, qy); Map(x, y, px, py);
        if (!m_open) { m_sx = m_cx = px; m_sy = m_cy = py; m_open = true; return; }
        const float dd = std::fabs(m_cx - 2 * qx + px) + std::fabs(m_cy - 2 * qy + py);
        // Twice: finely for the coverage, coarsely (or finely, when collecting) for the edge list.
        for (int pass = m_accumulate ? 0 : 1; pass < 2; ++pass) {
            const int n = Steps(dd * 0.25f / (pass == 0 ? kTol : m_edgeTol));
            float ax = m_cx, ay = m_cy;
            for (int i = 1; i <= n; ++i) {
                const float t = (float)i / (float)n, u = 1 - t;
                const float bx = u * u * m_cx + 2 * u * t * qx + t * t * px;
                const float by = u * u * m_cy + 2 * u * t * qy + t * t * py;
                if (pass == 0) Seg(ax, ay, bx, by); else PushEdge(ax, ay, bx, by);
                ax = bx; ay = by;
            }
        }
        m_cx = px; m_cy = py;
    }
    void Cubic(float c1x, float c1y, float c2x, float c2y, float x, float y) override {
        float ax, ay, bx, by, px, py;
        Map(c1x, c1y, ax, ay); Map(c2x, c2y, bx, by); Map(x, y, px, py);
        if (!m_open) { m_sx = m_cx = px; m_sy = m_cy = py; m_open = true; return; }
        const float d1 = std::fabs(m_cx - 2 * ax + bx) + std::fabs(m_cy - 2 * ay + by);
        const float d2 = std::fabs(ax - 2 * bx + px) + std::fabs(ay - 2 * by + py);
        for (int pass = m_accumulate ? 0 : 1; pass < 2; ++pass) {   // coverage, then the edge list
            const int n = Steps(0.75f * std::max(d1, d2) / (pass == 0 ? kTol : m_edgeTol));
            float lx = m_cx, ly = m_cy;
            for (int i = 1; i <= n; ++i) {
                const float t = (float)i / (float)n, u = 1 - t;
                const float k0 = u * u * u, k1 = 3 * u * u * t, k2 = 3 * u * t * t, k3 = t * t * t;
                const float qx = k0 * m_cx + k1 * ax + k2 * bx + k3 * px;
                const float qy = k0 * m_cy + k1 * ay + k2 * by + k3 * py;
                if (pass == 0) Seg(lx, ly, qx, qy); else PushEdge(lx, ly, qx, qy);
                lx = qx; ly = qy;
            }
        }
        m_cx = px; m_cy = py;
    }
    void Close() override { CloseSub(); }

    // Write coverage 0..255 for the w x h area. `path` (the same outline again) gives the
    // scanline pass finely flattened edges; without it the coarse list is used.
    void Finish(std::uint8_t* out, int stride, const detail::Recorder* path = nullptr) {
        CloseSub();
        std::vector<std::uint8_t>& overlap = m_s.overlap;
        overlap.assign((std::size_t)m_h, 0);
        bool any = false;
        for (int y = 0; y < m_h; ++y) {
            const float* row = &m_acc[(std::size_t)y * m_stride];
            std::uint8_t* o = out + (std::size_t)y * stride;
            float sum = 0;
            for (int x = 0; x < m_w; ++x) {
                sum += row[x];
                float v = std::fabs(sum);
                if (v > 1.0f + kOverlap) { overlap[(std::size_t)y] = 1; any = true; }
                if (v > 1.0f) v = 1.0f;
                o[x] = (std::uint8_t)(v * 255.0f + 0.5f);
            }
        }
        if (m_edges.empty()) return;
        // Winding on 4 sample lines per row: the crossings of each line, sorted by x, summed
        // left to right (a winding-2 span between two sample lines can be missed; it is
        // under a quarter pixel tall and barely shows).
        {
            const int lines = m_h * kCheckLines;
            auto lineRange = [&](const Edge& e, int& l0, int& l1) {
                l0 = std::max(ICeil(e.y0 * kCheckLines - 0.5f), 0);
                l1 = std::min(ICeil(e.y1 * kCheckLines - 0.5f), lines);
            };
            std::vector<int>& at = m_s.at;
            at.assign((std::size_t)lines + 1, 0);
            for (const Edge& e : m_edges) { int l0, l1; lineRange(e, l0, l1); for (int l = l0; l < l1; ++l) ++at[(std::size_t)l + 1]; }
            for (int l = 0; l < lines; ++l) at[(std::size_t)l + 1] += at[(std::size_t)l];
            std::vector<std::pair<float, float>>& cx = m_s.cx;
            cx.resize((std::size_t)at[(std::size_t)lines]);
            {
                std::vector<int>& fill = m_s.fill;
                fill.assign(at.begin(), at.end() - 1);
                for (const Edge& e : m_edges) {
                    int l0, l1; lineRange(e, l0, l1);
                    for (int l = l0; l < l1; ++l)
                        cx[(std::size_t)fill[(std::size_t)l]++] = {e.x0 + (((float)l + 0.5f) / (float)kCheckLines - e.y0) * e.dxdy, e.dir};
                }
            }
            for (int l = 0; l < lines; ++l) {
                const int y = l / kCheckLines;
                const int a = at[(std::size_t)l], b = at[(std::size_t)l + 1];
                if (overlap[(std::size_t)y] || b - a < 4) continue;   // winding 2 needs at least 4 crossings
                if (b - a == 4) {   // the common case (a letter with a counter): a 5-swap network
                    auto* c = &cx[(std::size_t)a];
                    auto sw = [](std::pair<float, float>& p, std::pair<float, float>& q) { if (q.first < p.first) std::swap(p, q); };
                    sw(c[0], c[1]); sw(c[2], c[3]); sw(c[0], c[2]); sw(c[1], c[3]); sw(c[1], c[2]);
                } else {
                    std::sort(cx.begin() + a, cx.begin() + b);
                }
                float w = 0;
                for (int k = a; k < b; ++k)
                    if (std::fabs(w += cx[(std::size_t)k].second) > 1.5f) { overlap[(std::size_t)y] = 1; any = true; break; }
            }
        }
        if (!any) return;
        if (path) {   // the outline again, flattened finely, edges only
            m_edges.clear();
            m_accumulate = false; m_edgeTol = kTol;
            ReplayInto(*path, *this);
            CloseSub();
        }
        // The edges each row touches (index lists, one pass to count, one to fill).
        std::vector<int> start((std::size_t)m_h + 1, 0), idx;
        for (const Edge& e : m_edges)
            for (int r = std::max(0, (int)e.y0); r < std::min(m_h, ICeil(e.y1)); ++r) ++start[(std::size_t)r + 1];
        for (int r = 0; r < m_h; ++r) start[(std::size_t)r + 1] += start[(std::size_t)r];
        idx.resize((std::size_t)start[(std::size_t)m_h]);
        {
            std::vector<int> fill(start.begin(), start.end() - 1);
            for (std::size_t i = 0; i < m_edges.size(); ++i) {
                const Edge& e = m_edges[i];
                for (int r = std::max(0, (int)e.y0); r < std::min(m_h, ICeil(e.y1)); ++r)
                    idx[(std::size_t)fill[(std::size_t)r]++] = (int)i;
            }
        }
        std::vector<std::pair<float, float>> xs;   // crossing x, winding step
        auto crossings = [&](int row, float sy) {
            xs.clear();
            for (int j = start[(std::size_t)row]; j < start[(std::size_t)row + 1]; ++j) {
                const Edge& e = m_edges[(std::size_t)idx[(std::size_t)j]];
                if (sy >= e.y0 && sy < e.y1) xs.push_back({e.x0 + (sy - e.y0) * e.dxdy, e.dir});
            }
            std::sort(xs.begin(), xs.end());
        };
        std::vector<float> cov((std::size_t)m_w + 1);
        for (int y = 0; y < m_h; ++y) {
            const bool near = overlap[(std::size_t)y] || (y > 0 && overlap[(std::size_t)y - 1]) ||
                              (y + 1 < m_h && overlap[(std::size_t)y + 1]);
            if (!near) continue;
            std::fill(cov.begin(), cov.end(), 0.0f);
            for (int k = 0; k < kSamples; ++k) {
                crossings(y, (float)y + ((float)k + 0.5f) / (float)kSamples);
                float wind = 0;
                for (std::size_t i = 0; i + 1 < xs.size(); ++i) {
                    wind += xs[i].second;
                    if (std::fabs(wind) < 0.5f) continue;   // outside: winding 0
                    AddSpan(cov, xs[i].first, xs[i + 1].first, 1.0f / (float)kSamples);
                }
            }
            std::uint8_t* o = out + (std::size_t)y * stride;
            for (int x = 0; x < m_w; ++x) o[x] = (std::uint8_t)(std::min(cov[(std::size_t)x], 1.0f) * 255.0f + 0.5f);
        }
    }

private:
    static constexpr float kTol = 0.01f;   // flattening tolerance, pixels
    static constexpr float kOverlap = 0.02f;   // running sum this far past 1: winding 2 somewhere
    static constexpr int kSamples = 16;        // sample lines per pixel row in overlapping rows
    static constexpr int kCheckLines = 4;      // lines per row that look for winding 2
    static constexpr float kEdgeTol = 0.2f;    // the edge list for those checks may be coarse
    // Coverage of [a, b) on one sample line, `weight` per whole pixel.
    void AddSpan(std::vector<float>& cov, float a, float b, float weight) const {
        a = std::max(a, 0.0f); b = std::min(b, (float)m_w);
        if (!(b > a)) return;
        const int ia = (int)a, ib = (int)b;
        if (ia == ib) { cov[(std::size_t)ia] += (b - a) * weight; return; }
        cov[(std::size_t)ia] += ((float)(ia + 1) - a) * weight;
        for (int x = ia + 1; x < ib; ++x) cov[(std::size_t)x] += weight;
        if (ib < m_w) cov[(std::size_t)ib] += (b - (float)ib) * weight;
    }
    static int Steps(float v) {
        if (!(v > 1.0f)) return 1;
        const float n = std::ceil(std::sqrt(v));
        return n > 128.0f ? 128 : (int)n;
    }
    void Map(float x, float y, float& px, float& py) const {
        px = x * m_scale - m_ox;
        py = -y * m_scale - m_oy;
    }
    void CloseSub() {
        if (m_open) {
            Seg(m_cx, m_cy, m_sx, m_sy);
            PushEdge(m_cx, m_cy, m_sx, m_sy);
            m_cx = m_sx; m_cy = m_sy;
        }
        m_open = false;
    }

    // A line for the winding checks and the scanline pass, clipped to the rows.
    void PushEdge(float x0, float y0, float x1, float y1) {
        if (!Finite(x0) || !Finite(y0) || !Finite(x1) || !Finite(y1) || y0 == y1) return;
        float dir = 1.0f;
        if (y0 > y1) { std::swap(x0, x1); std::swap(y0, y1); dir = -1.0f; }
        const float H = (float)m_h;
        if (y1 <= 0.0f || y0 >= H) return;
        const float dxdy = (x1 - x0) / (y1 - y0);
        if (y0 < 0.0f) { x0 += -y0 * dxdy; y0 = 0.0f; }
        if (y1 > H) y1 = H;
        m_edges.push_back({x0, y0, y1, dxdy, dir});
    }

    // A line in bitmap space, into the coverage. Clip to the rows, then to the columns.
    void Seg(float x0, float y0, float x1, float y1) {
        if (!m_accumulate) return;
        if (!Finite(x0) || !Finite(y0) || !Finite(x1) || !Finite(y1)) return;
        if (y0 == y1) return;
        float dir = 1.0f;
        if (y0 > y1) { std::swap(x0, x1); std::swap(y0, y1); dir = -1.0f; }
        const float H = (float)m_h;
        if (y1 <= 0.0f || y0 >= H) return;
        const float dxdy = (x1 - x0) / (y1 - y0);
        if (y0 < 0.0f) { x0 += -y0 * dxdy; y0 = 0.0f; }
        if (y1 > H) { x1 = x0 + (H - y0) * dxdy; y1 = H; }
        // Columns: split where the segment crosses x = 0 and x = w (the box holds the outline,
        // so nearly every segment is inside and goes straight in).
        const float W = (float)m_w;
        if (x0 >= 0.0f && x1 >= 0.0f && x0 <= W && x1 <= W) { Accum(x0, y0, x1, y1, dir); return; }
        float ts[4]; int nt = 0;
        ts[nt++] = 0.0f;
        const float dx = x1 - x0;
        if (dx != 0.0f) {
            float t0 = (0.0f - x0) / dx, t1 = (W - x0) / dx;
            if (t0 > t1) std::swap(t0, t1);
            if (t0 > 0.0f && t0 < 1.0f) ts[nt++] = t0;
            if (t1 > 0.0f && t1 < 1.0f) ts[nt++] = t1;
        }
        ts[nt++] = 1.0f;
        for (int i = 0; i + 1 < nt; ++i) {
            const float ax = x0 + dx * ts[i], ay = y0 + (y1 - y0) * ts[i];
            const float bx = x0 + dx * ts[i + 1], by = y0 + (y1 - y0) * ts[i + 1];
            const float mid = 0.5f * (ax + bx);
            if (mid >= W) continue;                         // right of the bitmap: no pixel changes
            if (mid <= 0.0f) Accum(0.0f, ay, 0.0f, by, dir);   // left of it: covers all to its right
            else Accum(std::min(std::max(ax, 0.0f), W), ay, std::min(std::max(bx, 0.0f), W), by, dir);
        }
    }

    // y0 < y1 inside [0,h], x inside [0,w].
    void Accum(float x0, float y0, float x1, float y1, float dir) {
        if (!(y1 > y0)) return;
        const float dxdy = (x1 - x0) / (y1 - y0);
        float x = x0;
        const int rowFirst = (int)y0;
        int rowLast = ICeil(y1);
        if (rowLast > m_h) rowLast = m_h;
        for (int row = rowFirst; row < rowLast; ++row) {
            float* a = &m_acc[(std::size_t)row * m_stride];
            const float top = std::max((float)row, y0);
            const float bot = std::min((float)row + 1.0f, y1);
            const float dy = bot - top;
            if (dy <= 0.0f) continue;
            float xn = x + dxdy * dy;
            xn = std::min(std::max(xn, 0.0f), (float)m_w);
            const float d = dy * dir;
            const float xa = std::min(x, xn), xb = std::max(x, xn);
            const float fl = (float)IFloor(xa);
            const int i0 = (int)fl;
            const int i1 = ICeil(xb);
            if (i1 <= i0 + 1) {
                // Within one pixel column: the part of the row to the right of the
                // segment's mean x is covered.
                const float xm = 0.5f * (x + xn) - fl;
                a[i0] += d - d * xm;
                a[i0 + 1] += d * xm;
            } else {
                // Spans several columns: integrate the covered area per column.
                const float s = 1.0f / (xb - xa);
                const float x0f = xa - fl;
                const float a0 = 0.5f * s * (1.0f - x0f) * (1.0f - x0f);
                const float x1f = xb - (float)i1 + 1.0f;
                const float am = 0.5f * s * x1f * x1f;
                a[i0] += d * a0;
                if (i1 == i0 + 2) {
                    a[i0 + 1] += d * (1.0f - a0 - am);
                } else {
                    const float a1 = s * (1.5f - x0f);
                    a[i0 + 1] += d * (a1 - a0);
                    for (int xi = i0 + 2; xi < i1 - 1; ++xi) a[xi] += d * s;
                    const float a2 = a1 + (float)(i1 - i0 - 3) * s;
                    a[i1 - 1] += d * (1.0f - a2 - am);
                }
                a[i1] += d * am;
            }
            x = xn;
        }
    }

    int m_w, m_h, m_stride;
    float m_scale, m_ox, m_oy;
    bool m_accumulate = true;        // false: only collect edges (the scanline pass's fine list)
    float m_edgeTol = kEdgeTol;      // flattening tolerance of the edge list
    RasterScratch& m_s;
    std::vector<float>& m_acc;
    std::vector<Edge>& m_edges;
    float m_cx = 0, m_cy = 0, m_sx = 0, m_sy = 0;
    bool m_open = false;
};

} // namespace

namespace detail {
// Collects the extent of every point (control points included).
class Bounds : public PathSink {
public:
    float xmin = 1e30f, ymin = 1e30f, xmax = -1e30f, ymax = -1e30f;
    bool any = false;
    void Move(float x, float y) override { Add(x, y); }
    void Line(float x, float y) override { Add(x, y); }
    void Quad(float cx, float cy, float x, float y) override { Add(cx, cy); Add(x, y); }
    void Cubic(float a, float b, float c, float d, float x, float y) override { Add(a, b); Add(c, d); Add(x, y); }
    void Close() override {}
private:
    void Add(float x, float y) {
        if (!Finite(x) || !Finite(y)) return;
        any = true;
        xmin = std::min(xmin, x); xmax = std::max(xmax, x);
        ymin = std::min(ymin, y); ymax = std::max(ymax, y);
    }
};

// A path kept to be drawn later (and its point box): the outline is parsed once per glyph.
class Recorder : public PathSink {
public:
    struct Cmd { std::uint8_t op; float v[6]; };
    Recorder() : cmds(Buffer()) { cmds.clear(); }
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;
    std::vector<Cmd>& cmds;   // one per thread, reused (a Recorder is never nested)
    Bounds box;
    void Move(float x, float y) override { cmds.push_back({0, {x, y, 0, 0, 0, 0}}); box.Move(x, y); }
    void Line(float x, float y) override { cmds.push_back({1, {x, y, 0, 0, 0, 0}}); box.Line(x, y); }
    void Quad(float cx, float cy, float x, float y) override { cmds.push_back({2, {cx, cy, x, y, 0, 0}}); box.Quad(cx, cy, x, y); }
    void Cubic(float a, float b, float c, float d, float x, float y) override { cmds.push_back({3, {a, b, c, d, x, y}}); box.Cubic(a, b, c, d, x, y); }
    void Close() override { cmds.push_back({4, {0, 0, 0, 0, 0, 0}}); }
    static std::vector<Cmd>& Buffer() { static thread_local std::vector<Cmd> b; return b; }
    void Replay(PathSink& s) const {
        for (const Cmd& c : cmds) {
            switch (c.op) {
            case 0: s.Move(c.v[0], c.v[1]); break;
            case 1: s.Line(c.v[0], c.v[1]); break;
            case 2: s.Quad(c.v[0], c.v[1], c.v[2], c.v[3]); break;
            case 3: s.Cubic(c.v[0], c.v[1], c.v[2], c.v[3], c.v[4], c.v[5]); break;
            default: s.Close(); break;
            }
        }
    }
};
} // namespace detail

namespace {
using detail::Bounds;
void ReplayInto(const detail::Recorder& path, PathSink& sink) { path.Replay(sink); }

// TrueType contours (on/off-curve points, implied on-curve midpoints) -> path.
void EmitContours(const PointList& pl, PathSink& sink) {
    int s = 0;
    for (std::size_t c = 0; c < pl.ends.size(); ++c) {
        const int e = pl.ends[c];
        const int first = s;
        s = e + 1;
        const int n = e - first + 1;
        if (n < 2 || e >= (int)pl.pts.size()) continue;
        const PointList::P* p = &pl.pts[(std::size_t)first];
        float sx, sy;
        int begin, last;   // points walked after the start, inclusive range
        if (p[0].on) { sx = p[0].x; sy = p[0].y; begin = 1; last = n - 1; }
        else if (p[n - 1].on) { sx = p[n - 1].x; sy = p[n - 1].y; begin = 0; last = n - 2; }
        else { sx = 0.5f * (p[0].x + p[n - 1].x); sy = 0.5f * (p[0].y + p[n - 1].y); begin = 0; last = n - 1; }
        sink.Move(sx, sy);
        bool haveCtl = false;
        float qx = 0, qy = 0;
        for (int i = begin; i <= last; ++i) {
            if (p[i].on) {
                if (haveCtl) { sink.Quad(qx, qy, p[i].x, p[i].y); haveCtl = false; }
                else sink.Line(p[i].x, p[i].y);
            } else {
                if (haveCtl) sink.Quad(qx, qy, 0.5f * (qx + p[i].x), 0.5f * (qy + p[i].y));
                qx = p[i].x; qy = p[i].y; haveCtl = true;
            }
        }
        if (haveCtl) sink.Quad(qx, qy, sx, sy);
        sink.Close();
    }
}

int Bias(std::uint32_t count) { return count < 1240 ? 107 : (count < 33900 ? 1131 : 32768); }

} // namespace

// ---- Face: reading ------------------------------------------------------------------
std::uint32_t Face::U16(std::size_t o) const {
    return Has(o, 2) ? ((std::uint32_t)m_d[o] << 8) | m_d[o + 1] : 0u;
}
std::uint32_t Face::U32(std::size_t o) const {
    return Has(o, 4) ? ((std::uint32_t)m_d[o] << 24) | ((std::uint32_t)m_d[o + 1] << 16) |
                       ((std::uint32_t)m_d[o + 2] << 8) | m_d[o + 3] : 0u;
}

static std::uint32_t Tag(const char* t) {
    return ((std::uint32_t)(std::uint8_t)t[0] << 24) | ((std::uint32_t)(std::uint8_t)t[1] << 16) |
           ((std::uint32_t)(std::uint8_t)t[2] << 8) | (std::uint32_t)(std::uint8_t)t[3];
}

int Face::FaceCount(const std::uint8_t* data, std::size_t size) {
    if (!data || size < 12) return 0;
    Face f; f.m_d = data; f.m_n = size;
    const std::uint32_t tag = f.U32(0);
    if (tag == Tag("ttcf")) {
        const std::uint32_t n = f.U32(8);
        if (n == 0 || n > 4096 || !f.Has(12, (std::size_t)n * 4)) return 0;
        return (int)n;
    }
    if (tag == 0x00010000u || tag == Tag("true") || tag == Tag("OTTO")) return 1;
    return 0;
}

bool Face::FindTable(std::uint32_t dir, const char* tag, Table& t) const {
    const std::uint32_t want = Tag(tag);
    const std::uint32_t num = U16(dir + 4);
    for (std::uint32_t i = 0; i < num; ++i) {
        const std::size_t rec = (std::size_t)dir + 12 + (std::size_t)i * 16;
        if (!Has(rec, 16)) return false;
        if (U32(rec) != want) continue;
        const std::uint32_t off = U32(rec + 8), len = U32(rec + 12);
        if (off > m_n) return false;
        t.off = off;
        t.len = (std::uint32_t)std::min<std::size_t>(len, m_n - off);   // a truncated file keeps what is there
        return true;
    }
    return false;
}

bool Face::Load(const std::uint8_t* data, std::size_t size, int index) {
    *this = Face();
    if (!data || size < 12 || size > 0x7FFFFFFFu || index < 0) return false;
    m_d = data; m_n = size;
    std::uint32_t dir = 0;
    const int count = FaceCount(data, size);
    if (count == 0 || index >= count) { m_d = nullptr; m_n = 0; return false; }
    if (U32(0) == Tag("ttcf")) dir = U32(12 + (std::size_t)index * 4);
    if (!Has(dir, 12)) { m_d = nullptr; m_n = 0; return false; }
    const std::uint32_t sig = U32(dir);
    auto fail = [&]() { *this = Face(); return false; };
    if (sig != 0x00010000u && sig != Tag("true") && sig != Tag("OTTO")) return fail();

    if (!FindTable(dir, "head", m_head) || m_head.len < 54) return fail();
    if (!FindTable(dir, "hhea", m_hhea) || m_hhea.len < 36) return fail();
    if (!FindTable(dir, "maxp", m_maxp) || m_maxp.len < 6) return fail();
    if (!FindTable(dir, "hmtx", m_hmtx) || m_hmtx.len < 4) return fail();
    if (!FindTable(dir, "cmap", m_cmap) || m_cmap.len < 4) return fail();
    const bool haveGlyf = FindTable(dir, "glyf", m_glyf) && FindTable(dir, "loca", m_loca);
    const bool haveCff = FindTable(dir, "CFF ", m_cffT);
    if (!haveGlyf && !haveCff) return fail();
    m_cff = !haveGlyf;

    m_upem = (int)U16(m_head.off + 18);
    if (m_upem < 1 || m_upem > 16384) return fail();
    m_locaLong = I16(m_head.off + 50) != 0;
    m_numGlyphs = (int)U16(m_maxp.off + 4);
    if (m_numGlyphs < 1) return fail();
    m_asc = I16(m_hhea.off + 4);
    m_desc = I16(m_hhea.off + 6);
    m_gap = I16(m_hhea.off + 8);
    m_numHM = (int)U16(m_hhea.off + 34);
    m_numHM = std::min<int>(m_numHM, (int)(m_hmtx.len / 4));
    if (m_numHM < 1) return fail();

    PickCmap();   // a font without a usable cmap still loads: every character is "missing"
    if (m_cff && !LoadCff()) return fail();
    LoadKerning(dir);   // a damaged GPOS / kern table only loses kerning
    m_valid = true;
    return true;
}

bool Face::PickCmap() {
    const std::size_t base = m_cmap.off;
    const std::uint32_t n = U16(base + 2);
    int best = 0;
    for (std::uint32_t i = 0; i < n; ++i) {
        const std::size_t rec = base + 4 + (std::size_t)i * 8;
        if (!Has(rec, 8)) break;
        const std::uint32_t pid = U16(rec), eid = U16(rec + 2), off = U32(rec + 4);
        if (off >= m_cmap.len) continue;
        const std::size_t sub = base + off;
        const std::uint32_t fmt = U16(sub);
        if (fmt != 0 && fmt != 4 && fmt != 6 && fmt != 12 && fmt != 13) continue;
        int score = 0;
        if (pid == 3 && eid == 10) score = 60;
        else if (pid == 0 && (eid == 4 || eid == 6)) score = 55;
        else if (pid == 3 && eid == 1) score = 50;
        else if (pid == 0) score = 40;
        else if (pid == 3 && eid == 0) score = 30;
        else if (pid == 1 && eid == 0) score = 10;
        else continue;
        if (fmt == 12 || fmt == 13) score += 1;   // wins ties: covers beyond the BMP
        if (score > best) {
            best = score;
            m_cmapSub = sub; m_cmapFmt = (int)fmt;
            m_cmapSymbol = (pid == 3 && eid == 0);
        }
    }
    return best > 0;
}

int Face::CmapLookup(std::uint32_t cp) const {
    const std::size_t sub = m_cmapSub;
    switch (m_cmapFmt) {
    case 0:
        return cp < 256 ? (int)U8(sub + 6 + cp) : 0;
    case 6: {
        const std::uint32_t first = U16(sub + 6), cnt = U16(sub + 8);
        return (cp >= first && cp - first < cnt) ? (int)U16(sub + 10 + (std::size_t)(cp - first) * 2) : 0;
    }
    case 4: {
        if (cp > 0xFFFF) return 0;
        const std::uint32_t segX2 = U16(sub + 6);
        const std::uint32_t segs = segX2 / 2;
        if (segs == 0 || !Has(sub + 14, (std::size_t)segX2 * 4 + 2)) return 0;
        const std::size_t ends = sub + 14, starts = ends + segX2 + 2, deltas = starts + segX2, ranges = deltas + segX2;
        std::uint32_t lo = 0, hi = segs;   // first segment whose end >= cp
        while (lo < hi) {
            const std::uint32_t mid = (lo + hi) / 2;
            if (U16(ends + mid * 2) < cp) lo = mid + 1; else hi = mid;
        }
        if (lo >= segs) return 0;
        const std::uint32_t start = U16(starts + lo * 2);
        if (start > cp) return 0;
        const std::uint32_t delta = U16(deltas + lo * 2), ro = U16(ranges + lo * 2);
        if (ro == 0) return (int)((cp + delta) & 0xFFFF);
        const std::size_t addr = ranges + lo * 2 + ro + (std::size_t)(cp - start) * 2;
        if (!Has(addr, 2)) return 0;
        const std::uint32_t g = U16(addr);
        return g ? (int)((g + delta) & 0xFFFF) : 0;
    }
    case 12:
    case 13: {
        std::uint32_t ng = U32(sub + 12);
        const std::size_t avail = m_n > sub + 16 ? (m_n - (sub + 16)) / 12 : 0;
        if (ng > avail) ng = (std::uint32_t)avail;
        std::uint32_t lo = 0, hi = ng;
        while (lo < hi) {
            const std::uint32_t mid = (lo + hi) / 2;
            const std::size_t g = sub + 16 + (std::size_t)mid * 12;
            if (U32(g + 4) < cp) lo = mid + 1; else hi = mid;
        }
        if (lo >= ng) return 0;
        const std::size_t g = sub + 16 + (std::size_t)lo * 12;
        const std::uint32_t start = U32(g), glyph = U32(g + 8);
        if (start > cp) return 0;
        const std::uint32_t r = m_cmapFmt == 12 ? glyph + (cp - start) : glyph;
        return r > 0xFFFFu ? 0 : (int)r;
    }
    default: return 0;
    }
}

int Face::FindGlyph(std::uint32_t cp) const {
    if (!m_valid || m_cmapFmt < 0) return 0;
    int g = CmapLookup(cp);
    if (g == 0 && m_cmapSymbol && cp < 0x100) g = CmapLookup(0xF000u + cp);
    return (g > 0 && g < m_numGlyphs) ? g : 0;
}

float Face::ScaleForPixelHeight(float px) const {
    int h = m_asc - m_desc;
    if (h <= 0) h = m_upem;
    return h > 0 ? px / (float)h : 0.0f;
}

bool Face::HMetrics(int glyph, int& advance, int& lsb) const {
    advance = lsb = 0;
    if (!m_valid || glyph < 0 || glyph >= m_numGlyphs) return false;
    const std::size_t hm = m_hmtx.off;
    if (glyph < m_numHM) {
        advance = (int)U16(hm + (std::size_t)glyph * 4);
        lsb = I16(hm + (std::size_t)glyph * 4 + 2);
    } else {
        advance = (int)U16(hm + (std::size_t)(m_numHM - 1) * 4);
        const std::size_t o = (std::size_t)m_numHM * 4 + (std::size_t)(glyph - m_numHM) * 2;
        lsb = o + 2 <= m_hmtx.len ? I16(hm + o) : 0;
    }
    return true;
}

// ---- Kerning (OpenType spec: GPOS lookup type 2 and the 'kern' table) -----------------
namespace {
constexpr int kMaxPairSubtables = 1024;   // more than any real font; caps the work per pair
// Bytes of a ValueRecord with this ValueFormat: two per bit of the low byte.
int ValueSize(std::uint32_t fmt) {
    int n = 0;
    for (int b = 0; b < 8; ++b) n += (fmt >> b) & 1u;
    return n * 2;
}
// Byte offset of a field (bit `bit` of the ValueFormat) inside a ValueRecord.
int ValueField(std::uint32_t fmt, int bit) {
    int n = 0;
    for (int b = 0; b < bit; ++b) n += (fmt >> b) & 1u;
    return n * 2;
}
} // namespace

void Face::LoadKerning(std::uint32_t dir) {
    m_pairSubs.clear(); m_kernSubs.clear(); m_kernSource = 0;
    Table gpos;
    if (FindTable(dir, "GPOS", gpos) && gpos.len >= 10 && U16(gpos.off) == 1) {
        const std::size_t g = gpos.off;
        const std::uint32_t flOff = U16(g + 6), llOff = U16(g + 8);
        if (flOff && llOff) {
            const std::size_t fl = g + flOff, ll = g + llOff;
            // Lookups the 'kern' feature uses (in every script), in lookup-list order.
            std::vector<int> lookups;
            const std::uint32_t nf = U16(fl);
            for (std::uint32_t i = 0; i < nf && Has(fl + 2 + (std::size_t)i * 6, 6); ++i) {
                const std::size_t rec = fl + 2 + (std::size_t)i * 6;
                if (U32(rec) != Tag("kern")) continue;
                const std::size_t ft = fl + U16(rec + 4);
                const std::uint32_t n = U16(ft + 2);
                for (std::uint32_t j = 0; j < n && Has(ft + 4 + (std::size_t)j * 2, 2); ++j)
                    lookups.push_back((int)U16(ft + 4 + (std::size_t)j * 2));
            }
            std::sort(lookups.begin(), lookups.end());
            lookups.erase(std::unique(lookups.begin(), lookups.end()), lookups.end());
            const std::uint32_t nl = U16(ll);
            for (int li : lookups) {
                if ((std::uint32_t)li >= nl) continue;
                const std::size_t lo = ll + U16(ll + 2 + (std::size_t)li * 2);
                const std::uint32_t type = U16(lo), nsub = U16(lo + 4);
                if (type != 2 && type != 9) continue;
                for (std::uint32_t k = 0; k < nsub && Has(lo + 6 + (std::size_t)k * 2, 2); ++k) {
                    std::size_t st = lo + U16(lo + 6 + (std::size_t)k * 2);
                    std::uint32_t t = type;
                    if (t == 9) {   // extension: format 1, the real type, a 32-bit offset
                        if (U16(st) != 1) continue;
                        t = U16(st + 2);
                        st += U32(st + 4);
                    }
                    if (t != 2 || !Has(st, 10)) continue;
                    const std::uint32_t fmt = U16(st);
                    if (fmt != 1 && fmt != 2) continue;
                    if ((int)m_pairSubs.size() >= kMaxPairSubtables) break;
                    m_pairSubs.push_back({st, li});
                }
            }
        }
    }
    if (!m_pairSubs.empty()) { m_kernSource = 2; return; }

    Table kern;
    if (!FindTable(dir, "kern", kern) || kern.len < 4 || U16(kern.off) != 0) return;   // (Apple's version 1 is not read)
    const std::uint32_t n = U16(kern.off + 2);
    std::size_t off = kern.off + 4;
    const std::size_t end = (std::size_t)kern.off + kern.len;
    for (std::uint32_t i = 0; i < n && off + 14 <= end; ++i) {
        const std::uint32_t len = U16(off + 2), cov = U16(off + 4);
        const std::uint32_t fmt = cov >> 8;
        std::size_t next = off + len;
        if (fmt == 0) {
            // Large tables overflow the 16-bit length: trust the pair count instead.
            next = off + 14 + (std::size_t)U16(off + 6) * 6;
            const bool horizontal = (cov & 1u) != 0, cross = (cov & 4u) != 0, minimum = (cov & 2u) != 0;
            if (horizontal && !cross && !minimum && (int)m_kernSubs.size() < kMaxPairSubtables)
                m_kernSubs.push_back({off, (cov & 8u) != 0});
        }
        if (len < 6 && fmt != 0) break;
        if (next <= off) break;
        off = next;
    }
    if (!m_kernSubs.empty()) m_kernSource = 1;
}

int Face::CoverageIndex(std::size_t cov, int glyph) const {
    const std::uint32_t fmt = U16(cov);
    const std::uint32_t g = (std::uint32_t)glyph;
    if (fmt == 1) {
        std::uint32_t lo = 0, hi = U16(cov + 2);
        while (lo < hi) {
            const std::uint32_t mid = (lo + hi) / 2;
            const std::uint32_t v = U16(cov + 4 + (std::size_t)mid * 2);
            if (v == g) return (int)mid;
            if (v < g) lo = mid + 1; else hi = mid;
        }
        return -1;
    }
    if (fmt == 2) {
        std::uint32_t lo = 0, hi = U16(cov + 2);
        while (lo < hi) {   // first range whose end >= g
            const std::uint32_t mid = (lo + hi) / 2;
            if (U16(cov + 4 + (std::size_t)mid * 6 + 2) < g) lo = mid + 1; else hi = mid;
        }
        if (lo >= U16(cov + 2)) return -1;
        const std::size_t r = cov + 4 + (std::size_t)lo * 6;
        const std::uint32_t start = U16(r);
        if (start > g) return -1;
        return (int)(U16(r + 4) + (g - start));
    }
    return -1;
}

int Face::ClassOf(std::size_t cd, int glyph) const {
    const std::uint32_t fmt = U16(cd);
    const std::uint32_t g = (std::uint32_t)glyph;
    if (fmt == 1) {
        const std::uint32_t start = U16(cd + 2), count = U16(cd + 4);
        return (g >= start && g - start < count) ? (int)U16(cd + 6 + (std::size_t)(g - start) * 2) : 0;
    }
    if (fmt == 2) {
        const std::uint32_t n = U16(cd + 2);
        std::uint32_t lo = 0, hi = n;
        while (lo < hi) {
            const std::uint32_t mid = (lo + hi) / 2;
            if (U16(cd + 4 + (std::size_t)mid * 6 + 2) < g) lo = mid + 1; else hi = mid;
        }
        if (lo >= n) return 0;
        const std::size_t r = cd + 4 + (std::size_t)lo * 6;
        return U16(r) <= g ? (int)U16(r + 4) : 0;
    }
    return 0;   // glyphs no class lists are class 0
}

// The x adjustment one pair-adjustment subtable gives (the first glyph's XAdvance plus
// the second glyph's XPlacement); `applied` says whether the subtable covers the pair.
int Face::PairPosValue(std::size_t st, int left, int right, bool& applied) const {
    applied = false;
    const int ci = CoverageIndex(st + U16(st + 2), left);
    if (ci < 0) return 0;
    const std::uint32_t vf1 = U16(st + 4), vf2 = U16(st + 6);
    const int s1 = ValueSize(vf1), s2 = ValueSize(vf2);
    auto value = [&](std::size_t rec) {
        int v = 0;
        if (vf1 & 4u) v += I16(rec + (std::size_t)ValueField(vf1, 2));
        if (vf2 & 1u) v += I16(rec + (std::size_t)s1);
        return v;
    };
    if (U16(st) == 1) {
        if ((std::uint32_t)ci >= U16(st + 8)) return 0;
        const std::size_t ps = st + U16(st + 10 + (std::size_t)ci * 2);
        const std::size_t recSize = 2 + (std::size_t)s1 + (std::size_t)s2;
        std::uint32_t lo = 0, hi = U16(ps);
        while (lo < hi) {
            const std::uint32_t mid = (lo + hi) / 2;
            const std::size_t rec = ps + 2 + (std::size_t)mid * recSize;
            const std::uint32_t second = U16(rec);
            if (second == (std::uint32_t)right) { applied = true; return value(rec + 2); }
            if (second < (std::uint32_t)right) lo = mid + 1; else hi = mid;
        }
        return 0;
    }
    // Format 2: class pairs.
    const std::uint32_t c1n = U16(st + 12), c2n = U16(st + 14);
    const int c1 = ClassOf(st + U16(st + 8), left), c2 = ClassOf(st + U16(st + 10), right);
    if ((std::uint32_t)c1 >= c1n || (std::uint32_t)c2 >= c2n) return 0;
    applied = true;   // a covered first glyph: this subtable decides, even when its value is 0
    const std::size_t rec = st + 16 + ((std::size_t)c1 * c2n + (std::size_t)c2) * (std::size_t)(s1 + s2);
    return value(rec);
}

int Face::Kerning(int left, int right) const {
    if (!m_valid || left <= 0 || right <= 0 || left >= m_numGlyphs || right >= m_numGlyphs) return 0;
    if (m_kernSource == 2) {
        int total = 0, doneLookup = -1;
        for (const PairSub& p : m_pairSubs) {
            if (p.lookup == doneLookup) continue;   // the first subtable that applies ends a lookup
            bool applied = false;
            const int v = PairPosValue(p.off, left, right, applied);
            if (applied) { total += v; doneLookup = p.lookup; }
        }
        return total;
    }
    if (m_kernSource == 1) {
        const std::uint32_t key = ((std::uint32_t)left << 16) | (std::uint32_t)right;
        int total = 0;
        for (const KernSub& k : m_kernSubs) {
            std::uint32_t lo = 0, hi = U16(k.off + 6);
            while (lo < hi) {
                const std::uint32_t mid = (lo + hi) / 2;
                const std::size_t rec = k.off + 14 + (std::size_t)mid * 6;
                const std::uint32_t v = U32(rec);
                if (v == key) { total = k.replace ? I16(rec + 4) : total + I16(rec + 4); break; }
                if (v < key) lo = mid + 1; else hi = mid;
            }
        }
        return total;
    }
    return 0;
}

// ---- glyf ----------------------------------------------------------------------------
bool Face::GlyphData(int glyph, std::size_t& off, std::size_t& len) const {
    off = len = 0;
    if (glyph < 0 || glyph >= m_numGlyphs) return false;
    std::size_t a, b;
    const std::size_t g = (std::size_t)glyph;
    if (m_locaLong) {
        if ((g + 2) * 4 > m_loca.len) return false;
        a = U32(m_loca.off + g * 4); b = U32(m_loca.off + g * 4 + 4);
    } else {
        if ((g + 2) * 2 > m_loca.len) return false;
        a = (std::size_t)U16(m_loca.off + g * 2) * 2; b = (std::size_t)U16(m_loca.off + g * 2 + 2) * 2;
    }
    if (b <= a) return true;                 // empty glyph (space)
    if (a >= m_glyf.len) return false;
    if (b > m_glyf.len) b = m_glyf.len;      // truncated table
    if (b - a < 10) return false;
    off = m_glyf.off + a; len = b - a;
    return true;
}

bool Face::GlyfPoints(int glyph, int depth, PointList& out, int& budget) const {
    std::size_t off, len;
    if (!GlyphData(glyph, off, len)) return false;
    if (len == 0) return true;
    const std::size_t end = off + len;
    const int nc = I16(off);
    if (nc >= 0) {
        if (nc == 0) return true;
        std::size_t p = off + 10;
        if (p + (std::size_t)nc * 2 + 2 > end) return false;
        static thread_local std::vector<int> ends;   // (no recursion below: reused, not reallocated)
        ends.resize((std::size_t)nc);
        for (int i = 0; i < nc; ++i) {
            ends[(std::size_t)i] = (int)U16(p + (std::size_t)i * 2);
            if (i > 0 && ends[(std::size_t)i] <= ends[(std::size_t)i - 1]) return false;
        }
        const int np = ends.back() + 1;
        budget -= np;
        if (budget < 0) return false;
        p += (std::size_t)nc * 2;
        const std::size_t ins = U16(p);
        std::size_t q = p + 2 + ins;
        static thread_local std::vector<std::uint8_t> flags;
        flags.resize((std::size_t)np);
        for (int i = 0; i < np;) {
            if (q >= end) return false;
            const std::uint8_t f = (std::uint8_t)U8(q++);
            flags[(std::size_t)i++] = f;
            if (f & 8) {
                if (q >= end) return false;
                int r = (int)U8(q++);
                if (i + r > np) return false;
                while (r-- > 0) flags[(std::size_t)i++] = f;
            }
        }
        static thread_local std::vector<float> xs, ys;
        xs.resize((std::size_t)np); ys.resize((std::size_t)np);
        int v = 0;
        for (int i = 0; i < np; ++i) {
            const std::uint8_t f = flags[(std::size_t)i];
            if (f & 2) { if (q >= end) return false; const int d = (int)U8(q++); v += (f & 16) ? d : -d; }
            else if (!(f & 16)) { if (q + 2 > end) return false; v += I16(q); q += 2; }
            xs[(std::size_t)i] = (float)v;
        }
        v = 0;
        for (int i = 0; i < np; ++i) {
            const std::uint8_t f = flags[(std::size_t)i];
            if (f & 4) { if (q >= end) return false; const int d = (int)U8(q++); v += (f & 32) ? d : -d; }
            else if (!(f & 32)) { if (q + 2 > end) return false; v += I16(q); q += 2; }
            ys[(std::size_t)i] = (float)v;
        }
        const int base = (int)out.pts.size();
        out.pts.reserve(out.pts.size() + (std::size_t)np);
        for (int i = 0; i < np; ++i) out.pts.push_back({xs[(std::size_t)i], ys[(std::size_t)i], (flags[(std::size_t)i] & 1) != 0});
        for (int e : ends) out.ends.push_back(base + e);
        return true;
    }

    // Composite: a list of (glyph, transform, offset) components.
    if (depth >= kMaxCompositeDepth) return false;
    std::size_t q = off + 10;
    std::uint32_t flags;
    do {
        if (q + 4 > end) return false;
        flags = U16(q);
        const int gi = (int)U16(q + 2);
        q += 4;
        int a1, a2;
        if (flags & 1) {
            if (q + 4 > end) return false;
            if (flags & 2) { a1 = I16(q); a2 = I16(q + 2); } else { a1 = (int)U16(q); a2 = (int)U16(q + 2); }
            q += 4;
        } else {
            if (q + 2 > end) return false;
            if (flags & 2) { a1 = (std::int8_t)U8(q); a2 = (std::int8_t)U8(q + 1); } else { a1 = (int)U8(q); a2 = (int)U8(q + 1); }
            q += 2;
        }
        float ma = 1, mb = 0, mc = 0, md = 1;   // x' = ma*x + mc*y ; y' = mb*x + md*y
        const float k = 1.0f / 16384.0f;
        if (flags & 0x08) { if (q + 2 > end) return false; ma = md = (float)I16(q) * k; q += 2; }
        else if (flags & 0x40) { if (q + 4 > end) return false; ma = (float)I16(q) * k; md = (float)I16(q + 2) * k; q += 4; }
        else if (flags & 0x80) {
            if (q + 8 > end) return false;
            ma = (float)I16(q) * k; mb = (float)I16(q + 2) * k; mc = (float)I16(q + 4) * k; md = (float)I16(q + 6) * k;
            q += 8;
        }
        budget -= 1;
        if (budget < 0) return false;
        PointList child;
        if (!GlyfPoints(gi, depth + 1, child, budget)) return false;
        for (auto& p : child.pts) {
            const float x = p.x, y = p.y;
            p.x = ma * x + mc * y;
            p.y = mb * x + md * y;
        }
        float dx = 0, dy = 0;
        if (flags & 2) {
            dx = (float)a1; dy = (float)a2;
            if ((flags & 0x800) && !(flags & 0x1000)) {   // SCALED_COMPONENT_OFFSET
                dx *= std::sqrt(ma * ma + mc * mc);
                dy *= std::sqrt(md * md + mb * mb);
            }
        } else if (a1 >= 0 && (std::size_t)a1 < out.pts.size() && a2 >= 0 && (std::size_t)a2 < child.pts.size()) {
            // Point matching: slide the component so its point a2 lands on the parent's a1.
            dx = out.pts[(std::size_t)a1].x - child.pts[(std::size_t)a2].x;
            dy = out.pts[(std::size_t)a1].y - child.pts[(std::size_t)a2].y;
        }
        const int base = (int)out.pts.size();
        for (auto& p : child.pts) out.pts.push_back({p.x + dx, p.y + dy, p.on});
        for (int e : child.ends) out.ends.push_back(base + e);
    } while (flags & 0x20);
    return true;
}

bool Face::Outline(int glyph, PathSink& sink) const {
    if (!m_valid) return false;
    if (m_cff) return CffOutline(glyph, sink);
    PointList pl;
    int budget = kPointBudget;
    if (!GlyfPoints(glyph, 0, pl, budget)) return false;
    EmitContours(pl, sink);
    return true;
}

bool Face::GlyphBounds(int glyph, float& xmin, float& ymin, float& xmax, float& ymax) const {
    xmin = ymin = xmax = ymax = 0;
    if (!m_valid || glyph < 0 || glyph >= m_numGlyphs) return false;
    if (!m_cff) {
        // From the points themselves (like FreeType), not the glyph header: a composite's
        // or a sloppy tool's header box can be wrong, and a wrong box would clip the glyph.
        PointList pl;
        int budget = kPointBudget;
        if (!GlyfPoints(glyph, 0, pl, budget) || pl.pts.empty()) return false;
        xmin = xmax = pl.pts[0].x; ymin = ymax = pl.pts[0].y;
        for (const auto& p : pl.pts) {
            xmin = std::min(xmin, p.x); xmax = std::max(xmax, p.x);
            ymin = std::min(ymin, p.y); ymax = std::max(ymax, p.y);
        }
        return xmax > xmin && ymax > ymin;
    }
    Bounds b;
    if (!CffOutline(glyph, b) || !b.any) return false;
    xmin = b.xmin; ymin = b.ymin; xmax = b.xmax; ymax = b.ymax;
    return xmax > xmin && ymax > ymin;
}

// The outline read once: the path in font units, replayed into the rasterizer, and the
// box of its points (for glyf, every point, as GlyphBounds; for CFF, the path's points).
bool Face::Capture(int glyph, detail::Recorder& rec, float& xmin, float& ymin, float& xmax, float& ymax) const {
    xmin = ymin = xmax = ymax = 0;
    if (!m_valid || glyph < 0 || glyph >= m_numGlyphs) return false;
    if (!m_cff) {
        static thread_local PointList pl;   // reused: Capture is never nested
        pl.pts.clear(); pl.ends.clear();
        int budget = kPointBudget;
        if (!GlyfPoints(glyph, 0, pl, budget) || pl.pts.empty()) return false;
        xmin = xmax = pl.pts[0].x; ymin = ymax = pl.pts[0].y;
        for (const auto& p : pl.pts) {
            xmin = std::min(xmin, p.x); xmax = std::max(xmax, p.x);
            ymin = std::min(ymin, p.y); ymax = std::max(ymax, p.y);
        }
        EmitContours(pl, rec);
        return xmax > xmin && ymax > ymin;
    }
    if (!CffOutline(glyph, rec) || !rec.box.any) return false;
    xmin = rec.box.xmin; ymin = rec.box.ymin; xmax = rec.box.xmax; ymax = rec.box.ymax;
    return xmax > xmin && ymax > ymin;
}

bool Face::GlyphBox(int glyph, float scale, int& x0, int& y0, int& x1, int& y1) const {
    x0 = y0 = x1 = y1 = 0;
    float xmin, ymin, xmax, ymax;
    if (!(scale > 0.0f) || !Finite(scale) || !GlyphBounds(glyph, xmin, ymin, xmax, ymax)) return false;
    return BoxAt(xmin, ymin, xmax, ymax, scale, x0, y0, x1, y1);
}

bool Face::BoxAt(float xmin, float ymin, float xmax, float ymax, float scale, int& x0, int& y0, int& x1, int& y1) {
    x0 = y0 = x1 = y1 = 0;
    if (!(scale > 0.0f) || !Finite(scale)) return false;
    const float fx0 = std::floor(xmin * scale), fy0 = std::floor(-ymax * scale);
    const float fx1 = std::ceil(xmax * scale), fy1 = std::ceil(-ymin * scale);
    if (!(fx1 - fx0 > 0.0f) || !(fy1 - fy0 > 0.0f)) return false;
    if (fx1 - fx0 > (float)kMaxBox || fy1 - fy0 > (float)kMaxBox) return false;
    if (!Finite(fx0) || !Finite(fy0)) return false;
    x0 = (int)fx0; y0 = (int)fy0; x1 = (int)fx1; y1 = (int)fy1;
    return true;
}

bool Face::Render(int glyph, float scale, std::uint8_t* out, int w, int h, int stride) const {
    if (!out || w <= 0 || h <= 0 || stride < w) return false;
    for (int y = 0; y < h; ++y) std::memset(out + (std::size_t)y * stride, 0, (std::size_t)w);
    detail::Recorder rec;
    float xmin, ymin, xmax, ymax;
    int x0, y0, x1, y1;
    if (!Capture(glyph, rec, xmin, ymin, xmax, ymax) || !BoxAt(xmin, ymin, xmax, ymax, scale, x0, y0, x1, y1)) return false;
    Raster r(w, h, scale, x0, y0);
    rec.Replay(r);
    r.Finish(out, stride, &rec);
    return true;
}

bool Face::RenderGlyph(int glyph, float scale, std::vector<std::uint8_t>& bitmap, int& x0, int& y0, int& w, int& h) const {
    x0 = y0 = w = h = 0;
    bitmap.clear();
    detail::Recorder rec;
    float xmin, ymin, xmax, ymax;
    int x1, y1;
    if (!Capture(glyph, rec, xmin, ymin, xmax, ymax) || !BoxAt(xmin, ymin, xmax, ymax, scale, x0, y0, x1, y1)) {
        x0 = y0 = 0;
        return false;
    }
    w = x1 - x0; h = y1 - y0;
    bitmap.assign((std::size_t)w * (std::size_t)h, 0);
    Raster r(w, h, scale, x0, y0);
    rec.Replay(r);
    r.Finish(bitmap.data(), w, &rec);
    return true;
}

// ---- CFF -------------------------------------------------------------------------------
bool Face::CffIndex::Get(const Face& f, std::uint32_t i, std::size_t& start, std::size_t& len) const {
    if (i >= count) return false;
    auto rd = [&](std::uint32_t k) {
        std::size_t v = 0;
        for (std::uint32_t b = 0; b < offSize; ++b) v = (v << 8) | f.U8(offsets + (std::size_t)k * offSize + b);
        return v;
    };
    const std::size_t o1 = rd(i), o2 = rd(i + 1);
    if (o1 < 1 || o2 < o1) return false;
    start = data + o1; len = o2 - o1;
    return f.Has(start, len);
}

bool Face::ReadIndex(std::size_t at, CffIndex& idx, std::size_t& end) const {
    idx = CffIndex();
    if (!Has(at, 2)) return false;
    idx.count = U16(at);
    if (idx.count == 0) { end = at + 2; return true; }
    idx.offSize = U8(at + 2);
    if (idx.offSize < 1 || idx.offSize > 4) return false;
    idx.offsets = at + 3;
    const std::size_t table = ((std::size_t)idx.count + 1) * idx.offSize;
    if (!Has(idx.offsets, table)) return false;
    idx.data = idx.offsets + table - 1;
    std::size_t last = 0;
    for (std::uint32_t b = 0; b < idx.offSize; ++b) last = (last << 8) | U8(idx.offsets + (std::size_t)idx.count * idx.offSize + b);
    if (last < 1 || !Has(idx.data + 1, last - 1)) return false;
    end = idx.data + last;
    return true;
}

bool Face::LoadCff() {
    const std::size_t base = m_cffT.off;
    if (m_cffT.len < 4 || U8(base) != 1) return false;   // CFF 1 only (CFF2 is variable-font only)
    std::size_t p = base + U8(base + 2);
    CffIndex names, tops, strs;
    if (!ReadIndex(p, names, p) || !ReadIndex(p, tops, p) || !ReadIndex(p, strs, p) || !ReadIndex(p, m_gsubrs, p)) return false;

    struct Entry { int op; double v[4]; int n; };
    auto parseDict = [&](std::size_t s, std::size_t l, std::vector<Entry>& out) {
        double st[48]; int sp = 0;
        const std::size_t e = s + l;
        for (std::size_t i = s; i < e;) {
            const std::uint32_t b = U8(i++);
            if (b <= 21) {
                int op = (int)b;
                if (b == 12) { if (i >= e) return; op = 0x0C00 | (int)U8(i++); }
                Entry en; en.op = op; en.n = std::min(sp, 4);
                for (int k = 0; k < en.n; ++k) en.v[k] = st[sp - en.n + k];
                out.push_back(en);
                sp = 0;
            } else if (b == 28) { if (i + 2 > e) return; if (sp < 48) st[sp++] = I16(i); i += 2; }
            else if (b == 29) { if (i + 4 > e) return; if (sp < 48) st[sp++] = (double)(std::int32_t)U32(i); i += 4; }
            else if (b == 30) {   // real: nibbles until 0xF; value unused here
                for (;;) { if (i >= e) return; const std::uint32_t by = U8(i++); if ((by & 0x0F) == 0x0F || (by >> 4) == 0x0F) break; }
                if (sp < 48) st[sp++] = 0;
            }
            else if (b >= 32 && b <= 246) { if (sp < 48) st[sp++] = (int)b - 139; }
            else if (b >= 247 && b <= 250) { if (i >= e) return; if (sp < 48) st[sp++] = ((int)b - 247) * 256 + (int)U8(i) + 108; ++i; }
            else if (b >= 251 && b <= 254) { if (i >= e) return; if (sp < 48) st[sp++] = -((int)b - 251) * 256 - (int)U8(i) - 108; ++i; }
        }
    };
    auto find = [](const std::vector<Entry>& d, int op) -> const Entry* {
        for (const Entry& e : d) if (e.op == op) return &e;
        return nullptr;
    };
    auto readPrivate = [&](const std::vector<Entry>& d, CffFd& fd) {
        const Entry* pv = find(d, 18);
        if (!pv || pv->n < 2) return;
        const std::size_t psize = (std::size_t)std::max(0.0, pv->v[pv->n - 2]);
        const std::size_t poff = base + (std::size_t)std::max(0.0, pv->v[pv->n - 1]);
        if (!Has(poff, psize)) return;
        std::vector<Entry> pd;
        parseDict(poff, psize, pd);
        const Entry* sb = find(pd, 19);
        if (sb && sb->n >= 1) {
            std::size_t endp;
            if (ReadIndex(poff + (std::size_t)std::max(0.0, sb->v[sb->n - 1]), fd.subrs, endp)) fd.hasSubrs = true;
        }
    };

    std::size_t s, l;
    if (!tops.Get(*this, 0, s, l)) return false;
    std::vector<Entry> top;
    parseDict(s, l, top);
    const Entry* cs = find(top, 17);
    if (!cs || cs->n < 1) return false;
    std::size_t endp;
    if (!ReadIndex(base + (std::size_t)std::max(0.0, cs->v[cs->n - 1]), m_charStrings, endp)) return false;

    const Entry* fda = find(top, 0x0C24);
    const Entry* fds = find(top, 0x0C25);
    if (fda && fda->n >= 1) {   // CID-keyed: one Private dict per Font DICT
        CffIndex fdIdx;
        if (!ReadIndex(base + (std::size_t)std::max(0.0, fda->v[fda->n - 1]), fdIdx, endp)) return false;
        const std::uint32_t nfd = std::min<std::uint32_t>(fdIdx.count, 256);
        for (std::uint32_t i = 0; i < nfd; ++i) {
            CffFd fd;
            std::size_t fs, fl;
            if (fdIdx.Get(*this, i, fs, fl)) {
                std::vector<Entry> d;
                parseDict(fs, fl, d);
                readPrivate(d, fd);
            }
            m_fds.push_back(fd);
        }
        if (fds && fds->n >= 1) {
            m_fdSelect = base + (std::size_t)std::max(0.0, fds->v[fds->n - 1]);
            const std::uint32_t fmt = U8(m_fdSelect);
            if (fmt == 0 || fmt == 3) m_fdSelectFmt = (int)fmt;
        }
    }
    if (m_fds.empty()) {
        CffFd fd;
        readPrivate(top, fd);
        m_fds.push_back(fd);
    }
    return true;
}

bool Face::CffOutline(int glyph, PathSink& sink) const {
    if (glyph < 0 || glyph >= m_numGlyphs) return false;
    std::size_t s, l;
    if (!m_charStrings.Get(*this, (std::uint32_t)glyph, s, l)) return false;
    std::size_t fd = 0;
    if (m_fdSelectFmt == 0) fd = U8(m_fdSelect + 1 + (std::size_t)glyph);
    else if (m_fdSelectFmt == 3) {
        const std::uint32_t nr = U16(m_fdSelect + 1);
        for (std::uint32_t i = 0; i < nr; ++i) {   // ranges ascend; the last one whose first <= glyph
            const std::size_t r = m_fdSelect + 3 + (std::size_t)i * 3;
            if ((int)U16(r) > glyph) break;
            fd = U8(r + 2);
        }
    }
    if (fd >= m_fds.size()) fd = 0;
    CsState st;
    st.sink = &sink;
    std::memset(st.st, 0, sizeof st.st);
    std::memset(st.trans, 0, sizeof st.trans);
    const bool ok = CffRun(m_fds[fd].subrs, s, l, st, 0);
    if (st.open) sink.Close();
    return ok;
}

bool Face::CffRun(const CffIndex& local, std::size_t start, std::size_t len, CsState& s, int depth) const {
    if (depth > kMaxSubrDepth) return false;
    std::size_t i = start;
    const std::size_t end = start + len;
    double* st = s.st;
    PathSink& sink = *s.sink;
    auto moveTo = [&](double dx, double dy) {
        if (s.open) sink.Close();
        s.x += dx; s.y += dy;
        sink.Move((float)s.x, (float)s.y);
        s.open = true;
    };
    auto lineTo = [&](double dx, double dy) {
        s.x += dx; s.y += dy;
        sink.Line((float)s.x, (float)s.y);
    };
    auto curveTo = [&](double a, double b, double c, double d, double e, double f) {
        const double x1 = s.x + a, y1 = s.y + b, x2 = x1 + c, y2 = y1 + d;
        s.x = x2 + e; s.y = y2 + f;
        sink.Cubic((float)x1, (float)y1, (float)x2, (float)y2, (float)s.x, (float)s.y);
    };
    while (i < end) {
        if (++s.steps > kMaxCsSteps) return false;
        const std::uint32_t b0 = U8(i++);
        if (b0 >= 32 || b0 == 28) {
            double v;
            if (b0 == 28) { if (i + 2 > end) return false; v = I16(i); i += 2; }
            else if (b0 <= 246) v = (int)b0 - 139;
            else if (b0 <= 250) { if (i >= end) return false; v = ((int)b0 - 247) * 256 + (int)U8(i++) + 108; }
            else if (b0 <= 254) { if (i >= end) return false; v = -((int)b0 - 251) * 256 - (int)U8(i++) - 108; }
            else { if (i + 4 > end) return false; v = (double)(std::int32_t)U32(i) / 65536.0; i += 4; }
            if (s.sp >= 96) return false;
            st[s.sp++] = v;
            continue;
        }
        int& sp = s.sp;
        switch (b0) {
        case 1: case 3: case 18: case 23:   // stems: only counted (for the hint mask size)
            s.nStems += sp / 2; sp = 0; break;
        case 19: case 20:
            s.nStems += sp / 2; sp = 0;
            i += (std::size_t)(s.nStems + 7) / 8;
            break;
        case 21: if (sp < 2) return false; moveTo(st[sp - 2], st[sp - 1]); sp = 0; break;
        case 22: if (sp < 1) return false; moveTo(st[sp - 1], 0); sp = 0; break;
        case 4:  if (sp < 1) return false; moveTo(0, st[sp - 1]); sp = 0; break;
        case 5:
            for (int k = 0; k + 2 <= sp; k += 2) lineTo(st[k], st[k + 1]);
            sp = 0; break;
        case 6: case 7: {
            bool horiz = (b0 == 6);
            for (int k = 0; k < sp; ++k) { if (horiz) lineTo(st[k], 0); else lineTo(0, st[k]); horiz = !horiz; }
            sp = 0; break;
        }
        case 8:
            for (int k = 0; k + 6 <= sp; k += 6) curveTo(st[k], st[k + 1], st[k + 2], st[k + 3], st[k + 4], st[k + 5]);
            sp = 0; break;
        case 24: {   // rcurveline
            int k = 0;
            for (; k + 6 <= sp - 2; k += 6) curveTo(st[k], st[k + 1], st[k + 2], st[k + 3], st[k + 4], st[k + 5]);
            if (k + 2 <= sp) lineTo(st[k], st[k + 1]);
            sp = 0; break;
        }
        case 25: {   // rlinecurve
            int k = 0;
            for (; k + 2 <= sp - 6; k += 2) lineTo(st[k], st[k + 1]);
            if (k + 6 <= sp) curveTo(st[k], st[k + 1], st[k + 2], st[k + 3], st[k + 4], st[k + 5]);
            sp = 0; break;
        }
        case 26: {   // vvcurveto
            int k = 0; double dx1 = 0;
            if (sp & 1) dx1 = st[k++];
            for (; k + 4 <= sp; k += 4) { curveTo(dx1, st[k], st[k + 1], st[k + 2], 0, st[k + 3]); dx1 = 0; }
            sp = 0; break;
        }
        case 27: {   // hhcurveto
            int k = 0; double dy1 = 0;
            if (sp & 1) dy1 = st[k++];
            for (; k + 4 <= sp; k += 4) { curveTo(st[k], dy1, st[k + 1], st[k + 2], st[k + 3], 0); dy1 = 0; }
            sp = 0; break;
        }
        case 30: case 31: {   // vhcurveto / hvcurveto: alternate the starting direction
            bool horiz = (b0 == 31);
            for (int k = 0; k + 4 <= sp; k += 4) {
                const bool last = (sp - k == 5);
                if (horiz) curveTo(st[k], 0, st[k + 1], st[k + 2], last ? st[k + 4] : 0, st[k + 3]);
                else curveTo(0, st[k], st[k + 1], st[k + 2], st[k + 3], last ? st[k + 4] : 0);
                horiz = !horiz;
            }
            sp = 0; break;
        }
        case 10: case 29: {
            if (sp < 1) return false;
            const CffIndex& idx = (b0 == 10) ? local : m_gsubrs;
            const long n = (long)st[--sp] + Bias(idx.count);
            std::size_t ss, sl;
            if (n < 0 || !idx.Get(*this, (std::uint32_t)n, ss, sl)) return false;
            if (!CffRun(local, ss, sl, s, depth + 1)) return false;
            if (s.done) return true;
            break;
        }
        case 11: return true;
        case 14:
            s.done = true;
            if (s.open) { sink.Close(); s.open = false; }
            return true;
        case 12: {
            if (i >= end) return false;
            const std::uint32_t b1 = U8(i++);
            switch (b1) {
            case 34:   // hflex
                if (sp < 7) return false;
                curveTo(st[0], 0, st[1], st[2], st[3], 0);
                curveTo(st[4], 0, st[5], -st[2], st[6], 0);
                sp = 0; break;
            case 35:   // flex
                if (sp < 12) return false;
                curveTo(st[0], st[1], st[2], st[3], st[4], st[5]);
                curveTo(st[6], st[7], st[8], st[9], st[10], st[11]);
                sp = 0; break;
            case 36:   // hflex1
                if (sp < 9) return false;
                curveTo(st[0], st[1], st[2], st[3], st[4], 0);
                curveTo(st[5], 0, st[6], st[7], st[8], -(st[1] + st[3] + st[7]));
                sp = 0; break;
            case 37: { // flex1
                if (sp < 11) return false;
                const double dx = st[0] + st[2] + st[4] + st[6] + st[8];
                const double dy = st[1] + st[3] + st[5] + st[7] + st[9];
                curveTo(st[0], st[1], st[2], st[3], st[4], st[5]);
                if (std::fabs(dx) > std::fabs(dy)) curveTo(st[6], st[7], st[8], st[9], st[10], -dy);
                else curveTo(st[6], st[7], st[8], st[9], -dx, st[10]);
                sp = 0; break;
            }
            // The (deprecated, but legal) arithmetic operators.
            case 3: if (sp < 2) return false; st[sp - 2] = (st[sp - 2] != 0 && st[sp - 1] != 0); --sp; break;
            case 4: if (sp < 2) return false; st[sp - 2] = (st[sp - 2] != 0 || st[sp - 1] != 0); --sp; break;
            case 5: if (sp < 1) return false; st[sp - 1] = (st[sp - 1] == 0); break;
            case 9: if (sp < 1) return false; st[sp - 1] = std::fabs(st[sp - 1]); break;
            case 10: if (sp < 2) return false; st[sp - 2] += st[sp - 1]; --sp; break;
            case 11: if (sp < 2) return false; st[sp - 2] -= st[sp - 1]; --sp; break;
            case 12: if (sp < 2) return false; st[sp - 2] = st[sp - 1] != 0 ? st[sp - 2] / st[sp - 1] : 0; --sp; break;
            case 14: if (sp < 1) return false; st[sp - 1] = -st[sp - 1]; break;
            case 15: if (sp < 2) return false; st[sp - 2] = (st[sp - 2] == st[sp - 1]); --sp; break;
            case 18: if (sp < 1) return false; --sp; break;
            case 20: { if (sp < 2) return false; const long k = (long)st[sp - 1]; if (k >= 0 && k < 32) s.trans[k] = st[sp - 2]; sp -= 2; break; }
            case 21: { if (sp < 1) return false; const long k = (long)st[sp - 1]; st[sp - 1] = (k >= 0 && k < 32) ? s.trans[k] : 0; break; }
            case 22: if (sp < 4) return false; st[sp - 4] = (st[sp - 2] <= st[sp - 1]) ? st[sp - 4] : st[sp - 3]; sp -= 3; break;
            case 23: if (sp >= 96) return false; st[sp++] = 0.5; break;
            case 24: if (sp < 2) return false; st[sp - 2] *= st[sp - 1]; --sp; break;
            case 26: if (sp < 1) return false; st[sp - 1] = st[sp - 1] > 0 ? std::sqrt(st[sp - 1]) : 0; break;
            case 27: if (sp < 1 || sp >= 96) return false; st[sp] = st[sp - 1]; ++sp; break;
            case 28: if (sp < 2) return false; std::swap(st[sp - 1], st[sp - 2]); break;
            case 29: {
                if (sp < 1) return false;
                long k = (long)st[sp - 1]; --sp;
                if (k < 0) k = 0;
                if (k >= sp) return false;
                st[sp] = st[sp - 1 - k]; ++sp; break;
            }
            case 30: {   // roll
                if (sp < 2) return false;
                const long j = (long)st[sp - 1], n = (long)st[sp - 2];
                sp -= 2;
                if (n < 0 || n > sp) return false;
                if (n > 0) {
                    double tmp[96];
                    const long sh = ((j % n) + n) % n;
                    for (long k = 0; k < n; ++k) tmp[(k + sh) % n] = st[sp - n + k];
                    for (long k = 0; k < n; ++k) st[sp - n + k] = tmp[k];
                }
                break;
            }
            case 0: sp = 0; break;   // dotsection (obsolete)
            default: return false;
            }
            break;
        }
        default: return false;   // reserved operator
        }
    }
    return true;   // a subroutine may end without `return`; the top level without endchar
}

// ---- Atlas helpers ----------------------------------------------------------------------
bool ShelfPacker::Place(int gw, int gh, int& px, int& py) {
    if (gw <= 0 || gh <= 0) { px = py = 0; return true; }
    if (x + gw + pad > w) { y += rowH + pad; x = pad; rowH = 0; }
    if (x + gw + pad > w || y + gh + pad > h) return false;
    px = x; py = y;
    x += gw + pad;
    rowH = std::max(rowH, gh);
    return true;
}

bool BakeGlyph(const Face& face, int glyph, float scale, ShelfPacker& packer, std::uint8_t* atlas, BakedGlyph& out) {
    out = BakedGlyph();
    int adv = 0, lsb = 0;
    face.HMetrics(glyph, adv, lsb);
    out.xadvance = (float)adv * scale;
    static thread_local std::vector<std::uint8_t> bmp;
    int bx0, by0, gw, gh;
    if (!face.RenderGlyph(glyph, scale, bmp, bx0, by0, gw, gh)) return true;   // nothing to draw
    int px, py;
    if (!packer.Place(gw, gh, px, py)) return false;
    for (int y = 0; y < gh; ++y)
        std::memcpy(atlas + (std::size_t)(py + y) * packer.w + px, bmp.data() + (std::size_t)y * gw, (std::size_t)gw);
    out.x0 = px; out.y0 = py; out.x1 = px + gw; out.y1 = py + gh;
    out.xoff = (float)bx0; out.yoff = (float)by0;
    return true;
}

} // namespace ttf
} // namespace okay
