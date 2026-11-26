#pragma once
#include "okay/Core/ImportSidecar.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Vec4.hpp"
#include "okay/Math/Mat4.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Render/Lighting.hpp"
#include "okay/Render/LightSubset.hpp"
#include "okay/Render/LocalShadowMath.hpp"
#include "okay/Render/ShadowMath.hpp"
#include "okay/Render/Atmosphere.hpp"
#include "okay/Render/DofMath.hpp"
#include "okay/Render/SsaoMath.hpp"
#include "okay/Render/PostQualityMath.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/LODGroup.hpp"
#include "okay/Render/Culling.hpp"
#include "okay/Render/Visibility.hpp"
#include "okay/Render/TriangleSpan.hpp"
#include <unordered_set>
#include "okay/Graphics/Image.hpp"
#include "okay/Render/ProcTexture.hpp"
#include "okay/Render/TextureAsset.hpp"   // the shared texture library every renderer draws from
#include <memory>
#include "okay/Render/OkayShader.hpp"
#include "okay/Texture/Dds.hpp"         // .dds pictures: read and decoded on the CPU (okay::tex)
#include "okay/Core/Log.hpp"
#include <cstring>
#include "okay/Core/Profiler.hpp"
#include "okay/Core/Time.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
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
//
// "Virtual"/cascaded shadows: instead of stretching one map over the whole world
// (blurry once the map covers a big level), we render several cascades that hug a
// shrinking region in front of the camera. The nearest cascade is tiny in world
// units so its texels are dense -> crisp shadows where you're looking; farther
// cascades grow to cover distance cheaply. Each fragment samples the tightest
// cascade that contains it. This is how "sharp + cheap over a huge map" is done on
// DX11-class hardware (the practical stand-in for Nanite-era virtual shadow maps).
struct ShadowCascade {
    std::vector<float> depth;   // min light-space NDC depth per texel
    int   size = 0;
    Mat4  viewProj;
    float texelWorld = 0.0f;    // world size of one shadow texel (for normal-offset bias)
    Vec3  center{0, 0, 0};      // world-space centre of the region this cascade covers
    float radius = 0.0f;        // world-space radius of that region (for cascade selection)
    float depthRange = 1.0f;    // world length of the depth range (NDC z -1..1), turns depth gaps into metres
    Vec3  axisX{1, 0, 0}, axisY{0, 1, 0};   // world directions of the shadow map's x and y (for the receiver-plane depth)
};
struct ShadowMap {
    static const int kMaxCascades = 4;
    ShadowCascade cascade[kMaxCascades];
    int   count   = 0;          // active cascades
    bool  enabled = false;
    Vec3  eye{0, 0, 0};         // camera position this frame (for the distance fade)
    Vec3  lightDir{0, -1, 0};   // the way the sun shines (unit)
    float fadeDistance = 0.0f;  // sun shadows fade out towards this distance (0 = no fade)
};
inline ShadowMap& Shadows();
inline float ShadowFactor(const Vec3& wpos, const Vec3& n);

// Spot / point / area light shadows (software). Declared here so the per-pixel
// shading can ask for them; the maps and the sampling live below ShadowFactor.
inline bool  LocalShadowsActive();
inline void  LocalShadowFactors(const Vec3& wpos, const Vec3& n, float* perLight);

// Fresnel rim light: a soft glow on surfaces facing away from the camera (grazing
// angles), which separates objects from the background and reads as a subtle
// backlight. Tunable; applied in the per-pixel path. Defined here so the Raster's
// shading can use them.
inline bool&  RimLightEnabled() { static bool v = false; return v; }  // off by default: the grazing-angle glow swept across big floors as the camera moved (opt-in in the Rendering panel)
inline float& RimStrength()     { static float v = 0.25f; return v; }
inline float& RimPower()        { static float v = 3.0f; return v; }

// Hemisphere ambient: tint the ambient term by sky (up) vs ground (down). Strength
// scales how far each hemisphere departs from the flat ambient level.
// Legacy globals, superseded by the scene's RenderSettings::ambientMode and
// ambientSkyTint (saved with the scene, used by every renderer). No longer read.
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

// Scene depth for particle occlusion: the per-pixel W-buffer (1/w, larger = nearer)
// from the most recent software 3D render, downsampled to the output resolution.
// The particle overlay samples this so particles hide BEHIND solid geometry instead
// of always drawing on top ("in your face"). `valid` is false when the frame was
// drawn by a GPU renderer (no depth read-back), so the overlay then skips occlusion.
struct OcclusionDepth { std::vector<float> d; int w = 0, h = 0; bool valid = false; };
inline OcclusionDepth& SceneOcclusionDepth() { static OcclusionDepth o; return o; }

/// True if the scene is NEARER than a particle at clip-w `clipW` at normalized
/// screen coords (nx,ny in [0,1]) — so the particle is hidden behind geometry. The
/// normalized coords make this independent of the depth buffer's resolution (the
/// occlusion pre-pass may run at a lower res than the screen). Safe with no/invalid
/// data: returns false (visible).
inline bool ParticleOccluded(float nx, float ny, float clipW) {
    const OcclusionDepth& od = SceneOcclusionDepth();
    if (!od.valid || od.w <= 0 || od.h <= 0) return false;
    int x = (int)(nx * od.w), y = (int)(ny * od.h);
    if (x < 0 || y < 0 || x >= od.w || y >= od.h) return false;
    float sceneNear = od.d[(std::size_t)y * od.w + x];   // 1/w; 0 = empty/far
    if (sceneNear <= 0.0f) return false;                 // nothing drawn here
    float partNear = clipW > 1e-4f ? 1.0f / clipW : 0.0f;
    return sceneNear > partNear * 1.02f;                 // scene clearly in front -> occlude
}

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
// A texture's mip levels plus how it is sampled (from the asset's import settings: Filter and Wrap).
// A view of a TextureAsset's mip chain (shared, not copied), with how it is sampled.
struct MipChain {
    std::shared_ptr<const std::vector<Image>> levels;
    std::uint64_t serial = 0;   // the asset serial the view was taken from
    int filter = TextureSampling::Trilinear;
    int wrap = TextureSampling::Repeat;
    std::size_t size() const { return levels ? levels->size() : 0; }
    bool empty() const { return size() == 0; }
    const Image& operator[](std::size_t i) const { return (*levels)[i]; }
};

