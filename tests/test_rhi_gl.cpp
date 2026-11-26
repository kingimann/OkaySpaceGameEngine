// The RHI's OpenGL backend against its Software backend, on a real GL context (Mesa's software GL
// under Xvfb, through tests/run_under_xvfb.sh); skips (77) without a display or GL.
//   * device, capabilities, texture upload and read-back
//   * the RHI test scene (triangle, indexed cube with depth and culling, textured quad, blending)
//     drawn by both backends: the pictures agree within a small tolerance (edges and 8-bit rounding)
//   * an OkayShader surface through the RHI on both backends
//   * the bloom pass, the first feature moved onto the RHI:
//       - the OpenGL renderer's Bloom (GLRenderer::RenderToPixels with Bloom on and no post chain)
//         equals the bloom pass run by the RHI's OpenGL device on the same frame, pixel for pixel
//       - and the Software device's bloom on that frame within a tolerance
//   OKAY_SHOT_DIR=<folder>          save rhi_gl.png, rhi_software_ref.png, rhi_bloom_*.png there
//   OKAY_RHI_BLOOM_DUMP=<file>      write the renderer's bloom frame (raw RGBA8)
//   OKAY_RHI_BLOOM_REF=<file>       compare the renderer's bloom frame with such a file, pixel for pixel
//                                   (used once to compare the old OpenGL bloom code with the RHI one)
#include "test_framework.hpp"
#include "rhi_test_scene.hpp"
#include "gl_test_context.hpp"
#include <Okay.hpp>
#include "okay/Platform/Platform.hpp"
#include "okay/Render/GLRenderer.hpp"
#include "okay/Render/SoftwareRenderer.hpp"
#include "okay/Render/RHI/BloomPass.hpp"
#include "okay/Render/RHI/SurfaceShader.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>

using namespace okay;

namespace {

// Rows top-down (the renderer's read-back) <-> bottom-up (RHI order).
std::vector<std::uint8_t> FlipRows(const std::uint8_t* px, int w, int h) {
    std::vector<std::uint8_t> out((std::size_t)w * h * 4);
    for (int y = 0; y < h; ++y) std::memcpy(&out[(std::size_t)y * w * 4], px + (std::size_t)(h - 1 - y) * w * 4, (std::size_t)w * 4);
    return out;
}

bool RunBloom(rhi::Device& dev, const std::vector<std::uint8_t>& frame, int w, int h, float thr, float inten, std::vector<std::uint8_t>& out) {
    rhi::BloomPass bloom;
    if (!bloom.Init(dev)) return false;
    rhi::TextureDesc td; td.width = w; td.height = h; td.renderTarget = true; td.initialData = frame.data();
    rhi::TextureHandle t = dev.CreateTexture(td);
    const bool ok = t && bloom.Apply(t, w, h, thr, inten) && dev.ReadTexture(t, out);
    dev.Destroy(t);
    return ok;
}

void Report(const char* what, const rhitest::Diff& d, std::size_t pixels) {
    std::printf("  %s: max %d, mean %.3f, pixels over the limit %d of %zu\n", what, d.maxDiff, d.mean, d.over, pixels);
}

} // namespace

