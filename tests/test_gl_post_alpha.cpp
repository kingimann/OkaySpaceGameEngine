#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Platform/Platform.hpp"
#include "okay/Render/GLRenderer.hpp"
#include "okay/Render/SoftwareRenderer.hpp"
#include <cstdio>
#include <cstdlib>
#include <memory>

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
    if (!std::getenv("DISPLAY")) { std::printf("SKIP: no $DISPLAY (run through tests/run_under_xvfb.sh)\n"); return 77; }
    platform::Backend* b = platform::Default();
    if (!b || !b->Init(platform::InitFlags{true, false, false})) { std::printf("SKIP: no window backend\n"); return 77; }
    platform::WindowDesc d;
    d.title = "gl_post_alpha"; d.width = 64; d.height = 64; d.opengl = true; d.hidden = true; d.resizable = false;
    auto win = b->MakeWindow(d);
    if (!win) { std::printf("SKIP: MakeWindow failed: %s\n", b->LastError().c_str()); return 77; }
    platform::GLAttributes gla;
    if (!win->CreateGLContext(gla)) { std::printf("SKIP: no GL context\n"); return 77; }
    if (!GLRenderer::LoadGL([](const char* n) -> void* { return platform::Default()->GLProcAddress(n); })) {
        std::printf("SKIP: GL entry points missing\n"); return 77;
    }
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

    gl->Destroy();
    gl.reset();
    win->DestroyGLContext();
    TEST_MAIN_RESULT();
}
