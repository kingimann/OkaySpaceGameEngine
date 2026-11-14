#include "okay/Components/LODGroup.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Optimize/MeshSimplify.hpp"
#include <algorithm>
#include <cmath>

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

void LODGroup::RebuildFromRatios(const Mesh& src) {
    lodMeshes.clear();
    for (int i = 1; i < (int)levels.size(); ++i) {
        optimize::SimplifyOptions opt;
        float r = levels[(std::size_t)i].ratio;
        if (!(r > 0.0f && r <= 1.0f)) r = 0.5f;
        opt.targetRatio = r;
        lodMeshes.push_back(optimize::SimplifyMesh(src, opt).mesh);
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
    if (!debugTint || level < 0) return;
    static const float k[4][3] = {{1, 0.35f, 0.35f}, {0.4f, 1, 0.45f}, {0.4f, 0.6f, 1}, {1, 0.85f, 0.3f}};
    const int i = level < 0 ? 0 : (level > 3 ? 3 : level);
    r = k[i][0]; g = k[i][1]; b = k[i][2];
}

void LODGroup::Awake() {
    if (lodMeshes.size() + 1 >= levels.size() && !lodMeshes.empty()) return;
    if (levels.size() < 2) return;
    auto* mr = gameObject ? gameObject->GetComponent<MeshRenderer>() : nullptr;
    if (mr && !mr->DisplayMesh().vertices.empty()) RebuildFromRatios(mr->DisplayMesh());
}

} // namespace okay
