#pragma once
// ---------------------------------------------------------------------------
// okay::fbx - our own FBX reader (FBX 6.0 to 7.x, binary and ASCII), written from the
// public format description. It is the only FBX reader: Render/ModelImport
// reads every .fbx with it (the vendored ufbx it replaced, and the OKAY_OWN_FBX
// switch between the two, were deleted on 2026-11-23).
//
// Two layers, and two import steps:
//   1. Parse()  - bytes -> a plain element tree (Document). No scene knowledge.
//   2. Build()  - Document -> Scene: nodes with transforms, meshes as the
//                 file's polygons (positions, polygon index lists and the
//                 per-corner layers), skins, blend shapes, materials, textures
//                 and the animation curves (the clips planned, not keyed).
//                 Already converted to right-handed Y-up, meters (what
//                 ModelImport needs).
//   3. The import steps: BuildParts() turns a mesh's polygons into triangles
//                 split per material, with equal corners shared (Mesh::parts);
//                 BuildClips() makes the clips' keys from the curves.
//
// Pure standard C++ (no engine headers, no <mutex>/<thread>), so it also
// builds standalone for the hostile-input fuzz tests. What is NOT handled yet
// is listed in docs/content/model_readers.md.
// ---------------------------------------------------------------------------
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
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
    std::size_t maxUVSets       = 8;                  ///< UV sets read per mesh (LayerElementUV); the ones after are skipped
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
    /// 'a': 1 + the index of the Document's own buffer that holds the array (Document::inflated for a binary file,
    /// Document::numbers for an ASCII one); 0 when the array is a view into the parsed bytes. (In the padding after `type`.)
    std::uint32_t own = 0;
    union {                        ///< read the member the type says (or Num(), s()): they share their bytes (24 bytes a property)
        std::int64_t i;            ///< Y C I L (set to 0 by the constructor below)
        double d;                  ///< F D
        Array arr;                 ///< 'a': numeric arrays (f d l i b c)
        std::string_view sv;       ///< S R: written by the readers; read it through s()
    };
    /// A user-provided constructor: GCC 11 deletes the implicit one for a union holding members with non-trivial
    /// default constructors (Array, string_view) even when another member has a default initializer (the release
    /// workflow's Linux game server job builds with GCC 11 on ubuntu-22.04).
    Prop() : i(0) {}
    /// S R (raw bytes for R): a view into the parsed bytes; empty for every other type.
    std::string_view s() const { return (type == 'S' || type == 'R') ? sv : std::string_view(); }
    bool IsNumber() const { return type == 'Y' || type == 'C' || type == 'I' || type == 'L' || type == 'F' || type == 'D'; }
    /// The number, or 0 for a string, raw or array property.
    double Num() const { return (type == 'F' || type == 'D') ? d : IsNumber() ? (double)i : 0.0; }
    /// Makes this an array property's storage (the union member `arr` becomes the one in use).
    void SetArray(char elemType, std::uint32_t count, const std::uint8_t* data) {
        Array a; a.type = elemType; a.count = count; a.data = data;
        new (&arr) Array(a);
    }
};

struct Elem;
/// The properties of one element (a range of Document::props).
struct PropList {
    const Prop* p = nullptr; std::uint32_t n = 0;
    std::uint32_t begin_ = 0;      ///< while parsing: index of the first property in Document::props (fills padding)
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
    union {
        const Elem* next = nullptr;    ///< next sibling
        /// while parsing: (first child + 1) << 32 | (next sibling + 1), indices into Document::elems, 0 = none; turned
        /// into `children.first` and `next` when the tree is complete (48 bytes an element instead of 64)
        std::uint64_t links_;
    };
    const Elem* Find(const char* n) const;
};
inline ElemList::It& ElemList::It::operator++() { e = e->next; return *this; }

/// The allocator of ByteBuffer: new elements are left unset on resize (default-initialised: for bytes, no zero fill).
template <class T> struct UninitAllocator : std::allocator<T> {
    template <class U> struct rebind { using other = UninitAllocator<U>; };
    UninitAllocator() noexcept = default;
    template <class U> UninitAllocator(const UninitAllocator<U>&) noexcept {}
    template <class U> void construct(U* p) noexcept { ::new ((void*)p) U; }
    template <class U, class... A> void construct(U* p, A&&... a) { ::new ((void*)p) U(static_cast<A&&>(a)...); }
};
/// Bytes that are written in full right after they are made room for (decoded zlib arrays): a resize does not zero
/// them first (zero filling a 16 MB array cost about a tenth of reading the file it came from).
using ByteBuffer = std::vector<std::uint8_t, UninitAllocator<std::uint8_t>>;
/// The same for the numbers of an ASCII array (each is written before it is read).
using NumberBuffer = std::vector<double, UninitAllocator<double>>;

