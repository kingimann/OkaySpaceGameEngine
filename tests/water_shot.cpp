// Headless pictures, example scenes and a speed benchmark for the water. Not a test: a tool.
//
//   water_shot --scenes <dir>   write examples/water/*.okayscene (lake, ocean, river, shore)
//   water_shot --shots  <dir>   render pictures with the SOFTWARE renderer (PNG, 640x360): the four scenes, the four sea
//                               states, the old look next to the new look, and the camera under the surface
//   water_shot --bench          print milliseconds per frame of the water for a 100 x 100 m surface at 48 / 96 / 192
// The pictures are made on the CPU with no window and no GPU. docs/water_visuals.md.
#include "water_scenes.hpp"
#include "okay/Render/SoftwareRenderer.hpp"
#include "okay/Render/Atmosphere.hpp"
#include "okay/Graphics/Image.hpp"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace okay;

namespace {
constexpr int kW = 640, kH = 360;

void SkyFill(Raster& r, const Scene& sc, float horizonRow) {
    Vec3 top, hor, bot;
    EffectiveSkyColors(sc.renderSettings, top, hor, bot);
    for (int y = 0; y < r.height; ++y) {
        const float t = std::min(1.0f, std::max(0.0f, (horizonRow - y) / std::max(1.0f, horizonRow)));
        const float k = std::pow(t, 0.7f);
        const Vec3 c = hor + (top - hor) * k;
        const std::uint32_t px = 0xFF000000u | ((std::uint32_t)std::min(255.0f, c.z * 255.0f) << 16) |
                                 ((std::uint32_t)std::min(255.0f, c.y * 255.0f) << 8) | (std::uint32_t)std::min(255.0f, c.x * 255.0f);
        for (int x = 0; x < r.width; ++x) r.color[(std::size_t)y * r.width + x] = px;
    }
}

Raster Shot(Scene& sc, float simSeconds, int fps = 30) {
    const int n = (int)(simSeconds * fps);
    for (int i = 0; i < n; ++i) sc.Update(1.0f / fps);
    Camera* cam = sc.mainCamera;
    const Vec3 eye = cam->transform->Position();
    const Vec3 fwd = cam->transform->Forward();
    Raster r;
    r.Resize(kW, kH);
    r.Clear(0xFFD8B890u);
    const float pitch = std::asin(std::max(-1.0f, std::min(1.0f, fwd.y)));
    SkyFill(r, sc, kH * (0.5f + pitch * 0.9f));
    ApplySceneLight(sc);
    const Mat4 proj = Mat4::Perspective(cam->fieldOfView, (float)kW / kH, 0.2f, 700.0f);
    RenderMeshes(r, sc, proj * Mat4::LookAt(eye, eye + fwd, Vec3{0, 1, 0}), eye, nullptr);
    return r;
}

bool Save(const Raster& r, const std::string& path) {
    Image img(r.width, r.height);
    for (int y = 0; y < r.height; ++y)
        for (int x = 0; x < r.width; ++x) {
            const std::uint32_t v = r.Get(x, y);
            img.SetPixel(x, y, Color((v & 0xFF) / 255.0f, ((v >> 8) & 0xFF) / 255.0f, ((v >> 16) & 0xFF) / 255.0f, 1.0f));
        }
    return img.SavePNG(path);
}

void Run(const std::string& dir, const char* name, void (*build)(Scene&), float seconds, void (*tweak)(Scene&) = nullptr) {
    Scene sc(name);
    build(sc);
    if (tweak) tweak(sc);
    Raster r = Shot(sc, seconds);
    const std::string p = dir + "/" + name + ".png";
    std::printf("%s %s\n", Save(r, p) ? "wrote" : "FAILED", p.c_str());
}

double Bench(int resolution, bool rich, bool lod) {
    Scene sc("bench");
    GameObject* g = sc.CreateGameObject("Water");
    auto* w = g->AddComponent<Water>();
    w->size = 100.0f; w->resolution = resolution; w->depth = 10.0f;
    if (rich) { w->ApplyPreset(3); w->size = 100.0f; w->resolution = resolution; w->lod = lod; w->interactive = false; }
    w->Apply();
    for (int i = 0; i < 4; ++i) sc.Update(1.0f / 60.0f);   // warm up
    const int frames = 40;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < frames; ++i) sc.Update(1.0f / 60.0f);
    const auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count() / frames;
}
} // namespace

int main(int argc, char** argv) {
    if (argc >= 3 && !std::strcmp(argv[1], "--scenes")) {
        const std::string d = argv[2];
        struct { const char* n; void (*b)(Scene&); } list[] = {
            {"lake", watersc::BuildLake}, {"ocean", watersc::BuildOcean}, {"river", watersc::BuildRiver}, {"shore", watersc::BuildShore}};
        int bad = 0;
        for (auto& e : list) {
            Scene sc(e.n);
            e.b(sc);
            const std::string p = d + "/" + e.n + ".okayscene";
            const bool ok = SceneSerializer::SaveToFile(sc, p);
            std::printf("%s %s\n", ok ? "wrote" : "FAILED", p.c_str());
            bad += ok ? 0 : 1;
        }
        return bad;
    }
    if (argc >= 3 && !std::strcmp(argv[1], "--shots")) {
        const std::string d = argv[2];
        Run(d, "lake", watersc::BuildLake, 3.0f);
        Run(d, "ocean_rough", watersc::BuildOcean, 3.0f);
        Run(d, "river", watersc::BuildRiver, 3.0f);
        Run(d, "shore", watersc::BuildShore, 3.0f);
        Run(d, "ocean_calm", watersc::BuildOcean, 3.0f, [](Scene& s) { for (auto* w : s.FindObjectsOfType<Water>()) w->SetSeaState(1); });
        Run(d, "ocean_storm", watersc::BuildOcean, 3.0f, [](Scene& s) { for (auto* w : s.FindObjectsOfType<Water>()) w->SetSeaState(4); });
        Run(d, "ocean_old_look", watersc::BuildOcean, 3.0f, [](Scene& s) {
            for (auto* w : s.FindObjectsOfType<Water>()) {
                w->realisticLook = false; w->lod = false; w->style = Water::WaveStyle::Classic; w->seaState = 0;
                w->waveHeight = 0.35f; w->waveLength = 9.0f; w->waveSpeed = 1.1f; w->color = Color::FromBytes(40, 110, 165, 165);
                w->opacity = 0.72f; w->ripples = 0.0f; w->Apply();
            }
        });
        Run(d, "underwater", watersc::BuildLake, 2.0f, [](Scene& s) {
            s.mainCamera->transform->localPosition = {-10.0f, -2.0f, -8.0f};
            s.mainCamera->transform->LookAt({10.0f, -2.5f, 6.0f}, {0, 1, 0});
        });
        return 0;
    }
    if (argc >= 2 && !std::strcmp(argv[1], "--bench")) {
        std::printf("Water cost for a 100 x 100 m surface (one core, Release; mesh build only, no drawing). Milliseconds per frame.\n");
        std::printf("%-10s %-14s %-14s %-18s\n", "resolution", "old (Classic)", "rich look", "rich look + rings");
        for (int res : {48, 96, 192})
            std::printf("%-10d %-14.3f %-14.3f %-18.3f\n", res, Bench(res, false, false), Bench(res, true, false), Bench(res, true, true));
        return 0;
    }
    std::printf("usage: water_shot --scenes <dir> | --shots <dir> | --bench\n");
    return 1;
}