/// A Custom (.okayshader) surface for TrianglePhong: the CPU program, the material's property values,
/// its textures and the time. The program runs per pixel before lighting, like the GPU renderers' copy.
struct CustomSurface {
    const oshader::CpuProgram* prog = nullptr;
    const float* props = nullptr;
    const MipChain* tex[oshader::kMaxTextures] = {nullptr, nullptr, nullptr, nullptr};
    float time = 0.0f;
    bool unlit = false;
    // Texture lookup for oshader::RunCpu (bilinear on the top level, wrapped; white when unbound).
    static void Sample(const void* ctx, int slot, float u, float v, float rgba[4]) {
        const CustomSurface* cs = static_cast<const CustomSurface*>(ctx);
        const MipChain* m = (slot >= 0 && slot < oshader::kMaxTextures) ? cs->tex[slot] : nullptr;
        if (!m || m->empty() || (*m)[0].Width() <= 0) { rgba[0] = rgba[1] = rgba[2] = rgba[3] = 1.0f; return; }
        u -= std::floor(u); v -= std::floor(v);
        const Color c = (*m)[0].Sample(u, v);
        rgba[0] = c.r; rgba[1] = c.g; rgba[2] = c.b; rgba[3] = c.a;
    }
};

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
    // Texture address modes (the texture's "Wrap" import setting).
    static int WrapIndex(int a, int n, int mode) {
        if (mode == TextureSampling::Clamp) return a < 0 ? 0 : (a >= n ? n - 1 : a);
        if (mode == TextureSampling::Mirror) {
            const int period = 2 * n;
            int m = a % period; if (m < 0) m += period;
            return m < n ? m : period - 1 - m;
        }
        a %= n; return a < 0 ? a + n : a;
    }
    // Nearest-texel lookup (the "Point" filter), one mip level.
    static Color NearestWrap(const Image& img, float u, float v, int wrap) {
        const int tw = img.Width(), th = img.Height();
        if (tw <= 0 || th <= 0) return Color(1, 1, 1, 1);
        Color c = img.GetPixel(WrapIndex((int)std::floor(u * tw), tw, wrap), WrapIndex((int)std::floor(v * th), th, wrap));
        return Color(c.r, c.g, c.b, 1.0f);
    }
    // Bilinear texture lookup with wrapping (one mip level).
    static Color BilerpWrap(const Image& img, float u, float v, int wrap = TextureSampling::Repeat) {
        int tw = img.Width(), th = img.Height();
        if (tw <= 0 || th <= 0) return Color(1, 1, 1, 1);
        auto wr = [wrap](int a, int n) { return WrapIndex(a, n, wrap); };
        // v = 0 is the image's top row, as on the GPU renderers and in glTF.
        float fu = u * tw - 0.5f, fv = v * th - 0.5f;
        int x0 = (int)std::floor(fu), y0 = (int)std::floor(fv);
        float ax = fu - x0, ay = fv - y0;
        Color c00 = img.GetPixel(wr(x0, tw), wr(y0, th)), c10 = img.GetPixel(wr(x0 + 1, tw), wr(y0, th));
        Color c01 = img.GetPixel(wr(x0, tw), wr(y0 + 1, th)), c11 = img.GetPixel(wr(x0 + 1, tw), wr(y0 + 1, th));
        float r0 = c00.r + (c10.r - c00.r) * ax, r1 = c01.r + (c11.r - c01.r) * ax;
        float g0 = c00.g + (c10.g - c00.g) * ax, g1 = c01.g + (c11.g - c01.g) * ax;
        float b0 = c00.b + (c10.b - c00.b) * ax, b1 = c01.b + (c11.b - c01.b) * ax;
        return Color(r0 + (r1 - r0) * ay, g0 + (g1 - g0) * ay, b0 + (b1 - b0) * ay, 1.0f);
    }
    // Sample a mip chain the way its texture's import settings say: Trilinear blends the two nearest levels,
    // Bilinear smooths within the nearest level, Point takes the nearest texel of the nearest level.
    // `wrap` is Repeat / Clamp / Mirror. A chain with one level (mipmaps off) always reads the full picture.
    static Color SampleMips(const MipChain& mips, float u, float v, float lod) {
        int n = (int)mips.size();
        if (n == 0) return Color(1, 1, 1, 1);
        float maxl = (float)(n - 1);
        lod = lod < 0 ? 0 : (lod > maxl ? maxl : lod);
        if (mips.filter != TextureSampling::Trilinear) {
            const Image& lv = mips[(int)(lod + 0.5f)];
            return mips.filter == TextureSampling::Point ? NearestWrap(lv, u, v, mips.wrap) : BilerpWrap(lv, u, v, mips.wrap);
        }
        int l0 = (int)lod, l1 = l0 + 1 < n ? l0 + 1 : l0;
        float f = lod - l0;
        Color a = BilerpWrap(mips[l0], u, v, mips.wrap);
        if (f <= 0.001f || l1 == l0) return a;
        Color b = BilerpWrap(mips[l1], u, v, mips.wrap);
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
                     const MipChain& mips, const Color& tint,
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
                       const MipChain* mips, const Color& base, const Color& tint, const Vec3& eye,
                       float shininess, float specularK, float er, float eg, float eb,
                       const float* FOG, float fr, float fg, float fb,
                       const MipChain* normalMips = nullptr,
                       const Vec3& tangent = Vec3{1, 0, 0}, float normalStrength = 1.0f,
                       float reflectivity = 0.0f,
                       const MipChain* specMips = nullptr,
                       float metallic = 0.0f,
                       int toonBands = 0,
                       float matRimStr = 0.0f, float matRimPow = 3.0f,
                       float rimR = 1.0f, float rimG = 1.0f, float rimB = 1.0f,
                       bool triplanar = false, float triTileX = 1.0f, float triTileY = 1.0f,
                       int clipY0 = 0, int clipY1 = (1 << 30),
                       int shaderMode = 0, const float* gradTop = nullptr, const float* gradBot = nullptr,
                       const MipChain* aoMips = nullptr, float aoStrength = 1.0f,
                       bool receiveShadows = true, const LightSubset* lightSubset = nullptr,
                       const float* vcol = nullptr,     // vcol: r g b of each corner's vertex colour (9 floats), or null
                       const CustomSurface* custom = nullptr) {   // a Custom (.okayshader) surface, or null
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
        const bool aoMap = aoMips && !aoMips->empty() && (*aoMips)[0].Width() > 0;
        int aw = aoMap ? (*aoMips)[0].Width() : 0, ah = aoMap ? (*aoMips)[0].Height() : 0;
        LodGrad lgA = aoMap ? MakeLodGrad(X, Y, inv, U, V, IW) : LodGrad{};
        // Hoist the global render-state out of the per-pixel loop: these accessors
        // each carry a function-call + thread-safe-static-guard cost that adds up
        // over a million pixels.
        const bool  rimOn     = RimLightEnabled() && RimStrength() > 0.0f;
        const float rimStr    = RimStrength();
        const float rimPow    = RimPower();
        const bool  shadowsOn = Shadows().enabled && receiveShadows;
        const bool  envOn     = EnvSky().enabled;
        const bool  localShadowsOn = receiveShadows && LocalShadowsActive();
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
                // Custom (.okayshader) surface: run the CPU program on the same inputs the GPU
                // shaders give it (uv, position, view, normal, vertex colour, time, material defaults).
                oshader::SurfaceOut cso;
                const bool haveCs = custom && custom->prog;
                if (haveCs) {
                    oshader::SurfaceIn si;
                    const float iwc = w0 * IW[0] + w1 * IW[1] + w2 * IW[2];
                    const float uc = iwc != 0.0f ? (float)((w0 * U[0] + w1 * U[1] + w2 * U[2]) / iwc) : 0.0f;
                    const float vc = iwc != 0.0f ? (float)((w0 * V[0] + w1 * V[1] + w2 * V[2]) / iwc) : 0.0f;
                    si.uv[0] = uc; si.uv[1] = vc;
                    si.position[0] = wpos.x; si.position[1] = wpos.y; si.position[2] = wpos.z;
                    const Vec3 ve = (eye - wpos).Normalized();
                    si.view[0] = ve.x; si.view[1] = ve.y; si.view[2] = ve.z;
                    si.normal[0] = n.x; si.normal[1] = n.y; si.normal[2] = n.z;
                    float vr = 1.0f, vg = 1.0f, vb = 1.0f;
                    if (vcol) { vr = a0 * vcol[0] + a1 * vcol[3] + a2 * vcol[6]; vg = a0 * vcol[1] + a1 * vcol[4] + a2 * vcol[7]; vb = a0 * vcol[2] + a1 * vcol[5] + a2 * vcol[8]; }
                    si.vcolor[0] = vr; si.vcolor[1] = vg; si.vcolor[2] = vb;
                    si.time = custom->time;
                    float ar = base.r, ag = base.g, ab = base.b;
                    if (textured && iwc != 0.0f) {
                        const Color tc = SampleMips(*mips, uc, vc, PixelLod(uc * iwc, vc * iwc, iwc, lg2, tw, th));
                        ar = tc.r * tint.r; ag = tc.g * tint.g; ab = tc.b * tint.b;
                    }
                    si.albedo[0] = ar * vr; si.albedo[1] = ag * vg; si.albedo[2] = ab * vb;
                    si.emission[0] = er; si.emission[1] = eg; si.emission[2] = eb;
                    si.alpha = base.a; si.metallic = metallic; si.specular = specularK; si.shininess = shininess;
                    oshader::RunCpu(*custom->prog, si, custom->props, &CustomSurface::Sample, custom, cso);
                    if (cso.discard) continue;
                    const Vec3 cn{cso.normal[0], cso.normal[1], cso.normal[2]};
                    const float cl = cn.Magnitude();
                    if (cl > 1e-6f && std::isfinite(cl)) n = cn * (1.0f / cl);
                    if (custom->unlit) {   // no lighting and no fog, like the GPU shaders' unlit path
                        float cr = cso.albedo[0] + cso.emission[0], cg = cso.albedo[1] + cso.emission[1], cb = cso.albedo[2] + cso.emission[2];
                        if (cso.alpha < 0.999f) {
                            const std::uint32_t dst = color[i];
                            const float a = cso.alpha < 0.0f ? 0.0f : cso.alpha, ia = 1.0f - a;
                            cr = cr * a + (float)(dst & 0xFF) / 255.0f * ia;
                            cg = cg * a + (float)((dst >> 8) & 0xFF) / 255.0f * ia;
                            cb = cb * a + (float)((dst >> 16) & 0xFF) / 255.0f * ia;
                            color[i] = PackRGB(cr, cg, cb);
                        } else {
                            depth[i] = d;
                            color[i] = PackRGB(cr, cg, cb);
                            if (!gvalid.empty()) { gpos[i] = wpos; gnrm[i] = n; gvalid[i] = 1; }
                        }
                        continue;
                    }
                }
                const float pxSpecK = haveCs ? cso.specular : specularK;
                const float pxShine = haveCs ? cso.shininess : shininess;
                const float pxMetal = haveCs ? cso.metallic : metallic;
                const float pxEr = haveCs ? cso.emission[0] : er, pxEg = haveCs ? cso.emission[1] : eg, pxEb = haveCs ? cso.emission[2] : eb;
                const float pxAlpha = haveCs ? cso.alpha : base.a;
                // Spot / point / area shadows: every light gets its own 0..1 factor
                // (the sun's comes from its cascades), so a lamp's shadow does not
                // darken the sun and the other way round. Off = the original path.
                float lightSh[SceneLights::kMaxShadowedLights];
                const bool perLightSh = localShadowsOn;
                float sh = 1.0f;
                Vec3 lit;
                if (perLightSh) {
                    LocalShadowFactors(wpos, n, lightSh);
                    if (shadowsOn) {
                        sh = ShadowFactor(wpos, n);
                        const int sunI = SceneLights::SunIndex();
                        if (sunI >= 0 && sunI < SceneLights::kMaxShadowedLights) lightSh[sunI] = sh;
                    }
                    lit = lightSubset ? ShadeNormalizedSubset(wpos, n, lightSh, *lightSubset)
                                      : SceneLights::ShadeNormalized(wpos, n, lightSh);
                } else {
                    lit = lightSubset ? ShadeNormalizedSubset(wpos, n, nullptr, *lightSubset)
                                      : SceneLights::ShadeNormalized(wpos, n);   // n already unit
                }
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
                // Ambient-occlusion map: darken the ambient + diffuse lighting in
                // creases/contact areas. 1 = lit.
                float ao = 1.0f;
                if (aoMap) {
                    float iwa = w0 * IW[0] + w1 * IW[1] + w2 * IW[2];
                    if (iwa != 0.0f) {
                        float ua = (w0 * U[0] + w1 * U[1] + w2 * U[2]) / iwa;
                        float va = (w0 * V[0] + w1 * V[1] + w2 * V[2]) / iwa;
                        Color ac = SampleMips(*aoMips, ua, va, PixelLod(ua * iwa, va * iwa, iwa, lgA, aw, ah));
                        float l = 0.2126f * ac.r + 0.7152f * ac.g + 0.0722f * ac.b;
                        ao = 1.0f - aoStrength * (1.0f - l);   // aoStrength blends toward full occlusion
                    }
                }
                // Cast shadows: fade the direct light toward the ambient floor for
                // fragments occluded from the light (specular is shadowed below).
                if (shadowsOn && !perLightSh) {
                    sh = ShadowFactor(wpos, n);
                    if (sh < 0.999f) {
                        Vec3 amb = SceneLights::AmbientAtPoint(wpos, n);
                        lit.x = amb.x + (lit.x - amb.x) * sh; if (lit.x < 0) lit.x = 0;
                        lit.y = amb.y + (lit.y - amb.y) * sh; if (lit.y < 0) lit.y = 0;
                        lit.z = amb.z + (lit.z - amb.z) * sh; if (lit.z < 0) lit.z = 0;
                    }
                }
                // Toon (cel) shader: quantize the diffuse lighting into hard bands so
                // the surface reads as flat cartoon shades instead of a smooth ramp.
                if (toonBands > 0) {
                    float q = (float)toonBands;
                    lit.x = std::ceil(std::fmin(lit.x, 1.0f) * q) / q;   // bands stop at 1 (over-bright light has no extra band)
                    lit.y = std::ceil(std::fmin(lit.y, 1.0f) * q) / q;
                    lit.z = std::ceil(std::fmin(lit.z, 1.0f) * q) / q;
                }
                if (aoMap) { lit.x *= ao; lit.y *= ao; lit.z *= ao; }   // occlude ambient + diffuse
                // Colored Blinn-Phong specular from every light (sun + point + spot),
                // each tinted by its own color and attenuation; the sun's part is
                // shadowed. Scaled by the material strength and the gloss map.
                Vec3 spec{0, 0, 0};
                if (pxSpecK > 0.0f) {
                    Vec3 toEye = (eye - wpos).Normalized();
                    spec = lightSubset ? SpecularSubset(wpos, n, toEye, pxShine, sh, perLightSh ? lightSh : nullptr, *lightSubset)
                                       : SceneLights::SpecularN(wpos, n, toEye, pxShine, sh, perLightSh ? lightSh : nullptr);
                    float ks = pxSpecK * gloss;
                    spec.x *= ks; spec.y *= ks; spec.z *= ks;
                    // Toon: collapse the highlight to a single hard glint (on/off).
                    if (toonBands > 0) {
                        float sm = std::fmax(spec.x, std::fmax(spec.y, spec.z));
                        float st = sm > 0.5f * ks ? ks : 0.0f;
                        spec.x = st; spec.y = st; spec.z = st;
                    }
                }
                float br = base.r, bg = base.g, bb2 = base.b;
                if (vcol) { br *= a0 * vcol[0] + a1 * vcol[3] + a2 * vcol[6]; bg *= a0 * vcol[1] + a1 * vcol[4] + a2 * vcol[7]; bb2 *= a0 * vcol[2] + a1 * vcol[5] + a2 * vcol[8]; }
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
                // A Custom surface's albedo replaces the base colour worked out above.
                if (haveCs) { br = cso.albedo[0]; bg = cso.albedo[1]; bb2 = cso.albedo[2]; }
                float metal = pxMetal < 0 ? 0 : (pxMetal > 1 ? 1 : pxMetal);
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
                float cr = br * lit.x * diff + spec.x * f0r + rim * lit.x + mrimR + pxEr;
                float cg = bg * lit.y * diff + spec.y * f0g + rim * lit.y + mrimG + pxEg;
                float cb = bb2 * lit.z * diff + spec.z * f0b + rim * lit.z + mrimB + pxEb;
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
                // Transparency: a material alpha < 1 (e.g. water) blends over what's
                // already in the buffer instead of overwriting it, and does NOT write
                // depth — so geometry behind shows through. (Order transparent meshes
                // after opaque ones — they draw in scene order.)
                if (pxAlpha < 0.999f) {
                    std::uint32_t dst = color[i];
                    float a = pxAlpha < 0.0f ? 0.0f : pxAlpha, ia = 1.0f - a;
                    cr = cr * a + (float)(dst & 0xFF)        / 255.0f * ia;
                    cg = cg * a + (float)((dst >> 8) & 0xFF) / 255.0f * ia;
                    cb = cb * a + (float)((dst >> 16) & 0xFF)/ 255.0f * ia;
                    color[i] = PackRGB(cr, cg, cb);
                } else {
                    depth[i] = d;
                    color[i] = PackRGB(cr, cg, cb);
                    if (!gvalid.empty()) { gpos[i] = wpos; gnrm[i] = n; gvalid[i] = 1; }
                }
            }
        }
    }
};

