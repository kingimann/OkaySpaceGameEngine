// Streaming cell grid. See StreamingGrid.hpp.
#include "okay/Optimize/StreamingGrid.hpp"
#include <algorithm>
#include <cmath>

namespace okay::optimize {
namespace {

struct Want {
    float priority;
    CellCoord c;
};

}  // namespace

StreamingGrid::StreamingGrid(const StreamingConfig& cfg) { SetConfig(cfg); }

void StreamingGrid::SetConfig(const StreamingConfig& cfg) {
    cfg_ = cfg;
    if (cfg_.cellSize < 1e-3f) cfg_.cellSize = 1e-3f;
    if (cfg_.loadRadius < 0.0f) cfg_.loadRadius = 0.0f;
    if (cfg_.unloadRadius < cfg_.loadRadius) cfg_.unloadRadius = cfg_.loadRadius;
}

CellCoord StreamingGrid::Norm(const CellCoord& c) const {
    CellCoord n = c;
    if (cfg_.plane == GridPlane::XZ) n.y = 0;
    if (cfg_.plane == GridPlane::XY) n.z = 0;
    return n;
}

bool StreamingGrid::RegisterCell(const CellCoord& cIn, const std::string& asset, float cost) {
    const CellCoord c = Norm(cIn);
    auto it = cells_.find(c);
    if (it != cells_.end()) return false;
    Cell cell;
    cell.asset = asset;
    cell.cost = cost < 0.0f ? 0.0f : cost;
    cells_.emplace(c, cell);
    return true;
}

int StreamingGrid::RegisterRange(const CellCoord& lo, const CellCoord& hi, float cost) {
    int added = 0;
    for (int x = lo.x; x <= hi.x; ++x)
        for (int y = lo.y; y <= hi.y; ++y)
            for (int z = lo.z; z <= hi.z; ++z) {
                const CellCoord c{x, y, z};
                const std::string name = "cell_" + std::to_string(x) + "_" + std::to_string(y) + "_" + std::to_string(z);
                if (RegisterCell(c, name, cost)) ++added;
            }
    return added;
}

bool StreamingGrid::UnregisterCell(const CellCoord& cIn) {
    const CellCoord c = Norm(cIn);
    auto it = cells_.find(c);
    if (it == cells_.end()) return false;
    if (it->second.state == CellState::Loaded && hooks_.requestUnload) hooks_.requestUnload(c, it->second.asset);
    if (it->second.state == CellState::Loading && hooks_.cancelLoad) hooks_.cancelLoad(c, it->second.asset);
    active_.erase(c);
    cells_.erase(it);
    return true;
}

CellCoord StreamingGrid::CellOf(const Vec3& p) const {
    auto f = [&](float v) { return static_cast<int>(std::floor(v / cfg_.cellSize)); };
    CellCoord c;
    c.x = f(p.x);
    c.y = cfg_.plane == GridPlane::XZ ? 0 : f(p.y);
    c.z = cfg_.plane == GridPlane::XY ? 0 : f(p.z);
    return c;
}

float StreamingGrid::DistanceToCell(const Vec3& p, const CellCoord& c) const {
    auto gap = [&](float v, int cell) {
        const float lo = static_cast<float>(cell) * cfg_.cellSize, hi = lo + cfg_.cellSize;
        return v < lo ? lo - v : (v > hi ? v - hi : 0.0f);
    };
    const float dx = gap(p.x, c.x);
    const float dy = cfg_.plane == GridPlane::XZ ? 0.0f : gap(p.y, c.y);
    const float dz = cfg_.plane == GridPlane::XY ? 0.0f : gap(p.z, c.z);
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

StreamingFrame StreamingGrid::Update(const Vec3& pos, const Vec3& dirIn) {
    ++frame_;
    StreamingFrame out;
    // Direction in the grid's active axes.
    Vec3 dir = dirIn;
    if (cfg_.plane == GridPlane::XZ) dir.y = 0.0f;
    if (cfg_.plane == GridPlane::XY) dir.z = 0.0f;
    const float dl = dir.Magnitude();
    const bool haveDir = dl > 1e-6f;
    if (haveDir) dir = dir * (1.0f / dl);

    auto centre = [&](const CellCoord& c) {
        const float h = cfg_.cellSize * 0.5f;
        return Vec3{c.x * cfg_.cellSize + h, cfg_.plane == GridPlane::XZ ? pos.y : c.y * cfg_.cellSize + h,
                    cfg_.plane == GridPlane::XY ? pos.z : c.z * cfg_.cellSize + h};
    };
    auto priority = [&](const CellCoord& c, float dist) {
        if (dist <= 0.0f || !haveDir || cfg_.viewDirectionWeight <= 0.0f) return dist;
        Vec3 to = centre(c) - pos;
        if (cfg_.plane == GridPlane::XZ) to.y = 0.0f;
        if (cfg_.plane == GridPlane::XY) to.z = 0.0f;
        const float tl = to.Magnitude();
        if (tl < 1e-6f) return dist;
        const float facing = Vec3::Dot(dir, to * (1.0f / tl));          // -1 behind .. 1 ahead
        return dist * (1.0f + cfg_.viewDirectionWeight * (1.0f - facing));
    };

    // 1. Unloads / cancels: active cells beyond the unload radius, farthest first.
    std::vector<std::pair<float, CellCoord>> outside;
    for (const CellCoord& c : active_) {
        const float d = DistanceToCell(pos, c);
        if (d > cfg_.unloadRadius) outside.emplace_back(d, c);
    }
    std::sort(outside.begin(), outside.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first > b.first;
        return a.second < b.second;
    });
    int unloads = 0;
    for (const auto& f : outside) {
        Cell& cell = cells_[f.second];
        if (cell.state == CellState::Loading) {
            if (hooks_.cancelLoad) hooks_.cancelLoad(f.second, cell.asset);
            cell.state = CellState::Unloaded;
            active_.erase(f.second);
            out.loadCancelled.push_back(f.second);
        } else if (cell.state == CellState::Loaded) {
            if (cfg_.maxUnloadsPerFrame > 0 && unloads >= cfg_.maxUnloadsPerFrame) continue;
            if (hooks_.requestUnload) hooks_.requestUnload(f.second, cell.asset);
            cell.state = CellState::Unloaded;
            active_.erase(f.second);
            out.unloadRequested.push_back(f.second);
            ++unloads;
        }
    }

    // 2. Wanted cells: inside the load radius, not loaded, not backing off.
    std::vector<Want> wants;
    int inRadius = 0;
    const int r = static_cast<int>(std::ceil(cfg_.loadRadius / cfg_.cellSize)) + 1;
    const int dims = cfg_.plane == GridPlane::XYZ ? 3 : 2;
    double boxCount = 1.0;
    for (int i = 0; i < dims; ++i) boxCount *= (2.0 * r + 1.0);
    auto consider = [&](const CellCoord& c, const Cell& cell) {
        const float d = DistanceToCell(pos, c);
        if (d > cfg_.loadRadius) return;
        ++inRadius;
        if (cell.state != CellState::Unloaded || cell.retryAt > frame_) return;
        wants.push_back({priority(c, d), c});
    };
    if (boxCount < static_cast<double>(cells_.size())) {
        const CellCoord o = CellOf(pos);
        for (int x = o.x - r; x <= o.x + r; ++x)
            for (int y = (cfg_.plane == GridPlane::XYZ || cfg_.plane == GridPlane::XY) ? o.y - r : 0;
                 y <= ((cfg_.plane == GridPlane::XYZ || cfg_.plane == GridPlane::XY) ? o.y + r : 0); ++y)
                for (int z = (cfg_.plane == GridPlane::XY) ? 0 : o.z - r; z <= ((cfg_.plane == GridPlane::XY) ? 0 : o.z + r); ++z) {
                    const CellCoord c{x, y, z};
                    auto it = cells_.find(c);
                    if (it != cells_.end()) consider(c, it->second);
                }
    } else {
        for (const auto& kv : cells_) consider(kv.first, kv.second);
    }
    out.wanted = inRadius;
    std::sort(wants.begin(), wants.end(), [](const Want& a, const Want& b) {
        if (a.priority != b.priority) return a.priority < b.priority;
        return a.c < b.c;
    });

    // 3. Requests within the budgets.
    float used = ResidentCost();
    int inFlight = 0;
    for (const CellCoord& c : active_) if (cells_[c].state == CellState::Loading) ++inFlight;
    int issued = 0;
    float issuedCost = 0.0f;
    for (std::size_t i = 0; i < wants.size(); ++i) {
        const int remaining = static_cast<int>(wants.size() - i);
        Cell& cell = cells_[wants[i].c];
        if ((cfg_.maxLoadsPerFrame > 0 && issued >= cfg_.maxLoadsPerFrame) ||
            (cfg_.maxInFlight > 0 && inFlight >= cfg_.maxInFlight) ||
            (cfg_.maxLoadCostPerFrame > 0.0f && issued > 0 && issuedCost + cell.cost > cfg_.maxLoadCostPerFrame)) {
            out.deferredByLimit += remaining;
            break;
        }
        if (cfg_.memoryBudget > 0.0f && used + cell.cost > cfg_.memoryBudget) {
            // Make room: first loaded cells outside the load radius (farthest first), then loaded cells inside
            // it that rank strictly worse than this one (so the nearest cells always win a tight budget).
            struct Victim { bool outsideRadius; float priority; CellCoord c; };
            std::vector<Victim> evict;
            for (const CellCoord& a : active_) {
                if (cells_[a].state != CellState::Loaded) continue;
                const float d = DistanceToCell(pos, a);
                const bool outsideRadius = d > cfg_.loadRadius;
                const float pr = priority(a, d);
                if (outsideRadius || pr > wants[i].priority) evict.push_back({outsideRadius, pr, a});
            }
            std::sort(evict.begin(), evict.end(), [](const Victim& a, const Victim& b) {
                if (a.outsideRadius != b.outsideRadius) return a.outsideRadius;
                if (a.priority != b.priority) return a.priority > b.priority;
                return a.c < b.c;
            });
            for (const Victim& e : evict) {
                if (used + cell.cost <= cfg_.memoryBudget) break;
                Cell& victim = cells_[e.c];
                if (hooks_.requestUnload) hooks_.requestUnload(e.c, victim.asset);
                victim.state = CellState::Unloaded;
                active_.erase(e.c);
                used -= victim.cost;
                out.unloadRequested.push_back(e.c);
                ++out.evictedForBudget;
            }
            if (used + cell.cost > cfg_.memoryBudget) { out.deferredByBudget += remaining; break; }
        }
        if (hooks_.requestLoad && !hooks_.requestLoad(wants[i].c, cell.asset)) {
            out.deferredByLimit += remaining;     // the loader is full right now
            break;
        }
        cell.state = CellState::Loading;
        active_.insert(wants[i].c);
        used += cell.cost;
        issuedCost += cell.cost;
        ++issued; ++inFlight;
        out.loadRequested.push_back(wants[i].c);
    }
    for (const CellCoord& c : active_) {
        if (cells_[c].state == CellState::Loaded) ++out.loaded; else ++out.loading;
    }
    return out;
}

bool StreamingGrid::NotifyLoaded(const CellCoord& cIn) {
    const CellCoord c = Norm(cIn);
    auto it = cells_.find(c);
    if (it == cells_.end() || it->second.state != CellState::Loading) return false;
    it->second.state = CellState::Loaded;
    it->second.failures = 0;
    return true;
}

void StreamingGrid::NotifyLoadFailed(const CellCoord& cIn) {
    const CellCoord c = Norm(cIn);
    auto it = cells_.find(c);
    if (it == cells_.end() || it->second.state != CellState::Loading) return;
    it->second.state = CellState::Unloaded;
    ++it->second.failures;
    it->second.retryAt = frame_ + static_cast<std::uint64_t>(std::max(cfg_.retryDelayFrames, 0));
    active_.erase(c);
}

CellState StreamingGrid::State(const CellCoord& cIn) const {
    const CellCoord c = Norm(cIn);
    auto it = cells_.find(c);
    return it == cells_.end() ? CellState::Unloaded : it->second.state;
}

std::vector<CellCoord> StreamingGrid::LoadedCells() const {
    std::vector<CellCoord> v;
    for (const CellCoord& c : active_) if (cells_.at(c).state == CellState::Loaded) v.push_back(c);
    return v;
}

std::vector<CellCoord> StreamingGrid::LoadingCells() const {
    std::vector<CellCoord> v;
    for (const CellCoord& c : active_) if (cells_.at(c).state == CellState::Loading) v.push_back(c);
    return v;
}

float StreamingGrid::ResidentCost() const {
    float s = 0.0f;
    for (const CellCoord& c : active_) s += cells_.at(c).cost;
    return s;
}

int StreamingGrid::FailureCount(const CellCoord& cIn) const {
    const CellCoord c = Norm(cIn);
    auto it = cells_.find(c);
    return it == cells_.end() ? 0 : it->second.failures;
}

} // namespace okay::optimize
