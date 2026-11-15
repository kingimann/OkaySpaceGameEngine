// The scene's Post-processing Ambient Occlusion switch in the software renderer: with post.ssao on, RenderMeshes fills
// the G-buffer and runs ApplySSAO with the scene's radius, intensity and power. Off leaves the frame as before; the
// scene switch with the same settings gives the same frame as the process-wide switch; intensity 0 does nothing;
// a higher power darkens the contacts more (postq::ApplyPowered, shared with the GPU passes). Written for this engine.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Render/SoftwareRenderer.hpp"
#include "okay/Graphics/Image.hpp"
#include <cstdlib>
#include <cstring>
#include <string>
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

// OKAY_SHOT_DIR=<folder>: also render the scene bigger and save it as a PNG (the before/after pictures in the docs).
void Shot(const Scene& s, const char* name) {
    const char* dir = std::getenv("OKAY_SHOT_DIR");
    if (!dir || !*dir) return;
    const int w = 640, h = 480;
    Raster r; r.Resize(w, h); r.Clear(0xFF2A2420u);
    const Vec3 eye{2.2f, 1.6f, 3.2f};
    const Mat4 vp = Mat4::Perspective(60.0f, (float)w / h, 0.1f, 100.0f) * Mat4::LookAt(eye, {0, -0.4f, 0}, Vec3::Up);
    RenderMeshes(r, s, vp, eye);
    Image img(w, h);
    std::memcpy(img.Data(), r.color.data(), (std::size_t)w * h * 4);   // ABGR8888 words = RGBA bytes
    img.SavePNG(std::string(dir) + "/" + name);
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
    Shot(s, "software_ssao_off.png");
    post.ssao = true; post.ssaoRadius = 0.5f; post.ssaoIntensity = 0.8f; post.ssaoPower = 1.0f;
    const Raster on = Render(s);
    Shot(s, "software_ssao_on.png");
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

    // the settings the scene pass used are put back afterwards (the process values set above: radius 0.5)
    CHECK(SSAORadius() == 0.5f);
    CHECK(SSAOStrength() == 0.8f);
    CHECK(SSAOPower() == 1.0f);
    CHECK(!SSAOSceneFormula());

    // review: the process-wide switch is NOT capped at strength 1. The editor's "AO strength" slider goes to 2 and
    // the original formula (1 - strength * occlusion) darkens more above 1; with the cap, 1.5 looked like 1.0.
    post.ssao = false;
    SSAOEnabled() = true; SSAORadius() = 0.5f;
    SSAOStrength() = 1.0f;
    const Raster s10 = Render(s);
    SSAOStrength() = 1.5f;
    const Raster s15 = Render(s);
    SSAOEnabled() = false; SSAOStrength() = 0.6f;
    std::printf("  process AO: strength 1.0 %.0f, strength 1.5 %.0f\n", Brightness(s10), Brightness(s15));
    CHECK(Brightness(s15) < Brightness(s10));
    TEST_MAIN_RESULT();
}
