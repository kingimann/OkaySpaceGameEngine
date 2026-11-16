// Headless benchmark for digging in the smooth voxel terrain: no window, no GPU.
//
// It drives the real Voxel Digger (a held left mouse button, a camera sweeping over the terrain, 60 frames a second,
// brush radius 2.5, strength 8 per second, like a player) and records what the CPU does per frame:
//   * the whole Scene::Update (digger + terrain + everything), and the Profiler rows Voxel.Edit / Voxel.Mesh /
//     Voxel.Upload that the editor's Profiler window shows;
//   * what the terrain handed the renderer (chunks and bytes), and a CPU replica of what the OpenGL renderer then does
//     with every mesh that changed (GLRenderer::EnsureMeshVB: the change check, the expansion to its interleaved vertex
//     layout and the exact content hash). The GPU upload itself and the driver are NOT measured: there is no GPU here.
//
// Run:  bench_voxel_edit [size 0..3|all] [reps] [frames]       sizes: 0 default 64x36x64, 1 Sandbox 72x44x72,
//                                                                2 large 160x64x160, 3 huge 256x64x256
// It runs every size three ways: the previous edit path with the previous renderer hash (what a player had before),
// the new edit path with the previous hash, and the new edit path with the faster hash (okay::meshhash).
// Not part of ctest (it prints numbers, it does not check them); tests/test_voxel_chunks.cpp checks the behaviour.
#include "okay/Components/VoxelTerrain.hpp"
#include "okay/Components/VoxelDigger.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Core/Profiler.hpp"
#include "okay/Render/MeshHash.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <numeric>
#include <string>
#include <vector>

using namespace okay;
using Clock = std::chrono::steady_clock;
static double NowMs() { return std::chrono::duration<double, std::milli>(Clock::now().time_since_epoch()).count(); }

// ---- CPU replica of the GL renderer's work on a changed mesh (engine/src/Render/GLRenderer.cpp) -------------
static bool g_newHash = true;   // true: okay::meshhash::Bytes (what the renderers use now); false: the byte-at-a-time loop they used before
static std::uint64_t FnvBytes(const void* data, std::size_t nbytes, std::uint64_t seed = 1469598103934665603ull) {
    if (g_newHash) return meshhash::Bytes(data, nbytes, seed);
    const unsigned char* p = (const unsigned char*)data; std::uint64_t h = seed;
    for (std::size_t i = 0; i < nbytes; ++i) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}