// The texture every renderer draws with comes from one shared library (okay/Render/TextureAsset.hpp): one decoded
// asset per name, loaded through AssetManager, reference counted, hot reloaded, counted in MemoryTracker. The functions
// below are the software renderer's (and the older callers') way into it.

// How the GPU renderers sample an asset: its import Filter / Wrap / Mipmaps / anisotropy (defaults for "@" and "proc:").
inline TextureSampling TextureSamplingOf(const TextureAsset& a) {
    TextureSampling t;
    t.filter = a.settings.filter;
    t.wrap = a.settings.wrap;
    t.mipmaps = a.settings.mipmaps;
    t.anisotropy = a.settings.filter == TextureSettings::Trilinear ? a.settings.anisotropy : 1;   // with trilinear only
    return t;
}

// Bumped whenever cached pictures are dropped, so GPU renderers that keep their own copy by name re-check them.
inline unsigned& TextureCacheGeneration() { static unsigned g = 0; return g; }

// Register an in-memory image under a name so it can be used without a file on
// disk (e.g. an embedded/procedural matcap that ships inside the exe). Names
// conventionally start with '@' to avoid colliding with real file paths.
inline void RegisterTexture(const std::string& name, Image img) {
    textures::Register(name, std::move(img));
}

// A .dds picture as RGBA8: its top level, block formats decoded on the CPU (okay::tex::DecodeBc). The texture library
// decodes .dds files this way for the software and Direct3D 12 renderers; OpenGL and Direct3D 11 upload them compressed.
inline bool LoadDdsImage(const std::string& path, Image& img) {
    tex::DdsImage d;
    std::string why;
    std::vector<std::uint8_t> rgba;
    if (!tex::ReadDdsFile(path, &d, &why) || !tex::DdsLevelToRGBA8(d, 0, &rgba)) {
        Log::Warning("[asset] cannot load image '", path, "': ", why.empty() ? std::string("decode failed") : why);
        img = Image();
        return false;
    }
    img = Image(d.width, d.height);
    std::memcpy(img.Data(), rgba.data(), rgba.size());
    return true;
}

