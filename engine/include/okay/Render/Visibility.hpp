#pragma once
// What the renderers draw this frame (feature map 2.8 / 27): frustum culling,
// occlusion against Occluder components, LOD selection, and static/dynamic
// batching. Build() fills LastCullCounters. A scene with no LOD Group, no
// Occluder and nothing safe to merge produces one draw per mesh, the same list
// the renderers walked before.
#include "okay/Math/Mat4.hpp"
#include "okay/Render/Culling.hpp"
#include "okay/Scene/Scene.hpp"
#include <vector>

namespace okay {

class Mesh;

namespace visibility {

struct Item {
    GameObject* go = nullptr;
    MeshRenderer* mr = nullptr;
    const Mesh* mesh = nullptr;
    Mat4 model;
    int lod = -1;           ///< -1 when the object has no LOD Group
    bool batched = false;   ///< mesh is already in world space; model is identity
};

struct Frame {
    std::vector<Item> items;          ///< what a GPU renderer submits (merged where safe)
    int frustumCulled = 0;
    int occluded = 0;
    int lodCulled = 0;
    int lodCount[4] = {0, 0, 0, 0};
    int batches = 0;
    int drawsSaved = 0;
    int individuals = 0;              ///< visible objects before merging
    bool ready = false;

    /// Mesh for a shadow pass. Null when this camera's LOD culled the object.
    /// Camera-frustum and occlusion do not apply: a hidden caster can still shade.
    const Mesh* ShadowMesh(const GameObject* go, const MeshRenderer* mr) const;
    /// False when this camera should not draw the object (mask, frustum, occlusion, LOD).
    bool Visible(const GameObject* go) const;
    /// LOD (or display) mesh for a visible object. Null when not visible.
    const Mesh* MeshFor(const GameObject* go) const;
};

/// Vertical field of view (degrees) the host was rendering with. Renderers pass
/// it into Build. 60 matches a default camera until a host sets it.
inline float& RenderViewFovY() { static float v = 60.0f; return v; }

inline bool& OcclusionCullingEnabled() { static bool on = true; return on; }
inline bool& StaticBatchingEnabled() { static bool on = true; return on; }
inline bool& DynamicBatchingEnabled() { static bool on = true; return on; }

const Frame& Build(const Scene& scene, const Mat4& vp, const Vec3& eye, int viewW, int viewH,
                   float fovY, const GameObject* ignore);
const Frame& Current();

} // namespace visibility
} // namespace okay