static std::uint64_t MeshSampleHash(const Mesh& m) {
    std::size_t sizes[5] = { m.vertices.size(), m.triangles.size(), m.normals.size(), m.uvs.size(), m.triColors.size() };
    std::uint64_t h = FnvBytes(sizes, sizeof(sizes));
    const std::size_t vn = m.vertices.size(), step = vn / 2048 + 1;
    if (g_newHash && step == 1) { if (vn) h = FnvBytes(m.vertices.data(), vn * sizeof(Vec3), h); }   // as the renderers do now
    else for (std::size_t i = 0; i < vn; i += step) h = FnvBytes(&m.vertices[i], sizeof(Vec3), h);
    const std::size_t tn = m.triangles.size(), tstep = tn / 2048 + 1;
    if (g_newHash && tstep == 1) { if (tn) h = FnvBytes(m.triangles.data(), tn * sizeof(int), h); }
    else for (std::size_t i = 0; i < tn; i += tstep) h = FnvBytes(&m.triangles[i], sizeof(int), h);
    return h;
}
static std::uint64_t MeshFullHash(const Mesh& m) {
    std::uint64_t h = 1469598103934665603ull;
    if (!m.vertices.empty())  h = FnvBytes(m.vertices.data(),  m.vertices.size()  * sizeof(Vec3),  h);
    if (!m.normals.empty())   h = FnvBytes(m.normals.data(),   m.normals.size()   * sizeof(Vec3),  h);
    if (!m.uvs.empty())       h = FnvBytes(m.uvs.data(),       m.uvs.size()       * sizeof(Vec2),  h);
    if (!m.triangles.empty()) h = FnvBytes(m.triangles.data(), m.triangles.size() * sizeof(int),   h);
    if (!m.triColors.empty()) h = FnvBytes(m.triColors.data(), m.triColors.size() * sizeof(Color), h);
    return h;
}
static std::vector<float> g_verts;
static std::size_t ExpandVerts(const Mesh& mesh) {   // the same arithmetic as GLRenderer::BuildExpandedVerts (14 floats a vertex)
    const auto& V = mesh.vertices; const auto& T = mesh.triangles;
    const bool hasN = mesh.HasNormals();
    const bool hasUV = !mesh.uvs.empty() && mesh.uvs.size() == V.size();
    const bool faceCols = mesh.HasFaceColors();
    const int nv = (int)V.size();
    g_verts.resize(T.size() / 3 * 3 * 14);
    float* out = g_verts.data();
    for (std::size_t i = 0; i + 2 < T.size(); i += 3) {
        int a = T[i], b = T[i + 1], c = T[i + 2];
        if (a < 0 || b < 0 || c < 0 || a >= nv || b >= nv || c >= nv) continue;
        Vec3 fn = Vec3::Cross(V[b] - V[a], V[c] - V[a]);
        { float m = fn.Magnitude(); fn = m > 1e-8f ? fn * (1.0f / m) : Vec3{0, 1, 0}; }
        const float ax = std::fabs(fn.x), ay = std::fabs(fn.y), az = std::fabs(fn.z);
        float cr = 1, cg = 1, cb = 1;
        if (faceCols) { const Color& fc = mesh.triColors[i / 3]; cr = fc.r; cg = fc.g; cb = fc.b; }
        const int idx[3] = {a, b, c};
        float uu[3], vv[3];
        for (int k = 0; k < 3; ++k) {
            const Vec3& p = V[idx[k]];
            if (hasUV) { uu[k] = mesh.uvs[idx[k]].x; vv[k] = mesh.uvs[idx[k]].y; }
            else if (ax >= ay && ax >= az) { uu[k] = p.z + 0.5f; vv[k] = p.y + 0.5f; }
            else if (ay >= ax && ay >= az) { uu[k] = p.x + 0.5f; vv[k] = p.z + 0.5f; }
            else                           { uu[k] = p.x + 0.5f; vv[k] = p.y + 0.5f; }
        }
        Vec3 e1 = V[b] - V[a], e2 = V[c] - V[a];
        float du1 = uu[1] - uu[0], dv1 = vv[1] - vv[0], du2 = uu[2] - uu[0], dv2 = vv[2] - vv[0];
        float det = du1 * dv2 - du2 * dv1;
        Vec3 tan = std::fabs(det) > 1e-8f ? (e1 * dv2 - e2 * dv1) * (1.0f / det) : e1;
        { float m = tan.Magnitude(); tan = m > 1e-6f ? tan * (1.0f / m) : Vec3{1, 0, 0}; }
        for (int k = 0; k < 3; ++k) {
            const Vec3& p = V[idx[k]];
            Vec3 n = hasN ? mesh.normals[idx[k]] : fn;
            out[0] = p.x; out[1] = p.y; out[2] = p.z; out[3] = n.x; out[4] = n.y; out[5] = n.z;
            out[6] = uu[k]; out[7] = vv[k]; out[8] = cr; out[9] = cg; out[10] = cb;
            out[11] = tan.x; out[12] = tan.y; out[13] = tan.z; out += 14;
        }
    }
    g_verts.resize((std::size_t)(out - g_verts.data()));
    return g_verts.size() * sizeof(float);
}

static double Median(std::vector<double> v) { if (v.empty()) return 0; std::sort(v.begin(), v.end()); return v[v.size() / 2]; }
static double Pct(std::vector<double> v, double p) { if (v.empty()) return 0; std::sort(v.begin(), v.end()); return v[std::min(v.size() - 1, (std::size_t)(p * v.size()))]; }

struct Cfg { const char* name; int nx, ny, nz; float vox; float surf, amp; unsigned seed; };

struct Result {
    std::vector<double> update, edit, mesh, upload, glExpand, glHash, glCheck, total, chunks, handed, tris;
    double bytesHandedMedian = 0, glUploadMedian = 0, idleHashAll = 0, idleHashMax = 0, applyIdle = 0;
    int meshes = 0, chunkObjects = 0;
};