// The picture for a texture name ("proc:..." procedural, "@..." registered, a .dds, any image file), with its import
// settings applied; null when it cannot be loaded. Shared with every renderer (textures::ForRender).
inline Image* GetCachedTexture(const std::string& path) {
    const TextureAsset* a = textures::ForRender(path);
    return a ? const_cast<Image*>(&a->image) : nullptr;
}

// The software renderer's sampling views of the assets' mip chains (filter and wrap from the import settings), rebuilt
// when the asset's serial changes (hot reload, reimport, a painted terrain splat).
inline std::unordered_map<std::string, MipChain>& MipCacheMap() {
    static std::unordered_map<std::string, MipChain> cache;
    return cache;
}

// Forget a picture's cached pixels and mip chain so the next draw reads the file and its import settings again
// (the editor calls this after Apply on the Import Settings box or a reimport). A picture still held elsewhere is
// decoded again in place, so every handle sees the new version.
inline void InvalidateTextureCache(const std::string& path) {
    textures::Drop(path);
    textures::Reload(path);
    MipCacheMap().erase(path);
    ++TextureCacheGeneration();
}

// Drop every picture loaded from a file (pictures registered by name, "@...", are kept: they have no file to
// read again). For when the caller does not know which cache key a file went in under.
inline void InvalidateAllTextureCaches() {
    textures::InvalidateAll();
    MipCacheMap().clear();
    ++TextureCacheGeneration();
}

// A texture's mip chain: the asset's own (built once with our gamma-correct box or Kaiser filter, see TextureAsset::Mips).
// Returns null if the texture failed to load / is empty.
inline const MipChain* GetCachedMips(const std::string& path) {
    const TextureAsset* a = textures::ForRender(path);
    auto& cache = MipCacheMap();
    auto it = cache.find(path);
    if (a && it != cache.end() && it->second.serial == a->serial && it->second.levels) return &it->second;
    if (textures::ReadOnly()) return nullptr;   // the render bands (several threads) never change the cache
    if (!a) { if (it != cache.end()) cache.erase(it); return nullptr; }
    MipChain& c = cache[path];
    if (c.serial != a->serial || !c.levels) {
        c.levels = a->SharedMips();
        c.serial = a->serial;
        c.filter = a->settings.filter;
        c.wrap = a->settings.wrap;
    }
    return c.empty() ? nullptr : &c;
}

/// Per-pixel (Phong) lighting toggle. On by default for quality (smooth shading
/// + correct specular on every pixel); turn off to fall back to faster per-vertex
/// (Gouraud) shading on very large scenes.
// Diagnostic switches for tests and the benchmark. Both are on by default, and each one only skips work that cannot change a
// pixel (tests/test_software_perf_paths.cpp renders scenes with them on and off and compares every pixel).
inline bool& BandSkipEnabled()     { static bool v = true; return v; }   // meshes and triangles wholly outside a thread's rows are not processed by that thread
inline bool& LightCullingEnabled() { static bool v = true; return v; }   // each mesh shades with only the lights that can reach it

inline bool& PerPixelLighting() { static bool v = false; return v; }  // default off (per request); flat/Gouraud path, now crack-free via double-precision rasterization. Opt-in in the Rendering panel.

// ---- Directional shadow mapping --------------------------------------------
// A depth map rendered from the scene's directional light. Per-pixel shading
// projects each fragment into this map and darkens it if something is closer to
// the light (i.e. it's occluded), giving real cast shadows. (ShadowMap struct is
// declared above the Raster class so shading can consult it.)
inline ShadowMap& Shadows()      { static ShadowMap s; return s; }
inline bool& ShadowsEnabled()    { static bool v = true; return v; }   // on by default (modern look); opt-out
/// When shadows are on and this frame has a shadow map, lit meshes that receive shadows take the per-pixel
/// path even if PerPixelLighting() is off (shadows are only sampled there). Default on, so shadows show by
/// default. Turn off to get the faster per-vertex path back (it draws no shadows).
inline bool& ShadowsUsePerPixel() { static bool v = true; return v; }
inline int&  ShadowMapResolution(){ static int s = 2048; return s; }   // per-cascade texels
/// The software renderer never builds a cascade larger than this many texels a side, whatever ShadowMapResolution() says (the GPU
/// renderers do not use this). Three 2048 x 2048 cascades took about 200 ms a frame on the CPU, which is the whole frame of a machine that
/// fell back to the software renderer; 1024 is about four times cheaper and still reads as a shadow. Set 4096 to get the old sharpness back.
inline int&  SoftwareShadowCap()  { static int s = [] { const char* e = std::getenv("OKAY_SOFTWARE_SHADOW_CAP"); const int v = e ? std::atoi(e) : 0; return v >= 64 ? v : 1024; }(); return s; }

