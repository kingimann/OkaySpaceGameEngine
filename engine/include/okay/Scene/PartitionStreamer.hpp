#pragma once
// PartitionStreamer: keeps the cells near streaming sources loaded in a Scene
// (docs/foundations/world_streaming.md). Not the older Components/WorldStreamer, which is left as it is.
//
//   MemoryChunkStore store;  store.Add(split);              // chunks from SplitScene, or DirectoryChunkStore
//   BudgetedCellLoader loader(0.002);
//   PartitionStreamer streamer(scene, WorldGrid(64), store, loader);
//   ...every frame, between updates:  streamer.Tick();
//
// Each Tick: gather the sources (StreamingSource components that are enabled and active, and any extra
// samples you give it), decide which cells are wanted (within a source's load radius) and which loaded
// cells may go (outside EVERY source's unload radius: the gap is the hysteresis), unload those (writing
// their objects' current state back to an in-memory overlay first), cancel queued cells nobody wants
// any more, queue the wanted ones by distance / priority and let the loader load what its budget
// allows. Objects keep their guid across an unload and load.
//
// Changes live in the overlay, never in the store, so authored chunk files stay as they are; a save
// keeps the overlay (CaptureState / WriteState). Only objects that came from a chunk are tracked: an
// object a game spawns into a cell belongs to the host scene and is not saved by the cell.
#include "okay/Core/SaveFramework.hpp"
#include "okay/Scene/CellLoader.hpp"
#include "okay/Scene/StreamingSource.hpp"
#include "okay/Scene/WorldChunk.hpp"
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace okay::worldpart {

/// One point that wants cells, with its numbers already cleaned (see StreamingSource::Effective*).
struct SourceSample {
    Vec3 position;
    float loadRadius = 0.0f;
    float unloadRadius = 0.0f;   ///< >= loadRadius
    float priority = 1.0f;       ///< > 0; a cell's urgency is distance / priority
};

/// What PlanCells decided.
struct CellPlan {
    /// Cells wanted (known, within some source's load radius), most urgent first: (cell, score).
    std::vector<std::pair<CellCoord, float>> wanted;
    std::vector<CellCoord> unload;   ///< loaded cells outside every source's unload radius
    std::vector<CellCoord> cancel;   ///< queued cells outside every source's unload radius
};

/// The streaming decision, with no scene and no loader: a pure function (for tests and tools).
/// `known` is the sorted list of cells that have a chunk. A source with a non-finite position or an
/// unusable radius is ignored. With no usable source nothing is wanted and nothing is unloaded
/// (a streamer without sources does not empty the world). The two lists `loaded` and `queued`
/// decide `unload` and `cancel`.
CellPlan PlanCells(const WorldGrid& grid, const std::vector<SourceSample>& sources, const std::vector<CellCoord>& known,
                   const std::vector<CellCoord>& loaded, const std::vector<CellCoord>& queued);

enum class CellState { Unloaded, Queued, Loaded, Failed };

struct TickReport {
    int sources = 0;            ///< usable sources this tick
    int wanted = 0;
    int queued = 0;             ///< cells newly queued
    int loaded = 0;             ///< cells that finished loading this tick
    int unloaded = 0;
    int cancelled = 0;
    int failures = 0;
    int pending = 0;            ///< still queued after the tick
    std::vector<std::string> errors;
};

class PartitionStreamer {
public:
    /// All four must outlive the streamer. A cell is known when the store holds a chunk named for it
    /// (call RefreshCatalog after the store gains chunks).
    PartitionStreamer(Scene& scene, const WorldGrid& grid, IChunkStore& store, ICellLoader& loader);

    /// Samples used besides the scene's StreamingSource components (a camera position, a network peer).
    void SetExtraSources(std::vector<SourceSample> extra) { m_extra = std::move(extra); }
    /// Re-read the store's list of chunk names.
    void RefreshCatalog();
    const std::vector<CellCoord>& KnownCells() const { return m_known; }

    TickReport Tick();

    CellState State(const CellCoord& cell) const;
    std::vector<CellCoord> LoadedCells() const;
    std::size_t LoadedCount() const;
    /// Guids of the top-level objects a loaded cell holds (empty if not loaded).
    std::vector<std::string> RootsOf(const CellCoord& cell) const;
    /// Ticks to wait before a failed cell is tried again (default 60; at least 1).
    void SetRetryTicks(int ticks) { m_retryTicks = ticks < 1 ? 1 : ticks; }

    /// Load / unload one cell now, outside the plan (tools, a teleport). LoadNow bypasses the loader.
    /// Both return false with `error` when it cannot be done; an unload that cannot capture a tree
    /// leaves the cell loaded.
    bool LoadNow(const CellCoord& cell, std::string* error = nullptr);
    bool UnloadNow(const CellCoord& cell, std::string* error = nullptr);
    /// Unload every loaded cell. With `keepState` false the objects are dropped without being written
    /// back (their changes are lost). Returns how many were unloaded.
    int UnloadAll(bool keepState = true);

    // ---- saves ----
    /// Cells whose state differs from the store (what a save must keep).
    std::size_t ModifiedCount() const { return m_overlay.size(); }
    /// Write the live state of every loaded cell into the overlay, and return the overlay as lines
    /// that hold no line breaks (so they fit a SaveBlock string list). Loaded cells stay loaded.
    std::vector<std::string> CaptureState();
    /// Put a CaptureState result back: every streamed object is dropped, the overlay is replaced, and
    /// the next Tick loads cells from it. All or nothing: a bad line (damaged chunk, wrong name) changes
    /// nothing and returns false with `error`.
    bool RestoreState(const std::vector<std::string>& lines, std::string* error = nullptr);
    /// CaptureState into a save block as "chunks.*" strings, and the reverse. ReadState returns false
    /// (nothing changed) when the block holds no streaming state or it is damaged.
    void WriteState(SaveWriter& w);
    bool ReadState(const SaveReader& r, std::string* error = nullptr);

    /// Line escaping used by the state lines (exposed for tests): \\, \n, \r, \t and NUL.
    static std::string EscapeLine(const std::string& s);
    static bool UnescapeLine(const std::string& s, std::string& out);

private:
    struct Loaded {
        std::vector<std::string> roots;
        std::uint64_t hash = 0;   // of the canonical chunk text as loaded
    };
    Scene& m_scene;
    WorldGrid m_grid;
    IChunkStore& m_store;
    ICellLoader& m_loader;
    std::vector<SourceSample> m_extra;
    std::vector<CellCoord> m_known;
    std::map<CellCoord, Loaded> m_loaded;
    std::set<CellCoord> m_queued;
    std::map<CellCoord, std::uint64_t> m_retryAt;     // failed cells: tick when they may be queued again
    std::map<std::string, std::string> m_overlay;     // chunk name -> text that replaces the store's
    std::uint64_t m_tick = 0;
    int m_retryTicks = 60;

    bool ReadChunkText(const std::string& name, std::string& out) const;
    LoadStatus LoadCell(const CellCoord& cell, std::string* error);
    bool CaptureCell(const CellCoord& cell, const Loaded& rec, std::string& text, std::vector<GameObject*>& live, std::string* error) const;
    std::vector<SourceSample> GatherSources() const;
};

} // namespace okay::worldpart
