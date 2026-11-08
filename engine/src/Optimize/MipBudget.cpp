// Texture mip streaming budget. See MipBudget.hpp.
#include "okay/Optimize/MipBudget.hpp"
#include <algorithm>
#include <cmath>
#include <queue>

namespace okay::optimize {
namespace {

int MipsOf(const MipTexture& t) {
    const int full = FullMipCount(t.width, t.height);
    return t.mipCount > 0 ? std::min(t.mipCount, full) : full;
}

int MipDim(int v, int mip) { return std::max(1, v >> std::min(mip, 30)); }

/// Last mip the texture may be demoted to: the resident floor.
int FloorMip(const MipTexture& t, const MipOptions& opt) {
    const int n = MipsOf(t);
    const int dim = std::max(opt.minDimension, 1);
    int m = 0;
    while (m + 1 < n && std::max(MipDim(t.width, m + 1), MipDim(t.height, m + 1)) >= dim) ++m;
    return m;
}

struct Entry {
    double score;      // lower = demote first
    int index;
    int mip;           // the mip this entry was computed for
};
struct EntryGreater {
    bool operator()(const Entry& a, const Entry& b) const {
        if (a.score != b.score) return a.score > b.score;
        return a.index > b.index;
    }
};

}  // namespace

int FullMipCount(int width, int height) {
    int m = std::max(std::max(width, height), 1), n = 1;
    while (m > 1) { m >>= 1; ++n; }
    return n;
}

float MipChainBytes(int width, int height, float bytesPerPixel, int fromMip, int mipCount) {
    double total = 0.0;
    for (int m = std::max(fromMip, 0); m < mipCount; ++m)
        total += static_cast<double>(MipDim(width, m)) * MipDim(height, m) * bytesPerPixel;
    return static_cast<float>(total);
}

int DesiredMip(const MipTexture& t, const MipOptions& opt) {
    const int last = MipsOf(t) - 1;
    if (t.pinned) return 0;
    const int floorMip = std::min(FloorMip(t, opt), last);
    if (t.screenPixels <= 0.0f) return floorMip;
    const float texSize = static_cast<float>(std::max(t.width, t.height)) * std::max(t.uvScale, 1e-6f);
    const float d = std::log2(std::max(texSize / t.screenPixels, 1e-6f)) + opt.mipBias;
    int m = static_cast<int>(std::floor(d));
    if (t.previousMip >= 0) {
        const float h = std::max(opt.hysteresis, 0.0f);
        const int prev = std::min(t.previousMip, last);
        if (d >= static_cast<float>(prev) - h && d < static_cast<float>(prev) + 1.0f + h) m = prev;   // stay put
    }
    return std::min(std::max(m, 0), floorMip);
}

MipPlan PlanMips(const std::vector<MipTexture>& textures, const MipOptions& opt) {
    MipPlan plan;
    plan.budgetBytes = opt.budgetBytes;
    const int n = static_cast<int>(textures.size());
    plan.choices.resize(n);
    std::vector<int> floorMip(n), count(n);
    for (int i = 0; i < n; ++i) {
        const MipTexture& t = textures[i];
        count[i] = MipsOf(t);
        floorMip[i] = t.pinned ? 0 : std::min(FloorMip(t, opt), count[i] - 1);
        MipChoice& c = plan.choices[i];
        c.id = t.id;
        c.desiredMip = DesiredMip(t, opt);
        c.mip = c.desiredMip;
        c.fullBytes = MipChainBytes(t.width, t.height, t.bytesPerPixel, 0, count[i]);
        plan.fullBytes += c.fullBytes;
    }
    auto bytesAt = [&](int i, int mip) {
        const MipTexture& t = textures[i];
        return MipChainBytes(t.width, t.height, t.bytesPerPixel, mip, count[i]);
    };
    double total = 0.0;
    for (int i = 0; i < n; ++i) {
        plan.choices[i].bytes = bytesAt(i, plan.choices[i].mip);
        total += plan.choices[i].bytes;
    }
    plan.desiredBytes = static_cast<float>(total);

    if (opt.budgetBytes > 0.0f && total > static_cast<double>(opt.budgetBytes)) {
        std::priority_queue<Entry, std::vector<Entry>, EntryGreater> heap;
        auto push = [&](int i) {
            const int m = plan.choices[i].mip;
            if (textures[i].pinned || m >= floorMip[i]) return;
            const double saved = static_cast<double>(plan.choices[i].bytes) - bytesAt(i, m + 1);
            if (saved <= 0.0) return;
            const double visible = std::max(static_cast<double>(textures[i].screenPixels), 1.0);
            heap.push({std::max(textures[i].importance, 1e-6f) * visible / saved, i, m});
        };
        for (int i = 0; i < n; ++i) push(i);
        while (total > static_cast<double>(opt.budgetBytes) && !heap.empty()) {
            const Entry e = heap.top(); heap.pop();
            MipChoice& c = plan.choices[e.index];
            if (c.mip != e.mip) continue;                          // stale
            const float next = bytesAt(e.index, c.mip + 1);
            total -= static_cast<double>(c.bytes) - next;
            ++c.mip;
            c.bytes = next;
            c.demotedForBudget = true;
            ++plan.demotions;
            push(e.index);
        }
        plan.overBudget = total > static_cast<double>(opt.budgetBytes);
    }
    plan.totalBytes = static_cast<float>(total);
    for (int i = 0; i < n; ++i) {
        MipChoice& c = plan.choices[i];
        c.width = MipDim(textures[i].width, c.mip);
        c.height = MipDim(textures[i].height, c.mip);
    }
    return plan;
}

} // namespace okay::optimize
