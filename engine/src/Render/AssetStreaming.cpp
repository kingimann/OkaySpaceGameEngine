// Asset streaming: texture mips planned from the camera, model meshes loaded in the background.
// See okay/Render/AssetStreaming.hpp. Written for this engine on its own TextureAsset library, Texture/Stream
// (ProjectedPixels), Optimize/MipBudget (PlanMips) and JobSystem; no outside code.
#include "okay/Render/AssetStreaming.hpp"

#include "okay/Components/LODGroup.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Core/Jobs.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Modeling/MeshAsset.hpp"
#include "okay/Optimize/MipBudget.hpp"
#include "okay/Render/Culling.hpp"
#include "okay/Render/TextureAsset.hpp"
#include "okay/Render/Visibility.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Texture/Stream.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <unordered_map>
#include <vector>

namespace okay {
namespace streaming {

namespace {

struct MeshLoad {
    std::string path, dir;
    std::atomic<int> state{0};   // 0 running, 1 done, 2 failed
    Mesh mesh;
    std::string texture, error;
};

struct Waiter {
    std::shared_ptr<MeshLoad> load;
    std::string path;            // the renderer's meshPath when it started waiting (guards against a reused address)
};

struct State {
    Stats last;
    int meshMode = 0;
    bool meshEnvRead = false;
    std::unordered_map<std::string, std::shared_ptr<MeshLoad>> loads;   // by path + dir
    std::unordered_map<const MeshRenderer*, Waiter> waiting;
    std::vector<JobHandle> handles;
    std::uint64_t meshesLoaded = 0, meshFailed = 0;
};
State& St() { static State* s = new State(); return *s; }

void ReadMeshEnv() {
    State& s = St();
    if (s.meshEnvRead) return;
    s.meshEnvRead = true;
    if (const char* e = std::getenv("OKAY_MESH_ASYNC")) if (e[0] == '1') s.meshMode = 2;
}

void AddNames(const MeshRenderer& mr, std::vector<const std::string*>& out) {
    const std::string* names[] = {&mr.texture, &mr.normalMap, &mr.specularMap, &mr.aoMap, &mr.metallicRoughnessMap, &mr.emissiveMap};
    for (const std::string* n : names) if (!n->empty()) out.push_back(n);
    for (const std::string& n : mr.slotTextures) if (!n.empty()) out.push_back(&n);
}

void PlanTextures(Scene& scene, const Vec3& eye, float fovYDeg, int viewH, Stats& st) {
    const std::vector<textures::StreamInfo> infos = textures::StreamedTextures();
    st.textures = (int)infos.size();
    if (infos.empty()) return;
    std::unordered_map<std::string, int> index;
    for (int i = 0; i < (int)infos.size(); ++i) index[infos[(std::size_t)i].name] = i;

    // 1. The largest on-screen size of each texture (pixels, longest side), over every renderer that uses it.
    std::vector<float> pixels(infos.size(), 0.0f), tiling(infos.size(), 1.0f);
    const float fovY = (fovYDeg > 1.0f ? fovYDeg : 60.0f) * 3.14159265f / 180.0f;
    const int screenH = viewH > 0 ? viewH : 1080;
    std::vector<const std::string*> names;
    for (const auto& up : scene.Objects()) {
        GameObject* go = up.get();
        if (!go || !go->ActiveInHierarchy()) continue;
        auto* mr = go->GetComponent<MeshRenderer>();
        if (!mr || !mr->enabled) continue;
        names.clear();
        AddNames(*mr, names);
        if (names.empty()) continue;
        Vec3 c; float r = 0.5f;
        if (!ObjectWorldSphere(go, mr, c, r) || !(r > 1e-5f)) { c = go->transform->Position(); r = 0.5f; }
        const float dist = std::max((c - eye).Magnitude() - r, 0.01f);   // the nearest point of the object
        const float px = tex::ProjectedPixels(2.0f * r, dist, fovY, screenH);
        const float uv = std::max(std::max(std::fabs(mr->tiling.x), std::fabs(mr->tiling.y)), 0.01f);
        for (const std::string* n : names) {
            auto it = index.find(*n);
            if (it == index.end()) continue;
            const std::size_t i = (std::size_t)it->second;
            if (px > pixels[i]) { pixels[i] = px; tiling[i] = uv; }
        }
    }

    // 2. The mip each should keep, inside the budget.
    const Settings& cfg = Config();
    std::vector<optimize::MipTexture> in(infos.size());
    for (std::size_t i = 0; i < infos.size(); ++i) {
        optimize::MipTexture& t = in[i];
        t.id = (int)i;
        t.width = infos[i].fullWidth;
        t.height = infos[i].fullHeight;
        t.bytesPerPixel = 4.0f;
        t.screenPixels = pixels[i];
        t.uvScale = tiling[i];
        t.previousMip = infos[i].pendingMip >= 0 ? infos[i].pendingMip : infos[i].residentMip;
    }
    optimize::MipOptions mo;
    mo.budgetBytes = cfg.budgetMB > 0.0f ? cfg.budgetMB * 1048576.0f : (float)textures::BudgetBytes();
    mo.mipBias = cfg.mipBias;
    mo.minDimension = std::max(1, cfg.minDimension);
    const optimize::MipPlan plan = optimize::PlanMips(in, mo);
    st.budgetBytes = (std::size_t)mo.budgetBytes;
    st.overBudget = plan.overBudget;
    st.desiredBytes = (std::size_t)plan.desiredBytes;
    st.fullBytes = (std::size_t)plan.fullBytes;
    st.plannedThisFrame = (int)infos.size();

    // 3. Evictions now; loads most visible first, up to the per-frame cap.
    struct Load { std::size_t i; float px; };
    std::vector<Load> loads;
    for (std::size_t i = 0; i < infos.size(); ++i) {
        const int want = plan.choices[i].mip;
        const int have = infos[i].residentMip;
        if (want > have) textures::RequestMip(infos[i].name, want);
        else if (want < have) loads.push_back({i, pixels[i]});
        if (want == plan.choices[i].desiredMip && want >= have && infos[i].pendingMip < 0) ++st.texturesSharp;
    }
    std::sort(loads.begin(), loads.end(), [](const Load& a, const Load& b) { return a.px != b.px ? a.px > b.px : a.i < b.i; });
    const float cap = cfg.uploadMBPerFrame > 0.0f ? cfg.uploadMBPerFrame * 1048576.0f : 0.0f;
    float queued = 0.0f;
    for (const Load& l : loads) {
        const textures::StreamInfo& info = infos[l.i];
        if (info.pendingMip >= 0) continue;   // one load at a time per texture
        const int want = plan.choices[l.i].mip;
        const float bytes = optimize::MipChainBytes(info.fullWidth, info.fullHeight, 4.0f, want, 0);
        if (cap > 0.0f && queued > 0.0f && queued + bytes > cap) break;   // the rest waits for a later frame
        if (textures::RequestMip(info.name, want) == textures::StreamResult::Queued) queued += bytes;
    }
}

} // namespace

Settings& Config() { static Settings s; return s; }

void Update(Scene& scene, const Vec3& eye, float fovYDeg, int viewHeightPx) {
    Stats st;
    st.texturesOn = textures::Streaming();
    textures::ApplyStreamLoads();
    ApplyMeshLoads(scene);
    if (st.texturesOn) PlanTextures(scene, eye, fovYDeg, viewHeightPx, st);
    St().last.plannedThisFrame = st.plannedThisFrame;
    St().last.texturesSharp = st.texturesSharp;
    St().last.desiredBytes = st.desiredBytes;
    St().last.overBudget = st.overBudget;
}

void Update(Scene& scene) {
    const visibility::Frame& f = visibility::Current();
    if (!f.ready) { textures::ApplyStreamLoads(); ApplyMeshLoads(scene); return; }
    Update(scene, f.eye, f.fovY, f.viewH);
}

// ---- meshes ------------------------------------------------------------------------------------------------------

void SetMeshAsyncMode(int mode) { ReadMeshEnv(); St().meshMode = mode < 0 ? 0 : (mode > 2 ? 2 : mode); }
int MeshAsyncMode() { ReadMeshEnv(); return St().meshMode; }
bool MeshAsyncLoading() { return MeshAsyncMode() != 0; }

bool SceneTextAsksForAsyncMeshes(const std::string& text) {
    // The component's generic record: "  assetstreamer ... backgroundMeshes=1 ..." on one line.
    for (std::size_t at = text.find("assetstreamer"); at != std::string::npos; at = text.find("assetstreamer", at + 1)) {
        const std::size_t lineStart = text.rfind('\n', at);
        const std::size_t from = lineStart == std::string::npos ? 0 : lineStart + 1;
        std::size_t k = from;
        while (k < at && (text[k] == ' ' || text[k] == '\t')) ++k;
        if (k != at) continue;                                  // not the start of a record
        const std::size_t end = text.find('\n', at);
        const std::string line = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
        if (line.find(" backgroundMeshes=1") != std::string::npos) return true;
    }
    return false;
}

void LoadMeshInBackground(MeshRenderer* mr, const std::string& path, const std::string& sceneDir) {
    if (!mr || path.empty()) return;
    State& s = St();
    const std::string key = path + "\n" + sceneDir;
    std::shared_ptr<MeshLoad>& load = s.loads[key];
    if (!load) {
        load = std::make_shared<MeshLoad>();
        load->path = path;
        load->dir = sceneDir;
        std::shared_ptr<MeshLoad> l = load;
        JobOptions opt;
        opt.name = "mesh load " + path;
        s.handles.push_back(JobSystem::Global().Submit([l]() {
            const bool ok = modeling::LoadMeshAsset(l->path, l->mesh, &l->texture, &l->error, l->dir);
            l->state.store(ok ? 1 : 2, std::memory_order_release);
        }, opt));
    }
    // The placeholder: a unit cube, until the file is read.
    mr->mesh = Mesh::Cube();
    mr->meshPath = path;
    mr->meshPathHash = 0;
    s.waiting[mr] = Waiter{load, path};
}

int ApplyMeshLoads(Scene& scene) {
    State& s = St();
    if (s.waiting.empty()) {
        if (!s.loads.empty()) s.loads.clear();
        return 0;
    }
    int handed = 0;
    std::unordered_map<const MeshRenderer*, bool> seen;
    for (const auto& up : scene.Objects()) {
        GameObject* go = up.get();
        if (!go) continue;
        for (MeshRenderer* mr : go->GetComponents<MeshRenderer>()) {
            auto it = s.waiting.find(mr);
            if (it == s.waiting.end()) continue;
            seen[mr] = true;
            MeshLoad& l = *it->second.load;
            const int state = l.state.load(std::memory_order_acquire);
            if (state == 0) continue;
            if (mr->meshPath != it->second.path) { s.waiting.erase(it); continue; }   // someone changed it meanwhile
            if (state == 1) {
                mr->mesh = l.mesh;
                mr->meshPathHash = modeling::GeometryHash(mr->mesh);
                if (!l.texture.empty() && mr->texture.empty()) mr->texture = l.texture;
                if (auto* lod = go->GetComponent<LODGroup>(); lod && lod->levels.size() > 1) lod->RebuildFromRatios(mr->mesh);
                ++s.meshesLoaded;
                ++handed;
            } else {
                Log::Warning("Mesh file for '", go->name, "': ", l.error);
                ++s.meshFailed;
            }
            s.waiting.erase(it);
        }
    }
    // Waiters whose renderer is not in this scene any more (destroyed while its file loaded) are dropped once it is read.
    for (auto it = s.waiting.begin(); it != s.waiting.end();) {
        if (!seen.count(it->first) && it->second.load->state.load(std::memory_order_acquire) != 0) it = s.waiting.erase(it);
        else ++it;
    }
    // Loads nobody waits for any more (finished, or their objects are gone) are dropped once done.
    for (auto it = s.loads.begin(); it != s.loads.end();) {
        bool used = false;
        for (const auto& w : s.waiting) if (w.second.load == it->second) { used = true; break; }
        if (!used && it->second->state.load(std::memory_order_acquire) != 0) it = s.loads.erase(it);
        else ++it;
    }
    s.handles.erase(std::remove_if(s.handles.begin(), s.handles.end(), [](const JobHandle& h) { return h.IsDone(); }), s.handles.end());
    return handed;
}

void FinishMeshLoads() {
    State& s = St();
    const std::vector<JobHandle> hs = s.handles;
    for (const JobHandle& h : hs) { try { JobSystem::Global().Wait(h); } catch (...) {} }
}

bool MeshPending(const MeshRenderer* mr) { return St().waiting.count(mr) != 0; }

// ---- stats -------------------------------------------------------------------------------------------------------

Stats GetStats() {
    State& s = St();
    Stats st = s.last;
    st.texturesOn = textures::Streaming();
    const std::vector<textures::StreamInfo> infos = textures::StreamedTextures();
    st.textures = (int)infos.size();
    st.residentBytes = 0;
    std::size_t full = 0;
    for (const auto& i : infos) {
        st.residentBytes += i.bytes;
        full += (std::size_t)optimize::MipChainBytes(i.fullWidth, i.fullHeight, 4.0f, 0, 0);
    }
    st.fullBytes = full;
    const textures::StreamCounters c = textures::GetStreamCounters();
    st.textureLoadsPending = c.pending;
    st.textureLoadsDone = c.loadsDone;
    st.textureLoadsQueued = c.loadsQueued;
    st.textureEvictions = c.evictions;
    const Settings& cfg = Config();
    st.budgetBytes = cfg.budgetMB > 0.0f ? (std::size_t)(cfg.budgetMB * 1048576.0f) : textures::BudgetBytes();
    st.meshesOn = MeshAsyncLoading();
    int pending = 0;
    for (const auto& kv : s.loads) if (kv.second->state.load(std::memory_order_acquire) == 0) ++pending;
    st.meshLoadsPending = pending;
    st.meshesWaiting = (int)s.waiting.size();
    st.meshesLoaded = s.meshesLoaded;
    st.meshLoadsFailed = s.meshFailed;
    return st;
}

std::string OverlayLine() {
    const Stats s = GetStats();
    char buf[200];
    if (!s.texturesOn && !s.meshesOn && s.meshesWaiting == 0) return "streaming: off";
    std::snprintf(buf, sizeof(buf), "streaming: %d textures %.1f MB / %.0f MB (full %.1f MB), %d loading%s; meshes %d loading",
                  s.textures, s.residentBytes / 1048576.0, s.budgetBytes / 1048576.0, s.fullBytes / 1048576.0,
                  s.textureLoadsPending, s.overBudget ? ", OVER BUDGET" : "", s.meshesWaiting);
    return buf;
}

void ResetForTests() {
    FinishMeshLoads();
    State& s = St();
    s.loads.clear();
    s.waiting.clear();
    s.handles.clear();
    s.meshesLoaded = s.meshFailed = 0;
    s.last = Stats{};
}

} // namespace streaming
} // namespace okay