/// How far (world units) cascaded shadows reach in front of the camera. The near
/// cascade hugs a small slice of this so its shadows stay crisp no matter how big
/// the level is; the far cascade stretches to the full distance. 0 (or less) =
/// legacy mode: one map fit to the whole scene (blurry on big maps, kept for
/// back-compat). Default focuses on the playable area around the camera.
inline float& ShadowDistance()   { static float v = 80.0f; return v; }
/// Number of cascades (1..ShadowMap::kMaxCascades). More = sharper falloff across
/// distance at the cost of extra depth passes. 0/legacy distance ignores this.
inline int&   ShadowCascades()   { static int v = 3; return v; }

namespace detail {
/// Rasterize every shadow-casting triangle in `scene` into one cascade's depth
/// buffer (min light-space depth per texel). `cull` skips casters whose bounds
/// don't touch the cascade region, keeping far cascades cheap.
void RenderCascadeDepth(const Scene& scene, ShadowCascade& cas, bool cull);

/// Fit a single cascade's light-space ortho to a world sphere (centre/radius),
/// snapping the centre to the texel grid so shadows don't shimmer as the camera
/// moves, then rasterize the scene depth into it.
void BuildCascade(const Scene& scene, ShadowCascade& cas, const Vec3& center,
                         float radius, const Vec3& L, float depthPad, int S, bool cull);
} // namespace detail

/// Render the scene depth from the directional light into the cascaded shadow
/// maps. `vp` and `eye` are the camera's view-projection and position — the
/// cascades are focused in front of the camera. Called by RenderMeshes.
void RenderShadowMap(const Scene& scene, const Mat4& vp, const Vec3& eye);

/// Compute a SINGLE directional-light orthographic view-projection that the GPU
/// backends (OpenGL / D3D11 / D3D12) render their depth pre-pass into and then
/// sample for real cast shadows. This reuses the exact ortho/LookAt fit the
/// software cascades use (so GPU shadows line up with the CPU reference), but
/// collapses to one camera-focused map: the region is a sphere of radius
/// ~ShadowDistance centred just in front of the camera (or the whole scene when
/// ShadowDistance<=0). Returns false when there's nothing to shadow.
///   `outVP`        light-space view-projection (clip space, GL/D3D both use the
///                  0.5*xy+0.5 remap on the GPU side).
///   `outTexelWorld` world size of one shadow texel (for normal-offset bias).
bool ComputeDirectionalShadowVP(const Scene& scene, const Mat4& camVp, const Vec3& eye,
                                       Mat4& outVP, float& outTexelWorld);

/// Up-facing ("sky") and down-facing ("ground") ambient colours for the scene's
/// Ambient Mode, from the gathered flat ambient `amb`. Every renderer blends
/// between the two by the surface normal's Y, so they all shade alike:
///   Flat       sky = ground = amb
///   Hemisphere sky = amb * 1.15, ground = amb * 0.55
///   Sky        amb shifted toward the sky gradient (top/horizon) and the Sky
///              Bottom colour by ambientSkyTint; the midpoint stays amb.
inline void SceneAmbientColors(const Scene::RenderSettings& rs, const Vec3& amb, Vec3& sky, Vec3& ground) {
    auto cl = [](float v) { return v < 0.0f ? 0.0f : v; };
    if (rs.ambientMode == Scene::RenderSettings::AmbientFlat) { sky = ground = amb; return; }
    if (rs.ambientMode != Scene::RenderSettings::AmbientSky) {
        sky = amb * 1.15f; ground = amb * 0.55f; return;
    }
    Vec3 top, hor, grdC;
    EffectiveSkyColors(rs, top, hor, grdC);   // the gradient, or the sun's palette (Procedural sky)
    Vec3 skyC = (top + hor) * 0.5f;
    Vec3 mid = (skyC + grdC) * 0.5f;
    float k = rs.ambientSkyTint;
    sky    = {cl(amb.x + (skyC.x - mid.x) * k), cl(amb.y + (skyC.y - mid.y) * k), cl(amb.z + (skyC.z - mid.z) * k)};
    ground = {cl(amb.x + (grdC.x - mid.x) * k), cl(amb.y + (grdC.y - mid.y) * k), cl(amb.z + (grdC.z - mid.z) * k)};
}

/// Shadow softness in shadow-map texels (the Poisson disk's radius). Larger =
/// softer, fuzzier shadow edges; smaller = crisp/hard. ~2.5 reads as soft-ish.
inline float& ShadowSoftness() { static float v = 2.5f; return v; }

/// The shadow lookup as it was before the lighting upgrade (see ClassicLighting()).
inline float ShadowFactorClassic(const Vec3& wpos, const Vec3& n) {
    ShadowMap& sm = Shadows();
    if (!sm.enabled || sm.count <= 0) return 1.0f;
    static const float kPoisson[6][2] = {
        { 0.000f,  0.000f}, {-0.326f, -0.406f}, { 0.840f, -0.074f},
        {-0.696f,  0.457f}, { 0.185f,  0.893f}, { 0.507f, -0.640f},
    };
    const float radius = ShadowSoftness();
    const float bias = 0.004f;
    const LightSample* sun = SceneLights::Sun();
    const float strength = sun ? sun->shadowStrength : 1.0f;
    const float biasMul = sun ? sun->shadowBias : 1.0f;
    for (int ci = 0; ci < sm.count; ++ci) {
        const ShadowCascade& cas = sm.cascade[ci];
        if (cas.size <= 0) continue;
        bool last = (ci == sm.count - 1);
        if (!last) {
            Vec3 d = wpos - cas.center;
            if (d.x * d.x + d.y * d.y + d.z * d.z > cas.radius * cas.radius) continue;
        }
        Vec3 p = wpos + n * (cas.texelWorld * 3.5f * biasMul);
        Vec4 lc = cas.viewProj * Vec4{p, 1.0f};
        if (lc.w == 0) return 1.0f;
        float iw = 1.0f / lc.w, x = lc.x * iw, y = lc.y * iw, z = lc.z * iw;
        if (x < -1 || x > 1 || y < -1 || y > 1 || z > 1) {
            if (last) return 1.0f; else continue;
        }
        float fx = (x * 0.5f + 0.5f) * cas.size, fy = (1.0f - (y * 0.5f + 0.5f)) * cas.size;
        float lit = 0.0f;
        for (int s = 0; s < 6; ++s) {
            int px = (int)(fx + kPoisson[s][0] * radius);
            int py = (int)(fy + kPoisson[s][1] * radius);
            if (px < 0 || py < 0 || px >= cas.size || py >= cas.size) { lit += 1.0f; continue; }
            if (z - bias * biasMul <= cas.depth[(std::size_t)py * cas.size + px]) lit += 1.0f;
        }
        return 1.0f - strength * (1.0f - lit / 6.0f);
    }
    return 1.0f;
}

