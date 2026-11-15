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
#include <cmath>
#include <vector>

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

Raster RenderBig(const Scene& s, int w, int h) {
    Raster r; r.Resize(w, h); r.Clear(0xFF2A2420u);
    const Vec3 eye{2.2f, 1.6f, 3.2f};
    const Mat4 vp = Mat4::Perspective(60.0f, (float)w / h, 0.1f, 100.0f) * Mat4::LookAt(eye, {0, -0.4f, 0}, Vec3::Up);
    RenderMeshes(r, s, vp, eye);
    return r;
}

// Largest difference of any colour channel between two frames.
int MaxChannelDiff(const Raster& a, const Raster& b) {
    int worst = 0;
    for (std::size_t i = 0; i < a.color.size() && i < b.color.size(); ++i)
        for (int sh = 0; sh < 24; sh += 8) {
            int d = (int)((a.color[i] >> sh) & 0xFF) - (int)((b.color[i] >> sh) & 0xFF);
            if (d < 0) d = -d;
            if (d > worst) worst = d;
        }
    return worst;
}

// Banding measure. The AO factor of a pixel is (red with AO) / (red without AO): AO only multiplies the colour.
// Walk away from the box's bottom edge (up the front wall, or down the floor in front of it), one row at a time,
// and average that factor over many columns. Real contact shadow only gets LIGHTER as you move away from the
// contact, so every step that gets DARKER again is a stripe. Returns the sum of those darkening steps (0 = no
// stripes) and, in maxStep, the biggest single row-to-row change.
struct Bands { double darkening = 0, maxStep = 0; int rows = 0; };
Bands MeasureBands(const Raster& off, const Raster& on, bool floorSide) {
    const int W = off.width;
    auto red = [](const Raster& r, int x, int y) { return (int)(r.color[(std::size_t)y * r.width + x] & 0xFF); };
    // Front wall of the box in the 640x480 view: its centre is near (290, 200); it is one flat colour without AO.
    const int wallRed = red(off, 290, 200);
    const int K = 40;
    std::vector<double> sum(K, 0.0); std::vector<int> cnt(K, 0);
    for (int x = 262; x <= 322 && x < W; ++x) {
        int y = 200;
        if (red(off, x, y) != wallRed) continue;
        while (y + 1 < off.height && red(off, x, y + 1) == wallRed) ++y;        // the wall's last row above the floor
        for (int k = 2; k < K; ++k) {                                          // skip the 2 rows at the edge itself
            const int yy = floorSide ? y + k : y - k;
            if (yy < 0 || yy >= off.height) break;
            const int o = red(off, x, yy);
            if (o <= 0) continue;
            if (!floorSide && o != wallRed) break;
            sum[k] += (double)red(on, x, yy) / o; ++cnt[k];
        }
    }
    Bands b; double prev = -1;
    for (int k = 2; k < K; ++k) {
        if (cnt[k] < 20) break;
        const double v = sum[k] / cnt[k];
        if (prev >= 0) {
            if (v < prev) b.darkening += prev - v;
            if (std::fabs(v - prev) > b.maxStep) b.maxStep = std::fabs(v - prev);
        }
        prev = v; ++b.rows;
    }
    return b;
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
    // The two formulas are equal on paper (1 - s*occ against 1 + (occ' - 1)*s) but round differently in floats,
    // so a channel may land one step apart: compare with a small tolerance, not exactly.
    const int procDiff = MaxChannelDiff(proc, on);
    std::printf("  process vs scene switch: largest channel difference %d\n", procDiff);
    CHECK(proc.color.size() == on.color.size());
    CHECK(procDiff <= 2);
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

    // Banding (review of docs/images/software_post/software_ssao_on.png: horizontal stripes on the box walls and
    // ghost lines on the floor). Moving away from the contact the AO may only get lighter; a run that darkens again
    // is a stripe. Measured on the 640x480 picture the docs show.
    {
        post.ssao = false;
        const Raster bigOff = RenderBig(s, 640, 480);
        post.ssao = true; post.ssaoRadius = 0.5f; post.ssaoIntensity = 0.8f; post.ssaoPower = 1.0f;
        const Raster bigOn = RenderBig(s, 640, 480);
        post.ssao = false;
        const Bands wall = MeasureBands(bigOff, bigOn, false);
        const Bands flr = MeasureBands(bigOff, bigOn, true);
        std::printf("  banding: wall %d rows, darkening %.4f, biggest step %.4f; floor %d rows, darkening %.4f, "
                    "biggest step %.4f\n", wall.rows, wall.darkening, wall.maxStep, flr.rows, flr.darkening, flr.maxStep);
        CHECK(wall.rows >= 20 && flr.rows >= 20);       // the measure really ran on the wall and the floor
        CHECK(wall.darkening < 0.004);                  // no dark/light/dark stripes up the wall
        CHECK(flr.darkening < 0.004);                   // no ghost lines on the floor
        CHECK(wall.maxStep < 0.02);                     // no hard steps between rows
        CHECK(flr.maxStep < 0.02);
    }
    TEST_MAIN_RESULT();
}
