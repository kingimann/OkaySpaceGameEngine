// Software ambient occlusion: banding and halo near a crease. Written for this engine.
//
// Seen in the headless pictures of the scene SSAO switch: stripes across the wall of a box just above the floor, and a
// grey shelf on the floor beside it that ended in a hard edge. The cause was the sample pattern: ApplySSAO looked at
// every crease from the same 8 points in the same directions, so the darkness near a crease was a staircase of 8 steps
// instead of a smooth fade. The fix turns the pattern per pixel (a 4x4 ordered tile of 16 angles) and blurs over exactly
// that tile (see ApplySSAO in SoftwareRenderer.hpp).
//
// What is measured (a box standing on a floor, 640x480, scene SSAO radius 0.5, intensity 0.8). Every pixel carries the
// ratio  lit brightness with SSAO on / lit brightness with SSAO off  = the occlusion that was applied (1 = untouched).
// The ratios are averaged in bins by world position (so 8-bit rounding does not matter) and read as a profile:
//   * wall profile: bins of height above the floor on the box's front face. The occlusion must get lighter as you go
//     up. "Ripple" is the sum of all the places where it gets darker again, divided by the total rise. A smooth fade
//     has ripple near 0; stripes make it large.
//   * floor profile: the same, in bins of distance from the box along the floor.
//   * flat parts: the wall well above the crease and the floor far from the box must stay at exactly 1 (no false
//     occlusion on a flat surface), and the variance of their per-bin means must be near 0 (the "per-row variance").
// OKAY_SHOT_DIR=<folder> (and optionally OKAY_SHOT_TAG=<name>, default "now") saves the zoomed frame and the zoomed
// occlusion map, which are the pictures in docs/render_status.md.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Render/SoftwareRenderer.hpp"
#include "okay/Graphics/Image.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace okay;

