// Software renderer shadow maps.
// Bodies of okay:: free functions moved out of engine/include/okay/Render/SoftwareRenderer.hpp (modularization plan M2);
// the declarations (with their default arguments) stay in the header. The code is unchanged.
#include "okay/Render/SoftwareRenderer.hpp"

namespace okay {

namespace detail {
void RenderCascadeDepth(const Scene& scene, ShadowCascade& cas, bool cull) {
    const int S = cas.size;
    auto visible = [](const GameObject& go) {
        // The raised-arm copy is for one camera. It never casts, even into a
        // shadow map another view would sample.
        if (go.firstPersonOnly) return static_cast<MeshRenderer*>(nullptr);
        auto* mr = go.GetComponent<MeshRenderer>();
        return mr && go.ActiveInHierarchy() && mr->enabled && !mr->wireframe && mr->castShadows ? mr : nullptr;
    };
    const float cr = cas.radius * 1.05f, cr2 = cr * cr;
    for (const auto& go : scene.Objects()) {
        auto* mr = visible(*go); if (!mr) continue;
        Mat4 model = go->transform->LocalToWorldMatrix();
        if (cull) {
            // Sphere(cascade) vs the caster's world AABB: clamp the centre into the
            // box and test the squared distance — skip casters wholly outside.
            Vec3 blo, bhi; mr->DisplayMesh().Bounds(blo, bhi);
            Vec3 wlo{1e30f, 1e30f, 1e30f}, whi{-1e30f, -1e30f, -1e30f};
            for (int c = 0; c < 8; ++c) {
                Vec3 w = model.MultiplyPoint({(c & 1) ? bhi.x : blo.x, (c & 2) ? bhi.y : blo.y, (c & 4) ? bhi.z : blo.z});
                wlo.x = std::fmin(wlo.x, w.x); wlo.y = std::fmin(wlo.y, w.y); wlo.z = std::fmin(wlo.z, w.z);
                whi.x = std::fmax(whi.x, w.x); whi.y = std::fmax(whi.y, w.y); whi.z = std::fmax(whi.z, w.z);
            }
            float qx = std::fmax(wlo.x, std::fmin(cas.center.x, whi.x)) - cas.center.x;
            float qy = std::fmax(wlo.y, std::fmin(cas.center.y, whi.y)) - cas.center.y;
            float qz = std::fmax(wlo.z, std::fmin(cas.center.z, whi.z)) - cas.center.z;
            if (qx * qx + qy * qy + qz * qz > cr2) continue;
        }
        Mat4 mvp = cas.viewProj * model;
        const Mesh* shMesh = visibility::Current().ShadowMesh(go.get(), mr);
        if (!shMesh) continue;
        const auto& V = shMesh->vertices; const auto& T = shMesh->triangles;
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
            RasterDepthTriangle(cas.depth.data(), S, sxv, syv, szv);   // (TriangleSpan.hpp: the same per texel test, only texels that can be covered)
        }
    }
}
} // namespace detail

namespace detail {
void BuildCascade(const Scene& scene, ShadowCascade& cas, const Vec3& center,
                         float radius, const Vec3& L, float depthPad, int S, bool cull) {
    cas.size = S;
    cas.radius = radius;
    float texel = (2.0f * radius) / (float)S;
    // (Classic lighting rounds the centre on the world axes instead, which still shimmers.)
    const Vec3 ctr = ClassicLighting() ? Vec3{std::floor(center.x / texel) * texel, std::floor(center.y / texel) * texel,
                                              std::floor(center.z / texel) * texel} : center;
    cas.center = ctr;
    cas.texelWorld = texel;
    Vec3 up = std::fabs(L.y) > 0.99f ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
    float back = radius * 2.0f + depthPad;
    Vec3 eye = ctr - L * back;
    const float farZ = back + radius * 2.0f + depthPad;
    const Mat4 view = Mat4::LookAt(eye, ctr, up);
    // Stable cascades: the texel grid is tied to the world (shadowmath::CascadeSnapShift), so the shadow edges do not
    // crawl when the camera moves or turns.
    cas.viewProj = Mat4::Ortho(-radius, radius, -radius, radius, 0.05f, farZ)
                 * Mat4::Translate(ClassicLighting() ? Vec3{0, 0, 0} : shadowmath::CascadeSnapShift(view, texel)) * view;
    cas.depthRange = farZ - 0.05f;
    { const Mat4 inv = view.Inverse(); const Vec3 o = inv.MultiplyPoint({0, 0, 0});
      cas.axisX = (inv.MultiplyPoint({1, 0, 0}) - o).Normalized(); cas.axisY = (inv.MultiplyPoint({0, 1, 0}) - o).Normalized(); }
    cas.depth.assign((std::size_t)S * S, 2.0f);
    RenderCascadeDepth(scene, cas, cull);
}
} // namespace detail

