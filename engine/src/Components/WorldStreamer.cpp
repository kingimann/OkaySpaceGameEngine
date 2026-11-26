// WorldStreamer: cells of a big world streamed in and out around a target (docs/engine/asset_streaming.md).
// The cell choice is optimize::StreamingGrid's; each cell is a streamed scene load (Scene::BeginAsyncLoad, stream).
#include "okay/Components/WorldStreamer.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/VoxelTerrain.hpp"
#include "okay/Core/SafeCast.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iterator>
#include <string>

namespace okay {

using optimize::CellCoord;

namespace {
WorldStreamer::CellState FromPhase(Scene::LoadPhase p) {
    switch (p) {
        case Scene::LoadPhase::Reading:   return WorldStreamer::CellState::Reading;
        case Scene::LoadPhase::Creating:  return WorldStreamer::CellState::Creating;
        case Scene::LoadPhase::Loaded:    return WorldStreamer::CellState::Loaded;
        case Scene::LoadPhase::Unloading: return WorldStreamer::CellState::Unloading;
        default:                          return WorldStreamer::CellState::Unloaded;
    }
}

// A cell whose voxel terrain was dug or built on stays: unloading it would throw the player's work away (the cell
// file is the authored one and is never written back).
bool CellEdited(const Scene& scene, int handle) {
    for (std::uint64_t id : scene.LoadedRoots(handle))
        if (GameObject* root = GameObject::FromInstanceId(id))
            for (VoxelTerrain* vt : root->GetComponentsInChildren<VoxelTerrain>(true))
                if (vt && vt->editCount > 0) return true;
    return false;
}
} // namespace

int WorldStreamer::CellOf(float v) const {
    if (cellSize <= 0.0f) return 0;
    return SafeInt(std::floor(v / cellSize));
}

std::string WorldStreamer::CellFile(int x, int z) const {
    std::string f = folder;
    if (!f.empty() && f.back() != '/' && f.back() != '\\') f += '/';
    return f + prefix + "_" + std::to_string(x) + "_" + std::to_string(z) + ext;
}

bool WorldStreamer::TargetPose(Vec3& pos, Vec3& dir) const {
    Scene* s = GetScene();
    if (!s) return false;
    Transform* t = nullptr;
    if (!target.empty()) {
        if (GameObject* go = s->Find(target)) t = go->transform;
    } else if (s->mainCamera && s->mainCamera->gameObject) {
        t = s->mainCamera->gameObject->transform;
    }
    if (!t) return false;
    pos = t->Position();
    dir = t->Forward();
    return true;
}

void WorldStreamer::Configure() {
    if (!m_core) m_core = std::make_shared<Core>();
    m_core->scene = GetScene();
    optimize::StreamingConfig cfg;
    cfg.cellSize = cellSize > 0.0f ? cellSize : 1.0f;
    cfg.plane = optimize::GridPlane::XZ;
    cfg.ringDistance = true;
    const int lr = loadRadius < 0 ? 0 : loadRadius;
    const int ur = unloadRadius < lr ? lr : unloadRadius;
    cfg.loadRadius = static_cast<float>(lr) * cfg.cellSize;
    cfg.unloadRadius = static_cast<float>(ur) * cfg.cellSize;
    // Every wanted cell is asked for at once: the scene reads them nearest first and the frame budget spreads the
    // object creation, so per-frame request limits would only add latency.
    cfg.maxLoadsPerFrame = 0;
    cfg.maxInFlight = 0;
    cfg.maxUnloadsPerFrame = 0;
    m_core->grid.SetConfig(cfg);

    std::weak_ptr<Core> weak = m_core;
    WorldStreamer* self = this;
    optimize::StreamingHooks hooks;
    hooks.requestLoad = [weak, self](const CellCoord& c, const std::string& file) {
        auto core = weak.lock();
        if (!core || !core->scene) return false;
        Scene::AsyncLoadOptions o;
        o.additive = true;
        o.stream = true;
        const Vec3 ahead = self->LookAhead();
        o.priority = -std::min(core->grid.Priority(self->m_pos, self->m_dir, c),
                               core->grid.Priority(self->m_pos + ahead, self->m_dir, c));
        std::weak_ptr<Core> w = core;
        auto mine = std::make_shared<int>(0);   // this load's handle, known once BeginAsyncLoad returns
        o.onDone = [w, c, file, mine](bool ok) {
            auto k = w.lock();
            if (!k) return;
            auto it = k->handles.find(c);
            if (it == k->handles.end() || it->second != *mine) return;   // dropped already (or a newer load of it)
            if (!ok) {   // missing or broken file: retried after StreamingConfig::retryDelayFrames updates
                k->handles.erase(it);
                k->grid.NotifyLoadFailed(c);
                return;
            }
            // Link each cell root to its file, as the one-go prefab instantiation did (the editor's apply/revert).
            for (std::uint64_t id : k->scene->LoadedRoots(it->second))
                if (GameObject* root = GameObject::FromInstanceId(id)) root->prefabSource = file;
            if (!k->grid.NotifyLoaded(c)) {   // not wanted any more (cannot happen with the hooks below, but be safe)
                k->scene->UnloadAdditiveSpread(it->second);
                k->handles.erase(it);
            }
        };
        const int h = core->scene->BeginAsyncLoad(file, std::move(o));
        if (h <= 0) return false;
        *mine = h;
        core->handles[c] = h;
        return true;
    };
    auto drop = [weak](const CellCoord& c, const std::string&) {
        auto core = weak.lock();
        if (!core || !core->scene) return;
        auto it = core->handles.find(c);
        if (it == core->handles.end()) return;
        const int h = it->second;
        core->handles.erase(it);
        core->scene->UnloadAdditiveSpread(h);   // a load in flight is cancelled; loaded objects go over frames
        if (core->scene->GetLoadPhase(h) == Scene::LoadPhase::Unloading) core->leaving[c] = h;
    };
    hooks.requestUnload = drop;
    hooks.cancelLoad = drop;
    hooks.keepLoaded = [weak](const CellCoord& c, const std::string&) {
        auto core = weak.lock();
        if (!core || !core->scene) return false;
        auto it = core->handles.find(c);
        return it != core->handles.end() && CellEdited(*core->scene, it->second);
    };
    m_core->grid.SetHooks(hooks);
}

void WorldStreamer::ScanFolder() {
    // Register every "<prefix>_<x>_<z><ext>" file in the folder (a missing cell is simply not there).
    namespace fs = std::filesystem;
    m_scanned = true;
    std::error_code ec;
    std::string dir = folder.empty() ? std::string(".") : folder;
    const std::string head = prefix + "_";
    for (fs::directory_iterator it(fs::u8path(dir), ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code fe;
        if (!it->is_regular_file(fe)) continue;
        const std::string name = it->path().filename().u8string();
        if (name.size() <= head.size() + ext.size() || name.compare(0, head.size(), head) != 0) continue;
        if (name.compare(name.size() - ext.size(), ext.size(), ext) != 0) continue;
        const std::string mid = name.substr(head.size(), name.size() - head.size() - ext.size());   // "<x>_<z>"
        const std::size_t us = mid.find('_', mid.empty() ? 0 : 1);
        if (us == std::string::npos) continue;
        char* e1 = nullptr; char* e2 = nullptr;
        const std::string xs = mid.substr(0, us), zs = mid.substr(us + 1);
        const long x = std::strtol(xs.c_str(), &e1, 10), z = std::strtol(zs.c_str(), &e2, 10);
        if (xs.empty() || zs.empty() || *e1 || *e2) continue;
        CellCoord c{static_cast<int>(x), 0, static_cast<int>(z)};
        m_core->grid.RegisterCell(c, CellFile(c.x, c.z));
    }
}

void WorldStreamer::Start() {
    Configure();
    ScanFolder();
    Evaluate(true);
}

void WorldStreamer::Update(float dt) {
    if (!m_core) { Configure(); ScanFolder(); }
    Evaluate(false, dt);
}

Vec3 WorldStreamer::LookAhead() const {
    if (!(predictSeconds > 0.0f)) return Vec3{0, 0, 0};
    Vec3 a = m_velocity * predictSeconds;
    a.y = 0.0f;
    // Never look further than the unload ring: the cells being left must not be dropped for a guess.
    const float most = static_cast<float>(unloadRadius > 0 ? unloadRadius : 1) * cellSize;
    const float len = a.Magnitude();
    if (len > most && len > 0.0f) a = a * (most / len);
    return a;
}

void WorldStreamer::Evaluate(bool force, float dt) {
    if (!m_core) return;
    Vec3 p, d;
    if (!TargetPose(p, d)) return;
    m_pos = p; m_dir = d;
    // Velocity for predictive loading: a smoothed difference of positions (a teleport is one big step, forgotten
    // in a few frames).
    if (m_haveLastPos && dt > 1e-5f) {
        const Vec3 v = (p - m_lastPos) * (1.0f / dt);
        const float k = std::min(1.0f, dt * 4.0f);
        m_velocity = m_velocity + (v - m_velocity) * k;
    }
    m_lastPos = p; m_haveLastPos = true;
    const int cx = CellOf(p.x), cz = CellOf(p.z);
    const Vec3 ahead = LookAhead();
    const int ax = CellOf(p.x + ahead.x), az = CellOf(p.z + ahead.z);
    const bool moved = !m_haveCell || cx != m_curX || cz != m_curZ || ax != m_aheadX || az != m_aheadZ;
    m_curX = cx; m_curZ = cz; m_haveCell = true;
    m_aheadX = ax; m_aheadZ = az;
    const bool busy = !m_core->grid.LoadingCells().empty();
    if (!force && onlyOnCellChange && !moved && !busy) return;
    for (auto it = m_core->leaving.begin(); it != m_core->leaving.end();)   // forget cells that finished leaving
        it = (m_core->scene && m_core->scene->GetLoadPhase(it->second) == Scene::LoadPhase::Unloading)
                 ? std::next(it) : m_core->leaving.erase(it);
    m_core->grid.Update(p, d, ahead);
    if (moved && m_core->scene) {
        // The target moved on: cells still loading are re-ordered by their new distance.
        for (const CellCoord& c : m_core->grid.LoadingCells()) {
            auto it = m_core->handles.find(c);
            if (it != m_core->handles.end())
                m_core->scene->SetLoadPriority(it->second, -std::min(m_core->grid.Priority(p, d, c),
                                                                     m_core->grid.Priority(p + ahead, d, c)));
        }
    }
}

void WorldStreamer::SyncNow() {
    Configure();
    ScanFolder();
    Evaluate(true);
    if (!m_core->scene) return;
    // Finish every wanted cell now (the handles map can change under the callbacks: copy it first).
    std::vector<std::pair<CellCoord, int>> loading;
    for (const auto& kv : m_core->handles) loading.push_back(kv);
    for (const auto& kv : loading)
        if (m_core->grid.State(kv.first) == optimize::CellState::Loading) m_core->scene->CompleteLoad(kv.second);
}

void WorldStreamer::OnDestroy() {
    if (!m_core) return;
    Scene* s = GetScene();
    auto handles = std::move(m_core->handles);
    m_core.reset();   // the scene's callbacks for these loads find nothing now
    if (s) for (auto& kv : handles) s->UnloadAdditive(kv.second);
}

std::vector<std::pair<int,int>> WorldStreamer::LoadedCells() const {
    std::vector<std::pair<int,int>> out;
    if (!m_core) return out;
    for (const CellCoord& c : m_core->grid.LoadedCells()) out.emplace_back(c.x, c.z);
    return out;
}

WorldStreamer::CellState WorldStreamer::GetCellState(int x, int z) const {
    if (!m_core || !m_core->scene) return CellState::Unloaded;
    auto it = m_core->handles.find(CellCoord{x, 0, z});
    if (it == m_core->handles.end()) {
        auto l = m_core->leaving.find(CellCoord{x, 0, z});
        if (l == m_core->leaving.end()) return CellState::Unloaded;
        return FromPhase(m_core->scene->GetLoadPhase(l->second)) == CellState::Unloading ? CellState::Unloading
                                                                                          : CellState::Unloaded;
    }
    const CellState st = FromPhase(m_core->scene->GetLoadPhase(it->second));
    // The scene may already be done while the grid has not heard (the callback runs in the same pass): ask the grid.
    if (st == CellState::Loaded && m_core->grid.State(CellCoord{x, 0, z}) != optimize::CellState::Loaded)
        return CellState::Creating;
    return st;
}

bool WorldStreamer::IsCellWanted(int x, int z) const {
    return m_core && m_core->handles.count(CellCoord{x, 0, z}) != 0;
}

int WorldStreamer::LoadingCount() const {
    return m_core ? static_cast<int>(m_core->grid.LoadingCells().size()) : 0;
}

int WorldStreamer::KnownCellCount() const {
    return m_core ? m_core->grid.RegisteredCount() : 0;
}

} // namespace okay
