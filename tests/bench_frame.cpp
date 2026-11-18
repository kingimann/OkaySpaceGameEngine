// Frame-time benchmark (opt-in, NOT a test). Builds a fixed set of scenes, runs whole frames the way Play mode and the
// player do, and prints ms per frame split by phase, so a speed-up (or a slow-down) can be MEASURED and compared.
//
// What one frame is here (the same calls the player makes, minus the window and the GPU):
//   scene.Update(dt)           scripts, animation, physics, colliders, pointer pick   (split by the engine's profiler)
//   ApplySceneLight(scene)     gather the Light components for shading
//   RenderMeshesSS(...)        the CPU (software) renderer: culling, shadow maps, raster, post passes
//   RenderSceneUI(...)         the in-game UI pass, drawn onto a counting backend (only in the UI scene)
//
// Run:      bench_frame                         every scene, 100 frames each (after 30 warm-up frames), 640x360
//           bench_frame --list                  the scene names
//           bench_frame --scene cubes2000 --frames 300 --size 960x540
//           bench_frame --scene skinned100 --frames 20 --warmup 3     (short runs are what valgrind/callgrind wants)
// Output:   a table per scene (median, 95th percentile and max, in ms), then "BENCH" lines that are easy to paste into
//           docs/perf_log.md, then two hashes. The hashes are the identical-output check: a change that is only meant
//           to be faster must print the same `frame_hash` (the last picture) and `state_hash` (where every object is
//           after the run) as before it.
// Honest limits: CPU numbers from the software renderer on whatever machine runs it. No GPU, no window, no audio. Take the
// median of several runs; a shared machine is noisy by about 10 percent (the CPU-time column is less noisy than wall time).
// Written from scratch for this repository; it only calls the engine's own public API.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Render/SoftwareRenderer.hpp"
#include "okay/Render/ModelImport.hpp"
#include "okay/Physics/Broadphase.hpp"
#include "okay/Scene/ComponentCache.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/UI/UIBatcher.hpp"
#include "okay/UI/UIRenderer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

// Under `valgrind --tool=callgrind --collect-atstart=no` only the measured frames are counted (not the scene set-up).
// Outside valgrind these client requests do nothing.
#if defined(__has_include)
#if __has_include(<valgrind/callgrind.h>)
#include <valgrind/callgrind.h>
#define OKAY_BENCH_COLLECT_TOGGLE() CALLGRIND_TOGGLE_COLLECT
#endif
#endif
#ifndef OKAY_BENCH_COLLECT_TOGGLE
#define OKAY_BENCH_COLLECT_TOGGLE() ((void)0)
#endif

using namespace okay;
using namespace okay::ui2d;
using Clk = std::chrono::steady_clock;
using URect = okay::ui2d::Rect;   // okay::Rect also exists

