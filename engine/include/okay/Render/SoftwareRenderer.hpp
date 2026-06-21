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
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace okay {

/// A tiny software rasterizer with a per-pixel depth buffer, so overlapping 3D
/// triangles occlude correctly (unlike a painter's-algorithm sort). It fills an
/// ABGR8888 pixel buffer that both the player (SDL texture) and the editor
/// (ImGui image) display. Flat-shaded; this is the engine's reference 3D path.
class Raster {
public:
    int width = 0, height = 0;
    std::vector<std::uint32_t> color;   // ABGR8888 (matches SDL_PIXELFORMAT_ABGR8888)
    std::vector<float>         depth;    // smaller = nearer; cleared to +inf

    void Resize(int w, int h) {
        width = w < 1 ? 1 : w;
        height = h < 1 ? 1 : h;
        color.assign((std::size_t)width * height, 0u);
        depth.assign((std::size_t)width * height, std::numeric_limits<float>::infinity());
    }

    /// Clear color (ABGR) and reset the depth buffer to "far".
    void Clear(std::uint32_t abgr) {
        std::fill(color.begin(), color.end(), abgr);
        std::fill(depth.begin(), depth.end(), std::numeric_limits<float>::infinity());
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

    /// Rasterize a triangle given screen-space points (pixels) + per-vertex depth
    /// (camera distance; smaller = nearer), depth-tested per pixel.
    void Triangle(float x0, float y0, float d0,
                  float x1, float y1, float d1,
                  float x2, float y2, float d2, std::uint32_t abgr) {
        int minX = (int)std::floor(std::fmin(x0, std::fmin(x1, x2)));
        int maxX = (int)std::ceil (std::fmax(x0, std::fmax(x1, x2)));
        int minY = (int)std::floor(std::fmin(y0, std::fmin(y1, y2)));
        int maxY = (int)std::ceil (std::fmax(y0, std::fmax(y1, y2)));
        if (minX < 0) minX = 0;
        if (minY < 0) minY = 0;
        if (maxX >= width) maxX = width - 1;
        if (maxY >= height) maxY = height - 1;

        float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
        if (area == 0.0f) return;
        float inv = 1.0f / area;
        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                float px = x + 0.5f, py = y + 0.5f;
                float w0 = ((x1 - px) * (y2 - py) - (x2 - px) * (y1 - py)) * inv;
                float w1 = ((x2 - px) * (y0 - py) - (x0 - px) * (y2 - py)) * inv;
                float w2 = 1.0f - w0 - w1;
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;       // outside triangle
                float d = w0 * d0 + w1 * d1 + w2 * d2;
                std::size_t i = (std::size_t)y * width + x;
                if (d < depth[i]) { depth[i] = d; color[i] = abgr; }
            }
        }
    }

    /// Gouraud triangle: per-vertex light color (LR/LG/LB) interpolated across
    /// the face and multiplied by a constant per-face albedo `base`, plus a
    /// constant specular + emissive, with optional per-tri fog. This is what
    /// makes low-poly organic meshes look smooth without extra geometry.
    void TriangleSmooth(const float* X, const float* Y, const float* D,
                        const float* LR, const float* LG, const float* LB,
                        const Color& base, float spec, float er, float eg, float eb,
                        float fog, float fr, float fg, float fb) {
        int minX = (int)std::floor(std::fmin(X[0], std::fmin(X[1], X[2])));
        int maxX = (int)std::ceil (std::fmax(X[0], std::fmax(X[1], X[2])));
        int minY = (int)std::floor(std::fmin(Y[0], std::fmin(Y[1], Y[2])));
        int maxY = (int)std::ceil (std::fmax(Y[0], std::fmax(Y[1], Y[2])));
        if (minX < 0) minX = 0;
        if (minY < 0) minY = 0;
        if (maxX >= width) maxX = width - 1;
        if (maxY >= height) maxY = height - 1;
        float area = (X[1] - X[0]) * (Y[2] - Y[0]) - (X[2] - X[0]) * (Y[1] - Y[0]);
        if (area == 0.0f) return;
        float inv = 1.0f / area;
        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                float px = x + 0.5f, py = y + 0.5f;
                float w0 = ((X[1] - px) * (Y[2] - py) - (X[2] - px) * (Y[1] - py)) * inv;
                float w1 = ((X[2] - px) * (Y[0] - py) - (X[0] - px) * (Y[2] - py)) * inv;
                float w2 = 1.0f - w0 - w1;
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                float d = w0 * D[0] + w1 * D[1] + w2 * D[2];
                std::size_t i = (std::size_t)y * width + x;
                if (d >= depth[i]) continue;
                float lr = w0 * LR[0] + w1 * LR[1] + w2 * LR[2];
                float lg = w0 * LG[0] + w1 * LG[1] + w2 * LG[2];
                float lb = w0 * LB[0] + w1 * LB[1] + w2 * LB[2];
                float cr = base.r * lr + spec + er;
                float cg = base.g * lg + spec + eg;
                float cb = base.b * lb + spec + eb;
                if (fog > 0.0f) {
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
                     const float* LR, const float* LG, const float* LB, float spec,
                     float er, float eg, float eb,
                     float fog, float fr, float fg, float fb) {
        int minX = (int)std::floor(std::fmin(X[0], std::fmin(X[1], X[2])));
        int maxX = (int)std::ceil (std::fmax(X[0], std::fmax(X[1], X[2])));
        int minY = (int)std::floor(std::fmin(Y[0], std::fmin(Y[1], Y[2])));
        int maxY = (int)std::ceil (std::fmax(Y[0], std::fmax(Y[1], Y[2])));
        if (minX < 0) minX = 0;
        if (minY < 0) minY = 0;
        if (maxX >= width) maxX = width - 1;
        if (maxY >= height) maxY = height - 1;
        float area = (X[1] - X[0]) * (Y[2] - Y[0]) - (X[2] - X[0]) * (Y[1] - Y[0]);
        if (area == 0.0f || mips.empty()) return;
        float inv = 1.0f / area;
        int tw = mips[0].Width(), th = mips[0].Height();
        if (tw <= 0 || th <= 0) return;
        LodGrad lg2 = MakeLodGrad(X, Y, inv, U, V, IW);
        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                float px = x + 0.5f, py = y + 0.5f;
                float w0 = ((X[1] - px) * (Y[2] - py) - (X[2] - px) * (Y[1] - py)) * inv;
                float w1 = ((X[2] - px) * (Y[0] - py) - (X[0] - px) * (Y[2] - py)) * inv;
                float w2 = 1.0f - w0 - w1;
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                float d = w0 * D[0] + w1 * D[1] + w2 * D[2];
                std::size_t i = (std::size_t)y * width + x;
                if (d >= depth[i]) continue;
                float A = w0 * U[0] + w1 * U[1] + w2 * U[2];
                float Av = w0 * V[0] + w1 * V[1] + w2 * V[2];
                float iw = w0 * IW[0] + w1 * IW[1] + w2 * IW[2];
                if (iw == 0.0f) continue;
                float u = A / iw, v = Av / iw;
                Color tc = SampleMips(mips, u, v, PixelLod(A, Av, iw, lg2, tw, th));
                float lr = w0 * LR[0] + w1 * LR[1] + w2 * LR[2];
                float lg = w0 * LG[0] + w1 * LG[1] + w2 * LG[2];
                float lb = w0 * LB[0] + w1 * LB[1] + w2 * LB[2];
                float cr = tc.r * tint.r * lr + spec + er;
                float cg = tc.g * tint.g * lg + spec + eg;
                float cb = tc.b * tint.b * lb + spec + eb;
                if (fog > 0.0f) {
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
                       float fog, float fr, float fg, float fb) {
        int minX = (int)std::floor(std::fmin(X[0], std::fmin(X[1], X[2])));
        int maxX = (int)std::ceil (std::fmax(X[0], std::fmax(X[1], X[2])));
        int minY = (int)std::floor(std::fmin(Y[0], std::fmin(Y[1], Y[2])));
        int maxY = (int)std::ceil (std::fmax(Y[0], std::fmax(Y[1], Y[2])));
        if (minX < 0) minX = 0;
        if (minY < 0) minY = 0;
        if (maxX >= width) maxX = width - 1;
        if (maxY >= height) maxY = height - 1;
        float area = (X[1] - X[0]) * (Y[2] - Y[0]) - (X[2] - X[0]) * (Y[1] - Y[0]);
        if (area == 0.0f) return;
        float inv = 1.0f / area;
        const bool textured = mips && !mips->empty() && (*mips)[0].Width() > 0;
        int tw = textured ? (*mips)[0].Width() : 0, th = textured ? (*mips)[0].Height() : 0;
        LodGrad lg2 = textured ? MakeLodGrad(X, Y, inv, U, V, IW) : LodGrad{};
        Vec3 toLight = SceneLight::Direction() * -1.0f;
        toLight = toLight.Normalized();
        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                float px = x + 0.5f, py = y + 0.5f;
                float w0 = ((X[1] - px) * (Y[2] - py) - (X[2] - px) * (Y[1] - py)) * inv;
                float w1 = ((X[2] - px) * (Y[0] - py) - (X[0] - px) * (Y[2] - py)) * inv;
                float w2 = 1.0f - w0 - w1;
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                float d = w0 * D[0] + w1 * D[1] + w2 * D[2];
                std::size_t i = (std::size_t)y * width + x;
                if (d >= depth[i]) continue;
                // Interpolated world normal + position (affine; renormalized).
                Vec3 n{w0 * NXa[0] + w1 * NXa[1] + w2 * NXa[2],
                       w0 * NYa[0] + w1 * NYa[1] + w2 * NYa[2],
                       w0 * NZa[0] + w1 * NZa[1] + w2 * NZa[2]};
                n = n.Normalized();
                Vec3 wpos{w0 * WXa[0] + w1 * WXa[1] + w2 * WXa[2],
                          w0 * WYa[0] + w1 * WYa[1] + w2 * WYa[2],
                          w0 * WZa[0] + w1 * WZa[1] + w2 * WZa[2]};
                Vec3 lit = SceneLights::ShadeColor(wpos, n);
                float spec = 0.0f;
                if (specularK > 0.0f) {
                    Vec3 toEye = (eye - wpos).Normalized();
                    Vec3 h = (toLight + toEye).Normalized();
                    float nh = Vec3::Dot(n, h);
                    if (nh > 0.0f) spec = std::pow(nh, shininess) * specularK;
                }
                float br = base.r, bg = base.g, bb2 = base.b;
                if (textured) {
                    float A = w0 * U[0] + w1 * U[1] + w2 * U[2];
                    float Av = w0 * V[0] + w1 * V[1] + w2 * V[2];
                    float iw = w0 * IW[0] + w1 * IW[1] + w2 * IW[2];
                    if (iw != 0.0f) {
                        Color tc = SampleMips(*mips, A / iw, Av / iw, PixelLod(A, Av, iw, lg2, tw, th));
                        br = tc.r * tint.r; bg = tc.g * tint.g; bb2 = tc.b * tint.b;
                    }
                }
                float cr = br * lit.x + spec + er;
                float cg = bg * lit.y + spec + eg;
                float cb = bb2 * lit.z + spec + eb;
                if (fog > 0.0f) {
                    cr = cr * (1.0f - fog) + fr * fog;
                    cg = cg * (1.0f - fog) + fg * fog;
                    cb = cb * (1.0f - fog) + fb * fog;
                }
                depth[i] = d;
                color[i] = PackRGB(cr, cg, cb);
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
inline bool& PerPixelLighting() { static bool v = true; return v; }

/// Render all active MeshRenderers in `scene` into `r` with the given
/// view-projection matrix and camera position. Two-sided + flat-shaded via the
/// global SceneLight; depth-tested so overlapping meshes occlude correctly.
inline void RenderMeshes(Raster& r, const Scene& scene, const Mat4& vp, const Vec3& eye) {
    const float W = (float)r.width, H = (float)r.height;
    const auto& rs = scene.renderSettings;
    const bool  fogOn = rs.fog && rs.fogEnd > rs.fogStart;
    const float fogR = rs.fogColor.r, fogG = rs.fogColor.g, fogB = rs.fogColor.b;
    for (const auto& go : scene.Objects()) {
        auto* mr = go->GetComponent<MeshRenderer>();
        if (!mr || !go->active || !mr->enabled || mr->wireframe) continue;   // wireframe drawn as lines
        Mat4 model = go->transform->LocalToWorldMatrix();
        const auto& v = mr->mesh.vertices;
        const auto& t = mr->mesh.triangles;
        const bool hasUV = mr->mesh.uvs.size() == v.size();
        const bool faceCols = mr->mesh.HasFaceColors();   // per-triangle colors?
        const std::vector<Image>* tex = mr->texture.empty() ? nullptr : GetCachedMips(mr->texture);
        Image* mcap = (mr->matcap.empty() || mr->unlit) ? nullptr : GetCachedTexture(mr->matcap);
        // Matcap shading needs per-vertex normals; fall back if the mesh has none.
        const bool useMatcap = mcap && mr->mesh.HasNormals();
        // Smooth (Gouraud) shading when the mesh carries per-vertex normals and
        // isn't textured/unlit — interpolated lighting across each face. Matcap
        // also rides the smooth path (it interpolates a per-vertex sampled color).
        const bool smooth = useMatcap || (!tex && mr->mesh.HasNormals());
        const Mat4 nrm = model;   // linear part transforms normals (rigid/uniform)

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
        // A clip-space vertex carrying its (unprojected) UV, for near-plane
        // clipping. Clipping in homogeneous space before the /w divide is what
        // prevents triangles from exploding/vanishing when you zoom in close.
        struct CV { float x, y, z, w, u, v, lr, lg, lb, nx, ny, nz, wx, wy, wz; };
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
                // Per-vertex world normal (smoothed if the mesh has normals, else
                // the face normal), flipped to match a culled back face.
                Vec3 nk = mr->mesh.HasNormals()
                              ? nrm.MultiplyVector(mr->mesh.normals[idx[k]]).Normalized()
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
                } else if (!mr->unlit && mr->mesh.HasNormals()) {
                    lk = SceneLights::ShadeColor(wp[k], nk);
                }
                in[k] = {c.x, c.y, c.z, c.w, uu * mr->tiling.x, vt * mr->tiling.y,
                         lk.x, lk.y, lk.z, nk.x, nk.y, nk.z, wp[k].x, wp[k].y, wp[k].z};
            }

            // Sutherland-Hodgman clip against the near plane (w > NEAR).
            const float NEAR = 1e-4f;
            CV poly[8]; int pn = 0;
            for (int e = 0; e < 3; ++e) {
                const CV& A = in[e];
                const CV& B = in[(e + 1) % 3];
                bool inA = A.w > NEAR, inB = B.w > NEAR;
                if (inA) poly[pn++] = A;
                if (inA != inB) {
                    float tt = (NEAR - A.w) / (B.w - A.w);
                    poly[pn++] = {A.x + (B.x - A.x) * tt, A.y + (B.y - A.y) * tt,
                                  A.z + (B.z - A.z) * tt, A.w + (B.w - A.w) * tt,
                                  A.u + (B.u - A.u) * tt, A.v + (B.v - A.v) * tt,
                                  A.lr + (B.lr - A.lr) * tt, A.lg + (B.lg - A.lg) * tt,
                                  A.lb + (B.lb - A.lb) * tt,
                                  A.nx + (B.nx - A.nx) * tt, A.ny + (B.ny - A.ny) * tt,
                                  A.nz + (B.nz - A.nz) * tt, A.wx + (B.wx - A.wx) * tt,
                                  A.wy + (B.wy - A.wy) * tt, A.wz + (B.wz - A.wz) * tt};
                }
            }
            if (pn < 3) continue;   // entirely behind the camera

            // Material: diffuse (Lambert, or flat when unlit) + Blinn-Phong
            // specular highlight + self-illuminating emissive.
            // Colored multi-light diffuse (directional + point + spot + ambient).
            Vec3 lit = mr->unlit ? Vec3{1.0f, 1.0f, 1.0f}
                                 : SceneLights::ShadeColor(centroid, normal);
            float spec = 0.0f;
            if (!mr->unlit && mr->specular > 0.0f) {
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
                sd = c.z * iw;
                uo = c.u * iw; vo = c.v * iw;
            };
            // Per-pixel (Phong) shading for lit meshes that carry normals — smooth
            // surfaces + correct specular, textured or not. Matcap/unlit/no-normal
            // meshes keep their existing paths.
            const bool perPixel = PerPixelLighting() && !mr->unlit && !useMatcap && mr->mesh.HasNormals();
            for (int j = 1; j + 1 < pn; ++j) {
                const CV* tri[3] = {&poly[0], &poly[j], &poly[j + 1]};
                float sx[3], sy[3], sd[3], iw[3], uu[3], vv[3];
                for (int k = 0; k < 3; ++k)
                    project(*tri[k], sx[k], sy[k], sd[k], iw[k], uu[k], vv[k]);
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
                                    fogF, fogR, fogG, fogB);
                } else if (tex) {
                    float lr[3] = {tri[0]->lr, tri[1]->lr, tri[2]->lr};
                    float lg[3] = {tri[0]->lg, tri[1]->lg, tri[2]->lg};
                    float lb[3] = {tri[0]->lb, tri[1]->lb, tri[2]->lb};
                    r.TriangleTex(sx, sy, sd, iw, uu, vv, *tex, mr->color, lr, lg, lb, spec,
                                  mr->emissive.r, mr->emissive.g, mr->emissive.b,
                                  fogF, fogR, fogG, fogB);
                } else if (smooth) {
                    float lr[3] = {tri[0]->lr, tri[1]->lr, tri[2]->lr};
                    float lg[3] = {tri[0]->lg, tri[1]->lg, tri[2]->lg};
                    float lb[3] = {tri[0]->lb, tri[1]->lb, tri[2]->lb};
                    r.TriangleSmooth(sx, sy, sd, lr, lg, lb, base, spec,
                                     mr->emissive.r, mr->emissive.g, mr->emissive.b,
                                     fogF, fogR, fogG, fogB);
                } else
                    r.Triangle(sx[0], sy[0], sd[0], sx[1], sy[1], sd[1],
                               sx[2], sy[2], sd[2], abgr);
            }
        }
    }
}

/// Supersampled render for smoother (anti-aliased) edges: draw the scene at `ss`x
/// resolution into `work`, then box-downsample to a w*h ABGR8888 image. Returns a
/// pointer to the w*h pixels to display (work.color when ss==1, else `out`).
/// Clears to transparent first; call ApplySceneLight() before this.
inline const std::uint32_t* RenderMeshesSS(Raster& work, std::vector<std::uint32_t>& out,
                                           const Scene& scene, const Mat4& vp, const Vec3& eye,
                                           int w, int h, int ss) {
    if (w < 1) w = 1; if (h < 1) h = 1; if (ss < 1) ss = 1;
    while (ss > 1 && ((long)w * ss > 4096 || (long)h * ss > 4096)) --ss;
    const int iw = w * ss, ih = h * ss;
    work.Resize(iw, ih);
    work.Clear(0u);
    RenderMeshes(work, scene, vp, eye);
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
