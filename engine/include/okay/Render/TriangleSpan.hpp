#pragma once
// ---------------------------------------------------------------------------
// Row spans for the software renderer's triangle loops.
//
// The shadow depth pass visits every pixel of a triangle's bounding box and runs three edge tests on it. A big ground
// quad cut in two triangles by a diagonal has a bounding box twice the size of the triangle in it, so half of the
// visits are for pixels the triangle cannot cover. TriangleRows::Row() gives, for one pixel row, the range of columns
// outside which no pixel can pass the tests, so the loop skips them.
//
// It is an EXACT shortcut, not an approximation: the range is widened by a bound on the float rounding error of the
// per-pixel edge tests (and by one column), so every pixel the float tests would accept is inside it. The loop still
// runs the very same float tests and the very same depth update on every pixel it does visit, so the depth buffer
// comes out bit for bit as before (tests/test_triangle_span.cpp renders random, huge, thin and sliver triangles both ways
// and compares the buffers).
//
// Written from scratch from the half plane form of the three edge functions; no other code was used. Header only.
// ---------------------------------------------------------------------------
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#if defined(__SSE2__) && !defined(OKAY_NO_SIMD)
#include <emmintrin.h>   // SSE2: part of every x86-64 CPU, so the Windows programs can use it too
#define OKAY_TRIANGLE_SPAN_SSE2 1
#endif

namespace okay {
namespace detail {

/// The three barycentric tests the rasterizers run in float, for one screen triangle:
///   w0 = ((x1-px)*(y2-py) - (x2-px)*(y1-py)) * inv
///   w1 = ((x2-px)*(y0-py) - (x0-px)*(y2-py)) * inv
///   w2 = 1 - w0 - w1                           a pixel is covered when w0, w1, w2 >= edgeMin
/// where px = x + 0.5, py = y + 0.5 and inv = 1 / area (the triangle's signed area).
class TriangleRows {
public:
    /// `sx`, `sy`: the three vertices in pixels. `inv`: the 1/area the caller uses. `edgeMin`: 0 or a small negative
    /// number. [minx, maxx]: the columns the caller would visit. Returns false when no safe range can be computed
    /// (a coordinate or the area is out of the range this is worked out for); the caller then visits every column.
    bool Init(const float* sx, const float* sy, float inv, float edgeMin, int minx, int maxx) {
        valid_ = false;
        for (int k = 0; k < 3; ++k) {
            if (!(std::fabs(sx[k]) < 1.0e7f) || !(std::fabs(sy[k]) < 1.0e7f)) return false;   // also false for NaN
            x_[k] = (double)sx[k]; y_[k] = (double)sy[k];
        }
        const double ai = std::fabs((double)inv);
        if (!(ai > 1.0e-20) || !(ai < 1.0e12) || !(edgeMin <= 0.0f) || !(edgeMin > -1.0f)) return false;
        sgn_ = inv > 0.0f ? 1.0 : -1.0;
        absInv_ = ai;
        edgeMin_ = (double)edgeMin;
        // How far a vertex can be from a pixel centre in x, over every column of the box.
        const double c0 = (double)minx + 0.5, c1 = (double)maxx + 0.5;
        uMax_ = 0.0;
        for (int k = 0; k < 3; ++k) uMax_ = std::max(uMax_, std::max(std::fabs(x_[k] - c0), std::fabs(x_[k] - c1)));
        slope0_ = y_[1] - y_[2];
        slope1_ = y_[2] - y_[0];
        valid_ = true;
        return true;
    }

