// Depth of field in the software renderer (ApplyDof / ApplySceneDof in SoftwareRenderer.hpp) on a small synthetic
// frame: a fine checkerboard near the camera (left half) and far away (right half). The part inside the focus zone
// stays exactly as it was; the part outside is blurred toward its average; the transparent background does not darken
// a blurred edge; the scene switch gates it. Uses DofMath.hpp (the shared circle of confusion). Written for this engine.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Render/SoftwareRenderer.hpp"
#include "okay/Graphics/Image.hpp"
#include <cstdlib>
#include <cstring>
#include <string>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

using namespace okay;

namespace {
const int W = 64, H = 32;

Raster Frame(float nearDist, float farDist) {
    Raster r;
    r.Resize(W, H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const bool white = ((x / 2) + (y / 2)) % 2 == 0;
            const std::uint32_t v = white ? 255u : 0u;
            r.color[(std::size_t)y * W + x] = (255u << 24) | (v << 16) | (v << 8) | v;
            r.depth[(std::size_t)y * W + x] = 1.0f / (x < W / 2 ? nearDist : farDist);   // W-buffer: 1/w
        }
    return r;
}
// Mean and spread of the red channel over a column range (rows 8..23, away from the top and bottom edges).
void Stats(const Raster& r, int x0, int x1, float& mean, float& spread) {
    double s = 0, s2 = 0; int n = 0;
    for (int y = 8; y < 24; ++y)
        for (int x = x0; x < x1; ++x) {
            const float v = (float)(r.color[(std::size_t)y * W + x] & 0xFF);
            s += v; s2 += v * v; ++n;
        }
    mean = (float)(s / n);
    spread = (float)std::sqrt(std::fmax(0.0, s2 / n - (s / n) * (s / n)));
}
} // namespace

