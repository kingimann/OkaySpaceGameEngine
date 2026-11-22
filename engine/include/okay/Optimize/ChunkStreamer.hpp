#pragma once
// ---------------------------------------------------------------------------
// Chunk streaming for voxel and procedural worlds (feature map 27.4): chunks
// identified by an integer coordinate are generated (or restored from a saved
// copy), put into the world, taken out and remembered around one or more
// moving viewers. The world can be endless: chunks are registered with the
// planner as viewers approach and forgotten again far behind them.
//
//   - Decisions come from StreamingGrid (radius with hysteresis, nearest and
//     in-front-of-the-viewer first, per-frame limits).
//   - `generate` runs on the engine job system (EngineJobs()), off the main
//     thread; it must build plain data only and never touch the scene.
//   - `attach` runs on the main thread, at most `maxAttachPerFrame` chunks and
//     `attachBudgetMs` milliseconds a frame (at least one chunk), so mesh
//     building and upload are spread over frames.
//   - `detach` takes a chunk out. When it returns data (an edited chunk), the
//     data is kept and the next load of that chunk uses it instead of
//     generating it again.
//
// This header has no locks or threads (rule 21a).
// ---------------------------------------------------------------------------
#include "okay/Optimize/StreamingGrid.hpp"
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <vector>

namespace okay::optimize {

using ChunkPayload = std::shared_ptr<void>;

struct ChunkStreamerConfig {
    float chunkSize = 16.0f;            ///< world units per chunk edge
    GridPlane plane = GridPlane::XZ;    ///< XZ: columns (terrain); XYZ: cubes (space, caves going down for ever)
    float loadRadius = 64.0f;           ///< chunks whose box is this close to a viewer are wanted
    float unloadRadius = 96.0f;         ///< chunks farther than this from every viewer go
    int   maxRequestsPerFrame = 4;      ///< new generations started per frame (0 = no limit)
    int   maxInFlight = 8;              ///< generations running at once (0 = no limit)
    int   maxAttachPerFrame = 2;        ///< chunks put into the world per frame (at least 1)
    float attachBudgetMs = 4.0f;        ///< main-thread time per frame for attaching (at least one chunk)
    float viewDirectionWeight = 0.5f;   ///< chunks in front of a viewer first
    bool  background = true;            ///< false: generate on the calling thread (tests)
};

struct ChunkHooks {
    /// Worker thread: the chunk's data (null = nothing there, e.g. empty sky: the chunk counts as loaded).
    std::function<ChunkPayload(const CellCoord&)> generate;
    /// Main thread: put the chunk into the world. False = it failed (tried again later).
    std::function<bool(const CellCoord&, ChunkPayload&)> attach;
    /// Main thread: take it out. Return data to keep for the next load (an edited chunk), or null.
    std::function<ChunkPayload(const CellCoord&)> detach;
};

struct ChunkStats {
    int loaded = 0, generating = 0, ready = 0, saved = 0, registered = 0;
    std::uint64_t generated = 0, restored = 0, attached = 0, detached = 0, cancelled = 0;
    double generateMs = 0.0;            ///< worker time spent generating, summed
    double lastAttachMs = 0.0;          ///< main-thread time the last Update spent attaching
    double maxAttachMs = 0.0;           ///< the most any one Update spent attaching
    int lastAttachCount = 0;
};

class ChunkStreamer {
public:
    explicit ChunkStreamer(const ChunkStreamerConfig& cfg = ChunkStreamerConfig());
    ~ChunkStreamer();
    ChunkStreamer(const ChunkStreamer&) = delete;
    ChunkStreamer& operator=(const ChunkStreamer&) = delete;

    void SetConfig(const ChunkStreamerConfig& cfg);
    const ChunkStreamerConfig& Config() const { return cfg_; }
    void SetHooks(ChunkHooks hooks) { hooks_ = std::move(hooks); }

    /// Once a frame on the main thread.
    void Update(const std::vector<StreamingViewer>& viewers);
    /// Plan, then finish every generation and attach every wanted chunk now (blocking): the first frame.
    void Flush(const std::vector<StreamingViewer>& viewers);
    /// Take every chunk out (detach runs, so edits are kept) and stop what is generating.
    void Clear();

    CellCoord ChunkOf(const Vec3& p) const { return grid_.CellOf(p); }
    CellState State(const CellCoord& c) const { return grid_.State(c); }
    bool IsLoaded(const CellCoord& c) const { return grid_.State(c) == CellState::Loaded; }
    std::vector<CellCoord> LoadedChunks() const { return grid_.LoadedCells(); }
    int  LoadedCount() const { return static_cast<int>(grid_.LoadedCells().size()); }

    bool HasSaved(const CellCoord& c) const { return saved_.count(c) != 0; }
    /// Keep data for a chunk (it is used the next time the chunk loads).
    void Save(const CellCoord& c, ChunkPayload data) { if (data) saved_[c] = std::move(data); }
    const std::map<CellCoord, ChunkPayload>& SavedChunks() const { return saved_; }
    void ForgetSaved() { saved_.clear(); }

    ChunkStats Stats() const;

private:
    struct Slot;
    struct Pending;
    void RegisterAround(const std::vector<StreamingViewer>& viewers);
    void Collect();
    void Attach(bool all);
    ChunkStreamerConfig cfg_;
    ChunkHooks hooks_;
    StreamingGrid grid_;
    std::set<CellCoord> registered_;
    std::map<CellCoord, std::unique_ptr<Pending>> generating_;
    struct Ready { CellCoord c; ChunkPayload data; bool restored = false; };
    std::vector<Ready> ready_;                                 // generated or restored, waiting to be attached (in order)
    std::set<CellCoord> attached_;
    std::map<CellCoord, ChunkPayload> saved_;
    ChunkStats stats_;
};

} // namespace okay::optimize
