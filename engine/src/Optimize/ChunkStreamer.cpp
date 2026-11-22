// Chunk streaming for voxel and procedural worlds. See ChunkStreamer.hpp.
#include "okay/Optimize/ChunkStreamer.hpp"
#include "okay/Core/Jobs.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace okay::optimize {

/// What a generation job leaves behind; written by the job, read on the main thread once it is done.
struct ChunkStreamer::Slot {
    ChunkPayload data;
    double ms = 0.0;
};

struct ChunkStreamer::Pending {
    JobHandle job;
    std::shared_ptr<Slot> slot;
};

ChunkStreamer::ChunkStreamer(const ChunkStreamerConfig& cfg) { SetConfig(cfg); }
ChunkStreamer::~ChunkStreamer() = default;

void ChunkStreamer::SetConfig(const ChunkStreamerConfig& cfg) {
    cfg_ = cfg;
    if (cfg_.chunkSize < 1e-3f) cfg_.chunkSize = 1e-3f;
    if (cfg_.unloadRadius < cfg_.loadRadius) cfg_.unloadRadius = cfg_.loadRadius;
    StreamingConfig g;
    g.cellSize = cfg_.chunkSize;
    g.plane = cfg_.plane;
    g.loadRadius = cfg_.loadRadius;
    g.unloadRadius = cfg_.unloadRadius;
    g.maxLoadsPerFrame = cfg_.maxRequestsPerFrame;
    g.maxInFlight = cfg_.maxInFlight;
    g.maxUnloadsPerFrame = 0;
    g.viewDirectionWeight = cfg_.viewDirectionWeight;
    g.retryDelayFrames = 30;
    grid_.SetConfig(g);
}

void ChunkStreamer::RegisterAround(const std::vector<StreamingViewer>& viewers) {
    // Register every chunk a viewer could want; forget unloaded chunks well past every viewer, so an endless
    // walk keeps the table small.
    const int r = static_cast<int>(std::ceil(cfg_.loadRadius / cfg_.chunkSize)) + 1;
    const bool useY = cfg_.plane != GridPlane::XZ;
    const bool useZ = cfg_.plane != GridPlane::XY;
    for (const StreamingViewer& v : viewers) {
        const CellCoord o = grid_.CellOf(v.position);
        for (int x = o.x - r; x <= o.x + r; ++x)
            for (int y = useY ? o.y - r : 0; y <= (useY ? o.y + r : 0); ++y)
                for (int z = useZ ? o.z - r : 0; z <= (useZ ? o.z + r : 0); ++z) {
                    const CellCoord c{x, y, z};
                    if (registered_.insert(c).second) grid_.RegisterCell(c, std::string(), 1.0f);
                }
    }
    const float forget = cfg_.unloadRadius * 1.5f + cfg_.chunkSize;
    for (auto it = registered_.begin(); it != registered_.end();) {
        if (grid_.State(*it) != CellState::Unloaded) { ++it; continue; }
        float d = 1e30f;
        for (const StreamingViewer& v : viewers) d = std::min(d, grid_.DistanceToCell(v.position, *it));
        if (d > forget) { grid_.UnregisterCell(*it); it = registered_.erase(it); }
        else ++it;
    }
}

void ChunkStreamer::Collect() {
    for (auto it = generating_.begin(); it != generating_.end();) {
        Pending& p = *it->second;
        if (p.job.Valid() && !p.job.IsDone()) { ++it; continue; }
        if (p.job.Failed()) {          // the generator threw: try again later
            grid_.NotifyLoadFailed(it->first);
        } else {
            stats_.generateMs += p.slot->ms;
            ++stats_.generated;
            ready_.push_back(Ready{it->first, std::move(p.slot->data), false});
        }
        it = generating_.erase(it);
    }
}

void ChunkStreamer::Attach(bool all) {
    const auto t0 = std::chrono::steady_clock::now();
    int n = 0;
    const int cap = cfg_.maxAttachPerFrame < 1 ? 1 : cfg_.maxAttachPerFrame;
    std::size_t i = 0;
    for (; i < ready_.size(); ++i) {
        if (!all && n >= cap) break;
        if (!all && n > 0 && cfg_.attachBudgetMs > 0.0f &&
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() >= cfg_.attachBudgetMs)
            break;
        const CellCoord c = ready_[i].c;
        if (grid_.State(c) != CellState::Loading) continue;     // cancelled while it waited
        bool ok = true;
        if (hooks_.attach && ready_[i].data) ok = hooks_.attach(c, ready_[i].data);
        if (ok && grid_.NotifyLoaded(c)) { attached_.insert(c); ++stats_.attached; }
        else if (!ok) grid_.NotifyLoadFailed(c);
        ++n;
    }
    ready_.erase(ready_.begin(), ready_.begin() + static_cast<long>(i));
    stats_.lastAttachCount = n;
    stats_.lastAttachMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (!all) stats_.maxAttachMs = std::max(stats_.maxAttachMs, stats_.lastAttachMs);
}

