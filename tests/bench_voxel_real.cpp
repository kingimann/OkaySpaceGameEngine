// Headless cost of the real-life digging options (docs/terrain_water.md): the whole Scene::Update per frame while a player
// holds the mouse, with the old digger, with the Realistic preset (store + debris + slump + collapse), with the debris cap
// raised so many bodies are alive, and the slump / loose-piece passes on their own. CPU only (no renderer, no GPU).
// Run: bench_voxel_real [frames]   Not part of ctest (it prints numbers).
#include "okay/Components/VoxelTerrain.hpp"
#include "okay/Components/VoxelDigger.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace okay;
using Clock = std::chrono::steady_clock;
static double NowMs() { return std::chrono::duration<double, std::milli>(Clock::now().time_since_epoch()).count(); }

struct Stats { double med = 0, p95 = 0, worst = 0; int debris = 0; };
static Stats Pct(std::vector<double> v) {
    Stats s; if (v.empty()) return s;
    std::sort(v.begin(), v.end());
    s.med = v[v.size() / 2]; s.p95 = v[(std::size_t)(v.size() * 0.95)]; s.worst = v.back();
    return s;
}

static Stats RunDig(int nx, int ny, int nz, float vs, int mode, int frames, int debrisMax) {
    Scene s("B"); s.physicsEnabled = true;
    GameObject* cam = s.CreateGameObject("Cam");
    auto* c = cam->AddComponent<Camera>(); c->main = true;
    GameObject* g = s.CreateGameObject("Terrain");
    auto* v = g->AddComponent<VoxelTerrain>();
    v->Resize(nx, ny, nz); v->voxelSize = vs; v->Generate(0.5f, 8.0f, 0.0f, 2024u);
    if (mode > 0) v->GenerateMaterials(2024u);
    v->Apply();
    auto* d = g->AddComponent<VoxelDigger>();
    d->radius = 2.5f; d->strength = 8.0f; d->showBrush = false;
    if (mode == 1) d->ApplyPreset(VoxelDigger::Preset::Realistic);
    if (mode == 2) { d->ApplyPreset(VoxelDigger::Preset::Realistic); d->debrisMax = debrisMax; d->power = 1.0e9f; d->strength = 30.0f; d->radius = 3.5f; }
    if (mode > 0) d->debrisMax = debrisMax;
    s.Update(0.0f);
    Stats out; std::vector<double> t;
    float sy = 0; v->SurfaceY(0, 0, sy);
    cam->transform->localRotation = Quat::LookRotation(Vec3{0, 1, 0}, Vec3{0, 0, -1});
    int peak = 0;
    for (int f = 0; f < frames; ++f) {
        const float a = f * 0.04f;
        cam->transform->localPosition = {std::cos(a) * 6.0f, sy + 20.0f, std::sin(a) * 6.0f};
        Input::FeedMouse({0, 0}, (f % 90) < 70 ? 1u : 0u);       // dig 70 frames, release 20 (collapse runs on release)
        const double t0 = NowMs();
        s.Update(1.0f / 60.0f);
        t.push_back(NowMs() - t0);
        peak = std::max(peak, VoxelDebris::Live());
    }
    Input::FeedMouse({0, 0}, 0u);
    out = Pct(t); out.debris = peak;
    return out;
}

