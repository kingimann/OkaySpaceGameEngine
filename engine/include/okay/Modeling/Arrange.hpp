#pragma once
// Align, distribute and centre objects along one axis, as plain numbers (the editor feeds in each object's
// bounding range on that axis and moves the object by the returned shift).
#include <algorithm>
#include <cmath>
#include <vector>

namespace okay::modeling {

struct Range1D { float lo = 0, hi = 0; };   ///< an object's extent along one axis

enum class AlignTo { Min, Centre, Max };

/// How far to move each object so all of them line up on the smallest low edge, the common centre, or the
/// largest high edge of the group. Fewer than two objects, or any non-finite number, gives all zeros.
inline std::vector<float> AlignShifts(const std::vector<Range1D>& r, AlignTo to) {
    std::vector<float> out(r.size(), 0.0f);
    if (r.size() < 2) return out;
    float lo = r[0].lo, hi = r[0].hi;
    for (const Range1D& x : r) { if (!std::isfinite(x.lo + x.hi)) return out; lo = std::min(lo, x.lo); hi = std::max(hi, x.hi); }
    for (std::size_t i = 0; i < r.size(); ++i) {
        switch (to) {
            case AlignTo::Min: out[i] = lo - r[i].lo; break;
            case AlignTo::Max: out[i] = hi - r[i].hi; break;
            case AlignTo::Centre: out[i] = (lo + hi) * 0.5f - (r[i].lo + r[i].hi) * 0.5f; break;
        }
    }
    return out;
}

/// How far to move each object so the gaps between neighbours (in order along the axis) are equal; the first and
/// last objects stay put. Fewer than three objects gives all zeros.
inline std::vector<float> DistributeShifts(const std::vector<Range1D>& r) {
    std::vector<float> out(r.size(), 0.0f);
    if (r.size() < 3) return out;
    for (const Range1D& x : r) if (!std::isfinite(x.lo + x.hi)) return out;
    std::vector<std::size_t> order(r.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return r[a].lo + r[a].hi < r[b].lo + r[b].hi; });
    const Range1D& first = r[order.front()]; const Range1D& last = r[order.back()];
    float used = 0;
    for (std::size_t i : order) used += r[i].hi - r[i].lo;
    const float span = last.hi - first.lo;
    const float gap = (span - used) / (float)(r.size() - 1);
    float cursor = first.lo;
    for (std::size_t k = 0; k < order.size(); ++k) {
        const std::size_t i = order[k];
        out[i] = cursor - r[i].lo;
        cursor += (r[i].hi - r[i].lo) + gap;
    }
    return out;
}

} // namespace okay::modeling
