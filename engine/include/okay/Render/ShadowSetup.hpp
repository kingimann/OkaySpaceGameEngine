#pragma once
// ---------------------------------------------------------------------------
// Shadow planning shared by the GPU backends (OpenGL + Direct3D 11).
//
// Each frame the renderer asks PlanShadows() what to draw into its two shadow
// atlases, then renders one depth pass per tile and samples them in the main
// shader:
//
//   * Sun atlas: the main directional light (SceneLights::SunIndex) gets up to
//     ShadowMap::kMaxCascades orthographic cascades laid out side by side
//     (atlas = count*S x S). Cascade spheres grow geometrically out to
//     ShadowDistance(), exactly like the software renderer's RenderShadowMap, so
//     close shadows stay crisp in big levels. ShadowDistance() <= 0 keeps the
//     legacy single map fit to the whole scene.
//
//   * Local atlas: a kLocalGrid x kLocalGrid grid of square tiles for point,
//     spot and area lights that have castShadows on. A spot light takes one
//     perspective tile; a point/area light takes six (one per cube face, chosen in
//     the shader by the dominant axis from the light). Lights are picked nearest
//     to the camera first until the tiles run out.
//
// Shadow bias is applied in WORLD space in the shaders (the receiver is pushed
// along its normal and toward the light by a few shadow texels), so it is the same
// for ortho and perspective maps and doesn't depend on the depth range.
// ---------------------------------------------------------------------------
#include "okay/Render/SoftwareRenderer.hpp"   // ShadowDistance/ShadowCascades/... globals
#include "okay/Render/Culling.hpp"
#include "okay/Render/Lighting.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Math/Mat4.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace okay {

struct ShadowCascadeGpu {
    Mat4  vp;                 // light view-projection (clip space)
    Vec3  center{0, 0, 0};    // world sphere this cascade covers
    float radius = 0.0f;
    float texelWorld = 0.0f;  // world size of one shadow texel
};

struct LocalShadowGpu {
    int   light = -1;         // index into SceneLights::List()
    int   firstTile = 0;      // first tile in the local atlas
    int   tiles = 0;          // 1 (spot) or 6 (point/area cube faces)
    Mat4  vp[6];              // per-tile view-projection
    float texelAt1 = 0.0f;    // world size of one texel at distance 1 from the light
    float nearZ = 0.05f, farZ = 10.0f;
};

struct ShadowPlan {
    static constexpr int kMaxCascades = 4;
    static constexpr int kLocalGrid = 4;                 // 4x4 tiles
    static constexpr int kMaxLocalTiles = kLocalGrid * kLocalGrid;
    // Point-light cube faces are rendered slightly wider than 90 degrees so the
    // soft-shadow filter never reads past a face edge.
    static constexpr float kCubeTanHalf = 1.12f;

    int sunLight = -1;                  // light index of the shadowed sun (-1 = none)
    int cascades = 0;
    int cascadeSize = 0;                // texels per cascade tile (square)
    ShadowCascadeGpu cascade[kMaxCascades];
    std::vector<LocalShadowGpu> local;
    int tilesUsed = 0;
    int tileSize = 0;                   // texels per local tile (square)
};

