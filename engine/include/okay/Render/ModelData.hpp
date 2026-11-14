#pragma once
// ---------------------------------------------------------------------------
// ModelData — the seam between the engine and any third-party model importer.
//
//   * ImportedModel is OUR result type (nodes, mesh parts, materials, skeleton
//     weights, animation clips). No Assimp (or other library) type appears here.
//   * ImportModelData() is the single entry point that picks a backend for a
//     format: our own reader, or Assimp when the build has it (OKAY_USE_ASSIMP).
//     ModelImport.cpp builds scenes and meshes from ImportedModel only.
//   * Our own readers (OBJ+MTL, STL ascii/binary, PLY ascii/binary, COLLADA, 3DS, BVH, OFF, SMD, MD5, MS3D, ASE, 3MF, AMF,
//     X3D, DXF, LWO, MD2, MD3, Irrlicht mesh, B3D, DirectX .x) are
//     always compiled in. Which backend a format uses is a per-format switch
//     (SetImportBackend) with a build-time default (CMake OKAY_PREFER_OWN_READERS).
//
// See docs/model_import.md.
// ---------------------------------------------------------------------------
#include "okay/Render/Mesh.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Math/Mat4.hpp"
#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace okay {

/// One node of the model's transform hierarchy. Nodes are listed parent-first
/// (a node's `parent` index is always lower than its own), so a single forward
/// pass builds the tree. Node 0 is the root.
struct ImportedNode {
    std::string name;
    int   parent = -1;
    Vec3  position{0, 0, 0};
    Quat  rotation{0, 0, 0, 1};
    Vec3  scale{1, 1, 1};
};

/// Material factors and texture files (resolved to paths the renderer can load).
/// Unset factors are negative / `has*` false.
struct ImportedMaterial {
    std::string name;
    std::string texture, normalMap, specularMap, aoMap;
    bool  hasDiffuse = false;   Color diffuse{1, 1, 1, 1};
    bool  hasEmissive = false;  Color emissive{0, 0, 0, 1};
    float shininess = -1.0f;    ///< Phong exponent, < 0 = unset
    float metallic  = -1.0f;
    float roughness = -1.0f;
};

struct ImportedBone {
    std::string name;
    Mat4 inverseBind;
};

/// One drawable piece: a mesh on a node, with an optional material and skin.
struct ImportedPart {
    Mesh mesh;
    int  node = 0;          ///< index into ImportedModel::nodes
    int  material = -1;     ///< index into ImportedModel::materials, -1 = none
    // Skin (empty = rigid). When bones is non-empty, jointIdx/jointWt are
    // parallel to mesh.vertices and hold the 4 strongest influences.
    std::vector<ImportedBone> bones;
    std::vector<std::array<int, 4>>   jointIdx;
    std::vector<std::array<float, 4>> jointWt;
};

struct ImportedKeyV3 { float time; Vec3 value; };
struct ImportedKeyQ  { float time; Quat value; };
/// Animation of one node (by node name), times in seconds.
struct ImportedChannel {
    std::string node;
    std::vector<ImportedKeyV3> position, scale;
    std::vector<ImportedKeyQ>  rotation;
};
struct ImportedClip {
    std::string name;
    std::vector<ImportedChannel> channels;
};

/// Everything a backend reads from a model file, in our own types.
struct ImportedModel {
    std::vector<ImportedNode>     nodes;
    std::vector<ImportedPart>     parts;
    std::vector<ImportedMaterial> materials;
    std::vector<ImportedClip>     clips;
    std::string backend;            ///< "own" or "assimp" — who produced this
    /// Parts of the file the reader did not read (or read only partly), one line each.
    /// Our own readers fill this instead of dropping content silently; ImportModelData
    /// also logs each line as a warning. Empty = everything in the file was read.
    std::vector<std::string> notes;
};