struct Document {
    std::uint32_t version = 0;     ///< 7400, 7500, ... (0 for ASCII files without a header)
    bool binary = false;
    Elem root;                     ///< unnamed; its children are the top-level sections
    std::size_t elementCount = 0;
    // storage (the pointers in the tree point into these)
    std::vector<Elem> elems;
    std::vector<Prop> props;
    std::vector<ByteBuffer> inflated;                  ///< decoded zlib arrays
    std::vector<NumberBuffer> numbers;                 ///< ASCII arrays (doubles, or int32 / float32 values packed in)
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

/// Internal (the parsers and the FBX 6.x rewrite): turns the index links recorded in Elem::links_ and PropList::begin_
/// while a Document is built into its pointers. Run once, after the last element is added.
void LinkTree(Document& d);

/// What the readers say about a file of a version they do not read (`version` as 7400 is FBX 7.4): which version it is
/// and what to do (export it again as FBX 2011 or newer). FBX 6.0 to 7.x is read.
std::string UnsupportedVersionMessage(std::uint32_t version);

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

/// A numeric array the scene keeps after the Document is gone: `a` reads it, `keep` owns the bytes `a.data` points into
/// (shared, so a copied Scene stays valid). LoadMemory / LoadFile hand over the Document's own decoded buffers (no
/// copy); arrays that were views into the caller's bytes, and everything Build() gets from a const Document, are copied.
struct KeptArray {
    Array a;
    bool present = false;                  ///< the file had this array (it may still be empty)
    std::shared_ptr<const void> keep;
    std::size_t size() const { return a.count; }
    bool empty() const { return a.count == 0; }
    double operator[](std::size_t k) const { return a[k]; }
};

/// How a layer's values belong to polygon corners (the file's MappingInformationType).
enum class LayerMapping : std::uint8_t { None, ByPolygonVertex, ByControlPoint, ByPolygon, AllSame, ByEdge };

/// One layer element of a mesh (normals, UVs, colours or material ids) as the file stores it.
struct MeshLayer {
    LayerMapping mapping = LayerMapping::None;
    bool indexed = false;          ///< ReferenceInformationType IndexToDirect: `indices` picks the value per corner
    int dim = 1;                   ///< numbers per value (3 normals, 2 UVs, 4 colours, 1 material id)
    KeptArray values, indices;
    bool Ok() const { return mapping != LayerMapping::None && values.present; }
};

/// A mesh's polygons as the file stores them: what the reader produces. BuildParts() turns them into Mesh::parts
/// (triangulated, split per material, equal corners shared); the importer calls it.
struct Polygons {
    KeptArray positions;           ///< the file's Vertices, 3 per control point, in file units ...
    double positionScale = 1.0;    ///< ... times this gives output units (the unit conversion; axes are in the node matrices)
    KeptArray polygonVertexIndex;  ///< the file's PolygonVertexIndex: a control point per corner, a polygon's last as ~index
    KeptArray edges;               ///< the file's Edges, kept only when a layer is mapped ByEdge
    MeshLayer normals, uvs, colors, materials;
    /// The UV sets after the first (`uvs` is set 0), in the order of their LayerElementUV index; names in Mesh::uvSetNames.
    std::vector<MeshLayer> extraUVs;
    std::uint64_t corners = 0;     ///< triangle corners BuildParts makes (counted by the reader for Limits::maxCorners)
    bool empty() const { return !positions.present || !polygonVertexIndex.present; }
};

/// The triangles of one material slot, indexed: corner i of the triangle list is vertices[indices[i]]. Corners that
/// are equal byte for byte share one vertex, numbered in order of first use (so the importer's own de-duplication
/// gives the same vertex order as it did on the unindexed list).
struct Part {
    int slot = 0;                  ///< material slot (index into Mesh::materials)
    std::vector<Corner> vertices;  ///< distinct corners, in order of first use
    std::vector<std::uint32_t> indices;   ///< 3 per triangle, into `vertices`
    /// The mesh's UV sets after the first (Corner::uv is set 0): extraUVs[k] holds set k + 1, two floats (u, v as stored,
    /// v bottom-up) per entry of `vertices`. Empty when the mesh has one UV set or none. Two corners share a vertex only
    /// when these agree too.
    std::vector<std::vector<float>> extraUVs;
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
    /// The name of every UV set the mesh has (its LayerElementUV "Name"), in index order: [0] is Corner::uv, [k] is
    /// Part::extraUVs[k - 1]. Empty when the mesh has no UVs.
    std::vector<std::string> uvSetNames;
    Polygons polygons;             ///< the file's polygons (what the reader produces)
    std::vector<Part> parts;       ///< non-empty parts, ascending slot: empty until BuildParts(mesh)
    std::vector<int> materials;    ///< slot -> index into Scene::materials (-1 unknown)
    std::vector<Cluster> clusters; ///< empty when not skinned
    /// per control point: influences, strongest first
    std::vector<std::vector<Influence>> skinWeights;
    std::vector<BlendChannel> channels;
    /// Triangles in `parts` (0 until BuildParts), or the count BuildParts will make when the parts are not built yet.
    std::uint32_t triangleCount() const {
        if (parts.empty()) return (std::uint32_t)(polygons.corners / 3);
        std::uint32_t t = 0; for (const Part& p : parts) t += (std::uint32_t)(p.indices.size() / 3); return t;
    }
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
    double start = 0;              ///< the file time (seconds) that clip time 0 is (the take's LocalStart, else its first key)
    std::vector<NodeAnim> nodes;
    std::vector<BlendAnim> blends;
};

/// What the clips' keys are made from, kept by the reader until BuildClips (an internal type of FbxScene.cpp).
struct ClipSource { virtual ~ClipSource() = default; };

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
    std::vector<Clip> clips;           ///< name, duration and start from the reader; nodes and blends after BuildClips
    /// The reader's animation curves and clip plans until BuildClips makes the keys (then empty). Shared by copies.
    std::shared_ptr<ClipSource> clipSource;
    Settings settings;
    std::string creator;
};

