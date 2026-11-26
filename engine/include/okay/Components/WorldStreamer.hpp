#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Optimize/StreamingGrid.hpp"
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace okay {

/// World Partition / level streaming — "only load the map near the player" without the 32 GB RAM bill. The world is
/// authored as a grid of cell files (one prefab per chunk, named "<prefix>_<x>_<z>.okayprefab" under `folder`). At
/// runtime this watches a target (the main camera, or a named object) and keeps just the cells near it loaded: cells
/// within `loadRadius` rings of the target's cell are streamed in, cells past `unloadRadius` are streamed out. The gap
/// between the two radii is hysteresis so a player pacing a border doesn't thrash a chunk in and out every frame.
///
/// Which cells to load is decided by optimize::StreamingGrid (nearest first, cells in front of the target first). Each
/// cell is a streamed scene load (Scene::BeginAsyncLoad with AsyncLoadOptions::stream): its file is read and its meshes
/// and textures decoded on job threads through the asset streamer (AssetManager::Stream), then its objects are made a
/// few at a time under the streaming frame budget, so a big cell never stops a frame. A cell left behind before it
/// finished is cancelled; unloading is spread over frames too. docs/engine/asset_streaming.md.
///
/// Authoring model: each cell is a self-contained prefab whose objects already sit at their final world positions (the
/// streamer does not move them). The folder is listed when the streamer starts (and on SyncNow), so a sparse /
/// non-rectangular world just works. Cells are addressed on the XZ plane (Y is "up").
class WorldStreamer : public Behaviour {
public:
    float cellSize     = 50.0f;   ///< world units per cell edge (XZ)
    int   loadRadius   = 2;       ///< load cells within this Chebyshev ring of the target's cell
    int   unloadRadius = 3;       ///< unload cells past this ring (>= loadRadius gives hysteresis)
    std::string folder = "cells/";///< directory the cell files live in (trailing slash optional)
    std::string prefix = "cell";  ///< cell file base name; file = "<folder><prefix>_<x>_<z>.okayprefab"
    std::string ext    = ".okayprefab"; ///< cell file extension
    std::string target;           ///< name of the object to follow; empty = the main camera

    /// Re-evaluate the loaded set only when the target crosses into a new cell (or cells are still loading).
    /// Turn off to force an evaluation on every Update (rarely needed).
    bool onlyOnCellChange = true;

    /// Predictive loading: cells near where the target will be this many seconds from now (from its measured
    /// velocity) are asked for too, so a fast player does not outrun the loading. 0 = off.
    float predictSeconds = 1.0f;

    /// Where one cell stands.
    enum class CellState { Unloaded, Reading, Creating, Loaded, Unloading };

    void Start() override;
    void Update(float dt) override;
    void OnDestroy() override;

    // ---- Introspection (used by the editor HUD and tests) -----------------
    /// Cells whose objects are all in, as (x,z) grid coordinates.
    std::vector<std::pair<int,int>> LoadedCells() const;
    std::size_t LoadedCount() const { return LoadedCells().size(); }
    bool IsCellLoaded(int x, int z) const { return GetCellState(x, z) == CellState::Loaded; }
    /// Loading or loaded: the streamer wants this cell.
    bool IsCellWanted(int x, int z) const;
    CellState GetCellState(int x, int z) const;
    /// Cells still being read or made.
    int  LoadingCount() const;
    /// Cell files found in the folder.
    int  KnownCellCount() const;

    /// The target's measured velocity (world units per second), what predictive loading uses.
    Vec3 MeasuredVelocity() const { return m_velocity; }

    /// The cell the target currently occupies (valid once it's been seen).
    bool CurrentCell(int& x, int& z) const {
        if (!m_haveCell) return false;
        x = m_curX; z = m_curZ; return true;
    }

    /// Path of the cell file for grid coords (x, z).
    std::string CellFile(int x, int z) const;

    /// Teleport / loading screen: list the folder again, re-evaluate around the target right now and finish every
    /// wanted cell in this call (no spreading). Cells to drop are still removed over the next frames.
    void SyncNow();

private:
    // Shared with the scene's load callbacks, which can outlive this component.
    struct Core {
        optimize::StreamingGrid grid;
        std::map<optimize::CellCoord, int> handles;   // cell -> scene load handle (loading or loaded)
        std::map<optimize::CellCoord, int> leaving;   // cell -> load handle being unloaded over frames
        Scene* scene = nullptr;                       // the scene the loads go into
    };
    std::shared_ptr<Core> m_core;
    int  m_curX = 0, m_curZ = 0;
    bool m_haveCell = false;
    bool m_scanned = false;
    Vec3 m_pos, m_dir;
    Vec3 m_velocity;          // smoothed, world units per second
    Vec3 m_lastPos;
    bool m_haveLastPos = false;
    int  m_aheadX = 0, m_aheadZ = 0;   // the cell the look-ahead point was in at the last evaluation

    int  CellOf(float v) const;
    bool TargetPose(Vec3& pos, Vec3& dir) const;
    void Configure();
    void ScanFolder();
    void Evaluate(bool force, float dt = 0.0f);
    Vec3 LookAhead() const;
};

} // namespace okay