/// What a model holds, for comparing two backends' output of the same file.
struct ModelCounts {
    int nodes = 0, parts = 0, skinnedParts = 0, bones = 0, vertices = 0, triangles = 0;
    int clips = 0, channels = 0;
    std::size_t keys = 0;
    bool operator==(const ModelCounts& o) const {
        return nodes == o.nodes && parts == o.parts && skinnedParts == o.skinnedParts && bones == o.bones &&
               vertices == o.vertices && triangles == o.triangles && clips == o.clips &&
               channels == o.channels && keys == o.keys;
    }
};
ModelCounts CountModel(const ImportedModel& m);

// ---- Our own readers (always built in) -------------------------------------
// Each parses an in-memory file. They never read past the buffer, never
// allocate from an unchecked count, and return false with a one-line `err` on
// malformed input. The path versions read the whole file first (size-capped).

/// Wavefront OBJ plus its .mtl (looked up in `baseDir`, which ends with a
/// slash or is empty). One part per `usemtl` run; faces are fan-triangulated;
/// v/vt/vn and negative indices are handled. UVs are kept as written (the
/// same as Mesh::LoadOBJ). Reads Kd, Ke, Ns, map_Kd, map_Bump/bump/norm,
/// map_Ks, map_Ka/map_ao from the .mtl. A missing .mtl is not an error.
bool ReadObjBuffer(const std::string& text, const std::string& baseDir, ImportedModel& out, std::string* err = nullptr);
/// STL, ASCII or binary (told apart by the binary size rule). One part per
/// `solid` (ASCII) or one part (binary). Vertices are welded per position+normal.
bool ReadStlBuffer(const std::string& bytes, ImportedModel& out, std::string* err = nullptr);
/// PLY, ascii / binary_little_endian / binary_big_endian. Vertex x,y,z, normals,
/// UVs (s,t / u,v / texture_u,texture_v); face vertex_indices lists are
/// fan-triangulated. Vertex colors and other elements are skipped. UV v is
/// flipped (v = 1 - v) like the other bottom-up formats. Without normals,
/// smooth normals are generated.
bool ReadPlyBuffer(const std::string& bytes, ImportedModel& out, std::string* err = nullptr);
/// True when `path` is a PLY whose header declares `element vertex` more than once.
/// Our reader rejects that file. Assimp still loads it, and the two blocks' normals
/// and UVs no longer line up with positions, so the import seam refuses it before
/// either backend runs. A missing file, or a PLY with one vertex element, is false.
bool PlyFileDeclaresDuplicateVertex(const std::string& path);

/// COLLADA (.dae) 1.4 / 1.5. Reads geometry (triangles, polylist, polygons; fan-triangulated,
/// welded per position+normal+uv, smooth normals when the file has none), materials (phong /
/// blinn / lambert / constant: diffuse, emission, shininess, specular and bump textures),
/// the node hierarchy (matrix, translate, rotate, scale, instance_node), skins (4 strongest
/// weights, bind shape folded into the inverse bind matrix) and animations whose channels
/// drive a whole node matrix (decomposed to position / rotation / scale keys). The root node
/// carries the file's unit scale and the up-axis rotation (the engine is Y-up). UV v is
/// flipped like the other bottom-up formats. `flatten` bakes every node transform into the
/// vertices and returns one root node, no skins, no clips. Everything not read (lines,
/// tristrips, morphs, per-component animation channels, vertex colors, extra UV sets,
/// cameras, lights ...) is reported in `out.notes`. `baseDir` ends with a slash or is empty.
bool ReadColladaBuffer(const std::string& text, const std::string& baseDir, ImportedModel& out, std::string* err = nullptr, bool flatten = false);

