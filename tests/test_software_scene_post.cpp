#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Render/SoftwareRenderer.hpp"
#include <cmath>

using namespace okay;

// Software renderer: the scene's Post-processing section (exposure, tonemap curve, colour grading, vignette,
// bloom, FXAA) is read, not just the process-wide switches. Neutral settings give the old frame exactly.
namespace {
std::uint32_t Px(const Raster& r, int x, int y) { return r.Get(x, y); }
int Ch(std::uint32_t v, int k) { return (int)((v >> (k * 8)) & 0xFF); }
void Fill(Raster& r, std::uint32_t abgr) { r.Resize(64, 64); r.Clear(abgr); }
Scene::RenderSettings Neutral() { Scene::RenderSettings rs; return rs; }
}

int main() {
    RUN_SUITE("software_scene_post");
    FXAAEnabled() = false; BloomEnabled() = false; ColorGradeEnabled() = false; ToneMapEnabled() = true; Exposure() = 1.0f;
    const std::uint32_t grey = 0xFF808080u;

    // Neutral scene post == the old ApplyToneMap output, pixel for pixel.
    {
        Raster a, b; Fill(a, grey); Fill(b, grey);
        ApplyToneMap(a);
        ApplyScenePost(b, PlanScenePost(Neutral()));
        CHECK(a.color == b.color);
    }
    // Exposure brightens, and 0 darkens.
    {
        auto rs = Neutral(); rs.post.exposure = 2.0f;
        Raster r; Fill(r, grey); ApplyScenePost(r, PlanScenePost(rs));
        Raster n; Fill(n, grey); ApplyScenePost(n, PlanScenePost(Neutral()));
        CHECK(Ch(Px(r, 5, 5), 0) > Ch(Px(n, 5, 5), 0));
        rs.post.exposure = 0.0f;
        Raster z; Fill(z, grey); ApplyScenePost(z, PlanScenePost(rs));
        CHECK(Ch(Px(z, 5, 5), 0) == 0);
    }
    // Tonemap curve: None is a straight clamp; Filmic differs from ACES; scene tonemap off = clamp.
    {
        auto rs = Neutral(); rs.post.tonemap = Scene::RenderSettings::PostSettings::TonemapNone;
        Raster r; Fill(r, grey); ApplyScenePost(r, PlanScenePost(rs));
        CHECK(Ch(Px(r, 1, 1), 0) == 128);
        rs.post.tonemap = Scene::RenderSettings::PostSettings::TonemapFilmic;
        Raster f; Fill(f, grey); ApplyScenePost(f, PlanScenePost(rs));
        Raster a; Fill(a, grey); ApplyScenePost(a, PlanScenePost(Neutral()));
        CHECK(Ch(Px(f, 1, 1), 0) != Ch(Px(a, 1, 1), 0));
        auto off = Neutral(); off.tonemap = false;
        Raster o; Fill(o, grey); ApplyScenePost(o, PlanScenePost(off));
        CHECK(Ch(Px(o, 1, 1), 0) == 128);
        ToneMapEnabled() = false;   // the process-wide switch still wins
        Raster p; Fill(p, grey); ApplyScenePost(p, PlanScenePost(Neutral()));
        CHECK(Ch(Px(p, 1, 1), 0) == 128);
        ToneMapEnabled() = true;
    }
    // Grading: saturation 0 makes grey of colour; warm temperature raises red over blue; contrast spreads.
    {
        auto rs = Neutral(); rs.tonemap = false; rs.post.grading = true; rs.post.saturation = 0.0f;
        Raster r; Fill(r, 0xFF2040E0u);   // abgr: r=0xE0, g=0x40, b=0x20
        ApplyScenePost(r, PlanScenePost(rs));
        CHECK(Ch(Px(r, 2, 2), 0) == Ch(Px(r, 2, 2), 1) && Ch(Px(r, 2, 2), 1) == Ch(Px(r, 2, 2), 2));
        rs.post.saturation = 1.0f; rs.post.temperature = 1.0f;
        Raster w; Fill(w, grey); ApplyScenePost(w, PlanScenePost(rs));
        CHECK(Ch(Px(w, 2, 2), 0) > Ch(Px(w, 2, 2), 2));
        rs.post.temperature = 0.0f; rs.post.contrast = 2.0f;
        Raster c; Fill(c, 0xFFC0C0C0u); ApplyScenePost(c, PlanScenePost(rs));
        CHECK(Ch(Px(c, 2, 2), 0) > 0xC0);
        // Grading off: the numbers in the section are ignored.
        rs.post.grading = false;
        Raster g; Fill(g, 0xFFC0C0C0u); ApplyScenePost(g, PlanScenePost(rs));
        CHECK(Ch(Px(g, 2, 2), 0) == 0xC0);
    }
    // Vignette: corners darker than the centre, strength and smoothness matter, an empty (alpha 0) pixel gains coverage.
    {
        auto rs = Neutral(); rs.tonemap = false; rs.vignette = 1.0f;
        Raster r; Fill(r, grey); ApplyScenePost(r, PlanScenePost(rs));
        CHECK(Ch(Px(r, 32, 32), 0) >= Ch(Px(r, 0, 0), 0) + 30);
        CHECK(Ch(Px(r, 32, 32), 0) == 128);
        auto weak = rs; weak.vignette = 0.3f;
        Raster w; Fill(w, grey); ApplyScenePost(w, PlanScenePost(weak));
        CHECK(Ch(Px(w, 0, 0), 0) > Ch(Px(r, 0, 0), 0));
        auto sm = rs; sm.post.vignetteSmooth = 1.0f;
        Raster s; Fill(s, grey); ApplyScenePost(s, PlanScenePost(sm));
        CHECK(Ch(Px(s, 20, 20), 0) < Ch(Px(r, 20, 20), 0));   // a wider falloff reaches further in
        Raster e; Fill(e, 0u); ApplyScenePost(e, PlanScenePost(rs));
        CHECK((Px(e, 0, 0) >> 24) > 100);       // corner: black coverage added
        CHECK((Px(e, 32, 32) >> 24) == 0);      // centre stays empty
    }
    // A whole frame: RenderMeshes reads the scene and claims the vignette (the host then skips its overlay).
    {
        Scene s("frame");
        GameObject* go = s.CreateGameObject("Floor");
        go->AddComponent<MeshRenderer>()->mesh = Mesh::Plane(40.0f);
        const Vec3 eye{0, 8, 8};
        const Mat4 vp = Mat4::Perspective(70.0f, 1.0f, 0.1f, 100.0f) * Mat4::LookAt(eye, Vec3{0, 0, 0}, Vec3{0, 1, 0});
        ApplySceneLight(s);
        Raster plain; plain.Resize(64, 64); plain.Clear(0xFF000000u); RenderMeshes(plain, s, vp, eye);
        CHECK(!PostVignetteHandled());
        s.renderSettings.vignette = 1.0f; s.renderSettings.post.exposure = 1.0f;
        Raster v; v.Resize(64, 64); v.Clear(0xFF000000u); RenderMeshes(v, s, vp, eye);
        CHECK(PostVignetteHandled());
        CHECK(Ch(Px(v, 0, 63), 0) < Ch(Px(plain, 0, 63), 0));
        CHECK(Px(v, 32, 40) == Px(plain, 32, 40));
        s.renderSettings.vignette = 0.0f; s.renderSettings.post.exposure = 0.5f;
        Raster d; d.Resize(64, 64); d.Clear(0xFF000000u); RenderMeshes(d, s, vp, eye);
        CHECK(!PostVignetteHandled());
        CHECK(Ch(Px(d, 32, 40), 0) < Ch(Px(plain, 32, 40), 0));
        // Scene Bloom switch turns the glow on without the process switch, and restores the globals.
        s.renderSettings.post.exposure = 1.0f; s.renderSettings.post.bloom = true; s.renderSettings.post.bloomThreshold = 0.2f;
        Raster bl; bl.Resize(64, 64); bl.Clear(0xFF000000u); RenderMeshes(bl, s, vp, eye);
        CHECK(!BloomEnabled());
        CHECK(bl.color != plain.color);
        // Scene FXAA likewise, and the process switch stays as it was.
        s.renderSettings.post.bloom = false; s.renderSettings.post.fxaa = true;
        Raster fx; fx.Resize(64, 64); fx.Clear(0xFF000000u); RenderMeshes(fx, s, vp, eye);
        CHECK(!FXAAEnabled());
    }
    // ---- Depth of field (the scene's switch) -----------------------------------------
    // Distances from the W-buffer, scaled like the GPU depth image (past 150 units = sky).
    {
        CHECK_NEAR(DofPixelDistance(1.0f / 10.0f), 10.0f, 1e-3f);
        CHECK_NEAR(DofPixelDistance(0.0f), dof::Distance(1.0f, ssao::kFar), 1e-3f);          // nothing drawn = sky
        CHECK_NEAR(DofPixelDistance(1.0f / 400.0f), dof::Distance(1.0f, ssao::kFar), 1e-3f);  // past far = sky
        CHECK(DofPerspective(Mat4::Perspective(60.0f, 1.0f, 0.1f, 100.0f) * Mat4::LookAt(Vec3{0, 0, 5}, Vec3{}, Vec3{0, 1, 0})));
        CHECK(!DofPerspective(Mat4::Ortho(-5, 5, -5, 5, 0.1f, 100.0f)));
        // The ray scale turns view depth into straight-line distance (what the GPU depth image holds):
        // 1 on the view axis, sqrt(3) in the corner of a 90 degree square frustum.
        const DofRays rays = DofRaysFor(Mat4::Perspective(90.0f, 1.0f, 0.1f, 100.0f) * Mat4::LookAt(Vec3{3, 1, 2}, Vec3{0, 0, -4}, Vec3{0, 1, 0}));
        CHECK_NEAR(DofRayScale(rays, 0.0f, 0.0f), 1.0f, 1e-3f);
        CHECK_NEAR(DofRayScale(rays, 1.0f, 1.0f), std::sqrt(3.0f), 2e-3f);
        CHECK_NEAR(DofRayScale(rays, -1.0f, 0.0f), std::sqrt(2.0f), 2e-3f);
    }
    // A checkerboard: the left half near and in focus, the right half far away, a band of empty sky at the top
    // (wider than the blur's 16-pixel reach).
    {
        const int N = 64;
        Raster r; r.Resize(N, N);
        for (int y = 0; y < N; ++y)
            for (int x = 0; x < N; ++x) {
                const std::size_t i = (std::size_t)y * N + x;
                if (y < 24) { r.color[i] = 0x00000000u; r.depth[i] = 0.0f; continue; }   // sky: transparent, no depth
                r.color[i] = ((x / 2 + y / 2) & 1) ? 0xFFFFFFFFu : 0xFF000000u;
                r.depth[i] = x < N / 2 ? 1.0f / 5.0f : 1.0f / 60.0f;
            }
        const Raster before = r;
        const Mat4 pvp = Mat4::Perspective(60.0f, 1.0f, 0.1f, 300.0f) * Mat4::LookAt(Vec3{0, 0, 0}, Vec3{0, 0, -1}, Vec3{0, 1, 0});
        ApplySceneDof(r, 5.0f, 4.0f, pvp);   // sharp from 3 to 7 units (the near half is 5 to 6.5 along its rays)
        bool nearSame = true;
        for (int y = 24; y < N; ++y) for (int x = 0; x < N / 2; ++x) nearSame = nearSame && r.Get(x, y) == before.Get(x, y);
        CHECK(nearSame);                                          // in focus: bit for bit as it was
        int lo = 255, hi = 0;                                     // out of focus: the checks blur toward grey
        for (int y = 40; y < 60; ++y) for (int x = 40; x < 60; ++x) { lo = std::min(lo, Ch(r.Get(x, y), 0)); hi = std::max(hi, Ch(r.Get(x, y), 0)); }
        CHECK(hi - lo < 60);
        CHECK(lo > 60 && hi < 200);
        CHECK((r.Get(32, 2) >> 24) == 0);                         // sky well away from anything stays transparent
        CHECK((r.Get(50, 50) >> 24) == 255);                      // a far solid surface stays solid
        // Everything in focus: nothing changes at all.
        Raster f = before;
        ApplySceneDof(f, 30.0f, 200.0f, pvp);
        bool allSame = true;
        for (int y = 24; y < N; ++y) for (int x = 0; x < N; ++x) allSame = allSame && f.Get(x, y) == before.Get(x, y);
        CHECK(allSame);
    }
    // Through RenderMeshes: the scene switch blurs the far floor and leaves the focused part; an ortho camera is skipped.
    {
        Scene s("dof");
        GameObject* go = s.CreateGameObject("Floor");
        go->AddComponent<MeshRenderer>()->mesh = Mesh::Plane(200.0f);
        go->GetComponent<MeshRenderer>()->texture = "proc:checker";
        const Vec3 eye{0, 2, 0};
        const Mat4 vp = Mat4::Perspective(70.0f, 1.0f, 0.1f, 300.0f) * Mat4::LookAt(eye, Vec3{0, 0, -10}, Vec3{0, 1, 0});
        ApplySceneLight(s);
        Raster off; off.Resize(64, 64); off.Clear(0u); RenderMeshes(off, s, vp, eye);
        s.renderSettings.post.dof = true; s.renderSettings.post.dofFocus = 3.0f; s.renderSettings.post.dofRange = 2.0f;
        Raster on; on.Resize(64, 64); on.Clear(0u); RenderMeshes(on, s, vp, eye);
        CHECK(on.color != off.color);
        CHECK(on.Get(32, 63) == off.Get(32, 63));                 // the floor at the feet (about 2.5 units away) is in focus
        CHECK((on.Get(32, 2) >> 24) == 0);                        // the sky above the horizon stays empty
        const Mat4 ovp = Mat4::Ortho(-10, 10, -10, 10, 0.1f, 300.0f) * Mat4::LookAt(eye, Vec3{0, 0, -10}, Vec3{0, 1, 0});
        s.renderSettings.post.dof = false;
        Raster o1; o1.Resize(64, 64); o1.Clear(0u); RenderMeshes(o1, s, ovp, eye);
        s.renderSettings.post.dof = true;
        Raster o2; o2.Resize(64, 64); o2.Clear(0u); RenderMeshes(o2, s, ovp, eye);
        CHECK(o1.color == o2.color);
    }
    // ---- AO Power (post.ssaoPower) with the scene's ambient occlusion -----------------
    // A higher power darkens the occluded crease further and never brightens (power 1 = AoCurve unchanged).
    {
        Scene s("ao");
        s.CreateGameObject("Floor")->AddComponent<MeshRenderer>()->mesh = Mesh::Plane(20.0f);
        GameObject* box = s.CreateGameObject("Box");
        box->AddComponent<MeshRenderer>()->mesh = Mesh::Cube(2.0f);
        box->transform->localPosition = {0, 1, 0};
        const Vec3 eye{3, 3, 5};
        const Mat4 vp = Mat4::Perspective(60.0f, 1.0f, 0.1f, 100.0f) * Mat4::LookAt(eye, Vec3{0, 0.5f, 0}, Vec3{0, 1, 0});
        ApplySceneLight(s);
        s.renderSettings.post.ssao = true; s.renderSettings.post.ssaoRadius = 0.8f; s.renderSettings.post.ssaoIntensity = 1.2f;
        auto frame = [&](float power) {
            s.renderSettings.post.ssaoPower = power;
            Raster r; r.Resize(96, 96); r.Clear(0u); RenderMeshes(r, s, vp, eye);
            return r;
        };
        const float savedPower = SSAOPower();
        SSAOPower() = 1.0f;
        const Raster p1 = frame(1.0f);
        const Raster p3 = frame(3.0f);
        CHECK(p3.color != p1.color);
        long sum1 = 0, sum3 = 0; int darker = 0, brighter = 0;
        for (std::size_t i = 0; i < p1.color.size(); ++i) {
            const int a = Ch(p1.color[i], 1), b = Ch(p3.color[i], 1);
            sum1 += a; sum3 += b;
            if (b < a) ++darker;
            if (b > a) ++brighter;
        }
        CHECK(sum3 < sum1);            // a higher power only darkens (ao <= 1)
        CHECK(darker > 0); CHECK(brighter == 0);
        CHECK(SSAOPower() == 1.0f);    // the scene value does not leak into the process-wide one
        SSAOPower() = savedPower;
    }
    SceneLight::Reset();
    TEST_MAIN_RESULT();
}