/// 0 = fully shadowed, 1 = fully lit, for a world position.
/// Picks the tightest cascade that contains the point (crispest available), then:
///   1. pushes the point along its normal by an amount that grows with the slope (normal offset),
///   2. looks for blockers around it and measures the gap between blocker and receiver,
///   3. filters a disc whose size grows with that gap (soft penumbra: sharp where an object touches the ground,
///      softer the farther the shadow falls), comparing depths with a slope-scaled bias,
///   4. fades the whole shadow out over the last fifth of the shadow distance.
/// `ShadowSoftness()` is the smallest filter radius; the sun's Soft Edges setting widens it with the gap.
inline float ShadowFactor(const Vec3& wpos, const Vec3& n) {
    if (ClassicLighting()) return ShadowFactorClassic(wpos, n);
    ShadowMap& sm = Shadows();
    if (!sm.enabled || sm.count <= 0) return 1.0f;

    const float fade = shadowmath::DistanceFade((wpos - sm.eye).Magnitude(), sm.fadeDistance);
    if (fade <= 0.0f) return 1.0f;
    const float baseR = ShadowSoftness();
    const LightSample* sun = SceneLights::Sun();
    const float strength = (sun ? sun->shadowStrength : 1.0f) * fade;   // per-light Strength, faded with distance
    const float biasMul = sun ? sun->shadowBias : 1.0f;                  // per-light Bias
    const float soft = sun ? sun->shadowSoftness : 0.5f;                 // per-light Soft Edges
    float ndl = Vec3::Dot(n, sm.lightDir * -1.0f);
    ndl = ndl < 0.0f ? 0.0f : (ndl > 1.0f ? 1.0f : ndl);

    // Choose the smallest cascade whose region contains the fragment (its texels
    // are the densest available there); fall back to the last (widest) cascade.
    for (int ci = 0; ci < sm.count; ++ci) {
        const ShadowCascade& cas = sm.cascade[ci];
        if (cas.size <= 0) continue;
        bool last = (ci == sm.count - 1);
        if (!last) {
            Vec3 d = wpos - cas.center;
            if (d.x * d.x + d.y * d.y + d.z * d.z > cas.radius * cas.radius) continue;  // try the next ring out
        }
        Vec3 p = wpos + n * shadowmath::NormalOffset(ndl, cas.texelWorld, biasMul);   // normal-offset bias
        Vec4 lc = cas.viewProj * Vec4{p, 1.0f};
        if (lc.w == 0) return 1.0f;
        float iw = 1.0f / lc.w, x = lc.x * iw, y = lc.y * iw, z = lc.z * iw;
        if (x < -1 || x > 1 || y < -1 || y > 1 || z > 1) {
            if (last) return 1.0f; else continue;
        }
        const float fx = (x * 0.5f + 0.5f) * cas.size, fy = (1.0f - (y * 0.5f + 0.5f)) * cas.size;
        const float zPerWorld = 2.0f / (cas.depthRange > 1e-4f ? cas.depthRange : 1.0f);   // NDC depth per metre
        const float constBias = cas.texelWorld * 1.0f * biasMul * zPerWorld;                  // about one texel; the slope is handled per tap
        auto tapDepth = [&](float ox, float oy, float& dOut, float& shiftNdc) {
            shiftNdc = shadowmath::ReceiverPlaneShift(n, sm.lightDir, cas.axisX, cas.axisY, ox, oy, cas.texelWorld) * zPerWorld;
            const int px = (int)(fx + ox), py = (int)(fy + oy);
            if (px < 0 || py < 0 || px >= cas.size || py >= cas.size) return false;
            dOut = cas.depth[(std::size_t)py * cas.size + px];
            return true;
        };
        // 1. Blocker search: the centre, a ring as wide as the minimum filter, and a ring a bit wider (13 taps). The inner ring matters: a
        //    small caster (a head) can be one texel wide in a coarse cascade, and a sparse wide ring alone would step over it.
        const float searchR = baseR + 1.5f + 4.0f * soft;
        int blockers = 0; float gapSum = 0.0f;
        for (int k = -1; k < 12; ++k) {
            float tx = 0.0f, ty = 0.0f, rr = 0.0f;
            if (k >= 0) { shadowmath::DiscTap(k % 6, 6, tx, ty); const float len = std::sqrt(tx * tx + ty * ty); if (len > 1e-4f) { tx /= len; ty /= len; } rr = k < 6 ? baseR : searchR; }
            float d, sh;
            if (!tapDepth(tx * rr, ty * rr, d, sh)) continue;
            if (z + sh - constBias > d) { ++blockers; gapSum += (z + sh - d) / zPerWorld; }
        }
        if (blockers == 0) return 1.0f;                           // nothing in the way: no need to filter
        const float gap = gapSum / (float)blockers;
        // 2. Filter radius from the gap, then a disc of taps, each compared with the receiver plane at that tap.
        const float radius = shadowmath::PenumbraTexels(gap, cas.texelWorld, soft, baseR);
        const int taps = radius > 3.0f ? 16 : 8;
        float lit = 0.0f;
        for (int k = 0; k < taps; ++k) {
            float tx, ty; shadowmath::DiscTap(k, taps, tx, ty);
            float d, sh;
            if (!tapDepth(tx * radius, ty * radius, d, sh)) { lit += 1.0f; continue; }
            if (z + sh - constBias <= d) lit += 1.0f;
        }
        return 1.0f - strength * (1.0f - lit / (float)taps);
    }
    return 1.0f;
}

// ---- Spot / point / area light shadows -------------------------------------
// The software renderer's local light shadows. Each shadow-casting spot light gets
// one perspective depth map, each point or area light six cube-face maps, built
// with the same views the GPU backends use (BuildLocalLightView). Maps store the
// distance along the view axis (1/w is linear on screen, so it is interpolated
// and inverted per texel), which lets the bias be a plain world distance.
//
// Gates: ShadowsEnabled() (the same switch as the sun), the light's own Cast
// Shadows and Strength, the mesh's Cast / Receive Shadows. Like the sun's shadow
// it is drawn only on the per-pixel lighting path (PerPixelLighting, or a shader
// that forces it). Up to 16 tiles per frame, nearest lights first (a spot is 1
// tile, a point light 6), the same budget as the GPU atlas.
struct LocalShadowMapSW {
    int light = -1;                       // index into SceneLights::List()
    LocalLightView view;
    std::vector<float> depth[6];          // view distance per texel; 1e30 = empty
};
struct LocalShadowSet {
    static const int kMaxTiles = 16;
    std::vector<LocalShadowMapSW> maps;
    int  size = 0;                        // texels per tile edge
    int  lightMap[SceneLights::kMaxShadowedLights];   // light index -> maps index, -1 = none
    bool enabled = false;
    LocalShadowSet() { for (int& m : lightMap) m = -1; }
};
inline LocalShadowSet& LocalShadows() { static LocalShadowSet s; return s; }
/// Texels per local shadow tile edge (software). 256 keeps 16 tiles near 4 MB.
inline int& LocalShadowResolution() { static int v = 256; return v; }
inline bool LocalShadowsActive() { return LocalShadows().enabled; }

namespace detail {
/// Rasterize one clip-space triangle's view distance into a local shadow tile.
/// The triangle is cut against the near plane first (w > nearZ), so a big ground
/// quad that reaches behind the light still shadows what is in front of it.
void RasterLocalDepth(std::vector<float>& depth, int S, const Vec4 in[3], float nearZ);
} // namespace detail

/// Build this frame's spot / point / area shadow maps. Called by RenderMeshes
/// after the sun's cascades. Leaves LocalShadows().enabled false when nothing in
/// the scene needs one.
void RenderLocalShadowMaps(const Scene& scene, const Mat4& camVp, const Vec3& eye);

