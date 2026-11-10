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
    /// Tint the object by its level in the view (debug). Off by default.
    bool debugTint = false;

    std::vector<Level> levels;          ///< best first; level 0 is the MeshRenderer
    std::vector<Mesh> lodMeshes;        ///< lodMeshes[i] is the mesh for level i+1

    /// Last choice, fed back into SelectLod so a boundary does not flicker.
    int lodState = -1;
    int activeLevel = 0;

    /// Build four levels (100/50/25/10 percent) from `src` and distances from the chain.
    void Generate(const Mesh& src);
    /// Fill lodMeshes from each level's ratio. Level 0 is not stored.
    void RebuildFromRatios(const Mesh& src);

    /// The mesh a camera should draw for `level`, or null when culled.
    const Mesh* MeshForLevel(int level, const MeshRenderer* mr) const;

    /// RGB tint for the debug view. White when debug is off.
    void DebugTint(int level, float& r, float& g, float& b) const;

    void Awake() override;
};

} // namespace okay
