// World Partition: split a scene into streaming cells. See WorldPartition.hpp.
#include "okay/Scene/WorldPartition.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/Canvas.hpp"
#include "okay/Components/LevelStreaming.hpp"
#include "okay/Components/Light.hpp"
#include "okay/Components/StreamingSource.hpp"
#include "okay/Components/VoxelWorld.hpp"
#include "okay/Components/WorldStreamer.hpp"
#include "okay/Math/Bounds.hpp"
#include "okay/Physics/Collider2D.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Scene/Transform.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <set>

namespace okay {

namespace {

int CellIndex(float v, float cell) { return (int)std::floor(v / cell); }

/// Why an object stays loaded whatever its position ("" = it may stream).
std::string KeepReason(GameObject& go) {
    std::vector<GameObject*> all{&go};
    for (std::size_t i = 0; i < all.size(); ++i)
        for (Transform* c : all[i]->transform->Children()) if (c && c->gameObject) all.push_back(c->gameObject);
    for (GameObject* g : all) {
        if (g->GetComponent<Camera>()) return "camera";
        if (Light* l = g->GetComponent<Light>()) if (l->type == Light::Type::Directional) return "sun light";
        if (g->GetComponent<Canvas>()) return "UI";
        if (g->GetComponent<WorldStreamer>()) return "World Streamer";
        if (g->GetComponent<LevelStreamingVolume>()) return "Level Streaming Volume";
        if (g->GetComponent<StreamingSource>()) return "Streaming Source";
        if (g->GetComponent<VoxelWorld>()) return "Voxel World";
        if (g->tag == "Player") return "Player";
    }
    return {};
}

bool HasCollider(GameObject& go) {
    std::vector<GameObject*> all{&go};
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (all[i]->GetComponent<Collider3D>() || all[i]->GetComponent<Collider2D>()) return true;
        for (Transform* c : all[i]->transform->Children()) if (c && c->gameObject) all.push_back(c->gameObject);
    }
    return false;
}

std::string CellName(const std::string& prefix, int x, int z) {
    return prefix + "_" + std::to_string(x) + "_" + std::to_string(z);
}

}  // namespace

PartitionPlan PlanPartition(const Scene& scene, const PartitionSettings& settingsIn) {
    PartitionSettings st = settingsIn;
    if (!(st.cellSize > 0.01f)) st.cellSize = 0.01f;
    PartitionPlan plan;
    bool any = false;
    for (const auto& up : scene.Objects()) {
        GameObject* go = up.get();
        if (!go || go->IsDestroyed() || go->transient || !go->transform || go->transform->Parent()) continue;
        PartitionPlan::Entry e;
        e.object = go;
        const Bounds b = Scene::WorldBounds(*go, true);
        e.centre = b.IsEmpty() ? go->transform->Position() : b.center;
        PartitionLayer* layer = go->GetComponent<PartitionLayer>();
        const PartitionLayer::Mode mode = layer ? layer->mode : PartitionLayer::Mode::Auto;
        if (mode == PartitionLayer::Mode::AlwaysLoaded) { e.alwaysLoaded = true; e.reason = "Partition Layer"; }
        else if (mode == PartitionLayer::Mode::Auto) {
            const std::string keep = KeepReason(*go);
            if (!keep.empty()) { e.alwaysLoaded = true; e.reason = keep; }
            else if (b.IsEmpty() && !HasCollider(*go)) { e.alwaysLoaded = true; e.reason = "draws nothing"; }
            else if (!b.IsEmpty()) {
                const Vec3 sz = b.Size();
                const float span = std::max(sz.x, sz.z) / st.cellSize;
                if (span > st.maxSpanCells && st.spanRule == PartitionSettings::SpanRule::AlwaysLoaded) {
                    e.alwaysLoaded = true;
                    e.reason = "spans " + std::to_string((int)std::ceil(span)) + " cells";
                }
            }
        }
        if (!e.alwaysLoaded) {
            e.cellX = CellIndex(e.centre.x, st.cellSize);
            e.cellZ = CellIndex(e.centre.z, st.cellSize);
            if (e.reason.empty()) e.reason = "cell " + std::to_string(e.cellX) + "," + std::to_string(e.cellZ);
            ++plan.cellCounts[{e.cellX, e.cellZ}];
            ++plan.streamed;
            const float x0 = e.cellX * st.cellSize, z0 = e.cellZ * st.cellSize;
            if (!any) { plan.minX = x0; plan.minZ = z0; plan.maxX = x0 + st.cellSize; plan.maxZ = z0 + st.cellSize; any = true; }
            plan.minX = std::min(plan.minX, x0); plan.minZ = std::min(plan.minZ, z0);
            plan.maxX = std::max(plan.maxX, x0 + st.cellSize); plan.maxZ = std::max(plan.maxZ, z0 + st.cellSize);
        } else {
            ++plan.alwaysLoaded;
        }
        plan.entries.push_back(e);
    }
    return plan;
}