namespace {
const int W = 640, H = 480;
const float kRadius = 0.5f;

void Build(Scene& s) {
    GameObject* floor = s.CreateGameObject("Floor");
    floor->transform->localPosition = {0, -0.6f, 0};
    floor->transform->localScale = {8, 0.2f, 8};
    floor->AddComponent<MeshRenderer>()->color = Color::FromBytes(200, 200, 200);
    GameObject* box = s.CreateGameObject("Box");
    box->AddComponent<MeshRenderer>()->color = Color::FromBytes(200, 200, 200);   // a unit cube, bottom on the floor at y = -0.5
}

Raster Render(const Scene& s, double* ms = nullptr) {
    Raster r; r.Resize(W, H); r.Clear(0xFF000000u);
    const Vec3 eye{2.2f, 1.6f, 3.2f};
    const Mat4 vp = Mat4::Perspective(60.0f, (float)W / H, 0.1f, 100.0f) * Mat4::LookAt(eye, {0, -0.4f, 0}, Vec3::Up);
    const auto t0 = std::chrono::steady_clock::now();
    RenderMeshes(r, s, vp, eye);
    if (ms) *ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return r;
}

double Sum3(std::uint32_t v) { return (double)(v & 0xFF) + (double)((v >> 8) & 0xFF) + (double)((v >> 16) & 0xFF); }

// The occlusion applied at pixel i, or -1 when the pixel is too dark to tell.
double Ratio(const Raster& on, const Raster& off, std::size_t i) {
    const double d = Sum3(off.color[i]);
    return d > 30.0 ? Sum3(on.color[i]) / d : -1.0;
}

struct Profile {
    std::vector<double> mean;   // per bin, -1 when the bin has too few pixels
    double binWidth = 0;
};

// Mean ratio per bin of `coord` for the pixels `pick` accepts.
template <class Pick, class Coord>
Profile MakeProfile(const Raster& on, const Raster& off, double lo, double hi, double binWidth, Pick pick, Coord coord) {
    const int nb = (int)std::lround((hi - lo) / binWidth);
    std::vector<double> sum((std::size_t)nb, 0.0); std::vector<int> cnt((std::size_t)nb, 0);
    for (std::size_t i = 0; i < on.gvalid.size(); ++i) {
        if (!on.gvalid[i] || !pick(on.gpos[i], on.gnrm[i])) continue;
        const double c = coord(on.gpos[i]);
        if (c < lo || c >= hi) continue;
        const double ra = Ratio(on, off, i);
        if (ra < 0) continue;
        const int b = (int)((c - lo) / binWidth);
        sum[(std::size_t)b] += ra; ++cnt[(std::size_t)b];
    }
    Profile p; p.binWidth = binWidth; p.mean.assign((std::size_t)nb, -1.0);
    for (int b = 0; b < nb; ++b) if (cnt[(std::size_t)b] >= 8) p.mean[(std::size_t)b] = sum[(std::size_t)b] / cnt[(std::size_t)b];
    return p;
}

struct Shape { double rise = 0, ripple = 0, maxStep = 0, first = 0, last = 0; int bins = 0; };

// The profile should rise from its first bins to its last ones. Ripple = (sum of every drop from one bin to the next)
// / rise; maxStep = the biggest single step / rise.
Shape Analyse(const Profile& p) {
    Shape s;
    std::vector<double> v;
    for (double m : p.mean) if (m >= 0) v.push_back(m);
    s.bins = (int)v.size();
    if (v.size() < 8) return s;
    s.first = (v[0] + v[1]) * 0.5; s.last = (v[v.size() - 1] + v[v.size() - 2] + v[v.size() - 3]) / 3.0;
    s.rise = s.last - s.first;
    double drops = 0, step = 0;
    for (std::size_t i = 0; i + 1 < v.size(); ++i) {
        if (v[i + 1] < v[i]) drops += v[i] - v[i + 1];
        step = std::max(step, std::fabs(v[i + 1] - v[i]));
    }
    if (s.rise > 1e-6) { s.ripple = drops / s.rise; s.maxStep = step / s.rise; }
    return s;
}

// A flat part: the biggest distance of a bin's mean from 1, and the variance of the bin means.
void Flat(const Profile& p, double& maxDev, double& variance, int& bins) {
    maxDev = 0; variance = 0; bins = 0;
    double mean = 0;
    for (double m : p.mean) if (m >= 0) { mean += m; ++bins; maxDev = std::max(maxDev, std::fabs(1.0 - m)); }
    if (bins == 0) return;
    mean /= bins;
    for (double m : p.mean) if (m >= 0) variance += (m - mean) * (m - mean);
    variance /= bins;
}

// OKAY_SHOT_DIR: the zoomed frame and the zoomed occlusion map around the base of the box.
void Shot(const Raster& on, const Raster& off) {
    const char* dir = std::getenv("OKAY_SHOT_DIR");
    if (!dir || !*dir) return;
    const char* tagEnv = std::getenv("OKAY_SHOT_TAG");
    const std::string tag = tagEnv && *tagEnv ? tagEnv : "now";
    const int x0 = 215, y0 = 150, cw = 215, ch = 150, z = 3;   // the base of the box, 3x
    Image a(cw * z, ch * z), b(cw * z, ch * z);
    std::uint32_t* pa = (std::uint32_t*)a.Data(); std::uint32_t* pb = (std::uint32_t*)b.Data();
    for (int y = 0; y < ch * z; ++y)
        for (int x = 0; x < cw * z; ++x) {
            const std::size_t i = (std::size_t)(y0 + y / z) * W + (x0 + x / z);
            pa[(std::size_t)y * cw * z + x] = on.color[i];
            const double ra = Ratio(on, off, i);
            const double t = ra < 0 ? 0.0 : std::min(1.0, std::max(0.0, (ra - 0.6) / 0.4));   // 0.6 .. 1 stretched to black .. white
            const std::uint32_t g = (std::uint32_t)(t * 255.0 + 0.5);
            pb[(std::size_t)y * cw * z + x] = 0xFF000000u | (g << 16) | (g << 8) | g;
        }
    a.SavePNG(std::string(dir) + "/ssao_quality_" + tag + "_render.png");
    b.SavePNG(std::string(dir) + "/ssao_quality_" + tag + "_ao.png");
}
} // namespace

