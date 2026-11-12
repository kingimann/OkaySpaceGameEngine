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

// ---- Exact-area coverage accumulation ---------------------------------------------
// Each line segment adds, to the cells it crosses, the signed area it sweeps to the
// right of itself; a running sum along the row then gives each pixel's covered
// fraction (|sum|, clamped to 1: non-zero winding for same-direction overlaps).
class Raster : public PathSink {
public:
    Raster(int w, int h, float scale, int ox, int oy)
        : m_w(w), m_h(h), m_stride(w + 2), m_scale(scale), m_ox((float)ox), m_oy((float)oy),
          m_acc((std::size_t)(w + 2) * (std::size_t)h, 0.0f) {}

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
        m_cx = px; m_cy = py;
    }
    void Quad(float cx, float cy, float x, float y) override {
        float qx, qy, px, py; Map(cx, cy, qx, qy); Map(x, y, px, py);
        if (!m_open) { m_sx = m_cx = px; m_sy = m_cy = py; m_open = true; return; }
        const float dd = std::fabs(m_cx - 2 * qx + px) + std::fabs(m_cy - 2 * qy + py);
        const int n = Steps(dd * 0.25f / kTol);
        float ax = m_cx, ay = m_cy;
        for (int i = 1; i <= n; ++i) {
            const float t = (float)i / (float)n, u = 1 - t;
            const float bx = u * u * m_cx + 2 * u * t * qx + t * t * px;
            const float by = u * u * m_cy + 2 * u * t * qy + t * t * py;
            Seg(ax, ay, bx, by);
            ax = bx; ay = by;
        }
        m_cx = px; m_cy = py;
    }
    void Cubic(float c1x, float c1y, float c2x, float c2y, float x, float y) override {
        float ax, ay, bx, by, px, py;
        Map(c1x, c1y, ax, ay); Map(c2x, c2y, bx, by); Map(x, y, px, py);
        if (!m_open) { m_sx = m_cx = px; m_sy = m_cy = py; m_open = true; return; }
        const float d1 = std::fabs(m_cx - 2 * ax + bx) + std::fabs(m_cy - 2 * ay + by);
        const float d2 = std::fabs(ax - 2 * bx + px) + std::fabs(ay - 2 * by + py);
        const int n = Steps(0.75f * std::max(d1, d2) / kTol);
        float lx = m_cx, ly = m_cy;
        for (int i = 1; i <= n; ++i) {
            const float t = (float)i / (float)n, u = 1 - t;
            const float k0 = u * u * u, k1 = 3 * u * u * t, k2 = 3 * u * t * t, k3 = t * t * t;
            const float qx = k0 * m_cx + k1 * ax + k2 * bx + k3 * px;
            const float qy = k0 * m_cy + k1 * ay + k2 * by + k3 * py;
            Seg(lx, ly, qx, qy);
            lx = qx; ly = qy;
        }
        m_cx = px; m_cy = py;
    }
    void Close() override { CloseSub(); }

    // Write coverage 0..255 for the w x h area.
    void Finish(std::uint8_t* out, int stride) {
        CloseSub();
        for (int y = 0; y < m_h; ++y) {
            const float* row = &m_acc[(std::size_t)y * m_stride];
            std::uint8_t* o = out + (std::size_t)y * stride;
            float sum = 0;
            for (int x = 0; x < m_w; ++x) {
                sum += row[x];
                float v = std::fabs(sum);
                if (v > 1.0f) v = 1.0f;
                o[x] = (std::uint8_t)(v * 255.0f + 0.5f);
            }
        }
    }

private:
    static constexpr float kTol = 0.01f;   // flattening tolerance, pixels
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
            m_cx = m_sx; m_cy = m_sy;
        }
        m_open = false;
    }

    // A line in bitmap space. Clip to the rows, then to the columns.
    void Seg(float x0, float y0, float x1, float y1) {
        if (!Finite(x0) || !Finite(y0) || !Finite(x1) || !Finite(y1)) return;
        if (y0 == y1) return;
        float dir = 1.0f;
        if (y0 > y1) { std::swap(x0, x1); std::swap(y0, y1); dir = -1.0f; }
        const float H = (float)m_h;
        if (y1 <= 0.0f || y0 >= H) return;
        const float dxdy = (x1 - x0) / (y1 - y0);
        if (y0 < 0.0f) { x0 += -y0 * dxdy; y0 = 0.0f; }
        if (y1 > H) { x1 = x0 + (H - y0) * dxdy; y1 = H; }
        // Columns: split where the segment crosses x = 0 and x = w.
        const float W = (float)m_w;
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
        int rowLast = (int)std::ceil(y1);
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
            const float fl = std::floor(xa);
            const int i0 = (int)fl;
            const int i1 = (int)std::ceil(xb);
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
    std::vector<float> m_acc;
    float m_cx = 0, m_cy = 0, m_sx = 0, m_sy = 0;
    bool m_open = false;
};

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
        std::vector<int> ends((std::size_t)nc);
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
        std::vector<std::uint8_t> flags((std::size_t)np);
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
        std::vector<float> xs((std::size_t)np), ys((std::size_t)np);
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

bool Face::GlyphBox(int glyph, float scale, int& x0, int& y0, int& x1, int& y1) const {
    x0 = y0 = x1 = y1 = 0;
    float xmin, ymin, xmax, ymax;
    if (!(scale > 0.0f) || !Finite(scale) || !GlyphBounds(glyph, xmin, ymin, xmax, ymax)) return false;
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
    int x0, y0, x1, y1;
    if (!GlyphBox(glyph, scale, x0, y0, x1, y1)) return false;
    Raster r(w, h, scale, x0, y0);
    if (!Outline(glyph, r)) return false;
    r.Finish(out, stride);
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
    int bx0, by0, bx1, by1;
    if (!face.GlyphBox(glyph, scale, bx0, by0, bx1, by1)) return true;   // nothing to draw
    const int gw = bx1 - bx0, gh = by1 - by0;
    int px, py;
    if (!packer.Place(gw, gh, px, py)) return false;
    face.Render(glyph, scale, atlas + (std::size_t)py * packer.w + px, gw, gh, packer.w);
    out.x0 = px; out.y0 = py; out.x1 = px + gw; out.y1 = py + gh;
    out.xoff = (float)bx0; out.yoff = (float)by0;
    return true;
}

} // namespace ttf
} // namespace okay
