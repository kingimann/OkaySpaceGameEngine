// What one Project panel thumbnail of a scene, prefab or model costs on the software renderer (editor/main.cpp RenderFramedImage
// draws a 128 px picture at 2x2 supersampling). Before 2026-11-25 it ran with every process-wide pass the Scene view uses: the sun's
// shadow maps (three 2048 x 2048 cascades by default), local light shadow maps, ambient occlusion, bloom and FXAA, so one tile cost
// about a Scene view frame, and it overwrote the Scene view's occlusion depth and counters. okay::ReducedRenderScope turns those
// passes off for the picture and puts everything back. The bounds are work done (shadow maps made, state left behind), which do not
// depend on how loaded the machine is; the times are printed for the report.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

using namespace okay;

namespace {

GameObject* Cube(Scene& s, Vec3 at, Vec3 scale) {
    GameObject* go = s.CreateGameObject("Cube");
    go->transform->localPosition = at;
    go->transform->localScale = scale;
    auto* mr = go->AddComponent<MeshRenderer>();
    mr->mesh = Mesh::Cube();
    mr->color = Color(0.7f, 0.6f, 0.5f, 1.0f);
    return go;
}

// A prefab-sized scene: a floor, a 12 x 12 block of crates and a sun that casts shadows (the default).
void Build(Scene& s) {
    GameObject* sun = s.CreateGameObject("Sun");
    sun->transform->localRotation = Quat::Euler(50.0f, 30.0f, 0.0f);
    sun->AddComponent<Light>();
    Cube(s, {0, -0.5f, 0}, {30, 1, 30});
    for (int z = 0; z < 12; ++z)
        for (int x = 0; x < 12; ++x) Cube(s, {(float)x * 2.0f - 11.0f, 0.5f, (float)z * 2.0f - 11.0f}, {1, 1 + (float)((x + z) % 3), 1});
    s.Start();
    s.Update(0.0f);
}

struct Shot { double ms = 0; int pixels = 0; bool sunShadows = false; };

Shot Render(Scene& s, bool reduced) {
    ApplySceneLight(s);
    const Vec3 eye{18, 16, 18};
    const Mat4 vp = Mat4::Perspective(40.0f, 1.0f, 0.05f, 4000.0f) * Mat4::LookAt(eye, {0, 0, 0}, Vec3::Up);
    Raster work; std::vector<std::uint32_t> out;
    Shot r;
    const auto t0 = std::chrono::steady_clock::now();
    const std::uint32_t* px = nullptr;
    if (reduced) {
        ReducedRenderScope scope(/*depthOnly=*/false, /*keepOcclusionDepth=*/true);
        px = RenderMeshesSS(work, out, s, vp, eye, 128, 128, 2);
        r.sunShadows = Shadows().enabled || LocalShadows().enabled;
    } else {
        px = RenderMeshesSS(work, out, s, vp, eye, 128, 128, 2);
        r.sunShadows = Shadows().enabled || LocalShadows().enabled;
    }
    r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    for (int i = 0; px && i < 128 * 128; ++i) if (px[i] >> 24) ++r.pixels;
    return r;
}

double Median(std::vector<double> v) { std::sort(v.begin(), v.end()); return v.empty() ? 0.0 : v[v.size() / 2]; }

}  // namespace

int main() {
    RUN_SUITE("thumbnail render: no shadow maps or post passes, and the Scene view's state is left as it was");
    {
        Scene s;
        Build(s);
        const bool shadowsWere = ShadowsEnabled(), fxaaWas = FXAAEnabled(), toneWas = ToneMapEnabled();
        // The full render really makes shadow maps here, or the comparison would prove nothing.
        const Shot full = Render(s, false);
        CHECK(full.sunShadows);
        CHECK(full.pixels > 128 * 128 / 4);
        // What the Scene view published after its own frame: a reduced render must leave it.
        OcclusionDepth& od = SceneOcclusionDepth();
        od.w = 3; od.h = 2; od.valid = true; od.d = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f};
        LastCullCounters().drawn = 4242;
        PostVignetteHandled() = true;
        const Shot cheap = Render(s, true);
        CHECK(!cheap.sunShadows);                                   // no sun or local shadow map was made
        CHECK(cheap.pixels > 128 * 128 / 4);                        // still a picture of the scene
        CHECK(od.w == 3 && od.h == 2 && od.valid && od.d.size() == 6 && od.d[5] == 0.6f);
        CHECK(LastCullCounters().drawn == 4242);
        CHECK(PostVignetteHandled());
        CHECK(ShadowsEnabled() == shadowsWere && FXAAEnabled() == fxaaWas && ToneMapEnabled() == toneWas);

        std::vector<double> a, b;
        for (int i = 0; i < 5; ++i) { a.push_back(Render(s, false).ms); b.push_back(Render(s, true).ms); }
        std::printf("  thumbnail 128 px x2 of a 146-object scene: full %.1f ms, reduced %.1f ms (median of 5)\n", Median(a), Median(b));
    }

    RUN_SUITE("depth-only pass (FillOcclusionDepth) still publishes its depth and keeps the frame's counters");
    {
        Scene s;
        Build(s);
        ApplySceneLight(s);
        const Vec3 eye{18, 16, 18};
        const Mat4 vp = Mat4::Perspective(40.0f, 1.0f, 0.05f, 4000.0f) * Mat4::LookAt(eye, {0, 0, 0}, Vec3::Up);
        Raster r; std::vector<std::uint32_t> out;
        LastCullCounters().drawn = 77;
        SceneOcclusionDepth().valid = false;
        FillOcclusionDepth(r, out, s, vp, eye, 48, 32);
        CHECK(SceneOcclusionDepth().valid && SceneOcclusionDepth().w == 48);
        CHECK(LastCullCounters().drawn == 77);
        CHECK(ShadowsEnabled() && ToneMapEnabled());
    }
    TEST_MAIN_RESULT();
}