/// 3D Studio (.3ds), chunk format. Meshes (points, faces, UVs, smoothing groups -> smooth normals,
/// per-face material groups -> one part per material), materials (diffuse, specular and bump
/// textures, colours, shininess, self illumination), the keyframe node hierarchy with position /
/// rotation / scale tracks (30 frames per second), and the master scale. The root node carries the
/// master scale and the Z-up to Y-up rotation. UV v is flipped. `flatten` bakes that into the vertices
/// and returns one root node, no clips. What was not read (lights, cameras, opacity / reflection maps,
/// texture tiling, material transparency ...) is in `out.notes`. `baseDir` ends with a slash or is empty.
bool Read3dsBuffer(const std::string& bytes, const std::string& baseDir, ImportedModel& out, std::string* err = nullptr, bool flatten = false);
/// BVH motion capture (.bvh): the joint hierarchy as nodes (position = OFFSET, no meshes) and the
/// motion as one clip with a position and rotation key per frame and joint. Rotation channels are
/// composed in the file's channel order. Units are kept (BVH has none); the Y-up right-handed
/// convention of the common exporters is assumed.
bool ReadBvhBuffer(const std::string& text, ImportedModel& out, std::string* err = nullptr);
/// OFF (Object File Format, text): one mesh part from `OFF` / `NOFF` / `STOFF` (and `C` variants) files: vertices,
/// optional per-vertex normals and texture coordinates, polygons of any size (convex ones fanned, concave ones ear
/// clipped). Smooth normals are generated when the file has none. Colors, `4OFF` and `nOFF` are not read (reported in
/// `out.notes`, or an error for the last two). Units, axes and texture coordinates are kept as written.
bool ReadOffBuffer(const std::string& text, ImportedModel& out, std::string* err = nullptr);
/// SMD (Valve Studiomdl Data, text): the bone list as nodes under a root that turns Z-up into Y-up, triangles as one
/// skinned part per material (up to four bone weights per vertex, vertices welded, v flipped), the bind pose from the
/// first frame and, when the file has several frames, one clip ("animation", 30 frames per second assumed). Euler
/// angles follow Valve's convention. Vertex animation (flex) sections are not read (reported in `out.notes`).
/// `baseDir` ends with a slash or is empty (it prefixes the material names, which are the texture files).
bool ReadSmdBuffer(const std::string& text, const std::string& baseDir, ImportedModel& out, std::string* err = nullptr);
/// MD5 (id Tech 4 / Doom 3): `meshText` is an md5mesh, `animText` an md5anim (either may be empty, not both). The joints
/// become nodes under a root that turns Z-up into Y-up; each mesh is one skinned part (vertices computed at the bind
/// pose from the weights, four strongest joints per vertex, smooth normals generated, winding reversed to counter
/// clockwise, texture coordinates kept as written); the animation is one clip at the file's frameRate. With only an animation, the nodes come from
/// its hierarchy and base frame. Shader names that are not image files set no texture. `baseDir` ends with a slash or is empty.
bool ReadMd5Buffer(const std::string& meshText, const std::string& animText, const std::string& baseDir, ImportedModel& out, std::string* err = nullptr);
/// MS3D (MilkShape 3D, binary, versions 3 and 4): one skinned part per group (vertices welded on position, normal
/// and texture coordinate; normals and UVs come per triangle corner), materials (diffuse and emissive colour,
/// shininess, texture; the transparency goes into the diffuse alpha), the joint hierarchy as nodes, up to four joint
/// weights per vertex (the version 4 extra-weight block when present), and one clip ("animation") from the joints' rotation
/// and translation keys. Y-up like the engine; units, winding and texture coordinates are kept as written.
/// Comments, ambient / specular colours and smoothing groups are not read (reported in `out.notes`).
/// `baseDir` ends with a slash or is empty (it prefixes relative texture paths).
bool ReadMs3dBuffer(const std::string& bytes, const std::string& baseDir, ImportedModel& out, std::string* err = nullptr);
/// ASE (3ds Max ASCII Scene Export, text; UTF-8 or UTF-16 with a byte order mark): one part per object and material
/// (sub-materials by face material id; vertices welded; normals from MESH_NORMALS or built from the smoothing groups),
/// diffuse colour and bitmap, the node hierarchy from NODE_PARENT / NODE_TM and the position / rotation / scale sample
/// tracks as one clip (TCB and Bezier keys read as plain keys). The root node turns Z-up into Y-up; v is flipped.
/// Cameras, lights and shapes are plain empty nodes; vertex colours, extra mapping channels, other material maps and
/// texture transforms are not read (reported in `out.notes`). `baseDir` ends with a slash or is empty.
bool ReadAseBuffer(const std::string& text, const std::string& baseDir, ImportedModel& out, std::string* err = nullptr);
/// MD2 (Quake II model, binary): the first frame of the vertex animation as one static part (vertices welded on
/// position and texture coordinate, smooth normals generated, winding reversed to counter clockwise, texture coordinates as written), the
/// first skin name as the texture. The root node turns Z-up into Y-up. Further frames (vertex animation, which the engine
/// has no clip type for), the stored normals and the OpenGL command list are not read (reported in `out.notes`).
/// `baseDir` ends with a slash or is empty (it prefixes the skin path).
bool ReadMd2Buffer(const std::string& bytes, const std::string& baseDir, ImportedModel& out, std::string* err = nullptr);
/// MD3 (Quake III Arena model, binary): the first frame of the vertex animation, one static part per surface (vertices
/// welded, the stored normals decoded, winding reversed to counter clockwise, texture coordinates as written), the first shader name of each
/// surface as its texture (as written: Quake paths are relative to the game folder), and each tag (attachment point) of the
/// first frame as an empty child node of the root, which turns Z-up into Y-up. Further frames, tag animation, `.skin` and
/// `.shader` files are not read (reported in `out.notes`).
bool ReadMd3Buffer(const std::string& bytes, ImportedModel& out, std::string* err = nullptr);
/// Irrlicht mesh (.irrmesh, XML; UTF-8 or UTF-16 with a byte order mark): one part per `<buffer>` (vertices of type
/// standard, 2tcoords or tangents; triangle indices), with the buffer's material: diffuse and emissive colour,
/// shininess, the first texture, and the second texture as the normal map for normalmap / parallaxmap types. Texture
/// coordinates, coordinates and winding as written (Irrlicht is left-handed; Assimp keeps them too); no root rotation.
/// Second texture coordinates, tangents, vertex colours, lightmaps and material flags are not read (reported in
/// `out.notes`). `baseDir` ends with a slash or is empty (it prefixes relative texture paths). The Irrlicht scene
/// format (.irr) is not read.
bool ReadIrrMeshBuffer(const std::string& text, const std::string& baseDir, ImportedModel& out, std::string* err = nullptr);
/// B3D (Blitz3D model, binary): the node tree (position, scale, rotation), one part per TRIS chunk (vertices welded per
/// vertex number, normals, first texture coordinate set, the brush as material: diffuse colour and alpha, shininess,
/// first texture), skin weights (the four strongest per vertex; every node with a BONE chunk is a joint) and one clip
/// ("animation") from the KEYS chunks at the ANIM chunk's frame rate. Blitz3D is left-handed: Z is mirrored and the winding
/// reversed. Vertex colours, further texture coordinate sets, texture and brush flags and effects are not read (reported in
/// `out.notes`). `baseDir` ends with a slash or is empty (it prefixes relative texture paths).
bool ReadB3dBuffer(const std::string& bytes, const std::string& baseDir, ImportedModel& out, std::string* err = nullptr);
/// DirectX .x (text, binary, and MSZIP-compressed "tzip" / "bzip"; 32 and 64 bit floats): the frame tree as nodes, each
/// mesh split into one part per material (vertices welded per position and normal, polygons fan-triangulated, normals as
/// stored or smooth ones, the first texture coordinate set as written), materials (diffuse colour and alpha, emissive,
/// power as shininess, texture and normal map file), skin weights (the four strongest per vertex, the offset matrix as
/// the inverse bind) and each AnimationSet as one clip (rotation, scale, position and matrix keys at the file's
/// AnimTicksPerSecond, 25 without one). DirectX is left-handed and writes matrices for row vectors: Z is mirrored and the
/// winding reversed. Vertex colours, further texture coordinate sets, specular colour and shared (referenced) meshes
/// are not read (reported in `out.notes`). `baseDir` ends with a slash or is empty (it prefixes relative texture paths).
bool ReadXBuffer(const std::string& bytes, const std::string& baseDir, ImportedModel& out, std::string* err = nullptr);