/// 0 = fully shadowed, 1 = fully lit, for light `li` at world point `wpos` with
/// unit normal `n`. 1 when the light has no map. The receiver is moved along its
/// normal and toward the light by a few texels (a world-space bias, like the GPU
/// shaders), then a 6-tap Poisson filter reads the map.
inline float LocalShadowFactor(int li, const Vec3& wpos, const Vec3& n) {
    LocalShadowSet& ls = LocalShadows();
    if (!ls.enabled || li < 0 || li >= SceneLights::kMaxShadowedLights) return 1.0f;
    const int mi = ls.lightMap[li];
    if (mi < 0) return 1.0f;
    const LocalShadowMapSW& m = ls.maps[mi];
    const LightSample& L = SceneLights::List()[li];
    Vec3 toL = L.pos - wpos;
    const float dist = toL.Magnitude();
    if (dist < 1e-4f || dist >= L.range) return 1.0f;           // at the light, or out of its reach
    const Vec3 dir = toL * (1.0f / dist);
    const int face = m.view.tiles == 6 ? CubeFace(wpos - L.pos) : 0;
    const float texel = m.view.texelAt1 * dist;                  // world size of a texel here
    Vec3 p = wpos + n * (texel * 2.0f * L.shadowBias) + dir * (texel * 1.5f * L.shadowBias);
    Vec4 c = m.view.vp[face] * Vec4{p, 1.0f};
    if (c.w <= m.view.nearZ) return 1.0f;                        // behind the spot's view
    const float iw = 1.0f / c.w, x = c.x * iw, y = c.y * iw;
    if (x < -1.0f || x > 1.0f || y < -1.0f || y > 1.0f) return 1.0f;   // outside the cone's view
    static const float kPoisson[6][2] = {
        { 0.000f,  0.000f}, {-0.326f, -0.406f}, { 0.840f, -0.074f},
        {-0.696f,  0.457f}, { 0.185f,  0.893f}, { 0.507f, -0.640f},
    };
    const int S = ls.size;
    const float fx = (x * 0.5f + 0.5f) * S, fy = (1.0f - (y * 0.5f + 0.5f)) * S;
    const float radius = ShadowSoftness() * 0.6f;
    const float eps = texel * 0.75f * L.shadowBias;
    float lit = 0.0f;
    for (int k = 0; k < 6; ++k) {
        int px = (int)(fx + kPoisson[k][0] * radius), py = (int)(fy + kPoisson[k][1] * radius);
        if (px < 0 || py < 0 || px >= S || py >= S) { lit += 1.0f; continue; }
        if (c.w <= m.depth[face][(std::size_t)py * S + px] + eps) lit += 1.0f;
    }
    return 1.0f - L.shadowStrength * (1.0f - lit / 6.0f);
}

/// Fill `perLight[0 .. kMaxShadowedLights)` with every light's local shadow
/// factor (1 for the sun, for lights without a map, and past the light count).
inline void LocalShadowFactors(const Vec3& wpos, const Vec3& n, float* perLight) {
    const auto& lights = SceneLights::List();
    for (int i = 0; i < SceneLights::kMaxShadowedLights; ++i) {
        perLight[i] = (i < (int)lights.size() && lights[i].type != 0)
                          ? LocalShadowFactor(i, wpos, n) : 1.0f;
    }
}

// ---- Screen-space ambient occlusion ----------------------------------------
inline bool&  SSAOEnabled()  { static bool v = false; return v; }     // off by default (perf); opt-in
inline float& SSAORadius()   { static float v = 0.45f; return v; }  // world units
inline float& SSAOStrength() { static float v = 0.6f; return v; }
inline float& SSAOPower()    { static float v = 1.0f; return v; }   // occlusion ^ power (PostQualityMath.hpp AoCurve); 1 = unchanged

/// Darken creases/contacts using the per-pixel world position + normal G-buffer:
/// sample a hemisphere around each pixel and count how many samples are occluded
/// by nearer geometry. Adds soft contact shadows / depth that flat lighting can't.
void ApplySSAO(Raster& r, const Mat4& vp, const Vec3& eye);

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
void ApplyBloom(Raster& r);

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
void ApplyToneMap(Raster& r);

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

void ApplyColorGrade(Raster& r);

// ---- The scene's Post-processing section, on the CPU -------------------------
/// What the software renderer takes from Scene::RenderSettings (the part that is cheap on a CPU):
/// exposure, the tonemap curve (None / ACES / Filmic, and the scene's Tonemap switch), white balance,
/// saturation, contrast, lift / gamma / gain, the vignette (with its smoothness), bloom and FXAA.
/// Same maths as the OpenGL "uber" pass (kUberFrag), but on 8-bit colour: the software renderer shades
/// in display space and has no HDR buffer, so the curves act on 0..1 values (the process-wide switches
/// behave exactly as before). The scene's ambient occlusion (ApplySSAO with the scene's radius and
/// intensity) and depth of field (ApplySceneDof) are separate passes in RenderMeshes.
struct ScenePostPlan {
    int   tonemap = 1;           // 0 none, 1 ACES, 2 Filmic
    float exposure = 1.0f;
    bool  grade = false;
    float temperature = 0, tint = 0, saturation = 1, contrast = 1;
    Vec3  lift{0, 0, 0}, gamma{0, 0, 0}, gain{0, 0, 0};
    float vignette = 0, vignetteSmooth = 0.5f;
    bool  any() const { return tonemap != 0 || exposure != 1.0f || grade || vignette > 0.0f; }
};
/// Fold the scene's settings and the process-wide switches into one plan. The process-wide tonemap /
/// exposure switch still turns the curve off, as before; the scene can turn it off too.
inline ScenePostPlan PlanScenePost(const Scene::RenderSettings& rs) {
    ScenePostPlan p;
    const auto& ps = rs.post;
    p.tonemap = (ToneMapEnabled() && rs.tonemap) ? ps.tonemap : 0;
    p.exposure = ExposureScale(rs) * (ToneMapEnabled() ? Exposure() : 1.0f);
    p.grade = ps.grading;
    p.temperature = ps.temperature; p.tint = ps.tint; p.saturation = ps.saturation; p.contrast = ps.contrast;
    p.lift = ps.lift; p.gamma = ps.gamma; p.gain = ps.gain;
    { const float vg = rs.vignette + rs.vignettePulse; p.vignette = vg > 1.0f ? 1.0f : (vg > 0.0f ? vg : 0.0f); }
    p.vignetteSmooth = ps.vignetteSmooth;
    return p;
}
/// Apply a plan to one 0..1 colour (exposure, curve, grade). Exposed so tests can check single values.
inline Vec3 ScenePostColor(const ScenePostPlan& p, Vec3 c) {
    c = c * p.exposure;
    if (p.tonemap != 0) c = TonemapColor(c, p.tonemap);
    else c = {Mathf::Clamp01(c.x), Mathf::Clamp01(c.y), Mathf::Clamp01(c.z)};
    if (p.grade) {
        c = {c.x * (1.0f + 0.25f * p.temperature), c.y * (1.0f + 0.2f * p.tint), c.z * (1.0f - 0.25f * p.temperature)};
        const float l = 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z;
        c = {l + (c.x - l) * p.saturation, l + (c.y - l) * p.saturation, l + (c.z - l) * p.saturation};
        c = {(c.x - 0.5f) * p.contrast + 0.5f, (c.y - 0.5f) * p.contrast + 0.5f, (c.z - 0.5f) * p.contrast + 0.5f};
        c = {c.x * (1.0f + p.gain.x) + p.lift.x * (1.0f - c.x),
             c.y * (1.0f + p.gain.y) + p.lift.y * (1.0f - c.y),
             c.z * (1.0f + p.gain.z) + p.lift.z * (1.0f - c.z)};
        auto g = [](float v, float gm) { return std::pow(v < 0 ? 0.0f : v, 1.0f / std::fmax(1.0f + gm * 0.75f, 0.1f)); };
        c = {g(c.x, p.gamma.x), g(c.y, p.gamma.y), g(c.z, p.gamma.z)};
    }
    return c;
}
/// The vignette darkening (0..0.8) at a pixel, the same formula as the GPU uber pass.
inline float ScenePostVignette(const ScenePostPlan& p, float u, float v, float aspect) {
    if (p.vignette <= 0.0f) return 0.0f;
    const float dx = (u - 0.5f) * aspect * 2.0f, dy = (v - 0.5f) * 2.0f;
    const float rr = std::sqrt(dx * dx + dy * dy) / std::sqrt(aspect * aspect + 1.0f);
    const float e0 = std::fmax(1.0f - p.vignetteSmooth * 1.3f, 0.0f), e1 = 1.05f;
    float t = (rr - e0) / (e1 - e0); t = t < 0 ? 0.0f : (t > 1 ? 1.0f : t);
    return 0.8f * p.vignette * t * t * (3.0f - 2.0f * t);
}
void ApplyScenePost(Raster& r, const ScenePostPlan& p);