namespace detail {
/// World-space bounds of every shadow-relevant mesh. False when there are none.
inline bool ShadowSceneBounds(const Scene& scene, Vec3& lo, Vec3& hi) {
    lo = {1e30f, 1e30f, 1e30f}; hi = {-1e30f, -1e30f, -1e30f};
    bool any = false;
    for (const auto& go : scene.Objects()) {
        if (!go || !go->active) continue;
        auto* mr = go->template GetComponent<MeshRenderer>();
        if (!mr || !mr->enabled || mr->wireframe || mr->mesh.vertices.empty()) continue;
        Mat4 model = go->transform->LocalToWorldMatrix();
        Vec3 blo, bhi; mr->mesh.Bounds(blo, bhi);
        for (int c = 0; c < 8; ++c) {
            Vec3 w = model.MultiplyPoint({(c & 1) ? bhi.x : blo.x, (c & 2) ? bhi.y : blo.y, (c & 4) ? bhi.z : blo.z});
            lo.x = std::fmin(lo.x, w.x); lo.y = std::fmin(lo.y, w.y); lo.z = std::fmin(lo.z, w.z);
            hi.x = std::fmax(hi.x, w.x); hi.y = std::fmax(hi.y, w.y); hi.z = std::fmax(hi.z, w.z);
            any = true;
        }
    }
    return any;
}

/// Orthographic light view-projection around a world sphere, with the centre
/// snapped to the texel grid IN LIGHT SPACE so shadows don't shimmer when the
/// camera moves (same fit as ComputeDirectionalShadowVP).
inline void FitCascade(ShadowCascadeGpu& c, const Vec3& center, float R, const Vec3& L,
                       float depthPad, int S) {
    float texel = (2.0f * R) / (float)S;
    Vec3 up = std::fabs(L.y) > 0.99f ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
    float back = R * 2.0f + depthPad;
    Mat4 view = Mat4::LookAt(center - L * back, center, up);
    Mat4 proj = Mat4::Ortho(-R, R, -R, R, 0.05f, back + R * 2.0f + depthPad);
    Vec4 lc = view * Vec4{center, 1.0f};
    float sx = std::floor(lc.x / texel) * texel;
    float sy = std::floor(lc.y / texel) * texel;
    c.vp = proj * Mat4::Translate({sx - lc.x, sy - lc.y, 0.0f}) * view;
    c.center = center;
    c.radius = R;
    c.texelWorld = texel;
}
} // namespace detail