int main(int argc, char** argv) {
    const int frames = argc > 1 ? std::atoi(argv[1]) : 270;
    struct Cfg { const char* name; int nx, ny, nz; float vs; } cfgs[] = {{"Sandbox 72x44x72 @1.5", 72, 44, 72, 1.5f}, {"large 160x64x160 @1.0", 160, 64, 160, 1.0f}};
    for (const Cfg& c : cfgs) {
        std::printf("== %s, %d frames, hold 70 / release 20\n", c.name, frames);
        const Stats a = RunDig(c.nx, c.ny, c.nz, c.vs, 0, frames, 48);
        std::printf("  old digger (vanish)                  median %.2f ms  p95 %.2f  worst %.2f\n", a.med, a.p95, a.worst);
        const Stats b = RunDig(c.nx, c.ny, c.nz, c.vs, 1, frames, 48);
        std::printf("  Realistic preset (debris cap 48)     median %.2f ms  p95 %.2f  worst %.2f  peak debris %d\n", b.med, b.p95, b.worst, b.debris);
        const Stats d = RunDig(c.nx, c.ny, c.nz, c.vs, 2, frames, 150);
        std::printf("  debris-heavy (cap 150, big brush)    median %.2f ms  p95 %.2f  worst %.2f  peak debris %d\n", d.med, d.p95, d.worst, d.debris);
    }
    // the passes alone
    {
        VoxelTerrain v; v.Resize(160, 64, 160); v.voxelSize = 1.0f; v.Generate(0.5f, 8.0f, 0.0f, 7u); v.GenerateMaterials(7u);
        float sy = 0; v.SurfaceY(0, 0, sy);
        std::vector<double> sl, ex;
        for (int i = 0; i < 40; ++i) {
            v.Edit({(float)(i % 5), sy - 1.0f, 0.0f}, 3.5f, 12.0f, VoxelTerrain::EditOptions{});
            double t0 = NowMs(); const int n = v.Slump(40); sl.push_back(NowMs() - t0); (void)n;
            t0 = NowMs(); v.ExtractFloaters(6); ex.push_back(NowMs() - t0);
        }
        const Stats a = Pct(sl), b = Pct(ex);
        std::printf("== passes on a 160x64x160 field (655k voxels), per stroke frame\n  Slump(40): median %.3f ms, worst %.3f ms\n  ExtractFloaters(6): median %.3f ms, worst %.3f ms\n", a.med, a.worst, b.med, b.worst);
    }
    // cave-in, budgeted generation, fluid and footprints
    {
        VoxelTerrain v; v.Resize(160, 64, 160); v.voxelSize = 1.0f; v.FillSlab(0.6f); v.EnableMaterials(4);
        for (int z = 0; z < v.nz; ++z) for (int y = 10; y < 18; ++y) for (int x = 60; x < 100; ++x) v.Set(x, y, z, -3.0f);   // a wide tunnel
        std::vector<double> t;
        for (int i = 0; i < 20; ++i) {
            v.Edit({(float)(i - 10), 14.0f, 0.0f}, 3.0f, 10.0f, VoxelTerrain::EditOptions{});
            const double t0 = NowMs(); const auto p = v.CaveIn(4, 6); t.push_back(NowMs() - t0); (void)p;
        }
        Stats a = Pct(t);
        std::printf("== CaveIn(4) after an edit on a 160x64x160 field: median %.3f ms, worst %.3f ms\n", a.med, a.worst);
    }
    {
        VoxelTerrain v; v.Resize(256, 64, 256); v.voxelSize = 1.0f;
        VoxelTerrain::LayerRules rules; rules.oreVeins = 40;
        const double w0 = NowMs(); v.Generate(0.5f, 8.0f, 0.3f, 9u); v.GenerateMaterials(9u, rules); const double oneShot = NowMs() - w0;
        VoxelTerrain b; b.Resize(256, 64, 256); b.voxelSize = 1.0f; b.BeginGenerate(0.5f, 8.0f, 0.3f, 9u, true, rules);
        std::vector<double> t; int steps = 0;
        for (;;) { const double t0 = NowMs(); const bool done = b.StepGenerate(2.0f); t.push_back(NowMs() - t0); ++steps; if (done) break; }
        const Stats a = Pct(t);
        std::printf("== generate 256x64x256 (4.2M voxels): one shot %.0f ms (one frame); budgeted 2 ms: %d steps, median %.2f ms, worst %.2f ms\n", oneShot, steps, a.med, a.worst);
    }
    {
        VoxelTerrain v; v.Resize(96, 48, 96); v.voxelSize = 1.0f; v.FillSlab(0.4f);
        v.Dig({0, 19.0f, 0}, 8.0f, 40.0f); v.fluidOn = true; v.fluidBudget = 600;
        v.PourFluid({0, 24.0f, 0}, 4000);
        std::vector<double> t; int moved = 0, n = 0;
        while (n < 5000 && v.FluidActiveCells() > 0) { const double t0 = NowMs(); const int m = v.FluidStep(); t.push_back(NowMs() - t0); moved += m; ++n; }
        const Stats a = Pct(t);
        std::printf("== fluid, 4000 cells pouring into a pit, budget 600/step: %d steps to rest, median %.3f ms, worst %.3f ms per step\n", n, a.med, a.worst);
    }
    // the world-edge catch (VoxelTerrain::catchFalling): one pass over every rigid body, done every sixth frame
    {
        Scene s("C"); s.physicsEnabled = false;
        GameObject* g = s.CreateGameObject("Terrain");
        auto* v = g->AddComponent<VoxelTerrain>(); v->Resize(72, 44, 72); v->voxelSize = 1.5f; v->FillSlab(0.5f);
        for (int i = 0; i < 300; ++i) { GameObject* b = s.CreateGameObject("B"); b->transform->localPosition = {(float)(i % 20), 30.0f, (float)(i / 20)}; b->AddComponent<Rigidbody3D>(); }
        s.Update(0.0f);
        std::vector<double> t;
        for (int i = 0; i < 50; ++i) { const double t0 = NowMs(); v->CatchFallers(); t.push_back(NowMs() - t0); }
        const Stats a = Pct(t);
        std::printf("== world-edge catch over 300 rigid bodies: median %.3f ms per pass (one pass every 6th frame)\n", a.med);
    }
    return 0;
}
