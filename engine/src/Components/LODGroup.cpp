#include "okay/Components/LODGroup.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Optimize/MeshSimplify.hpp"
#include "okay/Render/Visibility.hpp"
#include "okay/Modeling/MeshAsset.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <unordered_map>

namespace okay {

void LODGroup::Generate(const Mesh& src) {
    optimize::LodChain chain = optimize::GenerateLodChain(src, {1.0f, 0.5f, 0.25f, 0.1f});
    levels.clear();
    lodMeshes.clear();
    const int n = std::max(1, (int)chain.levels.size());
    levels.resize((std::size_t)std::min(n, kMaxLevels));
    for (int i = 0; i < (int)levels.size(); ++i) {
        const bool have = i < (int)chain.levels.size();
        const float nextDist = (have && i + 1 < (int)chain.levels.size() && chain.levels[i + 1].switchDistance > 1.0f)
                                    ? chain.levels[i + 1].switchDistance
                                    : (8.0f * (float)(i + 1) * (float)(i + 1));
        levels[(std::size_t)i].distance = nextDist;
        levels[(std::size_t)i].screenSize = (have && i + 1 < (int)chain.levels.size())
                                                 ? std::clamp(chain.levels[i + 1].switchScreenHeight, 0.02f, 0.95f)
                                                 : std::clamp(0.5f / (float)(i + 1), 0.02f, 0.95f);
        levels[(std::size_t)i].ratio = have ? chain.levels[(std::size_t)i].ratio : 1.0f;
        if (i > 0 && have) lodMeshes.push_back(chain.levels[(std::size_t)i].mesh);
    }
    // Distances must grow, or SelectLod never walks down the chain.
    for (int i = 1; i < (int)levels.size(); ++i)
        if (!(levels[(std::size_t)i].distance > levels[(std::size_t)i - 1].distance + 0.05f))
            levels[(std::size_t)i].distance = levels[(std::size_t)i - 1].distance + 5.0f;
    for (int i = 1; i < (int)levels.size(); ++i)
        if (!(levels[(std::size_t)i].screenSize < levels[(std::size_t)i - 1].screenSize - 0.005f))
            levels[(std::size_t)i].screenSize = std::max(0.01f, levels[(std::size_t)i - 1].screenSize * 0.5f);
    cullDistance = levels.back().distance * 1.5f;
    cullScreen = std::max(0.0f, levels.back().screenSize * 0.45f);
    useScreenSize = false;
    forceLevel = -1;
    lodState = -1;
}

void LODGroup::GenerateScreenSize(const Mesh& src, int levelCount, float reduction, const std::vector<float>& screen) {
    const int n = std::clamp(levelCount, 1, kMaxLevels);
    if (!(reduction > 0.01f && reduction < 1.0f)) reduction = 0.5f;
    levels.assign((std::size_t)n, Level{});
    float ratio = 1.0f, prev = 1.0f;
    for (int i = 0; i < n; ++i) {
        Level& L = levels[(std::size_t)i];
        L.ratio = ratio;
        float t = i < (int)screen.size() ? screen[(std::size_t)i] : (i == 0 ? 0.5f : prev * 0.5f);
        if (!(t > 0.001f && t < prev)) t = (i == 0 ? std::min(0.5f, 0.999f) : prev * 0.5f);   // must fall level by level
        L.screenSize = t;
        L.distance = 10.0f * (float)(i + 1) * (float)(i + 1);   // used only if someone switches to distance mode
        prev = t;
        ratio *= reduction;
    }
    useScreenSize = true;
    // Cull once the object is half as tall as where the last level would hand over (0 = never for one level).
    cullScreen = n >= 2 ? levels[(std::size_t)(n - 2)].screenSize * 0.25f : 0.0f;
    cullDistance = levels.back().distance * 1.5f;
    forceLevel = -1;
    lodState = -1;
    fadeFrom = shownLevel = kNoFade;
    RebuildFromRatios(src);
}

namespace {
// Many objects share one model (a scattered field of rocks): simplify each (mesh, ratio) once and copy the result.
// Main thread only, like the components. Bounded: cleared when it grows past 256 entries.
struct LodCacheKey { std::uint64_t geo; std::uint32_t ratio; bool operator==(const LodCacheKey& o) const { return geo == o.geo && ratio == o.ratio; } };
struct LodCacheHash { std::size_t operator()(const LodCacheKey& k) const { return (std::size_t)(k.geo ^ ((std::uint64_t)k.ratio * 0x9E3779B97F4A7C15ull)); } };
std::unordered_map<LodCacheKey, Mesh, LodCacheHash>& LodCache() { static std::unordered_map<LodCacheKey, Mesh, LodCacheHash> c; return c; }
} // namespace

void LODGroup::RebuildFromRatios(const Mesh& src) {
    lodMeshes.clear();
    const std::uint64_t geo = modeling::GeometryHash(src) ^ (std::uint64_t)src.normals.size() * 1315423911ull;
    for (int i = 1; i < (int)levels.size(); ++i) {
        optimize::SimplifyOptions opt;
        float r = levels[(std::size_t)i].ratio;
        if (!(r > 0.0f && r <= 1.0f)) r = 0.5f;
        opt.targetRatio = r;
        std::uint32_t rb = 0; std::memcpy(&rb, &r, sizeof rb);
        auto& cache = LodCache();
        auto it = cache.find({geo, rb});
        if (it == cache.end()) {
            if (cache.size() > 256) cache.clear();
            it = cache.emplace(LodCacheKey{geo, rb}, optimize::SimplifyMesh(src, opt).mesh).first;
        }
        lodMeshes.push_back(it->second);
    }
}

const Mesh* LODGroup::MeshForLevel(int level, const MeshRenderer* mr) const {
    if (level < 0 || !mr) return nullptr;
    if (level == 0) return &mr->DisplayMesh();
    const int i = level - 1;
    if (i >= 0 && i < (int)lodMeshes.size() && !lodMeshes[(std::size_t)i].vertices.empty())
        return &lodMeshes[(std::size_t)i];
    return &mr->DisplayMesh();
}

void LODGroup::DebugTint(int level, float& r, float& g, float& b) const {
    r = g = b = 1.0f;
    if (!(debugTint || visibility::LodOverlay()) || level < 0) return;
    // Red, green, blue, yellow, magenta, cyan: one colour per level (the Scene view's LOD overlay uses the same).
    static const float k[6][3] = {{1, 0.35f, 0.35f}, {0.4f, 1, 0.45f}, {0.4f, 0.6f, 1}, {1, 0.85f, 0.3f}, {1, 0.4f, 1}, {0.35f, 1, 1}};
    const int i = level > 5 ? 5 : level;
    r = k[i][0]; g = k[i][1]; b = k[i][2];
}

void LODGroup::Awake() {
    if (lodMeshes.size() + 1 >= levels.size() && !lodMeshes.empty()) return;
    if (levels.size() < 2) return;
    auto* mr = gameObject ? gameObject->GetComponent<MeshRenderer>() : nullptr;
    if (mr && !mr->DisplayMesh().vertices.empty()) RebuildFromRatios(mr->DisplayMesh());
}

} // namespace okay
