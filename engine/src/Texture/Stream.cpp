// okay::tex streaming planner on top of optimize::PlanMips; see Stream.hpp.
#include "okay/Texture/Stream.hpp"
#include <algorithm>
#include <cmath>

namespace okay::tex {

float ProjectedPixels(float worldSize, float dist, float fovY, int screenHeight) {
    if (!(worldSize > 0.0f) || screenHeight <= 0 || !(fovY > 0.0f) || !(fovY < 3.1f)) return 0.0f;
    const float d = std::max(dist, 0.01f);
    return worldSize / (d * 2.0f * std::tan(fovY * 0.5f)) * float(screenHeight);
}

StreamPlan StreamPlanner::Plan(const StreamCamera& cam, const std::vector<StreamTexture>& textures,
                               const std::vector<StreamInstance>& instances, const StreamOptions& opt) {
    StreamPlan out;
    const std::size_t n = textures.size();
    std::vector<optimize::MipTexture> in(n);
    std::map<int, std::size_t> index;
    for (std::size_t i = 0; i < n; ++i) {
        const StreamTexture& t = textures[i];
        optimize::MipTexture& m = in[i];
        m.id = t.id; m.width = std::max(t.width, 1); m.height = std::max(t.height, 1); m.mipCount = t.mipCount;
        m.bytesPerPixel = t.bytesPerPixel; m.importance = t.importance; m.pinned = t.pinned;
        m.previousMip = ResidentMip(t.id);
        index[t.id] = i;
    }
    // biggest on-screen size per texture (the tiling of that instance goes with it)
    std::vector<float> best(n, 0.0f);
    for (const StreamInstance& s : instances) {
        if (!s.visible) continue;
        auto it = index.find(s.textureId);
        if (it == index.end()) continue;
        const float dx = s.x - cam.x, dy = s.y - cam.y, dz = s.z - cam.z;
        const float px = ProjectedPixels(s.worldSize, std::sqrt(dx * dx + dy * dy + dz * dz), cam.fovY, cam.screenHeight);
        const float effective = px / std::max(s.uvScale, 1e-6f);   // texel density: more tiling needs a sharper mip
        if (effective > best[it->second]) { best[it->second] = effective; in[it->second].screenPixels = px; in[it->second].uvScale = s.uvScale; }
    }
    optimize::MipOptions mo;
    mo.budgetBytes = opt.budgetBytes; mo.mipBias = opt.mipBias; mo.minDimension = opt.minDimension; mo.hysteresis = opt.hysteresis;
    const optimize::MipPlan plan = optimize::PlanMips(in, mo);
    out.overBudget = plan.overBudget;

    out.requests.resize(n);
    std::vector<std::size_t> loadsOrder;
    for (std::size_t i = 0; i < n; ++i) {
        StreamRequest& r = out.requests[i];
        const optimize::MipChoice& c = plan.choices[i];
        r.id = textures[i].id; r.mip = c.mip; r.desiredMip = c.desiredMip; r.previousMip = in[i].previousMip;
        r.screenPixels = in[i].screenPixels;
        const int count = in[i].mipCount > 0 ? std::min(in[i].mipCount, optimize::FullMipCount(in[i].width, in[i].height))
                                              : optimize::FullMipCount(in[i].width, in[i].height);
        if (r.previousMip < 0 || r.mip < r.previousMip) loadsOrder.push_back(i);
        if (r.previousMip >= 0 && r.mip > r.previousMip)
            r.evictBytes = optimize::MipChainBytes(in[i].width, in[i].height, in[i].bytesPerPixel, r.previousMip, count) -
                           optimize::MipChainBytes(in[i].width, in[i].height, in[i].bytesPerPixel, r.mip, count);
    }
    // loads, most visible first, until the per-frame upload cap is used up (at least one always goes through)
    std::stable_sort(loadsOrder.begin(), loadsOrder.end(), [&](std::size_t a, std::size_t b) {
        return out.requests[a].screenPixels > out.requests[b].screenPixels;
    });
    float spent = 0.0f;
    bool first = true;
    for (std::size_t i : loadsOrder) {
        StreamRequest& r = out.requests[i];
        const optimize::MipTexture& t = in[i];
        const int count = t.mipCount > 0 ? std::min(t.mipCount, optimize::FullMipCount(t.width, t.height)) : optimize::FullMipCount(t.width, t.height);
        const float have = r.previousMip < 0 ? 0.0f : optimize::MipChainBytes(t.width, t.height, t.bytesPerPixel, r.previousMip, count);
        const float want = optimize::MipChainBytes(t.width, t.height, t.bytesPerPixel, r.mip, count);
        float bytes = want - have;
        if (opt.uploadBytesPerFrame > 0.0f && !first && spent + bytes > opt.uploadBytesPerFrame && r.previousMip >= 0) {
            r.deferred = true; r.mip = r.previousMip; ++out.deferred;     // keep the blurrier mip a little longer
            continue;
        }
        first = false;
        spent += bytes; r.loadBytes = bytes; ++out.loads;
    }
    for (std::size_t i = 0; i < n; ++i) {
        StreamRequest& r = out.requests[i];
        const optimize::MipTexture& t = in[i];
        const int count = t.mipCount > 0 ? std::min(t.mipCount, optimize::FullMipCount(t.width, t.height)) : optimize::FullMipCount(t.width, t.height);
        if (r.evictBytes > 0.0f) ++out.evictions;
        out.loadBytes += r.loadBytes; out.evictBytes += r.evictBytes;
        out.residentBytes += optimize::MipChainBytes(t.width, t.height, t.bytesPerPixel, r.mip, count);
        m_resident[r.id] = r.mip;
    }
    return out;
}

} // namespace okay::tex
