// LOD transitions without popping (feature map 2.8): dithered cross-fade between levels, LOD bias from the
// quality presets, and the Scene-view LOD colour overlay. Software renderer checked pixel by pixel; the OpenGL
// and Direct3D 11 shaders use the same dither table (LodDither.hpp).
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Core/DisplaySettings.hpp"
#include "okay/Render/LodDither.hpp"
#include <chrono>
#include <cmath>
#include <set>

using namespace okay;

static Mat4 CamVP(const Vec3& eye) {
    const Mat4 view = Mat4::LookAt(eye, {0, 0, 0}, {0, 1, 0});
    const Mat4 proj = Mat4::Perspective(60.0f, 1.0f, 0.1f, 500.0f);
    return proj * view;
}

// A sphere with a two-level LOD Group: level 0 inside 10 units, level 1 to 30, culled past 30.
static GameObject* Ball(Scene& s, LODGroup** out) {
    GameObject* go = s.CreateGameObject("Ball");
    auto* mr = go->AddComponent<MeshRenderer>();
    mr->mesh = Mesh::Sphere(1.0f, 12);
    mr->color = Color::White;
    auto* lod = go->AddComponent<LODGroup>();
    lod->levels.resize(2);
    lod->levels[0].distance = 10.0f; lod->levels[0].ratio = 1.0f;
    lod->levels[1].distance = 30.0f; lod->levels[1].ratio = 0.3f;
    lod->cullDistance = 30.0f;
    lod->hysteresis = 0.0f;
    lod->RebuildFromRatios(mr->mesh);
    *out = lod;
    return go;
}

static int CountFor(const visibility::Frame& f, const GameObject* go) {
    int n = 0;
    for (const auto& it : f.items) if (it.go == go) ++n;
    return n;
}

