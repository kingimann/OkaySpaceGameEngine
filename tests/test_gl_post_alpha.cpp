#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Platform/Platform.hpp"
#include "gl_test_context.hpp"
#include "okay/Render/GLRenderer.hpp"
#include "okay/Render/SoftwareRenderer.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace okay;

// The OpenGL renderer's post chain keeps the frame's coverage (alpha). The editor and the player
// render with a transparent clear (alpha 0). When the GPU sky pass is off (a camera that clears to
// a solid colour, Sky off, an orthographic camera in the player, the Scene view's sky toggle) they
// paint the background behind the picture themselves, so a pixel no mesh covered must stay
// transparent after the post passes, or the background is replaced by a dark, opaque blur. Runs on a real GL context (Mesa's software GL under Xvfb, through
// tests/run_under_xvfb.sh); skips (77) without a display or GL.
namespace {
int A(std::uint32_t v) { return (int)(v >> 24); }
int Ch(std::uint32_t v, int k) { return (int)((v >> (k * 8)) & 0xFF); }
}

int main() {
    RUN_SUITE("gl_post_alpha");
    std::unique_ptr<platform::Window> win;
    if (const int skip = gltest::OpenContext("gl_post_alpha", 64, 64, win)) return skip;
    auto gl = std::make_unique<GLRenderer>();

    // A cube in focus in front of an empty (sky) background, seen from a perspective camera.
    Scene s("dof");
    GameObject* cube = s.CreateGameObject("Cube");
    cube->AddComponent<MeshRenderer>()->mesh = Mesh::Cube(2.0f);
    s.CreateGameObject("Sun")->AddComponent<Light>();
    const Vec3 eye{0, 0, 6};
    const Mat4 vp = Mat4::Perspective(60.0f, 1.0f, 0.1f, 100.0f) * Mat4::LookAt(eye, Vec3{0, 0, 0}, Vec3{0, 1, 0});
    s.Start(); s.Update(0.0f);
    ApplySceneLight(s);
    SkyPassEnabled() = false;   // as the hosts set it for a solid-colour camera: they paint the background
    const int W = 64, H = 64;
    auto render = [&](std::vector<std::uint32_t>& out) {
        const std::uint32_t* px = gl->RenderToPixels(s, vp, eye, W, H, 1, 0.0f, 0.0f, 0.0f, 0.0f);
        CHECK(px != nullptr);
        if (px) out.assign(px, px + (std::size_t)W * H);
        else out.assign((std::size_t)W * H, 0u);
    };

    // Post chain on (exposure), no depth of field: the corner stays empty, the cube is drawn.
    std::vector<std::uint32_t> plain, dof;
    s.renderSettings.post.exposure = 1.2f;
    render(plain);
    std::printf("  chain only: corner a=%d  centre a=%d rgb=%d,%d,%d\n", A(plain[0]), A(plain[32 * W + 32]),
                Ch(plain[32 * W + 32], 0), Ch(plain[32 * W + 32], 1), Ch(plain[32 * W + 32], 2));
    CHECK(A(plain[0]) == 0);
    CHECK(A(plain[32 * W + 32]) == 255);

    // Depth of field on, focused on the cube: the sky (far away, fully blurred) must stay transparent.
    s.renderSettings.post.dof = true; s.renderSettings.post.dofFocus = 6.0f; s.renderSettings.post.dofRange = 2.0f;
    render(dof);
    std::printf("  dof: corner a=%d rgb=%d,%d,%d  centre a=%d rgb=%d,%d,%d\n", A(dof[0]), Ch(dof[0], 0), Ch(dof[0], 1), Ch(dof[0], 2),
                A(dof[32 * W + 32]), Ch(dof[32 * W + 32], 0), Ch(dof[32 * W + 32], 1), Ch(dof[32 * W + 32], 2));
    CHECK(A(dof[0]) <= 2);                               // was 255: the sky turned an opaque dark blur
    CHECK(A(dof[W - 1]) <= 2 && A(dof[(H - 1) * W]) <= 2);
    CHECK(A(dof[32 * W + 32]) == 255);                   // the cube in focus stays solid ...
    for (int k = 0; k < 3; ++k)                          // ... and keeps its colour
        CHECK(std::abs(Ch(dof[32 * W + 32], k) - Ch(plain[32 * W + 32], k)) <= 3);

    // Edge smoothing (the scene's "fxaa" switch; our own pass, kEdgeSmoothFrag, provenance audit A3). A turned cube
    // has slanted, stair-stepped silhouette edges: the pass changes pixels along them, leaves flat areas and the
    // empty background alone, adds no ringing (every value stays inside its neighbourhood's range) and makes the
    // edges less stepped (less total variation).
    {
        s.renderSettings.post.dof = false;
        cube->transform->localRotation = Quat::Euler(25.0f, 35.0f, 10.0f);
        s.Update(0.0f);
        std::vector<std::uint32_t> off, on;
        s.renderSettings.post.fxaa = false; render(off);
        s.renderSettings.post.fxaa = true;  render(on);
        auto lum = [](std::uint32_t v) { return (0.2126 * Ch(v, 0) + 0.7152 * Ch(v, 1) + 0.0722 * Ch(v, 2)) * A(v) / 255.0; };
        int changed = 0, ringing = 0;
        double tvOff = 0, tvOn = 0;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                const std::uint32_t a = off[y * W + x], b = on[y * W + x];
                bool diff = false;
                for (int k = 0; k < 4; ++k) diff = diff || std::abs((int)((a >> (k * 8)) & 0xFF) - (int)((b >> (k * 8)) & 0xFF)) > 6;
                changed += diff;
                for (int k = 0; k < 4; ++k) {          // inside the 5 x 5 range of the unsmoothed frame
                    int lo = 255, hi = 0;
                    for (int dy = -2; dy <= 2; ++dy)
                        for (int dx = -2; dx <= 2; ++dx) {
                            const int xx = x + dx, yy = y + dy;
                            if (xx < 0 || yy < 0 || xx >= W || yy >= H) continue;
                            const int c = (int)((off[yy * W + xx] >> (k * 8)) & 0xFF);
                            lo = std::min(lo, c); hi = std::max(hi, c);
                        }
                    const int c = (int)((b >> (k * 8)) & 0xFF);
                    if (c < lo - 2 || c > hi + 2) ++ringing;
                }
                if (x + 1 < W) { tvOff += std::fabs(lum(a) - lum(off[y * W + x + 1])); tvOn += std::fabs(lum(b) - lum(on[y * W + x + 1])); }
                if (y + 1 < H) { tvOff += std::fabs(lum(a) - lum(off[(y + 1) * W + x])); tvOn += std::fabs(lum(b) - lum(on[(y + 1) * W + x])); }
            }
        int partialOff = 0, partialOn = 0;                    // pixels part-covered at the silhouette (soft edge)
        for (int i = 0; i < W * H; ++i) { partialOff += A(off[i]) > 10 && A(off[i]) < 245; partialOn += A(on[i]) > 10 && A(on[i]) < 245; }
        std::printf("  edge smoothing: %d pixels changed, %d out of range, total variation %.0f -> %.0f, part-covered %d -> %d\n",
                    changed, ringing, tvOff, tvOn, partialOff, partialOn);
        CHECK(changed > 20);                                  // the silhouette was smoothed
        CHECK(changed < W * H / 4);                           // ... and only there
        CHECK(ringing == 0);
        CHECK(tvOn < tvOff * 0.98);
        CHECK(partialOn > partialOff + 40);                  // softer silhouette (measured 71 -> 151 on Mesa)
        CHECK(A(on[0]) == 0 && A(on[W - 1]) == 0);            // the empty background stays empty
        for (int k = 0; k < 3; ++k) CHECK(std::abs(Ch(on[32 * W + 32], k) - Ch(off[32 * W + 32], k)) <= 2);   // flat face untouched
    }

    gl->Destroy();
    gl.reset();
    win->DestroyGLContext();
    TEST_MAIN_RESULT();
}
