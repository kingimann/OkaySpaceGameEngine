#include "okay/Scene/CellLoader.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <vector>

namespace okay::worldpart {

void QueuedCellLoader::Enqueue(const CellCoord& cell, float score) {
    if (std::isnan(score)) score = std::numeric_limits<float>::max();
    m_queue[cell] = score;
}

std::vector<CellCoord> QueuedCellLoader::Order() const {
    std::vector<std::pair<float, CellCoord>> v;
    v.reserve(m_queue.size());
    for (const auto& kv : m_queue) v.push_back({kv.second, kv.first});
    std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first < b.first;
        return a.second < b.second;
    });
    std::vector<CellCoord> out;
    out.reserve(v.size());
    for (const auto& p : v) out.push_back(p.second);
    return out;
}

int QueuedCellLoader::Pump(const CellLoadFn& load) {
    BeginPump();
    if (!load) return 0;
    const std::vector<CellCoord> order = Order();
    int started = 0;
    for (const CellCoord& c : order) {
        if (!m_queue.count(c)) continue;            // cancelled by an earlier call this pump
        if (!Allow(started)) break;
        ++started;
        const LoadStatus st = load(c);
        if (st == LoadStatus::Retry) continue;       // stays queued; not tried again until the next Pump
        m_queue.erase(c);
    }
    return started;
}

} // namespace okay::worldpart