bool ReadObjFile(const std::string& path, ImportedModel& out, std::string* err = nullptr);
bool ReadStlFile(const std::string& path, ImportedModel& out, std::string* err = nullptr);
bool ReadPlyFile(const std::string& path, ImportedModel& out, std::string* err = nullptr);
bool ReadColladaFile(const std::string& path, ImportedModel& out, std::string* err = nullptr, bool flatten = false);
bool Read3dsFile(const std::string& path, ImportedModel& out, std::string* err = nullptr, bool flatten = false);
bool ReadBvhFile(const std::string& path, ImportedModel& out, std::string* err = nullptr);
bool ReadOffFile(const std::string& path, ImportedModel& out, std::string* err = nullptr);
bool ReadSmdFile(const std::string& path, ImportedModel& out, std::string* err = nullptr);
/// `path` is an .md5mesh (the .md5anim with the same name next to it is read too when it exists) or an .md5anim alone.
bool ReadMd5File(const std::string& path, ImportedModel& out, std::string* err = nullptr);
bool ReadMs3dFile(const std::string& path, ImportedModel& out, std::string* err = nullptr);
bool ReadAseFile(const std::string& path, ImportedModel& out, std::string* err = nullptr);
bool ReadMd2File(const std::string& path, ImportedModel& out, std::string* err = nullptr);
bool ReadMd3File(const std::string& path, ImportedModel& out, std::string* err = nullptr);
bool ReadIrrMeshFile(const std::string& path, ImportedModel& out, std::string* err = nullptr);
bool ReadB3dFile(const std::string& path, ImportedModel& out, std::string* err = nullptr);
bool ReadXFile(const std::string& path, ImportedModel& out, std::string* err = nullptr);

