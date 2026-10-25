#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Render/ShadowSetup.hpp"
#include <cmath>
#include <string>

using namespace okay;

// Lighting + shadow options: per-light shadow settings and flicker gathered by
// ApplySceneLight, scene/mesh/light save + reload, the GPU shadow plan (sun
// cascades + spot/point tiles), the Ambient Mode colours, and the software
// renderer honouring the per-light / per-mesh switches.
namespace {
GameObject* MakeMesh(Scene& s, const char* name, Mesh m, Vec3 pos) {
    GameObject* go = s.CreateGameObject(name);
    go->transform->localPosition = pos;
    go->AddComponent<MeshRenderer>()->mesh = m;
    return go;
}
Light* MakeLight(Scene& s, const char* name, Light::Type t, Vec3 pos, Vec3 aim) {
    GameObject* go = s.CreateGameObject(name);
    go->transform->localPosition = pos;
    go->transform->localRotation = Quat::LookRotation(aim, std::fabs(aim.y) > 0.99f ? Vec3{1, 0, 0} : Vec3{0, 1, 0});
    auto* l = go->AddComponent<Light>();
    l->type = t;
    return l;
}
Vec3 Ndc(const Mat4& vp, const Vec3& p) {
    Vec4 c = vp * Vec4{p, 1.0f};
    return Vec3{c.x / c.w, c.y / c.w, c.z / c.w};
}
bool Inside(const Vec3& n, float lim = 1.0f) {
    return std::fabs(n.x) <= lim && std::fabs(n.y) <= lim && n.z >= -1.0f && n.z <= 1.0f;
}
} // namespace