namespace {

double MsSince(Clk::time_point t0) { return std::chrono::duration<double, std::milli>(Clk::now() - t0).count(); }

// CPU time of the whole process (all threads), in ms. Less sensitive to other jobs on the machine than the wall clock.
double CpuMs() {
    timespec ts{};
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
}

struct Lcg {   // our own tiny deterministic random source (the scenes must be identical on every run)
    std::uint64_t s;
    explicit Lcg(std::uint64_t seed) : s(seed) {}
    std::uint32_t Next() { s = s * 6364136223846793005ULL + 1442695040888963407ULL; return (std::uint32_t)(s >> 33); }
    float Unit() { return (float)(Next() & 0xFFFFFF) / 16777216.0f; }
    float Range(float a, float b) { return a + (b - a) * Unit(); }
};

std::uint64_t Fnv(std::uint64_t h, const void* data, std::size_t n) {
    const unsigned char* p = (const unsigned char*)data;
    for (std::size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}

// What a scene needs to be run: the scene, the camera and what to draw.
struct World {
    std::unique_ptr<Scene> scene;
    Vec3 eye{0, 30, 60}, target{0, 0, 0};
    bool ui = false;               // also run the in-game UI pass
    bool render3d = true;
    float dt = 1.0f / 60.0f;
    bool fixedStep = true;         // Play and the player run physics in fixed steps with interpolation
};

GameObject* AddMesh(Scene& s, const char* name, const Mesh& m, Vec3 pos, Color col, Vec3 scale = {1, 1, 1}) {
    GameObject* go = s.CreateGameObject(name);
    go->transform->localPosition = pos;
    go->transform->localScale = scale;
    auto* mr = go->AddComponent<MeshRenderer>();
    mr->mesh = m;
    mr->color = col;
    return go;
}

void AddCamera(World& w) {
    GameObject* cam = w.scene->CreateGameObject("Camera");
    cam->transform->localPosition = w.eye;
    // The camera looks along its local -Z (as the view matrix of a GL style camera does), and LookRotation points +Z.
    const Vec3 f = (w.target - w.eye).Normalized();
    cam->transform->localRotation = Quat::LookRotation(f * -1.0f, Vec3{0, 1, 0});
    cam->AddComponent<Camera>();
}

void AddSun(Scene& s, bool shadows) {
    GameObject* go = s.CreateGameObject("Sun");
    go->transform->localRotation = Quat::LookRotation(Vec3{0.3f, -1, 0.2f}.Normalized(), Vec3{0, 0, 1});
    auto* l = go->AddComponent<Light>();
    l->type = Light::Type::Directional;
    l->castShadows = shadows;
}

void AddGround(Scene& s, float half, bool physics) {
    GameObject* g = AddMesh(s, "Ground", Mesh::Cube(1.0f), {0, -0.5f, 0}, Color(0.35f, 0.5f, 0.3f), {half * 2, 1, half * 2});
    if (physics) {
        g->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
        g->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
    }
}

Color Tint(Lcg& r) { return Color(0.3f + 0.6f * r.Unit(), 0.3f + 0.6f * r.Unit(), 0.3f + 0.6f * r.Unit()); }

// ---- the scenes -------------------------------------------------------------------------------------------------------
// Counts scale with `scale` (1.0 = the numbers in the name) so a quick run is possible.
int Scaled(int n, float scale) { int v = (int)(n * scale + 0.5f); return v < 1 ? 1 : v; }

World MakeCubes(float scale) {   // 2,000 static cubes with colliders, one sun with cascaded shadows
    World w; w.scene = std::make_unique<Scene>("cubes"); w.eye = {0, 38, 62}; w.target = {0, 0, 0};
    Scene& s = *w.scene;
    Lcg rnd(1);
    AddGround(s, 120.0f, true);
    const int n = Scaled(2000, scale);
    Mesh cube = Mesh::Cube(1.0f);
    for (int i = 0; i < n; ++i) {
        float x = ((i % 50) - 25) * 2.6f, z = ((i / 50) - 20) * 2.6f;
        GameObject* g = AddMesh(s, "Cube", cube, {x, 0.5f + (i % 7) * 0.05f, z}, Tint(rnd));
        g->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
    }
    AddSun(s, true);
    AddCamera(w);
    return w;
}

World MakeBodies(float scale) {   // 500 dynamic boxes and spheres dropped onto a floor
    World w; w.scene = std::make_unique<Scene>("bodies"); w.eye = {0, 14, 26}; w.target = {0, 3, 0};
    Scene& s = *w.scene;
    Lcg rnd(2);
    AddGround(s, 60.0f, true);
    const int n = Scaled(500, scale);
    Mesh cube = Mesh::Cube(1.0f), ball = Mesh::Sphere(0.5f, 8, 12);
    for (int i = 0; i < n; ++i) {
        float x = ((i % 10) - 5) * 1.3f + rnd.Range(-0.2f, 0.2f), z = (((i / 10) % 10) - 5) * 1.3f + rnd.Range(-0.2f, 0.2f);
        float y = 1.0f + (i / 100) * 1.4f;
        const bool box = (i & 1) == 0;
        GameObject* g = AddMesh(s, box ? "Box" : "Ball", box ? cube : ball, {x, y, z}, Tint(rnd));
        if (box) g->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
        else g->AddComponent<SphereCollider3D>()->radius = 0.5f;
        g->AddComponent<Rigidbody3D>();
    }
    AddSun(s, true);
    AddCamera(w);
    return w;
}

std::string RigFile(const char* f) { return std::string(OKAY_SOURCE_DIR) + "/tests/assets/rigs/" + f; }

int AddCharacters(Scene& s, int n, float spacing, const char* file) {
    int made = 0;
    const int cols = (int)std::ceil(std::sqrt((double)n));
    for (int i = 0; i < n; ++i) {
        bool ok = false;
        GameObject* root = ImportModelScene(s, RigFile(file), &ok);
        if (!root) continue;
        root->transform->localPosition = {((i % cols) - cols / 2) * spacing, 0, ((i / cols) - cols / 2) * spacing};
        ++made;
    }
    return made;
}

World MakeSkinned(float scale) {   // 100 imported skinned characters playing a clip
    World w; w.scene = std::make_unique<Scene>("skinned"); w.eye = {0, 12, 26}; w.target = {0, 1, 0};
    Scene& s = *w.scene;
    AddGround(s, 60.0f, false);
    const int made = AddCharacters(s, Scaled(100, scale), 2.2f, "mixamo_t_walk_only.glb");
    if (made == 0) std::printf("  note: the rig file did not import; the scene is empty\n");
    AddSun(s, true);
    AddCamera(w);
    return w;
}

World MakeLights(float scale) {   // 64 point lights over 400 meshes (no local shadows: shading cost only)
    World w; w.scene = std::make_unique<Scene>("lights"); w.eye = {0, 30, 44}; w.target = {0, 0, 0};
    Scene& s = *w.scene;
    Lcg rnd(3);
    AddGround(s, 60.0f, false);
    Mesh ball = Mesh::Sphere(0.6f, 8, 12), cube = Mesh::Cube(1.0f);
    const int n = Scaled(400, scale);
    for (int i = 0; i < n; ++i) {
        float x = ((i % 20) - 10) * 3.0f, z = ((i / 20) - 10) * 3.0f;
        AddMesh(s, "Prop", (i & 1) ? ball : cube, {x, 0.6f, z}, Tint(rnd));
    }
    const int nl = Scaled(64, scale);
    for (int i = 0; i < nl; ++i) {
        GameObject* go = s.CreateGameObject("Lamp");
        go->transform->localPosition = {((i % 8) - 4) * 7.0f + 3.0f, 3.0f, ((i / 8) - 4) * 7.0f + 3.0f};
        auto* l = go->AddComponent<Light>();
        l->type = Light::Type::Point; l->range = 9.0f; l->intensity = 0.8f; l->castShadows = false;
        l->color = Color(0.6f + 0.4f * rnd.Unit(), 0.6f + 0.4f * rnd.Unit(), 0.6f + 0.4f * rnd.Unit());
    }
    AddSun(s, false);
    AddCamera(w);
    return w;
}

World MakeUI(float scale) {   // the UI bench's 500 widgets under a canvas in a 10,000 object scene
    World w; w.scene = std::make_unique<Scene>("ui"); w.render3d = false; w.ui = true; w.fixedStep = false;
    Scene& s = *w.scene;
    s.physicsEnabled = false;
    GameObject* cv = s.CreateGameObject("canvas");
    cv->AddComponent<Canvas>()->scaleMode = Canvas::ScaleMode::ConstantPixelSize;
    int made = 1;
    const int n = Scaled(500, scale);
    for (int i = 0; i < n; ++i) {
        GameObject* g = s.CreateGameObject("w");
        g->transform->SetParent(cv->transform, false);
        ++made;
        const float x = 10.0f + (i % 20) * 95.0f, y = 10.0f + (i / 20) * 42.0f;
        switch (i % 10) {
            case 0: case 1: { auto* p = g->AddComponent<UIPanel>(); p->position = Vec2{x, y}; p->size = Vec2{90, 38}; p->borderWidth = (i % 3 == 0) ? 1.0f : 0.0f; break; }
            case 2: case 3: { auto* b = g->AddComponent<UIButton>(); b->position = Vec2{x, y}; b->size = Vec2{90, 38}; b->label = (i % 4 == 0) ? "Start a new game" : "Play"; break; }
            case 4: case 5: { auto* t = g->AddComponent<TextRenderer>(); t->screenSpace = true; t->screenPos = Vec2{x, y}; t->pixelSize = 1.5f; t->text = (i % 4 == 1) ? "Score: 123456 points so far" : "HP 100"; break; }
            case 6: { auto* pb = g->AddComponent<UIProgressBar>(); pb->position = Vec2{x, y}; pb->size = Vec2{90, 12}; pb->value = 0.4f; break; }
            case 7: { auto* im = g->AddComponent<UIImage>(); im->position = Vec2{x, y}; im->size = Vec2{32, 24}; break; }
            case 8: { auto* tg = g->AddComponent<UIToggle>(); tg->position = Vec2{x, y}; tg->label = "Sound"; break; }
            default: { auto* sl = g->AddComponent<UISlider>(); sl->position = Vec2{x, y}; sl->size = Vec2{90, 16}; break; }
        }
    }
    for (; made < Scaled(10000, scale); ++made) s.CreateGameObject("prop");
    return w;
}

World MakeMixed(float scale) {   // a typical small game level: props, bodies, characters, a sun and a few lamps
    World w; w.scene = std::make_unique<Scene>("mixed"); w.eye = {0, 16, 30}; w.target = {0, 1, 0};
    Scene& s = *w.scene;
    Lcg rnd(4);
    AddGround(s, 80.0f, true);
    Mesh cube = Mesh::Cube(1.0f), ball = Mesh::Sphere(0.5f, 8, 12);
    const int props = Scaled(300, scale);
    for (int i = 0; i < props; ++i) {
        float x = ((i % 25) - 12) * 3.2f, z = ((i / 25) - 6) * 3.2f - 8.0f;
        GameObject* g = AddMesh(s, "Prop", cube, {x, 0.5f, z}, Tint(rnd), {1, 1 + (i % 3), 1});
        g->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
    }
    const int bodies = Scaled(100, scale);
    for (int i = 0; i < bodies; ++i) {
        GameObject* g = AddMesh(s, "Ball", ball, {((i % 10) - 5) * 1.2f, 2.0f + (i / 10) * 1.2f, 6.0f + rnd.Range(-0.2f, 0.2f)}, Tint(rnd));
        g->AddComponent<SphereCollider3D>()->radius = 0.5f;
        g->AddComponent<Rigidbody3D>();
    }
    AddCharacters(s, Scaled(20, scale), 2.5f, "mixamo_t_walk_only.glb");
    for (int i = 0; i < Scaled(8, scale); ++i) {
        GameObject* go = s.CreateGameObject("Lamp");
        go->transform->localPosition = {(i - 4) * 9.0f, 3.0f, 2.0f};
        auto* l = go->AddComponent<Light>();
        l->type = Light::Type::Point; l->range = 12.0f; l->castShadows = false;
    }
    AddSun(s, true);
    AddCamera(w);
    return w;
}

struct SceneDef { const char* name; const char* what; World (*make)(float); };
const SceneDef kScenes[] = {
    {"cubes2000",   "2,000 static cubes with colliders, sun with cascaded shadows", MakeCubes},
    {"bodies500",   "500 dynamic boxes and spheres falling and piling up",          MakeBodies},
    {"skinned100",  "100 imported skinned characters playing a clip",               MakeSkinned},
    {"lights64",    "64 point lights over 400 props",                               MakeLights},
    {"ui500",       "500 UI widgets in a 10,000 object scene (UI pass only)",       MakeUI},
    {"mixed",       "300 props, 100 bodies, 20 characters, 8 lamps and a sun",      MakeMixed},
};

// ---- measuring --------------------------------------------------------------------------------------------------------
struct Series { std::vector<double> v; void Add(double x) { v.push_back(x); }
    double Pct(double p) const { if (v.empty()) return 0; std::vector<double> c = v; std::sort(c.begin(), c.end());
        std::size_t i = (std::size_t)(p * (double)(c.size() - 1) + 0.5); return c[i]; } };

// A counting painter: the UI pass draws onto it and no pixels are made.
struct CountingBackend : Painter {
    int drawCalls = 0, triangles = 0, uploads = 0, dummyTex = 0;
    bool clip = false; URect clipRect{0, 0, 0, 0};
    void SetDrawColor(std::uint8_t, std::uint8_t, std::uint8_t, std::uint8_t) override {}
    void FillRect(const URect*) override { ++drawCalls; }
    void DrawRect(const URect*) override { ++drawCalls; }
    void DrawLine(int, int, int, int) override { ++drawCalls; }
    void DrawPoint(int, int) override { ++drawCalls; }
    void SetClipRect(const URect* r) override { clip = r != nullptr; if (r) clipRect = *r; }
    void GetClipRect(URect* r) override { *r = clipRect; }
    bool IsClipEnabled() override { return clip; }
    void SetScale(float, float) override {}
    Tex LoadTexture(const std::string&) override { return nullptr; }
    Tex FontAtlas(TtfFont*) override { return nullptr; }
    void QueryTexture(Tex, int* w, int* h) override { *w = *h = 0; }
    void SetTextureColorMod(Tex, std::uint8_t, std::uint8_t, std::uint8_t) override {}
    void SetTextureAlphaMod(Tex, std::uint8_t) override {}
    void Copy(Tex, const URect*, const URect*) override { ++drawCalls; }
    void CopyF(Tex, const URect*, const FRect*) override { ++drawCalls; }
    void CopyEx(Tex, const URect*, const URect*, double, const Point*, int) override { ++drawCalls; }
    void CopyExF(Tex, const URect*, const FRect*, double, const FPoint*, int) override { ++drawCalls; }
    void Geometry(Tex, const Vertex*, int, const int*, int ni) override { ++drawCalls; triangles += ni / 3; }
    Tex UploadAtlas(const Image&, int, Tex existing) override { ++uploads; return existing ? existing : &dummyTex; }
};

struct Options {
    std::string scene = "all";
    int frames = 100, warmup = 30, width = 640, height = 360;
    float scale = 1.0f;
    int shadows = 1;               // --shadows 0 switches the sun's shadow maps off (the software renderer spends most of a frame on them)
    int perPixel = 0;              // --perpixel 1: the per-pixel lighting path for every mesh (the default shades per vertex unless shadows are on)
    bool legacySw = false;         // --legacy-sw: the software renderer without this pass's shortcuts (the same pictures, slower)
    bool noSpan = false, noBandSkip = false, noLightCull = false;   // each shortcut alone
    std::string saveScenes;        // --save-scenes DIR: write the 3D bench scenes as .okayscene files (examples/perf) and stop
    std::string sceneFile;         // --scene-file PATH: benchmark a scene file instead of a built in scene
    bool legacyPhysics = false;    // --legacy-physics: all pairs of colliders and a plain Raycast per falling body, as before (the same motion, slower)
    bool noQueryCache = false;     // --no-query-cache: the physics steps walk the scene for every component list, as before
    bool list = false, quiet = false;
};

std::uint64_t StateHash(Scene& s) {   // where every object is (exact float bits): the "same behaviour" check
    std::uint64_t h = 1469598103934665603ULL;
    for (const auto& go : s.Objects()) {
        if (!go->transform) continue;
        Vec3 p = go->transform->Position();
        h = Fnv(h, &p, sizeof(p));
    }
    return h;
}

struct Result {
    std::map<std::string, Series> phase;
    std::uint64_t frameHash = 0, stateHash = 0;
    int objects = 0, drawn = 0, culled = 0;
};

World LoadWorld(const std::string& path) {   // a scene file: its own camera and lights; the first frames' contents decide the rest
    World w;
    w.scene = std::make_unique<Scene>("file");
    std::string err;
    if (!SceneSerializer::LoadFromFile(*w.scene, path, &err)) { std::fprintf(stderr, "could not load %s: %s\n", path.c_str(), err.c_str()); w.scene.reset(); return w; }
    w.render3d = true;
    if (Camera* cam = w.scene->FindObjectOfType<Camera>()) {
        w.eye = cam->transform->Position();
        w.target = w.eye + cam->transform->Forward() * -10.0f;   // (the camera looks along its local -Z, see AddCamera)
    }
    return w;
}

Result RunScene(const SceneDef& def, const Options& o) {
    Result res;
    World w = o.sceneFile.empty() ? def.make(o.scale) : LoadWorld(o.sceneFile);
    if (!w.scene) return res;
    Scene& s = *w.scene;
    if (w.fixedStep) s.UseFixedStepPhysics(true);
    ShadowsEnabled() = o.shadows != 0;
    PerPixelLighting() = o.perPixel != 0;
    // The software renderer's shortcuts (each only skips work that cannot change a pixel): on unless asked off, so a run
    // with and without them can be compared in one binary, and must print the same frame_hash.
    detail::TriangleSpanDisabled() = o.legacySw || o.noSpan;
    BandSkipEnabled() = !(o.legacySw || o.noBandSkip);
    LightCullingEnabled() = !(o.legacySw || o.noLightCull);
    ComponentCacheDisabled() = o.noQueryCache;
    broadphase::ForceBruteForce() = o.legacyPhysics;
    UICanvas::Set(1920.0f, 1080.0f);
    s.Start();
    res.objects = (int)s.Objects().size();

    const float aspect = (float)o.width / (float)o.height;
    Mat4 vp = Mat4::Perspective(60.0f, aspect, 0.1f, 400.0f) * Mat4::LookAt(w.eye, w.target, Vec3{0, 1, 0});
    if (s.mainCamera) {   // the camera object's own matrices, so the pointer pick in Update uses the same view
        const Mat4 camVp = s.mainCamera->ProjectionMatrix(aspect) * s.mainCamera->ViewMatrix();
        const Vec4 c = camVp * Vec4{w.target, 1.0f};
        if (c.w > 0.0f && std::fabs(c.x) < c.w && std::fabs(c.y) < c.w) vp = camVp;   // it sees the target: use it
        else std::printf("  note: the camera object does not look at the target; rendering with the plain look-at matrix\n");
    }

    Raster work;
    std::vector<std::uint32_t> down;
    CountingBackend backend;
    BatchPainter batch(backend);
    const std::uint32_t* px = nullptr;

    Profiler& prof = Prof();
    prof.enabled = true;

    for (int f = 0; f < o.warmup + o.frames; ++f) {
        if (f == o.warmup) OKAY_BENCH_COLLECT_TOGGLE();
        const double cpu0 = CpuMs();
        const auto t0 = Clk::now();
        prof.BeginFrame();
        double tUpdate = 0, tLight = 0, tRender = 0, tUi = 0, cUpdate = 0, cRender = 0, cUi = 0;
        {
            const auto t = Clk::now(); const double c = CpuMs();
            s.Update(w.dt);
            tUpdate = MsSince(t); cUpdate = CpuMs() - c;
        }
        if (w.render3d) {
            auto t = Clk::now();
            ApplySceneLight(s);
            tLight = MsSince(t);
            t = Clk::now(); const double c = CpuMs();
            px = RenderMeshesSS(work, down, s, vp, w.eye, o.width, o.height, 1, nullptr);
            tRender = MsSince(t); cRender = CpuMs() - c;
        }
        if (w.ui) {
            const auto t = Clk::now(); const double c = CpuMs();
            backend.drawCalls = 0; backend.triangles = 0;
            RenderSceneUI(s, batch, 1920, 1080);
            batch.Flush();
            tUi = MsSince(t); cUi = CpuMs() - c;
        }
        const double total = MsSince(t0);
        prof.EndFrame(total);
        const double cpu = CpuMs() - cpu0;
        if (f < o.warmup) continue;
        res.phase["frame (wall)"].Add(total);
        res.phase["frame (cpu, all threads)"].Add(cpu);
        res.phase["update"].Add(tUpdate);
        res.phase["light gather"].Add(tLight);
        res.phase["render 3d"].Add(tRender);
        res.phase["ui"].Add(tUi);
        res.phase["update (cpu)"].Add(cUpdate);
        res.phase["render 3d (cpu, all threads)"].Add(cRender);
        res.phase["ui (cpu)"].Add(cUi);
        // The engine's own profiler sections for this frame (self time, merged by name).
        const Profiler::Frame& pf = prof.FrameAt(prof.Count() - 1);
        std::map<std::string, double> sec;
        for (const auto& b : pf.buckets) sec[b.name] += b.self;
        double inUpdate = 0;
        for (const auto& kv : sec) {
            res.phase["  " + kv.first].Add(kv.second);
            if (kv.first.compare(0, 7, "Scripts") == 0 || kv.first.compare(0, 7, "Physics") == 0 || kv.first.compare(0, 5, "Scene") == 0) inUpdate += kv.second;
        }
        res.phase["  update: unlabelled"].Add(tUpdate - inUpdate > 0 ? tUpdate - inUpdate : 0);
    }
    OKAY_BENCH_COLLECT_TOGGLE();
    prof.enabled = false;

    if (px) res.frameHash = Fnv(1469598103934665603ULL, px, (std::size_t)o.width * o.height * 4);
    res.stateHash = StateHash(s);
    res.drawn = LastCullCounters().drawn;
    return res;
}

void Print(const SceneDef& def, const Options& o, const Result& r) {
    std::printf("\n== %s: %s ==\n", def.name, def.what);
    std::printf("   %d objects, %dx%d, %d frames after %d warm-up, scale %.2f, meshes drawn in the last frame: %d\n",
                r.objects, o.width, o.height, o.frames, o.warmup, o.scale, r.drawn);
    std::printf("   %-34s %10s %10s %10s\n", "phase (ms per frame)", "median", "p95", "max");
    for (const auto& kv : r.phase) {
        if (kv.second.v.empty()) continue;
        const double med = kv.second.Pct(0.5), p95 = kv.second.Pct(0.95), mx = kv.second.Pct(1.0);
        if (kv.first[0] == ' ' && mx < 0.005) continue;        // hide empty profiler sections
        if ((kv.first == "ui" || kv.first == "ui (cpu)") && mx == 0.0) continue;
        if (kv.first == "render 3d (cpu, all threads)" && mx == 0.0) continue;
        if (kv.first == "render 3d" && med == 0.0 && mx == 0.0) continue;
        if (kv.first == "light gather" && med == 0.0 && mx == 0.0) continue;
        std::printf("   %-34s %10.3f %10.3f %10.3f\n", kv.first.c_str(), med, p95, mx);
    }
    for (const auto& kv : r.phase) {   // machine readable rows: BENCH,scene,phase,median_ms,p95_ms
        if (kv.second.v.empty()) continue;
        const char* name = kv.first.c_str();
        while (*name == ' ') ++name;
        std::printf("BENCH,%s,%s,%.4f,%.4f\n", def.name, name, kv.second.Pct(0.5), kv.second.Pct(0.95));
    }
    std::printf("   frame_hash=%016llx state_hash=%016llx\n", (unsigned long long)r.frameHash, (unsigned long long)r.stateHash);
}

bool ParseArgs(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char* what) -> const char* { if (i + 1 >= argc) { std::fprintf(stderr, "%s needs a value\n", what); return nullptr; } return argv[++i]; };
        if (a == "--list") o.list = true;
        else if (a == "--scene") { const char* v = next("--scene"); if (!v) return false; o.scene = v; }
        else if (a == "--frames") { const char* v = next("--frames"); if (!v) return false; o.frames = std::atoi(v); }
        else if (a == "--warmup") { const char* v = next("--warmup"); if (!v) return false; o.warmup = std::atoi(v); }
        else if (a == "--shadows") { const char* v = next("--shadows"); if (!v) return false; o.shadows = std::atoi(v); }
        else if (a == "--perpixel") { const char* v = next("--perpixel"); if (!v) return false; o.perPixel = std::atoi(v); }
        else if (a == "--legacy-sw") o.legacySw = true;
        else if (a == "--no-span") o.noSpan = true;
        else if (a == "--no-query-cache") o.noQueryCache = true;
        else if (a == "--legacy-physics") o.legacyPhysics = true;
        else if (a == "--save-scenes") { const char* v = next("--save-scenes"); if (!v) return false; o.saveScenes = v; }
        else if (a == "--scene-file") { const char* v = next("--scene-file"); if (!v) return false; o.sceneFile = v; }
        else if (a == "--no-bandskip") o.noBandSkip = true;
        else if (a == "--no-lightcull") o.noLightCull = true;
        else if (a == "--scale") { const char* v = next("--scale"); if (!v) return false; o.scale = (float)std::atof(v); }
        else if (a == "--size") { const char* v = next("--size"); if (!v) return false; if (std::sscanf(v, "%dx%d", &o.width, &o.height) != 2) return false; }
        else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return false; }
    }
    if (const char* e = std::getenv("OKAY_BENCH_FRAMES")) o.frames = std::atoi(e);
    if (o.frames < 1 || o.warmup < 0 || o.width < 16 || o.height < 16 || o.scale <= 0.0f) return false;
    return true;
}

} // namespace

