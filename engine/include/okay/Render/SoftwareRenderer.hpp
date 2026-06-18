#pragma once
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Vec4.hpp"
#include "okay/Math/Mat4.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Render/Lighting.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
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
};

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
        for (std::size_t i = 0; i + 2 < t.size(); i += 3) {
            Vec3 wp[3];
            for (int k = 0; k < 3; ++k) wp[k] = model.MultiplyPoint(v[t[i + k]]);
            Vec3 normal = Vec3::Cross(wp[1] - wp[0], wp[2] - wp[0]).Normalized();
            Vec3 centroid = (wp[0] + wp[1] + wp[2]) * (1.0f / 3.0f);
            if (Vec3::Dot(normal, eye - centroid) < 0.0f) normal = normal * -1.0f; // two-sided
            float sx[3], sy[3], sd[3]; bool ok = true;
            for (int k = 0; k < 3; ++k) {
                Vec4 c = vp * Vec4{wp[k], 1.0f};
                if (c.w <= 0.05f) { ok = false; break; }       // behind camera
                sx[k] = (c.x / c.w * 0.5f + 0.5f) * W;
                sy[k] = (1.0f - (c.y / c.w * 0.5f + 0.5f)) * H;
                // Normalized-device depth (clip z/w). Unlike world distance, this
                // is affine in screen space, so per-pixel barycentric interpolation
                // gives the correct depth — fixes faces bleeding through at angles.
                sd[k] = c.z / c.w;
            }
            if (!ok) continue;
            std::uint32_t abgr = Raster::Pack(mr->color, SceneLight::Shade(normal));
            r.Triangle(sx[0], sy[0], sd[0], sx[1], sy[1], sd[1], sx[2], sy[2], sd[2], abgr);
        }
    }
}

} // namespace okay