/// How close the clip's linear playback must stay to the file's curves when animation keys are made from the file's
/// own key times (BuildOptions::fileKeys; docs/content/model_readers.md (FBX part), "Animation keys"). Between two keys the engine plays
/// translation and scale linearly and rotation as a normalised linear blend of quaternions; keys are added until that
/// playback is within these of the curves.
struct KeyTolerance {
    double positionRel = 1e-4;     ///< translation: this times the node's largest translation at its keys ...
    double positionAbs = 1e-5;     ///< ... plus this, in output units (metres with convertToYUpMeters)
    double rotationDeg = 0.05;     ///< rotation: angle between the played and the true orientation
    double scaleRel = 1e-4;        ///< scale: this times max(1, the node's largest |scale| at its keys)
    double percent = 0.01;         ///< blend shape DeformPercent (0..100), so 1e-4 of the weight
    /// Seconds: a key is added only more than this from its neighbours. File keys up to twice this apart (every frame
    /// at 30 Hz or faster) get nothing in between: playback there is linear between the file's own keys, as the
    /// fixed-rate bake was on the same grid, and the tolerances above are not enforced there. Keys 2 to 3 times this
    /// apart (25 or 24 Hz) get at most one key in the middle.
    double minGap = 1.0 / 60.0;
    double maxEulerStep = 45.0;    ///< degrees one Euler angle may move between two keys (at least 8 keys a turn)
};

/// Whether BuildOptions::fileKeys is on by default: yes, unless the environment variable OKAY_FBX_BAKE30 is "1"
/// (read once), which brings back the bake at a fixed rate for comparison.
bool FileKeysDefault();

struct BuildOptions {
    bool convertToYUpMeters = true;    ///< right-handed Y-up, 1 unit = 1 m (what ModelImport needs)
    /// Animation keys. true: a key at every key time of the node's curves (and the take's start and end), plus keys
    /// in between where linear playback would leave `keyTolerance`; a step gets a key just before and at its jump.
    /// false: every animated node sampled `bakeRate` times a second over the take (the only way before 2026-11-16).
    bool fileKeys = FileKeysDefault();
    double bakeRate = 30.0;            ///< samples per second when fileKeys is false
    KeyTolerance keyTolerance;
    Limits limits;
};

bool Build(const Document& doc, Scene& out, std::string* err, const BuildOptions& opt = BuildOptions());
/// The import step that turns a mesh's polygons into `parts`: polygons triangulated (ear clipping; missing normals made
/// flat per polygon), split per material slot, equal corners (byte for byte) sharing one vertex in order of first use.
/// Does nothing when the parts are already built. `releasePolygons` frees the polygon arrays afterwards. Returns false
/// (parts left empty) only when the mesh goes past `lim.maxCorners`, which the reader already refuses.
bool BuildParts(Mesh& m, std::string* err = nullptr, const Limits& lim = Limits(), bool releasePolygons = false);
/// BuildParts for every mesh of the scene.
bool BuildParts(Scene& s, std::string* err = nullptr, const Limits& lim = Limits(), bool releasePolygons = false);
/// The import step that makes the clips' keys (Clip::nodes and Clip::blends) from the curves the reader kept: keys at
/// the file's own key times plus the ones BuildOptions::keyTolerance needs, or the fixed-rate bake (BuildOptions as
/// given to the reader). Does nothing when there is nothing left to make. Returns false (err set) when the file goes
/// past Limits::maxTotalBakedSamples while the keys are made.
bool BuildClips(Scene& s, std::string* err = nullptr);
/// Parse + Build. The meshes come back as polygons (Mesh::polygons): call BuildParts for triangles; the clips come back
/// without keys: call BuildClips.
bool LoadMemory(const std::uint8_t* data, std::size_t size, Scene& out, std::string* err, const BuildOptions& opt = BuildOptions());
bool LoadFile(const std::string& path, Scene& out, std::string* err, const BuildOptions& opt = BuildOptions());

} // namespace fbx
} // namespace okay
