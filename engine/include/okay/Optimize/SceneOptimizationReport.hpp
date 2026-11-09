#pragma once
// ---------------------------------------------------------------------------
// Scene optimization report (feature map 27): static analysis of a scene's
// rendering cost, from a flat list of plain structs so it has no Scene
// dependency. An editor panel or a build step fills OptScene (one OptObject
// per renderer) and gets back findings with a severity, a plain explanation
// and a suggestion, as text or JSON.
//
// Checks:
//   TinyDrawCalls          many small meshes, each its own draw call
//   HugeTextureSmallObject big textures on objects that cannot show the detail
//   HighPolyNoLod          dense meshes without a LOD chain
//   DuplicateMaterials     materials that are identical but separate (they break batching)
//   OverdrawRisk           stacks of overlapping transparent objects
//   TooManyDrawCalls       more objects than the draw call budget
//   TextureMemory          more texture memory than the budget
// ---------------------------------------------------------------------------
#include "okay/Math/Bounds.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace okay::optimize {

struct OptTexture {
    std::string name;
    int   width = 0, height = 0;
    float bytesPerPixel = 4.0f;
    bool  mipmaps = true;
    float Bytes() const {
        return static_cast<float>(width) * static_cast<float>(height) * bytesPerPixel * (mipmaps ? 1.3333f : 1.0f);
    }
};

struct OptMaterial {
    int id = 0;
    std::string name;
    std::string shader;
    std::string signature;      ///< anything that makes two materials differ: a hash of properties and texture names
};

struct OptObject {
    std::string name;
    int   triangles = 0;
    int   materialId = -1;      ///< index into OptScene::materials by OptMaterial::id (-1 = none)
    int   meshId = -1;          ///< objects sharing a mesh can be instanced
    Bounds bounds;              ///< world-space
    bool  isStatic = false;
    bool  transparent = false;
    bool  skinned = false;
    bool  hasLod = false;
    float screenPixels = -1.0f; ///< largest on-screen size (pixels) when known, else -1
    std::vector<OptTexture> textures;
};

struct OptScene {
    std::vector<OptObject> objects;
    std::vector<OptMaterial> materials;     ///< optional; without it duplicate materials cannot be found
};

struct OptSettings {
    int   tinyTriangleLimit = 100;          ///< a mesh with at most this many triangles is "tiny"
    int   tinyWarnCount = 100, tinyCriticalCount = 500;
    int   highPolyTriangles = 20000;        ///< without a LOD chain this is flagged
    int   highPolyCritical = 100000;
    int   hugeTextureSize = 1024;           ///< longest side that counts as huge
    float recommendedTexelsPerUnit = 256.0f;///< used when no screen size is known
    float hugeTextureRatio = 4.0f;          ///< texture this many times bigger than useful is flagged
    float wasteWarnMB = 8.0f, wasteCriticalMB = 64.0f;
    int   overdrawLayers = 6;               ///< transparent objects overlapping this many others
    int   overdrawCritical = 16;
    int   drawCallBudget = 2000;
    float textureBudgetMB = 1024.0f;
    int   maxListed = 8;                    ///< how many object names a finding lists
};

enum class Severity { Info = 0, Warning = 1, Critical = 2 };

enum class FindingKind {
    TinyDrawCalls,
    HugeTextureSmallObject,
    HighPolyNoLod,
    DuplicateMaterials,
    OverdrawRisk,
    TooManyDrawCalls,
    TextureMemory,
};

struct Finding {
    FindingKind kind = FindingKind::TinyDrawCalls;
    Severity severity = Severity::Info;
    std::string title;
    std::string detail;
    std::string suggestion;
    std::vector<int> objects;       ///< indices into OptScene::objects (all affected ones)
    std::vector<std::string> listed;///< names of the first few, for display
    float impact = 0.0f;            ///< used for ordering inside a severity (count, megabytes ...)
};

struct SceneStats {
    int objects = 0, staticObjects = 0, transparentObjects = 0;
    long long triangles = 0;
    int materials = 0;
    int uniqueTextures = 0;
    float textureMB = 0.0f;
    int estimatedDrawCalls = 0;     ///< one per object (no batching assumed)
};

struct OptimizationReport {
    SceneStats stats;
    std::vector<Finding> findings;  ///< worst first
    int Count(Severity s) const;
    bool Has(FindingKind k) const { return Get(k) != nullptr; }
    const Finding* Get(FindingKind k) const;
    std::string ToText() const;
    std::string ToJson() const;
};

OptimizationReport AnalyzeScene(const OptScene& scene, const OptSettings& settings = OptSettings());

const char* SeverityName(Severity s);
const char* FindingKindName(FindingKind k);

} // namespace okay::optimize