static Result Run(const Cfg& c, bool oldPath, int frames, bool newHash) {
    g_newHash = newHash;
    VoxelTerrain::ForceOldRemesh(oldPath ? 1 : 0);
    Result r;
    Scene sc("bench"); sc.physicsEnabled = false;
    GameObject* go = sc.CreateGameObject("Terrain");
    auto* v = go->AddComponent<VoxelTerrain>();
    v->Resize(c.nx, c.ny, c.nz); v->voxelSize = c.vox;
    v->Generate(c.surf, c.amp, 0.0f, c.seed);
    v->Apply();
    auto* dig = go->AddComponent<VoxelDigger>();
    dig->radius = 2.5f; dig->strength = 8.0f; dig->showBrush = true;
    GameObject* camObj = sc.CreateGameObject("Camera");
    auto* cam = camObj->AddComponent<Camera>(); cam->main = true;
    sc.Update(0.0f);   // flush: components Awake / Start

    // The camera hangs 6 m above the surface and looks straight down: the brush follows it over the ground.
    auto place = [&](float x, float z) {
        float sy = 0.0f; v->SurfaceY(x, z, sy);
        camObj->transform->localPosition = {x, sy + 6.0f, z};
        camObj->transform->localRotation = Quat::LookRotation(Vec3{0, 1, 0}, Vec3{0, 0, -1});   // the digger aims along -Forward
    };
    float x = -4.0f, z = -2.0f;
    place(x, z);
    Prof().enabled = true;

    // meshes the renderer would see for this terrain, and the pointers it saw last frame
    auto meshesOf = [&]() {
        std::vector<const Mesh*> out;
        if (auto* mr = go->GetComponent<MeshRenderer>()) out.push_back(&mr->mesh);
        for (Transform* t : go->transform->Children())
            if (t && t->gameObject && t->gameObject->active && !t->gameObject->IsDestroyed())
                if (auto* mr = t->gameObject->GetComponent<MeshRenderer>()) out.push_back(&mr->mesh);
        return out;
    };
    std::map<const Mesh*, std::pair<const void*, std::size_t>> seen;
    for (const Mesh* m : meshesOf()) seen[m] = {m->vertices.data(), m->vertices.size()};

    const float dt = 1.0f / 60.0f;
    for (int f = 0; f < frames; ++f) {
        x += 2.0f * dt; z += 1.0f * dt;               // about 2.2 m/s
        place(x, z);
        Input::FeedMouse({0, 0}, 1u);                  // left button held: dig
        Prof().BeginFrame();
        const double t0 = NowMs();
        sc.Update(dt);
        const double t1 = NowMs();
        Prof().EndFrame(t1 - t0);
        const Profiler::Frame& pf = Prof().Displayed();
        double pe = 0, pm = 0, pu = 0;
        for (const auto& b : pf.buckets) {
            if (b.name == "Voxel.Edit") pe += b.self;
            else if (b.name == "Voxel.Mesh") pm += b.self;
            else if (b.name == "Voxel.Upload") pu += b.self;
        }
        // what the renderer would do for every mesh of the terrain this frame
        double check = 0, expand = 0, hash = 0, upBytes = 0, changed = 0;
        const std::vector<const Mesh*> ms = meshesOf();
        for (const Mesh* m : ms) {
            auto it = seen.find(m);
            const bool same = it != seen.end() && it->second.first == (const void*)m->vertices.data() && it->second.second == m->vertices.size();
            const double a = NowMs();
            volatile std::uint64_t sh = MeshSampleHash(*m); (void)sh;
            check += NowMs() - a;
            if (!same && !m->vertices.empty()) {
                const double b = NowMs(); upBytes += (double)ExpandVerts(*m); expand += NowMs() - b;
                const double d = NowMs(); volatile std::uint64_t fh = MeshFullHash(*m); (void)fh; hash += NowMs() - d;
                changed += 1;
            }
            seen[m] = {m->vertices.data(), m->vertices.size()};
        }
        if (f < 2) continue;   // the first frames convert / warm up
        r.update.push_back(t1 - t0); r.edit.push_back(pe); r.mesh.push_back(pm); r.upload.push_back(pu);
        r.glCheck.push_back(check); r.glExpand.push_back(expand); r.glHash.push_back(hash);
        r.total.push_back((t1 - t0) + check + expand + hash);
        r.chunks.push_back(v->lastStats.chunksRemeshed); r.handed.push_back((double)v->lastStats.bytesHandedOff);
        r.tris.push_back(0);
        r.glUploadMedian += upBytes / 1048576.0;
    }
    Input::FeedMouse({0, 0}, 0u);
    sc.Update(dt);   // release: the floater cleanup
    if (!r.update.empty()) r.glUploadMedian /= (double)r.update.size();
    r.bytesHandedMedian = Median(r.handed) / 1048576.0;
    r.meshes = (int)meshesOf().size();
    r.chunkObjects = v->ChunkObjectCount();
    // an idle Apply and the "exact check about once a second" a static mesh gets
    const double i0 = NowMs(); for (int k = 0; k < 20; ++k) v->Apply(); r.applyIdle = (NowMs() - i0) / 20.0;
    for (const Mesh* m : meshesOf()) {
        const double a = NowMs(); volatile std::uint64_t fh = MeshFullHash(*m); (void)fh; const double e = NowMs() - a;
        r.idleHashAll += e; r.idleHashMax = std::max(r.idleHashMax, e);
    }
    Prof().enabled = false;
    VoxelTerrain::ForceOldRemesh(-1);
    return r;
}

