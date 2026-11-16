// Whole-frame cost of the voxel Sandbox, headless (no window, no GPU): idle, walking, looking around and digging, and what
// each part of the scene costs when it is taken away. It builds the New Project > Sandbox template exactly as the editor
// does, runs Scene::Update with the running game's input, and also times one CPU (software) render, which is what a machine
// without a working GPU falls back to.
//
// Run: bench_voxel_frame [frames]          Not part of ctest (it prints numbers).
#include "okay/Components/VoxelTerrain.hpp"
#include "okay/Components/VoxelDigger.hpp"
#include "okay/Components/Water.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Render/SoftwareRenderer.hpp"
#include "okay/Render/Culling.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Input/GameInput.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Scene/SceneTemplates.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace okay;
using Clock = std::chrono::steady_clock;
static double NowMs() { return std::chrono::duration<double, std::milli>(Clock::now().time_since_epoch()).count(); }

struct Pct { double med = 0, p95 = 0, worst = 0; };
static Pct P(std::vector<double> v) {
    Pct r; if (v.empty()) return r;
    std::sort(v.begin(), v.end());
    r.med = v[v.size() / 2]; r.p95 = v[(std::size_t)(v.size() * 0.95)]; r.worst = v.back();
    return r;
}

enum class Act { Idle, Walk, Look, Dig };
static const char* Name(Act a) { return a == Act::Idle ? "idle" : a == Act::Walk ? "walk" : a == Act::Look ? "look around" : "dig"; }

static void Count(Scene& s, long& tris, int& meshes, int& chunkObjs) {
    tris = 0; meshes = 0; chunkObjs = 0;
    for (auto& up : s.Objects()) {
        GameObject* g = up.get();
        if (!g || !g->active) continue;
        if (auto* mr = g->GetComponent<MeshRenderer>()) { tris += mr->mesh.TriangleCount(); if (mr->mesh.TriangleCount() > 0) ++meshes; }
        if (g->name.rfind("Voxel Chunk", 0) == 0) ++chunkObjs;
    }
}

struct Cfg { const char* name; int gx, gy, gz; float hill; unsigned seed; };

static Pct Run(const Cfg& c, Act act, int frames, bool noPhysics, bool noWater, bool noPlayer, bool noDigger) {
    Scene s("B");
    Templates::VoxelSandbox(s, c.gx, c.gy, c.gz, c.hill, c.seed);
    s.physicsEnabled = !noPhysics;
    s.runScripts = true;
    if (noWater) if (auto* g = s.Find("Water")) s.Destroy(g);
    if (noPlayer) { if (auto* g = s.Find("Player")) s.Destroy(g); }
    if (noDigger) if (auto* g = s.Find("Terrain")) if (auto* d = g->GetComponent<VoxelDigger>()) g->RemoveComponent(d);
    Input::FeedMouse({0, 0}, 0u); Input::FeedKeys({});
    for (int i = 0; i < 90; ++i) s.Update(1.0f / 60.0f);          // settle: the player lands, the water runs
    std::vector<double> t;
    for (int f = 0; f < frames; ++f) {
        std::vector<char> keys;
        unsigned mouse = 0;
        Vec2 pos{0, 0};
        if (act == Act::Walk) keys.push_back('w');
        if (act == Act::Look) pos = {(float)(f * 6 % 800), (float)(300 + (f % 40))};
        if (act == Act::Dig) mouse = 1u;
        if (act == Act::Dig) if (auto* cg = s.Find("FPS Camera")) {     // the player's camera, held 6 m above the ground looking down, sweeping a little
            if (cg->transform->Parent()) cg->transform->SetParent(nullptr, true);
            float sy = 0; if (auto* tg = s.Find("Terrain")) if (auto* tv = tg->GetComponent<VoxelTerrain>()) tv->SurfaceY(0, 0, sy);
            cg->transform->localPosition = {std::sin(f * 0.05f) * 4.0f, sy + 6.0f, std::cos(f * 0.05f) * 4.0f};
            cg->transform->localRotation = Quat::LookRotation(Vec3{0, 1, 0}, Vec3{0, 0, -1});
        }
        Input::FeedKeys(keys); Input::FeedMouse(pos, mouse);
        const double t0 = NowMs();
        s.Update(1.0f / 60.0f);
        t.push_back(NowMs() - t0);
    }
    Input::FeedKeys({}); Input::FeedMouse({0, 0}, 0u);
    if (act == Act::Dig) if (auto* g = s.Find("Terrain")) if (auto* v = g->GetComponent<VoxelTerrain>()) { const char* why = GameInputBlockReason(&s, static_cast<unsigned>(GameInputMask::World)); Vec3 lc; auto* dg = g->GetComponent<VoxelDigger>(); const bool aim = dg && dg->AimAtVoxel(v, lc); { auto* pg = s.Find("Player"); auto* cg2 = s.Find("FPS Camera"); if (pg && cg2) { const Vec3 pp = pg->transform->Position(), cp = cg2->transform->Position(), fw = cg2->transform->Forward(); std::printf("    (player at %.1f %.1f %.1f, camera at %.1f %.1f %.1f facing %.2f %.2f %.2f)\n", pp.x, pp.y, pp.z, cp.x, cp.y, cp.z, fw.x, fw.y, fw.z); } } std::printf("    (blocked by: %s; aim hits terrain: %d; main camera: %d)\n", why && *why ? why : "nothing", (int)aim, (int)(s.mainCamera != nullptr)); }
    if (act == Act::Dig) if (auto* g = s.Find("Terrain")) if (auto* v = g->GetComponent<VoxelTerrain>()) std::printf("    (dig run: %u edits reached the terrain, %d chunk objects, last rebuild %.2f ms)\n", (unsigned)v->editCount, v->ChunkObjectCount(), v->lastStats.totalMs);
    return P(t);
}