// ---- Backend selection (the seam) ------------------------------------------

enum class ImportBackend {
    Default,   ///< the format's default (see docs/model_import.md)
    Own,       ///< our reader; the formats listed at the top of this file have one (HasOwnReader tells)
    Assimp     ///< Assimp; only when AssimpAvailable()
};

/// True when `ext` (".stl", any case) has one of our own readers.
/// Collada (.dae), 3DS (.3ds) and BVH (.bvh) are the odd ones out: their readers are off by default
/// (docs/model_import.md); each runs when SetImportBackend(ext, Own), the environment variable
/// OKAY_OWN_COLLADA=1 / OKAY_OWN_3DS=1 / OKAY_OWN_BVH=1 or the matching CMake option says so
/// (run-time choice beats the variable, which beats CMake; =0 forces it off).
bool HasOwnReader(const std::string& ext);
/// Choose the backend for one extension (".stl"), or for every format with
/// an own reader when `ext` is "*". Returns false for an extension without an own
/// reader (nothing to switch). Not meant to be flipped while another thread imports.
bool SetImportBackend(const std::string& ext, ImportBackend which);
/// The backend a read of `ext` will actually use right now, after defaults and
/// fallbacks: Own, Assimp, or Default when no backend reads that format.
ImportBackend EffectiveImportBackend(const std::string& ext);

/// The single entry point for every Assimp / own-reader read. Reads `path` with
/// the chosen backend (`which` = Default: per-format switch). `flatten` is
/// the legacy "one static mesh" mode: node transforms are baked into the
/// vertices and no skins or clips are returned. Returns false with `err` set when
/// the file can't be read.
bool ImportModelData(const std::string& path, ImportedModel& out, ImportBackend which = ImportBackend::Default,
                     bool flatten = false, std::string* err = nullptr);

/// One merged Mesh from an ImportedModel (parts concatenated, node transforms
/// NOT applied: use flatten for that), with `outTexture` getting the first
/// diffuse texture found.
Mesh MergeModelMeshes(const ImportedModel& m, std::string* outTexture = nullptr);

class Scene;
class GameObject;
/// Build the model as a node hierarchy in `scene` (MeshRenderers, SkinnedMesh
/// from bones, a ModelAnimator with the clips). Returns the root, or nullptr.
GameObject* BuildSceneFromModel(Scene& scene, const ImportedModel& m, const std::string& path);

} // namespace okay