    /// Narrow [xa, xb] (inclusive) to the columns of row `y` where a pixel can be covered. Leaves xa > xb when
    /// the row has none. Only call after Init() returned true.
    void Row(int y, int& xa, int& xb) const {
        if (!valid_) return;
        const double py = (double)y + 0.5;
        const double v0 = y_[0] - py, v1 = y_[1] - py, v2 = y_[2] - py;
        const double a0 = x_[1] * v2 - x_[2] * v1;           // E0(px) = a0 + slope0 * px
        const double a1 = x_[2] * v0 - x_[0] * v2;           // E1(px) = a1 + slope1 * px
        const double vMax = std::max(std::fabs(v0), std::max(std::fabs(v1), std::fabs(v2)));
        // A bound (with a safety factor of 4) on how far the float evaluation of E0 or E1 can be from the exact value.
        const double errE = (1.0 / 524288.0) * uMax_ * vMax;
        // The same for w0 + w1 <= 1 - edgeMin: the error of the two products and of the 1 - w0 - w1 subtractions.
        const double errW = 1.0e-6 + 8.0 * errE * absInv_;
        const double thr = 1.001 * edgeMin_ / absInv_;      // w >= edgeMin as a bound on sgn * E (edgeMin <= 0)
        double lo = -std::numeric_limits<double>::infinity(), hi = std::numeric_limits<double>::infinity();
        bool empty = false;
        auto atLeast = [&](double a, double b) {              // sgn * (a + b * px) >= thr - errE
            const double A = sgn_ * a, B = sgn_ * b, C = thr - errE - A;   // B * px >= C
            if (B > 0.0) lo = std::max(lo, C / B);
            else if (B < 0.0) hi = std::min(hi, C / B);
            else if (C > 0.0) empty = true;
        };
        atLeast(a0, slope0_);
        atLeast(a1, slope1_);
        {   // w0 + w1 <= 1 - edgeMin, i.e. sgn * (E0 + E1) <= (1 - edgeMin + errW) / |inv|
            const double A2 = sgn_ * (a0 + a1), B2 = sgn_ * (slope0_ + slope1_);
            const double R = (1.0 - edgeMin_ + errW) / absInv_ - A2;         // B2 * px <= R
            if (B2 > 0.0) hi = std::min(hi, R / B2);
            else if (B2 < 0.0) lo = std::max(lo, R / B2);
            else if (R < 0.0) empty = true;
        }
        if (empty) { xb = xa - 1; return; }
        // px = x + 0.5, then one column of slack on each side for the rounding of this arithmetic itself.
        const double fa = std::max((double)xa, std::floor(lo - 1.5));
        const double fb = std::min((double)xb, std::ceil(hi + 0.5));
        if (!(fa <= fb)) { xb = xa - 1; return; }
        xa = (int)fa; xb = (int)fb;
    }

private:
    bool valid_ = false;
    double x_[3] = {0, 0, 0}, y_[3] = {0, 0, 0};
    double sgn_ = 1.0, absInv_ = 1.0, edgeMin_ = 0.0, uMax_ = 0.0, slope0_ = 0.0, slope1_ = 0.0;
};

/// Diagnostic switch (tests, the benchmark): true makes RasterDepthTriangle run the plain loop over the whole bounding
/// box, one texel at a time, which is what the shadow pass did before the row spans and the four texel loop existed.
inline bool& TriangleSpanDisabled() { static bool v = false; return v; }

/// Rasterize one triangle into a min-depth buffer `depth` (S x S texels, row major): every texel whose centre passes
/// the three edge tests gets min(old, z), z interpolated from `sz`. The arithmetic per texel is the loop the sun's
/// cascades always used; only the texels that cannot be covered are skipped (see TriangleRows).
inline void RasterDepthTriangle(float* depth, int S, const float sxv[3], const float syv[3], const float szv[3]) {
    int minx = (int)std::floor(std::fmin(sxv[0], std::fmin(sxv[1], sxv[2])));
    int maxx = (int)std::ceil (std::fmax(sxv[0], std::fmax(sxv[1], sxv[2])));
    int miny = (int)std::floor(std::fmin(syv[0], std::fmin(syv[1], syv[2])));
    int maxy = (int)std::ceil (std::fmax(syv[0], std::fmax(syv[1], syv[2])));
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx >= S) maxx = S - 1;
    if (maxy >= S) maxy = S - 1;
    float area = (sxv[1] - sxv[0]) * (syv[2] - syv[0]) - (sxv[2] - sxv[0]) * (syv[1] - syv[0]);
    if (area == 0.0f) return;
    float inv = 1.0f / area;
    if (TriangleSpanDisabled()) {   // the plain loop, as before
        for (int y = miny; y <= maxy; ++y)
            for (int x = minx; x <= maxx; ++x) {
                float px = x + 0.5f, py = y + 0.5f;
                float w0 = ((sxv[1] - px) * (syv[2] - py) - (sxv[2] - px) * (syv[1] - py)) * inv;
                float w1 = ((sxv[2] - px) * (syv[0] - py) - (sxv[0] - px) * (syv[2] - py)) * inv;
                float w2 = 1.0f - w0 - w1;
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                float z = w0 * szv[0] + w1 * szv[1] + w2 * szv[2];
                float& dref = depth[(std::size_t)y * S + x];
                if (z < dref) dref = z;
            }
        return;
    }
    // Only a box with room to skip is worth the per row set-up (a small triangle's box is nearly all triangle).
    TriangleRows rows;
    const bool narrow = (maxx - minx) >= 24 && (maxy - miny) >= 8 && rows.Init(sxv, syv, inv, 0.0f, minx, maxx);
#if defined(OKAY_TRIANGLE_SPAN_SSE2)
    // Four texels at a time. Every lane does exactly the float operations of the scalar loop below, in the same order,
    // with the same single precision rounding (SSE2 has no extended precision and this code is never contracted into
    // multiply-adds), so a lane's result is the scalar result bit for bit.
    const __m128 vX0 = _mm_set1_ps(sxv[0]), vX1 = _mm_set1_ps(sxv[1]), vX2 = _mm_set1_ps(sxv[2]);
    const __m128 vS0 = _mm_set1_ps(szv[0]), vS1 = _mm_set1_ps(szv[1]), vS2 = _mm_set1_ps(szv[2]);
    const __m128 vInv = _mm_set1_ps(inv), vHalf = _mm_set1_ps(0.5f), vOne = _mm_set1_ps(1.0f), vZero = _mm_setzero_ps();
    const __m128i vLane = _mm_set_epi32(3, 2, 1, 0);
#endif
    for (int y = miny; y <= maxy; ++y) {
        int xa = minx, xb = maxx;
        if (narrow) rows.Row(y, xa, xb);
        const float py = y + 0.5f;
        const float dy0 = syv[0] - py, dy1 = syv[1] - py, dy2 = syv[2] - py;   // the same differences the per texel code takes
        float* row = depth + (std::size_t)y * S;
        int x = xa;
#if defined(OKAY_TRIANGLE_SPAN_SSE2)
        const __m128 vDy0 = _mm_set1_ps(dy0), vDy1 = _mm_set1_ps(dy1), vDy2 = _mm_set1_ps(dy2);
        for (; x + 3 <= xb; x += 4) {
            const __m128 px = _mm_add_ps(_mm_cvtepi32_ps(_mm_add_epi32(_mm_set1_epi32(x), vLane)), vHalf);   // x + 0.5f
            const __m128 ex1 = _mm_sub_ps(vX1, px), ex2 = _mm_sub_ps(vX2, px), ex0 = _mm_sub_ps(vX0, px);
            const __m128 w0 = _mm_mul_ps(_mm_sub_ps(_mm_mul_ps(ex1, vDy2), _mm_mul_ps(ex2, vDy1)), vInv);
            const __m128 w1 = _mm_mul_ps(_mm_sub_ps(_mm_mul_ps(ex2, vDy0), _mm_mul_ps(ex0, vDy2)), vInv);
            const __m128 w2 = _mm_sub_ps(_mm_sub_ps(vOne, w0), w1);
            // The scalar test is `if (w0 < 0 || w1 < 0 || w2 < 0) continue;`: a lane passes when none of them is below 0.
            const __m128 fail = _mm_or_ps(_mm_or_ps(_mm_cmplt_ps(w0, vZero), _mm_cmplt_ps(w1, vZero)), _mm_cmplt_ps(w2, vZero));
            const __m128 z = _mm_add_ps(_mm_add_ps(_mm_mul_ps(w0, vS0), _mm_mul_ps(w1, vS1)), _mm_mul_ps(w2, vS2));
            const __m128 old = _mm_loadu_ps(row + x);
            const __m128 write = _mm_andnot_ps(fail, _mm_cmplt_ps(z, old));      // covered and nearer
            _mm_storeu_ps(row + x, _mm_or_ps(_mm_and_ps(write, z), _mm_andnot_ps(write, old)));
        }
#endif
        for (; x <= xb; ++x) {
            float px = x + 0.5f;
            float w0 = ((sxv[1] - px) * dy2 - (sxv[2] - px) * dy1) * inv;
            float w1 = ((sxv[2] - px) * dy0 - (sxv[0] - px) * dy2) * inv;
            float w2 = 1.0f - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            float z = w0 * szv[0] + w1 * szv[1] + w2 * szv[2];
            float& dref = row[x];
            if (z < dref) dref = z;
        }
    }
}

} // namespace detail
} // namespace okay
