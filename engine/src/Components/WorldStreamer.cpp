// World streaming on the StreamingGrid planner and the background scene queue. See WorldStreamer.hpp
// and docs/engine/world_streaming.md.
#include "okay/Components/WorldStreamer.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/StreamingSource.hpp"
#include "okay/Components/VoxelTerrain.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Scene/Transform.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <set>
#include <sstream>

namespace okay {

using optimize::CellCoord;
using optimize::CellState;

namespace {

/// Parse "<prefix>_<x>_<z><ext>" (x and z may be negative). False for any other name.
bool ParseCellName(const std::string& name, const std::string& prefix, const std::string& ext, int& x, int& z) {
    if (name.size() <= prefix.size() + ext.size() + 1) return false;
    if (name.compare(0, prefix.size(), prefix) != 0 || name[prefix.size()] != '_') return false;
    if (name.compare(name.size() - ext.size(), ext.size(), ext) != 0) return false;
    const std::string mid = name.substr(prefix.size() + 1, name.size() - prefix.size() - 1 - ext.size());
    const std::size_t us = mid.find('_', 1);   // skip a leading minus sign
    if (us == std::string::npos) return false;
    auto num = [](const std::string& s, int& out) {
        if (s.empty() || s.size() > 10) return false;
        std::size_t i = (s[0] == '-') ? 1 : 0;
        if (i == s.size()) return false;
        long v = 0;
        for (; i < s.size(); ++i) {
            if (s[i] < '0' || s[i] > '9') return false;
            v = v * 10 + (s[i] - '0');
        }
        out = (int)(s[0] == '-' ? -v : v);
        return true;
    };
    return num(mid.substr(0, us), x) && num(mid.substr(us + 1), z);
}

}  // namespace

struct WorldStreamer::Impl {
    optimize::StreamingGrid grid;
    SceneStreamQueue queue;
    struct CellLoad {
        std::vector<int> tickets;     // one per document (a remembered cell can have several roots)
        bool pinned = false;          // kept although out of range (edited voxels, rememberChanges off)
    };
    std::map<CellCoord, CellLoad> cells;            // cells requested or loaded
    std::map<CellCoord, std::vector<std::string>> snapshots;   // remembered unloaded cells, one text per root
    std::map<CellCoord, std::string> files;         // cells found by the scan -> path
    std::string resolvedFolder;
    bool setUp = false;
    bool haveCell = false;
    int curX = 0, curZ = 0;
    std::vector<CellCoord> lastSourceCells;
    bool needPlan = true;
    Stats stats;
    // debug overlay
    std::uint64_t overlayId = 0;
    std::uint64_t overlaySig = 0;
};

WorldStreamer::WorldStreamer() : sceneDir(CurrentSceneLoadDirectory()), m(std::make_unique<Impl>()) {}
WorldStreamer::~WorldStreamer() = default;

std::string WorldStreamer::CellFile(int x, int z) const {
    std::string f = folder;
    if (!f.empty() && f.back() != '/' && f.back() != '\\') f += '/';
    return f + prefix + "_" + std::to_string(x) + "_" + std::to_string(z) + ext;
}

std::string WorldStreamer::ResolvedFolder() const { return m->resolvedFolder; }

void WorldStreamer::Rescan() {
    namespace fs = std::filesystem;
    std::error_code ec;
    std::string dir = folder.empty() ? std::string(".") : folder;
    fs::path p(dir);
    if (!fs::is_directory(p, ec) && p.is_relative() && !sceneDir.empty()) {
        fs::path alt = fs::path(sceneDir) / p;
        if (fs::is_directory(alt, ec)) p = alt;
    }
    m->resolvedFolder = p.generic_string();
    std::map<CellCoord, std::string> found;
    std::map<CellCoord, float> sizes;
    if (fs::is_directory(p, ec)) {
        for (fs::directory_iterator it(p, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            int x = 0, z = 0;
            if (!ParseCellName(it->path().filename().string(), prefix, ext, x, z)) continue;
            CellCoord c{x, 0, z};
            found[c] = it->path().generic_string();
            const auto bytes = it->file_size(ec);
            sizes[c] = ec ? 0.001f : std::max(0.001f, (float)((double)bytes / (1024.0 * 1024.0)));
        }
    }
    // New files join the grid; files that went away leave it (a loaded one unloads through the hook).
    for (const auto& kv : m->files)
        if (!found.count(kv.first)) m->grid.UnregisterCell(kv.first);
    for (const auto& kv : found)
        if (!m->files.count(kv.first)) m->grid.RegisterCell(kv.first, kv.second, sizes[kv.first]);
    m->files = std::move(found);
    m->stats.cellFiles = (int)m->files.size();
    m->needPlan = true;
}

void WorldStreamer::EnsureSetup() {
    optimize::StreamingConfig cfg;
    cfg.cellSize = cellSize > 0.001f ? cellSize : 0.001f;
    cfg.plane = optimize::GridPlane::XZ;
    cfg.metric = optimize::CellDistance::Chebyshev;
    // A ring of N cells: a cell N steps away has a gap of less than N cells to the source, a cell N + 1 away
    // at least N. The tiny shave keeps a source standing exactly on a cell edge from reaching one more ring.
    const int lr = std::max(0, loadRadius);
    const int ur = std::max(lr, unloadRadius);
    cfg.loadRadius = (float)lr * cfg.cellSize * (1.0f - 1e-5f);
    cfg.unloadRadius = (float)ur * cfg.cellSize * (1.0f - 1e-5f);
    cfg.maxLoadsPerFrame = maxLoadsPerFrame;
    cfg.maxInFlight = maxInFlight;
    cfg.maxUnloadsPerFrame = 0;
    cfg.viewDirectionWeight = viewDirectionWeight < 0.0f ? 0.0f : viewDirectionWeight;
    cfg.memoryBudget = memoryBudgetMB > 0.0f ? memoryBudgetMB : 0.0f;
    cfg.retryDelayFrames = 120;
    m->grid.SetConfig(cfg);
    m->queue.attachBudgetMs = attachBudgetMs > 0.05f ? attachBudgetMs : 0.05f;
    m->queue.background = backgroundLoading;
    if (!m->setUp) {
        m->setUp = true;
        Rescan();
    }
}

std::vector<optimize::StreamingViewer> WorldStreamer::Sources() const {
    std::vector<optimize::StreamingViewer> out;
    Scene* s = GetScene();
    if (!s) return out;
    Transform* t = nullptr;
    if (!target.empty()) {
        if (GameObject* go = s->Find(target)) t = go->transform;
    } else if (s->mainCamera && s->mainCamera->gameObject) {
        t = s->mainCamera->gameObject->transform;
    }
    if (t) out.push_back({t->Position(), t->Forward()});
    if (followSources)
        for (const StreamingSource::Point& p : StreamingSource::Collect(s)) out.push_back({p.position, p.direction});
    return out;
}

void WorldStreamer::Plan(bool force) {
    Scene* s = GetScene();
    if (!s) return;
    EnsureSetup();
    const std::vector<optimize::StreamingViewer> views = Sources();
    m->stats.sources = (int)views.size();
    if (views.empty()) return;                     // nothing to stream around this frame: keep what is there
    std::vector<CellCoord> sourceCells;
    for (const auto& v : views) sourceCells.push_back(m->grid.CellOf(v.position));
    const CellCoord first = sourceCells.front();
    m->curX = first.x; m->curZ = first.z; m->haveCell = true;
    const bool moved = sourceCells != m->lastSourceCells;
    m->lastSourceCells = sourceCells;
    if (!force && onlyOnCellChange && !moved && !m->needPlan) return;

    // Completions are told to the grid in Update, after its plan, never from inside a hook.
    optimize::StreamingHooks hooks;
    hooks.requestLoad = [this](const CellCoord& c, const std::string& path) {
        Impl::CellLoad& cl = m->cells[c];
        if (cl.pinned) return true;                       // still in the scene from before
        cl.tickets.clear();
        auto snap = m->snapshots.find(c);
        if (rememberChanges && snap != m->snapshots.end()) {
            // The copy stays until the cell is in: a load cancelled half way must not lose the changes.
            for (const std::string& text : snap->second) cl.tickets.push_back(m->queue.RequestText(text));
        } else {
            cl.tickets.push_back(m->queue.RequestFile(path));
        }
        ++m->stats.loadsRequested;
        return true;
    };
    auto dropCell = [this, s](const CellCoord& c) {
        auto f = m->cells.find(c);
        if (f == m->cells.end()) return;
        for (int t : f->second.tickets) m->queue.Release(*s, t);
        m->cells.erase(f);
    };
    hooks.requestUnload = [this, s, dropCell](const CellCoord& c, const std::string&) {
        auto f = m->cells.find(c);
        if (f == m->cells.end()) return;
        std::vector<GameObject*> roots;
        for (int t : f->second.tickets)
            for (std::uint64_t id : m->queue.Roots(t))
                if (GameObject* go = GameObject::FromInstanceId(id))
                    if (go->scene() == s && !go->IsDestroyed()) roots.push_back(go);
        // A cell whose voxel terrain was dug or built on stays loaded: the voxel field is saved quantized, so a copy
        // would not bring the exact dig back (and a dug cell is usually where the player is working).
        for (GameObject* go : roots)
            for (VoxelTerrain* vt : go->GetComponentsInChildren<VoxelTerrain>(true))
                if (vt && vt->editCount > 0) { f->second.pinned = true; return; }
        if (rememberChanges) {
            std::vector<std::string> texts;
            for (GameObject* go : roots) texts.push_back(SceneSerializer::SerializeObject(*go));
            m->snapshots[c] = std::move(texts);
        }
        ++m->stats.unloads;
        dropCell(c);
    };
    hooks.cancelLoad = [dropCell](const CellCoord& c, const std::string&) { dropCell(c); };
    m->grid.SetHooks(hooks);
    const optimize::StreamingFrame fr = m->grid.Update(views);
    m->grid.SetHooks(optimize::StreamingHooks{});
    ++m->stats.plans;
    m->stats.wanted = fr.wanted;
    // Plan again next frame while something is held back or loading.
    m->needPlan = fr.deferredByLimit > 0 || fr.deferredByBudget > 0 || fr.loading > 0;
}

void WorldStreamer::Start() {
    EnsureSetup();
    SyncNow();
}

void WorldStreamer::SyncNow() {
    Scene* s = GetScene();
    if (!s) return;
    EnsureSetup();
    // Unlimited requests for this one plan, then finish them all here.
    const int keepLoads = maxLoadsPerFrame, keepFlight = maxInFlight;
    maxLoadsPerFrame = 0; maxInFlight = 0;
    EnsureSetup();
    Plan(true);
    maxLoadsPerFrame = keepLoads; maxInFlight = keepFlight;
    EnsureSetup();
    m->queue.Flush(*s);
    Update(0.0f);
}

void WorldStreamer::Update(float /*dt*/) {
    Scene* s = GetScene();
    if (!s) return;
    EnsureSetup();
    Plan(false);
    m->queue.Pump(*s);
    // Cells whose documents are all in are loaded; one that failed is retried later by the grid.
    std::vector<CellCoord> done, failed;
    for (auto& kv : m->cells) {
        if (m->grid.State(kv.first) != CellState::Loading) continue;
        bool all = true, bad = false;
        for (int t : kv.second.tickets) {
            const StreamLoadState st = m->queue.State(t);
            if (st == StreamLoadState::Failed || st == StreamLoadState::Unknown) bad = true;
            else if (st != StreamLoadState::Loaded) all = false;
        }
        if (bad) failed.push_back(kv.first);
        else if (all) done.push_back(kv.first);
    }
    for (const CellCoord& c : done) {
        m->cells[c].pinned = false;
        m->snapshots.erase(c);   // the cell is back as it was left; its copy is the live objects now
        if (m->grid.NotifyLoaded(c)) ++m->stats.loadsFinished;
    }
    for (const CellCoord& c : failed) {
        auto f = m->cells.find(c);
        if (f != m->cells.end()) { for (int t : f->second.tickets) m->queue.Release(*s, t); m->cells.erase(f); }
        m->grid.NotifyLoadFailed(c);
        ++m->stats.loadsFailed;
        m->needPlan = true;
    }
    if (!done.empty()) m->needPlan = true;

    // ---- debug overlay -----------------------------------------------------------------------------
    GameObject* ov = m->overlayId ? GameObject::FromInstanceId(m->overlayId) : nullptr;
    if (ov && (ov->IsDestroyed() || ov->scene() != s)) ov = nullptr;
    if (!debugOverlay) {
        if (ov) s->Destroy(ov);
        m->overlayId = 0; m->overlaySig = 0;
        return;
    }
    // Rebuild only when a cell changed state or a source changed cell.
    std::uint64_t sig = 1469598103934665603ull;
    auto mix = [&sig](std::uint64_t v) { sig = (sig ^ v) * 1099511628211ull; };
    for (const auto& kv : m->files) {
        mix((std::uint64_t)(std::int64_t)kv.first.x); mix((std::uint64_t)(std::int64_t)kv.first.z);
        mix((std::uint64_t)m->grid.State(kv.first));
    }
    for (const CellCoord& c : m->lastSourceCells) { mix((std::uint64_t)(std::int64_t)c.x * 31 + 7); mix((std::uint64_t)(std::int64_t)c.z); }
    mix((std::uint64_t)(std::llround(debugHeight * 100.0f)));
    if (ov && sig == m->overlaySig) return;
    if (!ov) {
        ov = s->CreateGameObject("Streaming Debug");
        ov->transient = true;
        auto* mr = ov->AddComponent<MeshRenderer>();
        mr->unlit = true; mr->doubleSided = true; mr->castShadows = false; mr->receiveShadows = false;
        mr->groundShadow = false;
        m->overlayId = ov->InstanceId();
    }
    m->overlaySig = sig;
    Mesh mesh;
    mesh.name = "StreamingDebug";
    const float cs = cellSize > 0.001f ? cellSize : 0.001f;
    const float y = debugHeight;
    auto quad = [&](float x0, float z0, float x1, float z1, const Color& col) {
        const int b = (int)mesh.vertices.size();
        mesh.vertices.push_back({x0, y, z0}); mesh.vertices.push_back({x1, y, z0});
        mesh.vertices.push_back({x1, y, z1}); mesh.vertices.push_back({x0, y, z1});
        for (int i : {0, 2, 1, 0, 3, 2}) mesh.triangles.push_back(b + i);
        mesh.triColors.push_back(col); mesh.triColors.push_back(col);
    };
    const Color loaded = Color::FromBytes(60, 210, 90), loading = Color::FromBytes(250, 200, 40),
                onDisk = Color::FromBytes(120, 120, 130), source = Color::FromBytes(255, 255, 255);
    for (const auto& kv : m->files) {
        const CellState st = m->grid.State(kv.first);
        const Color col = st == CellState::Loaded ? loaded : st == CellState::Loading ? loading : onDisk;
        const float x0 = kv.first.x * cs, z0 = kv.first.z * cs, x1 = x0 + cs, z1 = z0 + cs;
        const float w = cs * (st == CellState::Unloaded ? 0.015f : 0.035f);
        quad(x0, z0, x1, z0 + w, col); quad(x0, z1 - w, x1, z1, col);          // outline
        quad(x0, z0 + w, x0 + w, z1 - w, col); quad(x1 - w, z0 + w, x1, z1 - w, col);
        if (st != CellState::Unloaded) {                                        // a filled centre square
            const float h = cs * (st == CellState::Loaded ? 0.18f : 0.10f);
            const float cx = x0 + cs * 0.5f, cz = z0 + cs * 0.5f;
            quad(cx - h, cz - h, cx + h, cz + h, col);
        }
    }
    for (const auto& v : Sources()) {
        const float h = cs * 0.06f;
        quad(v.position.x - h, v.position.z - h, v.position.x + h, v.position.z + h, source);
    }
    if (auto* mr = ov->GetComponent<MeshRenderer>()) mr->mesh = std::move(mesh);
}

void WorldStreamer::OnDestroy() {
    Scene* s = GetScene();
    if (!s) return;
    m->queue.Clear(*s);
    m->cells.clear();
    if (m->overlayId)
        if (GameObject* ov = GameObject::FromInstanceId(m->overlayId))
            if (ov->scene() == s && !ov->IsDestroyed()) s->Destroy(ov);
    m->overlayId = 0;
}

std::vector<std::pair<int,int>> WorldStreamer::LoadedCells() const {
    std::vector<std::pair<int,int>> out;
    for (const CellCoord& c : m->grid.LoadedCells()) out.push_back({c.x, c.z});
    return out;
}
std::size_t WorldStreamer::LoadedCount() const { return m->grid.LoadedCells().size(); }
bool WorldStreamer::IsCellLoaded(int x, int z) const { return m->grid.State(CellCoord{x, 0, z}) == CellState::Loaded; }
bool WorldStreamer::IsCellLoading(int x, int z) const { return m->grid.State(CellCoord{x, 0, z}) == CellState::Loading; }
bool WorldStreamer::HasCellFile(int x, int z) const { return m->files.count(CellCoord{x, 0, z}) != 0; }
int  WorldStreamer::CellFileCount() const { return (int)m->files.size(); }
std::vector<std::pair<int,int>> WorldStreamer::CellFiles() const {
    std::vector<std::pair<int,int>> out;
    for (const auto& kv : m->files) out.push_back({kv.first.x, kv.first.z});
    return out;
}

bool WorldStreamer::CurrentCell(int& x, int& z) const {
    if (!m->haveCell) return false;
    x = m->curX; z = m->curZ;
    return true;
}

std::vector<std::uint64_t> WorldStreamer::CellRoots(int x, int z) const {
    std::vector<std::uint64_t> out;
    auto f = m->cells.find(CellCoord{x, 0, z});
    if (f == m->cells.end()) return out;
    for (int t : f->second.tickets)
        for (std::uint64_t id : m->queue.Roots(t)) out.push_back(id);
    return out;
}

bool WorldStreamer::HasSnapshot(int x, int z) const { return m->snapshots.count(CellCoord{x, 0, z}) != 0; }
std::size_t WorldStreamer::SnapshotCount() const { return m->snapshots.size(); }
void WorldStreamer::ForgetChanges() { m->snapshots.clear(); }

WorldStreamer::Stats WorldStreamer::GetStats() const {
    Stats s = m->stats;
    s.cellFiles = (int)m->files.size();
    s.loaded = (int)m->grid.LoadedCells().size();
    s.loading = (int)m->grid.LoadingCells().size();
    s.snapshots = (int)m->snapshots.size();
    s.readMs = m->queue.Stats().readMs;
    return s;
}

std::string WorldStreamer::DebugText() const {
    const Stats s = GetStats();
    std::ostringstream o;
    o << "stream: " << s.loaded << " loaded, " << s.loading << " loading, " << s.cellFiles << " files, "
      << s.snapshots << " remembered, " << s.sources << (s.sources == 1 ? " source" : " sources");
    return o.str();
}

} // namespace okay
