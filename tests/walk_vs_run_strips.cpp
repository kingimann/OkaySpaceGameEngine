// Pictures of the built-in character's stride (docs/walk_vs_run.md): a side view of one full stride in 8 poses, drawn by
// the engine's own software renderer, headless. These are software renders made on Linux, not screenshots of the
// Windows player: the poses are what the engine produces, the lighting and the camera are this tool's.
//
//   walk_vs_run_strips <outdir>
//       writes new_walk.png (the default walk speed), run.png (the default run speed) and sheet.png (both).
//   OKAY_OLD_GAIT=1 walk_vs_run_strips <outdir>
//       writes old_walk.png (what plain W looked like before: 4.5 m/s), old_run.png (Shift: 8.0 m/s) and sheet_old.png.
//   walk_vs_run_strips <outdir> <name> <speed m/s> <shift 0|1>
//       one strip of your own.
//
// The pose comes from the same path a new project uses (the Character with the default Animator graph), stepped the way
// a controller steps it. The 8 tiles are evenly spaced in time over one stride, starting at the left foot touching down.
// Written from the engine's own APIs (Raster, ImageWrite, Font8x8); the framing (a side camera, a ground with 0.5 m tick
// marks so the length of a step can be read off) is this file's own.
#include "walk_vs_run_measure.hpp"
#include "okay/Core/ImageWrite.hpp"
#include "okay/Graphics/Font.hpp"
#include "okay/Render/SoftwareRenderer.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace okay;
using namespace walkrun;

