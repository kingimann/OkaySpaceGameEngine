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
#include <cstring>
#include <string>
#include <string_view>
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
    // Animation. A small file can claim many stacks/curves/keys, or point many stacks at the same curves,
    // so besides the per-clip cap above there is a budget for the whole file.
    std::size_t maxAnimStacks   = 128;                ///< AnimationStack objects (takes)
    std::size_t maxAnimLayers   = 1024;               ///< AnimationLayer objects
    std::size_t maxAnimCurves   = 128u * 1024;        ///< AnimationCurve objects, and AnimationCurveNode objects, each
    std::size_t maxAnimKeys     = 4u * 1024 * 1024;   ///< keys over all curves
    std::size_t maxTotalBakedSamples = 4u * 1024 * 1024;   ///< baked evaluations over all clips: samples x animated curve nodes
};

// ---- Layer 1: the element tree -------------------------------------------
//
// The tree does not copy the file: names, strings, raw blobs and uncompressed binary arrays are views into the
// bytes given to Parse(), so those bytes must stay alive (and unchanged) as long as the Document is used.
// LoadMemory / LoadFile keep them alive while they build the Scene. Only zlib arrays (inflated) and ASCII arrays
// (parsed numbers) are stored in the Document itself. Every element and property sits in two flat arrays of the
// Document (one allocation each, grown by doubling), not in a vector per element.

/// A numeric array as the file stores it: little-endian elements of one type, read on demand (no conversion to
/// double up front, so a float array stays 4 bytes per number).
struct Array {
    char type = 0;                 ///< 'd' f64, 'f' f32, 'l' i64, 'i' i32, 'b' bool (byte != 0), 'c' u8
    std::uint32_t count = 0;
    const std::uint8_t* data = nullptr;   ///< count elements, unaligned
    std::size_t size() const { return count; }
    bool empty() const { return count == 0; }
    /// Element k as a double (exactly what the reader used to store: (double)value, bool as 0 / 1).
    double operator[](std::size_t k) const {
        const std::uint8_t* q = data;
        switch (type) {
            case 'd': { double v; std::memcpy(&v, q + k * 8, 8); return v; }
            case 'f': { float v; std::memcpy(&v, q + k * 4, 4); return (double)v; }
            case 'i': { std::int32_t v; std::memcpy(&v, q + k * 4, 4); return (double)v; }
            case 'l': { std::int64_t v; std::memcpy(&v, q + k * 8, 8); return (double)v; }
            case 'b': return q[k] != 0 ? 1.0 : 0.0;
            default:  return (double)q[k];   // 'c'
        }
    }
};

struct Prop {
    char type = 0;                 ///< binary type letter: Y C I F D L S R, or 'a' = numeric array
    union {
        std::int64_t i = 0;        ///< Y C I L
        double d;                  ///< F D (shares the bytes of `i`: read the one the type says, or Num())
    };
    std::string_view s;            ///< S R (raw bytes for R); a view into the parsed bytes
    Array arr;                     ///< numeric arrays (f d l i b c)
    bool IsNumber() const { return type == 'Y' || type == 'C' || type == 'I' || type == 'L' || type == 'F' || type == 'D'; }
    double Num() const { return (type == 'F' || type == 'D') ? d : (double)i; }
};

struct Elem;
/// The properties of one element (a range of Document::props).
struct PropList {
    const Prop* p = nullptr; std::uint32_t n = 0;
    std::size_t size() const { return n; }
    bool empty() const { return n == 0; }
    const Prop& operator[](std::size_t k) const { return p[k]; }
    const Prop* begin() const { return p; }
    const Prop* end() const { return p + n; }
};
/// The children of one element, linked first child -> next sibling.
struct ElemList {
    const Elem* first = nullptr;
    struct It {
        const Elem* e;
        const Elem& operator*() const { return *e; }
        const Elem* operator->() const { return e; }
        It& operator++();
        bool operator==(const It& o) const { return e == o.e; }
        bool operator!=(const It& o) const { return e != o.e; }
    };
    It begin() const { return {first}; }
    It end() const { return {nullptr}; }
    bool empty() const { return first == nullptr; }
};

struct Elem {
    std::string_view name;
    PropList props;
    ElemList children;
    const Elem* next = nullptr;    ///< next sibling
    // while parsing: indices into Document::elems / props (turned into the pointers above at the end)
    std::uint32_t propBegin_ = 0, firstChild_ = 0xFFFFFFFFu, nextSibling_ = 0xFFFFFFFFu;
    const Elem* Find(const char* n) const;
};
inline ElemList::It& ElemList::It::operator++() { e = e->next; return *this; }

struct Document {
    std::uint32_t version = 0;     ///< 7400, 7500, ... (0 for ASCII files without a header)
    bool binary = false;
    Elem root;                     ///< unnamed; its children are the top-level sections
    std::size_t elementCount = 0;
    // storage (the pointers in the tree point into these)
    std::vector<Elem> elems;
    std::vector<Prop> props;
    std::vector<std::vector<std::uint8_t>> inflated;   ///< decoded zlib arrays
    std::vector<std::vector<double>> numbers;          ///< ASCII arrays
    Document() = default;
    Document(Document&&) = default;
    Document& operator=(Document&&) = default;
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;
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
    int aoTexture = -1;                            ///< ambient occlusion map (PBR property sets only)
    /// Which property set the values came from: "" / "lambert" / "phong" for the classic FBX material, or the PBR
    /// set a modelling tool wrote ("3dsmax-physical", "3dsmax-pbr-metal-rough", "3dsmax-pbr-spec-gloss",
    /// "gltf", "openpbr"). The set is recognised the way ufbx does it (the tool's class id in the material).
    std::string shaderSet;
};

/// One corner of one triangle, in the mesh's geometry space (axes/unit converted).
struct Corner {
    float p[3] = {0, 0, 0};
    float n[3] = {0, 0, 0};
    float uv[2] = {0, 0};          ///< as stored in the file (v is bottom-up)
    float color[4] = {1, 1, 1, 1};
    std::uint32_t cp = 0;          ///< control point (vertex) index, for skin / blend shapes
};

static_assert(sizeof(Corner) == 52, "Corner is compared and hashed byte for byte: no padding");

/// The triangles of one material slot, indexed: corner i of the triangle list is vertices[indices[i]]. Corners that
/// are equal byte for byte share one vertex, numbered in order of first use (so the importer's own de-duplication
/// gives the same vertex order as it did on the unindexed list).
struct Part {
    int slot = 0;                  ///< material slot (index into Mesh::materials)
    std::vector<Corner> vertices;  ///< distinct corners, in order of first use
    std::vector<std::uint32_t> indices;   ///< 3 per triangle, into `vertices`
    std::size_t cornerCount() const { return indices.size(); }
    const Corner& corner(std::size_t i) const { return vertices[indices[i]]; }
    /// The unindexed list (3 corners per triangle), for tests and tools.
    std::vector<Corner> ExpandCorners() const {
        std::vector<Corner> out; out.reserve(indices.size());
        for (std::uint32_t i : indices) out.push_back(vertices[i]);
        return out;
    }
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
    std::uint32_t triangleCount() const { std::uint32_t t = 0; for (const Part& p : parts) t += (std::uint32_t)(p.indices.size() / 3); return t; }
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
