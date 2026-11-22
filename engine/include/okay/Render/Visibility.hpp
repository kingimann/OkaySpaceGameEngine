#pragma once
// What the renderers draw this frame (feature map 2.8 / 27): frustum culling,
// occlusion against Occluder components, LOD selection, and static/dynamic
// batching. Build() fills LastCullCounters. A scene with no LOD Group, no
// Occluder and nothing safe to merge produces one draw per mesh, the same list
// the renderers walked before.
#include "okay/Math/Mat4.hpp"
#include "okay/Render/Culling.hpp"
#include "okay/Scene/Scene.hpp"
#include <cstdlib>
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
    /// LOD cross-fade (LodDither.hpp): draw only the pixels LodDitherKeep(x, y, ditherFade, ditherInvert) allows.
    /// 1 and not inverted = draw everything (no fade). A fading object has two items: the incoming level
    /// (not inverted) and the outgoing one (`fadeSecondary`, inverted, same ditherFade).
    float ditherFade = 1.0f;
    bool ditherInvert = false;
    bool fadeSecondary = false;
    bool Dithered() const { return ditherInvert || ditherFade < 1.0f; }
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
    int lodFading = 0;                ///< objects cross-fading between LOD levels this frame
    std::size_t lodTriangles = 0;     ///< triangles in the items (both levels of a fading object counted)
    bool ready = false;
    Vec3 eye;                         ///< the camera this frame was built for (asset streaming plans from it)
    float fovY = 60.0f;
    int viewH = 0;

    /// Mesh for a shadow pass. Null when this camera's LOD culled the object.
    /// Camera-frustum and occlusion do not apply: a hidden caster can still shade.
    const Mesh* ShadowMesh(const GameObject* go, const MeshRenderer* mr) const;
    /// False when this camera should not draw the object (mask, frustum, occlusion, LOD).
    bool Visible(const GameObject* go) const;
    /// LOD (or display) mesh for a visible object. Null when not visible.
    const Mesh* MeshFor(const GameObject* go) const;
    /// Cross-fade of a visible object, for a renderer that walks objects (the software one): `fade` is the
    /// dither value for MeshFor's mesh (1 = no fade), `mainLod` its level. Returns the outgoing level's mesh
    /// (drawn inverted, level `fadeLod`), or null when only MeshFor's mesh is drawn.
    const Mesh* FadeFor(const GameObject* go, float& fade, int& mainLod, int& fadeLod) const;
};

/// Vertical field of view (degrees) the host was rendering with. Renderers pass
/// it into Build. 60 matches a default camera until a host sets it.
inline float& RenderViewFovY() { static float v = 60.0f; return v; }

/// LOD bias for every LOD Group: > 1 keeps detailed levels longer (distances are divided by it, screen sizes
/// multiplied), < 1 switches to cheaper levels sooner. Set by the quality presets (DisplaySettings) and by
/// OKAY_LOD_BIAS (the starting value, until a quality preset is applied); 1 = as authored.
inline float& LodBias() {
    static float b = [] {
        const char* e = std::getenv("OKAY_LOD_BIAS");
        const float v = e ? (float)std::atof(e) : 1.0f;
        return (v > 0.01f && v < 100.0f) ? v : 1.0f;
    }();
    return b;
}
/// Scene view "LOD colours" overlay: every LOD Group tinted by its level (red 0, green 1, blue 2, yellow 3...).
inline bool& LodOverlay() { static bool on = false; return on; }
/// Seconds the LOD cross-fade runs on. Negative (the default) = the real clock; tests set a value.
inline double& LodClockOverride() { static double t = -1.0; return t; }
double LodClock();

inline bool& OcclusionCullingEnabled() { static bool on = true; return on; }
inline bool& StaticBatchingEnabled() { static bool on = true; return on; }
inline bool& DynamicBatchingEnabled() { static bool on = true; return on; }

const Frame& Build(const Scene& scene, const Mat4& vp, const Vec3& eye, int viewW, int viewH,
                   float fovY, const GameObject* ignore);
const Frame& Current();

} // namespace visibility
} // namespace okay
