#pragma once
// LOD Group (feature map 2.8 / 27.1). Sits on the same object as a MeshRenderer.
// Level 0 is that renderer's mesh. Further levels are cheaper copies, chosen
// per camera with optimize::SelectLod (hysteresis included). Nothing here is
// drawn until a renderer asks MeshForLevel.
#include "okay/Optimize/LodGroup.hpp"
#include "okay/Render/Mesh.hpp"
#include "okay/Scene/Component.hpp"
#include <vector>

namespace okay {

class MeshRenderer;

class LODGroup : public Component {
public:
    static constexpr int kMaxLevels = 6;

    struct Level {
        float distance = 20.0f;     ///< leave this level past this many world units (distance mode)
        float screenSize = 0.5f;    ///< leave this level below this fraction of the view height
        float ratio = 1.0f;         ///< triangle fraction the generated mesh keeps (1 = the original)
    };

    /// False: switch on `distance`. True: switch on `screenSize`.
    bool useScreenSize = false;
    float hysteresis = 0.1f;
    /// Past this distance (or below cullScreen when useScreenSize) the object is not drawn.
    /// 0 = never cull.
    float cullDistance = 0.0f;
    float cullScreen = 0.0f;
    /// -1 = automatic. Otherwise force that level (levels.size() = culled).
    int forceLevel = -1;
    /// Tint the object by its level in the view (debug). Off by default. The Scene view's
    /// "LOD colours" overlay (visibility::LodOverlay) tints every LOD Group the same way.
    bool debugTint = false;

    /// How a level change looks. CrossFade (the default) dissolves the new level in over the old
    /// one with a screen-door dither for `fadeDuration` seconds, so nothing pops; None switches at once.
    enum FadeMode { FadeNone = 0, FadeCrossFade = 1 };
    int fadeMode = FadeCrossFade;
    float fadeDuration = 0.35f;

    std::vector<Level> levels;          ///< best first; level 0 is the MeshRenderer
    std::vector<Mesh> lodMeshes;        ///< lodMeshes[i] is the mesh for level i+1

    /// Last choice, fed back into SelectLod so a boundary does not flicker.
    int lodState = -1;
    int activeLevel = 0;

    /// Cross-fade state (runtime, not saved): the level being left (-1 = culled) and when the change began
    /// (visibility::LodClock seconds). fadeFrom == kNoFade when nothing is fading.
    static constexpr int kNoFade = -2;
    int fadeFrom = kNoFade;
    int shownLevel = kNoFade;   ///< the level shown last frame (-1 culled); kNoFade before the first frame
    double fadeStart = 0.0;

    /// Build four levels (100/50/25/10 percent) from `src` and distances from the chain.
    void Generate(const Mesh& src);
    /// Build `levelCount` levels (1..kMaxLevels), each keeping `reduction` of the triangles of the one before,
    /// switched by screen size: `screen[i]` is where level i is left (missing entries halve the previous one,
    /// starting at 0.5). Below a quarter of the last hand-over threshold the object is culled. What model import uses.
    void GenerateScreenSize(const Mesh& src, int levelCount, float reduction, const std::vector<float>& screen);
    /// Fill lodMeshes from each level's ratio. Level 0 is not stored.
    void RebuildFromRatios(const Mesh& src);

    /// The mesh a camera should draw for `level`, or null when culled.
    const Mesh* MeshForLevel(int level, const MeshRenderer* mr) const;

    /// RGB tint for the debug view. White when debug is off.
    void DebugTint(int level, float& r, float& g, float& b) const;

    void Awake() override;
};

} // namespace okay
