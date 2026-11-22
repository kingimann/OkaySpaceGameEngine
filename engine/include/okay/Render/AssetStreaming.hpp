#pragma once
// ---------------------------------------------------------------------------
// Asset streaming (feature map 2.13 / 27.4): textures stream their mips, model meshes load in the background.
// World, level and cell streaming (loading parts of a scene) is a different system and not here.
//
// Textures. With texture streaming on (textures::SetStreaming, OKAY_TEXTURE_STREAMING=1, or an Asset Streaming
// component in the scene), every picture starts at a small mip (TextureAsset.hpp). Update() then, once a frame:
//   1. measures how big each texture is on screen: for every mesh renderer that uses it, its bounding sphere's
//      diameter at its distance from the camera (tex::ProjectedPixels), times the material's tiling;
//   2. lets optimize::PlanMips pick the mip each texture should keep (one texel per pixel, hysteresis, and the memory
//      budget: when over it, the textures that lose the least detail per byte give up a mip first);
//   3. queues sharper mips as background loads on the engine's JobSystem, most visible first, at most
//      `uploadMBPerFrame` of new pixels per frame, and drops mips that are no longer needed (evicted at once).
//
// Meshes. With mesh background loading on (SetMeshAsyncMode, OKAY_MESH_ASYNC=1; the player turns it on for a
// scene with an Asset Streaming component), the scene loader does not stop for a model file: the object shows a
// placeholder cube, the file is read on the JobSystem, and Update() swaps the real mesh in when it is ready (and
// rebuilds the object's LOD Group levels from it). One load per file, shared by every object that uses it.
//
// Sounds are not handled here: an AudioSource plays a long clip as a stream already (AudioSource::kStreamSeconds).
// Main thread only. No <mutex>/<thread> (CLAUDE.md rule 21a): the work runs on JobSystem::Global().
// ---------------------------------------------------------------------------
#include "okay/Math/Vec3.hpp"
#include <cstddef>
#include <cstdint>
#include <string>

namespace okay {

class Scene;
class MeshRenderer;

namespace streaming {

struct Settings {
    float budgetMB = 0.0f;          ///< texture memory the planner keeps to; 0 = the texture library's budget
    float uploadMBPerFrame = 16.0f; ///< new mip pixels queued per frame (0 = no cap); a texture with only its start mip is never held back
    float mipBias = 0.0f;           ///< > 0 blurrier and cheaper, < 0 sharper
    int   minDimension = 4;         ///< never go below this longest side
};
Settings& Config();

/// Plan and apply texture mips for `scene` seen from `eye`, and apply finished mesh loads. Once a frame.
void Update(Scene& scene, const Vec3& eye, float fovYDeg, int viewHeightPx);
/// The same, from the camera of the last frame the renderers built (visibility::Current()). Does nothing before one.
void Update(Scene& scene);

// ---- meshes ------------------------------------------------------------------------------------------------------
/// When the scene loader reads model files in the background: 0 never (the default, and the editor, which may save
/// meshes back to their files), 1 when the scene has an Asset Streaming component with "Load meshes in the
/// background" on (the player sets this), 2 always. OKAY_MESH_ASYNC=1 makes the default 2.
void SetMeshAsyncMode(int mode);
int MeshAsyncMode();
bool MeshAsyncLoading();   ///< mode != 0
/// True when the scene text has an Asset Streaming component record asking for background meshes.
bool SceneTextAsksForAsyncMeshes(const std::string& text);
/// Load `path` (an .obj or .okaymesh, searched like the scene loader does, then in `sceneDir`) for `mr` in the
/// background. `mr` shows a placeholder cube until ApplyMeshLoads hands it the mesh.
void LoadMeshInBackground(MeshRenderer* mr, const std::string& path, const std::string& sceneDir);
/// Hand finished loads to the renderers of `scene` that wait for them. Returns how many renderers got a mesh.
int ApplyMeshLoads(Scene& scene);
/// Wait for every queued mesh load (tests, a loading screen); ApplyMeshLoads still hands them over.
void FinishMeshLoads();
/// True while `mr` shows a placeholder.
bool MeshPending(const MeshRenderer* mr);

// ---- numbers for the Stats panel and the F3 overlay ----------------------------------------------------------------
struct Stats {
    bool   texturesOn = false;
    int    textures = 0;             ///< streamable textures held
    int    texturesSharp = 0;        ///< at the mip the screen asks for
    std::size_t residentBytes = 0;   ///< CPU bytes of those textures now
    std::size_t fullBytes = 0;       ///< what they would take at full size
    std::size_t desiredBytes = 0;    ///< what the screen asks for (before the budget)
    std::size_t budgetBytes = 0;
    bool   overBudget = false;
    int    textureLoadsPending = 0;
    std::uint64_t textureLoadsDone = 0, textureEvictions = 0, textureLoadsQueued = 0;
    bool   meshesOn = false;
    int    meshLoadsPending = 0;     ///< files still being read
    int    meshesWaiting = 0;        ///< renderers showing a placeholder
    std::uint64_t meshesLoaded = 0, meshLoadsFailed = 0;
    int    plannedThisFrame = 0;     ///< textures the last Update planned
};
Stats GetStats();
/// One line: "streaming: 24 textures 12.3 MB / 256 MB (full 96.0 MB), 2 loading; meshes 1 loading".
std::string OverlayLine();

/// For tests: forget pending mesh loads and counters.
void ResetForTests();

} // namespace streaming
} // namespace okay
