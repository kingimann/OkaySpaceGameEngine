#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Optimize/StreamingGrid.hpp"
#include "okay/Scene/SceneStreamQueue.hpp"
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace okay {

class GameObject;

/// World streaming / World Partition (feature map 2.6.6, 27.4): only the part of a big map near the
/// player is in memory. The world is a grid of cell files on the XZ plane, one prefab per cell, named
/// "<prefix>_<x>_<z>.okayprefab" under `folder` (the World Partition tool writes them; hand-made cells
/// work the same). Each cell's objects already sit at their final world positions.
///
/// How it streams (docs/engine/world_streaming.md):
///   - The cells are found by scanning `folder` once at Start (Rescan() looks again). A relative folder is
///     also looked for next to the scene file. Missing cells are simply not there (a sparse world works).
///   - `Optimize/StreamingGrid` decides what to load: every cell within `loadRadius` cells of a source
///     (square rings), unloaded only past `unloadRadius` (the gap is hysteresis, so pacing a border does not
///     thrash), nearest first with a bonus for cells in front of the source, at most `maxLoadsPerFrame` new
///     requests a frame and `maxInFlight` at once, and an optional memory budget (cell file sizes).
///   - Sources: `target` (a named object, or the main camera when empty) and, with `followSources`, every
///     enabled StreamingSource in the scene.
///   - Loading never blocks a frame (`backgroundLoading`): the file is read and unpacked on the job system,
///     then the objects are made a few per frame in `attachBudgetMs` of main-thread time.
///   - `rememberChanges`: when a cell unloads, its objects as they are now (moved, damaged, removed) are kept in
///     memory and come back the same when the cell loads again; off, changes are lost on unload. A cell whose
///     voxel terrain was dug or built on stays loaded either way (the voxel field is saved quantized).
///   - `debugOverlay` draws the grid over the world: green = loaded, yellow = loading, grey = on disk.
class WorldStreamer : public Behaviour {
public:
    float cellSize     = 50.0f;   ///< world units per cell edge (XZ)
    int   loadRadius   = 2;       ///< load cells within this ring of a source's cell
    int   unloadRadius = 3;       ///< unload cells past this ring (>= loadRadius gives hysteresis)
    std::string folder = "cells/";///< directory the cell files live in (trailing slash optional)
    std::string prefix = "cell";  ///< cell file base name; file = "<folder><prefix>_<x>_<z><ext>"
    std::string ext    = ".okayprefab"; ///< cell file extension
    std::string target;           ///< name of the object to follow; empty = the main camera

    /// Plan again only when a source enters a new cell or work is pending (a load in flight, a request held
    /// back by a limit). Off: plan every frame.
    bool onlyOnCellChange = true;

    // ---- streaming (saved as trailing fields of the `worldstreamer` record) --------------------------
    bool  backgroundLoading = true;   ///< read cells on the job system and make them over several frames
    float attachBudgetMs = 2.0f;      ///< main-thread milliseconds per frame for making streamed objects
    int   maxLoadsPerFrame = 2;       ///< new cell requests per frame (0 = no limit)
    int   maxInFlight = 4;            ///< cells loading at once (0 = no limit)
    float viewDirectionWeight = 0.5f; ///< 0 = nearest first; higher = cells in front of a source first
    float memoryBudgetMB = 0.0f;      ///< 0 = no limit; otherwise the cell files resident may not exceed this
    bool  rememberChanges = true;     ///< keep each unloaded cell's objects as they were (see above)
    bool  followSources = true;       ///< StreamingSource components also keep cells loaded
    bool  debugOverlay = false;       ///< draw the cell grid coloured by state
    float debugHeight = 0.3f;         ///< world height of the overlay

    /// Folder of the scene file this streamer was loaded from (not saved): a relative `folder` is also
    /// looked for there, so a scene opened from anywhere finds its cells.
    std::string sceneDir;

    WorldStreamer();
    ~WorldStreamer() override;

    void Start() override;
    void Update(float dt) override;
    void OnDestroy() override;

    // ---- Introspection (used by the editor, the overlay and tests) -------------------------------------
    std::vector<std::pair<int,int>> LoadedCells() const;   ///< cells fully in the scene, as (x, z)
    std::size_t LoadedCount() const;
    bool IsCellLoaded(int x, int z) const;
    bool IsCellLoading(int x, int z) const;
    bool HasCellFile(int x, int z) const;                  ///< found by the last scan
    int  CellFileCount() const;
    /// Every cell file the last scan found, as (x, z) (Rescan() scans; the editor calls it for a selected streamer).
    std::vector<std::pair<int,int>> CellFiles() const;
    /// The cell a source occupies (the target, or the first StreamingSource), once one has been seen.
    bool CurrentCell(int& x, int& z) const;
    /// Path of the cell file for grid coords (x, z) as configured (before the scene-folder lookup).
    std::string CellFile(int x, int z) const;
    /// The folder actually scanned (after the scene-folder lookup).
    std::string ResolvedFolder() const;
    /// Top-level objects of a loaded cell.
    std::vector<std::uint64_t> CellRoots(int x, int z) const;
    /// A remembered copy of an unloaded cell exists.
    bool HasSnapshot(int x, int z) const;
    std::size_t SnapshotCount() const;
    /// Drop every remembered copy: cells load from their files again.
    void ForgetChanges();

    /// Scan the folder again (new cell files), then plan.
    void Rescan();
    /// Plan and load every wanted cell now, blocking (the first frame, a teleport).
    void SyncNow();

    struct Stats {
        int cellFiles = 0, loaded = 0, loading = 0, wanted = 0, snapshots = 0;
        std::uint64_t loadsRequested = 0, loadsFinished = 0, loadsFailed = 0, unloads = 0, plans = 0;
        int sources = 0;
        double readMs = 0.0;          ///< worker time spent reading cell files
    };
    Stats GetStats() const;
    /// One line for a HUD or the log: "stream: 9 loaded, 2 loading, 25 files, 3 remembered".
    std::string DebugText() const;
    /// The places cells are kept loaded around this frame.
    std::vector<optimize::StreamingViewer> Sources() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m;
    void Plan(bool force);
    void EnsureSetup();
};

} // namespace okay