int main() {
    RUN_SUITE("light_shadows");

    const Vec3 eye{0, 6, 18};
    const Mat4 vp = Mat4::Perspective(60.0f, 16.0f / 9.0f, 0.1f, 200.0f) *
                    Mat4::LookAt(eye, Vec3{0, 0, 0}, Vec3{0, 1, 0});

    // --- ApplySceneLight: shadow fields, sun index, disabled lights -----------
    {
        Scene s("gather");
        Light* off = MakeLight(s, "Off", Light::Type::Directional, {0, 5, 0}, {0, -1, 0});
        off->enabled = false;                               // disabled: no light at all
        Light* lamp = MakeLight(s, "Lamp", Light::Type::Point, {2, 2, 0}, {0, 0, 1});
        lamp->castShadows = false; lamp->shadowStrength = 0.4f; lamp->shadowBias = 2.0f;
        Light* sun = MakeLight(s, "Sun", Light::Type::Directional, {0, 9, 0}, {-0.5f, -1, -0.3f});
        sun->shadowStrength = 0.75f;
        ApplySceneLight(s);
        const auto& L = SceneLights::List();
        CHECK(L.size() == 2);                               // the disabled light was skipped
        CHECK(L[0].type == 1 && !L[0].castShadows);
        CHECK_NEAR(L[0].shadowStrength, 0.4f, 1e-6f);
        CHECK_NEAR(L[0].shadowBias, 2.0f, 1e-6f);
        CHECK(SceneLights::SunIndex() == 1);                // the first ENABLED directional
        CHECK(SceneLights::Sun() && SceneLights::Sun()->castShadows);
        CHECK_NEAR(SceneLights::Sun()->shadowStrength, 0.75f, 1e-6f);
        Vec3 d = SceneLight::Direction();
        Vec3 want = Vec3{-0.5f, -1, -0.3f}.Normalized();
        CHECK_NEAR(d.x, want.x, 1e-3f); CHECK_NEAR(d.y, want.y, 1e-3f);
    }
    {
        Scene s("nolights");
        ApplySceneLight(s);
        CHECK(SceneLights::List().size() == 1);             // the default sun
        CHECK(SceneLights::SunIndex() == 0);
    }

    // --- Flicker: steady until played, then varies within [1-amount, 1] ------
    {
        Scene s("flicker");
        Light* l = MakeLight(s, "Torch", Light::Type::Point, {0, 1, 0}, {0, 0, 1});
        l->intensity = 2.0f;
        CHECK_NEAR(l->FlickerFactor(), 1.0f, 1e-6f);        // no flicker set
        l->flicker = 0.5f; l->flickerSpeed = 10.0f;
        CHECK_NEAR(l->FlickerFactor(), 1.0f, 1e-6f);        // not played yet: full light in edit mode
        float lo = 2.0f, hi = -1.0f, prev = -1.0f; int changes = 0;
        for (int i = 0; i < 200; ++i) {
            l->Update(1.0f / 60.0f);
            float f = l->FlickerFactor();
            lo = std::fmin(lo, f); hi = std::fmax(hi, f);
            if (prev >= 0.0f && std::fabs(f - prev) > 1e-4f) ++changes;
            prev = f;
        }
        CHECK(lo >= 0.5f - 1e-4f && hi <= 1.0f + 1e-4f);
        CHECK(hi - lo > 0.1f);                              // it really flickers
        CHECK(changes > 100);
        ApplySceneLight(s);
        CHECK_NEAR(SceneLights::List()[0].color.x, 2.0f * prev, 1e-4f);   // intensity x flicker
    }

    // --- Save + reload: light, mesh and scene settings ------------------------
    {
        Scene s("save");
        GameObject* g = MakeMesh(s, "Ground", Mesh::Plane(10.0f), {0, 0, 0});
        g->GetComponent<MeshRenderer>()->castShadows = false;
        GameObject* b = MakeMesh(s, "Box", Mesh::Cube(1.0f), {0, 1, 0});
        b->GetComponent<MeshRenderer>()->receiveShadows = false;
        Light* l = MakeLight(s, "Spot", Light::Type::Spot, {0, 4, 0}, {0, -1, 0});
        l->castShadows = false; l->shadowStrength = 0.3f; l->shadowBias = 1.5f;
        l->flicker = 0.25f; l->flickerSpeed = 6.0f; l->enabled = false;
        s.renderSettings.ambientMode = Scene::RenderSettings::AmbientSky;
        s.renderSettings.ambientSkyTint = 1.2f;
        std::string text = SceneSerializer::Serialize(s);
        CHECK(text.find("ambientmode 2") != std::string::npos);
        Scene r("reload");
        std::string err;
        CHECK(SceneSerializer::Deserialize(r, text, &err));
        CHECK(err.empty());
        CHECK(r.renderSettings.ambientMode == Scene::RenderSettings::AmbientSky);
        CHECK_NEAR(r.renderSettings.ambientSkyTint, 1.2f, 1e-5f);
        GameObject* rg = r.Find("Ground"); GameObject* rb = r.Find("Box"); GameObject* rl = r.Find("Spot");
        CHECK(rg && rb && rl);
        if (rg && rb && rl) {
            CHECK(!rg->GetComponent<MeshRenderer>()->castShadows);
            CHECK(rg->GetComponent<MeshRenderer>()->receiveShadows);
            CHECK(rb->GetComponent<MeshRenderer>()->castShadows);
            CHECK(!rb->GetComponent<MeshRenderer>()->receiveShadows);
            Light* rl2 = rl->GetComponent<Light>();
            CHECK(rl2 && rl2->type == Light::Type::Spot);
            if (rl2) {
                CHECK(!rl2->castShadows);
                CHECK_NEAR(rl2->shadowStrength, 0.3f, 1e-5f);
                CHECK_NEAR(rl2->shadowBias, 1.5f, 1e-5f);
                CHECK_NEAR(rl2->flicker, 0.25f, 1e-5f);
                CHECK_NEAR(rl2->flickerSpeed, 6.0f, 1e-5f);
                CHECK(!rl2->enabled);
            }
        }
        // Defaults write nothing new (older engines keep loading plain scenes).
        Scene d("defaults");
        MakeMesh(d, "Box", Mesh::Cube(1.0f), {0, 0, 0});
        MakeLight(d, "Sun", Light::Type::Directional, {0, 5, 0}, {0, -1, 0});
        std::string dt = SceneSerializer::Serialize(d);
        CHECK(dt.find("meshshadows") == std::string::npos);
        CHECK(dt.find("ambientmode") == std::string::npos);
        Scene dr("defaults2");
        CHECK(SceneSerializer::Deserialize(dr, dt));
        Light* dl = dr.Find("Sun") ? dr.Find("Sun")->GetComponent<Light>() : nullptr;
        CHECK(dl && dl->castShadows && dl->enabled && dl->flicker == 0.0f);
        CHECK(dr.renderSettings.ambientMode == Scene::RenderSettings::AmbientHemisphere);
    }

    // --- Ambient Mode colours ---------------------------------------------------
    {
        Scene::RenderSettings rs;
        Vec3 amb{0.2f, 0.3f, 0.4f}, sky, ground;
        rs.ambientMode = Scene::RenderSettings::AmbientFlat;
        SceneAmbientColors(rs, amb, sky, ground);
        CHECK_NEAR(sky.y, 0.3f, 1e-6f); CHECK_NEAR(ground.y, 0.3f, 1e-6f);
        rs.ambientMode = Scene::RenderSettings::AmbientHemisphere;
        SceneAmbientColors(rs, amb, sky, ground);
        CHECK_NEAR(sky.y, 0.3f * 1.15f, 1e-6f); CHECK_NEAR(ground.y, 0.3f * 0.55f, 1e-6f);
        rs.ambientMode = Scene::RenderSettings::AmbientSky;
        rs.skyTop = Color{0, 0, 1, 1}; rs.skyHorizon = Color{0, 0, 1, 1}; rs.skyBottom = Color{1, 0, 0, 1};
        rs.ambientSkyTint = 1.0f;
        SceneAmbientColors(rs, amb, sky, ground);
        CHECK(sky.z > amb.z && sky.x < amb.x);              // tops lean blue
        CHECK(ground.x > amb.x && ground.z < amb.z);        // undersides lean red
        CHECK_NEAR((sky.y + ground.y) * 0.5f, amb.y, 1e-5f);   // level unchanged on average
    }

    // --- GPU shadow plan: sun cascades -----------------------------------------
    ShadowsEnabled() = true;
    ShadowMapResolution() = 1024;
    ShadowCascades() = 3;
    ShadowDistance() = 80.0f;
    {
        Scene s("plan");
        MakeMesh(s, "Ground", Mesh::Plane(40.0f), {0, 0, 0});
        MakeMesh(s, "Box", Mesh::Cube(2.0f), {0, 1, 0});
        MakeLight(s, "Sun", Light::Type::Directional, {0, 10, 0}, {-0.4f, -1, -0.3f});
        ApplySceneLight(s);
        ShadowPlan plan;
        PlanShadows(s, vp, eye, plan);
        CHECK(plan.cascades == 3);
        CHECK(plan.sunLight == 0);
        CHECK(plan.cascadeSize == 1024);
        for (int c = 0; c < plan.cascades; ++c) {
            const auto& cs = plan.cascade[c];
            CHECK_NEAR(cs.texelWorld, 2.0f * cs.radius / 1024.0f, 1e-4f);
            if (c > 0) CHECK(cs.radius > plan.cascade[c - 1].radius);
            // Points anywhere in the cascade sphere land inside its ortho map.
            const Vec3 offs[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
            for (const Vec3& o : offs) CHECK(Inside(Ndc(cs.vp, cs.center + o * (cs.radius * 0.99f))));
        }
        // The box shadows the ground: depth along the light at the box top is
        // nearer than at the ground point it shades.
        Vec3 L = SceneLights::Sun()->dir.Normalized();
        Vec3 top{0, 2, 0}, groundHit = top + L * (2.0f / -L.y);
        Vec3 nTop = Ndc(plan.cascade[0].vp, top), nGround = Ndc(plan.cascade[0].vp, groundHit);
        CHECK_NEAR(nTop.x, nGround.x, 1e-3f); CHECK_NEAR(nTop.y, nGround.y, 1e-3f);
        CHECK(nTop.z < nGround.z);

        // A camera far from a small scene still gets its cascades ON the scene
        // (they're fit to where the scene is along the view, not just near the eye).
        {
            Scene small("small");
            MakeMesh(small, "Ground", Mesh::Plane(4.0f), {0, 0, 0});
            MakeMesh(small, "Box", Mesh::Cube(1.0f), {0, 0.5f, 0});
            MakeLight(small, "Sun", Light::Type::Directional, {0, 10, 0}, {-0.4f, -1, -0.3f});
            ApplySceneLight(small);
            const Vec3 farEye{0, 30, 60};
            Mat4 farVp = Mat4::Perspective(60.0f, 16.0f / 9.0f, 0.1f, 500.0f) *
                         Mat4::LookAt(farEye, Vec3{0, 0, 0}, Vec3{0, 1, 0});
            ShadowPlan fp;
            PlanShadows(small, farVp, farEye, fp);
            CHECK(fp.cascades == 3);
            bool covered = false;
            for (int c = 0; c < fp.cascades; ++c)
                if ((Vec3{0, 0, 0} - fp.cascade[c].center).Magnitude() < fp.cascade[c].radius * 0.8f) covered = true;
            CHECK(covered);
            // ...and the cascades are no bigger than the scene needs.
            CHECK(fp.cascade[fp.cascades - 1].radius < 4.5f);
            // Beyond the shadow distance: no sun cascades at all.
            ShadowDistance() = 30.0f;
            PlanShadows(small, farVp, farEye, fp);
            CHECK(fp.cascades == 0);
            ShadowDistance() = 80.0f;
            ApplySceneLight(s);
        }

        // Legacy whole-scene mode = one cascade; Cast Shadows off = none.
        ShadowDistance() = 0.0f;
        PlanShadows(s, vp, eye, plan);
        CHECK(plan.cascades == 1);
        ShadowDistance() = 80.0f;
        s.Find("Sun")->GetComponent<Light>()->castShadows = false;
        ApplySceneLight(s);
        PlanShadows(s, vp, eye, plan);
        CHECK(plan.cascades == 0);
        ShadowsEnabled() = false;
        s.Find("Sun")->GetComponent<Light>()->castShadows = true;
        ApplySceneLight(s);
        PlanShadows(s, vp, eye, plan);
        CHECK(plan.cascades == 0 && plan.local.empty());
        ShadowsEnabled() = true;
    }

    // --- GPU shadow plan: spot + point tiles --------------------------------------
    {
        Scene s("local");
        MakeMesh(s, "Ground", Mesh::Plane(40.0f), {0, 0, 0});
        MakeLight(s, "Sun", Light::Type::Directional, {0, 10, 0}, {0, -1, 0});
        Light* p1 = MakeLight(s, "P1", Light::Type::Point, {0, 2, 0}, {0, 0, 1});
        Light* p2 = MakeLight(s, "P2", Light::Type::Point, {3, 2, 2}, {0, 0, 1});
        Light* p3 = MakeLight(s, "P3", Light::Type::Point, {-3, 2, -6}, {0, 0, 1});
        Light* sp = MakeLight(s, "Spot", Light::Type::Spot, {-2, 4, 0}, {0, -1, 0});
        Light* nc = MakeLight(s, "NoCast", Light::Type::Point, {1, 1, 1}, {0, 0, 1});
        Light* far = MakeLight(s, "Behind", Light::Type::Point, {0, 2, 60}, {0, 0, 1});   // behind the camera
        for (Light* l : {p1, p2, p3, nc, far}) l->range = 6.0f;
        sp->range = 8.0f; sp->spotAngle = 50.0f;
        nc->castShadows = false;
        ApplySceneLight(s);
        ShadowPlan plan;
        PlanShadows(s, vp, eye, plan);
        // 16 tiles: two point lights (12) + the spot (1); the third point light
        // (needs 6) doesn't fit; NoCast and the off-screen light get none.
        CHECK(plan.tileSize == 256);
        CHECK(plan.local.size() == 3);
        CHECK(plan.tilesUsed == 13);
        int points = 0, spots = 0;
        const auto& L = SceneLights::List();
        for (const auto& ls : plan.local) {
            const LightSample& smp = L[ls.light];
            CHECK(smp.castShadows);
            CHECK((smp.pos - Vec3{1, 1, 1}).Magnitude() > 0.1f);     // not NoCast
            CHECK((smp.pos - Vec3{0, 2, 60}).Magnitude() > 0.1f);    // not the one behind
            if (ls.tiles == 6) {
                ++points;
                // Every direction from a point light lands inside the face the
                // shaders pick for it, within the rendered (wider than 90 deg) face.
                const Vec3 dirs[8] = {{1, 0.2f, 0.1f}, {-1, 0.9f, 0.3f}, {0.1f, 1, -0.95f}, {0.3f, -1, 0.2f},
                                      {0.5f, 0.4f, 1}, {-0.2f, -0.6f, -1}, {0.7f, -0.7f, 0.69f}, {-0.6f, 0.59f, -0.6f}};
                for (const Vec3& d : dirs) {
                    Vec3 v = d.Normalized() * 3.0f;
                    int f = CubeFace(v);
                    Vec3 n = Ndc(ls.vp[f], smp.pos + v);
                    CHECK(Inside(n, 1.0f / ShadowPlan::kCubeTanHalf + 1e-3f));
                }
            } else {
                ++spots;
                CHECK(ls.tiles == 1);
                // A point inside the cone projects into the spot's tile.
                CHECK(Inside(Ndc(ls.vp[0], smp.pos + Vec3{0.8f, -3.0f, 0.4f})));
                CHECK(Inside(Ndc(ls.vp[0], smp.pos + Vec3{0, -7.5f, 0})));
            }
            CHECK(ls.texelAt1 > 0.0f);
            CHECK(ls.firstTile + ls.tiles <= ShadowPlan::kMaxLocalTiles);
        }
        CHECK(points == 2 && spots == 1);
        // Tiles don't overlap.
        for (std::size_t i = 0; i < plan.local.size(); ++i)
            for (std::size_t j = i + 1; j < plan.local.size(); ++j) {
                const auto& a = plan.local[i]; const auto& b = plan.local[j];
                CHECK(a.firstTile + a.tiles <= b.firstTile || b.firstTile + b.tiles <= a.firstTile);
            }
        // Casters out of a light's range are skipped.
        const LightSample& l0 = L[plan.local[0].light];
        CHECK(CasterInLightRange(l0, l0.pos + Vec3{l0.range + 0.5f, 0, 0}, 1.0f));    // overlaps the reach
        CHECK(!CasterInLightRange(l0, l0.pos + Vec3{l0.range + 1.5f, 0, 0}, 1.0f));   // wholly outside
    }

    // --- Software renderer honours the per-light / per-mesh switches ---------
    {
        Scene s("sw");
        MakeMesh(s, "Ground", Mesh::Plane(40.0f), {0, 0, 0});
        GameObject* caster = MakeMesh(s, "Caster", Mesh::Cube(2.0f), {0, 4, 0});
        Light* sun = MakeLight(s, "Sun", Light::Type::Directional, {0, 10, 0}, {0, -1, 0});
        ShadowDistance() = 80.0f;
        auto under = [&]() {
            ApplySceneLight(s);
            RenderShadowMap(s, vp, eye);
            return ShadowFactor(Vec3{0, 0, 0}, Vec3{0, 1, 0});
        };
        CHECK(under() < 0.5f);                                  // shadowed
        sun->shadowStrength = 0.5f;
        float half = under();
        CHECK(half >= 0.45f && half < 0.95f);                   // lighter shadow
        sun->shadowStrength = 1.0f;
        sun->castShadows = false;
        CHECK_NEAR(under(), 1.0f, 1e-4f);                       // the sun casts no shadow
        sun->castShadows = true;
        caster->GetComponent<MeshRenderer>()->castShadows = false;
        CHECK_NEAR(under(), 1.0f, 1e-4f);                       // the cube doesn't block light
    }

    ShadowsEnabled() = false;
    SceneLight::Reset();
    TEST_MAIN_RESULT();
}