int main(int argc, char** argv) {
    const int frames = argc > 1 ? std::atoi(argv[1]) : 240;
    const Cfg cfgs[] = {{"Sandbox 72x44x72 (New Project > Sandbox)", 72, 44, 72, 9.0f, 2024u},
                        {"big world 128x56x128 (voxel_big_world)", 128, 56, 128, 12.0f, 77u}};
    for (const Cfg& c : cfgs) {
        std::printf("== %s\n", c.name);
        {
            Scene s("C"); Templates::VoxelSandbox(s, c.gx, c.gy, c.gz, c.hill, c.seed);
            long tris; int meshes, chunks; Count(s, tris, meshes, chunks);
            auto* v = s.Find("Terrain")->GetComponent<VoxelTerrain>();
            long tt = s.Find("Terrain")->GetComponent<MeshRenderer>()->mesh.TriangleCount();
            std::printf("  scene: %zu objects, %ld triangles in all, terrain %ld triangles (%d chunks of %d cubes), %d mesh objects\n",
                        s.Objects().size(), tris, tt, v->lastStats.chunksTotal, 16, meshes);
            // one software render, what a machine with no working GPU shows
            Raster r; r.Resize(960, 540); r.Clear(0xFF9CC4E8u);
            ApplySceneLight(s);
            const Vec3 eye{0, 40, 60};
            const Mat4 vp = Mat4::Perspective(60.0f, 16.0f / 9.0f, 0.1f, 600.0f) * Mat4::LookAt(eye, Vec3{0, 15, 0}, Vec3{0, 1, 0});
            std::vector<double> rt;
            for (int i = 0; i < 4; ++i) { const double t0 = NowMs(); RenderMeshes(r, s, vp, eye); rt.push_back(NowMs() - t0); }
            std::printf("  software render 960x540 (CPU fallback, overview camera): median %.0f ms per frame\n", P(rt).med);
            // from the player's eye, and with parts switched off
            float sy = 0; v->SurfaceY(0, 0, sy);
            const Vec3 eye2{0, sy + 1.62f, 0};
            const Mat4 vp2 = Mat4::Perspective(60.0f, 16.0f / 9.0f, 0.1f, 600.0f) * Mat4::LookAt(eye2, Vec3{0, sy + 1.0f, 30}, Vec3{0, 1, 0});
            auto timeIt = [&](const char* what, int w, int h) {
                Raster rr; rr.Resize(w, h); rr.Clear(0xFF9CC4E8u); std::vector<double> t2;
                for (int i = 0; i < 3; ++i) { const double t0 = NowMs(); RenderMeshes(rr, s, vp2, eye2); t2.push_back(NowMs() - t0); }
                std::printf("    player view %dx%d, %-28s %.0f ms\n", w, h, what, P(t2).med);
            };
            {   // the GPU hosts' half-resolution software depth pass for particles: it ran every frame once the digger's dust object existed
                Raster oc; std::vector<std::uint32_t> od;
                std::vector<double> ta, tb;
                for (int i = 0; i < 3; ++i) {
                    ShadowsEnabled() = true;  { const double t0 = NowMs(); RenderMeshesSS(oc, od, s, vp2, eye2, 640, 360, 1, nullptr); ta.push_back(NowMs() - t0); }
                    { const double t0 = NowMs(); FillOcclusionDepth(oc, od, s, vp2, eye2, 640, 360, nullptr); tb.push_back(NowMs() - t0); }
                }
                { std::vector<double> tc; for (int i = 0; i < 3; ++i) { const double t0 = NowMs(); FillOcclusionDepth(oc, od, s, vp2, eye2, 427, 240, nullptr); tc.push_back(NowMs() - t0); } std::printf("    ... at 427x240 (the cap now used): %.1f ms\n", P(tc).med); }
                std::printf("    GPU host's occlusion depth pass at 640x360: before (shadows on) %.0f ms, now (FillOcclusionDepth) %.0f ms\n", P(ta).med, P(tb).med);
            }
            timeIt("as the template ships", 960, 540);
            timeIt("as the template ships", 480, 270);
            ShadowsEnabled() = false; timeIt("shadows off", 960, 540); ShadowsEnabled() = true;
            for (int res : {2048, 1024, 512}) for (int casc : {3, 2, 1}) {
                ShadowMapResolution() = res; ShadowCascades() = casc;
                std::vector<double> ts; for (int i = 0; i < 3; ++i) { const double t0 = NowMs(); RenderShadowMap(s, vp2, eye2); ts.push_back(NowMs() - t0); }
                char nm[64]; std::snprintf(nm, sizeof(nm), "shadow map only %d x %d cascades", res, casc);
                std::printf("    %-40s %.0f ms\n", nm, P(ts).med);
            }
            ShadowMapResolution() = 2048; ShadowCascades() = 3;
            const bool fog = s.renderSettings.fog; s.renderSettings.fog = false; timeIt("fog off", 960, 540); s.renderSettings.fog = fog;
            if (auto* wg = s.Find("Water")) {
                wg->active = false; timeIt("water hidden", 960, 540); wg->active = true;
                if (auto* wm = wg->GetComponent<MeshRenderer>()) {
                    const bool rs = wm->receiveShadows; const float rf = wm->reflectivity, sp = wm->specular; const bool cs = wm->castShadows;
                    wm->receiveShadows = false; timeIt("water: no receive shadows", 960, 540); wm->receiveShadows = rs;
                    wm->reflectivity = 0.0f; timeIt("water: no reflection", 960, 540); wm->reflectivity = rf;
                    wm->specular = 0.0f; timeIt("water: no specular", 960, 540); wm->specular = sp;
                    wm->castShadows = false; timeIt("water: no cast shadows", 960, 540); wm->castShadows = cs;
                    wm->receiveShadows = false; wm->reflectivity = 0.0f; wm->specular = 0.0f; timeIt("water: all three off", 960, 540);
                    wm->receiveShadows = rs; wm->reflectivity = rf; wm->specular = sp;
                }
            }
            if (auto* tg = s.Find("Terrain")) { tg->active = false; timeIt("terrain hidden", 960, 540); tg->active = true; }
        }
        for (Act a : {Act::Idle, Act::Walk, Act::Look, Act::Dig}) {
            const Pct all = Run(c, a, frames, false, false, false, false);
            std::printf("  %-12s Scene::Update  median %.2f ms  p95 %.2f  worst %.2f\n", Name(a), all.med, all.p95, all.worst);
        }
        const Pct np = Run(c, Act::Idle, frames, true, false, false, false);
        const Pct nw = Run(c, Act::Idle, frames, false, true, false, false);
        const Pct npl = Run(c, Act::Idle, frames, false, false, true, false);
        const Pct nd = Run(c, Act::Idle, frames, false, false, false, true);
        const Pct bare = Run(c, Act::Idle, frames, true, true, true, true);
        std::printf("  idle, taking parts away: no physics %.2f ms | no water %.2f | no player %.2f | no digger %.2f | none of them %.2f\n", np.med, nw.med, npl.med, nd.med, bare.med);
    }
    return 0;
}