int main() {
    RUN_SUITE("dither table");
    {
        std::set<int> seen;
        for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) seen.insert(LodDitherIndex(x, y));
        CHECK(seen.size() == 16u);
        CHECK(LodDitherIndex(0, 0) == 0 && LodDitherIndex(1, 0) == 8 && LodDitherIndex(0, 1) == 12 && LodDitherIndex(1, 1) == 4);
        CHECK(LodDitherIndex(5, 6) == LodDitherIndex(1, 2));   // repeats every 4 pixels
        // The two halves cover every pixel exactly once, for any fade.
        for (float fade : {0.0f, 0.1f, 0.37f, 0.5f, 0.93f, 1.0f}) {
            int both = 0, none = 0, kept = 0;
            for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) {
                const bool a = LodDitherKeep(x, y, fade, false), b = LodDitherKeep(x, y, fade, true);
                both += a && b; none += !a && !b; kept += a;
            }
            CHECK(both == 0); CHECK(none == 0);
            CHECK(std::fabs(kept / 64.0f - fade) <= 1.0f / 16.0f + 1e-4f);
        }
    }

    RUN_SUITE("cross-fade between levels");
    {
        Scene s;
        LODGroup* lod = nullptr;
        GameObject* go = Ball(s, &lod);
        CHECK(lod->fadeMode == LODGroup::FadeCrossFade);   // on by default: no popping
        lod->fadeDuration = 0.4f;
        visibility::LodClockOverride() = 100.0;
        const visibility::Frame& f0 = visibility::Build(s, CamVP({0, 0, 5}), {0, 0, 5}, 128, 128, 60, nullptr);
        CHECK(CountFor(f0, go) == 1);   // first frame: nothing to fade from
        CHECK(f0.items[0].lod == 0);
        CHECK(f0.items[0].ditherFade >= 1.0f && !f0.items[0].ditherInvert);
        // Step back past 10 units: level 1 fades in over level 0.
        visibility::LodClockOverride() = 101.0;
        const visibility::Frame& f1 = visibility::Build(s, CamVP({0, 0, 15}), {0, 0, 15}, 128, 128, 60, nullptr);
        CHECK(CountFor(f1, go) == 2);
        CHECK(f1.lodFading == 1);
        const visibility::Item* inc = nullptr; const visibility::Item* out = nullptr;
        for (const auto& it : f1.items) if (it.go == go) (it.fadeSecondary ? out : inc) = &it;
        CHECK(inc && out);
        if (inc && out) {
            CHECK(inc->lod == 1 && out->lod == 0);
            CHECK(inc->ditherFade < 0.05f && !inc->ditherInvert);
            CHECK(out->ditherInvert && std::fabs(out->ditherFade - inc->ditherFade) < 1e-6f);
            CHECK(inc->mesh->TriangleCount() < out->mesh->TriangleCount());
        }
        visibility::LodClockOverride() = 101.2;   // half way
        const visibility::Frame& f2 = visibility::Build(s, CamVP({0, 0, 15}), {0, 0, 15}, 128, 128, 60, nullptr);
        CHECK(CountFor(f2, go) == 2);
        for (const auto& it : f2.items) if (it.go == go) CHECK_NEAR(it.ditherFade, 0.5f, 0.02f);
        float fade = -1; int a = -9, b = -9;
        const Mesh* from = f2.FadeFor(go, fade, a, b);
        CHECK(from != nullptr); CHECK_NEAR(fade, 0.5f, 0.02f); CHECK(a == 1 && b == 0);
        visibility::LodClockOverride() = 101.5;   // done
        const visibility::Frame& f3 = visibility::Build(s, CamVP({0, 0, 15}), {0, 0, 15}, 128, 128, 60, nullptr);
        CHECK(CountFor(f3, go) == 1);
        CHECK(f3.items[0].lod == 1 && f3.items[0].ditherFade >= 1.0f);
        CHECK(f3.FadeFor(go, fade, a, b) == nullptr);
        CHECK(f3.lodFading == 0);

        // Past the cull distance the object dissolves away instead of vanishing.
        visibility::LodClockOverride() = 103.0;
        const visibility::Frame& f4 = visibility::Build(s, CamVP({0, 0, 40}), {0, 0, 40}, 128, 128, 60, nullptr);
        CHECK(CountFor(f4, go) == 1);
        CHECK(f4.lodFading == 1);
        if (!f4.items.empty()) { CHECK(f4.items[0].lod == 1); CHECK(f4.items[0].ditherFade > 0.95f); }
        visibility::LodClockOverride() = 103.3;
        const visibility::Frame& f5 = visibility::Build(s, CamVP({0, 0, 40}), {0, 0, 40}, 128, 128, 60, nullptr);
        CHECK(CountFor(f5, go) == 1);
        if (!f5.items.empty()) CHECK_NEAR(f5.items[0].ditherFade, 0.25f, 0.02f);
        visibility::LodClockOverride() = 104.0;
        const visibility::Frame& f6 = visibility::Build(s, CamVP({0, 0, 40}), {0, 0, 40}, 128, 128, 60, nullptr);
        CHECK(CountFor(f6, go) == 0);
        CHECK(f6.lodCulled == 1);

        // Fade off: the old behaviour, a hard switch.
        lod->fadeMode = LODGroup::FadeNone;
        visibility::LodClockOverride() = 105.0;
        visibility::Build(s, CamVP({0, 0, 5}), {0, 0, 5}, 128, 128, 60, nullptr);
        visibility::LodClockOverride() = 105.1;
        const visibility::Frame& f7 = visibility::Build(s, CamVP({0, 0, 15}), {0, 0, 15}, 128, 128, 60, nullptr);
        CHECK(CountFor(f7, go) == 1);
        CHECK(f7.items[0].lod == 1 && f7.items[0].ditherFade >= 1.0f);

        // Saved and loaded.
        lod->fadeMode = LODGroup::FadeCrossFade; lod->fadeDuration = 0.65f;
        Scene back; std::string err;
        CHECK(SceneSerializer::Deserialize(back, SceneSerializer::Serialize(s), &err));
        LODGroup* lb = nullptr;
        for (const auto& up : back.Objects()) if (auto* g = up->GetComponent<LODGroup>()) lb = g;
        CHECK(lb && lb->fadeMode == LODGroup::FadeCrossFade);
        if (lb) CHECK_NEAR(lb->fadeDuration, 0.65f, 1e-4f);
        lod->fadeMode = LODGroup::FadeNone;
        Scene back2;
        CHECK(SceneSerializer::Deserialize(back2, SceneSerializer::Serialize(s), &err));
        lb = nullptr;
        for (const auto& up : back2.Objects()) if (auto* g = up->GetComponent<LODGroup>()) lb = g;
        CHECK(lb && lb->fadeMode == LODGroup::FadeNone);
        visibility::LodClockOverride() = -1.0;
    }

    RUN_SUITE("lod bias");
    {
        Scene s;
        LODGroup* lod = nullptr;
        GameObject* go = Ball(s, &lod);
        lod->fadeMode = LODGroup::FadeNone;
        visibility::LodBias() = 1.0f;
        lod->lodState = -1;
        const visibility::Frame& a = visibility::Build(s, CamVP({0, 0, 14}), {0, 0, 14}, 128, 128, 60, nullptr);
        CHECK(CountFor(a, go) == 1 && a.items[0].lod == 1);
        visibility::LodBias() = 2.0f;    // detail kept twice as far
        lod->lodState = -1;
        const visibility::Frame& b = visibility::Build(s, CamVP({0, 0, 14}), {0, 0, 14}, 128, 128, 60, nullptr);
        CHECK(CountFor(b, go) == 1 && b.items[0].lod == 0);
        visibility::LodBias() = 0.5f;    // cheaper levels sooner
        lod->lodState = -1;
        const visibility::Frame& c = visibility::Build(s, CamVP({0, 0, 8}), {0, 0, 8}, 128, 128, 60, nullptr);
        CHECK(CountFor(c, go) == 1 && c.items[0].lod == 1);
        // Screen-size mode takes the bias too.
        lod->useScreenSize = true;
        lod->levels[0].screenSize = 0.3f; lod->levels[1].screenSize = 0.05f; lod->cullScreen = 0.0f;
        visibility::LodBias() = 1.0f; lod->lodState = -1;
        const visibility::Frame& d = visibility::Build(s, CamVP({0, 0, 8}), {0, 0, 8}, 128, 128, 60, nullptr);
        const int plain = d.items.empty() ? -1 : d.items[0].lod;
        visibility::LodBias() = 4.0f; lod->lodState = -1;
        const visibility::Frame& e = visibility::Build(s, CamVP({0, 0, 8}), {0, 0, 8}, 128, 128, 60, nullptr);
        CHECK(plain == 1);
        CHECK(!e.items.empty() && e.items[0].lod == 0);
        visibility::LodBias() = 1.0f;
        // The quality presets carry a bias: Low cheaper, High sharper.
        CHECK(DisplaySettings::Preset(0).lodBias < DisplaySettings::Preset(1).lodBias);
        CHECK(DisplaySettings::Preset(1).lodBias < DisplaySettings::Preset(2).lodBias);
        CHECK_NEAR(DisplaySettings::Preset(1).lodBias, 1.0f, 1e-6f);
        DisplaySettings::ApplyPreset(0);
        CHECK_NEAR(visibility::LodBias(), DisplaySettings::Preset(0).lodBias, 1e-6f);
        DisplaySettings::ApplyPreset(1);
        CHECK_NEAR(visibility::LodBias(), 1.0f, 1e-6f);
    }

    RUN_SUITE("lod colour overlay");
    {
        LODGroup g;
        float r, gg, b;
        g.DebugTint(1, r, gg, b);
        CHECK(r == 1 && gg == 1 && b == 1);   // off
        visibility::LodOverlay() = true;
        g.DebugTint(0, r, gg, b);
        CHECK(r > gg && r > b);                // level 0 red
        g.DebugTint(1, r, gg, b);
        CHECK(gg > r && gg > b);               // level 1 green
        g.DebugTint(4, r, gg, b);
        CHECK(!(r == 1 && gg == 1 && b == 1));
        visibility::LodOverlay() = false;
    }

    RUN_SUITE("software renderer dissolves between levels");
    {
        Scene s;
        LODGroup* lod = nullptr;
        Ball(s, &lod);
        lod->fadeDuration = 1.0f;
        s.Start();
        visibility::LodOverlay() = true;   // level 0 red, level 1 green
        Raster r; std::vector<std::uint32_t> down;
        auto count = [&](int& red, int& green) {
            red = green = 0;
            for (std::uint32_t p : r.color) {
                const int R = (int)(p & 255), G = (int)((p >> 8) & 255), B = (int)((p >> 16) & 255);
                if (R > G + 25 && R > B + 25) ++red;
                else if (G > R + 25 && G > B + 25) ++green;
            }
        };
        const Vec3 nearEye{0, 0, 9.5f}, farEye{0, 0, 10.5f};
        visibility::LodClockOverride() = 10.0;
        RenderMeshesSS(r, down, s, CamVP(nearEye), nearEye, 128, 128, 1, nullptr);
        int red = 0, green = 0;
        count(red, green);
        CHECK(red > 200 && green == 0);
        visibility::LodClockOverride() = 11.0;   // fade starts
        RenderMeshesSS(r, down, s, CamVP(farEye), farEye, 128, 128, 1, nullptr);
        visibility::LodClockOverride() = 11.5;   // half way
        RenderMeshesSS(r, down, s, CamVP(farEye), farEye, 128, 128, 1, nullptr);
        count(red, green);
        std::printf("  half-way dissolve: %d red (LOD 0) and %d green (LOD 1) pixels\n", red, green);
        CHECK(red > 60 && green > 60);
        CHECK(std::fabs((float)red - (float)green) < 0.35f * (float)(red + green));
        // Neighbouring pixels alternate: a dissolve, not two halves.
        int mixed = 0;
        for (int y = 0; y < 128; ++y) for (int x = 0; x + 1 < 128; ++x) {
            const std::uint32_t p = r.Get(x, y), q = r.Get(x + 1, y);
            const bool pr = (p & 255) > ((p >> 8) & 255) + 25, qg = ((q >> 8) & 255) > (q & 255) + 25;
            mixed += pr && qg;
        }
        CHECK(mixed > 30);
        visibility::LodClockOverride() = 12.5;   // finished
        RenderMeshesSS(r, down, s, CamVP(farEye), farEye, 128, 128, 1, nullptr);
        count(red, green);
        CHECK(red == 0 && green > 200);
        CHECK(!SoftwareLodDither().Active());   // the band workers leave no dither behind
        visibility::LodOverlay() = false;
        visibility::LodClockOverride() = -1.0;
    }
    RUN_SUITE("dense scene: triangles and frame time with and without LOD");
    {
        // 196 spheres of 4,032 triangles in a 14 x 14 field, seen from one end (software renderer, 320 x 180).
        Scene s;
        Mesh ball = Mesh::Sphere(1.0f, 48);
        std::vector<LODGroup*> groups;
        for (int z = 0; z < 14; ++z)
            for (int x = 0; x < 14; ++x) {
                GameObject* go = s.CreateGameObject("B");
                go->transform->localPosition = {(x - 6.5f) * 4.0f, 0.0f, -(float)z * 5.0f};
                go->isStatic = true;
                auto* mr = go->AddComponent<MeshRenderer>();
                mr->mesh = ball;
                auto* lod = go->AddComponent<LODGroup>();
                lod->levels.resize(4);
                const float d[4] = {12, 25, 45, 90}, r[4] = {1.0f, 0.35f, 0.12f, 0.04f};
                for (int i = 0; i < 4; ++i) { lod->levels[(std::size_t)i].distance = d[i]; lod->levels[(std::size_t)i].ratio = r[i]; }
                lod->cullDistance = 0.0f;
                lod->fadeMode = LODGroup::FadeNone;
                lod->RebuildFromRatios(ball);
                groups.push_back(lod);
            }
        s.Start();
        const Vec3 eye{0, 4, 14};
        const Mat4 view = Mat4::LookAt(eye, {0, 0, -30}, {0, 1, 0});
        const Mat4 vp = Mat4::Perspective(60.0f, 320.0f / 180.0f, 0.1f, 500.0f) * view;
        Raster r; std::vector<std::uint32_t> down;
        auto run = [&](bool lodOn, std::size_t& tris, double& ms) {
            for (LODGroup* g : groups) { g->forceLevel = lodOn ? -1 : 0; g->lodState = -1; }
            RenderMeshesSS(r, down, s, vp, eye, 320, 180, 1, nullptr);   // warm caches
            tris = visibility::Current().lodTriangles;
            const auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < 5; ++i) RenderMeshesSS(r, down, s, vp, eye, 320, 180, 1, nullptr);
            ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 5.0;
        };
        std::size_t trisOff = 0, trisOn = 0; double msOff = 0, msOn = 0;
        run(false, trisOff, msOff);
        run(true, trisOn, msOn);
        std::printf("  without LOD: %zu triangles, %.1f ms a frame; with LOD: %zu triangles, %.1f ms a frame\n", trisOff, msOff, trisOn, msOn);
        CHECK(trisOn * 3 < trisOff);   // far fewer triangles (frame time is printed, not checked: shared machine)
    }
    TEST_MAIN_RESULT();
}
