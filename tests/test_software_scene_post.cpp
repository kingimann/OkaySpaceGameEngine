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
    SceneLight::Reset();
    TEST_MAIN_RESULT();
}