void ChunkStreamer::Update(const std::vector<StreamingViewer>& viewers) {
    RegisterAround(viewers);
    StreamingHooks h;
    h.requestLoad = [this](const CellCoord& c, const std::string&) {
        auto s = saved_.find(c);
        if (s != saved_.end()) {                 // an edited chunk comes back as it was left
            ready_.push_back(Ready{c, std::move(s->second), true});
            saved_.erase(s);
            ++stats_.restored;
            return true;
        }
        auto p = std::make_unique<Pending>();
        p->slot = std::make_shared<Slot>();
        auto gen = hooks_.generate;
        if (cfg_.background && gen) {
            std::shared_ptr<Slot> slot = p->slot;
            JobOptions o;
            o.name = "chunk generate";
            p->job = EngineJobs().Submit([slot, gen, c]() {
                const auto t0 = std::chrono::steady_clock::now();
                slot->data = gen(c);
                slot->ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            }, o);
        } else if (gen) {
            const auto t0 = std::chrono::steady_clock::now();
            p->slot->data = gen(c);
            p->slot->ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        }
        generating_[c] = std::move(p);
        return true;
    };
    h.requestUnload = [this](const CellCoord& c, const std::string&) {
        if (attached_.erase(c)) {
            ++stats_.detached;
            if (hooks_.detach) {
                ChunkPayload keep = hooks_.detach(c);
                if (keep) saved_[c] = std::move(keep);
            }
        }
    };
    h.cancelLoad = [this](const CellCoord& c, const std::string&) {
        auto g = generating_.find(c);
        if (g != generating_.end()) {
            if (g->second->job.Valid()) EngineJobs().Cancel(g->second->job);   // one already running finishes into its slot
            generating_.erase(g);
        }
        for (auto it = ready_.begin(); it != ready_.end(); ++it)
            if (it->c == c) {
                if (it->restored && it->data) saved_[c] = std::move(it->data);   // edits are not lost by a cancel
                ready_.erase(it);
                break;
            }
        ++stats_.cancelled;
    };
    grid_.SetHooks(h);
    grid_.Update(viewers);
    grid_.SetHooks(StreamingHooks{});
    Collect();
    Attach(false);
}

void ChunkStreamer::Flush(const std::vector<StreamingViewer>& viewers) {
    ChunkStreamerConfig keep = cfg_;
    ChunkStreamerConfig all = cfg_;
    all.maxRequestsPerFrame = 0;
    all.maxInFlight = 0;
    SetConfig(all);
    Update(viewers);
    for (auto& kv : generating_) if (kv.second->job.Valid()) EngineJobs().WaitFor(kv.second->job, 120.0);
    Collect();
    Attach(true);
    SetConfig(keep);
}

void ChunkStreamer::Clear() {
    for (auto& kv : generating_) if (kv.second->job.Valid()) EngineJobs().Cancel(kv.second->job);
    generating_.clear();
    for (Ready& r : ready_) if (r.restored && r.data) saved_[r.c] = std::move(r.data);
    ready_.clear();
    for (const CellCoord& c : std::vector<CellCoord>(attached_.begin(), attached_.end())) {
        ++stats_.detached;
        if (hooks_.detach) {
            ChunkPayload keep = hooks_.detach(c);
            if (keep) saved_[c] = std::move(keep);
        }
    }
    attached_.clear();
    for (const CellCoord& c : registered_) grid_.UnregisterCell(c);
    registered_.clear();
}

ChunkStats ChunkStreamer::Stats() const {
    ChunkStats s = stats_;
    s.loaded = static_cast<int>(grid_.LoadedCells().size());
    s.generating = static_cast<int>(generating_.size());
    s.ready = static_cast<int>(ready_.size());
    s.saved = static_cast<int>(saved_.size());
    s.registered = static_cast<int>(registered_.size());
    return s;
}

} // namespace okay::optimize
