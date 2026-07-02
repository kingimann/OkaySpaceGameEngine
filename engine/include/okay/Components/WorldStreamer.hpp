#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include <map>
#include <vector>
#include <string>
#include <cmath>

namespace okay {

/// World Partition / level streaming — Unreal's "only load the map near the
/// player" without the 32 GB RAM bill. The world is authored as a grid of cell
/// files (one prefab per chunk, named "<prefix>_<x>_<z>.okayprefab" under
/// `folder`). At runtime this watches a target (the main camera, or a named
/// object) and keeps just the cells near it loaded: cells within `loadRadius`
/// are instantiated, cells past `unloadRadius` are destroyed. The gap between
/// the two radii is hysteresis so a player pacing a border doesn't thrash a
/// chunk in and out every frame.
///
/// Authoring model: each cell is a self-contained prefab whose objects already
/// sit at their final world positions (the streamer does not move them). Bake a
/// big map once into cell files, drop this on an empty object, point it at the
/// folder, and huge worlds stream in around the player. Missing cell files are
/// skipped silently, so a sparse / non-rectangular world just works.
///
/// Cells are addressed on the XZ plane (Y is "up"), matching the 3D world.
class WorldStreamer : public Behaviour {
public:
    float cellSize     = 50.0f;   ///< world units per cell edge (XZ)
    int   loadRadius   = 2;       ///< load cells within this Chebyshev ring of the target's cell
    int   unloadRadius = 3;       ///< unload cells past this ring (>= loadRadius gives hysteresis)
    std::string folder = "cells/";///< directory the cell files live in (trailing slash optional)
    std::string prefix = "cell";  ///< cell file base name; file = "<folder><prefix>_<x>_<z>.okayprefab"
    std::string ext    = ".okayprefab"; ///< cell file extension
    std::string target;           ///< name of the object to follow; empty = the main camera

    /// Re-evaluate the loaded set only when the target crosses into a new cell.
    /// Turn off to force an evaluation on every Update (rarely needed).
    bool onlyOnCellChange = true;

    void Start() override { Evaluate(true); }

    void Update(float /*dt*/) override {
        Vec3 p;
        if (!TargetPos(p)) return;
        int cx = CellOf(p.x), cz = CellOf(p.z);
        if (onlyOnCellChange && m_haveCell && cx == m_curX && cz == m_curZ) return;
        Evaluate(false, cx, cz);
    }

    void OnDestroy() override {
        Scene* s = GetScene();
        if (s) for (auto& kv : m_loaded) if (kv.second) s->Destroy(kv.second);
        m_loaded.clear();
    }

    // ---- Introspection (used by the editor HUD and tests) -----------------
    /// Cells currently resident, as (x,z) grid coordinates.
    std::vector<std::pair<int,int>> LoadedCells() const {
        std::vector<std::pair<int,int>> out; out.reserve(m_loaded.size());
        for (auto& kv : m_loaded) out.push_back(kv.first);
        return out;
    }
    std::size_t LoadedCount() const { return m_loaded.size(); }
    bool IsCellLoaded(int x, int z) const { return m_loaded.count({x, z}) != 0; }

    /// The cell the target currently occupies (valid once it's been seen).
    bool CurrentCell(int& x, int& z) const {
        if (!m_haveCell) return false;
        x = m_curX; z = m_curZ; return true;
    }

    /// Path of the cell file for grid coords (x, z).
    std::string CellFile(int x, int z) const {
        std::string f = folder;
        if (!f.empty() && f.back() != '/' && f.back() != '\\') f += '/';
        return f + prefix + "_" + std::to_string(x) + "_" + std::to_string(z) + ext;
    }

    /// Force a re-evaluation right now (e.g. after teleporting the target).
    void SyncNow() { Evaluate(true); }

private:
    std::map<std::pair<int,int>, GameObject*> m_loaded;
    int  m_curX = 0, m_curZ = 0;
    bool m_haveCell = false;

    int CellOf(float v) const {
        if (cellSize <= 0.0f) return 0;
        return (int)std::floor(v / cellSize);
    }

    bool TargetPos(Vec3& out) const {
        Scene* s = GetScene();
        if (!s) return false;
        Transform* t = nullptr;
        if (!target.empty()) {
            if (GameObject* go = s->Find(target)) t = go->transform;
        } else if (s->mainCamera && s->mainCamera->gameObject) {
            t = s->mainCamera->gameObject->transform;
        }
        if (!t) return false;
        out = t->Position();
        return true;
    }

    void Evaluate(bool force) {
        Vec3 p;
        if (!TargetPos(p)) return;
        Evaluate(force, CellOf(p.x), CellOf(p.z));
    }

    void Evaluate(bool /*force*/, int cx, int cz) {
        Scene* s = GetScene();
        if (!s) return;
        m_curX = cx; m_curZ = cz; m_haveCell = true;

        // Unload anything beyond the unload ring (Chebyshev distance).
        for (auto it = m_loaded.begin(); it != m_loaded.end(); ) {
            int dx = std::abs(it->first.first - cx);
            int dz = std::abs(it->first.second - cz);
            if (std::max(dx, dz) > unloadRadius) {
                if (it->second) s->Destroy(it->second);
                it = m_loaded.erase(it);
            } else {
                ++it;
            }
        }

        // Load anything inside the load ring that isn't resident yet.
        int r = loadRadius < 0 ? 0 : loadRadius;
        for (int z = cz - r; z <= cz + r; ++z) {
            for (int x = cx - r; x <= cx + r; ++x) {
                auto key = std::make_pair(x, z);
                if (m_loaded.count(key)) continue;
                std::string err;
                GameObject* root = SceneSerializer::InstantiateFromFile(*s, CellFile(x, z), &err);
                // Record the slot even when the file is missing/empty (root ==
                // nullptr) so we don't retry a non-existent cell every frame.
                m_loaded[key] = root;
            }
        }
    }
};

} // namespace okay