void RenderShadowMap(const Scene& scene, const Mat4& vp, const Vec3& eye) {
    ShadowMap& sm = Shadows();
    sm.count = 0;
    if (!ShadowsEnabled()) { sm.enabled = false; return; }
    if (const LightSample* sun = SceneLights::Sun())   // the sun's own Cast Shadows switch
        if (!sun->castShadows || sun->shadowStrength <= 0.0f) { sm.enabled = false; return; }

    // Whole-scene bounds: used for the legacy fit and to size the light's depth
    // range so tall casters above a cascade still register.
    Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    bool any = false;
    auto visible = [](const GameObject& go) {
        auto* mr = go.GetComponent<MeshRenderer>();
        return mr && go.ActiveInHierarchy() && mr->enabled && !mr->wireframe ? mr : nullptr;
    };
    for (const auto& go : scene.Objects()) {
        auto* mr = visible(*go); if (!mr) continue;
        Mat4 model = go->transform->LocalToWorldMatrix();
        Vec3 blo, bhi; mr->DisplayMesh().Bounds(blo, bhi);
        for (int c = 0; c < 8; ++c) {
            Vec3 w = model.MultiplyPoint({(c & 1) ? bhi.x : blo.x, (c & 2) ? bhi.y : blo.y, (c & 4) ? bhi.z : blo.z});
            lo.x = std::fmin(lo.x, w.x); lo.y = std::fmin(lo.y, w.y); lo.z = std::fmin(lo.z, w.z);
            hi.x = std::fmax(hi.x, w.x); hi.y = std::fmax(hi.y, w.y); hi.z = std::fmax(hi.z, w.z);
            any = true;
        }
    }
    if (!any) { sm.enabled = false; return; }

    Vec3 L = SceneLight::Direction(); { float m = L.Magnitude(); L = m > 1e-6f ? L * (1.0f / m) : Vec3{0, -1, 0}; }
    sm.eye = eye; sm.lightDir = L;
    sm.fadeDistance = ShadowDistance() > 0.0f ? ShadowDistance() : 0.0f;
    const int S = std::min(ShadowMapResolution(), std::max(64, SoftwareShadowCap()));
    // Depth padding = the scene's vertical extent, so a caster anywhere above a
    // cascade region is still inside the light frustum's depth range.
    float depthPad = (hi.y - lo.y) + (hi.x - lo.x) + (hi.z - lo.z);

    float dist = ShadowDistance();
    if (dist <= 0.0f) {
        // ---- Legacy: one cascade fit to the entire scene -------------------
        Vec3 ctr{(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f};
        float R = 0.5f * std::sqrt((hi.x - lo.x) * (hi.x - lo.x) + (hi.y - lo.y) * (hi.y - lo.y) +
                                   (hi.z - lo.z) * (hi.z - lo.z)) + 0.05f;
        detail::BuildCascade(scene, sm.cascade[0], ctr, R, L, depthPad, S, /*cull=*/false);
        sm.count = 1; sm.enabled = true;
        return;
    }

    // ---- Cascaded, camera-focused --------------------------------------------
    // Camera forward, recovered from the view-projection (works for any camera
    // source — editor fly-cam or the game's main camera).
    Mat4 invVp = vp.Inverse();
    Vec3 pNear = invVp.MultiplyPoint({0.0f, 0.0f, -1.0f});
    Vec3 pFar  = invVp.MultiplyPoint({0.0f, 0.0f,  1.0f});
    Vec3 fwd = pFar - pNear; { float m = fwd.Magnitude(); fwd = m > 1e-6f ? fwd * (1.0f / m) : Vec3{0, 0, -1}; }

    int n = ShadowCascades(); if (n < 1) n = 1; if (n > ShadowMap::kMaxCascades) n = ShadowMap::kMaxCascades;
    // Split the [near, far] range so each cascade's radius grows geometrically:
    // dense texels up close, broad coverage far away.
    const float nearR = std::fmax(dist * 0.06f, 1.0f);
    for (int i = 0; i < n; ++i) {
        float t = (float)(i + 1) / (float)n;
        float R = nearR * std::pow(dist / nearR, t);          // end radius of cascade i
        // Centre the cascade sphere in front of the camera so it covers the view.
        Vec3 ctr = eye + fwd * (R * 0.85f);
        detail::BuildCascade(scene, sm.cascade[i], ctr, R, L, depthPad, S, /*cull=*/true);
    }
    sm.count = n;
    sm.enabled = true;
}

bool ComputeDirectionalShadowVP(const Scene& scene, const Mat4& camVp, const Vec3& eye,
                                       Mat4& outVP, float& outTexelWorld) {
    if (const LightSample* sun = SceneLights::Sun())   // the sun's own Cast Shadows switch
        if (!sun->castShadows || sun->shadowStrength <= 0.0f) return false;
    Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    bool any = false;
    for (const auto& go : scene.Objects()) {
        if (!go) continue;
        auto* mr = go->template GetComponent<MeshRenderer>();
        if (!mr || !go->ActiveInHierarchy() || !mr->enabled || mr->wireframe) continue;
        Mat4 model = go->transform->LocalToWorldMatrix();
        Vec3 blo, bhi; mr->DisplayMesh().Bounds(blo, bhi);
        for (int c = 0; c < 8; ++c) {
            Vec3 w = model.MultiplyPoint({(c & 1) ? bhi.x : blo.x, (c & 2) ? bhi.y : blo.y, (c & 4) ? bhi.z : blo.z});
            lo.x = std::fmin(lo.x, w.x); lo.y = std::fmin(lo.y, w.y); lo.z = std::fmin(lo.z, w.z);
            hi.x = std::fmax(hi.x, w.x); hi.y = std::fmax(hi.y, w.y); hi.z = std::fmax(hi.z, w.z);
            any = true;
        }
    }
    if (!any) return false;

    Vec3 L = SceneLight::Direction(); { float m = L.Magnitude(); L = m > 1e-6f ? L * (1.0f / m) : Vec3{0, -1, 0}; }
    const int S = ShadowMapResolution();
    float depthPad = (hi.y - lo.y) + (hi.x - lo.x) + (hi.z - lo.z);

    Vec3 ctr; float R;
    float dist = ShadowDistance();
    Vec3 sceneCtr{(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f};
    float sceneR = 0.5f * std::sqrt((hi.x - lo.x) * (hi.x - lo.x) + (hi.y - lo.y) * (hi.y - lo.y) +
                                    (hi.z - lo.z) * (hi.z - lo.z)) + 0.05f;
    if (dist <= 0.0f) {
        ctr = sceneCtr; R = sceneR;
    } else {
        // Camera-focused: a sphere of radius ~dist sitting in front of the camera.
        Mat4 invVp = camVp.Inverse();
        Vec3 pNear = invVp.MultiplyPoint({0.0f, 0.0f, -1.0f});
        Vec3 pFar  = invVp.MultiplyPoint({0.0f, 0.0f,  1.0f});
        Vec3 fwd = pFar - pNear; { float m = fwd.Magnitude(); fwd = m > 1e-6f ? fwd * (1.0f / m) : Vec3{0, 0, -1}; }
        R = std::fmin(dist, sceneR);
        if (R < 1.0f) R = 1.0f;
        ctr = eye + fwd * (R * 0.85f);
    }

    float texel = (2.0f * R) / (float)S;
    Vec3 up = std::fabs(L.y) > 0.99f ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
    float back = R * 2.0f + depthPad;
    Vec3 leye = ctr - L * back;
    Mat4 lightView = Mat4::LookAt(leye, ctr, up);
    Mat4 lightProj = Mat4::Ortho(-R, R, -R, R, 0.05f, back + R * 2.0f + depthPad);
    // Texel-snap the shadow centre IN LIGHT SPACE, not world space. The shadow
    // map's texel grid is aligned to the light's view axes, so snapping the centre
    // to a world-axis grid (as before) let the grid drift under the shadow whenever
    // the light — or the camera, for a camera-focused cascade — moved or rotated,
    // making the shadows "swim"/shimmer/glitch. Rounding the centre's light-space
    // X/Y to the texel grid and folding the sub-texel remainder back in as a
    // post-view translation keeps the grid rock-steady frame to frame.
    Mat4 snap = Mat4::Translate(shadowmath::CascadeSnapShift(lightView, texel));   // grid tied to the world
    outVP = lightProj * snap * lightView;
    outTexelWorld = texel;
    return true;
}

namespace detail {
void RasterLocalDepth(std::vector<float>& depth, int S, const Vec4 in[3], float nearZ) {
    Vec4 poly[4]; int n = 0;
    for (int i = 0; i < 3; ++i) {
        const Vec4& a = in[i]; const Vec4& b = in[(i + 1) % 3];
        const float da = a.w - nearZ, db = b.w - nearZ;
        if (da >= 0.0f) { if (n < 4) poly[n++] = a; }
        if ((da >= 0.0f) != (db >= 0.0f)) {
            float t = da / (da - db);
            if (n < 4) poly[n++] = Vec4{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                                        a.z + (b.z - a.z) * t, nearZ};
        }
    }
    if (n < 3) return;
    for (int j = 1; j + 1 < n; ++j) {
        const Vec4* tri[3] = {&poly[0], &poly[j], &poly[j + 1]};
        float sx[3], sy[3], iw[3];
        for (int k = 0; k < 3; ++k) {
            iw[k] = 1.0f / tri[k]->w;
            sx[k] = (tri[k]->x * iw[k] * 0.5f + 0.5f) * S;
            sy[k] = (1.0f - (tri[k]->y * iw[k] * 0.5f + 0.5f)) * S;
        }
        float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
        if (area == 0.0f) continue;
        float inv = 1.0f / area;
        int minx = (int)std::floor(std::fmin(sx[0], std::fmin(sx[1], sx[2])));
        int maxx = (int)std::ceil (std::fmax(sx[0], std::fmax(sx[1], sx[2])));
        int miny = (int)std::floor(std::fmin(sy[0], std::fmin(sy[1], sy[2])));
        int maxy = (int)std::ceil (std::fmax(sy[0], std::fmax(sy[1], sy[2])));
        if (minx < 0) minx = 0; if (miny < 0) miny = 0;
        if (maxx >= S) maxx = S - 1; if (maxy >= S) maxy = S - 1;
        for (int y = miny; y <= maxy; ++y)
            for (int x = minx; x <= maxx; ++x) {
                float px = x + 0.5f, py = y + 0.5f;
                float w0 = ((sx[1] - px) * (sy[2] - py) - (sx[2] - px) * (sy[1] - py)) * inv;
                float w1 = ((sx[2] - px) * (sy[0] - py) - (sx[0] - px) * (sy[2] - py)) * inv;
                float w2 = 1.0f - w0 - w1;
                const float e = -1e-4f;                       // a hair wide: no cracks along fan edges
                if (w0 < e || w1 < e || w2 < e) continue;
                float ivw = w0 * iw[0] + w1 * iw[1] + w2 * iw[2];
                if (ivw <= 0.0f) continue;
                float d = 1.0f / ivw;
                float& dref = depth[(std::size_t)y * S + x];
                if (d < dref) dref = d;
            }
    }
}
} // namespace detail

void RenderLocalShadowMaps(const Scene& scene, const Mat4& camVp, const Vec3& eye) {
    LocalShadowSet& ls = LocalShadows();
    ls.maps.clear(); ls.enabled = false;
    for (int& m : ls.lightMap) m = -1;
    if (!ShadowsEnabled()) return;
    const auto& lights = SceneLights::List();
    if (lights.empty()) return;

    struct Cand { int index; float key; };
    std::vector<Cand> cands;
    const Frustum camFr = Frustum::FromVP(camVp);
    for (int i = 0; i < (int)lights.size() && i < SceneLights::kMaxShadowedLights; ++i) {
        const LightSample& s = lights[i];
        if (s.type == 0 || !s.castShadows || s.shadowStrength <= 0.0f || s.range <= 0.0f) continue;
        if (s.color.x + s.color.y + s.color.z <= 1e-4f) continue;
        if (!camFr.SphereVisible(s.pos, s.range)) continue;      // its light cannot reach the view
        cands.push_back({i, (s.pos - eye).Magnitude() - s.range});
    }
    if (cands.empty()) return;
    std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.key < b.key; });

    int S = LocalShadowResolution(); if (S < 32) S = 32; if (S > 2048) S = 2048;
    int used = 0;
    for (const Cand& c : cands) {
        const LightSample& s = lights[c.index];
        const int need = s.type == 2 ? 1 : 6;
        if (used + need > LocalShadowSet::kMaxTiles) continue;   // a spot may still fit
        used += need;
        LocalShadowMapSW m;
        m.light = c.index;
        m.view = BuildLocalLightView(s, S);
        for (int f = 0; f < m.view.tiles; ++f) m.depth[f].assign((std::size_t)S * S, 1e30f);
        ls.lightMap[c.index] = (int)ls.maps.size();
        ls.maps.push_back(std::move(m));
    }
    ls.size = S;

    // Rasterize every shadow caster into each map that can reach it.
    std::vector<Vec4> clip;
    for (const auto& go : scene.Objects()) {
        if (!go || go->firstPersonOnly) continue;
        auto* mr = go->GetComponent<MeshRenderer>();
        if (!mr || !go->ActiveInHierarchy() || !mr->enabled || mr->wireframe || !mr->castShadows) continue;
        const Mesh* shMesh = visibility::Current().ShadowMesh(go.get(), mr);
        if (!shMesh || shMesh->vertices.empty()) continue;
        Mat4 model = go->transform->LocalToWorldMatrix();
        Vec3 blo, bhi; mr->DisplayMesh().Bounds(blo, bhi);
        Vec3 wlo{1e30f, 1e30f, 1e30f}, whi{-1e30f, -1e30f, -1e30f};
        for (int k = 0; k < 8; ++k) {
            Vec3 w = model.MultiplyPoint({(k & 1) ? bhi.x : blo.x, (k & 2) ? bhi.y : blo.y, (k & 4) ? bhi.z : blo.z});
            wlo.x = std::fmin(wlo.x, w.x); wlo.y = std::fmin(wlo.y, w.y); wlo.z = std::fmin(wlo.z, w.z);
            whi.x = std::fmax(whi.x, w.x); whi.y = std::fmax(whi.y, w.y); whi.z = std::fmax(whi.z, w.z);
        }
        const auto& V = shMesh->vertices; const auto& T = shMesh->triangles;
        for (LocalShadowMapSW& m : ls.maps) {
            const LightSample& s = lights[m.light];
            // Skip a caster wholly outside the light's reach (box vs sphere).
            float qx = std::fmax(wlo.x, std::fmin(s.pos.x, whi.x)) - s.pos.x;
            float qy = std::fmax(wlo.y, std::fmin(s.pos.y, whi.y)) - s.pos.y;
            float qz = std::fmax(wlo.z, std::fmin(s.pos.z, whi.z)) - s.pos.z;
            if (qx * qx + qy * qy + qz * qz > s.range * s.range) continue;
            for (int f = 0; f < m.view.tiles; ++f) {
                Mat4 mvp = m.view.vp[f] * model;
                clip.resize(V.size());
                for (std::size_t v = 0; v < V.size(); ++v) clip[v] = mvp * Vec4{V[v], 1.0f};
                for (std::size_t i = 0; i + 2 < T.size(); i += 3) {
                    const Vec4 tri[3] = {clip[T[i]], clip[T[i + 1]], clip[T[i + 2]]};
                    detail::RasterLocalDepth(m.depth[f], S, tri, m.view.nearZ);
                }
            }
        }
    }
    ls.enabled = true;
}

} // namespace okay