PartitionResult WritePartition(const Scene& scene, const PartitionSettings& settingsIn,
                               const std::string& outScenePath, const std::string& cellFolder) {
    namespace fs = std::filesystem;
    PartitionSettings st = settingsIn;
    if (!(st.cellSize > 0.01f)) st.cellSize = 0.01f;
    if (st.prefix.empty()) st.prefix = "cell";
    PartitionResult r;
    r.scenePath = outScenePath;
    r.cellFolder = cellFolder;
    std::error_code ec;
    const fs::path cellDir = fs::absolute(fs::path(cellFolder), ec);
    const fs::path sceneDir = fs::absolute(fs::path(outScenePath), ec).parent_path();

    // A partitioned scene is not partitioned again (its streamed objects are not in it any more, so every
    // cell would be removed).
    for (WorldStreamer* ws : scene.FindObjectsOfType<WorldStreamer>()) {
        if (!ws || ws->prefix != st.prefix) continue;
        fs::path f(ws->folder);
        if (f.is_relative()) f = (ws->sceneDir.empty() ? sceneDir : fs::path(ws->sceneDir)) / f;
        auto norm = [&ec](const fs::path& p) {
            std::string g = fs::weakly_canonical(p, ec).generic_string();
            while (g.size() > 1 && g.back() == '/') g.pop_back();
            return g;
        };
        if (norm(f) == norm(cellDir)) {
            r.error = "this scene is already partitioned (it has a World Streamer for these cells); run World Partition on the original scene";
            return r;
        }
    }

    const PartitionPlan plan = PlanPartition(scene, st);
    fs::create_directories(cellDir, ec);
    if (!fs::is_directory(cellDir, ec)) { r.error = "cannot create the cell folder " + cellDir.generic_string(); return r; }

    // 1. One prefab per non-empty cell.
    std::map<std::pair<int, int>, std::vector<GameObject*>> byCell;
    std::set<std::string> guidsStreamed;
    for (const PartitionPlan::Entry& e : plan.entries) {
        if (e.alwaysLoaded) continue;
        byCell[{e.cellX, e.cellZ}].push_back(e.object);
        guidsStreamed.insert(e.object->Guid());
    }
    std::set<std::string> written;
    for (const auto& kv : byCell) {
        Scene cell("cell");
        const std::string name = CellName(st.prefix, kv.first.first, kv.first.second);
        GameObject* root = cell.CreateGameObject(name);
        for (GameObject* src : kv.second) {
            GameObject* copy = SceneSerializer::Instantiate(cell, *src);
            if (!copy) { r.error = "could not copy '" + src->name + "'"; return r; }
            copy->transform->SetParent(root->transform, false);   // the root sits at the origin: positions stay world positions
        }
        const std::string file = name + ".okayprefab";
        if (!SceneSerializer::SaveObjectToFile(*root, (cellDir / file).string())) {
            r.error = "cannot write " + (cellDir / file).generic_string();
            return r;
        }
        written.insert(file);
        ++r.cellsWritten;
    }
    // 2. Cell files of this prefix that the plan no longer has go (an earlier run's cells).
    for (fs::directory_iterator it(cellDir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string fn = it->path().filename().string();
        if (written.count(fn) || fn.size() < st.prefix.size() + 12) continue;
        if (fn.compare(0, st.prefix.size() + 1, st.prefix + "_") != 0) continue;
        if (fn.size() < 11 || fn.compare(fn.size() - 11, 11, ".okayprefab") != 0) continue;
        const std::string mid = fn.substr(st.prefix.size() + 1, fn.size() - st.prefix.size() - 1 - 11);
        bool cellName = !mid.empty() && mid.find('_') != std::string::npos;
        for (char ch : mid) if (!(std::isdigit((unsigned char)ch) || ch == '-' || ch == '_')) cellName = false;
        if (!cellName) continue;
        std::error_code rm;
        if (fs::remove(it->path(), rm)) ++r.cellsRemoved;
    }

    // 3. The streaming scene: the source without the streamed objects, plus a World Streamer.
    Scene out(scene.Name());
    std::string err;
    if (!SceneSerializer::Deserialize(out, SceneSerializer::Serialize(scene), &err)) { r.error = "could not copy the scene: " + err; return r; }
    std::vector<GameObject*> drop;
    for (const auto& up : out.Objects())
        if (up && !up->transform->Parent() && guidsStreamed.count(up->Guid())) drop.push_back(up.get());
    for (GameObject* g : drop) out.DestroyImmediate(g);
    GameObject* so = out.CreateGameObject("World Streamer");
    auto* ws = so->AddComponent<WorldStreamer>();
    ws->cellSize = st.cellSize;
    ws->loadRadius = std::max(0, st.loadRadius);
    ws->unloadRadius = std::max(ws->loadRadius, st.unloadRadius);
    ws->prefix = st.prefix;
    fs::path rel = fs::relative(cellDir, sceneDir, ec);
    const bool under = !ec && !rel.empty() && rel.native().find(fs::path("..").native()) != 0;
    ws->folder = (under ? rel : cellDir).generic_string();
    if (!ws->folder.empty() && ws->folder.back() != '/') ws->folder += '/';
    if (!SceneSerializer::SaveToFile(out, outScenePath, &err)) { r.error = "cannot write " + outScenePath + ": " + err; return r; }
    r.objectsStreamed = plan.streamed;
    r.objectsAlwaysLoaded = plan.alwaysLoaded;
    r.ok = true;
    return r;
}

} // namespace okay