int main(int argc, char** argv) {
    Options o;
    if (!ParseArgs(argc, argv, o)) { std::fprintf(stderr, "usage: bench_frame [--list] [--scene NAME|all] [--frames N] [--warmup N] [--size WxH] [--scale F] [--shadows 0|1] [--perpixel 0|1] [--legacy-sw | --no-span --no-bandskip --no-lightcull] [--no-query-cache] [--legacy-physics] [--scene-file PATH] [--save-scenes DIR]\n"); return 2; }
    if (o.list) { for (const auto& d : kScenes) std::printf("%-12s %s\n", d.name, d.what); return 0; }
    Log::minLevel = Log::Level::Warning;
    if (!o.saveScenes.empty()) {   // examples/perf: the same scenes, as files the editor opens
        struct Out { const char* scene; const char* file; };
        const Out outs[] = {{"cubes2000", "many_colliders"}, {"bodies500", "falling_bodies"}, {"lights64", "many_lights"}};
        for (const Out& out : outs)
            for (const auto& d : kScenes)
                if (std::string(d.name) == out.scene) {
                    World w = d.make(1.0f);
                    const std::string path = o.saveScenes + "/" + out.file + ".okayscene";
                    std::string err;
                    const bool ok = SceneSerializer::SaveToFile(*w.scene, path, &err);
                    std::printf("%s %s %s\n", ok ? "wrote" : "FAILED", path.c_str(), err.c_str());
                }
        return 0;
    }
    int ran = 0;
    if (!o.sceneFile.empty()) {   // one scene from a file, reported under the name "file"
        static const SceneDef fileDef = {"file", "a scene file", nullptr};
        Print(fileDef, o, RunScene(fileDef, o));
        return 0;
    }
    for (const auto& d : kScenes) {
        if (o.scene != "all" && o.scene != d.name) continue;
        Print(d, o, RunScene(d, o));
        ++ran;
    }
    if (ran == 0) { std::fprintf(stderr, "no scene called '%s' (try --list)\n", o.scene.c_str()); return 2; }
    return 0;
}