static void Print(const char* label, const Result& r) {
    std::printf("  %-34s update %6.3f (edit %5.3f mesh %5.3f upload %5.3f) | GL replica: check %5.3f expand %6.3f hash %6.3f | CPU total med %7.3f p95 %7.3f max %7.3f ms\n",
                label, Median(r.update), Median(r.edit), Median(r.mesh), Median(r.upload), Median(r.glCheck), Median(r.glExpand), Median(r.glHash),
                Median(r.total), Pct(r.total, 0.95), Pct(r.total, 1.0));
    std::printf("  %-34s meshes drawn %d | chunks re-meshed per frame %.1f | handed to renderer %.3f MB/frame, expanded for upload %.3f MB/frame | idle: Apply %.3f ms, exact hash of all %.3f ms (largest one %.3f)\n",
                "", r.meshes, Median(r.chunks), r.bytesHandedMedian, r.glUploadMedian, r.applyIdle, r.idleHashAll, r.idleHashMax);
}

int main(int argc, char** argv) {
    const std::vector<Cfg> cfgs = {
        {"default 64x36x64 @0.8 (the class defaults)", 64, 36, 64, 0.8f, 0.5f, 6.0f, 1u},
        {"Sandbox 72x44x72 @1.5 (the template)",        72, 44, 72, 1.5f, 0.55f, 9.0f, 2024u},
        {"large 160x64x160 @1.0",                       160, 64, 160, 1.0f, 0.5f, 8.0f, 7u},
        {"huge 256x64x256 @1.0",                        256, 64, 256, 1.0f, 0.5f, 8.0f, 9u},
    };
    const std::string which = argc > 1 ? argv[1] : "all";
    const int reps = argc > 2 ? std::atoi(argv[2]) : 3;
    const int frames = argc > 3 ? std::atoi(argv[3]) : 150;
    for (int ci = 0; ci < (int)cfgs.size(); ++ci) {
        if (which != "all" && std::atoi(which.c_str()) != ci) continue;
        const Cfg& c = cfgs[ci];
        std::printf("== %s : %d cells, %d frames of a held left button, radius 2.5, strength 8/s, 60 fps\n", c.name, c.nx * c.ny * c.nz, frames);
        for (int rep = 0; rep < reps; ++rep) {
            std::printf(" rep %d\n", rep);
            const Result a = Run(c, true, frames, false);
            Print("before: old edit path, old hash", a);
            const Result b = Run(c, false, frames, false);
            Print("chunk objects only (old hash)", b);
            const Result d = Run(c, false, frames, true);
            Print("after: chunk objects + fast hash", d);
        }
    }
    return 0;
}