// ---- Depth of field (the scene's Post-processing switch), on the CPU ---------
// The software version of the GPU pass (DofMath.hpp holds the shared focus maths): a half-
// resolution copy of the finished frame is blurred by two rounds of a 9-tap binomial filter
// (weights 1 8 28 56 70 56 28 8 1, / 256: row 8 of Pascal's triangle, a close match to a
// gaussian), then mixed back in by each pixel's circle of confusion. The distance is the one
// the GPU depth image holds (straight-line distance from the eye, / ssao::kFar, so anything past
// 150 units counts as sky). It comes from the W-buffer (depth = 1 / view depth) times the length
// of the pixel's ray per unit of view depth (DofRayScale), which is why it needs a perspective
// camera: with an orthographic one every pixel has the same W and the pass is skipped
// (DofPerspective). The blur works on premultiplied colour (colour times coverage), so a
// far object fades softly into the empty sky without a dark fringe, and a pixel nothing covered
// (alpha 0, where the host paints its sky) stays transparent.

/// True when `vp` is a perspective projection (clip w depends on the position), so the
/// software W-buffer holds a real distance.
inline bool DofPerspective(const Mat4& vp) {
    return std::fabs(vp.at(0, 3)) + std::fabs(vp.at(1, 3)) + std::fabs(vp.at(2, 3)) > 1e-6f;
}
/// World distance the depth of field uses for one W-buffer value (1 / view depth; 0 = nothing drawn).
/// `rayScale` is the eye distance per unit of view depth along the pixel's ray (1 at the centre).
inline float DofPixelDistance(float invW, float rayScale = 1.0f) {
    float dn = invW > 0.0f ? (rayScale / invW) / ssao::kFar : 1.0f;
    if (dn > 1.0f) dn = 1.0f;
    return dof::Distance(dn, ssao::kFar);
}
/// The ray through a point of the screen (NDC x, y, y up) as an affine function of x and y, for
/// DofRayScale: points at one NDC depth lie on one view-depth plane, so the ray between two such
/// planes is affine in x and y.
struct DofRays {
    Vec3 c, ax, ay;   // ray(x, y) = c + ax * x + ay * y
    Vec3 wrow;        // clip w = dot(wrow, P) + const
};
inline DofRays DofRaysFor(const Mat4& vp) {
    const Mat4 inv = vp.Inverse();
    auto ray = [&](float x, float y) { return inv.MultiplyPoint({x, y, 0.5f}) - inv.MultiplyPoint({x, y, -0.5f}); };
    DofRays d;
    d.c = ray(0.0f, 0.0f);
    d.ax = ray(1.0f, 0.0f) - d.c;
    d.ay = ray(0.0f, 1.0f) - d.c;
    d.wrow = {vp.at(0, 3), vp.at(1, 3), vp.at(2, 3)};
    return d;
}
/// Eye distance per unit of view depth along the ray at NDC (x, y): 1 / cos(angle off the view axis).
inline float DofRayScale(const DofRays& d, float x, float y) {
    const Vec3 r = d.c + d.ax * x + d.ay * y;
    const float len = r.Magnitude(), dw = std::fabs(Vec3::Dot(d.wrow, r));
    return dw > 1e-9f ? len / dw : 1.0f;
}
/// Blur the frame where it is out of focus. `focus` and `range` are post.dofFocus / post.dofRange;
/// `vp` is the view-projection the frame was drawn with (a perspective one, DofPerspective).
void ApplySceneDof(Raster& r, float focus, float range, const Mat4& vp);

// ---- Anti-aliasing (FXAA-lite) ---------------------------------------------
inline bool& FXAAEnabled() { static bool v = true; return v; }

// Active camera's layer culling mask (Camera.cullingMask). RenderMeshes draws a
// mesh only if bit (1<<gameObject->layer) is set. The editor/player set this from
// the rendering camera each frame (~0 = all layers, the Scene view default).
inline int& RenderCullingMask() { static int v = ~0; return v; }

/// Cheap post-process anti-aliasing: where local contrast is high (a geometry
/// edge), blend the pixel toward its neighbourhood average by an amount scaled by
/// the contrast. Smooths jaggies without a full supersample.
void ApplyFXAA(Raster& r);

/// Render all active MeshRenderers in `scene` into `r` with the given
/// view-projection matrix and camera position. Two-sided + flat-shaded via the
/// global SceneLight; depth-tested so overlapping meshes occlude correctly.
void RenderMeshes(Raster& r, const Scene& scene, const Mat4& vp, const Vec3& eye,
                         const GameObject* ignore = nullptr);

/// Supersampled render for smoother (anti-aliased) edges: draw the scene at `ss`x
/// resolution into `work`, then box-downsample to a w*h ABGR8888 image. Returns a
/// pointer to the w*h pixels to display (work.color when ss==1, else `out`).
/// Clears to transparent first; call ApplySceneLight() before this.
const std::uint32_t* RenderMeshesSS(Raster& work, std::vector<std::uint32_t>& out,
                                           const Scene& scene, const Mat4& vp, const Vec3& eye,
                                           int w, int h, int ss, const GameObject* ignore = nullptr);

/// A second software pass that only exists to publish SceneOcclusionDepth.
/// Restores the cull counters from the real frame so a stats read still
/// describes the picture, not this cheaper depth pass.
void FillOcclusionDepth(Raster& work, std::vector<std::uint32_t>& out,
                               const Scene& scene, const Mat4& vp, const Vec3& eye,
                               int w, int h, const GameObject* ignore = nullptr);

/// While alive, the software renderer makes a cheaper picture: the process-wide passes that cost the most and add little to a
/// small or never-shown picture are off (the sun and local shadow maps, three 2048 x 2048 cascades by default; ambient
/// occlusion; bloom; FXAA; with `depthOnly` also the tone map and colour grade). When it ends the switches are put back, and so
/// is what the Scene view reads after its own render: the cull counters, the vignette flag and, with `keepOcclusionDepth`, the
/// occlusion depth its particles are tested against. Used by FillOcclusionDepth (depth only) and the editor's asset thumbnails
/// (editor/main.cpp RenderFramedImage). Not thread-safe: the switches are process-wide, like the renderer.
class ReducedRenderScope {
public:
    explicit ReducedRenderScope(bool depthOnly, bool keepOcclusionDepth);
    ~ReducedRenderScope();
    ReducedRenderScope(const ReducedRenderScope&) = delete;
    ReducedRenderScope& operator=(const ReducedRenderScope&) = delete;

private:
    bool m_shadows, m_fxaa, m_ssao, m_bloom, m_tone, m_grade, m_vignette, m_keepDepth;
    CullCounters m_cull;
    OcclusionDepth m_depth;
};

} // namespace okay
