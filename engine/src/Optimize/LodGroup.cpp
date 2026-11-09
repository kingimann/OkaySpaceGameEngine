// LOD group selection. See LodGroup.hpp.
#include "okay/Optimize/LodGroup.hpp"
#include <algorithm>
#include <cmath>

namespace okay::optimize {
namespace {
float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
}

bool LodGroup::Valid() const {
    const int n = LevelCount();
    if (n == 0 || size <= 0.0f || cullScreenHeight < 0.0f) return false;
    float prev = 2.0f;
    for (int i = 0; i + 1 < n; ++i) {
        const float t = levels[i].screenHeight;
        if (!(t > 0.0f && t <= 1.0f && t < prev)) return false;
        prev = t;
    }
    return n < 2 || cullScreenHeight < prev;
}

float LodView::FovHeightPx() const {
    const float t = std::tan(std::max(fovYDeg, 1e-3f) * 0.5f * 0.01745329252f);
    return viewportHeightPx / (2.0f * std::max(t, 1e-6f));
}

float LodGroupScreenHeight(const LodGroup& g, float distance, const LodView& view, float bias) {
    return std::max(bias, 0.0f) * LodScreenHeightFor(g.size, distance, view.fovYDeg);
}

LodSelection SelectLodByHeight(const LodGroup& g, float h, int previousLevel, float cullHeight) {
    LodSelection out;
    const int n = g.LevelCount();
    h = std::max(h, 0.0f);
    out.screenHeight = h;
    if (n == 0) { out.level = -1; out.culled = true; return out; }
    const float cull = std::max(cullHeight >= 0.0f ? cullHeight : g.cullScreenHeight, 0.0f);
    const float hy = std::min(std::max(g.hysteresis, 0.0f), 0.9f);
    // Threshold for leaving level i (to i+1, or to culled for the last level).
    auto thr = [&](int i) { return i < n - 1 ? g.levels[i].screenHeight : cull; };
    int cur;
    if (previousLevel < 0 || previousLevel > n) {
        cur = 0;
        while (cur < n && thr(cur) > 0.0f && h < thr(cur)) ++cur;
    } else {
        cur = previousLevel;
        while (cur < n && thr(cur) > 0.0f && h < thr(cur) * (1.0f - hy)) ++cur;
        while (cur > 0 && h >= thr(cur - 1) * (1.0f + hy)) --cur;
    }
    out.state = cur;
    if (cur >= n) { out.level = -1; out.culled = true; return out; }
    out.level = cur;
    if (g.fadeMode == LodFadeMode::CrossFade && cur < n - 1) {
        const float t = thr(cur);
        const float band = t * std::max(g.fadeWidth, 0.0f);
        if (band > 0.0f) {
            const float f = Clamp01((t + band - h) / band);
            if (f > 0.0f) { out.fadeTo = cur + 1; out.fade = f; }
        }
    }
    return out;
}

LodSelection SelectLod(const LodGroup& g, float distance, const LodView& view, float bias, int previousLevel) {
    const float h = LodGroupScreenHeight(g, distance, view, bias);
    float cull = g.cullScreenHeight;
    if (g.minPixelHeight > 0.0f && view.viewportHeightPx > 0.0f)
        cull = std::max(cull, g.minPixelHeight / view.viewportHeightPx);
    return SelectLodByHeight(g, h, previousLevel, cull);
}

float LodSwitchDistance(const LodGroup& g, int level, const LodView& view, float bias) {
    const int n = g.LevelCount();
    if (level < 0 || level >= n) return 0.0f;
    const float t = level < n - 1 ? g.levels[level].screenHeight : g.cullScreenHeight;
    if (t <= 0.0f) return Mathf::Infinity;
    return LodDistanceFor(g.size * std::max(bias, 1e-6f), t, view.fovYDeg);
}

LodGroup MakeLodGroup(const LodChain& chain, float cullScreenHeight) {
    LodGroup g;
    g.size = chain.objectSize > 0.0f ? chain.objectSize : 1.0f;
    g.cullScreenHeight = std::max(cullScreenHeight, 0.0f);
    const int n = static_cast<int>(chain.levels.size());
    for (int i = 0; i < n; ++i) {
        LodLevel l;
        l.meshIndex = i;
        l.triangles = chain.levels[i].triangles;
        l.name = chain.levels[i].mesh.name;
        l.screenHeight = i + 1 < n ? chain.levels[i + 1].switchScreenHeight : g.cullScreenHeight;
        g.levels.push_back(l);
    }
    return g;
}

} // namespace okay::optimize
