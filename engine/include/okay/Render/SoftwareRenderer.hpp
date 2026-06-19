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

    /// Textured triangle: perspective-correct UVs (pass u/w, v/w and 1/w per
    /// vertex). Samples `img` (wrapped), multiplies by `tint` and lighting
    /// `shade`, adds `spec` + emissive `(er,eg,eb)`. Depth-tested per pixel.
    void TriangleTex(const float* X, const float* Y, const float* D,
                     const float* IW, const float* U, const float* V,
                     const Image& img, const Color& tint,
                     float shade, float spec, float er, float eg, float eb) {
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
        int tw = img.Width(), th = img.Height();
        if (tw <= 0 || th <= 0) return;
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
                float iw = w0 * IW[0] + w1 * IW[1] + w2 * IW[2];
                if (iw == 0.0f) continue;
                float u = (w0 * U[0] + w1 * U[1] + w2 * U[2]) / iw;
                float v = (w0 * V[0] + w1 * V[1] + w2 * V[2]) / iw;
                int tx = (int)std::floor(u * tw) % tw; if (tx < 0) tx += tw;
                int ty = (int)std::floor((1.0f - v) * th) % th; if (ty < 0) ty += th;
                Color tc = img.GetPixel(tx, ty);
                depth[i] = d;
                color[i] = PackRGB(tc.r * tint.r * shade + spec + er,
                                   tc.g * tint.g * shade + spec + eg,
                                   tc.b * tint.b * shade + spec + eb);
            }
        }
    }
};

// Process-wide texture cache for the software renderer (path -> RGBA image).
// Loaded lazily; failed/empty paths cache an empty image so we don't retry.
inline Image* GetCachedTexture(const std::string& path) {
    static std::unordered_map<std::string, Image> cache;
    auto it = cache.find(path);
    if (it == cache.end()) {
        Image img;
        img.Load(path);   // leaves the image empty on failure
        it = cache.emplace(path, std::move(img)).first;
    }
    return it->second.Width() > 0 ? &it->second : nullptr;
}

/// Render all active MeshRenderers in `scene` into `r` with the given
/// view-projection matrix and camera position. Two-sided + flat-shaded via the
/// global SceneLight; depth-tested so overlapping meshes occlude correctly.
inline void RenderMeshes(Raster& r, const Scene& scene, const Mat4& vp, const Vec3& eye) {
    const float W = (float)r.width, H = (float)r.height;
    for (const auto& go : scene.Objects()) {
        auto* mr = go->GetComponent<MeshRenderer>();
        if (!mr || !go->active || mr->wireframe) continue;   // wireframe drawn as lines
        Mat4 model = go->transform->LocalToWorldMatrix();
        const auto& v = mr->mesh.vertices;
        const auto& t = mr->mesh.triangles;
        const bool hasUV = mr->mesh.uvs.size() == v.size();
        Image* tex = mr->texture.empty() ? nullptr : GetCachedTexture(mr->texture);
        for (std::size_t i = 0; i + 2 < t.size(); i += 3) {
            int idx[3] = {t[i], t[i + 1], t[i + 2]};
            Vec3 wp[3];
            for (int k = 0; k < 3; ++k) wp[k] = model.MultiplyPoint(v[idx[k]]);
            Vec3 normal = Vec3::Cross(wp[1] - wp[0], wp[2] - wp[0]).Normalized();
            Vec3 centroid = (wp[0] + wp[1] + wp[2]) * (1.0f / 3.0f);
            if (Vec3::Dot(normal, eye - centroid) < 0.0f) normal = normal * -1.0f; // two-sided
            float sx[3], sy[3], sd[3], iw[3]; bool ok = true;
            for (int k = 0; k < 3; ++k) {
                Vec4 c = vp * Vec4{wp[k], 1.0f};
                if (c.w <= 0.05f) { ok = false; break; }       // behind camera
                sx[k] = (c.x / c.w * 0.5f + 0.5f) * W;
                sy[k] = (1.0f - (c.y / c.w * 0.5f + 0.5f)) * H;
                // Normalized-device depth (clip z/w). Unlike world distance, this
                // is affine in screen space, so per-pixel barycentric interpolation
                // gives the correct depth — fixes faces bleeding through at angles.
                sd[k] = c.z / c.w;
                iw[k] = 1.0f / c.w;
            }
            if (!ok) continue;
            // Material: diffuse (Lambert, or flat when unlit) + Blinn-Phong
            // specular highlight + self-illuminating emissive.
            float shade = mr->unlit ? 1.0f : SceneLight::Shade(normal);
            float spec = 0.0f;
            if (!mr->unlit && mr->specular > 0.0f) {
                Vec3 toLight = SceneLight::Direction() * -1.0f;
                Vec3 toEye = (eye - centroid).Normalized();
                Vec3 h = (toLight.Normalized() + toEye).Normalized();
                float nh = Vec3::Dot(normal, h);
                if (nh > 0.0f) spec = std::pow(nh, mr->shininess) * mr->specular;
            }
            if (tex) {
                // UVs: use the mesh's own if present, else planar/box-project
                // from local position along the face's dominant axis (exact on
                // axis-aligned faces like cubes/planes).
                float u[3], vv[3];
                Vec3 ln = Vec3::Cross(v[idx[1]] - v[idx[0]], v[idx[2]] - v[idx[0]]);
                float ax = std::fabs(ln.x), ay = std::fabs(ln.y), az = std::fabs(ln.z);
                for (int k = 0; k < 3; ++k) {
                    float uu, vt;
                    if (hasUV) { uu = mr->mesh.uvs[idx[k]].x; vt = mr->mesh.uvs[idx[k]].y; }
                    else {
                        const Vec3& p = v[idx[k]];
                        if (ax >= ay && ax >= az) { uu = p.z + 0.5f; vt = p.y + 0.5f; }
                        else if (ay >= ax && ay >= az) { uu = p.x + 0.5f; vt = p.z + 0.5f; }
                        else { uu = p.x + 0.5f; vt = p.y + 0.5f; }
                    }
                    uu *= mr->tiling.x; vt *= mr->tiling.y;   // texture repeat
                    u[k] = uu * iw[k]; vv[k] = vt * iw[k];   // perspective-correct
                }
                r.TriangleTex(sx, sy, sd, iw, u, vv, *tex, mr->color, shade, spec,
                              mr->emissive.r, mr->emissive.g, mr->emissive.b);
            } else {
                std::uint32_t abgr = Raster::PackRGB(
                    mr->color.r * shade + spec + mr->emissive.r,
                    mr->color.g * shade + spec + mr->emissive.g,
                    mr->color.b * shade + spec + mr->emissive.b);
                r.Triangle(sx[0], sy[0], sd[0], sx[1], sy[1], sd[1], sx[2], sy[2], sd[2], abgr);
            }
        }
    }
}

} // namespace okay
