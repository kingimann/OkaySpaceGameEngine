#pragma once
// ---------------------------------------------------------------------------
// okay::fbx - our own FBX reader (binary 7.x and ASCII), written from the
// public format description. It is the side-by-side, NON-DEFAULT alternative to
// the vendored ufbx used by Render/ModelImport. Select it with the CMake option
// OKAY_OWN_FBX=ON (makes it the build default) or at run time with the
// environment variable OKAY_OWN_FBX=1 / okay::fbx::SetOwnReaderEnabled(true).
//
// Two layers:
//   1. Parse()  - bytes -> a plain element tree (Document). No scene knowledge.
//   2. Build()  - Document -> Scene: nodes with transforms, triangulated
//                 meshes split per material, skins, blend shapes, materials,
//                 textures and baked animation. Already converted to
//                 right-handed Y-up, meters (like ModelImport asks ufbx for).
//
// Pure standard C++ (no engine headers, no <mutex>/<thread>), so it also
// builds standalone for the hostile-input fuzz tests. What is NOT handled yet
// is listed in docs/own_fbx_reader.md.
// ---------------------------------------------------------------------------
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace okay {
namespace fbx {

/// Hard limits that keep hostile files cheap. Parsing fails (never crashes)
/// when a file goes past one of them.
struct Limits {
    std::size_t maxDepth        = 96;                 ///< element nesting
    std::size_t maxElements     = 4u * 1024 * 1024;   ///< elements in the whole tree
    std::size_t maxArrayBytes   = 512u * 1024 * 1024; ///< decoded array bytes, whole file
    std::size_t maxStringBytes  = 256u * 1024 * 1024; ///< one string / raw blob
    std::size_t maxBakedSamples = 20000;              ///< animation samples per clip
    std::size_t maxCorners      = 8u * 1024 * 1024;   ///< triangle corners built, whole scene
    std::size_t maxNodes        = 1u * 1024 * 1024;   ///< scene nodes (models)
    std::size_t maxNodeDepth    = 2048;               ///< scene hierarchy depth
};

// ---- Layer 1: the element tree -------------------------------------------

struct Prop {
    char type = 0;                 ///< binary type letter: Y C I F D L S R, or 'a' = numeric array
    std::int64_t i = 0;            ///< Y C I L
    double d = 0;                  ///< F D
    std::string s;                 ///< S R (raw bytes for R)
    std::vector<double> arr;       ///< every numeric array (f d l i b), as doubles
    bool IsNumber() const { return type == 'Y' || type == 'C' || type == 'I' || type == 'L' || type == 'F' || type == 'D'; }
    double Num() const { return (type == 'F' || type == 'D') ? d : (double)i; }
};

struct Elem {
    std::string name;
    std::vector<Prop> props;
    std::vector<Elem> children;
    const Elem* Find(const char* n) const;
};

struct Document {
    std::uint32_t version = 0;     ///< 7400, 7500, ... (0 for ASCII files without a header)
    bool binary = false;
    Elem root;                     ///< unnamed; its children are the top-level sections
    std::size_t elementCount = 0;
};

bool ParseBinary(const std::uint8_t* data, std::size_t size, Document& out, std::string* err, const Limits& lim = Limits());
bool ParseAscii(const std::uint8_t* data, std::size_t size, Document& out, std::string* err, const Limits& lim = Limits());
/// Picks binary or ASCII from the "Kaydara FBX Binary" magic.
bool Parse(const std::uint8_t* data, std::size_t size, Document& out, std::string* err, const Limits& lim = Limits());

/// zlib stream (RFC 1950/1951) decoder, our own. Fails if the output would pass `maxOut`.
bool Inflate(const std::uint8_t* in, std::size_t n, std::vector<std::uint8_t>& out, std::size_t maxOut);

// ---- Layer 2: the scene ---------------------------------------------------

/// 4x4 matrix, row-major, column-vector convention: p' = M p, translation in m[3], m[7], m[11].
struct Mat { double m[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}; };

struct Texture {
    std::string name, fileName, relativeFileName;
    std::vector<std::uint8_t> content;      ///< embedded bytes (Video "Content"), may be empty
};

struct Material {
    std::string name;
    bool hasDiffuse = false;   double diffuse[3] = {0.8, 0.8, 0.8};
    bool hasEmissive = false;  double emissive[3] = {0, 0, 0};
    bool hasEmissiveFactor = false; double emissiveFactor = 1.0;
    bool hasRoughness = false; double roughness = 0.5;
    bool hasMetalness = false; double metalness = 0.0;
    int diffuseTexture = -1, normalTexture = -1;   ///< index into Scene::textures
};

/// One corner of one triangle, in the mesh's geometry space (axes/unit converted).
struct Corner {
    float p[3] = {0, 0, 0};
    float n[3] = {0, 0, 0};
    float uv[2] = {0, 0};          ///< as stored in the file (v is bottom-up)
    float color[4] = {1, 1, 1, 1};
    std::uint32_t cp = 0;          ///< control point (vertex) index, for skin / blend shapes
};

struct Part {
    int slot = 0;                  ///< material slot (index into Mesh::materials)
    std::vector<Corner> corners;   ///< 3 per triangle
};

struct Influence { std::uint32_t cluster; float weight; };

struct Cluster {
    int boneNode = -1;             ///< index into Scene::nodes, -1 if the bone model is missing
    Mat geometryToBone;            ///< inverse bind: geometry space -> bone space
};

struct BlendChannel {
    std::string name;
    double weight = 0;             ///< 0..1 (DeformPercent / 100)
    std::vector<std::uint32_t> indices;   ///< control points
    std::vector<float> dp;         ///< 3 per entry
    std::vector<float> dn;         ///< 3 per entry, empty when the shape has no normals
};

struct Mesh {
    std::string name;
    std::uint32_t controlPoints = 0;
    bool hasNormals = false, hasUV = false, hasColors = false;
    std::vector<Part> parts;       ///< non-empty parts, ascending slot
    std::vector<int> materials;    ///< slot -> index into Scene::materials (-1 unknown)
    std::vector<Cluster> clusters; ///< empty when not skinned
    /// per control point: influences, strongest first
    std::vector<std::vector<Influence>> skinWeights;
    std::vector<BlendChannel> channels;
    std::uint32_t triangleCount() const { std::uint32_t t = 0; for (const Part& p : parts) t += (std::uint32_t)(p.corners.size() / 3); return t; }
};

struct SceneNode {
    std::string name;
    int parent = -1;               ///< -1 = child of the scene root
    std::vector<int> children;
    double translation[3] = {0, 0, 0};
    double rotation[4] = {0, 0, 0, 1};   ///< x y z w
    double scale[3] = {1, 1, 1};
    Mat local, world, geometryToNode, geometryToWorld;
    int mesh = -1;                 ///< index into Scene::meshes
};

struct Key3 { double time; double v[3]; };
struct Key4 { double time; double v[4]; };

struct NodeAnim {
    int node = -1;
    std::vector<Key3> translation, scale;
    std::vector<Key4> rotation;
};
struct BlendAnim {
    int mesh = -1, channel = -1;
    std::vector<Key3> percent;     ///< v[0] = DeformPercent (0..100)
};
struct Clip {
    std::string name;
    double duration = 0;
    std::vector<NodeAnim> nodes;
    std::vector<BlendAnim> blends;
};

struct Settings {
    int upAxis = 1, upSign = 1, frontAxis = 2, frontSign = 1, coordAxis = 0, coordSign = 1;
    double unitScaleFactor = 1.0;  ///< FBX UnitScaleFactor (1 = centimeter)
    double appliedScale = 0.01;    ///< what vertices/translations were multiplied by (unit -> meters)
};

struct Scene {
    std::vector<SceneNode> nodes;      ///< parents before children (depth-first)
    std::vector<int> roots;
    std::vector<Mesh> meshes;
    std::vector<Material> materials;
    std::vector<Texture> textures;
    std::vector<Clip> clips;
    Settings settings;
    std::string creator;
};

struct BuildOptions {
    bool convertToYUpMeters = true;    ///< right-handed Y-up, 1 unit = 1 m (what ModelImport asks ufbx for)
    double bakeRate = 30.0;
    Limits limits;
};

bool Build(const Document& doc, Scene& out, std::string* err, const BuildOptions& opt = BuildOptions());
bool LoadMemory(const std::uint8_t* data, std::size_t size, Scene& out, std::string* err, const BuildOptions& opt = BuildOptions());
bool LoadFile(const std::string& path, Scene& out, std::string* err, const BuildOptions& opt = BuildOptions());

/// Whether ModelImport should use this reader. Default: the OKAY_OWN_FBX CMake option, overridden
/// by the OKAY_OWN_FBX environment variable ("1"/"0") and by SetOwnReaderEnabled().
bool OwnReaderEnabled();
void SetOwnReaderEnabled(bool on);

} // namespace fbx
} // namespace okay
