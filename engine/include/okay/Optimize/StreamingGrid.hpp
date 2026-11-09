#pragma once
// ---------------------------------------------------------------------------
// World streaming helpers (feature map 27.4): the cell grid.
//
// StreamingGrid decides WHICH cells of a big world should be in memory for a
// moving viewer and hands the loading / unloading to the caller through
// hooks, so it works with the asset manager, a scene loader or a test double
// alike. It never loads anything itself and never blocks.
//
//   - Cells live on a 2D (ground plane XZ, or XY) or full 3D grid and are
//     registered by the world: RegisterCell(coord, asset, cost).
//   - A cell is loaded when the viewer is within `loadRadius` of it (distance
//     to the cell's box, not its centre) and unloaded only once the viewer is
//     farther than `unloadRadius` (> loadRadius): that gap is the hysteresis
//     that stops a viewer standing on a boundary from thrashing.
//   - Wanted cells are requested nearest first, with a bonus for cells in
//     front of the camera (`viewDirectionWeight`), within a per-frame budget
//     (new requests, cost, requests in flight) and a memory budget.
//   - Loading is asynchronous: requestLoad starts it, the caller reports back
//     with NotifyLoaded / NotifyLoadFailed whenever it finishes. A cell that
//     leaves the radius while loading gets cancelLoad.
//
// Update() is deterministic (ordered containers, ties broken by coordinate).
// ---------------------------------------------------------------------------
#include "okay/Math/Vec3.hpp"
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace okay::optimize {

enum class GridPlane {
    XZ,     ///< ground plane: cells are columns, height is ignored (the usual open world)
    XY,     ///< side view: depth is ignored
    XYZ,    ///< full 3D
};

struct CellCoord {
    int x = 0, y = 0, z = 0;
    bool operator==(const CellCoord& o) const { return x == o.x && y == o.y && z == o.z; }
    bool operator!=(const CellCoord& o) const { return !(*this == o); }
    bool operator<(const CellCoord& o) const {
        if (x != o.x) return x < o.x;
        if (y != o.y) return y < o.y;
        return z < o.z;
    }
};

enum class CellState { Unloaded, Loading, Loaded };

struct StreamingConfig {
    float cellSize = 32.0f;
    GridPlane plane = GridPlane::XZ;
    float loadRadius = 96.0f;
    float unloadRadius = 128.0f;          ///< raised to loadRadius if smaller
    int   maxLoadsPerFrame = 2;           ///< new load requests per Update (<= 0: unlimited)
    float maxLoadCostPerFrame = 0.0f;     ///< total cost of new requests per Update (0: unlimited; one is always allowed)
    int   maxInFlight = 4;                ///< cells loading at once (<= 0: unlimited)
    int   maxUnloadsPerFrame = 4;         ///< unload requests per Update (<= 0: unlimited)
    float memoryBudget = 0.0f;            ///< cost of loaded + loading cells (0: unlimited). When it is full,
                                          ///< farther loaded cells are unloaded for nearer wanted ones
    float viewDirectionWeight = 0.5f;     ///< 0 = pure distance; cells directly behind count (1 + 2w) times farther
    int   retryDelayFrames = 60;          ///< wait this many updates before retrying a failed load
};

/// What the world plugs in. Each one is optional.
struct StreamingHooks {
    /// Start loading. Return false to refuse for now (queue full): the cell stays unloaded and is retried.
    std::function<bool(const CellCoord&, const std::string& asset)> requestLoad;
    std::function<void(const CellCoord&, const std::string& asset)> requestUnload;
    /// A load that had started is no longer wanted.
    std::function<void(const CellCoord&, const std::string& asset)> cancelLoad;
};

struct StreamingFrame {
    std::vector<CellCoord> loadRequested;
    std::vector<CellCoord> unloadRequested;
    std::vector<CellCoord> loadCancelled;
    int wanted = 0;                ///< cells inside the load radius
    int loaded = 0, loading = 0;   ///< after this update
    int deferredByBudget = 0;      ///< wanted cells held back by the memory budget
    int deferredByLimit = 0;       ///< wanted cells held back by per-frame / in-flight limits
    int evictedForBudget = 0;      ///< loaded cells unloaded early to make room
};

class StreamingGrid {
public:
    explicit StreamingGrid(const StreamingConfig& cfg = StreamingConfig());

    void SetConfig(const StreamingConfig& cfg);
    const StreamingConfig& Config() const { return cfg_; }
    void SetHooks(const StreamingHooks& hooks) { hooks_ = hooks; }

    /// Add a cell the world has content for. False when it is already registered. In XZ mode
    /// y is ignored (always 0), in XY mode z is.
    bool RegisterCell(const CellCoord& c, const std::string& asset = std::string(), float cost = 1.0f);
    /// Register every cell from `lo` to `hi` inclusive, with a generated asset name "cell_x_y_z". Returns how many were new.
    int  RegisterRange(const CellCoord& lo, const CellCoord& hi, float cost = 1.0f);
    /// Remove a cell; a loaded one gets requestUnload first.
    bool UnregisterCell(const CellCoord& c);
    bool IsRegistered(const CellCoord& c) const { return cells_.count(Norm(c)) != 0; }
    int  RegisteredCount() const { return static_cast<int>(cells_.size()); }

    CellCoord CellOf(const Vec3& p) const;
    /// Distance from `p` to the cell's box (0 inside), measured in the grid's active axes.
    float DistanceToCell(const Vec3& p, const CellCoord& c) const;

    /// Advance one frame for a viewer at `pos` looking along `dir` (any length; zero = no direction bonus).
    StreamingFrame Update(const Vec3& pos, const Vec3& dir);

    /// The asset manager finished loading a cell. False when the cell was not loading
    /// (it was cancelled or unregistered): the caller should drop what it loaded.
    bool NotifyLoaded(const CellCoord& c);
    /// The load failed; the cell is retried after retryDelayFrames updates.
    void NotifyLoadFailed(const CellCoord& c);

    CellState State(const CellCoord& c) const;
    std::vector<CellCoord> LoadedCells() const;
    std::vector<CellCoord> LoadingCells() const;
    float ResidentCost() const;                    ///< cost of loaded + loading cells
    std::uint64_t Frame() const { return frame_; }
    int FailureCount(const CellCoord& c) const;

private:
    struct Cell {
        std::string asset;
        float cost = 1.0f;
        CellState state = CellState::Unloaded;
        std::uint64_t retryAt = 0;
        int failures = 0;
    };
    /// Cells outside the active axes are folded onto 0 (XZ ignores y, XY ignores z).
    CellCoord Norm(const CellCoord& c) const;
    StreamingConfig cfg_;
    StreamingHooks hooks_;
    std::map<CellCoord, Cell> cells_;
    std::set<CellCoord> active_;       // cells that are Loading or Loaded
    std::uint64_t frame_ = 0;
};

} // namespace okay::optimize
