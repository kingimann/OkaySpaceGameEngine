#pragma once
// Exact-area anti-aliased fill of outlines: the one coverage rasterizer of the engine, header-only and standard-library
// only, so the std-only image decoder (the SVG renderer in Core/ImageDecodeSvg.cpp) uses it as well as the TrueType
// glyph rasterizer (Graphics/TtfRaster.cpp) without linking anything.
//
// Each line segment adds, to the cells it crosses, the signed area it sweeps to the right of itself; a running sum along
// the row then gives each pixel's covered fraction. Non-zero fill: |sum| clamped to 1 (same-direction overlaps saturate,
// opposite directions cancel). Even-odd fill: the sum folded into 0..1 with a period of 2. Curves are flattened into
// lines to within 0.01 pixel. Coordinates are pixels of a w x h area, y down; anything outside is clipped.
// Written from the geometry (the area of a trapezoid under a line through a unit cell), not from another rasterizer.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace okay {
namespace raster {

class Coverage {
public:
    Coverage(int w, int h) : m_w(w), m_h(h), m_stride(w + 2), m_acc((std::size_t)(w + 2) * (std::size_t)h, 0.0f) {}

    int Width() const { return m_w; }
    int Height() const { return m_h; }

    // ---- path building (pixel coordinates) -----------------------------------------------------------------------
    void MoveTo(float x, float y) {
        CloseSub();
        m_cx = m_sx = x;
        m_cy = m_sy = y;
        m_open = true;
    }
    void LineTo(float x, float y) {
        if (!m_open) { MoveTo(x, y); return; }
        Seg(m_cx, m_cy, x, y);
        m_cx = x; m_cy = y;
    }
    void QuadTo(float qx, float qy, float px, float py) {
        if (!m_open) { MoveTo(px, py); return; }
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
    void CubicTo(float ax, float ay, float bx, float by, float px, float py) {
        if (!m_open) { MoveTo(px, py); return; }
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
    void Close() { CloseSub(); }

    // Coverage 0..255 for the w x h area into out[row * stride + col].
    void Finish(std::uint8_t* out, int stride, bool evenOdd = false) {
        CloseSub();
        for (int y = 0; y < m_h; ++y) {
            const float* row = &m_acc[(std::size_t)y * m_stride];
            std::uint8_t* o = out + (std::size_t)y * stride;
            float sum = 0;
            for (int x = 0; x < m_w; ++x) {
                sum += row[x];
                float v = std::fabs(sum);
                if (evenOdd) {
                    v = std::fmod(v, 2.0f);
                    if (v > 1.0f) v = 2.0f - v;
                } else if (v > 1.0f) {
                    v = 1.0f;
                }
                o[x] = (std::uint8_t)(v * 255.0f + 0.5f);
            }
        }
    }

    // A line in pixel space, clipped to the rows, then to the columns.
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

private:
    static constexpr float kTol = 0.01f;   // flattening tolerance, pixels
    static bool Finite(float v) { return v > -1e7f && v < 1e7f; }   // false for NaN too
    static int Steps(float v) {
        if (!(v > 1.0f)) return 1;
        const float n = std::ceil(std::sqrt(v));
        return n > 128.0f ? 128 : (int)n;
    }
    void CloseSub() {
        if (m_open) {
            Seg(m_cx, m_cy, m_sx, m_sy);
            m_cx = m_sx; m_cy = m_sy;
        }
        m_open = false;
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
    std::vector<float> m_acc;
    float m_cx = 0, m_cy = 0, m_sx = 0, m_sy = 0;
    bool m_open = false;
};

} // namespace raster
} // namespace okay
