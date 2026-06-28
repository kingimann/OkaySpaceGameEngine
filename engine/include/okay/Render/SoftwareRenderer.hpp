#pragma once
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Vec4.hpp"
#include "okay/Math/Mat4.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Render/Lighting.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Graphics/Image.hpp"
#include "okay/Core/Time.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>
#include <thread>

namespace okay {

/// Run `fn(rowStart, rowEnd)` over [y0, y1) split across hardware threads. Used to
/// parallelize the embarrassingly-parallel full-screen passes (post-processing and
/// downsample), where each row writes independent pixels. Falls back to a single
/// call for small ranges so tiny images don't pay thread-spawn overhead.
template <class Fn>
inline void ParallelRows(int y0, int y1, Fn&& fn) {
    int rows = y1 - y0;
    if (rows <= 0) return;
    unsigned hc = std::thread::hardware_concurrency();
    int n = (int)(hc == 0 ? 1 : hc);
    if (n > 8) n = 8;
    if (n <= 1 || rows < 64) { fn(y0, y1); return; }
    std::vector<std::thread> ts; ts.reserve(n - 1);
    int chunk = (rows + n - 1) / n;
    for (int t = 1; t < n; ++t) {
        int a = y0 + t * chunk, b = a + chunk; if (b > y1) b = y1;
        if (a >= b) break;
        ts.emplace_back([&fn, a, b] { fn(a, b); });
    }
    int firstEnd = y0 + chunk; if (firstEnd > y1) firstEnd = y1;
    fn(y0, firstEnd);                 // this thread does the first chunk
    for (auto& th : ts) th.join();
}

// Directional shadow map (depth from the light). Defined up here so the Raster's
// per-pixel shading can consult it; the implementation lives below RenderMeshes.
struct ShadowMap {
    std::vector<float> depth;   // min light-space NDC depth per texel
    int   size = 0;
    Mat4  viewProj;
    float texelWorld = 0.0f;    // world size of one shadow texel (for normal-offset bias)
    bool  enabled = false;
};
inline ShadowMap& Shadows();
inline float ShadowFactor(const Vec3& wpos, const Vec3& n);

// Fresnel rim light: a soft glow on surfaces facing away from the camera (grazing
// angles), which separates objects from the background and reads as a subtle
// backlight. Tunable; applied in the per-pixel path. Defined here so the Raster's
// shading can use them.
inline bool&  RimLightEnabled() { static bool v = false; return v; }  // off by default: the grazing-angle glow swept across big floors as the camera moved (opt-in in the Rendering panel)
inline float& RimStrength()     { static float v = 0.25f; return v; }
inline float& RimPower()        { static float v = 3.0f; return v; }

// Hemisphere ambient: tint the ambient term by sky (up) vs ground (down). Strength
// scales how far each hemisphere departs from the flat ambient level.
inline bool&  HemisphereAmbient()  { static bool v = true; return v; }
inline float& HemisphereStrength() { static float v = 0.7f; return v; }

// Environment sky for reflections: the renderer keeps its own copy of the scene's
// sky gradient (the actual sky is drawn screen-space by the player/editor, so the
// renderer can't see it). The per-pixel path mirrors this on glossy/reflective
// surfaces. RenderMeshes refreshes it from the scene's RenderSettings each frame.
struct EnvSkyData {
    Vec3 top{0.27f, 0.47f, 0.78f}, horizon{0.59f, 0.73f, 0.88f}, bottom{0.47f, 0.47f, 0.51f};
    bool enabled = false;
};
inline EnvSkyData& EnvSky() { static EnvSkyData e; return e; }

/// Sample the sky gradient by a world-space direction's vertical component:
/// horizon at the equator, fading to `top` looking up and `bottom` looking down.
inline Vec3 SampleEnvSky(const Vec3& dir) {
    EnvSkyData& e = EnvSky();
    float y = dir.y < -1.0f ? -1.0f : (dir.y > 1.0f ? 1.0f : dir.y);
    const Vec3& a = e.horizon;
    const Vec3& b = y >= 0.0f ? e.top : e.bottom;
    float t = y >= 0.0f ? y : -y;
    return Vec3{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

/// A tiny software rasterizer with a per-pixel depth buffer, so overlapping 3D
/// triangles occlude correctly (unlike a painter's-algorithm sort). It fills an
/// ABGR8888 pixel buffer that both the player (SDL texture) and the editor
/// (ImGui image) display. Flat-shaded; this is the engine's reference 3D path.
class Raster {
public:
    int width = 0, height = 0;
    std::vector<std::uint32_t> color;   // ABGR8888 (matches SDL_PIXELFORMAT_ABGR8888)
    std::vector<float>         depth;    // smaller = nearer; cleared to +inf
    // Optional G-buffer for screen-space ambient occlusion (filled by the
    // per-pixel path when SSAO is enabled; empty otherwise).
    std::vector<Vec3>          gpos;     // world position per pixel
    std::vector<Vec3>          gnrm;     // world normal per pixel
    std::vector<std::uint8_t>  gvalid;   // 1 where gpos/gnrm were written

    void Resize(int w, int h) {
        width = w < 1 ? 1 : w;
        height = h < 1 ? 1 : h;
        color.assign((std::size_t)width * height, 0u);
        // W-buffer: store 1/w (reciprocal view depth). Larger = nearer, far -> 0.
        // 1/w has uniform precision (unlike z/w, which is terrible far from a small
        // near plane and causes z-fighting/flicker), so a small near clip works
        // everywhere — no need to raise the near plane. Cleared to 0 = infinitely far.
        depth.assign((std::size_t)width * height, 0.0f);
    }

    /// Clear color (ABGR) and reset the W-buffer to "far" (1/w = 0).
    void Clear(std::uint32_t abgr) {
        std::fill(color.begin(), color.end(), abgr);
        std::fill(depth.begin(), depth.end(), 0.0f);
    }

    static std::uint32_t Pack(const Color& c, float shade = 1.0f) {
        auto b = [](float v) { v = v < 0 ? 0 : (v > 1 ? 1 : v); return (std::uint32_t)(v * 255.0f + 0.5f); };
        return (0xFFu << 24) | (b(c.b * shade) << 16) | (b(c.g * shade) << 8) | b(c.r * shade);
    }
    /// Pack already-computed linear RGB (each clamped to [0,1]).
    static std::uint32_t PackRGB(float r, float g, float bl) {
        auto b = [](float v) { v = v < 0 ? 0 : (v > 1 ? 1 : v); return (std::uint32_t)(v * 255.0f + 0.5f); };
        return (0xFFu << 24) | (b(bl) << 16) | (b(g) << 8) | b(r);
    }

    std::uint32_t Get(int x, int y) const { return color[(std::size_t)y * width + x]; }
    float Depth(int x, int y) const { return depth[(std::size_t)y * width + x]; }

    /// Top-left fill rule (what a GPU rasterizer does): per-edge acceptance bias so a
    /// pixel exactly on a shared edge is claimed by EXACTLY ONE of the two triangles
    /// — the one for which that edge is a top or left edge. The two triangles of a
    /// quad traverse the shared edge in opposite directions, so they always make
    /// opposite choices: no crack, and no overlapping z-fight shimmer. b0/b1/b2 apply
    /// to the barycentrics w0/w1/w2 (edges opposite v0/v1/v2). This replaces the old
    /// fat `-1e-3` overlap bias, whose ~1px band z-fought on big triangles (the
    /// shimmer). Cost: a few ops per triangle, nothing per pixel.
    static void EdgeBias(float x0, float y0, float x1, float y1, float x2, float y2,
                         double area, double& b0, double& b1, double& b2) {
        const double s = area < 0.0 ? -1.0 : 1.0;       // orient by winding
        auto tl = [s](double ax, double ay, double bx, double by) -> double {
            const double dx = (bx - ax) * s, dy = (by - ay) * s;   // edge a->b in winding order
            // y-down screen: a LEFT edge goes up (dy<0); a TOP edge is horizontal,
            // going left (dy==0 && dx<0). Boundary belongs to top-left edges.
            const bool topLeft = (dy < 0.0) || (dy == 0.0 && dx < 0.0);
            return topLeft ? -1e-9 : 1e-7;   // include boundary (>=0) vs exclude (>0)
        };
        b0 = tl(x1, y1, x2, y2);   // edge opposite v0
        b1 = tl(x2, y2, x0, y0);   // edge opposite v1
        b2 = tl(x0, y0, x1, y1);   // edge opposite v2
    }

    /// Rasterize a triangle given screen-space points (pixels) + per-vertex depth
    /// (camera distance; smaller = nearer), depth-tested per pixel.
    void Triangle(float x0, float y0, float d0,
                  float x1, float y1, float d1,
                  float x2, float y2, float d2, std::uint32_t abgr,
                  int clipY0 = 0, int clipY1 = (1 << 30)) {
        int minX = (int)std::floor(std::fmin(x0, std::fmin(x1, x2)));
        int maxX = (int)std::ceil (std::fmax(x0, std::fmax(x1, x2)));
        int minY = (int)std::floor(std::fmin(y0, std::fmin(y1, y2)));
        int maxY = (int)std::ceil (std::fmax(y0, std::fmax(y1, y2)));
        if (minX < 0) minX = 0;
        if (minY < 0) minY = 0;
        if (maxX >= width) maxX = width - 1;
        if (maxY >= height) maxY = height - 1;
        if (minY < clipY0) minY = clipY0; if (maxY > clipY1) maxY = clipY1;   // thread band

        float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
        if (area == 0.0f) return;
        float inv = 1.0f / area;
        // Incremental barycentric (linear in x): compute per scanline, step per pixel.
        // Double precision avoids float cancellation across screen-filling triangles.
        const double dInv = (double)inv;
        const double dw0 = (double)(y1 - y2) * dInv;
        const double dw1 = (double)(y2 - y0) * dInv;
        double b0, b1, b2; EdgeBias(x0, y0, x1, y1, x2, y2, (double)area, b0, b1, b2);
        for (int y = minY; y <= maxY; ++y) {
            const double py = y + 0.5, pxs = minX + 0.5;
            double w0 = ((x1 - pxs) * (y2 - py) - (x2 - pxs) * (y1 - py)) * dInv;
            double w1 = ((x2 - pxs) * (y0 - py) - (x0 - pxs) * (y2 - py)) * dInv;
            const std::size_t rowBase = (std::size_t)y * width;
            for (int x = minX; x <= maxX; ++x, w0 += dw0, w1 += dw1) {
                double w2 = 1.0 - w0 - w1;
                // Top-left fill rule: a shared edge belongs to exactly one triangle,
                // so no crack and no overlapping z-fight shimmer (GPU-style).
                if (w0 < b0 || w1 < b1 || w2 < b2) continue;
                float d = (float)(w0 * d0 + w1 * d1 + w2 * d2);
                std::size_t i = rowBase + x;
                if (d > depth[i] + 1e-6f) { depth[i] = d; color[i] = abgr; }   // W-buffer: larger 1/w = nearer
            }
        }
    }

    /// Gouraud triangle: per-vertex light color (LR/LG/LB) interpolated across
    /// the face and multiplied by a constant per-face albedo `base`, plus a
    /// constant specular + emissive, with optional per-tri fog. This is what
    /// makes low-poly organic meshes look smooth without extra geometry.
    void TriangleSmooth(const float* X, const float* Y, const float* D,
                        const float* LR, const float* LG, const float* LB,
                        const Color& base, const float* SP, float er, float eg, float eb,
                        const float* FOG, float fr, float fg, float fb,
                        int clipY0 = 0, int clipY1 = (1 << 30)) {
        int minX = (int)std::floor(std::fmin(X[0], std::fmin(X[1], X[2])));
        int maxX = (int)std::ceil (std::fmax(X[0], std::fmax(X[1], X[2])));
        int minY = (int)std::floor(std::fmin(Y[0], std::fmin(Y[1], Y[2])));
        int maxY = (int)std::ceil (std::fmax(Y[0], std::fmax(Y[1], Y[2])));
        if (minX < 0) minX = 0;
        if (minY < 0) minY = 0;
        if (maxX >= width) maxX = width - 1;
        if (maxY >= height) maxY = height - 1;
        if (minY < clipY0) minY = clipY0; if (maxY > clipY1) maxY = clipY1;   // thread band
        float area = (X[1] - X[0]) * (Y[2] - Y[0]) - (X[2] - X[0]) * (Y[1] - Y[0]);
        if (area == 0.0f) return;
        float inv = 1.0f / area;
        // Incremental barycentric: w0,w1 are linear in x, so compute them once at the
        // start of each row and add a constant step per pixel — no per-pixel multiplies
        // for the edge functions (the big fill-rate win). Hoist fog/specular: skip
        // their per-pixel interpolation entirely when the triangle has neither.
        const double dInv = (double)inv;
        const double dw0 = (double)(Y[1] - Y[2]) * dInv;   // d(w0)/dx
        const double dw1 = (double)(Y[2] - Y[0]) * dInv;   // d(w1)/dx
        double b0, b1, b2; EdgeBias(X[0], Y[0], X[1], Y[1], X[2], Y[2], (double)area, b0, b1, b2);
        const bool anySpec = SP[0] != 0.0f || SP[1] != 0.0f || SP[2] != 0.0f;
        const bool anyFog  = FOG[0] != 0.0f || FOG[1] != 0.0f || FOG[2] != 0.0f;
        for (int y = minY; y <= maxY; ++y) {
            const double py = y + 0.5, pxs = minX + 0.5;
            double w0 = ((X[1] - pxs) * (Y[2] - py) - (X[2] - pxs) * (Y[1] - py)) * dInv;
            double w1 = ((X[2] - pxs) * (Y[0] - py) - (X[0] - pxs) * (Y[2] - py)) * dInv;
            const std::size_t row = (std::size_t)y * width;
            for (int x = minX; x <= maxX; ++x, w0 += dw0, w1 += dw1) {
                double w2 = 1.0 - w0 - w1;
                if (w0 < b0 || w1 < b1 || w2 < b2) continue;   // top-left fill rule
                float d = (float)(w0 * D[0] + w1 * D[1] + w2 * D[2]);
                std::size_t i = row + x;
                if (d <= depth[i] + 1e-6f) continue;   // W-buffer: larger 1/w = nearer; bias = first-drawn wins ties
                float lr = (float)(w0 * LR[0] + w1 * LR[1] + w2 * LR[2]);
                float lg = (float)(w0 * LG[0] + w1 * LG[1] + w2 * LG[2]);
                float lb = (float)(w0 * LB[0] + w1 * LB[1] + w2 * LB[2]);
                float spec = anySpec ? (float)(w0 * SP[0] + w1 * SP[1] + w2 * SP[2]) : 0.0f;
                float cr = base.r * lr + spec + er;
                float cg = base.g * lg + spec + eg;
                float cb = base.b * lb + spec + eb;
                if (anyFog) {
                    float fog = (float)(w0 * FOG[0] + w1 * FOG[1] + w2 * FOG[2]);
                    cr = cr * (1.0f - fog) + fr * fog;
                    cg = cg * (1.0f - fog) + fg * fog;
                    cb = cb * (1.0f - fog) + fb * fog;
                }
                depth[i] = d;
                color[i] = PackRGB(cr, cg, cb);
            }
        }
    }

    /// Textured triangle: perspective-correct UVs (pass u/w, v/w and 1/w per
    /// vertex). Samples `img` (wrapped), multiplies by `tint` and lighting
    /// `shade`, adds `spec` + emissive `(er,eg,eb)`. Depth-tested per pixel.
    // Bilinear texture lookup with wrapping (one mip level).
    static Color BilerpWrap(const Image& img, float u, float v) {
        int tw = img.Width(), th = img.Height();
        if (tw <= 0 || th <= 0) return Color(1, 1, 1, 1);
        auto wr = [](int a, int n) { a %= n; return a < 0 ? a + n : a; };
        float fu = u * tw - 0.5f, fv = (1.0f - v) * th - 0.5f;
        int x0 = (int)std::floor(fu), y0 = (int)std::floor(fv);
        float ax = fu - x0, ay = fv - y0;
        Color c00 = img.GetPixel(wr(x0, tw), wr(y0, th)), c10 = img.GetPixel(wr(x0 + 1, tw), wr(y0, th));
        Color c01 = img.GetPixel(wr(x0, tw), wr(y0 + 1, th)), c11 = img.GetPixel(wr(x0 + 1, tw), wr(y0 + 1, th));
        float r0 = c00.r + (c10.r - c00.r) * ax, r1 = c01.r + (c11.r - c01.r) * ax;
        float g0 = c00.g + (c10.g - c00.g) * ax, g1 = c01.g + (c11.g - c01.g) * ax;
        float b0 = c00.b + (c10.b - c00.b) * ax, b1 = c01.b + (c11.b - c01.b) * ax;
        return Color(r0 + (r1 - r0) * ay, g0 + (g1 - g0) * ay, b0 + (b1 - b0) * ay, 1.0f);
    }
    // Trilinear mipmap sample: bilinear within the two nearest mip levels, blended.
    static Color SampleMips(const std::vector<Image>& mips, float u, float v, float lod) {
        int n = (int)mips.size();
        if (n == 0) return Color(1, 1, 1, 1);
        float maxl = (float)(n - 1);
        lod = lod < 0 ? 0 : (lod > maxl ? maxl : lod);
        int l0 = (int)lod, l1 = l0 + 1 < n ? l0 + 1 : l0;
        float f = lod - l0;
        Color a = BilerpWrap(mips[l0], u, v);
        if (f <= 0.001f || l1 == l0) return a;
        Color b = BilerpWrap(mips[l1], u, v);
        return Color(a.r + (b.r - a.r) * f, a.g + (b.g - a.g) * f, a.b + (b.b - a.b) * f, 1.0f);
    }
    // Per-vertex screen-space gradients of the (perspective) barycentric-weighted
    // attribute numerators/denominator, used to pick the mip level PER PIXEL. The
    // perspective-correct texcoord is u = A/B with A = interp(U/w), B = interp(1/w),
    // both affine in screen (px,py); their constant gradients come from the edge
    // functions. (Per-pixel LOD is essential for big triangles like a ground plane,
    // where one LOD for the whole triangle would leave the far end aliased.)
    struct LodGrad { float dAx, dAy, dVx, dVy, dBx, dBy; };
    static LodGrad MakeLodGrad(const float* X, const float* Y, float inv,
                               const float* U, const float* V, const float* IW) {
        float g0x = (Y[1] - Y[2]) * inv, g0y = (X[2] - X[1]) * inv;
        float g1x = (Y[2] - Y[0]) * inv, g1y = (X[0] - X[2]) * inv;
        float g2x = (Y[0] - Y[1]) * inv, g2y = (X[1] - X[0]) * inv;
        return {U[0]*g0x + U[1]*g1x + U[2]*g2x, U[0]*g0y + U[1]*g1y + U[2]*g2y,
                V[0]*g0x + V[1]*g1x + V[2]*g2x, V[0]*g0y + V[1]*g1y + V[2]*g2y,
                IW[0]*g0x + IW[1]*g1x + IW[2]*g2x, IW[0]*g0y + IW[1]*g1y + IW[2]*g2y};
    }
    static float PixelLod(float A, float Av, float B, const LodGrad& g, int tw, int th) {
        float B2 = B * B;
        if (B2 < 1e-20f) return 0.0f;
        float dudx = (g.dAx * B - A * g.dBx) / B2, dvdx = (g.dVx * B - Av * g.dBx) / B2;
        float dudy = (g.dAy * B - A * g.dBy) / B2, dvdy = (g.dVy * B - Av * g.dBy) / B2;
        float l2x = dudx * dudx + dvdx * dvdx, l2y = dudy * dudy + dvdy * dvdy;
        float m = (l2x > l2y ? l2x : l2y) * (float)tw * (float)th;
        return 0.5f * std::log2(m > 1e-12f ? m : 1e-12f);
    }

    void TriangleTex(const float* X, const float* Y, const float* D,
                     const float* IW, const float* U, const float* V,
                     const std::vector<Image>& mips, const Color& tint,
                     const float* LR, const float* LG, const float* LB, const float* SP,
                     float er, float eg, float eb,
                     const float* FOG, float fr, float fg, float fb,
                     int clipY0 = 0, int clipY1 = (1 << 30)) {
        int minX = (int)std::floor(std::fmin(X[0], std::fmin(X[1], X[2])));
        int maxX = (int)std::ceil (std::fmax(X[0], std::fmax(X[1], X[2])));
        int minY = (int)std::floor(std::fmin(Y[0], std::fmin(Y[1], Y[2])));
        int maxY = (int)std::ceil (std::fmax(Y[0], std::fmax(Y[1], Y[2])));
        if (minX < 0) minX = 0;
        if (minY < 0) minY = 0;
        if (maxX >= width) maxX = width - 1;
        if (maxY >= height) maxY = height - 1;
        if (minY < clipY0) minY = clipY0; if (maxY > clipY1) maxY = clipY1;   // thread band
        float area = (X[1] - X[0]) * (Y[2] - Y[0]) - (X[2] - X[0]) * (Y[1] - Y[0]);
        if (area == 0.0f || mips.empty()) return;
        float inv = 1.0f / area;
        int tw = mips[0].Width(), th = mips[0].Height();
        if (tw <= 0 || th <= 0) return;
        LodGrad lg2 = MakeLodGrad(X, Y, inv, U, V, IW);
        const double dInv = (double)inv;
        const double dw0 = (double)(Y[1] - Y[2]) * dInv;   // incremental barycentric steps
        const double dw1 = (double)(Y[2] - Y[0]) * dInv;
        double b0, b1, b2; EdgeBias(X[0], Y[0], X[1], Y[1], X[2], Y[2], (double)area, b0, b1, b2);
        const bool anySpec = SP[0] != 0.0f || SP[1] != 0.0f || SP[2] != 0.0f;
        const bool anyFog  = FOG[0] != 0.0f || FOG[1] != 0.0f || FOG[2] != 0.0f;
        for (int y = minY; y <= maxY; ++y) {
            const double py = y + 0.5, pxs = minX + 0.5;
            double w0 = ((X[1] - pxs) * (Y[2] - py) - (X[2] - pxs) * (Y[1] - py)) * dInv;
            double w1 = ((X[2] - pxs) * (Y[0] - py) - (X[0] - pxs) * (Y[2] - py)) * dInv;
            const std::size_t rowBase = (std::size_t)y * width;
            for (int x = minX; x <= maxX; ++x, w0 += dw0, w1 += dw1) {
                double w2 = 1.0 - w0 - w1;
                if (w0 < b0 || w1 < b1 || w2 < b2) continue;   // top-left fill rule
                float d = (float)(w0 * D[0] + w1 * D[1] + w2 * D[2]);
                std::size_t i = rowBase + x;
                if (d <= depth[i] + 1e-6f) continue;   // W-buffer: larger 1/w = nearer
                float A = (float)(w0 * U[0] + w1 * U[1] + w2 * U[2]);
                float Av = (float)(w0 * V[0] + w1 * V[1] + w2 * V[2]);
                float iw = (float)(w0 * IW[0] + w1 * IW[1] + w2 * IW[2]);
                if (iw == 0.0f) continue;
                float u = A / iw, v = Av / iw;
                Color tc = SampleMips(mips, u, v, PixelLod(A, Av, iw, lg2, tw, th));
                float lr = (float)(w0 * LR[0] + w1 * LR[1] + w2 * LR[2]);
                float lg = (float)(w0 * LG[0] + w1 * LG[1] + w2 * LG[2]);
                float lb = (float)(w0 * LB[0] + w1 * LB[1] + w2 * LB[2]);
                float spec = anySpec ? (float)(w0 * SP[0] + w1 * SP[1] + w2 * SP[2]) : 0.0f;
                float cr = tc.r * tint.r * lr + spec + er;
                float cg = tc.g * tint.g * lg + spec + eg;
                float cb = tc.b * tint.b * lb + spec + eb;
                if (anyFog) {
                    float fog = (float)(w0 * FOG[0] + w1 * FOG[1] + w2 * FOG[2]);
                    cr = cr * (1.0f - fog) + fr * fog;
                    cg = cg * (1.0f - fog) + fg * fog;
                    cb = cb * (1.0f - fog) + fb * fog;
                }
                depth[i] = d;
                color[i] = PackRGB(cr, cg, cb);
            }
        }
    }

    /// Per-pixel (Phong) lit triangle: interpolates the world normal + position
    /// across the face and shades EACH pixel (diffuse from all scene lights +
    /// Blinn-Phong specular), so curved/low-poly surfaces look smooth and specular
    /// highlights land correctly instead of being smeared between vertices. An
    /// optional texture (perspective-correct, bilinear) tints the surface.
    void TrianglePhong(const float* X, const float* Y, const float* D, const float* IW,
                       const float* U, const float* V,
                       const float* NXa, const float* NYa, const float* NZa,
                       const float* WXa, const float* WYa, const float* WZa,
                       const std::vector<Image>* mips, const Color& base, const Color& tint, const Vec3& eye,
                       float shininess, float specularK, float er, float eg, float eb,
                       const float* FOG, float fr, float fg, float fb,
                       const std::vector<Image>* normalMips = nullptr,
                       const Vec3& tangent = Vec3{1, 0, 0}, float normalStrength = 1.0f,
                       float reflectivity = 0.0f,
                       const std::vector<Image>* specMips = nullptr,
                       float metallic = 0.0f,
                       int toonBands = 0,
                       float matRimStr = 0.0f, float matRimPow = 3.0f,
                       float rimR = 1.0f, float rimG = 1.0f, float rimB = 1.0f,
                       bool triplanar = false, float triTileX = 1.0f, float triTileY = 1.0f,
                       int clipY0 = 0, int clipY1 = (1 << 30),
                       int shaderMode = 0, const float* gradTop = nullptr, const float* gradBot = nullptr) {
        int minX = (int)std::floor(std::fmin(X[0], std::fmin(X[1], X[2])));
        int maxX = (int)std::ceil (std::fmax(X[0], std::fmax(X[1], X[2])));
        int minY = (int)std::floor(std::fmin(Y[0], std::fmin(Y[1], Y[2])));
        int maxY = (int)std::ceil (std::fmax(Y[0], std::fmax(Y[1], Y[2])));
        if (minX < 0) minX = 0;
        if (minY < 0) minY = 0;
        if (maxX >= width) maxX = width - 1;
        if (maxY >= height) maxY = height - 1;
        if (minY < clipY0) minY = clipY0; if (maxY > clipY1) maxY = clipY1;   // thread band
        float area = (X[1] - X[0]) * (Y[2] - Y[0]) - (X[2] - X[0]) * (Y[1] - Y[0]);
        if (area == 0.0f) return;
        float inv = 1.0f / area;
        const bool textured = mips && !mips->empty() && (*mips)[0].Width() > 0;
        int tw = textured ? (*mips)[0].Width() : 0, th = textured ? (*mips)[0].Height() : 0;
        LodGrad lg2 = textured ? MakeLodGrad(X, Y, inv, U, V, IW) : LodGrad{};
        const bool bump = normalMips && !normalMips->empty() && (*normalMips)[0].Width() > 0;
        int nw = bump ? (*normalMips)[0].Width() : 0, nh = bump ? (*normalMips)[0].Height() : 0;
        LodGrad lgN = bump ? MakeLodGrad(X, Y, inv, U, V, IW) : LodGrad{};
        const bool glossMap = specMips && !specMips->empty() && (*specMips)[0].Width() > 0;
        int gw = glossMap ? (*specMips)[0].Width() : 0, gh = glossMap ? (*specMips)[0].Height() : 0;
        LodGrad lgG = glossMap ? MakeLodGrad(X, Y, inv, U, V, IW) : LodGrad{};
        // Hoist the global render-state out of the per-pixel loop: these accessors
        // each carry a function-call + thread-safe-static-guard cost that adds up
        // over a million pixels.
        const bool  rimOn     = RimLightEnabled() && RimStrength() > 0.0f;
        const float rimStr    = RimStrength();
        const float rimPow    = RimPower();
        const bool  shadowsOn = Shadows().enabled;
        const bool  envOn     = EnvSky().enabled;
        // Incremental barycentric (linear in x): per-scanline init, per-pixel step.
        // Double precision avoids float cancellation across screen-filling triangles.
        const double dInv = (double)inv;
        const double dw0 = (double)(Y[1] - Y[2]) * dInv;
        const double dw1 = (double)(Y[2] - Y[0]) * dInv;
        double b0, b1, b2; EdgeBias(X[0], Y[0], X[1], Y[1], X[2], Y[2], (double)area, b0, b1, b2);
        for (int y = minY; y <= maxY; ++y) {
            const double py = y + 0.5, pxs = minX + 0.5;
            double w0 = ((X[1] - pxs) * (Y[2] - py) - (X[2] - pxs) * (Y[1] - py)) * dInv;
            double w1 = ((X[2] - pxs) * (Y[0] - py) - (X[0] - pxs) * (Y[2] - py)) * dInv;
            const std::size_t rowBase = (std::size_t)y * width;
            for (int x = minX; x <= maxX; ++x, w0 += dw0, w1 += dw1) {
                double w2 = 1.0 - w0 - w1;
                if (w0 < b0 || w1 < b1 || w2 < b2) continue;   // top-left fill rule
                float d = (float)(w0 * D[0] + w1 * D[1] + w2 * D[2]);
                std::size_t i = rowBase + x;
                if (d <= depth[i] + 1e-6f) continue;   // W-buffer: larger 1/w = nearer
                // Perspective-correct interpolation of world normal + position
                // (weight each vertex by 1/w). Affine interpolation is badly wrong
                // for large triangles — e.g. a ground quad would sample shadows and
                // point lights at the wrong world point.
                float iws = w0 * IW[0] + w1 * IW[1] + w2 * IW[2];
                if (iws == 0.0f) continue;
                float a0 = w0 * IW[0] / iws, a1 = w1 * IW[1] / iws, a2 = w2 * IW[2] / iws;
                Vec3 n{a0 * NXa[0] + a1 * NXa[1] + a2 * NXa[2],
                       a0 * NYa[0] + a1 * NYa[1] + a2 * NYa[2],
                       a0 * NZa[0] + a1 * NZa[1] + a2 * NZa[2]};
                n = n.Normalized();
                if (bump) {
                    float iwn = w0 * IW[0] + w1 * IW[1] + w2 * IW[2];
                    if (iwn != 0.0f) {
                        float un = (w0 * U[0] + w1 * U[1] + w2 * U[2]) / iwn;
                        float vn = (w0 * V[0] + w1 * V[1] + w2 * V[2]) / iwn;
                        Color nc = SampleMips(*normalMips, un, vn,
                                              PixelLod(un * iwn, vn * iwn, iwn, lgN, nw, nh));
                        // Tangent-space normal (0..1 -> -1..1), scaled by strength.
                        Vec3 tn{(nc.r * 2.0f - 1.0f) * normalStrength,
                                (nc.g * 2.0f - 1.0f) * normalStrength,
                                 nc.b * 2.0f - 1.0f};
                        // Build an orthonormal TBN around the interpolated normal.
                        Vec3 T = tangent - n * Vec3::Dot(n, tangent);
                        float tl = T.Magnitude();
                        if (tl > 1e-6f) {
                            T = T * (1.0f / tl);
                            Vec3 B = Vec3::Cross(n, T);
                            Vec3 pn = T * tn.x + B * tn.y + n * tn.z;
                            float pl = pn.Magnitude();
                            if (pl > 1e-6f) n = pn * (1.0f / pl);
                        }
                    }
                }
                Vec3 wpos{a0 * WXa[0] + a1 * WXa[1] + a2 * WXa[2],
                          a0 * WYa[0] + a1 * WYa[1] + a2 * WYa[2],
                          a0 * WZa[0] + a1 * WZa[1] + a2 * WZa[2]};
                Vec3 lit = SceneLights::ShadeNormalized(wpos, n);   // n already unit
                // Specular/gloss map: a grayscale texture whose luminance scales the
                // specular highlight + reflection per texel (shiny tape on a matte
                // box, wet patches, worn metal). 1.0 (full) when no map is bound.
                float gloss = 1.0f;
                if (glossMap) {
                    float iwg = w0 * IW[0] + w1 * IW[1] + w2 * IW[2];
                    if (iwg != 0.0f) {
                        float ug = (w0 * U[0] + w1 * U[1] + w2 * U[2]) / iwg;
                        float vg = (w0 * V[0] + w1 * V[1] + w2 * V[2]) / iwg;
                        Color gc = SampleMips(*specMips, ug, vg, PixelLod(ug * iwg, vg * iwg, iwg, lgG, gw, gh));
                        gloss = 0.2126f * gc.r + 0.7152f * gc.g + 0.0722f * gc.b;
                    }
                }
                // Cast shadows: fade the direct light toward the ambient floor for
                // fragments occluded from the light (specular is shadowed below).
                float sh = 1.0f;
                if (shadowsOn) {
                    sh = ShadowFactor(wpos, n);
                    if (sh < 0.999f) {
                        Vec3 amb = SceneLights::AmbientAt(n);
                        lit.x = amb.x + (lit.x - amb.x) * sh; if (lit.x < 0) lit.x = 0;
                        lit.y = amb.y + (lit.y - amb.y) * sh; if (lit.y < 0) lit.y = 0;
                        lit.z = amb.z + (lit.z - amb.z) * sh; if (lit.z < 0) lit.z = 0;
                    }
                }
                // Toon (cel) shader: quantize the diffuse lighting into hard bands so
                // the surface reads as flat cartoon shades instead of a smooth ramp.
                if (toonBands > 0) {
                    float q = (float)toonBands;
                    lit.x = std::ceil(lit.x * q) / q;
                    lit.y = std::ceil(lit.y * q) / q;
                    lit.z = std::ceil(lit.z * q) / q;
                }
                // Colored Blinn-Phong specular from every light (sun + point + spot),
                // each tinted by its own color and attenuation; the sun's part is
                // shadowed. Scaled by the material strength and the gloss map.
                Vec3 spec{0, 0, 0};
                if (specularK > 0.0f) {
                    Vec3 toEye = (eye - wpos).Normalized();
                    spec = SceneLights::SpecularN(wpos, n, toEye, shininess, sh);
                    float ks = specularK * gloss;
                    spec.x *= ks; spec.y *= ks; spec.z *= ks;
                    // Toon: collapse the highlight to a single hard glint (on/off).
                    if (toonBands > 0) {
                        float sm = std::fmax(spec.x, std::fmax(spec.y, spec.z));
                        float st = sm > 0.5f * ks ? ks : 0.0f;
                        spec.x = st; spec.y = st; spec.z = st;
                    }
                }
                float br = base.r, bg = base.g, bb2 = base.b;
                if (textured && triplanar) {
                    // Triplanar: blend three world-axis projections by |normal|, so the
                    // texture wraps cliffs/terrain with no UV seams or stretching.
                    auto frac = [](float x) { return x - std::floor(x); };
                    const Image& img = (*mips)[0];
                    float ax = std::fabs(n.x), ay = std::fabs(n.y), az = std::fabs(n.z);
                    float s = ax + ay + az; if (s < 1e-5f) s = 1.0f;
                    ax /= s; ay /= s; az /= s;
                    Color cx = img.Sample(frac(wpos.z * triTileX), frac(wpos.y * triTileY)); // x-facing
                    Color cy = img.Sample(frac(wpos.x * triTileX), frac(wpos.z * triTileY)); // y-facing
                    Color cz = img.Sample(frac(wpos.x * triTileX), frac(wpos.y * triTileY)); // z-facing
                    float tr = cx.r * ax + cy.r * ay + cz.r * az;
                    float tg = cx.g * ax + cy.g * ay + cz.g * az;
                    float tb = cx.b * ax + cy.b * ay + cz.b * az;
                    br = tr * tint.r; bg = tg * tint.g; bb2 = tb * tint.b;
                } else if (textured) {
                    float A = w0 * U[0] + w1 * U[1] + w2 * U[2];
                    float Av = w0 * V[0] + w1 * V[1] + w2 * V[2];
                    float iw = w0 * IW[0] + w1 * IW[1] + w2 * IW[2];
                    if (iw != 0.0f) {
                        Color tc = SampleMips(*mips, A / iw, Av / iw, PixelLod(A, Av, iw, lg2, tw, th));
                        br = tc.r * tint.r; bg = tc.g * tint.g; bb2 = tc.b * tint.b;
                    }
                }
                // Gradient shader: replace the albedo with a two-colour ramp by the
                // surface's up-ness (downward faces -> bottom colour, up -> top).
                if (shaderMode == 3 && gradTop && gradBot) {
                    float t = n.y * 0.5f + 0.5f; t = t < 0 ? 0 : (t > 1 ? 1 : t);
                    br  = gradBot[0] + (gradTop[0] - gradBot[0]) * t;
                    bg  = gradBot[1] + (gradTop[1] - gradBot[1]) * t;
                    bb2 = gradBot[2] + (gradTop[2] - gradBot[2]) * t;
                }
                // Fresnel shader: a dark body with a glowing rim (the rim colour does the work).
                if (shaderMode == 4) { br *= 0.10f; bg *= 0.10f; bb2 *= 0.10f; }
                // Iridescent shader: an oil-slick / thin-film sheen — the albedo's hue
                // shifts with the view angle (Fresnel) through a cosine palette.
                if (shaderMode == 5) {
                    Vec3 toEye = (eye - wpos).Normalized();
                    float fz = 1.0f - std::fmax(0.0f, Vec3::Dot(n, toEye));
                    float ph = fz * 6.2831853f;
                    br  *= 0.5f + 0.5f * std::cos(ph);
                    bg  *= 0.5f + 0.5f * std::cos(ph + 2.094395f);
                    bb2 *= 0.5f + 0.5f * std::cos(ph + 4.188790f);
                }
                // Hologram shader: a dark, scan-lined body lit only at the grazing edges
                // (handled by the rim block below, forced strong like Fresnel).
                if (shaderMode == 6) {
                    float band = 0.55f + 0.45f * std::sin(wpos.y * 40.0f);   // horizontal scanlines
                    br *= 0.18f * band; bg *= 0.18f * band; bb2 *= 0.18f * band;
                }
                // Posterize shader: quantise the albedo into a few bands (retro / PSX).
                if (shaderMode == 7) {
                    const float lv = 5.0f;
                    br = std::floor(br * lv) / lv; bg = std::floor(bg * lv) / lv; bb2 = std::floor(bb2 * lv) / lv;
                }
                // Metallic workflow: metals have (almost) no diffuse, and they tint
                // both their specular highlight and their environment reflection by
                // the albedo color (so gold reflects gold). Dielectrics keep a white
                // highlight/reflection. f0* = lerp(white, albedo, metallic).
                float metal = metallic < 0 ? 0 : (metallic > 1 ? 1 : metallic);
                float diff  = 1.0f - 0.9f * metal;                 // metals kill diffuse
                float f0r = 1.0f + (br  - 1.0f) * metal;
                float f0g = 1.0f + (bg  - 1.0f) * metal;
                float f0b = 1.0f + (bb2 - 1.0f) * metal;
                // Fresnel rim: brighten grazing-angle edges (1 - n·view)^power,
                // tinted by the lit color so it reads as a soft backlight glow.
                float rim = 0.0f;
                if (rimOn) {
                    Vec3 toEye = (eye - wpos).Normalized();
                    float f = 1.0f - std::fmax(0.0f, Vec3::Dot(n, toEye));
                    // Cheap integer powers for the common rim exponents (avoid pow).
                    float fp = (rimPow == 3.0f) ? f * f * f
                             : (rimPow == 2.0f) ? f * f
                             : (rimPow == 4.0f) ? (f * f) * (f * f)
                             : std::pow(f, rimPow);
                    rim = fp * rimStr;
                }
                // Per-material Fresnel rim: an additive, colored backlight independent
                // of the global rim toggle (a first-class shader feature).
                float mrimR = 0.0f, mrimG = 0.0f, mrimB = 0.0f;
                float useRim = matRimStr;                      // Fresnel/Hologram force a strong rim
                if ((shaderMode == 4 || shaderMode == 6) && useRim < 0.8f) useRim = 1.6f;
                if (useRim > 0.0f) {
                    Vec3 toEye = (eye - wpos).Normalized();
                    float f = 1.0f - std::fmax(0.0f, Vec3::Dot(n, toEye));
                    float fp = (matRimPow == 3.0f) ? f * f * f
                             : (matRimPow == 2.0f) ? f * f
                             : (matRimPow == 4.0f) ? (f * f) * (f * f)
                             : std::pow(f, matRimPow);
                    float m = fp * useRim;
                    mrimR = m * rimR; mrimG = m * rimG; mrimB = m * rimB;
                }
                float cr = br * lit.x * diff + spec.x * f0r + rim * lit.x + mrimR + er;
                float cg = bg * lit.y * diff + spec.y * f0g + rim * lit.y + mrimG + eg;
                float cb = bb2 * lit.z * diff + spec.z * f0b + rim * lit.z + mrimB + eb;
                // Velvet shader: a soft fuzzy sheen that brightens grazing angles
                // (backscatter), lit by the scene — cloth / peach-skin look.
                if (shaderMode == 8) {
                    Vec3 toEye = (eye - wpos).Normalized();
                    float fz = 1.0f - std::fmax(0.0f, Vec3::Dot(n, toEye));
                    float s = fz * fz * 0.6f;
                    cr += br * lit.x * s; cg += bg * lit.y * s; cb += bb2 * lit.z * s;
                }
                // Environment reflection: mirror the sky gradient about the normal
                // and blend it in by a Fresnel-weighted reflectivity (Schlick).
                // Metals reflect strongly even without an explicit reflectivity, and
                // their reflection is tinted by the albedo (f0).
                float reflAmt = reflectivity > metal ? reflectivity : metal;
                float reflK = reflAmt * gloss;
                if (reflK > 0.0f && envOn) {
                    Vec3 toEye = (eye - wpos).Normalized();
                    float ndv = Vec3::Dot(n, toEye); if (ndv < 0.0f) ndv = 0.0f;
                    Vec3 R{n.x * (2.0f * ndv) - toEye.x, n.y * (2.0f * ndv) - toEye.y,
                           n.z * (2.0f * ndv) - toEye.z};
                    Vec3 env = SampleEnvSky(R);
                    float f = 1.0f - ndv; f = f * f * f * f * f;            // (1-n·v)^5
                    float k = reflK + (1.0f - reflK) * f;                   // Schlick
                    cr = cr * (1.0f - k) + env.x * f0r * k;
                    cg = cg * (1.0f - k) + env.y * f0g * k;
                    cb = cb * (1.0f - k) + env.z * f0b * k;
                }
                float fog = (float)(w0 * FOG[0] + w1 * FOG[1] + w2 * FOG[2]);   // per-vertex (no seam)
                if (fog > 0.0f) {
                    cr = cr * (1.0f - fog) + fr * fog;
                    cg = cg * (1.0f - fog) + fg * fog;
                    cb = cb * (1.0f - fog) + fb * fog;
                }
                depth[i] = d;
                color[i] = PackRGB(cr, cg, cb);
                if (!gvalid.empty()) { gpos[i] = wpos; gnrm[i] = n; gvalid[i] = 1; }
            }
        }
    }
};

// Process-wide texture cache for the software renderer (path -> RGBA image).
// Loaded lazily; failed/empty paths cache an empty image so we don't retry.
inline std::unordered_map<std::string, Image>& TextureCacheMap() {
    static std::unordered_map<std::string, Image> cache;
    return cache;
}

// Register an in-memory image under a name so it can be used without a file on
// disk (e.g. an embedded/procedural matcap that ships inside the exe). Names
// conventionally start with '@' to avoid colliding with real file paths.
inline void RegisterTexture(const std::string& name, Image img) {
    TextureCacheMap()[name] = std::move(img);
}

inline Image* GetCachedTexture(const std::string& path) {
    auto& cache = TextureCacheMap();
    auto it = cache.find(path);
    if (it == cache.end()) {
        Image img;
        img.Load(path);   // leaves the image empty on failure
        it = cache.emplace(path, std::move(img)).first;
    }
    return it->second.Width() > 0 ? &it->second : nullptr;
}

// Build the mipmap pyramid for `levels` (which already holds the base at [0]):
// box-downsample by 2 until 1x1. Mip levels let the renderer pick a smaller,
// pre-filtered texture for far-away/minified triangles, which kills the harsh
// shimmer/aliasing you get sampling a full-res texture at a distance.
inline void BuildMips(std::vector<Image>& levels) {
    if (levels.empty()) return;
    while (levels.back().Width() > 1 || levels.back().Height() > 1) {
        const Image& s = levels.back();
        int sw = s.Width(), sh = s.Height();
        int dw = sw > 1 ? sw / 2 : 1, dh = sh > 1 ? sh / 2 : 1;
        Image dimg(dw, dh);
        for (int y = 0; y < dh; ++y)
            for (int x = 0; x < dw; ++x) {
                int sx = x * 2, sy = y * 2;
                int sx1 = sx + 1 < sw ? sx + 1 : sx, sy1 = sy + 1 < sh ? sy + 1 : sy;
                Color a = s.GetPixel(sx, sy), b = s.GetPixel(sx1, sy);
                Color c = s.GetPixel(sx, sy1), e = s.GetPixel(sx1, sy1);
                dimg.SetPixel(x, y, Color((a.r + b.r + c.r + e.r) * 0.25f,
                                          (a.g + b.g + c.g + e.g) * 0.25f,
                                          (a.b + b.b + c.b + e.b) * 0.25f,
                                          (a.a + b.a + c.a + e.a) * 0.25f));
            }
        levels.push_back(std::move(dimg));
    }
}

inline std::unordered_map<std::string, std::vector<Image>>& MipCacheMap() {
    static std::unordered_map<std::string, std::vector<Image>> cache;
    return cache;
}

// A texture's mip chain (built lazily from the base image). Returns null if the
// texture failed to load / is empty.
inline const std::vector<Image>* GetCachedMips(const std::string& path) {
    auto& cache = MipCacheMap();
    auto it = cache.find(path);
    if (it == cache.end()) {
        std::vector<Image> levels;
        Image* base = GetCachedTexture(path);
        if (base && base->Width() > 0) { levels.push_back(*base); BuildMips(levels); }
        it = cache.emplace(path, std::move(levels)).first;
    }
    return (!it->second.empty() && it->second[0].Width() > 0) ? &it->second : nullptr;
}

/// Per-pixel (Phong) lighting toggle. On by default for quality (smooth shading
/// + correct specular on every pixel); turn off to fall back to faster per-vertex
/// (Gouraud) shading on very large scenes.
inline bool& PerPixelLighting() { static bool v = false; return v; }  // default off (per request); flat/Gouraud path, now crack-free via double-precision rasterization. Opt-in in the Rendering panel.

// ---- Directional shadow mapping --------------------------------------------
// A depth map rendered from the scene's directional light. Per-pixel shading
// projects each fragment into this map and darkens it if something is closer to
// the light (i.e. it's occluded), giving real cast shadows. (ShadowMap struct is
// declared above the Raster class so shading can consult it.)
inline ShadowMap& Shadows()      { static ShadowMap s; return s; }
inline bool& ShadowsEnabled()    { static bool v = false; return v; }  // off by default (perf); opt-in
inline int&  ShadowMapResolution(){ static int s = 1024; return s; }

/// Render the scene depth from the directional light into the shadow map. Call
/// once before RenderMeshes (RenderMeshes does this automatically).
inline void RenderShadowMap(const Scene& scene) {
    ShadowMap& sm = Shadows();
    if (!ShadowsEnabled()) { sm.enabled = false; return; }
    Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    bool any = false;
    auto visible = [](const GameObject& go) {
        auto* mr = go.GetComponent<MeshRenderer>();
        return mr && go.active && mr->enabled && !mr->wireframe ? mr : nullptr;
    };
    for (const auto& go : scene.Objects()) {
        auto* mr = visible(*go); if (!mr) continue;
        Mat4 model = go->transform->LocalToWorldMatrix();
        Vec3 blo, bhi; mr->mesh.Bounds(blo, bhi);
        for (int c = 0; c < 8; ++c) {
            Vec3 w = model.MultiplyPoint({(c & 1) ? bhi.x : blo.x, (c & 2) ? bhi.y : blo.y, (c & 4) ? bhi.z : blo.z});
            lo.x = std::fmin(lo.x, w.x); lo.y = std::fmin(lo.y, w.y); lo.z = std::fmin(lo.z, w.z);
            hi.x = std::fmax(hi.x, w.x); hi.y = std::fmax(hi.y, w.y); hi.z = std::fmax(hi.z, w.z);
            any = true;
        }
    }
    if (!any) { sm.enabled = false; return; }
    Vec3 ctr{(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f};
    float R = 0.5f * std::sqrt((hi.x - lo.x) * (hi.x - lo.x) + (hi.y - lo.y) * (hi.y - lo.y) +
                               (hi.z - lo.z) * (hi.z - lo.z)) + 0.05f;
    Vec3 L = SceneLight::Direction(); { float m = L.Magnitude(); L = m > 1e-6f ? L * (1.0f / m) : Vec3{0, -1, 0}; }
    Vec3 eye = ctr - L * (R * 2.0f);
    Vec3 up = std::fabs(L.y) > 0.99f ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
    sm.viewProj = Mat4::Ortho(-R, R, -R, R, 0.05f, R * 4.0f) * Mat4::LookAt(eye, ctr, up);
    int S = ShadowMapResolution(); sm.size = S; sm.depth.assign((std::size_t)S * S, 2.0f);
    sm.texelWorld = (2.0f * R) / (float)S;
    for (const auto& go : scene.Objects()) {
        auto* mr = visible(*go); if (!mr) continue;
        Mat4 mvp = sm.viewProj * go->transform->LocalToWorldMatrix();
        const auto& V = mr->mesh.vertices; const auto& T = mr->mesh.triangles;
        for (std::size_t i = 0; i + 2 < T.size(); i += 3) {
            float sxv[3], syv[3], szv[3]; bool ok = true;
            for (int k = 0; k < 3; ++k) {
                Vec4 c = mvp * Vec4{V[T[i + k]], 1.0f};
                if (c.w == 0) { ok = false; break; }
                float iw = 1.0f / c.w;
                sxv[k] = (c.x * iw * 0.5f + 0.5f) * S;
                syv[k] = (1.0f - (c.y * iw * 0.5f + 0.5f)) * S;
                szv[k] = c.z * iw;
            }
            if (!ok) continue;
            int minx = (int)std::floor(std::fmin(sxv[0], std::fmin(sxv[1], sxv[2])));
            int maxx = (int)std::ceil (std::fmax(sxv[0], std::fmax(sxv[1], sxv[2])));
            int miny = (int)std::floor(std::fmin(syv[0], std::fmin(syv[1], syv[2])));
            int maxy = (int)std::ceil (std::fmax(syv[0], std::fmax(syv[1], syv[2])));
            if (minx < 0) minx = 0; if (miny < 0) miny = 0;
            if (maxx >= S) maxx = S - 1; if (maxy >= S) maxy = S - 1;
            float area = (sxv[1] - sxv[0]) * (syv[2] - syv[0]) - (sxv[2] - sxv[0]) * (syv[1] - syv[0]);
            if (area == 0.0f) continue;
            float inv = 1.0f / area;
            for (int y = miny; y <= maxy; ++y)
                for (int x = minx; x <= maxx; ++x) {
                    float px = x + 0.5f, py = y + 0.5f;
                    float w0 = ((sxv[1] - px) * (syv[2] - py) - (sxv[2] - px) * (syv[1] - py)) * inv;
                    float w1 = ((sxv[2] - px) * (syv[0] - py) - (sxv[0] - px) * (syv[2] - py)) * inv;
                    float w2 = 1.0f - w0 - w1;
                    if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                    float z = w0 * szv[0] + w1 * szv[1] + w2 * szv[2];
                    float& dref = sm.depth[(std::size_t)y * S + x];
                    if (z < dref) dref = z;
                }
        }
    }
    sm.enabled = true;
}

/// Shadow softness in shadow-map texels (the Poisson disk's radius). Larger =
/// softer, fuzzier shadow edges; smaller = crisp/hard. ~2.5 reads as soft-ish.
inline float& ShadowSoftness() { static float v = 2.5f; return v; }

/// 0 = fully shadowed, 1 = fully lit, for a world position (Poisson-disk PCF).
/// The sample point is pushed a couple of texels along the surface normal — this
/// "normal-offset bias" is what stops a lit surface from shadowing itself (acne).
inline float ShadowFactor(const Vec3& wpos, const Vec3& n) {
    ShadowMap& sm = Shadows();
    if (!sm.enabled || sm.size <= 0) return 1.0f;
    Vec3 p = wpos + n * (sm.texelWorld * 3.5f);     // normal-offset bias (anti-acne)
    Vec4 lc = sm.viewProj * Vec4{p, 1.0f};
    if (lc.w == 0) return 1.0f;
    float iw = 1.0f / lc.w, x = lc.x * iw, y = lc.y * iw, z = lc.z * iw;
    if (x < -1 || x > 1 || y < -1 || y > 1 || z > 1) return 1.0f;
    float fx = (x * 0.5f + 0.5f) * sm.size, fy = (1.0f - (y * 0.5f + 0.5f)) * sm.size;
    const float bias = 0.004f;
    // 6-tap Poisson-disk PCF (center + 5 ring taps): scattered samples give a
    // smoother penumbra than a rigid grid, at roughly half the cost of a 12-tap
    // kernel — this runs per shaded pixel so the tap count matters for FPS.
    static const float kPoisson[6][2] = {
        { 0.000f,  0.000f}, {-0.326f, -0.406f}, { 0.840f, -0.074f},
        {-0.696f,  0.457f}, { 0.185f,  0.893f}, { 0.507f, -0.640f},
    };
    float radius = ShadowSoftness();
    float lit = 0.0f;
    for (int s = 0; s < 6; ++s) {
        int px = (int)(fx + kPoisson[s][0] * radius);
        int py = (int)(fy + kPoisson[s][1] * radius);
        if (px < 0 || py < 0 || px >= sm.size || py >= sm.size) { lit += 1.0f; continue; }
        if (z - bias <= sm.depth[(std::size_t)py * sm.size + px]) lit += 1.0f;
    }
    return lit / 6.0f;
}

// ---- Screen-space ambient occlusion ----------------------------------------
inline bool&  SSAOEnabled()  { static bool v = false; return v; }     // off by default (perf); opt-in
inline float& SSAORadius()   { static float v = 0.45f; return v; }  // world units
inline float& SSAOStrength() { static float v = 0.6f; return v; }

/// Darken creases/contacts using the per-pixel world position + normal G-buffer:
/// sample a hemisphere around each pixel and count how many samples are occluded
/// by nearer geometry. Adds soft contact shadows / depth that flat lighting can't.
inline void ApplySSAO(Raster& r, const Mat4& vp, const Vec3& eye) {
    if (r.gvalid.empty()) return;
    const int W = r.width, H = r.height;
    const float radius = SSAORadius(), strength = SSAOStrength();
    static const int K = 8;
    static Vec3 kern[K];
    static bool init = false;
    if (!init) {
        init = true; unsigned s = 1469598103u;
        auto rnd = [&]() { s = s * 1664525u + 1013904223u; return (float)((s >> 8) & 0xFFFFFF) / 16777216.0f; };
        for (int k = 0; k < K; ++k) {
            float x = rnd() * 2 - 1, y = rnd() * 2 - 1, z = rnd() * 0.85f + 0.15f;
            float l = std::sqrt(x * x + y * y + z * z); if (l < 1e-4f) l = 1;
            float sc = 0.2f + 0.8f * ((float)(k + 1) / K) * ((float)(k + 1) / K);
            kern[k] = Vec3{x / l, y / l, z / l} * sc;
        }
    }
    // Compute AO at HALF resolution (1/4 the hemisphere evaluations), then
    // bilinear-upsample when modulating the full-res color. The G-buffer stays
    // full-res; each half-res texel samples the G-buffer pixel under it.
    const int hw = (W + 1) / 2, hh = (H + 1) / 2;
    std::vector<float> ao((std::size_t)hw * hh, 1.0f);
    ParallelRows(0, hh, [&](int hya, int hyb) {
        for (int hy = hya; hy < hyb; ++hy)
        for (int hx = 0; hx < hw; ++hx) {
            int x = hx * 2, y = hy * 2;
            std::size_t i = (std::size_t)y * W + x;
            if (!r.gvalid[i]) continue;
            Vec3 P = r.gpos[i], N = r.gnrm[i];
            Vec3 up = std::fabs(N.y) > 0.99f ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
            Vec3 T = Vec3::Cross(up, N); { float l = T.Magnitude(); T = l > 1e-5f ? T * (1.0f / l) : Vec3{1, 0, 0}; }
            Vec3 B = Vec3::Cross(N, T);
            float occWeighted = 0.0f; int tot = 0;
            for (int k = 0; k < K; ++k) {
                Vec3 sp = P + (T * kern[k].x + B * kern[k].y + N * kern[k].z) * radius;
                Vec4 c = vp * Vec4{sp, 1.0f}; if (c.w <= 0) continue;
                float iw = 1.0f / c.w;
                int sx = (int)((c.x * iw * 0.5f + 0.5f) * W), sy = (int)((1.0f - (c.y * iw * 0.5f + 0.5f)) * H);
                if (sx < 0 || sy < 0 || sx >= W || sy >= H) continue;
                std::size_t j = (std::size_t)sy * W + sx; ++tot;
                if (!r.gvalid[j]) continue;
                float diff = (sp - eye).Magnitude() - (r.gpos[j] - eye).Magnitude();
                // Smooth range falloff so distant geometry doesn't cast wide halos.
                if (diff > 0.015f) {
                    float w = 1.0f - diff / radius;       // fades out past `radius`
                    if (w > 0.0f) occWeighted += w;
                }
            }
            if (tot > 0) { float a = 1.0f - strength * occWeighted / tot; ao[(std::size_t)hy * hw + hx] = a < 0 ? 0 : a; }
        }
    });
    // Blur the half-res AO (3x3 box) to kill the hemisphere-sampling noise — SSAO
    // is inherently grainy and needs this smoothing pass. Cheap at half resolution.
    {
        static std::vector<float> aob; aob = ao;
        ParallelRows(0, hh, [&](int ya, int yb) {
            for (int y = ya; y < yb; ++y)
            for (int x = 0; x < hw; ++x) {
                float s = 0; int c = 0;
                for (int dy = -1; dy <= 1; ++dy) {
                    int ny = y + dy; if (ny < 0 || ny >= hh) continue;
                    for (int dx = -1; dx <= 1; ++dx) {
                        int nx = x + dx; if (nx < 0 || nx >= hw) continue;
                        s += aob[(std::size_t)ny * hw + nx]; ++c;
                    }
                }
                ao[(std::size_t)y * hw + x] = c ? s / c : 1.0f;
            }
        });
    }
    // Bilinear-upsample the half-res AO and modulate the full-res color.
    ParallelRows(0, H, [&](int ya, int yb) {
        for (int y = ya; y < yb; ++y) {
        float fy = y * 0.5f; int y0 = (int)fy; float ay = fy - y0; int y1 = y0 + 1;
        if (y1 >= hh) y1 = hh - 1;
        for (int x = 0; x < W; ++x) {
            std::size_t i = (std::size_t)y * W + x;
            if (!r.gvalid[i]) continue;
            float fx = x * 0.5f; int x0 = (int)fx; float ax = fx - x0; int x1 = x0 + 1;
            if (x1 >= hw) x1 = hw - 1;
            float a00 = ao[(std::size_t)y0 * hw + x0], a10 = ao[(std::size_t)y0 * hw + x1];
            float a01 = ao[(std::size_t)y1 * hw + x0], a11 = ao[(std::size_t)y1 * hw + x1];
            float a = (a00 + (a10 - a00) * ax) + ((a01 + (a11 - a01) * ax) - (a00 + (a10 - a00) * ax)) * ay;
            std::uint32_t v = r.color[i];
            std::uint32_t R = (std::uint32_t)((v & 0xFF) * a), G = (std::uint32_t)(((v >> 8) & 0xFF) * a), Bc = (std::uint32_t)(((v >> 16) & 0xFF) * a);
            r.color[i] = (0xFFu << 24) | (Bc << 16) | (G << 8) | R;
        }
        }
    });
}

// ---- Bloom -----------------------------------------------------------------
inline bool&  BloomEnabled()   { static bool v = false; return v; }    // off by default (perf); opt-in
inline float& BloomThreshold() { static float v = 0.80f; return v; }  // 0..1 brightness
inline float& BloomIntensity() { static float v = 0.6f; return v; }

/// Glow on bright/emissive areas: extract pixels above a brightness threshold,
/// blur them, and add back. Makes lights, neon and hot highlights bleed light.
///
/// For speed the blur runs at QUARTER resolution (1/4 W x 1/4 H): the bright pass
/// is box-downsampled into a small buffer, blurred there (the blur is ~16x cheaper
/// than at full res), then bilinearly upsampled when added back. A wide full-res
/// gaussian would otherwise dominate the frame time.
inline void ApplyBloom(Raster& r) {
    if (!BloomEnabled()) return;
    int W = r.width, H = r.height;
    if (W < 8 || H < 8) return;
    const int DS = 4;                       // downsample factor
    int dw = W / DS, dh = H / DS;
    if (dw < 2 || dh < 2) return;
    std::size_t dN = (std::size_t)dw * dh;
    static std::vector<float> br, tmp;       // low-res bright pass (3ch interleaved)
    br.assign(dN * 3, 0.0f); tmp.assign(dN * 3, 0.0f);
    float t = BloomThreshold() * 255.0f;
    // Downsample + threshold: average each DS x DS block's over-threshold energy.
    const float invBlock = 1.0f / (float)(DS * DS);
    for (int dy = 0; dy < dh; ++dy)
        for (int dx = 0; dx < dw; ++dx) {
            float acc[3] = {0, 0, 0};
            int sy0 = dy * DS, sx0 = dx * DS;
            for (int yy = 0; yy < DS; ++yy) {
                const std::uint32_t* row = &r.color[(std::size_t)(sy0 + yy) * W + sx0];
                for (int xx = 0; xx < DS; ++xx) {
                    std::uint32_t v = row[xx];
                    float c0 = (float)(v & 0xFF) - t, c1 = (float)((v >> 8) & 0xFF) - t, c2 = (float)((v >> 16) & 0xFF) - t;
                    if (c0 > 0) acc[0] += c0; if (c1 > 0) acc[1] += c1; if (c2 > 0) acc[2] += c2;
                }
            }
            std::size_t di = ((std::size_t)dy * dw + dx) * 3;
            br[di] = acc[0] * invBlock; br[di + 1] = acc[1] * invBlock; br[di + 2] = acc[2] * invBlock;
        }
    // Separable box blur (3 passes ~ gaussian) at low res — cheap.
    int rad = 3;
    for (int pass = 0; pass < 3; ++pass) {
        for (int y = 0; y < dh; ++y)                      // horizontal
            for (int k = 0; k < 3; ++k) {
                float sum = 0; int cnt = 2 * rad + 1;
                for (int x = -rad; x <= rad; ++x) { int xx = x < 0 ? 0 : (x >= dw ? dw - 1 : x); sum += br[((std::size_t)y * dw + xx) * 3 + k]; }
                for (int x = 0; x < dw; ++x) {
                    tmp[((std::size_t)y * dw + x) * 3 + k] = sum / cnt;
                    int add = x + rad + 1, sub = x - rad;
                    add = add >= dw ? dw - 1 : add; sub = sub < 0 ? 0 : sub;
                    sum += br[((std::size_t)y * dw + add) * 3 + k] - br[((std::size_t)y * dw + sub) * 3 + k];
                }
            }
        for (int x = 0; x < dw; ++x)                      // vertical
            for (int k = 0; k < 3; ++k) {
                float sum = 0; int cnt = 2 * rad + 1;
                for (int y = -rad; y <= rad; ++y) { int yy = y < 0 ? 0 : (y >= dh ? dh - 1 : y); sum += tmp[((std::size_t)yy * dw + x) * 3 + k]; }
                for (int y = 0; y < dh; ++y) {
                    br[((std::size_t)y * dw + x) * 3 + k] = sum / cnt;
                    int add = y + rad + 1, sub = y - rad;
                    add = add >= dh ? dh - 1 : add; sub = sub < 0 ? 0 : sub;
                    sum += tmp[((std::size_t)add * dw + x) * 3 + k] - tmp[((std::size_t)sub * dw + x) * 3 + k];
                }
            }
    }
    // Bilinear-upsample the low-res bloom and add it back at full resolution.
    float intensity = BloomIntensity();
    float sxScale = (float)dw / W, syScale = (float)dh / H;
    ParallelRows(0, H, [&](int ya, int yb) {
        for (int y = ya; y < yb; ++y) {
        float fy = (y + 0.5f) * syScale - 0.5f; int y0 = (int)std::floor(fy); float ay = fy - y0;
        int y1 = y0 + 1; if (y0 < 0) { y0 = 0; } if (y1 < 0) y1 = 0; if (y0 >= dh) y0 = dh - 1; if (y1 >= dh) y1 = dh - 1;
        for (int x = 0; x < W; ++x) {
            float fx = (x + 0.5f) * sxScale - 0.5f; int x0 = (int)std::floor(fx); float ax = fx - x0;
            int x1 = x0 + 1; if (x0 < 0) { x0 = 0; } if (x1 < 0) x1 = 0; if (x0 >= dw) x0 = dw - 1; if (x1 >= dw) x1 = dw - 1;
            std::size_t i00 = ((std::size_t)y0 * dw + x0) * 3, i10 = ((std::size_t)y0 * dw + x1) * 3;
            std::size_t i01 = ((std::size_t)y1 * dw + x0) * 3, i11 = ((std::size_t)y1 * dw + x1) * 3;
            std::uint32_t v = r.color[(std::size_t)y * W + x];
            float o[3];
            for (int k = 0; k < 3; ++k) {
                float top = br[i00 + k] + (br[i10 + k] - br[i00 + k]) * ax;
                float bot = br[i01 + k] + (br[i11 + k] - br[i01 + k]) * ax;
                float bl = top + (bot - top) * ay;
                float c = (float)((v >> (k * 8)) & 0xFF) + bl * intensity;
                o[k] = c > 255 ? 255 : c;
            }
            r.color[(std::size_t)y * W + x] = (v & 0xFF000000u) | ((std::uint32_t)o[2] << 16) | ((std::uint32_t)o[1] << 8) | (std::uint32_t)o[0];
        }
        }
    });
}

// ---- Tone mapping (filmic) -------------------------------------------------
inline bool&  ToneMapEnabled() { static bool v = true; return v; }
inline float& Exposure()       { static float v = 1.0f; return v; }  // brightness multiplier

/// Filmic tone mapping (ACES approximation, Narkowicz 2015). The renderer shades
/// in roughly-linear light and used to just clamp at white, so any highlight past
/// 1.0 (specular, emissive, bloom) flattened into a hard, posterized white. ACES
/// instead rolls highlights off along an S-curve: bright areas keep their hue and
/// gradient, shadows gain a touch of contrast, and the whole frame reads more like
/// film than a flat clamp. Exposure scales the input first so the image can be
/// brightened/darkened as a whole. Runs after bloom, before edge AA.
inline void ApplyToneMap(Raster& r) {
    if (!ToneMapEnabled()) return;
    int W = r.width, H = r.height;
    if (W < 1 || H < 1) return;
    float e = Exposure();
    auto aces = [](float x) {
        // x in linear light; returns display value in [0,1].
        float n = x * (2.51f * x + 0.03f);
        float d = x * (2.43f * x + 0.59f) + 0.14f;
        float o = d > 1e-6f ? n / d : 0.0f;
        return o < 0.0f ? 0.0f : (o > 1.0f ? 1.0f : o);
    };
    ParallelRows(0, H, [&](int ya, int yb) {
        for (std::size_t i = (std::size_t)ya * W, end = (std::size_t)yb * W; i < end; ++i) {
            std::uint32_t v = r.color[i];
            float c0 = (float)( v        & 0xFF) / 255.0f;
            float c1 = (float)((v >> 8)  & 0xFF) / 255.0f;
            float c2 = (float)((v >> 16) & 0xFF) / 255.0f;
            float o0 = aces(c0 * e), o1 = aces(c1 * e), o2 = aces(c2 * e);
            r.color[i] = (v & 0xFF000000u)            // keep alpha (transparent bg = skybox)
                       | ((std::uint32_t)(o2 * 255.0f + 0.5f) << 16)
                       | ((std::uint32_t)(o1 * 255.0f + 0.5f) << 8)
                       |  (std::uint32_t)(o0 * 255.0f + 0.5f);
        }
    });
}

// ---- Color grading (brightness / contrast / saturation / vignette / gamma) --
// All neutral by default, so the look is unchanged until you opt in. A cheap final
// "look" pass: lets a game warm/cool the palette, punch up contrast, desaturate
// for a grim mood, darken the frame edges, or apply gamma — without shaders.
inline bool&  ColorGradeEnabled() { static bool v = false; return v; }
inline float& Brightness()  { static float v = 1.0f; return v; }   // multiply (1 = no change)
inline float& Contrast()    { static float v = 1.0f; return v; }   // around mid-grey (1 = no change)
inline float& Saturation()  { static float v = 1.0f; return v; }   // 0 = greyscale, 1 = normal
inline float& Vignette()    { static float v = 0.0f; return v; }   // 0 = off .. 1 = strong edge darkening
inline float& Gamma()       { static float v = 1.0f; return v; }   // 1 = none; 2.2 = linear->sRGB-ish

inline void ApplyColorGrade(Raster& r) {
    if (!ColorGradeEnabled()) return;
    int W = r.width, H = r.height;
    if (W < 1 || H < 1) return;
    const float br = Brightness(), ct = Contrast(), sat = Saturation();
    const float vig = Vignette() < 0.0f ? 0.0f : (Vignette() > 1.0f ? 1.0f : Vignette());
    const float invG = Gamma() > 1e-3f ? 1.0f / Gamma() : 1.0f;
    const bool doGamma = Mathf::Abs(Gamma() - 1.0f) > 1e-3f;
    const float cx = (W - 1) * 0.5f, cy = (H - 1) * 0.5f;
    const float maxR2 = cx * cx + cy * cy + 1e-6f;
    ParallelRows(0, H, [&](int ya, int yb) {
        for (int y = ya; y < yb; ++y) {
            float dy = (y - cy);
            for (int x = 0; x < W; ++x) {
                std::size_t i = (std::size_t)y * W + x;
                std::uint32_t v = r.color[i];
                float cr = (float)( v        & 0xFF) / 255.0f;
                float cg = (float)((v >> 8)  & 0xFF) / 255.0f;
                float cb = (float)((v >> 16) & 0xFF) / 255.0f;
                cr *= br; cg *= br; cb *= br;                       // brightness
                cr = (cr - 0.5f) * ct + 0.5f;                       // contrast about mid-grey
                cg = (cg - 0.5f) * ct + 0.5f;
                cb = (cb - 0.5f) * ct + 0.5f;
                if (sat != 1.0f) {                                  // saturation toward luma
                    float l = 0.2126f * cr + 0.7152f * cg + 0.0722f * cb;
                    cr = l + (cr - l) * sat; cg = l + (cg - l) * sat; cb = l + (cb - l) * sat;
                }
                if (vig > 0.0f) {                                  // edge darkening
                    float dx = (x - cx);
                    float f = 1.0f - vig * ((dx * dx + dy * dy) / maxR2);
                    if (f < 0.0f) f = 0.0f;
                    cr *= f; cg *= f; cb *= f;
                }
                if (doGamma) { cr = std::pow(cr < 0 ? 0 : cr, invG);
                               cg = std::pow(cg < 0 ? 0 : cg, invG);
                               cb = std::pow(cb < 0 ? 0 : cb, invG); }
                auto cl = [](float c) { return (std::uint32_t)((c < 0 ? 0 : (c > 1 ? 1 : c)) * 255.0f + 0.5f); };
                r.color[i] = (v & 0xFF000000u) | (cl(cb) << 16) | (cl(cg) << 8) | cl(cr);
            }
        }
    });
}

// ---- Anti-aliasing (FXAA-lite) ---------------------------------------------
inline bool& FXAAEnabled() { static bool v = true; return v; }

// Active camera's layer culling mask (Camera.cullingMask). RenderMeshes draws a
// mesh only if bit (1<<gameObject->layer) is set. The editor/player set this from
// the rendering camera each frame (~0 = all layers, the Scene view default).
inline int& RenderCullingMask() { static int v = ~0; return v; }

/// Cheap post-process anti-aliasing: where local contrast is high (a geometry
/// edge), blend the pixel toward its neighbourhood average by an amount scaled by
/// the contrast. Smooths jaggies without a full supersample.
inline void ApplyFXAA(Raster& r) {
    if (!FXAAEnabled()) return;
    int W = r.width, H = r.height;
    if (W < 3 || H < 3) return;
    static std::vector<std::uint32_t> src;
    src = r.color;
    auto luma = [](std::uint32_t v) {
        return 0.299f * (v & 0xFF) + 0.587f * ((v >> 8) & 0xFF) + 0.114f * ((v >> 16) & 0xFF);
    };
    ParallelRows(1, H - 1, [&](int ya, int yb) {
        for (int y = ya; y < yb; ++y)
        for (int x = 1; x < W - 1; ++x) {
            std::size_t i = (std::size_t)y * W + x;
            float m = luma(src[i]), n = luma(src[i - W]), s = luma(src[i + W]),
                  e = luma(src[i + 1]), w = luma(src[i - 1]);
            float lo = std::fmin(m, std::fmin(std::fmin(n, s), std::fmin(e, w)));
            float hi = std::fmax(m, std::fmax(std::fmax(n, s), std::fmax(e, w)));
            float contrast = hi - lo;
            if (contrast < 26.0f) continue;                 // not an edge
            float amt = contrast / (hi + 1.0f);
            if (amt > 0.7f) amt = 0.7f;
            auto ch = [&](std::uint32_t v, int sh) { return (float)((v >> sh) & 0xFF); };
            std::size_t nb[4] = {i - 1, i + 1, i - (std::size_t)W, i + (std::size_t)W};
            for (int c = 0; c < 3; ++c) {
                int sh = c * 8;
                float cv = ch(src[i], sh), av = cv;
                for (std::size_t k : nb) av += ch(src[k], sh);
                av /= 5.0f;
                float o = cv + (av - cv) * amt;
                std::uint32_t oi = (std::uint32_t)(o < 0 ? 0 : (o > 255 ? 255 : o));
                r.color[i] = (r.color[i] & ~(0xFFu << sh)) | (oi << sh);
            }
        }
    });
}

/// Render all active MeshRenderers in `scene` into `r` with the given
/// view-projection matrix and camera position. Two-sided + flat-shaded via the
/// global SceneLight; depth-tested so overlapping meshes occlude correctly.
inline void RenderMeshes(Raster& r, const Scene& scene, const Mat4& vp, const Vec3& eye,
                         const GameObject* ignore = nullptr) {
    const float W = (float)r.width, H = (float)r.height;
    const auto& rs = scene.renderSettings;
    const bool  fogOn = rs.fog && rs.fogEnd > rs.fogStart;
    const float fogR = rs.fogColor.r, fogG = rs.fogColor.g, fogB = rs.fogColor.b;
    // Refresh the environment sky (for reflective materials) from the scene.
    EnvSky().enabled = rs.skybox;
    EnvSky().top     = {rs.skyTop.r, rs.skyTop.g, rs.skyTop.b};
    EnvSky().horizon = {rs.skyHorizon.r, rs.skyHorizon.g, rs.skyHorizon.b};
    EnvSky().bottom  = {rs.skyBottom.r, rs.skyBottom.g, rs.skyBottom.b};
    // Hemisphere ambient: split the flat ambient into a sky-tinted (up) and
    // ground-tinted (down) term, keeping the midpoint equal to the current
    // ambient so the overall level is unchanged — only its direction is added.
    SceneLights::Hemisphere() = HemisphereAmbient();
    if (HemisphereAmbient()) {
        Vec3 a = SceneLights::AmbientColor();
        Vec3 skyC{(rs.skyTop.r + rs.skyHorizon.r) * 0.5f,
                  (rs.skyTop.g + rs.skyHorizon.g) * 0.5f,
                  (rs.skyTop.b + rs.skyHorizon.b) * 0.5f};
        Vec3 grdC{rs.skyBottom.r, rs.skyBottom.g, rs.skyBottom.b};
        Vec3 mid{(skyC.x + grdC.x) * 0.5f, (skyC.y + grdC.y) * 0.5f, (skyC.z + grdC.z) * 0.5f};
        float k = HemisphereStrength();
        auto cl = [](float v) { return v < 0.0f ? 0.0f : v; };
        SceneLights::SkyAmbient()    = {cl(a.x + (skyC.x - mid.x) * k),
                                        cl(a.y + (skyC.y - mid.y) * k),
                                        cl(a.z + (skyC.z - mid.z) * k)};
        SceneLights::GroundAmbient() = {cl(a.x + (grdC.x - mid.x) * k),
                                        cl(a.y + (grdC.y - mid.y) * k),
                                        cl(a.z + (grdC.z - mid.z) * k)};
    }
    RenderShadowMap(scene);   // depth-from-light pre-pass for cast shadows
    if (SSAOEnabled()) {      // allocate the SSAO G-buffer for this frame
        std::size_t n = (std::size_t)r.width * r.height;
        r.gpos.assign(n, Vec3{0, 0, 0}); r.gnrm.assign(n, Vec3{0, 0, 0});
        r.gvalid.assign(n, 0);
    } else if (!r.gvalid.empty()) {
        r.gpos.clear(); r.gnrm.clear(); r.gvalid.clear();
    }
    // Pre-warm the texture / mip caches single-threaded: the lazy caches insert
    // into shared unordered_maps, which is not safe to do from the worker threads
    // below. After this, the parallel band render only READS them.
    for (const auto& go : scene.Objects()) {
        auto* mr = go->GetComponent<MeshRenderer>();
        if (!mr) continue;
        if (!mr->texture.empty())     GetCachedMips(mr->texture);
        if (!mr->normalMap.empty())   GetCachedMips(mr->normalMap);
        if (!mr->specularMap.empty()) GetCachedMips(mr->specularMap);
        if (!mr->matcap.empty() && !mr->unlit && mr->shader != MeshRenderer::Shader::Unlit) GetCachedTexture(mr->matcap);
    }
    // Rasterize the meshes across CPU cores: each worker owns a horizontal band
    // of scanlines [bandY0, bandY1] and only fills pixels inside it, so colour /
    // depth / G-buffer writes never overlap between threads (no locks needed).
    // Vertex transforms are repeated per band, but pixel fill dominates.
    auto renderBand = [&](int bandY0, int bandY1) {
    for (const auto& go : scene.Objects()) {
        if (ignore && go.get() == ignore) continue;   // this camera skips this object (1st-person body)
        if (!(RenderCullingMask() & (1 << (go->layer & 31)))) continue;   // camera layer culling mask
        auto* mr = go->GetComponent<MeshRenderer>();
        if (!mr || !go->active || !mr->enabled || mr->wireframe) continue;   // wireframe drawn as lines
        // The Unlit shader is equivalent to the unlit flag for the renderer.
        const bool unlit = mr->unlit || mr->shader == MeshRenderer::Shader::Unlit;
        // Scrolling-UV offset (animated textures): advance the texture coordinates by
        // the scroll speed * elapsed time. Added after tiling, like Unity's offset.
        const float scrollU = mr->uvScroll.x * Time::ElapsedTime();
        const float scrollV = mr->uvScroll.y * Time::ElapsedTime();
        Mat4 model = go->transform->LocalToWorldMatrix();
        const auto& v = mr->mesh.vertices;
        const auto& t = mr->mesh.triangles;
        const bool hasUV = mr->mesh.uvs.size() == v.size();
        const bool faceCols = mr->mesh.HasFaceColors();   // per-triangle colors?
        const std::vector<Image>* tex = mr->texture.empty() ? nullptr : GetCachedMips(mr->texture);
        const std::vector<Image>* normalMips = mr->normalMap.empty() ? nullptr : GetCachedMips(mr->normalMap);
        const std::vector<Image>* specMips = mr->specularMap.empty() ? nullptr : GetCachedMips(mr->specularMap);
        Image* mcap = (mr->matcap.empty() || unlit) ? nullptr : GetCachedTexture(mr->matcap);
        // Matcap shading needs per-vertex normals; fall back if the mesh has none.
        const bool useMatcap = mcap && mr->mesh.HasNormals();
        // Smooth (Gouraud) shading when the mesh carries per-vertex normals and
        // isn't textured/unlit — interpolated lighting across each face. Matcap
        // also rides the smooth path (it interpolates a per-vertex sampled color).
        // Lit, untextured meshes shade via the smooth (Gouraud) path even without
        // per-vertex normals — they use the face normal, interpolated per vertex, so
        // there are no flat-shading facet lines (only truly unlit meshes stay flat).
        const bool smooth = useMatcap || (!tex && !unlit);
        // Normal matrix = inverse-transpose of the model's linear part. Using the
        // model matrix directly SKEWS normals under non-uniform scale (e.g. a ground
        // scaled {40,1,40} or a stretched sphere) — lighting then "acts up" as the
        // scale departs from uniform. The cofactor basis (cross products of the
        // transformed axes) is the inverse-transpose up to a positive scale, which
        // the per-vertex Normalized() removes — so it's correct for any scale/shear.
        const Vec3 mAxX = model.MultiplyVector({1, 0, 0});
        const Vec3 mAxY = model.MultiplyVector({0, 1, 0});
        const Vec3 mAxZ = model.MultiplyVector({0, 0, 1});
        const Vec3 nrmX = Vec3::Cross(mAxY, mAxZ);   // inverse-transpose basis columns
        const Vec3 nrmY = Vec3::Cross(mAxZ, mAxX);
        const Vec3 nrmZ = Vec3::Cross(mAxX, mAxY);
        auto xformNormal = [&](const Vec3& n) {
            return Vec3{nrmX.x * n.x + nrmY.x * n.y + nrmZ.x * n.z,
                        nrmX.y * n.x + nrmY.y * n.y + nrmZ.y * n.z,
                        nrmX.z * n.x + nrmY.z * n.y + nrmZ.z * n.z};
        };

        // --- Per-mesh frustum cull: project the 8 AABB corners to clip space and
        // skip the whole mesh when every corner is outside one frustum plane
        // (all behind the near plane, or all off one screen edge). A cheap test
        // that drops off-screen meshes before any per-triangle work. ---
        {
            Vec3 lo, hi; mr->mesh.Bounds(lo, hi);
            int outL = 0, outR = 0, outB = 0, outT = 0, outN = 0;
            for (int c = 0; c < 8; ++c) {
                Vec3 corner{(c & 1) ? hi.x : lo.x, (c & 2) ? hi.y : lo.y, (c & 4) ? hi.z : lo.z};
                Vec4 cp = vp * Vec4{model.MultiplyPoint(corner), 1.0f};
                if (cp.w <= 1e-4f) { ++outN; continue; }
                if (cp.x < -cp.w) ++outL;
                if (cp.x >  cp.w) ++outR;
                if (cp.y < -cp.w) ++outB;
                if (cp.y >  cp.w) ++outT;
            }
            if (outN == 8 || outL == 8 || outR == 8 || outB == 8 || outT == 8) continue;
        }
        // Inverted-hull outline pre-pass: draw an expanded shell of BACK faces in a
        // flat outline color behind the mesh, so only a clean silhouette edge survives
        // the main pass. A cosmetic pass — triangles crossing the near plane are
        // skipped rather than clipped.
        if (mr->outline && mr->outlineWidth > 0.0f) {
            std::uint32_t oabgr = Raster::PackRGB(mr->outlineColor.r, mr->outlineColor.g, mr->outlineColor.b);
            for (std::size_t i = 0; i + 2 < t.size(); i += 3) {
                int idx[3] = {t[i], t[i + 1], t[i + 2]};
                Vec3 wp0[3];
                for (int k = 0; k < 3; ++k) wp0[k] = model.MultiplyPoint(v[idx[k]]);
                Vec3 fn = Vec3::Cross(wp0[1] - wp0[0], wp0[2] - wp0[0]).Normalized();
                Vec3 cen = (wp0[0] + wp0[1] + wp0[2]) * (1.0f / 3.0f);
                // Render the same faces the main pass shows (its visible surface), then
                // expand them OUTWARD (away from the mesh, opposite the culling normal)
                // and push them a touch farther, so the unexpanded mesh covers them
                // everywhere except a clean silhouette ring.
                bool mainVisible = Vec3::Dot(fn, eye - cen) >= 0.0f;
                if (!mainVisible) continue;
                Vec3 outward = fn * -1.0f;
                float sxo[3], syo[3], sdo[3]; bool ok = true;
                for (int k = 0; k < 3; ++k) {
                    Vec4 c = vp * Vec4{wp0[k] + outward * mr->outlineWidth, 1.0f};
                    if (c.w <= 1e-4f) { ok = false; break; }
                    float iw = 1.0f / c.w;
                    sxo[k] = (c.x * iw * 0.5f + 0.5f) * W;
                    syo[k] = (1.0f - (c.y * iw * 0.5f + 0.5f)) * H;
                    // Push the shell slightly farther (smaller 1/w) so the mesh always
                    // wins where they overlap — the outline only survives at the rim.
                    sdo[k] = iw * 0.9f;
                }
                if (ok) r.Triangle(sxo[0], syo[0], sdo[0], sxo[1], syo[1], sdo[1],
                                   sxo[2], syo[2], sdo[2], oabgr, bandY0, bandY1);
            }
        }

        // A clip-space vertex carrying its (unprojected) UV, for near-plane
        // clipping. Clipping in homogeneous space before the /w divide is what
        // prevents triangles from exploding/vanishing when you zoom in close.
        struct CV { float x, y, z, w, u, v, lr, lg, lb, nx, ny, nz, wx, wy, wz, fo, sp; };
        for (std::size_t i = 0; i + 2 < t.size(); i += 3) {
            int idx[3] = {t[i], t[i + 1], t[i + 2]};
            Vec3 wp[3];
            for (int k = 0; k < 3; ++k) wp[k] = model.MultiplyPoint(v[idx[k]]);
            Vec3 normal = Vec3::Cross(wp[1] - wp[0], wp[2] - wp[0]).Normalized();
            Vec3 centroid = (wp[0] + wp[1] + wp[2]) * (1.0f / 3.0f);
            float facing = Vec3::Dot(normal, eye - centroid);
            // Backface culling: for a solid (single-sided) mesh, skip triangles
            // that face away from the camera — typically ~half the triangles of a
            // closed mesh, for free. Double-sided meshes (planes, billboards) keep
            // both faces and flip the normal so the lit side always shows.
            if (facing < 0.0f) {
                if (!mr->doubleSided) continue;
                normal = normal * -1.0f;
            }

            // Per-corner clip coords + UV (planar/box projection unless the mesh
            // carries its own UVs).
            Vec3 ln = Vec3::Cross(v[idx[1]] - v[idx[0]], v[idx[2]] - v[idx[0]]);
            float ax = std::fabs(ln.x), ay = std::fabs(ln.y), az = std::fabs(ln.z);
            CV in[3];
            float uvx[3], uvy[3];
            for (int k = 0; k < 3; ++k) {
                Vec4 c = vp * Vec4{wp[k], 1.0f};
                float uu, vt;
                if (hasUV) { uu = mr->mesh.uvs[idx[k]].x; vt = mr->mesh.uvs[idx[k]].y; }
                else {
                    const Vec3& p = v[idx[k]];
                    if (ax >= ay && ax >= az) { uu = p.z + 0.5f; vt = p.y + 0.5f; }
                    else if (ay >= ax && ay >= az) { uu = p.x + 0.5f; vt = p.z + 0.5f; }
                    else { uu = p.x + 0.5f; vt = p.y + 0.5f; }
                }
                uvx[k] = uu; uvy[k] = vt;
                // Per-vertex world normal (smoothed if the mesh has normals, else
                // the face normal), flipped to match a culled back face.
                Vec3 nk = mr->mesh.HasNormals()
                              ? xformNormal(mr->mesh.normals[idx[k]]).Normalized()
                              : normal;
                if (facing < 0.0f) nk = nk * -1.0f;

                // Per-vertex light color (Gouraud) from that normal; used by the
                // matcap and Gouraud paths. Per-pixel (Phong) shading uses nk + the
                // world position directly instead.
                Vec3 lk{1.0f, 1.0f, 1.0f};
                if (useMatcap) {
                    // Matcap: sample the lit-sphere by the camera-space normal.
                    Vec3 fwd = (wp[k] - eye).Normalized();
                    Vec3 rgt = Vec3::Cross(Vec3{0, 1, 0}, fwd);
                    float rl = rgt.Magnitude();
                    rgt = rl > 1e-4f ? rgt * (1.0f / rl) : Vec3{1, 0, 0};
                    Vec3 upv = Vec3::Cross(fwd, rgt);
                    float mx = Vec3::Dot(nk, rgt), my = Vec3::Dot(nk, upv);
                    float mu = mx * 0.5f + 0.5f, mv = 1.0f - (my * 0.5f + 0.5f);
                    Color mc = mcap->Sample(mu, mv);
                    lk = {mc.r, mc.g, mc.b};
                } else if (!unlit) {
                    // Per-vertex light (Gouraud) from the vertex normal — or the flat
                    // FACE normal when the mesh has none. Computing it per vertex (vs a
                    // single per-face value) interpolates smoothly across each face, so
                    // a flat ground under a point light has no hard facet line on its
                    // triangle diagonal even without per-pixel lighting.
                    lk = SceneLights::ShadeColor(wp[k], nk);
                }
                // Per-vertex fog factor (distance from the eye), interpolated across
                // the triangle by the rasterizer. A single per-triangle factor made a
                // hard diagonal seam on big quads (e.g. the ground) when fog was on.
                float fok = 0.0f;
                if (fogOn) {
                    float dd = (wp[k] - eye).Magnitude();
                    fok = (dd - rs.fogStart) / (rs.fogEnd - rs.fogStart);
                    fok = fok < 0.0f ? 0.0f : (fok > 1.0f ? 1.0f : fok);
                }
                // Per-vertex Blinn-Phong specular, interpolated by the rasterizer. A
                // single per-triangle value (from the centroid) made a hard diagonal
                // highlight seam on big quads like a shiny ground.
                float spk = 0.0f;
                if (!unlit && mr->specular > 0.0f) {
                    Vec3 toL = SceneLight::Direction() * -1.0f;
                    Vec3 toE = (eye - wp[k]).Normalized();
                    Vec3 hv = (toL.Normalized() + toE).Normalized();
                    float nh = Vec3::Dot(nk, hv);
                    if (nh > 0.0f) spk = std::pow(nh, mr->shininess) * mr->specular;
                }
                in[k] = {c.x, c.y, c.z, c.w, uu * mr->tiling.x + scrollU, vt * mr->tiling.y + scrollV,
                         lk.x, lk.y, lk.z, nk.x, nk.y, nk.z, wp[k].x, wp[k].y, wp[k].z, fok, spk};
            }

            // Per-triangle world-space tangent (from edges + UV deltas) for normal
            // mapping; constant across the face, so it survives near-plane clipping.
            Vec3 triTangent{1, 0, 0};
            if (normalMips) {
                Vec3 e1 = wp[1] - wp[0], e2 = wp[2] - wp[0];
                float du1 = uvx[1] - uvx[0], dv1 = uvy[1] - uvy[0];
                float du2 = uvx[2] - uvx[0], dv2 = uvy[2] - uvy[0];
                float det = du1 * dv2 - du2 * dv1;
                Vec3 T = std::fabs(det) > 1e-8f ? (e1 * dv2 - e2 * dv1) * (1.0f / det) : e1;
                float tl = T.Magnitude();
                triTangent = tl > 1e-6f ? T * (1.0f / tl) : Vec3{1, 0, 0};
            }

            // Sutherland-Hodgman clip against the true near plane. The plane is
            // z_clip >= -w_clip (NDC z >= -1), NOT just w > 0. Clipping on w alone
            // left geometry BETWEEN the camera and the near plane in the pipeline,
            // where 1/w explodes (up to ~1e4x) and projects vertices to gigantic
            // screen coordinates — precision blows up and the surface glitches when
            // you zoom in very close. Using the real near plane removes that zone
            // and keeps every drawn fragment's depth inside [-1,1].
            const float CLIP_EPS = 1e-5f;
            auto nearDist = [](const CV& c) { return c.z + c.w; };   // >0 = in front of near
            CV poly[8]; int pn = 0;
            for (int e = 0; e < 3; ++e) {
                const CV& A = in[e];
                const CV& B = in[(e + 1) % 3];
                float dA = nearDist(A), dB = nearDist(B);
                bool inA = dA > CLIP_EPS, inB = dB > CLIP_EPS;
                if (inA) poly[pn++] = A;
                if (inA != inB) {
                    float tt = dA / (dA - dB);
                    poly[pn++] = {A.x + (B.x - A.x) * tt, A.y + (B.y - A.y) * tt,
                                  A.z + (B.z - A.z) * tt, A.w + (B.w - A.w) * tt,
                                  A.u + (B.u - A.u) * tt, A.v + (B.v - A.v) * tt,
                                  A.lr + (B.lr - A.lr) * tt, A.lg + (B.lg - A.lg) * tt,
                                  A.lb + (B.lb - A.lb) * tt,
                                  A.nx + (B.nx - A.nx) * tt, A.ny + (B.ny - A.ny) * tt,
                                  A.nz + (B.nz - A.nz) * tt, A.wx + (B.wx - A.wx) * tt,
                                  A.wy + (B.wy - A.wy) * tt, A.wz + (B.wz - A.wz) * tt,
                                  A.fo + (B.fo - A.fo) * tt, A.sp + (B.sp - A.sp) * tt};
                }
            }
            if (pn < 3) continue;   // entirely behind the camera

            // Material: diffuse (Lambert, or flat when unlit) + Blinn-Phong
            // specular highlight + self-illuminating emissive.
            // Colored multi-light diffuse (directional + point + spot + ambient).
            Vec3 lit = unlit ? Vec3{1.0f, 1.0f, 1.0f}
                             : SceneLights::ShadeColor(centroid, normal);
            float spec = 0.0f;
            if (!unlit && mr->specular > 0.0f) {
                Vec3 toLight = SceneLight::Direction() * -1.0f;
                Vec3 toEye = (eye - centroid).Normalized();
                Vec3 h = (toLight.Normalized() + toEye).Normalized();
                float nh = Vec3::Dot(normal, h);
                if (nh > 0.0f) spec = std::pow(nh, mr->shininess) * mr->specular;
            }
            // Per-face color when the mesh carries one (e.g. character skin /
            // outfit / hair); otherwise the renderer's single albedo color.
            const Color& base = faceCols ? mr->mesh.triColors[i / 3] : mr->color;
            float cr = base.r * lit.x + spec + mr->emissive.r;
            float cg = base.g * lit.y + spec + mr->emissive.g;
            float cb = base.b * lit.z + spec + mr->emissive.b;
            // Distance fog: blend the color toward the fog color by how far the
            // triangle is, between fogStart and fogEnd (per-tri factor).
            float fogF = 0.0f;
            if (fogOn) {
                float dist = (centroid - eye).Magnitude();
                fogF = (dist - rs.fogStart) / (rs.fogEnd - rs.fogStart);
                fogF = fogF < 0.0f ? 0.0f : (fogF > 1.0f ? 1.0f : fogF);
                cr = cr * (1.0f - fogF) + fogR * fogF;
                cg = cg * (1.0f - fogF) + fogG * fogF;
                cb = cb * (1.0f - fogF) + fogB * fogF;
            }
            std::uint32_t abgr = Raster::PackRGB(cr, cg, cb);

            // Fan-triangulate the (possibly clipped) polygon and rasterize.
            auto project = [&](const CV& c, float& sx, float& sy, float& sd, float& iw,
                               float& uo, float& vo) {
                iw = 1.0f / c.w;
                sx = (c.x * iw * 0.5f + 0.5f) * W;
                sy = (1.0f - (c.y * iw * 0.5f + 0.5f)) * H;
                sd = iw;   // W-buffer depth (1/w): larger = nearer, uniform precision
                uo = c.u * iw; vo = c.v * iw;
            };
            // Per-pixel (Phong) shading for lit meshes that carry normals — smooth
            // surfaces + correct specular, textured or not. Matcap/unlit meshes
            // keep their existing paths. Meshes WITHOUT normals still go per-pixel:
            // each vertex carries the (constant) face normal, so they stay flat
            // shaded but gain per-pixel shadows, reflections, rim and hemisphere
            // ambient — which the basic flat path can't do. (This is what lets
            // primitives like Cube/Plane and the flat-shaded Character receive
            // cast shadows.)
            // Toon needs the per-pixel path (that's where the cel banding lives), so
            // it forces per-pixel lighting for its mesh regardless of the global toggle.
            const bool perPixel = (PerPixelLighting() || mr->shader == MeshRenderer::Shader::Toon
                                   || mr->shader == MeshRenderer::Shader::Gradient
                                   || mr->shader == MeshRenderer::Shader::Fresnel
                                   || mr->shader == MeshRenderer::Shader::Iridescent
                                   || mr->shader == MeshRenderer::Shader::Hologram
                                   || mr->shader == MeshRenderer::Shader::Posterize
                                   || mr->shader == MeshRenderer::Shader::Velvet
                                   || mr->rimStrength > 0.0f || mr->triplanar)
                                  && !unlit && !useMatcap;
            const int  shaderMode = (int)mr->shader;
            const float gradTopC[3] = {mr->gradientTop.r, mr->gradientTop.g, mr->gradientTop.b};
            const float gradBotC[3] = {mr->gradientBottom.r, mr->gradientBottom.g, mr->gradientBottom.b};
            for (int j = 1; j + 1 < pn; ++j) {
                const CV* tri[3] = {&poly[0], &poly[j], &poly[j + 1]};
                float sx[3], sy[3], sd[3], iw[3], uu[3], vv[3];
                for (int k = 0; k < 3; ++k)
                    project(*tri[k], sx[k], sy[k], sd[k], iw[k], uu[k], vv[k]);
                float fo[3] = {tri[0]->fo, tri[1]->fo, tri[2]->fo};   // per-vertex fog
                float sp3[3] = {tri[0]->sp, tri[1]->sp, tri[2]->sp};  // per-vertex specular
                if (perPixel) {
                    float nx[3] = {tri[0]->nx, tri[1]->nx, tri[2]->nx};
                    float ny[3] = {tri[0]->ny, tri[1]->ny, tri[2]->ny};
                    float nz[3] = {tri[0]->nz, tri[1]->nz, tri[2]->nz};
                    float wx[3] = {tri[0]->wx, tri[1]->wx, tri[2]->wx};
                    float wy[3] = {tri[0]->wy, tri[1]->wy, tri[2]->wy};
                    float wz[3] = {tri[0]->wz, tri[1]->wz, tri[2]->wz};
                    r.TrianglePhong(sx, sy, sd, iw, uu, vv, nx, ny, nz, wx, wy, wz,
                                    tex, base, mr->color, eye, mr->shininess, mr->specular,
                                    mr->emissive.r, mr->emissive.g, mr->emissive.b,
                                    fo, fogR, fogG, fogB,
                                    normalMips, triTangent, mr->normalStrength,
                                    mr->reflectivity, specMips, mr->metallic,
                                    (mr->shader == MeshRenderer::Shader::Toon ? mr->toonBands : 0),
                                    mr->rimStrength, mr->rimPower,
                                    mr->rimColor.r, mr->rimColor.g, mr->rimColor.b,
                                    mr->triplanar, mr->tiling.x, mr->tiling.y,
                                    bandY0, bandY1, shaderMode, gradTopC, gradBotC);
                } else if (tex) {
                    float lr[3] = {tri[0]->lr, tri[1]->lr, tri[2]->lr};
                    float lg[3] = {tri[0]->lg, tri[1]->lg, tri[2]->lg};
                    float lb[3] = {tri[0]->lb, tri[1]->lb, tri[2]->lb};
                    r.TriangleTex(sx, sy, sd, iw, uu, vv, *tex, mr->color, lr, lg, lb, sp3,
                                  mr->emissive.r, mr->emissive.g, mr->emissive.b,
                                  fo, fogR, fogG, fogB, bandY0, bandY1);
                } else if (smooth) {
                    float lr[3] = {tri[0]->lr, tri[1]->lr, tri[2]->lr};
                    float lg[3] = {tri[0]->lg, tri[1]->lg, tri[2]->lg};
                    float lb[3] = {tri[0]->lb, tri[1]->lb, tri[2]->lb};
                    r.TriangleSmooth(sx, sy, sd, lr, lg, lb, base, sp3,
                                     mr->emissive.r, mr->emissive.g, mr->emissive.b,
                                     fo, fogR, fogG, fogB, bandY0, bandY1);
                } else
                    r.Triangle(sx[0], sy[0], sd[0], sx[1], sy[1], sd[1],
                               sx[2], sy[2], sd[2], abgr, bandY0, bandY1);
            }
        }
    }
    };  // renderBand
    // Split the frame into horizontal bands across hardware threads.
    ParallelRows(0, r.height, [&](int ya, int yb) { renderBand(ya, yb - 1); });
    if (SSAOEnabled() && !r.gvalid.empty()) ApplySSAO(r, vp, eye);   // contact AO post-pass
    ApplyBloom(r);                                                   // bright-pass glow
    ApplyToneMap(r);                                                 // filmic highlight rolloff
    ApplyColorGrade(r);                                              // brightness/contrast/sat/vignette/gamma
    ApplyFXAA(r);                                                    // edge anti-aliasing
}

/// Supersampled render for smoother (anti-aliased) edges: draw the scene at `ss`x
/// resolution into `work`, then box-downsample to a w*h ABGR8888 image. Returns a
/// pointer to the w*h pixels to display (work.color when ss==1, else `out`).
/// Clears to transparent first; call ApplySceneLight() before this.
inline const std::uint32_t* RenderMeshesSS(Raster& work, std::vector<std::uint32_t>& out,
                                           const Scene& scene, const Mat4& vp, const Vec3& eye,
                                           int w, int h, int ss, const GameObject* ignore = nullptr) {
    if (w < 1) w = 1; if (h < 1) h = 1; if (ss < 1) ss = 1;
    while (ss > 1 && ((long)w * ss > 4096 || (long)h * ss > 4096)) --ss;
    const int iw = w * ss, ih = h * ss;
    work.Resize(iw, ih);
    work.Clear(0u);
    RenderMeshes(work, scene, vp, eye, ignore);
    if (ss == 1) return work.color.data();
    out.assign((std::size_t)w * h, 0u);
    const std::uint32_t* src = work.color.data();
    const int n = ss * ss;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            unsigned r = 0, g = 0, b = 0, a = 0;
            for (int sy = 0; sy < ss; ++sy) {
                const std::uint32_t* row = src + (std::size_t)(y * ss + sy) * iw + (std::size_t)x * ss;
                for (int sx = 0; sx < ss; ++sx) {
                    std::uint32_t p = row[sx];
                    r += p & 0xFFu; g += (p >> 8) & 0xFFu; b += (p >> 16) & 0xFFu; a += (p >> 24) & 0xFFu;
                }
            }
            out[(std::size_t)y * w + x] =
                (std::uint32_t)(r / n) | ((std::uint32_t)(g / n) << 8) |
                ((std::uint32_t)(b / n) << 16) | ((std::uint32_t)(a / n) << 24);
        }
    return out.data();
}

} // namespace okay