int main() {
    RUN_SUITE("software_ssao_quality");
    PerPixelLighting() = true;
    SSAOEnabled() = false; BloomEnabled() = false; FXAAEnabled() = false;
    Scene s("ssao quality");
    Build(s);
    auto& post = s.renderSettings.post;
    post.ssao = false;
    const Raster off = Render(s);
    post.ssao = true; post.ssaoRadius = kRadius; post.ssaoIntensity = 0.8f; post.ssaoPower = 1.0f;
    double aoMs = 0, offMs = 0;
    Render(s, &aoMs);                                    // one warm-up
    const Raster on = Render(s, &aoMs);
    post.ssao = false; Render(s, &offMs); post.ssao = true;
    std::printf("  time at %dx%d: with SSAO %.1f ms, without %.1f ms, so about %.1f ms for the occlusion pass\n", W, H, aoMs, offMs, aoMs - offMs);
    CHECK(on.color != off.color);
    Shot(on, off);

    // ---- the wall of the box (its +z face), by height above the floor
    auto wallPick = [](const Vec3& p, const Vec3& n) { return n.z > 0.9f && std::fabs(p.x) < 0.3f; };
    const Profile wall = MakeProfile(on, off, 0.0, 0.5, 0.02, wallPick, [](const Vec3& p) { return (double)p.y + 0.5; });
    const Shape ws = Analyse(wall);
    std::printf("  wall  : base %.3f -> %.3f at %.2f up (%d bins): ripple %.3f, biggest step %.3f of the rise\n",
                ws.first, ws.last, kRadius, ws.bins, ws.ripple, ws.maxStep);
    std::printf("  wall profile (height 0.01 .. 0.49 step 0.02):");
    for (double m : wall.mean) std::printf(" %.3f", m);
    std::printf("\n");
    CHECK(ws.bins >= 20);
    CHECK(ws.rise > 0.05);          // there is a real crease darkening at the base
    CHECK(ws.first < 0.95);
    CHECK(ws.ripple < 0.10);        // stripes made this 0.3 and more; a smooth fade is near 0

    // ---- the floor beside the box, by distance from the wall
    auto floorPick = [](const Vec3& p, const Vec3& n) { return n.y > 0.9f && p.y < -0.49f && p.z > 0.5f && std::fabs(p.x) < 0.3f; };
    const Profile flo = MakeProfile(on, off, 0.0, 0.5, 0.02, floorPick, [](const Vec3& p) { return (double)p.z - 0.5; });
    const Shape fs = Analyse(flo);
    std::printf("  floor : edge %.3f -> %.3f at %.2f out (%d bins): ripple %.3f, biggest step %.3f of the rise\n",
                fs.first, fs.last, kRadius, fs.bins, fs.ripple, fs.maxStep);
    std::printf("  floor profile (distance 0.01 .. 0.49 step 0.02):");
    for (double m : flo.mean) std::printf(" %.3f", m);
    std::printf("\n");
    CHECK(fs.bins >= 15);
    CHECK(fs.ripple < 0.06);        // the grey shelf (the fade stops, even dips, then drops off) made this 0.11; a smooth fade is near 0

    // ---- flat parts stay at exactly 1: the wall well above the crease, the floor well away from the box
    double dev = 0, var = 0; int nb = 0;
    const Profile wallTop = MakeProfile(on, off, 0.65, 0.95, 0.02, wallPick, [](const Vec3& p) { return (double)p.y + 0.5; });
    Flat(wallTop, dev, var, nb);
    std::printf("  flat wall (height 0.65 .. 0.95, %d bins): biggest distance from 1 = %.4f, variance of the bin means = %.2e\n", nb, dev, var);
    CHECK(nb >= 10);
    CHECK(dev < 0.01);
    CHECK(var < 1e-5);
    const Profile floorFar = MakeProfile(on, off, 0.7, 1.7, 0.05, floorPick, [](const Vec3& p) { return (double)p.z - 0.5; });
    Flat(floorFar, dev, var, nb);
    std::printf("  flat floor (0.7 .. 1.7 from the box, %d bins): biggest distance from 1 = %.4f, variance = %.2e\n", nb, dev, var);
    CHECK(nb >= 8);
    CHECK(dev < 0.01);
    CHECK(var < 1e-5);
    TEST_MAIN_RESULT();
}