/// Cube-face basis for point-light shadows: face f looks along kCubeDir[f]. The
/// shaders pick the face with the same dominant-axis rule as CubeFace().
inline Vec3 CubeFaceDir(int f) {
    static const Vec3 d[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    return d[f < 0 ? 0 : (f > 5 ? 5 : f)];
}
inline int CubeFace(const Vec3& v) {
    float ax = std::fabs(v.x), ay = std::fabs(v.y), az = std::fabs(v.z);
    if (ax >= ay && ax >= az) return v.x > 0.0f ? 0 : 1;
    if (ay >= az) return v.y > 0.0f ? 2 : 3;
    return v.z > 0.0f ? 4 : 5;
}

/// Sun cascade + local light tile assignment for this frame. `camVp`/`eye` are the
/// camera's; `maxAtlasWidth` caps count*S (the GPU's max texture size).
inline void PlanShadows(const Scene& scene, const Mat4& camVp, const Vec3& eye,
                        ShadowPlan& plan, int maxAtlasWidth = 8192) {
    plan = ShadowPlan{};
    if (!ShadowsEnabled()) return;
    Vec3 lo, hi;
    if (!detail::ShadowSceneBounds(scene, lo, hi)) return;
    const auto& lights = SceneLights::List();

    // ---- Sun cascades -------------------------------------------------------
    int sun = SceneLights::SunIndex();
    bool sunCasts = sun >= 0 && sun < (int)lights.size() && lights[sun].type == 0 &&
                    lights[sun].castShadows && lights[sun].shadowStrength > 0.0f;
    if (lights.empty()) sunCasts = true;   // no gathered lights: the global SceneLight sun
    if (sunCasts) {
        Vec3 L = lights.empty() ? SceneLight::Direction() : lights[sun].dir;
        { float m = L.Magnitude(); L = m > 1e-6f ? L * (1.0f / m) : Vec3{0, -1, 0}; }
        int n = ShadowCascades(); n = std::clamp(n, 1, ShadowPlan::kMaxCascades);
        int S = std::clamp(ShadowMapResolution(), 256, 4096);
        float dist = ShadowDistance();
        if (dist <= 0.0f) n = 1;
        while (n * S > maxAtlasWidth && S > 256) S /= 2;
        float depthPad = (hi.y - lo.y) + (hi.x - lo.x) + (hi.z - lo.z);
        Vec3 sceneCtr{(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f};
        float sceneR = 0.5f * (hi - lo).Magnitude() + 0.05f;
        if (dist <= 0.0f) {
            detail::FitCascade(plan.cascade[0], sceneCtr, sceneR, L, depthPad, S);
        } else {
            // Split the camera's view frustum by depth and fit each cascade's sphere
            // around its slice. The split range is clamped to where the scene
            // actually is along the view (so a camera far from a small scene still
            // gets its cascades on the scene) and to the shadow distance.
            Mat4 inv = camVp.Inverse();
            Vec3 nearC[4], ray[4];
            const float cx[4] = {-1, 1, 1, -1}, cy[4] = {-1, -1, 1, 1};
            for (int k = 0; k < 4; ++k) {
                nearC[k] = inv.MultiplyPoint({cx[k], cy[k], -1.0f});
                ray[k] = (inv.MultiplyPoint({cx[k], cy[k], 1.0f}) - nearC[k]).Normalized();
            }
            Vec3 fwd = (inv.MultiplyPoint({0, 0, 1}) - inv.MultiplyPoint({0, 0, -1})).Normalized();
            auto cornerAt = [&](int k, float d) {      // slice corner k at view depth d
                float dn = Vec3::Dot(nearC[k] - eye, fwd), dr = Vec3::Dot(ray[k], fwd);
                return nearC[k] + ray[k] * ((d - dn) / (dr > 1e-4f ? dr : 1e-4f));
            };
            float camNear = std::fmax(Vec3::Dot(nearC[0] - eye, fwd), 0.01f);
            float dmin = 1e30f, dmax = -1e30f;
            for (int c = 0; c < 8; ++c) {
                Vec3 w{(c & 1) ? hi.x : lo.x, (c & 2) ? hi.y : lo.y, (c & 4) ? hi.z : lo.z};
                float d = Vec3::Dot(w - eye, fwd);
                dmin = std::fmin(dmin, d); dmax = std::fmax(dmax, d);
            }
            float d0 = std::fmax(dmin, camNear), d1 = std::fmin(dmax, dist);
            if (d1 <= d0) n = 0;                       // the scene is behind the camera / beyond the shadow distance
            else if (d1 - d0 < 1.0f) d1 = d0 + 1.0f;
            // Practical split scheme: mostly logarithmic (dense near the camera),
            // blended with uniform so the far cascades aren't paper-thin.
            float split[ShadowPlan::kMaxCascades + 1];
            split[0] = d0;
            for (int i = 1; i <= n; ++i) {
                float t = (float)i / (float)n;
                float lg = d0 * std::pow(d1 / d0, t), un = d0 + (d1 - d0) * t;
                split[i] = 0.75f * lg + 0.25f * un;
            }
            for (int i = 0; i < n; ++i) {
                Vec3 pts[8];
                for (int k = 0; k < 4; ++k) { pts[k] = cornerAt(k, split[i]); pts[k + 4] = cornerAt(k, split[i + 1]); }
                Vec3 ctr{0, 0, 0};
                for (const Vec3& q : pts) ctr = ctr + q;
                ctr = ctr * (1.0f / 8.0f);
                float R = 0.0f;
                for (const Vec3& q : pts) R = std::fmax(R, (q - ctr).Magnitude());
                // A cascade never needs to be bigger than the whole scene, and its
                // radius is rounded up to ~9% steps so the texel size (and with it
                // the shadow edges) doesn't change every time the camera moves.
                if (R >= sceneR) { ctr = sceneCtr; R = sceneR; }
                if (R < 0.5f) R = 0.5f;
                R = std::pow(2.0f, std::ceil(std::log2(R) * 8.0f) / 8.0f);
                detail::FitCascade(plan.cascade[i], ctr, R, L, depthPad, S);
            }
        }
        plan.sunLight = lights.empty() ? -1 : sun;
        plan.cascades = n;
        plan.cascadeSize = S;
    }

    // ---- Local lights (spot = 1 tile, point/area = 6 cube tiles) -------------
    struct Cand { int index; float key; };
    std::vector<Cand> cands;
    const Frustum camFr = Frustum::FromVP(camVp);
    for (int i = 0; i < (int)lights.size() && i < 16; ++i) {
        const LightSample& s = lights[i];
        if (s.type == 0 || !s.castShadows || s.shadowStrength <= 0.0f || s.range <= 0.0f) continue;
        if (s.color.x + s.color.y + s.color.z <= 1e-4f) continue;
        if (!camFr.SphereVisible(s.pos, s.range)) continue;     // its light can't reach the view
        float d = (s.pos - eye).Magnitude() - s.range;          // nearest edge of its reach
        cands.push_back({i, d});
    }
    std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.key < b.key; });
    plan.tileSize = std::clamp(ShadowMapResolution() / 4, 256, 1024);
    for (const Cand& c : cands) {
        const LightSample& s = lights[c.index];
        int need = s.type == 2 ? 1 : 6;
        if (plan.tilesUsed + need > ShadowPlan::kMaxLocalTiles) continue;   // a spot may still fit
        LocalShadowGpu ls;
        ls.light = c.index;
        ls.firstTile = plan.tilesUsed;
        ls.tiles = need;
        ls.farZ = s.range;
        ls.nearZ = std::clamp(s.range * 0.01f, 0.02f, 0.2f);
        if (s.type == 2) {
            // Cone half-angle from cosOuter, plus a margin for the soft filter.
            float half = std::acos(std::clamp(s.cosOuter, -1.0f, 1.0f));
            float halfDeg = std::fmin(half * 57.2957795f + 4.0f, 85.0f);
            float tanHalf = std::tan(halfDeg * 0.0174532925f);
            Vec3 f = s.dir.Normalized();
            Vec3 up = std::fabs(f.y) > 0.99f ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
            ls.vp[0] = Mat4::Perspective(halfDeg * 2.0f, 1.0f, ls.nearZ, ls.farZ) *
                       Mat4::LookAt(s.pos, s.pos + f, up);
            ls.texelAt1 = 2.0f * tanHalf / (float)plan.tileSize;
        } else {
            const float fov = 2.0f * std::atan(ShadowPlan::kCubeTanHalf) * 57.2957795f;
            Mat4 proj = Mat4::Perspective(fov, 1.0f, ls.nearZ, ls.farZ);
            for (int f = 0; f < 6; ++f) {
                Vec3 d = CubeFaceDir(f);
                Vec3 up = (f == 2 || f == 3) ? Vec3{0, 0, 1} : Vec3{0, 1, 0};
                ls.vp[f] = proj * Mat4::LookAt(s.pos, s.pos + d, up);
            }
            ls.texelAt1 = 2.0f * ShadowPlan::kCubeTanHalf / (float)plan.tileSize;
        }
        plan.tilesUsed += need;
        plan.local.push_back(ls);
    }
}

/// Atlas origin (0..1) of local tile `t` in the kLocalGrid x kLocalGrid atlas.
inline void LocalTileOrigin(int t, float& u, float& v) {
    u = (float)(t % ShadowPlan::kLocalGrid) / (float)ShadowPlan::kLocalGrid;
    v = (float)(t / ShadowPlan::kLocalGrid) / (float)ShadowPlan::kLocalGrid;
}

/// Is the caster with world sphere (c, r) able to shadow anything lit by `s`?
inline bool CasterInLightRange(const LightSample& s, const Vec3& c, float r) {
    return (c - s.pos).SqrMagnitude() <= (s.range + r) * (s.range + r);
}

} // namespace okay
