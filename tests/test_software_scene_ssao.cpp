// The scene's Post-processing Ambient Occlusion switch in the software renderer: with post.ssao on, RenderMeshes fills
// the G-buffer and runs ApplySSAO with the scene's radius, intensity and power. Off leaves the frame as before; the
// scene switch with the same settings gives the same frame as the process-wide switch; intensity 0 does nothing;
// a higher power darkens the contacts more (postq::ApplyPowered, shared with the GPU passes). Written for this engine.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Render/SoftwareRenderer.hpp"
#include <cstdio>

using namespace okay;

namespace {
const int N = 128;

void Build(Scene& s) {
    GameObject* floor = s.CreateGameObject("Floor");
    floor->transform->localPosition = {0, -0.6f, 0};
    floor->transform->localScale = {8, 0.2f, 8};
    floor->AddComponent<MeshRenderer>()->color = Color::FromBytes(200, 200, 200);
    GameObject* box = s.CreateGameObject("Box");
    box->AddComponent<MeshRenderer>()->color = Color::FromBytes(200, 200, 200);
}

Raster Render(const Scene& s) {
    Raster r; r.Resize(N, N); r.Clear(0xFF000000u);
    const Vec3 eye{2.2f, 1.6f, 3.2f};
    const Mat4 vp = Mat4::Perspective(60.0f, 1.0f, 0.1f, 100.0f) * Mat4::LookAt(eye, {0, -0.4f, 0}, Vec3::Up);
    RenderMeshes(r, s, vp, eye);
    return r;
}

double Brightness(const Raster& r) {
    double sum = 0;
    for (std::uint32_t v : r.color) sum += (double)(v & 0xFF) + ((v >> 8) & 0xFF) + ((v >> 16) & 0xFF);
    return sum;
}
} // namespace

int main() {
    RUN_SUITE("software_scene_ssao");
    PerPixelLighting() = true;          // SSAO works on the per-pixel path (as with the process-wide switch)
    SSAOEnabled() = false; BloomEnabled() = false; FXAAEnabled() = false;
    Scene s("ao");
    Build(s);
    auto& post = s.renderSettings.post;

    post.ssao = false;
    const Raster off = Render(s);
    post.ssao = true; post.ssaoRadius = 0.5f; post.ssaoIntensity = 0.8f; post.ssaoPower = 1.0f;
    const Raster on = Render(s);
    std::printf("  light: off %.0f, scene AO on %.0f\n", Brightness(off), Brightness(on));
    CHECK(on.color != off.color);
    CHECK(Brightness(on) < Brightness(off));                                  // contacts darkened

    // the same settings through the process-wide switch: the same frame
    post.ssao = false;
    SSAOEnabled() = true; SSAORadius() = 0.5f; SSAOStrength() = 0.8f;
    const Raster proc = Render(s);
    SSAOEnabled() = false;
    CHECK(proc.color == on.color);
    CHECK(SSAORadius() == 0.5f && SSAOStrength() == 0.8f && SSAOPower() == 1.0f);   // the scene pass restored them

    // intensity 0: nothing changes
    post.ssao = true; post.ssaoIntensity = 0.0f;
    const Raster none = Render(s);
    CHECK(none.color == off.color);

    // a higher power darkens more
    post.ssaoIntensity = 0.8f; post.ssaoPower = 3.0f;
    const Raster p3 = Render(s);
    std::printf("  light: power 1 %.0f, power 3 %.0f\n", Brightness(on), Brightness(p3));
    CHECK(Brightness(p3) < Brightness(on));

    // the settings the scene pass used are put back afterwards
    CHECK(SSAORadius() == 0.45f || SSAORadius() == 0.5f);
    CHECK(SSAOPower() == 1.0f);
    TEST_MAIN_RESULT();
}