namespace {

constexpr int kTiles = 8;
constexpr int kTileW = 240, kTileH = 300;
constexpr int kLabelH = 36;

struct Rgba {
    int w = 0, h = 0;
    std::vector<std::uint8_t> px;   // RGBA
    Rgba() = default;
    Rgba(int w_, int h_, std::uint32_t abgr = 0xFFFFFFFFu) : w(w_), h(h_), px((std::size_t)w_ * h_ * 4) { Fill(abgr); }
    void Fill(std::uint32_t abgr) {
        for (std::size_t i = 0; i < (std::size_t)w * h; ++i) {
            px[i * 4 + 0] = (std::uint8_t)(abgr & 0xFF); px[i * 4 + 1] = (std::uint8_t)((abgr >> 8) & 0xFF);
            px[i * 4 + 2] = (std::uint8_t)((abgr >> 16) & 0xFF); px[i * 4 + 3] = 0xFF;
        }
    }
    void Blit(const Rgba& o, int x0, int y0) {
        for (int y = 0; y < o.h; ++y)
            for (int x = 0; x < o.w; ++x) {
                const int dx = x0 + x, dy = y0 + y;
                if (dx < 0 || dy < 0 || dx >= w || dy >= h) continue;
                for (int c = 0; c < 4; ++c) px[((std::size_t)dy * w + dx) * 4 + c] = o.px[((std::size_t)y * o.w + x) * 4 + c];
            }
    }
    void Rect(int x0, int y0, int rw, int rh, std::uint32_t abgr) {
        for (int y = y0; y < y0 + rh; ++y)
            for (int x = x0; x < x0 + rw; ++x) {
                if (x < 0 || y < 0 || x >= w || y >= h) continue;
                px[((std::size_t)y * w + x) * 4 + 0] = (std::uint8_t)(abgr & 0xFF);
                px[((std::size_t)y * w + x) * 4 + 1] = (std::uint8_t)((abgr >> 8) & 0xFF);
                px[((std::size_t)y * w + x) * 4 + 2] = (std::uint8_t)((abgr >> 16) & 0xFF);
            }
    }
    /// Text with the engine's 8x8 font, scaled by `s`.
    void Text(int x0, int y0, const std::string& t, int s, std::uint32_t abgr) {
        int cx = x0;
        for (char c : t) {
            for (int gy = 0; gy < 8; ++gy)
                for (int gx = 0; gx < 8; ++gx)
                    if (Font8x8::Pixel(c, gx, gy)) Rect(cx + gx * s, y0 + gy * s, s, s, abgr);
            cx += 8 * s;
        }
    }
};

Rgba FromRaster(const Raster& r) {
    Rgba o(r.width, r.height);
    for (std::size_t i = 0; i < (std::size_t)r.width * r.height; ++i) {
        const std::uint32_t v = r.color[i];
        o.px[i * 4 + 0] = (std::uint8_t)(v & 0xFF); o.px[i * 4 + 1] = (std::uint8_t)((v >> 8) & 0xFF);
        o.px[i * 4 + 2] = (std::uint8_t)((v >> 16) & 0xFF); o.px[i * 4 + 3] = 0xFF;
    }
    return o;
}

/// One pose, side view: the camera is on the body's right (+X) looking at it, so the character faces the right edge.
Rgba RenderPose(const Mesh& body, float shiftZ) {
    Scene s("tile");
    // Ground: a wide slab, with a dark tick every 0.5 m along the way the body goes (the Z axis).
    {
        GameObject* g = s.CreateGameObject("Ground");
        auto* mr = g->AddComponent<MeshRenderer>();
        mr->mesh = Mesh::Plane(40.0f);
        mr->color = Color::FromBytes(170, 180, 165);
    }
    for (int i = -12; i <= 12; ++i) {
        GameObject* t = s.CreateGameObject("Tick");
        t->transform->localPosition = {0.0f, 0.004f, -0.5f * i + shiftZ - std::fmod(shiftZ, 0.5f)};
        t->transform->localScale = {0.5f, 0.002f, 0.025f};
        auto* mr = t->AddComponent<MeshRenderer>();
        mr->mesh = Mesh::Cube(1.0f);
        mr->color = (i % 2 == 0) ? Color::FromBytes(60, 60, 70) : Color::FromBytes(120, 120, 130);
    }
    {
        GameObject* b = s.CreateGameObject("Body");
        b->transform->localPosition = {0.0f, 0.0f, shiftZ};
        auto* mr = b->AddComponent<MeshRenderer>();
        mr->mesh = body;
    }
    {
        GameObject* l = s.CreateGameObject("Sun");
        l->transform->localRotation = Quat::LookRotation(Vec3{0.35f, -0.75f, -0.25f}.Normalized(), Vec3{0, 0, 1});
        auto* lt = l->AddComponent<Light>();
        lt->type = Light::Type::Directional; lt->castShadows = true;
    }
    ApplySceneLight(s);
    const Vec3 eye{7.5f, 0.95f, shiftZ};
    const Mat4 vp = Mat4::Perspective(20.0f, (float)kTileW / (float)kTileH, 0.1f, 100.0f) *
                    Mat4::LookAt(eye, Vec3{0.0f, 0.92f, shiftZ}, Vec3{0, 1, 0});
    Raster r;
    r.Resize(kTileW, kTileH);
    r.Clear(0xFFF2EBE4u);   // a pale sky (ABGR)
    RenderMeshes(r, s, vp, eye);
    return FromRaster(r);
}

struct Strip {
    std::vector<Rgba> tiles;
    GaitStats stats;
    bool ok = false;
};

/// Drive a character at `speed`, find one full stride (left foot down to left foot down) and keep 8 poses of it.
Strip CaptureStrip(float speed, bool sprint, float walkSpeed, float runSpeed) {
    Strip out;
    Walker w(true);
    w.walkSpeed = walkSpeed; w.runSpeed = runSpeed;
    for (int i = 0; i < 180; ++i) w.Step(speed, sprint);                // settle
    std::vector<Mesh> frames; std::vector<float> lz; std::vector<int> leftDown;
    bool planted = false;
    const int N = 240;                                                  // 4 s: more than one stride at any speed here
    for (int i = 0; i < N; ++i) {
        w.Step(speed, sprint);
        frames.push_back(w.mr->mesh);
        lz.push_back(w.z);
        float h[2];
        if (!w.FootHeights(h)) continue;
        if (planted && h[0] > 0.025f) planted = false;
        else if (!planted && h[0] < 0.0125f) { planted = true; leftDown.push_back(i); }
    }
    if (leftDown.size() < 2) return out;
    const int i0 = leftDown[0], i1 = leftDown[1];
    for (int k = 0; k < kTiles; ++k) {
        const int f = i0 + (int)std::lround((double)(i1 - i0) * k / kTiles);
        out.tiles.push_back(RenderPose(frames[(std::size_t)f], lz[(std::size_t)f]));
    }
    out.stats = Measure(true, speed, sprint, walkSpeed, runSpeed, 6.0f, 2.0f);
    out.ok = out.stats.ok;
    return out;
}

bool SavePng(const std::string& path, const Rgba& img) {
    return ImageWrite::WritePNG(path, img.w, img.h, 4, img.px.data());
}

/// A labelled strip: a coloured title bar with the text, then the 8 tiles side by side.
Rgba Compose(const Strip& s, const std::string& title, std::uint32_t bar) {
    Rgba img(kTiles * kTileW, kLabelH + kTileH, 0xFFFFFFFFu);
    img.Rect(0, 0, img.w, kLabelH, bar);
    char sub[160];
    std::snprintf(sub, sizeof(sub), "%s   %.1f m/s   %.0f steps/min   step %.2f m   flight %.0f%%   bounce %.0f cm", title.c_str(), s.stats.speed,
                  s.stats.stepsPerMin, s.stats.stepLength, s.stats.flight * 100.0f, s.stats.bounce * 100.0f);
    img.Text(10, 10, sub, 2, 0xFFFFFFFFu);
    for (int k = 0; k < (int)s.tiles.size(); ++k) img.Blit(s.tiles[(std::size_t)k], k * kTileW, kLabelH);
    for (int k = 1; k < kTiles; ++k) img.Rect(k * kTileW, kLabelH, 1, kTileH, 0xFFC8C8C8u);   // thin dividers
    return img;
}

} // namespace

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : "walk_vs_run_pictures";
    std::printf("gait: %s\n", HumanoidMotion::OldGait() ? "OLD (OKAY_OLD_GAIT=1)" : "natural");
    FirstPersonController fp;            // the defaults a new project's player starts with
    const float walk = fp.walkSpeed, run = fp.runSpeed;
    std::printf("controller defaults: walk %.2f m/s, run %.2f m/s\n", walk, run);
    struct Job { std::string file, title; float speed; bool sprint; std::uint32_t bar; };
    std::vector<Job> jobs;
    if (argc >= 5) {
        jobs.push_back({std::string(argv[2]) + ".png", argv[2], (float)std::atof(argv[3]), std::atoi(argv[4]) != 0, 0xFF603C28u});
    } else if (HumanoidMotion::OldGait()) {
        jobs.push_back({"old_walk.png", "OLD walk (W)", walk, false, 0xFF2830B0u});
        jobs.push_back({"old_run.png", "OLD run (Shift)", run, true, 0xFF2830B0u});
    } else {
        jobs.push_back({"new_walk.png", "NEW walk (W)", walk, false, 0xFF3C8C2Cu});
        jobs.push_back({"run.png", "NEW run (Shift)", run, true, 0xFFB06030u});
    }
    std::vector<Rgba> rows;
    for (const Job& j : jobs) {
        Strip s = CaptureStrip(j.speed, j.sprint, walk, run);
        if (!s.ok || (int)s.tiles.size() != kTiles) { std::fprintf(stderr, "could not capture %s\n", j.file.c_str()); return 1; }
        Rgba img = Compose(s, j.title, j.bar);
        if (!SavePng(dir + "/" + j.file, img)) { std::fprintf(stderr, "could not write %s/%s (does the folder exist?)\n", dir.c_str(), j.file.c_str()); return 1; }
        std::printf("wrote %s/%s  (%dx%d)  %.1f m/s: %.0f steps/min, step %.2f m, flight %.0f%%, bounce %.1f cm\n", dir.c_str(), j.file.c_str(), img.w, img.h,
                    s.stats.speed, s.stats.stepsPerMin, s.stats.stepLength, s.stats.flight * 100.0f, s.stats.bounce * 100.0f);
        rows.push_back(std::move(img));
    }
    if (rows.size() > 1) {
        int h = 0;
        for (const Rgba& r : rows) h += r.h;
        Rgba sheet(rows[0].w, h, 0xFFFFFFFFu);
        int y = 0;
        for (const Rgba& r : rows) { sheet.Blit(r, 0, y); y += r.h; }
        const std::string name = HumanoidMotion::OldGait() ? "sheet_old.png" : "sheet.png";
        if (!SavePng(dir + "/" + name, sheet)) return 1;
        std::printf("wrote %s/%s\n", dir.c_str(), name.c_str());
    }
    return 0;
}