int main() {
    RUN_SUITE("rhi_gl");
    std::unique_ptr<platform::Window> win;
    if (const int skip = gltest::OpenContext("rhi_gl", 64, 64, win)) return skip;
    const char* shots = std::getenv("OKAY_SHOT_DIR");
    auto gl = rhi::CreateOpenGLDevice();
    auto sw = rhi::CreateSoftwareDevice();
    CHECK(gl != nullptr);
    if (!gl) TEST_MAIN_RESULT();
    std::printf("  device: %s\n", gl->GetCaps().deviceName.c_str());
    CHECK(gl->GetBackend() == rhi::Backend::OpenGL && gl->GetCaps().maxTextureSize >= 1024);

    // ---- Upload and read back ----
    {
        std::uint8_t px[5 * 3 * 4];
        for (int i = 0; i < 60; ++i) px[i] = (std::uint8_t)(i * 4 + 1);
        rhi::TextureDesc td; td.width = 5; td.height = 3; td.renderTarget = true; td.initialData = px;
        rhi::TextureHandle t = gl->CreateTexture(td);
        std::vector<std::uint8_t> back;
        CHECK(t && gl->ReadTexture(t, back) && back.size() == 60 && std::memcmp(back.data(), px, 60) == 0);
        gl->Destroy(t);
        CHECK(gl->LiveResources() == 0);
    }

    // ---- The test scene on both backends ----
    {
        const int W = 128, H = 128;
        std::vector<std::uint8_t> a, s; std::string err;
        CHECK(rhitest::DrawTestScene(*gl, W, H, a, &err));
        if (!err.empty()) std::printf("  gl scene: %s\n", err.c_str());
        CHECK(rhitest::DrawTestScene(*sw, W, H, s, &err));
        CHECK(gl->LiveResources() == 0);
        const rhitest::Diff d = rhitest::Compare(a, s, 8);
        Report("scene OpenGL vs Software (limit 8)", d, (std::size_t)W * H);
        CHECK(d.mean < 1.0);
        CHECK(d.over <= W * H / 50);   // only triangle edges may differ (2%)
        if (shots) {
            rhitest::SaveUpright(std::string(shots) + "/rhi_gl.png", a, W, H);
            rhitest::SaveUpright(std::string(shots) + "/rhi_software_ref.png", s, W, H);
        }
    }

    // ---- An OkayShader surface on both backends ----
    {
        oshader::Compiled c = oshader::Compile(
            "mode unlit; property color tint = (1.0, 0.8, 0.6); surface { float n = noise(uv * 6.0); albedo = vec3(uv.x, n, uv.y) * tint; }");
        rhi::ShaderDesc sd; std::string err;
        CHECK(c.ok && rhi::ShaderFromSurface(c, sd, &err));
        const Mat4 I; const float eye[3] = {0, 0, 3};
        const std::vector<std::uint8_t> u = rhi::SurfaceUniforms(I.m, I.m, 0.0f, eye, oshader::ResolveProperties(c, ""));
        std::vector<std::uint8_t> pics[2];
        CHECK(rhitest::DrawSurfaceQuad(*gl, sd, u, 64, 64, pics[0]));
        CHECK(rhitest::DrawSurfaceQuad(*sw, sd, u, 64, 64, pics[1]));
        const rhitest::Diff d = rhitest::Compare(pics[0], pics[1], 4);
        Report("surface OpenGL vs Software (limit 4)", d, 64 * 64);
        CHECK(d.over <= 64 * 64 / 100 && d.mean < 1.0);
        if (shots) {
            rhitest::SaveUpright(std::string(shots) + "/rhi_surface_gl.png", pics[0], 64, 64);
            rhitest::SaveUpright(std::string(shots) + "/rhi_surface_software.png", pics[1], 64, 64);
        }
    }

    // ---- The moved feature: the OpenGL renderer's Bloom ----
    {
        const int W = 96, H = 64;
        auto renderer = std::make_unique<GLRenderer>();
        Scene s("bloom");
        GameObject* cube = s.CreateGameObject("Glow");
        auto* mr = cube->AddComponent<MeshRenderer>();
        mr->mesh = Mesh::Cube(1.2f);
        mr->color = Color::FromBytes(255, 240, 200);
        mr->emissive = Color::FromBytes(255, 230, 160);
        GameObject* floor = s.CreateGameObject("Floor");
        auto* fr = floor->AddComponent<MeshRenderer>();
        fr->mesh = Mesh::Cube(1.0f);
        fr->color = Color::FromBytes(60, 70, 90);
        floor->transform->localPosition = Vec3{0, -1.2f, 0};
        floor->transform->localScale = Vec3{6, 0.2f, 6};
        s.CreateGameObject("Sun")->AddComponent<Light>();
        const Vec3 eye{0, 1.5f, 5};
        const Mat4 vp = Mat4::Perspective(55.0f, (float)W / (float)H, 0.1f, 100.0f) * Mat4::LookAt(eye, Vec3{0, 0, 0}, Vec3{0, 1, 0});
        s.Start(); s.Update(0.0f);
        ApplySceneLight(s);
        SkyPassEnabled() = false;
        const float thr = 0.55f, inten = 0.9f;
        BloomThreshold() = thr; BloomIntensity() = inten;
        auto frame = [&](bool bloomOn, std::vector<std::uint8_t>& out) {
            BloomEnabled() = bloomOn;
            const std::uint32_t* px = renderer->RenderToPixels(s, vp, eye, W, H, 1, 0.1f, 0.12f, 0.2f, 0.0f);
            CHECK(px != nullptr);
            out = px ? FlipRows(reinterpret_cast<const std::uint8_t*>(px), W, H) : std::vector<std::uint8_t>((std::size_t)W * H * 4, 0);
        };
        std::vector<std::uint8_t> withBloom, base;
        frame(false, base);
        frame(true, withBloom);
        BloomEnabled() = false;
        const rhitest::Diff changed = rhitest::Compare(withBloom, base, 0);
        std::printf("  renderer bloom changed %d pixels (max %d)\n", changed.over, changed.maxDiff);
        CHECK(changed.over > W * H / 20);   // the glow is visible

        std::vector<std::uint8_t> viaGl, viaSw;
        CHECK(RunBloom(*gl, base, W, H, thr, inten, viaGl));
        CHECK(RunBloom(*sw, base, W, H, thr, inten, viaSw));
        const rhitest::Diff exact = rhitest::Compare(withBloom, viaGl, 0);
        Report("renderer bloom vs RHI OpenGL bloom (exact)", exact, (std::size_t)W * H);
        CHECK(exact.over == 0);
        const rhitest::Diff ref = rhitest::Compare(withBloom, viaSw, 3);
        Report("renderer bloom vs RHI Software bloom (limit 3)", ref, (std::size_t)W * H);
        CHECK(ref.over <= W * H / 200 && ref.mean < 0.5);
        CHECK(gl->LiveResources() == 0 && sw->LiveResources() == 0);

        if (const char* dump = std::getenv("OKAY_RHI_BLOOM_DUMP")) {
            std::ofstream f(dump, std::ios::binary);
            f.write(reinterpret_cast<const char*>(withBloom.data()), (std::streamsize)withBloom.size());
        }
        if (const char* refFile = std::getenv("OKAY_RHI_BLOOM_REF")) {
            std::ifstream f(refFile, std::ios::binary);
            std::vector<std::uint8_t> old((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            const rhitest::Diff od = rhitest::Compare(withBloom, old, 0);
            Report("renderer bloom vs the saved frame (exact)", od, (std::size_t)W * H);
            CHECK(od.over == 0);
        }
        if (shots) {
            rhitest::SaveUpright(std::string(shots) + "/rhi_bloom_before.png", base, W, H);
            rhitest::SaveUpright(std::string(shots) + "/rhi_bloom_gl.png", withBloom, W, H);
            rhitest::SaveUpright(std::string(shots) + "/rhi_bloom_software.png", viaSw, W, H);
        }
        renderer->Destroy();
    }

    gl.reset();
    win->DestroyGLContext();
    TEST_MAIN_RESULT();
}