int main() {
    RUN_SUITE("software_dof");

    {   // focus on the near half: it stays sharp, the far half blurs toward grey
        Raster r = Frame(5.0f, 40.0f);
        const Raster before = r;
        ApplyDof(r, 5.0f, 2.0f);
        bool nearSame = true;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W / 2 - 1; ++x)
                if (r.color[(std::size_t)y * W + x] != before.color[(std::size_t)y * W + x]) nearSame = false;
        CHECK(nearSame);                                                  // coc 0: untouched, bit for bit
        float m0, s0, m1, s1;
        Stats(before, W / 2 + 4, W - 4, m0, s0);
        Stats(r, W / 2 + 4, W - 4, m1, s1);
        std::printf("  far half: spread %.1f -> %.1f, mean %.1f -> %.1f\n", s0, s1, m0, m1);
        CHECK(s0 > 100.0f);
        CHECK(s1 < s0 * 0.25f);                                           // blurred
        CHECK(std::fabs(m1 - m0) < 12.0f);                                // toward its own average
    }
    {   // focus on the far half instead: the reverse
        Raster r = Frame(5.0f, 40.0f);
        const Raster before = r;
        ApplyDof(r, 40.0f, 4.0f);
        float m0, s0, m1, s1;
        Stats(r, 4, W / 2 - 4, m1, s1);
        Stats(before, 4, W / 2 - 4, m0, s0);
        CHECK(s1 < s0 * 0.25f);
        bool farSame = true;
        for (int y = 0; y < H; ++y)
            for (int x = W / 2 + 1; x < W; ++x)
                if (r.color[(std::size_t)y * W + x] != before.color[(std::size_t)y * W + x]) farSame = false;
        CHECK(farSame);
    }
    {   // a pixel half way up the ramp is half way between sharp and blurred
        const float focus = 10.0f, range = 4.0f, d = 14.0f;              // dof::Coc = 0.5
        CHECK_NEAR(dof::Coc(d, focus, range), 0.5f, 1e-5f);
        Raster full = Frame(d, d), half = Frame(d, d);
        ApplyDof(full, focus, 0.001f);                                    // coc 1 everywhere (a tiny range)
        ApplyDof(half, focus, range);
        const Raster sharp = Frame(d, d);
        const std::size_t i = (std::size_t)16 * W + 20;
        const float vs = (float)(sharp.color[i] & 0xFF), vb = (float)(full.color[i] & 0xFF), vh = (float)(half.color[i] & 0xFF);
        std::printf("  ramp pixel: sharp %.0f, blurred %.0f, coc 0.5 gives %.0f\n", vs, vb, vh);
        CHECK(std::fabs(vh - (vs + vb) * 0.5f) <= 1.5f);
    }
    {   // the transparent background (depth 0 = sky, far away) next to an opaque far white block: the blurred edge
        // keeps the block's colour (premultiplied blur), only its coverage fades
        Raster r;
        r.Resize(W, H);
        for (auto& c : r.color) c = 0x00203040u;   // transparent background that still holds a dark colour
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                if (x >= 24 && x < 40) { r.color[(std::size_t)y * W + x] = 0xFFFFFFFFu; r.depth[(std::size_t)y * W + x] = 1.0f / 60.0f; }
        ApplyDof(r, 5.0f, 2.0f);
        const std::uint32_t edge = r.color[(std::size_t)16 * W + 22];    // just outside the block
        const std::uint32_t a = edge >> 24, red = edge & 0xFF;
        std::printf("  sky next to a blurred white block: alpha %u, red %u\n", a, red);
        CHECK(a > 10 && a < 250);                                         // partly covered now
        CHECK(red >= 250);                                                // still white, not darkened by the sky
    }
    {   // the scene switch: off leaves the frame alone; on blurs it
        Scene::RenderSettings rs;
        Raster r = Frame(5.0f, 40.0f);
        const Raster before = r;
        rs.post.dof = false;
        ApplySceneDof(r, rs);
        CHECK(r.color == before.color);
        rs.post.dof = true; rs.post.dofFocus = 5.0f; rs.post.dofRange = 2.0f;
        ApplySceneDof(r, rs);
        CHECK(r.color != before.color);
    }
    {   // everything in focus: the frame is left alone, bit for bit, and no blur work runs (the early out)
        Raster r = Frame(10.0f, 11.0f);
        const Raster before = r;
        const unsigned long long passes = DofBlurPasses();
        ApplyDof(r, 10.5f, 4.0f);
        CHECK(r.color == before.color);
        CHECK(r.depth == before.depth);
        CHECK(DofBlurPasses() == passes);
        Raster far = Frame(5.0f, 40.0f);                                  // and a frame with blur does count one pass
        ApplyDof(far, 5.0f, 2.0f);
        CHECK(DofBlurPasses() == passes + 1);
    }
    if (const char* dir = std::getenv("OKAY_SHOT_DIR"); dir && *dir) {   // before / after pictures for the docs
        Scene sc("dof shot");
        GameObject* floor = sc.CreateGameObject("Floor");
        floor->transform->localPosition = {0, -0.6f, -10}; floor->transform->localScale = {12, 0.2f, 40};
        floor->AddComponent<MeshRenderer>()->color = Color::FromBytes(170, 170, 170);
        GameObject* nearBox = sc.CreateGameObject("Near");
        nearBox->transform->localPosition = {-0.8f, 0, 0};
        nearBox->AddComponent<MeshRenderer>()->color = Color::FromBytes(60, 140, 230);
        for (int k = 0; k < 4; ++k) {
            GameObject* t = sc.CreateGameObject("Far");
            t->transform->localPosition = {-3.0f + 2.2f * k, 1.0f, -22.0f};
            t->transform->localScale = {1.2f, 3.0f, 1.2f};
            t->AddComponent<MeshRenderer>()->color = k % 2 ? Color::FromBytes(230, 70, 60) : Color::FromBytes(240, 220, 90);
        }
        const int w = 640, h = 400;
        const Vec3 eye{0, 0.6f, 4.0f};
        const Mat4 vp = Mat4::Perspective(55.0f, (float)w / h, 0.1f, 100.0f) * Mat4::LookAt(eye, {0, 0.3f, -5}, Vec3::Up);
        for (int on = 0; on < 2; ++on) {
            sc.renderSettings.post.dof = on != 0; sc.renderSettings.post.dofFocus = 4.0f; sc.renderSettings.post.dofRange = 3.0f;
            Raster r; r.Resize(w, h); r.Clear(0xFF2A2420u);
            RenderMeshes(r, sc, vp, eye);
            Image img(w, h);
            std::memcpy(img.Data(), r.color.data(), (std::size_t)w * h * 4);
            img.SavePNG(std::string(dir) + (on ? "/software_dof_on.png" : "/software_dof_off.png"));
        }
    }
    {   // once per frame: two renders of the same scene with depth of field on give the same picture (no compounding)
        Scene sc("dof");
        GameObject* nearBox = sc.CreateGameObject("Near");
        nearBox->AddComponent<MeshRenderer>()->color = Color::FromBytes(230, 230, 230);
        GameObject* farBox = sc.CreateGameObject("Far");
        farBox->transform->localPosition = {2.0f, 0.0f, -30.0f};
        farBox->transform->localScale = {6.0f, 6.0f, 1.0f};
        farBox->AddComponent<MeshRenderer>()->color = Color::FromBytes(220, 40, 40);
        sc.renderSettings.post.dof = true; sc.renderSettings.post.dofFocus = 4.0f; sc.renderSettings.post.dofRange = 2.0f;
        const Vec3 eye{0, 0, 4};
        const Mat4 vp = Mat4::Perspective(60.0f, 1.0f, 0.1f, 100.0f) * Mat4::LookAt(eye, {0, 0, 0}, Vec3::Up);
        Raster work; std::vector<std::uint32_t> out;
        const unsigned long long passes = DofBlurPasses();
        const std::uint32_t* p1 = RenderMeshesSS(work, out, sc, vp, eye, 64, 64, 1);
        const std::vector<std::uint32_t> first(p1, p1 + 64 * 64);
        const std::uint32_t* p2 = RenderMeshesSS(work, out, sc, vp, eye, 64, 64, 1);
        const std::vector<std::uint32_t> second(p2, p2 + 64 * 64);
        CHECK(first == second);
        CHECK(DofBlurPasses() == passes + 2);                             // exactly one pass per frame
    }
    {   // a 1080p frame, half near and half far: measured time (reported; a loose bound only, machines differ)
        Raster big;
        big.Resize(1920, 1080);
        for (int y = 0; y < 1080; ++y)
            for (int x = 0; x < 1920; ++x) {
                const std::uint32_t v = ((x / 3) + (y / 3)) % 2 ? 255u : 40u;
                big.color[(std::size_t)y * 1920 + x] = (255u << 24) | (v << 16) | (v << 8) | v;
                big.depth[(std::size_t)y * 1920 + x] = 1.0f / (x < 960 ? 5.0f : 40.0f);
            }
        ApplyDof(big, 5.0f, 2.0f);   // warm the reused buffers
        const auto t0 = std::chrono::steady_clock::now();
        const int frames = 5;
        for (int k = 0; k < frames; ++k) ApplyDof(big, 5.0f, 2.0f);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / frames;
        std::printf("  1920x1080 depth of field: %.1f ms per frame (%u hardware threads)\n", ms, std::thread::hardware_concurrency());
        // No check on the time: machines differ too much for a bound that could fail fairly. The number is reported
        // here and written into docs/render_status.md with the machine it came from.
        Raster calm = big;
        for (float& d : calm.depth) d = 1.0f / 5.0f;
        const auto t1 = std::chrono::steady_clock::now();
        ApplyDof(calm, 5.0f, 2.0f);
        const double msCalm = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
        std::printf("  1920x1080 all in focus (early out): %.2f ms\n", msCalm);
    }
    {   // tiny and empty frames are left alone and run no blur work
        const unsigned long long passes = DofBlurPasses();
        Raster t; t.Resize(1, 1);
        t.color[0] = 0xFF336699u; t.depth[0] = 1.0f / 40.0f;
        ApplyDof(t, 5.0f, 2.0f);
        CHECK(t.color[0] == 0xFF336699u);
        Raster e; e.Resize(4, 4); e.depth.clear();
        const std::vector<std::uint32_t> ec = e.color;
        ApplyDof(e, 5.0f, 2.0f);
        CHECK(e.color == ec);
        CHECK(DofBlurPasses() == passes);
    }
    TEST_MAIN_RESULT();
}
