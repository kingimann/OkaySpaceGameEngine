#include <set>
#include <exception>
#include <functional>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include "okay/Render/ModelImport.hpp"
#include "okay/Render/ThreeMfReader.hpp"
#include "okay/Render/AmfReader.hpp"
#include "okay/Render/X3dReader.hpp"
#include "okay/Render/DxfReader.hpp"
#include "okay/Render/LwoReader.hpp"
#include "okay/Core/Log.hpp"
#include <cstdlib>
#include <cstring>
#include "okay/Core/ImportSidecar.hpp"
#include "okay/Render/Gltf.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/Animator.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Components/SkinnedMesh.hpp"
#include "okay/Components/BlendShapes.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Animation/HumanoidLibrary.hpp"
#include "okay/Components/FootIK.hpp"
#include "okay/Components/HumanoidRetarget.hpp"
#include "okay/Components/LookAtIK.hpp"
#include "okay/Scene/SceneTemplates.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/PlayerController.hpp"
#include "okay/Components/ThirdPersonController.hpp"
#include "okay/Components/ThirdPersonShooterController.hpp"
#include "okay/Components/FirstPersonController.hpp"
#include "okay/Components/CharacterController3D.hpp"
#include "okay/Components/ClickToMoveController.hpp"
#include "okay/Components/TopDownController.hpp"
#include "okay/Components/NPCController.hpp"
#include "okay/Components/Platformer25DController.hpp"
#include "okay/Components/TopDownShooterController.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Math/Mat4.hpp"
#include "okay/Render/FbxReader.hpp"
#include "okay/Core/ThirdParty.hpp"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <atomic>
#include <chrono>
#include "ModelImportInternal.hpp"
#include "FileOpen.hpp"
#include <cstring>
#include <cstdio>

namespace okay {
using model_import::Lower;
using model_import::BaseName;
// Which reader read the last model (set by the import paths below, logged once per ImportModelScene): "the own LXO
// reader", "the own FBX reader", "Assimp", ... and the reading time in ms (-1 = not measured). Thread-local: imports on two threads
// do not mix.
static thread_local std::string g_readBy;
static thread_local double g_readMs = -1.0;
// Validate a freshly built skin's joint indices against its joint list (the one
// place every importer funnels through) and say so when the file had bad ones.
static void ValidateSkinJoints(SkinnedMesh& sm, const std::string& path) {
    const int bad = sm.SanitizeJoints(sm.joints.size());
    if (bad > 0)
        Log::Warning("model import '", path, "': ", bad, " joint index(es) outside the ", sm.joints.size(),
                     "-joint list were dropped (weight zeroed)");
}


bool AssimpAvailable() {
#ifdef OKAY_HAVE_ASSIMP
    return true;
#else
    return false;
#endif
}

std::string ImportableExtensions() {
    // Built in: OBJ, glTF, FBX (our own okay::fbx) and our own STL / PLY readers, plus everything
    // the Assimp build can read. .dae is added when our own Collada reader is switched on
    // (it is off by default, so this part is worked out per call, not cached).
    static const std::string list = [] {
        std::string out = ".obj .gltf .glb .fbx .stl .ply";
#ifdef OKAY_HAVE_ASSIMP
        std::string e = assimp_backend::Extensions(), cur;
        e += ' ';
        for (char c : e) {
            if (c != ' ') { cur += c; continue; }
            if (!cur.empty() && (" " + out + " ").find(" " + cur + " ") == std::string::npos) out += " " + cur;
            cur.clear();
        }
#endif
        return out;
    }();
    std::string res = list;
    for (const char* opt : {".dae", ".3ds", ".bvh", ".off", ".smd", ".md5mesh", ".md5anim", ".ms3d", ".ase", ".3mf", ".amf", ".x3d", ".dxf", ".lwo", ".blend", ".md2", ".md3", ".irrmesh", ".b3d", ".x", ".ac", ".acc", ".ac3d", ".mesh.xml", ".xgl", ".zgl", ".irr", ".nff", ".enff", ".ter", ".mdc", ".hmp", ".csm", ".q3o", ".q3s", ".raw", ".iqm", ".cob", ".pmx", ".lws", ".mot", ".mesh", ".zae", ".xml", ".lxo", ".ogex", ".mdl", ".bsp", ".pk3"})   // not ".ndo": that reader is a seam and refuses every file, so it must not be advertised
        if ((" " + res + " ").find(std::string(" ") + opt + " ") == std::string::npos && EffectiveImportBackend(opt) == ImportBackend::Own)
            res += std::string(" ") + opt;
    return res;
}

std::string model_import::Lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
static bool EndsWith(const std::string& s, const char* suf) {
    std::string t = suf;
    return s.size() >= t.size() && s.compare(s.size() - t.size(), t.size(), t) == 0;
}
static Mesh ImportFbxMerged(const std::string& path, bool* ok, std::string* outTexture);   // (FBX section below)
static Mesh ImportFbxMergedOwn(const std::string& path, bool* ok, std::string* outTexture, std::string* err);   // our own reader (okay::fbx)
static GameObject* ImportFbxSceneGraphOwn(Scene& scene, const std::string& path, bool* ok, std::string* err);
// Imported clips: every node track spans the whole clip (see ModelAnimator::NormalizeClip).
static void NormalizeImportedClips(GameObject* root) {
    if (!root) return;
    if (auto* ma = root->GetComponent<ModelAnimator>()) {
        // Baked imports carry a key per frame on every channel, most on straight
        // lines: reduce them (within ~0.03 deg / 0.05% of travel) before use —
        // smaller scenes, faster loads, cheaper sampling.
        ma->OptimizeClips();
        for (auto& c : ma->clips) ModelAnimator::NormalizeClip(c);
    }
}


// A .gltf / .glb file of version 1.0 that our own glTF 1.0 reader should read (only asked when that reader is switched on,
// so a default import never parses the file twice).
static bool Gltf1Wanted(const std::string& path) {
    if (EffectiveImportBackend(".gltf1") != ImportBackend::Own) return false;
    std::string why;   // reading and parsing the file can throw (bad_alloc on a huge file): a throw means "no"
    return RunReaderGuarded([&]() -> bool { return GltfFileIsVersion1(path); }, why);
}

// Only an ordinary file can be a model. A folder named "x.glb" or "x.obj" opens as a stream on some systems and reports a
// huge size or throws on the first read (std::length_error, std::bad_alloc, std::ios_base::failure ended the editor).
static bool IsModelFile(const std::string& path) {
    std::error_code ec;
    return !path.empty() && std::filesystem::is_regular_file(std::filesystem::u8path(path), ec);
}

Mesh ImportModel(const std::string& path, bool* ok, std::string* outTexture) {
    if (!IsModelFile(path)) { if (ok) *ok = false; return Mesh(); }
    std::string p = Lower(path);
    if (EndsWith(p, ".obj") && EffectiveImportBackend(".obj") != ImportBackend::Assimp) {
        g_readBy = "the own OBJ reader";
        return Mesh::LoadOBJ(path, ok, outTexture);
    }
    if ((EndsWith(p, ".gltf") || EndsWith(p, ".glb")) && !Gltf1Wanted(path)) { g_readBy = "the own glTF reader"; return LoadGLTF(path, ok, outTexture); }
    if (EndsWith(p, ".fbx")) {
        g_readBy = "the own FBX reader (one mesh)";   // the only FBX reader (ufbx was deleted on 2026-11-23)
        bool fok = false;
        Mesh m = ImportFbxMerged(path, &fok, outTexture);
        // There is no other FBX reader: a file ours refuses (or that holds no mesh) fails here, as in the scene path.
        if (ok) *ok = fok;
        return fok ? m : Mesh();
    }
    // Everything else goes through the import seam (okay/Render/ModelData.hpp): our own
    // STL / PLY readers, or Assimp when the build has it. One static mesh.
    ImportedModel im;
    Mesh m;
    if (!ImportModelData(path, im, ImportBackend::Default, /*flatten=*/true, nullptr)) {
        if (ok) *ok = false;
        return m;
    }
    m = MergeModelMeshes(im, outTexture);
    Mesh::NormalizeImportScale(m);   // cm/mm exports (Meshy FBX etc.) land at a usable size
    if (ok) *ok = !m.vertices.empty();
    return m;
}

std::string model_import::BaseName(const std::string& path) {
    std::string n = path;
    std::size_t sl = n.find_last_of("/\\"); if (sl != std::string::npos) n = n.substr(sl + 1);
    std::size_t dot = n.find_last_of('.');  if (dot != std::string::npos) n = n.substr(0, dot);
    return n.empty() ? "Model" : n;
}

// Build a glTF mesh that also carries per-vertex skin data (JOINTS_0 / WEIGHTS_0),
// kept aligned with the merged vertices. `jIdx`/`jWt` index into the skin's joint list.
// Up to 8 influences (JOINTS_0/1 + WEIGHTS_0/1) are read; the 4 strongest are kept
// and renormalized to sum to 1. `onlyPrim` >= 0 builds just that primitive (one
// material of a multi-material character).
static Mesh BuildSkinnedMesh(const gltf_detail::GltfDoc& doc, int meshIndex,
                             std::vector<std::array<int, 4>>& jIdx,
                             std::vector<std::array<float, 4>>& jWt, int onlyPrim = -1) {
    using namespace gltf_detail;
    Mesh mesh; jIdx.clear(); jWt.clear();
    const JVal* meshes = doc.root.Find("meshes");
    if (!meshes || meshIndex < 0 || meshIndex >= (int)meshes->arr.size()) return mesh;
    const JVal* prims = meshes->arr[meshIndex].Find("primitives");
    if (!prims) return mesh;
    int primNo = -1;
    for (const JVal& prim : prims->arr) {
        ++primNo;
        if (onlyPrim >= 0 && primNo != onlyPrim) continue;
        const JVal* attr = prim.Find("attributes"); if (!attr) continue;
        const int base = (int)mesh.vertices.size();
        PrimGeom g;
        if (!ReadPrimitive(doc, prim, g)) continue;   // malformed: left out, as AppendPrimitive does
        const int pn = g.pn;
        for (int i = 0; i < pn; ++i) mesh.vertices.push_back(Vec3{g.pos[i*3+0], g.pos[i*3+1], g.pos[i*3+2]});
        for (int i = 0; i < g.nn; ++i) mesh.normals.push_back(Vec3{g.nor[i*3+0], g.nor[i*3+1], g.nor[i*3+2]});
        for (int i = 0; i < g.tn; ++i) mesh.uvs.push_back(Vec2{g.uv[i*2+0], g.uv[i*2+1]});
        std::vector<float> jv[2], wv[2]; int jc[2] = {0, 0}, jn[2] = {0, 0}, wc[2] = {0, 0}, wn[2] = {0, 0};
        for (int set = 0; set < 2; ++set) {
            std::string js = "JOINTS_" + std::to_string(set), ws = "WEIGHTS_" + std::to_string(set);
            if (const JVal* J = attr->Find(js.c_str())) jv[set] = ReadAccessor(doc, J->Int(-1), jc[set], jn[set]);
            if (const JVal* W = attr->Find(ws.c_str())) wv[set] = ReadAccessor(doc, W->Int(-1), wc[set], wn[set]);
        }
        for (int i = 0; i < pn; ++i) {
            // Gather every influence, keep the strongest 4, renormalize.
            std::pair<float, int> inf[8]; int ni = 0;
            for (int set = 0; set < 2; ++set) {
                if (jc[set] < 4 || wc[set] < 4 || i >= jn[set] || i >= wn[set]) continue;
                for (int k = 0; k < 4; ++k) {
                    float w = wv[set][(std::size_t)i * wc[set] + k];
                    const float jf = jv[set][(std::size_t)i * jc[set] + k];
                    if (w > 0.0f && jf >= 0.0f && jf < 65536.0f) inf[ni++] = {w, (int)(jf + 0.5f)};
                }
            }
            std::sort(inf, inf + ni, [](const std::pair<float, int>& a, const std::pair<float, int>& b) { return a.first > b.first; });
            std::array<int, 4>   idx{0, 0, 0, 0};
            std::array<float, 4> w{1, 0, 0, 0};   // no weights at all: bound to joint 0
            if (ni > 0) {
                float sum = 0.0f;
                for (int k = 0; k < 4 && k < ni; ++k) sum += inf[k].first;
                w = {0, 0, 0, 0};
                for (int k = 0; k < 4 && k < ni; ++k) { idx[k] = inf[k].second; w[k] = sum > 0.0f ? inf[k].first / sum : 0.0f; }
            }
            jIdx.push_back(idx); jWt.push_back(w);
        }
        for (int v : g.tri) mesh.triangles.push_back(base + v);
    }
    if (mesh.normals.size() != mesh.vertices.size()) mesh.normals.clear();
    if (mesh.uvs.size()     != mesh.vertices.size()) mesh.uvs.clear();
    return mesh;
}

// Morph targets ("targets" on each primitive) of a glTF mesh as blend shapes,
// aligned with the vertices BuildSkinnedMesh / BuildMeshAt produce: primitives
// concatenated, or just `onlyPrim`. Names come from mesh.extras.targetNames,
// default weights from mesh.weights. Primitives without targets get zero offsets.
static void AddGltfMorphTargets(const gltf_detail::GltfDoc& doc, int meshIndex, int onlyPrim, GameObject* host) {
    using namespace gltf_detail;
    const JVal* meshes = doc.root.Find("meshes");
    if (!host || !meshes || meshIndex < 0 || meshIndex >= (int)meshes->arr.size()) return;
    const JVal& mesh = meshes->arr[meshIndex];
    const JVal* prims = mesh.Find("primitives");
    if (!prims) return;
    int nTargets = 0;
    for (const JVal& prim : prims->arr)
        if (const JVal* T = prim.Find("targets")) nTargets = std::max(nTargets, (int)T->arr.size());
    if (nTargets == 0) return;

    std::vector<std::vector<Vec3>> dp(nTargets), dn(nTargets);
    bool anyN = false;
    int primNo = -1, base = 0;
    for (const JVal& prim : prims->arr) {
        ++primNo;
        if (onlyPrim >= 0 && primNo != onlyPrim) continue;
        PrimGeom g;
        const int pn = ReadPrimitive(doc, prim, g) ? g.pn : 0;   // skipped primitives add no vertices
        for (int t = 0; t < nTargets; ++t) { dp[t].resize((std::size_t)base + pn, Vec3{0, 0, 0}); dn[t].resize((std::size_t)base + pn, Vec3{0, 0, 0}); }
        if (const JVal* T = prim.Find("targets"))
            for (int t = 0; t < (int)T->arr.size() && t < nTargets; ++t) {
                const JVal& tg = T->arr[t];
                if (const JVal* TP = tg.Find("POSITION")) {
                    int c = 0, n = 0; auto v = ReadAccessor(doc, TP->Int(-1), c, n);
                    for (int i = 0; i < n && i < pn && c >= 3; ++i) dp[t][(std::size_t)base + i] = Vec3{v[i*c], v[i*c+1], v[i*c+2]};
                }
                if (const JVal* TN = tg.Find("NORMAL")) {
                    int c = 0, n = 0; auto v = ReadAccessor(doc, TN->Int(-1), c, n);
                    for (int i = 0; i < n && i < pn && c >= 3; ++i) dn[t][(std::size_t)base + i] = Vec3{v[i*c], v[i*c+1], v[i*c+2]};
                    anyN = anyN || n > 0;
                }
            }
        base += pn;
    }
    // A part none of whose vertices move (another primitive carries the targets): no shapes.
    bool moves = false;
    for (int t = 0; t < nTargets && !moves; ++t)
        for (std::size_t v = 0; v < dp[t].size() && !moves; ++v)
            moves = dp[t][v].x != 0.0f || dp[t][v].y != 0.0f || dp[t][v].z != 0.0f;
    if (!moves) return;
    const JVal* names = nullptr;
    if (const JVal* ex = mesh.Find("extras")) names = ex->Find("targetNames");
    const JVal* defW = mesh.Find("weights");
    auto* bs = host->AddComponent<BlendShapes>();
    for (int t = 0; t < nTargets; ++t) {
        std::string nm = (names && t < (int)names->arr.size()) ? names->arr[t].Text() : std::string{};
        if (nm.empty()) nm = "shape" + std::to_string(t);
        float w = (defW && t < (int)defW->arr.size()) ? (float)defW->arr[t].Number(0.0) : 0.0f;
        bs->AddShape(nm, dp[t], anyN ? dn[t] : std::vector<Vec3>{}, w);
    }
    // An unskinned morphing mesh gets a pass-through skin (one joint: itself,
    // identity bind) so the renderers treat it as a mesh that changes.
    if (!host->GetComponent<SkinnedMesh>())
        if (auto* mr = host->GetComponent<MeshRenderer>()) {
            auto* sm = host->AddComponent<SkinnedMesh>();
            sm->bind = mr->mesh;
            sm->jointIdx.assign(mr->mesh.vertices.size(), std::array<int, 4>{0, 0, 0, 0});
            sm->jointWt.assign(mr->mesh.vertices.size(), std::array<float, 4>{1, 0, 0, 0});
            sm->joints = {host->transform}; sm->jointNames = {host->name};
            sm->inverseBind = {Mat4::Identity()};
        }
    bs->Apply(true);
}

// Apply a resolved glTF material to a MeshRenderer: albedo + normal-map textures,
// base-color tint and emissive color, and a metallic-roughness -> Blinn-Phong
// approximation (metallic drives the specular strength; smoother = tighter highlight).
static void ApplyGltfMaterial(MeshRenderer& mr, const gltf_detail::GltfMat& gm) {
    if (!gm.baseColorTex.empty()) mr.texture   = gm.baseColorTex;
    if (!gm.normalTex.empty())    mr.normalMap = gm.normalTex;
    if (!gm.aoTex.empty())        mr.aoMap     = gm.aoTex;
    if (!gm.metallicRoughnessTex.empty()) mr.metallicRoughnessMap = gm.metallicRoughnessTex;
    if (!gm.emissiveTex.empty())  mr.emissiveMap = gm.emissiveTex;
    if (gm.hasBaseColorFactor)    mr.color     = Color(gm.baseColor[0], gm.baseColor[1], gm.baseColor[2], gm.baseColor[3]);
    if (gm.hasEmissiveFactor)     mr.emissive  = Color(gm.emissive[0], gm.emissive[1], gm.emissive[2], 1.0f);
    if (gm.hasMetalRough) {
        // Map PBR metallic-roughness onto this renderer's fields: metallic is a real
        // field; roughness drives highlight tightness/strength; metals reflect the
        // environment more the smoother they are.
        float smooth = 1.0f - gm.roughness;
        mr.metallic     = gm.metallic;
        mr.shininess    = 4.0f + smooth * 124.0f;
        mr.specular     = 0.15f + smooth * 0.85f;
        mr.reflectivity = gm.metallic * smooth;
    }
}

// Auto-normalize an imported model's size. Exporters disagree on units — Meshy AI /
// FBX assets often arrive in centimeters, making a character 180 "meters" tall in
// the scene. When the imported hierarchy's largest mesh dimension is implausibly
// big (or microscopically small), scale the ROOT transform so it lands at a usable
// size; the mesh data itself is untouched, and the user can still adjust Scale.
static void AutoNormalizeImportScale(Scene& scene, GameObject* root) {
    if (!root || !root->transform) return;
    float maxDim = 0.0f;
    Vec3 wlo{0, 0, 0}, whi{0, 0, 0}; bool any = false;
    for (const auto& up : scene.Objects()) {
        GameObject* go = up.get();
        if (!go || !go->IsSelfOrDescendantOf(root)) continue;
        auto* mr = go->GetComponent<MeshRenderer>();
        if (!mr || mr->mesh.vertices.empty()) continue;
        // Measure in WORLD space: importers often park unit conversions in node
        // scales (e.g. Collada's 0.01 cm->m node), so raw mesh bounds alone would
        // mis-size the model and this correction would compound the error.
        Vec3 lo, hi; mr->mesh.Bounds(lo, hi);
        Mat4 l2w = go->transform->LocalToWorldMatrix();
        for (int c = 0; c < 8; ++c) {
            Vec3 corner{c & 1 ? hi.x : lo.x, c & 2 ? hi.y : lo.y, c & 4 ? hi.z : lo.z};
            Vec3 w = l2w.MultiplyPoint(corner);
            if (!any) { wlo = whi = w; any = true; }
            else {
                wlo.x = std::fmin(wlo.x, w.x); whi.x = std::fmax(whi.x, w.x);
                wlo.y = std::fmin(wlo.y, w.y); whi.y = std::fmax(whi.y, w.y);
                wlo.z = std::fmin(wlo.z, w.z); whi.z = std::fmax(whi.z, w.z);
            }
        }
    }
    if (any) {
        Vec3 sz = whi - wlo;
        maxDim = sz.x > sz.y ? (sz.x > sz.z ? sz.x : sz.z) : (sz.y > sz.z ? sz.y : sz.z);
    }
    if (maxDim <= 0.0f) return;
    float s = 1.0f;
    if (maxDim > 20.0f)        s = 2.0f / maxDim;      // cm-style export: bring to ~2 units
    else if (maxDim < 0.02f)   s = 2.0f / maxDim;      // mm/tiny export: scale up
    if (s != 1.0f)
        root->transform->localScale = root->transform->localScale * s;
}

// ============================================================================
// The model-import seam (okay/Render/ModelData.hpp): backend choice, the merged
// mesh, and the scene builder. Nothing below touches a third-party type.
// ============================================================================

ModelCounts CountModel(const ImportedModel& m) {
    ModelCounts c;
    c.nodes = (int)m.nodes.size();
    c.parts = (int)m.parts.size();
    std::set<std::string> bones;
    for (const ImportedPart& p : m.parts) {
        c.vertices += (int)p.mesh.vertices.size();
        c.triangles += p.mesh.TriangleCount();
        if (!p.bones.empty()) ++c.skinnedParts;
        for (const ImportedBone& b : p.bones) bones.insert(b.name);
    }
    c.bones = (int)bones.size();
    c.clips = (int)m.clips.size();
    for (const ImportedClip& cl : m.clips) {
        c.channels += (int)cl.channels.size();
        for (const ImportedChannel& ch : cl.channels) c.keys += ch.position.size() + ch.rotation.size() + ch.scale.size();
    }
    return c;
}

namespace {

#ifdef OKAY_PREFER_OWN_READERS_DEFAULT
constexpr bool kPreferOwnDefault = true;
#else
constexpr bool kPreferOwnDefault = false;
#endif

#ifdef OKAY_OWN_COLLADA_DEFAULT
constexpr bool kOwnColladaDefault = true;
#else
constexpr bool kOwnColladaDefault = false;
#endif
#ifdef OKAY_OWN_3DS_DEFAULT
constexpr bool kOwn3dsDefault = true;
#else
constexpr bool kOwn3dsDefault = false;
#endif
#ifdef OKAY_OWN_BVH_DEFAULT
constexpr bool kOwnBvhDefault = true;
#else
constexpr bool kOwnBvhDefault = false;
#endif

#ifdef OKAY_OWN_OFF_DEFAULT
constexpr bool kOwnOffDefault = true;
#else
constexpr bool kOwnOffDefault = false;
#endif

#ifdef OKAY_OWN_SMD_DEFAULT
constexpr bool kOwnSmdDefault = true;
#else
constexpr bool kOwnSmdDefault = false;
#endif

#ifdef OKAY_OWN_MD5_DEFAULT
constexpr bool kOwnMd5Default = true;
#else
constexpr bool kOwnMd5Default = false;
#endif

#ifdef OKAY_OWN_MS3D_DEFAULT
constexpr bool kOwnMs3dDefault = true;
#else
constexpr bool kOwnMs3dDefault = false;
#endif

#ifdef OKAY_OWN_ASE_DEFAULT
constexpr bool kOwnAseDefault = true;
#else
constexpr bool kOwnAseDefault = false;
#endif

#ifdef OKAY_OWN_3MF_DEFAULT
constexpr bool kOwn3mfDefault = true;
#else
constexpr bool kOwn3mfDefault = false;
#endif

#ifdef OKAY_OWN_AMF_DEFAULT
constexpr bool kOwnAmfDefault = true;
#else
constexpr bool kOwnAmfDefault = false;
#endif

#ifdef OKAY_OWN_X3D_DEFAULT
constexpr bool kOwnX3dDefault = true;
#else
constexpr bool kOwnX3dDefault = false;
#endif

#ifdef OKAY_OWN_DXF_DEFAULT
constexpr bool kOwnDxfDefault = true;
#else
constexpr bool kOwnDxfDefault = false;
#endif

#ifdef OKAY_OWN_LWO_DEFAULT
constexpr bool kOwnLwoDefault = true;
#else
constexpr bool kOwnLwoDefault = false;
#endif

#ifdef OKAY_OWN_MD2_DEFAULT
constexpr bool kOwnMd2Default = true;
#else
constexpr bool kOwnMd2Default = false;
#endif

#ifdef OKAY_OWN_MD3_DEFAULT
constexpr bool kOwnMd3Default = true;
#else
constexpr bool kOwnMd3Default = false;
#endif

#ifdef OKAY_OWN_IRRMESH_DEFAULT
constexpr bool kOwnIrrMeshDefault = true;
#else
constexpr bool kOwnIrrMeshDefault = false;
#endif

#ifdef OKAY_OWN_B3D_DEFAULT
constexpr bool kOwnB3dDefault = true;
#else
constexpr bool kOwnB3dDefault = false;
#endif

#ifdef OKAY_OWN_X_DEFAULT
constexpr bool kOwnXDefault = true;
#else
constexpr bool kOwnXDefault = false;
#endif

#ifdef OKAY_OWN_AC3D_DEFAULT
constexpr bool kOwnAc3dDefault = true;
#else
constexpr bool kOwnAc3dDefault = false;
#endif

#ifdef OKAY_OWN_OGRE_DEFAULT
constexpr bool kOwnOgreDefault = true;
#else
constexpr bool kOwnOgreDefault = false;
#endif

#ifdef OKAY_OWN_XGL_DEFAULT
constexpr bool kOwnXglDefault = true;
#else
constexpr bool kOwnXglDefault = false;
#endif

#ifdef OKAY_OWN_IRR_DEFAULT
constexpr bool kOwnIrrDefault = true;
#else
constexpr bool kOwnIrrDefault = false;
#endif

#ifdef OKAY_OWN_NFF_DEFAULT
constexpr bool kOwnNffDefault = true;
#else
constexpr bool kOwnNffDefault = false;
#endif

#ifdef OKAY_OWN_TER_DEFAULT
constexpr bool kOwnTerDefault = true;
#else
constexpr bool kOwnTerDefault = false;
#endif

#ifdef OKAY_OWN_MDC_DEFAULT
constexpr bool kOwnMdcDefault = true;
#else
constexpr bool kOwnMdcDefault = false;
#endif

#ifdef OKAY_OWN_HMP_DEFAULT
constexpr bool kOwnHmpDefault = true;
#else
constexpr bool kOwnHmpDefault = false;
#endif

#ifdef OKAY_OWN_CSM_DEFAULT
constexpr bool kOwnCsmDefault = true;
#else
constexpr bool kOwnCsmDefault = false;
#endif

#ifdef OKAY_OWN_Q3D_DEFAULT
constexpr bool kOwnQ3dDefault = true;
#else
constexpr bool kOwnQ3dDefault = false;
#endif

#ifdef OKAY_OWN_RAW_DEFAULT
constexpr bool kOwnRawDefault = true;
#else
constexpr bool kOwnRawDefault = false;
#endif

#ifdef OKAY_OWN_IQM_DEFAULT
constexpr bool kOwnIqmDefault = true;
#else
constexpr bool kOwnIqmDefault = false;
#endif

#ifdef OKAY_OWN_COB_DEFAULT
constexpr bool kOwnCobDefault = true;
#else
constexpr bool kOwnCobDefault = false;
#endif

#ifdef OKAY_OWN_NDO_DEFAULT
constexpr bool kOwnNdoDefault = true;
#else
constexpr bool kOwnNdoDefault = false;
#endif

#ifdef OKAY_OWN_PMX_DEFAULT
constexpr bool kOwnPmxDefault = true;
#else
constexpr bool kOwnPmxDefault = false;
#endif

#ifdef OKAY_OWN_LWS_DEFAULT
constexpr bool kOwnLwsDefault = true;
#else
constexpr bool kOwnLwsDefault = false;
#endif

#ifdef OKAY_OWN_OGREBIN_DEFAULT
constexpr bool kOwnOgreBinDefault = true;
#else
constexpr bool kOwnOgreBinDefault = false;
#endif

#ifdef OKAY_OWN_ZAE_DEFAULT
constexpr bool kOwnZaeDefault = true;
#else
constexpr bool kOwnZaeDefault = false;
#endif

#ifdef OKAY_OWN_XML_DEFAULT
constexpr bool kOwnXmlDefault = true;
#else
constexpr bool kOwnXmlDefault = false;
#endif

#ifdef OKAY_OWN_LXO_DEFAULT
constexpr bool kOwnLxoDefault = true;
#else
constexpr bool kOwnLxoDefault = false;
#endif

#ifdef OKAY_OWN_OPENGEX_DEFAULT
constexpr bool kOwnOpengexDefault = true;
#else
constexpr bool kOwnOpengexDefault = false;
#endif

#ifdef OKAY_OWN_MDL_DEFAULT
constexpr bool kOwnMdlDefault = true;
#else
constexpr bool kOwnMdlDefault = false;
#endif

#ifdef OKAY_OWN_Q3BSP_DEFAULT
constexpr bool kOwnQ3bspDefault = true;
#else
constexpr bool kOwnQ3bspDefault = false;
#endif

#ifdef OKAY_OWN_GLTF1_DEFAULT
constexpr bool kOwnGltf1Default = true;
#else
constexpr bool kOwnGltf1Default = false;
#endif

#ifdef OKAY_OWN_BLEND_DEFAULT
constexpr bool kOwnBlendDefault = true;
#else
constexpr bool kOwnBlendDefault = false;
#endif

// Opt-in readers (slot 3 and up): the environment variable that switches one, its name in messages and its CMake default.
// A new format adds one line here, one case in OwnSlot and one in the read dispatch of ImportModelData.
struct OptInFormat { const char* var; const char* name; bool cmakeDefault; };
constexpr OptInFormat kOptIn[] = {
    {"OKAY_OWN_COLLADA", "COLLADA", kOwnColladaDefault},   // 3
    {"OKAY_OWN_3DS",     "3DS",     kOwn3dsDefault},       // 4
    {"OKAY_OWN_BVH",     "BVH",     kOwnBvhDefault},       // 5
    {"OKAY_OWN_OFF",     "OFF",     kOwnOffDefault},       // 6
    {"OKAY_OWN_SMD",     "SMD",     kOwnSmdDefault},       // 7
    {"OKAY_OWN_MD5",     "MD5",     kOwnMd5Default},       // 8
    {"OKAY_OWN_MS3D",    "MS3D",    kOwnMs3dDefault},       // 9
    {"OKAY_OWN_ASE",     "ASE",     kOwnAseDefault},       // 10
    {"OKAY_OWN_3MF",     "3MF",     kOwn3mfDefault},       // 11
    {"OKAY_OWN_AMF",     "AMF",     kOwnAmfDefault},       // 12
    {"OKAY_OWN_X3D",     "X3D",     kOwnX3dDefault},       // 13
    {"OKAY_OWN_DXF",     "DXF",     kOwnDxfDefault},       // 14
    {"OKAY_OWN_LWO",     "LWO",     kOwnLwoDefault},       // 15
    {"OKAY_OWN_BLEND",   "Blender", kOwnBlendDefault},     // 16
    {"OKAY_OWN_MD2",     "MD2",     kOwnMd2Default},       // 17
    {"OKAY_OWN_MD3",     "MD3",     kOwnMd3Default},       // 18
    {"OKAY_OWN_IRRMESH", "IRRMESH", kOwnIrrMeshDefault},   // 19
    {"OKAY_OWN_B3D",     "B3D",     kOwnB3dDefault},       // 20
    {"OKAY_OWN_X",       "X",       kOwnXDefault},         // 21
    {"OKAY_OWN_AC3D",    "AC3D",    kOwnAc3dDefault},      // 22
    {"OKAY_OWN_OGRE",    "OGRE",    kOwnOgreDefault},      // 23
    {"OKAY_OWN_XGL",     "XGL",     kOwnXglDefault},       // 24
    {"OKAY_OWN_IRR",     "IRR",     kOwnIrrDefault},       // 25
    {"OKAY_OWN_NFF",     "NFF",     kOwnNffDefault},       // 26
    {"OKAY_OWN_TER",     "Terragen", kOwnTerDefault},      // 27
    {"OKAY_OWN_MDC",     "MDC",     kOwnMdcDefault},       // 28
    {"OKAY_OWN_HMP",     "HMP",     kOwnHmpDefault},       // 29
    {"OKAY_OWN_CSM",     "CSM",     kOwnCsmDefault},       // 30
    // Slot 32 is kept for SIB (no reader yet): an empty row, never reached (OwnSlot returns none of it), so that Q3D and RAW sit at the agreed numbers.
    {"OKAY_OWN_Q3D",     "Q3D",     kOwnQ3dDefault},       // 31
    {"", "", false},                                       // 32 (SIB, not written yet)
    {"OKAY_OWN_RAW",     "RAW",     kOwnRawDefault},       // 33
    {"OKAY_OWN_IQM",     "IQM",     kOwnIqmDefault},       // 34
    {"OKAY_OWN_COB",     "COB",     kOwnCobDefault},       // 35
    {"OKAY_OWN_NDO",     "NDO",     kOwnNdoDefault},       // 36
    {"OKAY_OWN_OPENGEX", "OpenGEX", kOwnOpengexDefault},   // 37 (.ogex)
    {"OKAY_OWN_MDL",     "MDL",     kOwnMdlDefault},       // 38 (.mdl: Quake 1 and Half-Life 1)
    {"OKAY_OWN_PMX",     "PMX",     kOwnPmxDefault},       // 39
    {"OKAY_OWN_LWS",     "LWS",     kOwnLwsDefault},       // 40 (.lws and .mot)
    {"OKAY_OWN_GLTF1",   "glTF 1.0", kOwnGltf1Default},    // 41: glTF 1.0 (pseudo-extension ".gltf1"; .gltf / .glb files of version 1)
    {"OKAY_OWN_Q3BSP",   "Q3BSP",   kOwnQ3bspDefault},     // 42 (.bsp and .pk3: Quake III Arena maps)
    {"OKAY_OWN_OGREBIN", "OGRE binary", kOwnOgreBinDefault},   // 43 (.mesh)
    {"OKAY_OWN_ZAE",     "ZAE",     kOwnZaeDefault},       // 44 (.zae)
    {"OKAY_OWN_XML",     "XML",     kOwnXmlDefault},       // 45 (.xml: the reader is picked by the root element)
    {"OKAY_OWN_LXO",     "LXO",     kOwnLxoDefault},       // 46 (.lxo: Modo, read by the LWO reader)
};
constexpr int kOwnSlots = 47;   // slots 22 to 26 are batch D, 27 is Terragen's, 28 to 30 are MDC, HMP, CSM, 31 and 33 are Q3D and RAW (32 is kept for SIB), 34 to 36 are IQM, COB, NDO (batch E3), 37 and 38 are OpenGEX and MDL, 39 and 40 are PMX and LWS/MOT (batch E5), 41 is glTF 1.0 (the pseudo-extension ".gltf1"), 42 is Q3BSP (.bsp, .pk3), 43 to 45 are OGRE binary, ZAE and .xml (the gap batch), 46 is LXO; raise this (and add rows above) when a reader needs a slot beyond 46

// 0 = .obj, 1 = .stl, 2 = .ply, 3 = .dae, 4 = .3ds, 5 = .bvh, 6 = .off; -1 = no own reader.
int OwnSlot(const std::string& ext) {
    const std::string e = Lower(ext);
    if (e == ".obj") return 0;
    if (e == ".stl") return 1;
    if (e == ".ply") return 2;
    if (e == ".dae") return 3;
    if (e == ".3ds") return 4;
    if (e == ".bvh") return 5;
    if (e == ".off") return 6;
    if (e == ".smd") return 7;
    if (e == ".md5mesh" || e == ".md5anim") return 8;
    if (e == ".ms3d") return 9;
    if (e == ".ase") return 10;
    if (e == ".3mf") return 11;
    if (e == ".amf") return 12;
    if (e == ".x3d") return 13;
    if (e == ".dxf") return 14;
    if (e == ".lwo") return 15;
    if (e == ".blend") return 16;
    if (e == ".md2") return 17;
    if (e == ".md3") return 18;
    if (e == ".irrmesh") return 19;
    if (e == ".b3d") return 20;
    if (e == ".x") return 21;
    if (e == ".ac" || e == ".acc" || e == ".ac3d") return 22;
    if (e == ".mesh.xml") return 23;
    if (e == ".xgl" || e == ".zgl") return 24;
    if (e == ".irr") return 25;
    if (e == ".nff" || e == ".enff") return 26;
    if (e == ".ter") return 27;
    if (e == ".mdc") return 28;
    if (e == ".hmp") return 29;
    if (e == ".csm") return 30;
    if (e == ".q3o" || e == ".q3s") return 31;
    if (e == ".raw") return 33;
    if (e == ".iqm") return 34;
    if (e == ".cob") return 35;
    if (e == ".ndo") return 36;
    if (e == ".ogex") return 37;
    if (e == ".mdl") return 38;
    if (e == ".pmx") return 39;
    if (e == ".lws" || e == ".mot") return 40;
    if (e == ".mesh") return 43;
    if (e == ".zae") return 44;
    if (e == ".xml") return 45;
    if (e == ".lxo") return 46;
    if (e == ".bsp" || e == ".pk3") return 42;
    if (e == ".gltf1") return 41;   // not a file extension: the switch for glTF 1.0 files (.gltf / .glb share 2.0's extensions)
    return -1;
}
std::atomic<int> g_backendChoice[kOwnSlots] = {};   // ImportBackend per own-reader format (0 = Default)

// Whether our reader is the one for a format (slot 3 and up). Order: SetImportBackend in code (checked by the caller), then
// OKAY_THIRD_PARTY=1 (every format back to Assimp; okay/Core/ThirdParty.hpp), then OKAY_OWN_<FMT>=0/1, then the CMake option
// (ON for every reader since the owner's approval of 2026-11-17, except the NDO seam, which refuses every file).
bool OwnOptedIn(int slot) {
    if (slot < 3 || slot - 3 >= (int)(sizeof(kOptIn) / sizeof(kOptIn[0]))) return false;
    if (ThirdPartyForced()) return false;
    bool v = kOptIn[slot - 3].cmakeDefault;
    if (const char* e = std::getenv(kOptIn[slot - 3].var)) { if (e[0] == '1') v = true; else if (e[0] == '0') v = false; }
    return v;
}

std::string ExtensionOf(const std::string& path) {
    {   // OGRE's XML meshes are ".mesh.xml": the extension is the last two parts, not the last one
        const std::string l = Lower(path);
        if (EndsWith(l, ".mesh.xml")) return ".mesh.xml";
    }
    std::size_t sl = path.find_last_of("/\\"), dot = path.find_last_of('.');
    if (dot == std::string::npos || (sl != std::string::npos && dot < sl)) return std::string();
    return Lower(path.substr(dot));
}

bool AssimpReadsExt(const std::string& ext) {
#ifdef OKAY_HAVE_ASSIMP
    return (" " + assimp_backend::Extensions() + " ").find(" " + Lower(ext) + " ") != std::string::npos;
#else
    (void)ext; return false;
#endif
}

// The one-static-mesh mode for the readers added after Collada / 3DS: every node's transform (the root's Z-up to Y-up
// turn included) is baked into the vertices and normals of the parts on it, skins and clips are dropped and one root
// node is left, as Assimp's pre-transform step does. (A skinned part at its bind pose is the mesh seen through its
// root, so the baked result is the bind pose.)
void FlattenImportedModel(ImportedModel& m) {
    std::vector<Mat4> world(m.nodes.size());
    for (std::size_t i = 0; i < m.nodes.size(); ++i) {
        const ImportedNode& n = m.nodes[i];
        const Mat4 l = Mat4::TRS(n.position, n.rotation, n.scale);
        world[i] = (n.parent >= 0 && (std::size_t)n.parent < i) ? world[(std::size_t)n.parent] * l : l;
    }
    for (ImportedPart& p : m.parts) {
        const Mat4 w = (p.node >= 0 && (std::size_t)p.node < world.size()) ? world[(std::size_t)p.node] : Mat4::Identity();
        const Mat4 inv = w.Inverse();
        for (Vec3& v : p.mesh.vertices) v = w.MultiplyPoint(v);
        for (Vec3& n : p.mesh.normals) {   // normals go through the inverse transpose
            const Vec3 t{inv.at(0, 0) * n.x + inv.at(0, 1) * n.y + inv.at(0, 2) * n.z,
                         inv.at(1, 0) * n.x + inv.at(1, 1) * n.y + inv.at(1, 2) * n.z,
                         inv.at(2, 0) * n.x + inv.at(2, 1) * n.y + inv.at(2, 2) * n.z};
            n = t.Normalized();
        }
        const float det = w.at(0, 0) * (w.at(1, 1) * w.at(2, 2) - w.at(2, 1) * w.at(1, 2)) - w.at(1, 0) * (w.at(0, 1) * w.at(2, 2) - w.at(2, 1) * w.at(0, 2)) +
                          w.at(2, 0) * (w.at(0, 1) * w.at(1, 2) - w.at(1, 1) * w.at(0, 2));
        if (det < 0.0f) for (std::size_t i = 0; i + 2 < p.mesh.triangles.size(); i += 3) std::swap(p.mesh.triangles[i + 1], p.mesh.triangles[i + 2]);   // a mirror turns the winding
        p.bones.clear(); p.jointIdx.clear(); p.jointWt.clear();
        p.node = 0;
    }
    ImportedNode root;
    root.name = m.nodes.empty() ? std::string("root") : m.nodes[0].name;
    m.nodes.assign(1, root);
    m.clips.clear();
}

} // namespace

bool HasOwnReader(const std::string& ext) { return OwnSlot(ext) >= 0; }

bool SetImportBackend(const std::string& ext, ImportBackend which) {
    if (ext == "*") { for (auto& c : g_backendChoice) c = (int)which; return true; }
    int s = OwnSlot(ext);
    if (s < 0) return false;
    g_backendChoice[s] = (int)which;
    return true;
}

ImportBackend EffectiveImportBackend(const std::string& ext) {
    const int s = OwnSlot(ext);
    if (s < 0) return AssimpReadsExt(ext) ? ImportBackend::Assimp : ImportBackend::Default;
    ImportBackend c = (ImportBackend)g_backendChoice[s].load();
    if (c == ImportBackend::Own) return ImportBackend::Own;
    if (c == ImportBackend::Assimp) return AssimpAvailable() ? ImportBackend::Assimp : ImportBackend::Own;
    // Default: OBJ has always been our own reader. STL / PLY go to Assimp when the
    // build has it (as before our readers existed), unless CMake asked for ours first.
    if (s >= 3) {   // the readers from Collada on: ours unless switched off (OKAY_OWN_<FMT>=0 or OKAY_THIRD_PARTY=1); then Assimp
        if (OwnOptedIn(s)) return ImportBackend::Own;
        if (AssimpAvailable()) return ImportBackend::Assimp;
        // OKAY_THIRD_PARTY=1 in a build without Assimp: nothing else can read the file, so ours still does. A single format
        // switched off with OKAY_OWN_<FMT>=0 stays off (the import then says how to switch it on).
        if (ThirdPartyForced() && kOptIn[s - 3].cmakeDefault) return ImportBackend::Own;
        return ImportBackend::Default;
    }
    if (s == 0 || !AssimpAvailable()) return ImportBackend::Own;
    // STL / PLY: ours by default (OKAY_PREFER_OWN_READERS, ON since 2026-11-17); OKAY_OWN_STL / OKAY_OWN_PLY = 0/1 and
    // OKAY_THIRD_PARTY=1 change it at run time.
    if (ThirdPartyForced()) return ImportBackend::Assimp;
    bool own = kPreferOwnDefault;
    if (const char* e = std::getenv(s == 1 ? "OKAY_OWN_STL" : "OKAY_OWN_PLY")) { if (e[0] == '1') own = true; else if (e[0] == '0') own = false; }
    return own ? ImportBackend::Own : ImportBackend::Assimp;
}

bool RunReaderGuarded(const std::function<bool()>& read, std::string& err) {
    try {
        return read();
    } catch (const std::bad_alloc&) {
        err = "the file needs more memory than this machine can give (refused)";
    } catch (const std::length_error&) {
        err = "the file asks for a larger buffer than is allowed (refused)";
    } catch (const std::exception& e) {
        err = std::string("the reader failed with an error (refused): ") + e.what();
    } catch (...) {
        err = "the reader failed with an unknown error (refused)";
    }
    return false;
}

// .xml (slot 45): several formats Assimp reads under this extension, told apart by the root element. Only the first 8 KiB are
// looked at (UTF-16 text with its zero bytes dropped): the root element, and for <mesh> its first child element (<sharedgeometry>,
// <submeshes> or <submesh> is OGRE XML, anything else an Irrlicht mesh). The file is then read by that format's own reader.
static bool ReadXmlByRoot(const std::string& path, ImportedModel& out, std::string* err, bool flatten, const IrrMeshLoader* loader) {
    char buf[8192];
    std::size_t n = 0;
    {
        std::FILE* fp = detail::OpenRead(path);
        if (!fp) { if (err) *err = "cannot open file"; return false; }
        n = std::fread(buf, 1, sizeof(buf), fp);
        std::fclose(fp);
    }
    if (std::memchr(buf, 0, n)) n = (std::size_t)(std::remove(buf, buf + n, '\0') - buf);   // UTF-16: the ASCII names survive
    // the next element name at or after `i` (the XML declaration, comments, a DOCTYPE and closing tags skipped); `i` ends after the name
    auto nextElement = [&](std::size_t& i) -> std::string {
        while (i + 1 < n) {
            if (buf[i] != '<') { ++i; continue; }
            if (i + 3 < n && buf[i + 1] == '!' && buf[i + 2] == '-' && buf[i + 3] == '-') {
                std::size_t k = i + 4;
                while (k + 2 < n && !(buf[k] == '-' && buf[k + 1] == '-' && buf[k + 2] == '>')) ++k;
                i = k + 3; continue;
            }
            if (buf[i + 1] == '?' || buf[i + 1] == '!' || buf[i + 1] == '/') { ++i; continue; }
            std::size_t e = i + 1;
            while (e < n && (std::isalnum((unsigned char)buf[e]) || buf[e] == '_' || buf[e] == '-' || buf[e] == ':' || buf[e] == '.')) ++e;
            std::string nm(buf + i + 1, e - i - 1);
            i = e;
            const std::size_t colon = nm.find(':');
            return Lower(colon == std::string::npos ? nm : nm.substr(colon + 1));
        }
        i = n;
        return std::string();
    };
    std::size_t at = 0;
    const std::string r = nextElement(at);
    if (r == "irr_scene") return ReadIrrSceneFile(path, out, err, loader);
    if (r == "collada") return ReadColladaFile(path, out, err, flatten);
    if (r == "world") return ReadXglFile(path, out, err);
    if (r == "mesh") {
        const std::string kid = nextElement(at);
        if (kid == "sharedgeometry" || kid == "submeshes" || kid == "submesh") return ReadOgreXmlFile(path, out, err);
        return ReadIrrMeshFile(path, out, err);
    }
    if (err) *err = r.empty() ? "XML: no root element found" : "XML: the root element <" + r.substr(0, 60) + "> is not one this engine reads (irr_scene, mesh, COLLADA, WORLD)";
    return false;
}

static std::string OwnReaderName(int slot) {
    if (slot == 0) return "the own OBJ reader";
    if (slot == 1) return "the own STL reader";
    if (slot == 2) return "the own PLY reader";
    const int n = (int)(sizeof(kOptIn) / sizeof(kOptIn[0]));
    if (slot >= 3 && slot - 3 < n && kOptIn[slot - 3].name[0]) return std::string("the own ") + kOptIn[slot - 3].name + " reader";
    return "an own reader";
}

static bool ImportModelDataImpl(const std::string& path, ImportedModel& out, ImportBackend which, bool flatten, std::string* err);

bool ImportModelData(const std::string& path, ImportedModel& out, ImportBackend which, bool flatten, std::string* err) {
    // The outermost call (an Irrlicht or LightWave scene reads its meshes through this function too) records which
    // reader read the file and how long it took, for the one log line of ImportModelScene.
    thread_local int depth = 0;
    const auto t0 = std::chrono::steady_clock::now();
    ++depth;
    bool r = false;
    try { r = ImportModelDataImpl(path, out, which, flatten, err); } catch (...) { --depth; throw; }
    --depth;
    if (depth == 0) {
        g_readMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (!r) g_readBy.clear();
    }
    return r;
}

static bool ImportModelDataImpl(const std::string& path, ImportedModel& out, ImportBackend which, bool flatten, std::string* err) {
    out = ImportedModel{};
    // `flatten` matters to Assimp and to our Collada reader (the others have no node transforms).
    const std::string ext = ExtensionOf(path);
    // glTF 1.0 (.gltf / .glb, version 1): our own reader when it is switched on (glTF 2.0 files are read by Gltf.hpp and never
    // come through here; with the switch off a glTF 1.0 file is refused by the glTF import, Assimp is never asked).
    // An explicit ImportBackend::Own asks for our reader whatever the switch says (as for the other formats).
    std::string probeWhy;
    if ((ext == ".gltf" || ext == ".glb") && (which == ImportBackend::Own ? RunReaderGuarded([&]() -> bool { return GltfFileIsVersion1(path); }, probeWhy)
                                                                         : which == ImportBackend::Default && Gltf1Wanted(path))) {
        std::string e1;
        const bool good = RunReaderGuarded([&]() -> bool { return ReadGltf1File(path, out, &e1); }, e1);
        if (!good) { out = ImportedModel{}; if (err) *err = e1; return false; }
        if (flatten) FlattenImportedModel(out);
        for (const std::string& n : out.notes) Log::Warning("model import '", path, "': ", n);
        g_readBy = "the own glTF 1.0 reader";
        return true;
    }
    // Our reader rejects a second `element vertex`. Assimp still loads that file,
    // and the default choice would hand it to Assimp after our reader said no.
    // The two blocks then contribute normals and UVs that do not line up with
    // positions. Refuse the file before either backend runs.
    if (ext == ".ply" && PlyFileDeclaresDuplicateVertex(path)) {
        if (err) *err = "PLY declares more than one vertex element";
        return false;
    }
    ImportBackend use = which == ImportBackend::Default ? EffectiveImportBackend(ext) : which;
    // Assimp 5.4.3's RAW importer leaves null child pointers in the scene when a file has two or more groups of triangles, and the
    // import crashed on them (RawFileUnsafeForAssimp, RawReader.cpp). Such a file is never handed to Assimp: with the default
    // choice (or the per-format switch set to Assimp) our RAW reader reads it, and an explicit request for Assimp is refused.
    bool assimpUnsafe = false;
#ifdef OKAY_HAVE_ASSIMP
    if (use == ImportBackend::Assimp && ext == ".raw" && RawFileUnsafeForAssimp(path)) {
        assimpUnsafe = true;
        if (which == ImportBackend::Assimp) {
            if (err) *err = "RAW: this file has more than one group of triangles, which crashes Assimp 5.4.3's RAW importer; read it with our own RAW reader (OKAY_OWN_RAW=1)";
            return false;
        }
        Log::Warning("model import '", path, "': more than one group of triangles, which Assimp 5.4.3's RAW importer cannot read safely; reading it with our own RAW reader");
        use = ImportBackend::Own;
    }
#endif
    if (use == ImportBackend::Assimp) {
#ifdef OKAY_HAVE_ASSIMP
        g_readBy = "Assimp";
        return assimp_backend::Read(path, flatten, out, err);
#else
        if (err) *err = "this build was compiled without Assimp";
        return false;
#endif
    }
    if (use == ImportBackend::Own) {
        const int s = OwnSlot(ext);
        if (s < 0) { if (err) *err = "no built-in reader for this format"; return false; }
        std::string e1;
        // the mesh files an Irrlicht scene names are read through this same seam, with the default choice of backend (our own
        // reader where it is switched on, else Assimp when the build has it); one level only, so a scene cannot name a scene
        thread_local int loaderDepth = 0;
        const IrrMeshLoader irrLoader = [&](const std::string& p, ImportedModel& m, std::string* e) {
            if (loaderDepth >= 1 || EndsWith(Lower(p), ".irr") || EndsWith(Lower(p), ".lws") || EndsWith(Lower(p), ".mot")) { if (e) *e = "a scene file may not load another scene"; return false; }
            ++loaderDepth;
            const bool r = ImportModelData(p, m, ImportBackend::Default, false, e);
            --loaderDepth;
            return r;
        };
        bool good = RunReaderGuarded([&]() -> bool { return s == 0 ? ReadObjFile(path, out, &e1) : s == 1 ? ReadStlFile(path, out, &e1) : s == 2 ? ReadPlyFile(path, out, &e1)
                  : s == 3 ? ReadColladaFile(path, out, &e1, flatten) : s == 4 ? Read3dsFile(path, out, &e1, flatten)
                  : s == 5 ? ReadBvhFile(path, out, &e1)
                  : s == 6 ? ReadOffFile(path, out, &e1)
                  : s == 7 ? ReadSmdFile(path, out, &e1)
                  : s == 8 ? ReadMd5File(path, out, &e1)
                  : s == 9 ? ReadMs3dFile(path, out, &e1)
                  : s == 10 ? ReadAseFile(path, out, &e1)
                  : s == 11 ? Read3mfFile(path, out, &e1, flatten)
                  : s == 12 ? ReadAmfFile(path, out, &e1, flatten)
                  : s == 13 ? ReadX3dFile(path, out, &e1, flatten)
                  : s == 14 ? ReadDxfFile(path, out, &e1, flatten)
                  : s == 15 ? ReadLwoFile(path, out, &e1)
                  : s == 16 ? ReadBlendFile(path, out, &e1, flatten)   // the Blender reader flattens by itself
                  : s == 17 ? ReadMd2File(path, out, &e1)
                  : s == 18 ? ReadMd3File(path, out, &e1)
                  : s == 19 ? ReadIrrMeshFile(path, out, &e1)
                  : s == 20 ? ReadB3dFile(path, out, &e1)
                  : s == 21 ? ReadXFile(path, out, &e1)
                  : s == 22 ? ReadAc3dFile(path, out, &e1)
                  : s == 23 ? ReadOgreXmlFile(path, out, &e1)
                  : s == 24 ? ReadXglFile(path, out, &e1)
                  : s == 25 ? ReadIrrSceneFile(path, out, &e1, &irrLoader)
                  : s == 26 ? ReadNffFile(path, out, &e1)
                  : s == 27 ? ReadTerragenFile(path, out, &e1)
                  : s == 28 ? ReadMdcFile(path, out, &e1)
                  : s == 29 ? ReadHmpFile(path, out, &e1)
                  : s == 30 ? ReadCsmFile(path, out, &e1)
                  : s == 31 ? ReadQ3dFile(path, out, &e1)
                  : s == 33 ? ReadRawFile(path, out, &e1)
                  : s == 34 ? ReadIqmFile(path, out, &e1)
                  : s == 35 ? ReadCobFile(path, out, &e1)
                  : s == 36 ? ReadNdoFile(path, out, &e1)
                  : s == 37 ? ReadOgexFile(path, out, &e1)
                  : s == 38 ? ReadMdlFile(path, out, &e1)
                  : s == 39 ? ReadPmxFile(path, out, &e1)
                  : s == 40 ? ReadLwsFile(path, out, &e1, &irrLoader)
                  : s == 43 ? ReadOgreBinaryFile(path, out, &e1)
                  : s == 44 ? ReadZaeFile(path, out, &e1, flatten)
                  : s == 45 ? ReadXmlByRoot(path, out, &e1, flatten, &irrLoader)
                  : s == 46 ? ReadLwoFile(path, out, &e1)   // LXOB is read by the LWO reader (LWO2 geometry chunks)
                  : s == 41 ? ReadGltf1File(path, out, &e1)
                  : s == 42 ? ReadQ3bspFile(path, out, &e1)   // a file named *.gltf1: read it as glTF 1.0
                  : (e1 = "no reader is wired to this slot", false); }, e1);   // every slot but 32 (kept for SIB, which has no reader of ours) is wired above
        if (!good) out = ImportedModel{};   // a reader that threw may have left half a model
        if (good && flatten && ((s >= 6 && s <= 10) || (s >= 17 && s != 44))) FlattenImportedModel(out);   // ZAE (44) is flattened by the COLLADA reader   // batch A (OFF..ASE) and batch C (slot 17 and up) have no flatten switch of their own; batch B and the Blender reader flatten themselves
        if (good) {
            for (const std::string& n : out.notes) Log::Warning("model import '", path, "': ", n);
            g_readBy = OwnReaderName(s);
            return true;
        }
        // Our reader said no. With the default choice, give Assimp a go when the build has it.
        if (which == ImportBackend::Default && AssimpAvailable() && !assimpUnsafe) {
#ifdef OKAY_HAVE_ASSIMP
            if (assimp_backend::Read(path, flatten, out, nullptr)) { g_readBy = "Assimp (" + OwnReaderName(s) + " refused it: " + e1.substr(0, 200) + ")"; return true; }
#endif
        }
        out = ImportedModel{};
        if (err) *err = e1;
        return false;
    }
    if (err) {
        const std::string x = ExtensionOf(path);
        const int xs = OwnSlot(x);
        const bool optIn = xs >= 3 && xs - 3 < (int)(sizeof(kOptIn) / sizeof(kOptIn[0]));
        const char* nm = optIn && kOptIn[xs - 3].name[0] ? kOptIn[xs - 3].name : nullptr;
        const char* var = optIn ? kOptIn[xs - 3].var : "";
        if (nm)
            *err = std::string("no ") + nm + " reader enabled: our own reader is switched off (" + var + "=0, or OKAY_THIRD_PARTY=1, or a build with -D" + var +
                   "=OFF; set " + var + "=1, build with -D" + var +
                   "=ON, or call SetImportBackend(\"" + x + "\", ImportBackend::Own)); the Assimp one needs -DOKAY_USE_ASSIMP=ON";
        else *err = "no reader for this format in this build";
    }
    return false;
}

Mesh MergeModelMeshes(const ImportedModel& m, std::string* outTexture) {
    Mesh out;
    for (const ImportedPart& p : m.parts) {
        const int base = (int)out.vertices.size();
        out.vertices.insert(out.vertices.end(), p.mesh.vertices.begin(), p.mesh.vertices.end());
        out.normals.insert(out.normals.end(), p.mesh.normals.begin(), p.mesh.normals.end());
        out.uvs.insert(out.uvs.end(), p.mesh.uvs.begin(), p.mesh.uvs.end());
        for (int t : p.mesh.triangles) out.triangles.push_back(base + t);
    }
    if (out.normals.size() != out.vertices.size()) out.normals.clear();
    if (out.uvs.size()     != out.vertices.size()) out.uvs.clear();
    if (outTexture)
        for (const ImportedMaterial& mat : m.materials)
            if (!mat.texture.empty()) { *outTexture = mat.texture; break; }
    return out;
}

namespace {

// Pull the WHOLE material onto a MeshRenderer — albedo, normal, specular/gloss
// and AO maps, plus diffuse/emissive colors and shininess/metallic/roughness
// factors — so a downloaded model lights like it does in other engines instead
// of arriving as flat white. Mirrors ApplyGltfMaterial.
void ApplyImportedMaterial(const ImportedModel& m, int mi, MeshRenderer* mr) {
    if (mi < 0 || mi >= (int)m.materials.size() || !mr) return;
    const ImportedMaterial& mat = m.materials[(std::size_t)mi];
    if (!mat.texture.empty())     mr->texture = mat.texture;
    if (!mat.normalMap.empty())   mr->normalMap = mat.normalMap;
    if (!mat.specularMap.empty()) mr->specularMap = mat.specularMap;
    if (!mat.aoMap.empty())       mr->aoMap = mat.aoMap;
    // The diffuse COLOR only applies when there's no albedo texture (textured
    // materials often carry a black/grey diffuse that would tint the texture to mud).
    if (mr->texture.empty() && mat.hasDiffuse && mat.diffuse.r + mat.diffuse.g + mat.diffuse.b > 0.02f)
        mr->color = Color(mat.diffuse.r, mat.diffuse.g, mat.diffuse.b, mr->color.a);
    if (mat.hasEmissive && mat.emissive.r + mat.emissive.g + mat.emissive.b > 0.02f)
        mr->emissive = Color(mat.emissive.r, mat.emissive.g, mat.emissive.b, 1.0f);
    if (mat.shininess > 1.0f) {
        mr->shininess = mat.shininess > 128.0f ? 128.0f : mat.shininess;
        if (mr->specular <= 0.0f) mr->specular = 0.4f;
    }
    if (mat.metallic > 0.0f) mr->metallic = mat.metallic > 1.0f ? 1.0f : mat.metallic;
    if (mat.roughness >= 0.0f) {
        float smooth = 1.0f - (mat.roughness > 1.0f ? 1.0f : mat.roughness);   // same mapping as glTF
        mr->shininess = 4.0f + smooth * 124.0f;
        mr->specular  = 0.15f + smooth * 0.85f;
    }
}

} // namespace

GameObject* BuildSceneFromModel(Scene& scene, const ImportedModel& model, const std::string& path) {
    // A model with nodes but no mesh (a BVH skeleton, a COLLADA file of cameras and lights only) imports as its node
    // hierarchy; a model with neither nodes nor parts is nothing to build.
    if (model.parts.empty() && model.nodes.empty()) return nullptr;
    GameObject* root = scene.CreateGameObject(BaseName(path));

    if (model.nodes.empty()) {
        // Flat result (our own OBJ / STL / PLY readers): the parts hang straight off
        // the root. Exporters disagree on units (Meshy etc. emit cm/mm), so scale all
        // parts together to a usable size — the same rule as Mesh::NormalizeImportScale.
        std::vector<Mesh> meshes;
        for (const ImportedPart& p : model.parts) meshes.push_back(p.mesh);
        {
            Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
            bool any = false;
            for (const Mesh& mm : meshes) {
                if (mm.vertices.empty()) continue;
                Vec3 a, b; mm.Bounds(a, b);
                lo = Vec3{std::fmin(lo.x, a.x), std::fmin(lo.y, a.y), std::fmin(lo.z, a.z)};
                hi = Vec3{std::fmax(hi.x, b.x), std::fmax(hi.y, b.y), std::fmax(hi.z, b.z)};
                any = true;
            }
            if (any) {
                Vec3 sz = hi - lo;
                float d = sz.x > sz.y ? (sz.x > sz.z ? sz.x : sz.z) : (sz.y > sz.z ? sz.y : sz.z);
                if (d > 0.0f && (d > 20.0f || d < 0.02f)) {
                    float s = 2.0f / d;
                    for (Mesh& mm : meshes) for (Vec3& v : mm.vertices) v = v * s;
                }
            }
        }
        int made = 0;
        for (std::size_t i = 0; i < model.parts.size(); ++i) {
            if (meshes[i].vertices.empty()) continue;
            GameObject* host = root;
            if (model.parts.size() > 1) {
                std::string nm = meshes[i].name.empty() ? "part" + std::to_string(i) : meshes[i].name;
                host = scene.CreateGameObject(nm);
                host->transform->SetParent(root->transform, false);
            }
            auto* mr = host->AddComponent<MeshRenderer>();
            mr->mesh = meshes[i]; mr->doubleSided = true;
            ApplyImportedMaterial(model, model.parts[i].material, mr);
            ++made;
        }
        if (made == 0) { scene.Destroy(root); return nullptr; }
        AutoNormalizeImportScale(scene, root);
        return root;
    }

    std::unordered_map<std::string, GameObject*> byName;
    struct MeshSite { GameObject* go; const ImportedPart* part; };
    std::vector<MeshSite> sites;
    std::vector<GameObject*> goOf(model.nodes.size(), nullptr);
    std::vector<int> partsOnNode(model.nodes.size(), 0);
    std::unordered_map<std::string, int> nameUse;   // later duplicates get a suffix (first keeps the name bones/channels use)
    for (std::size_t i = 0; i < model.nodes.size(); ++i) {
        const ImportedNode& n = model.nodes[i];
        std::string objName = n.name.empty() ? "node" : n.name;
        if (int c = nameUse[objName]++) objName += "_" + std::to_string(c);
        GameObject* g = scene.CreateGameObject(objName);
        g->transform->localPosition = n.position;
        g->transform->localRotation = n.rotation;
        g->transform->localScale    = n.scale;
        Transform* parent = (n.parent >= 0 && n.parent < (int)i && goOf[(std::size_t)n.parent]) ? goOf[(std::size_t)n.parent]->transform : root->transform;
        g->transform->SetParent(parent, false);
        goOf[i] = g;
        if (!n.name.empty() && !byName.count(n.name)) byName[n.name] = g;
        for (const ImportedPart& part : model.parts) {
            if (part.node != (int)i) continue;
            GameObject* host = g;
            int k = partsOnNode[i]++;
            if (k > 0) {   // extra meshes on this node get child objects
                host = scene.CreateGameObject(g->name + "_m" + std::to_string(k));
                host->transform->SetParent(g->transform, false);
            }
            sites.push_back({host, &part});
        }
    }

    for (const MeshSite& site : sites) {
        const ImportedPart& part = *site.part;
        if (part.mesh.vertices.empty()) continue;
        auto* mr = site.go->AddComponent<MeshRenderer>();
        mr->mesh = part.mesh; mr->doubleSided = true;
        ApplyImportedMaterial(model, part.material, mr);   // full material, not just albedo

        if (part.bones.empty()) continue;
        auto* sm = site.go->AddComponent<SkinnedMesh>();
        sm->bind = part.mesh;
        sm->jointIdx = part.jointIdx;
        sm->jointWt = part.jointWt;
        sm->jointIdx.resize(part.mesh.vertices.size(), std::array<int, 4>{0, 0, 0, 0});
        sm->jointWt.resize(part.mesh.vertices.size(), std::array<float, 4>{0, 0, 0, 0});
        for (const ImportedBone& b : part.bones) {
            auto it = byName.find(b.name);
            sm->joints.push_back(it != byName.end() ? it->second->transform : nullptr);
            sm->jointNames.push_back(b.name);
            sm->inverseBind.push_back(b.inverseBind);
        }
        ValidateSkinJoints(*sm, path);
    }

    // Animations -> a ModelAnimator clip library on the root (same as the glTF path).
    if (!model.clips.empty()) {
        auto* ma = root->AddComponent<ModelAnimator>();
        for (const ImportedClip& ic : model.clips) {
            ModelAnimator::Clip clip;
            clip.name = ic.name;
            for (const ImportedChannel& ch : ic.channels) {
                if (ch.node.empty() || !byName.count(ch.node)) continue;
                clip.nodes.push_back({ch.node, AnimationClip{}});
                AnimationClip& ac = clip.nodes.back().clip;
                ac.smoothKeys = false;   // sampled data: play back linearly, as authored
                for (const ImportedKeyV3& k : ch.position) {
                    ac.AddKey("position.x", k.time, k.value.x);
                    ac.AddKey("position.y", k.time, k.value.y);
                    ac.AddKey("position.z", k.time, k.value.z);
                }
                // Keep quaternion keys on one hemisphere: the Animator lerps the raw
                // components, and a sign flip between neighbours reads as a wild spin.
                Quat prev; bool hasPrev = false;
                for (const ImportedKeyQ& k : ch.rotation) {
                    Quat q = k.value;
                    if (hasPrev && (q.x*prev.x + q.y*prev.y + q.z*prev.z + q.w*prev.w) < 0.0f) {
                        q.x = -q.x; q.y = -q.y; q.z = -q.z; q.w = -q.w;
                    }
                    prev = q; hasPrev = true;
                    ac.AddKey("rotation.qx", k.time, q.x);
                    ac.AddKey("rotation.qy", k.time, q.y);
                    ac.AddKey("rotation.qz", k.time, q.z);
                    ac.AddKey("rotation.qw", k.time, q.w);
                }
                for (const ImportedKeyV3& k : ch.scale) {
                    ac.AddKey("scale.x", k.time, k.value.x);
                    ac.AddKey("scale.y", k.time, k.value.y);
                    ac.AddKey("scale.z", k.time, k.value.z);
                }
                if (ac.Tracks().empty()) clip.nodes.pop_back();
            }
            if (!clip.nodes.empty()) ma->clips.push_back(std::move(clip));
        }
        if (!ma->clips.empty()) { ma->active = 0; ma->autoPlay = true; }
    }

    NormalizeImportedClips(root);
    AutoNormalizeImportScale(scene, root);
    return root;
}

// ============================================================================
// FBX: read only by OUR reader (okay::fbx, Render/FbxReader; the vendored ufbx it replaced was deleted on 2026-11-23,
// CLAUDE.md rule 17). A file it refuses fails with a plain message: there is no other FBX reader to fall back to.
// ============================================================================
namespace {   // helpers of our own reader's import below
struct FbxVert {
    float px, py, pz, nx, ny, nz, u, v;
    int   j[4];
    float w[4];
    uint32_t vi;   ///< control point (for blend shape offsets)
};
Mesh TransformedMesh(Mesh m, const Mat4& t) {
    for (auto& v : m.vertices) v = t.MultiplyPoint(v);
    for (auto& n : m.normals) { n = t.MultiplyVector(n); const float l = n.Magnitude(); if (l > 1e-8f) n = n * (1.0f / l); }
    return m;
}
} // namespace
static Mesh ImportFbxMerged(const std::string& path, bool* ok, std::string* outTexture) {
    bool ownOk = false; std::string err;
    Mesh m = ImportFbxMergedOwn(path, &ownOk, outTexture, &err);
    if (ok) *ok = ownOk;
    if (!ownOk) Log::Error("model import '", path, "': ", err.empty() ? std::string("the own FBX reader could not read this file (there is no other FBX reader)") : err);
    return m;
}
static GameObject* ImportFbxSceneGraph(Scene& scene, const std::string& path, bool* ok, std::string* err, bool* rejected) {
    if (GameObject* r = ImportFbxSceneGraphOwn(scene, path, ok, err)) { g_readBy = "the own FBX reader"; return r; }
    if (err && err->empty()) *err = "the own FBX reader could not read this file (there is no other FBX reader)";
    if (rejected) *rejected = true;   // fail the import: nothing else can read an FBX here
    if (ok) *ok = false;
    return nullptr;
}

// ============================================================================
// FBX via OUR reader (okay::fbx, Render/FbxReader): the hierarchy, per-material
// meshes, skins, blend shapes and baked clips the rest of the engine uses.
// ============================================================================
namespace {

Mat4 OwnToMat4(const fbx::Mat& a) {
    Mat4 r = Mat4::Identity();
    for (int row = 0; row < 4; ++row) for (int col = 0; col < 4; ++col) r.at(col, row) = (float)a.m[row * 4 + col];
    return r;
}

std::string OwnTexturePath(const fbx::Texture* tex, const std::string& modelPath, int serial) {
    if (!tex) return {};
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = fs::u8path(modelPath).parent_path();
    for (const std::string* s : {&tex->fileName, &tex->relativeFileName}) {
        if (s->empty()) continue;
        const fs::path p = fs::u8path(*s);
        if (fs::exists(p, ec)) return p.u8string();
        if (fs::exists(dir / p, ec)) return (dir / p).u8string();
        // Windows paths in the file ("C:\art\x.png") on a POSIX host: use the part after the last separator.
        std::string base = *s; const std::size_t sl = base.find_last_of("/\\"); if (sl != std::string::npos) base = base.substr(sl + 1);
        if (!base.empty()) {
            if (fs::exists(dir / fs::u8path(base), ec)) return (dir / fs::u8path(base)).u8string();
            if (fs::exists(dir / "textures" / fs::u8path(base), ec)) return (dir / "textures" / fs::u8path(base)).u8string();
        }
    }
    if (!tex->content.empty()) {
        std::string ext = fs::path(tex->fileName).extension().string();
        if (ext.empty()) ext = (tex->content.size() > 2 && tex->content[0] == 0xFF && tex->content[1] == 0xD8) ? ".jpg" : ".png";
        fs::path out = dir / fs::u8path(modelPath).stem(); out += "_tex" + std::to_string(serial) + ext;
        if (!fs::exists(out, ec)) {
            std::ofstream of(out, std::ios::binary);
            if (!of) return {};
            of.write((const char*)tex->content.data(), (std::streamsize)tex->content.size());
        }
        return out.u8string();
    }
    return {};
}

void ApplyOwnMaterial(MeshRenderer& mr, const fbx::Scene& sc, int matIdx, const std::string& modelPath, int serial) {
    if (matIdx < 0 || matIdx >= (int)sc.materials.size()) return;
    const fbx::Material& m = sc.materials[(std::size_t)matIdx];
    if (m.diffuseTexture >= 0) {
        std::string p = OwnTexturePath(&sc.textures[(std::size_t)m.diffuseTexture], modelPath, serial);
        if (!p.empty()) mr.texture = p;
    }
    if (m.hasDiffuse && mr.texture.empty())   // with a texture the factor would only darken it (see ApplyFbxMaterial)
        mr.color = Color((float)m.diffuse[0], (float)m.diffuse[1], (float)m.diffuse[2], 1.0f);
    if (m.normalTexture >= 0) {
        std::string p = OwnTexturePath(&sc.textures[(std::size_t)m.normalTexture], modelPath, serial + 1000);
        if (!p.empty()) mr.normalMap = p;
    }
    if (m.hasEmissive) {
        const float f = m.hasEmissiveFactor ? (float)m.emissiveFactor : 1.0f;
        if (m.emissive[0] + m.emissive[1] + m.emissive[2] > 0.0) mr.emissive = Color((float)m.emissive[0] * f, (float)m.emissive[1] * f, (float)m.emissive[2] * f, 1.0f);
    }
    if (m.hasRoughness || m.hasMetalness) {
        const float rough = m.hasRoughness ? (float)m.roughness : 0.6f;
        const float metal = m.hasMetalness ? (float)m.metalness : 0.0f;
        const float smooth = 1.0f - std::fmin(1.0f, std::fmax(0.0f, rough));
        mr.metallic = metal;
        mr.shininess = 4.0f + smooth * 124.0f;
        mr.specular = 0.15f + smooth * 0.85f;
        mr.reflectivity = metal * smooth;
    }
}

int OwnMaterialOfSlot(const fbx::Mesh& m, int slot) {
    return slot >= 0 && slot < (int)m.materials.size() ? m.materials[(std::size_t)slot] : -1;
}

// One material part: deduplicated vertices + triangle list (first occurrence order).
// The reader already shares corners that are equal byte for byte (numbered in order of first use), so the engine
// vertex is built once per reader vertex; reader vertices that differ only in what the engine drops (vertex colour)
// are merged here. Numbering by first use of the reader vertices is the same as numbering by first use of the
// corners, so the result is the one the unindexed corner list gave.
Mesh BuildOwnPart(const fbx::Mesh& mesh, const fbx::Part& part, bool skinned,
                  std::vector<std::array<int, 4>>* jIdx, std::vector<std::array<float, 4>>* jWt,
                  std::vector<uint32_t>* outVi) {
    Mesh out;
    std::vector<FbxVert> verts;
    verts.reserve(part.vertices.size());
    for (const fbx::Corner& c : part.vertices) {
        FbxVert fv{};
        fv.px = c.p[0]; fv.py = c.p[1]; fv.pz = c.p[2];
        fv.nx = c.n[0]; fv.ny = c.n[1]; fv.nz = c.n[2];
        fv.u = c.uv[0]; fv.v = 1.0f - c.uv[1];   // FBX UVs are bottom-up
        fv.w[0] = 1.0f;
        fv.vi = c.cp;
        if (skinned && c.cp < mesh.skinWeights.size()) {
            const auto& inf = mesh.skinWeights[c.cp];
            float sum = 0.0f;
            fv.w[0] = 0.0f;
            const std::size_t nw = inf.size() < 4 ? inf.size() : 4;
            for (std::size_t wi = 0; wi < nw; ++wi) { fv.j[wi] = (int)inf[wi].cluster; fv.w[wi] = inf[wi].weight; sum += fv.w[wi]; }
            if (sum > 1e-6f) for (float& w : fv.w) w /= sum;
        }
        verts.push_back(fv);
    }
    if (verts.empty() || part.indices.empty()) return out;
    // Equal vertices (byte for byte) share one index, numbered in first-occurrence order. An open-addressing table of
    // indices into `verts` (power-of-two size, at least twice the vertex count, linear probing).
    std::vector<uint32_t> remap(verts.size());
    std::vector<uint32_t> unique;
    {
        std::size_t cap = 16;
        while (cap < verts.size() * 2) cap <<= 1;
        const uint32_t kEmpty = 0xFFFFFFFFu;
        std::vector<uint32_t> table(cap, kEmpty);   // holds positions in `unique`
        for (std::size_t i = 0; i < verts.size(); ++i) {
            const unsigned char* b = (const unsigned char*)&verts[i];
            static_assert(sizeof(FbxVert) % 4 == 0, "FbxVert is hashed 4 bytes at a time");
            std::uint64_t h = 0x9E3779B97F4A7C15ull;   // multiply-xor over 32-bit words, then a final mix
            for (std::size_t k = 0; k < sizeof(FbxVert); k += 4) {
                std::uint32_t w; std::memcpy(&w, b + k, 4);
                h = (h ^ w) * 0xFF51AFD7ED558CCDull;
                h ^= h >> 32;
            }
            h ^= h >> 29;
            std::size_t slot = (std::size_t)h & (cap - 1);
            for (;;) {
                const uint32_t u = table[slot];
                if (u == kEmpty) { table[slot] = (uint32_t)unique.size(); remap[i] = (uint32_t)unique.size(); unique.push_back((uint32_t)i); break; }
                if (std::memcmp(&verts[unique[u]], b, sizeof(FbxVert)) == 0) { remap[i] = u; break; }
                slot = (slot + 1) & (cap - 1);
            }
        }
    }
    const bool hasN = mesh.hasNormals, hasUV = mesh.hasUV;
    out.vertices.reserve(unique.size());
    if (hasN) out.normals.reserve(unique.size());
    if (hasUV) out.uvs.reserve(unique.size());
    if (jIdx) { jIdx->reserve(unique.size()); jWt->reserve(unique.size()); }
    if (outVi) outVi->reserve(unique.size());
    for (uint32_t u : unique) {
        const FbxVert& v = verts[u];
        out.vertices.push_back(Vec3{v.px, v.py, v.pz});
        if (hasN) out.normals.push_back(Vec3{v.nx, v.ny, v.nz});
        if (hasUV) out.uvs.push_back(Vec2{v.u, v.v});
        if (jIdx) { jIdx->push_back({v.j[0], v.j[1], v.j[2], v.j[3]}); jWt->push_back({v.w[0], v.w[1], v.w[2], v.w[3]}); }
        if (outVi) outVi->push_back(v.vi);
    }
    out.triangles.reserve(part.indices.size());
    for (uint32_t ix : part.indices) out.triangles.push_back((int)remap[ix]);
    return out;
}

void AddOwnBlendShapes(const fbx::Mesh& mesh, const std::vector<uint32_t>& vi, GameObject* g, const Mat4* lin) {
    if (!g || mesh.channels.empty()) return;
    BlendShapes* bs = nullptr;
    for (const fbx::BlendChannel& ch : mesh.channels) {
        std::vector<Vec3> dp(vi.size(), Vec3{0, 0, 0}), dn;
        const bool hasN = !ch.dn.empty() && ch.dn.size() == ch.dp.size();
        if (hasN) dn.assign(vi.size(), Vec3{0, 0, 0});
        std::unordered_map<uint32_t, uint32_t> slot;
        for (std::size_t k = 0; k < ch.indices.size(); ++k) slot[ch.indices[k]] = (uint32_t)k;
        for (std::size_t v = 0; v < vi.size(); ++v) {
            auto it = slot.find(vi[v]);
            if (it == slot.end()) continue;
            const std::size_t k = it->second;
            Vec3 d{ch.dp[k * 3], ch.dp[k * 3 + 1], ch.dp[k * 3 + 2]};
            dp[v] = lin ? lin->MultiplyVector(d) : d;
            if (hasN) { Vec3 n{ch.dn[k * 3], ch.dn[k * 3 + 1], ch.dn[k * 3 + 2]}; dn[v] = lin ? lin->MultiplyVector(n) : n; }
        }
        if (!bs) bs = g->AddComponent<BlendShapes>();
        std::string nm = ch.name;
        if (std::size_t dot = nm.find_last_of('.'); dot != std::string::npos && dot + 1 < nm.size()) nm = nm.substr(dot + 1);
        if (nm.empty()) nm = "shape" + std::to_string(bs->shapes.size());
        bs->AddShape(nm, dp, dn, (float)ch.weight);
    }
    if (!bs) return;
    if (!g->GetComponent<SkinnedMesh>())
        if (auto* mr = g->GetComponent<MeshRenderer>()) {
            auto* sm = g->AddComponent<SkinnedMesh>();
            sm->bind = mr->mesh;
            sm->jointIdx.assign(mr->mesh.vertices.size(), std::array<int, 4>{0, 0, 0, 0});
            sm->jointWt.assign(mr->mesh.vertices.size(), std::array<float, 4>{1, 0, 0, 0});
            sm->joints = {g->transform}; sm->jointNames = {g->name};
            sm->inverseBind = {Mat4::Identity()};
        }
    bs->Apply(true);
}

bool OwnIsIdentity(const fbx::Mat& m) {
    static const double id[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    for (int i = 0; i < 12; ++i) if (std::fabs(m.m[i] - id[i]) > 1e-6) return false;
    return true;
}

} // namespace

static Mesh ImportFbxMergedOwn(const std::string& path, bool* ok, std::string* outTexture, std::string* errOut) {
    Mesh merged;
    fbx::Scene sc; std::string err;
    // The reader gives polygons; the import triangulates them (each mesh's polygon arrays freed once it has its parts).
    if (!fbx::LoadFile(path, sc, &err) || !fbx::BuildParts(sc, &err, fbx::Limits(), true)) { if (ok) *ok = false; if (errOut) *errOut = err; return merged; }
    for (const fbx::SceneNode& node : sc.nodes) {
        if (node.mesh < 0) continue;
        const fbx::Mesh& fm = sc.meshes[(std::size_t)node.mesh];
        const Mat4 g2w = OwnToMat4(node.geometryToWorld);
        for (const fbx::Part& part : fm.parts) {
            Mesh m = TransformedMesh(BuildOwnPart(fm, part, false, nullptr, nullptr, nullptr), g2w);
            const int base = (int)merged.vertices.size();
            const bool keepN = merged.normals.size() == merged.vertices.size() && m.normals.size() == m.vertices.size();
            const bool keepUV = merged.uvs.size() == merged.vertices.size() && m.uvs.size() == m.vertices.size();
            merged.vertices.insert(merged.vertices.end(), m.vertices.begin(), m.vertices.end());
            if (keepN) merged.normals.insert(merged.normals.end(), m.normals.begin(), m.normals.end()); else merged.normals.clear();
            if (keepUV) merged.uvs.insert(merged.uvs.end(), m.uvs.begin(), m.uvs.end()); else merged.uvs.clear();
            for (int t : m.triangles) merged.triangles.push_back(base + t);
            if (outTexture && outTexture->empty()) {
                MeshRenderer tmp; ApplyOwnMaterial(tmp, sc, OwnMaterialOfSlot(fm, part.slot), path, 0);
                *outTexture = tmp.texture;
            }
        }
    }
    if (ok) *ok = !merged.vertices.empty();
    if (merged.vertices.empty() && errOut)
        *errOut = sc.nodes.empty() ? "the FBX file was read but holds no objects (no models, meshes or bones)"
                                   : "the FBX file was read but holds no meshes (only empty nodes, bones, cameras, lights or animation)";
    return merged;
}

static GameObject* ImportFbxSceneGraphOwn(Scene& scene, const std::string& path, bool* ok, std::string* err) {
    fbx::Scene sc;
    // The reader gives polygons and animation curves; the import triangulates the polygons (each mesh's polygon arrays
    // freed once it has its parts) and makes the clips' keys.
    if (!fbx::LoadFile(path, sc, err) || !fbx::BuildParts(sc, err, fbx::Limits(), true) || !fbx::BuildClips(sc, err)) { if (ok) *ok = false; return nullptr; }
    if (sc.nodes.empty()) {   // read, but nothing to make: a failure with its reason, not an empty object
        if (err) *err = "the FBX file was read but holds no objects (no models, meshes or bones)";
        if (ok) *ok = false;
        return nullptr;
    }
    GameObject* root = scene.CreateGameObject(BaseName(path));
    std::vector<GameObject*> goOf(sc.nodes.size(), nullptr);
    std::unordered_map<std::string, int> nameUse;
    int texSerial = 0;

    for (std::size_t i = 0; i < sc.nodes.size(); ++i) {   // parents come before children
        const fbx::SceneNode& n = sc.nodes[i];
        std::string nm = n.name;
        if (nm.empty()) nm = n.mesh >= 0 ? "mesh" : "node";
        if (int c = nameUse[nm]++) nm += "_" + std::to_string(c);
        GameObject* g = scene.CreateGameObject(nm);
        goOf[i] = g;
        g->transform->localPosition = Vec3{(float)n.translation[0], (float)n.translation[1], (float)n.translation[2]};
        g->transform->localRotation = Quat{(float)n.rotation[0], (float)n.rotation[1], (float)n.rotation[2], (float)n.rotation[3]};
        g->transform->localScale    = Vec3{(float)n.scale[0], (float)n.scale[1], (float)n.scale[2]};
        g->transform->SetParent(n.parent >= 0 ? goOf[(std::size_t)n.parent]->transform : root->transform, false);
    }

    for (std::size_t ni = 0; ni < sc.nodes.size(); ++ni) {
        const fbx::SceneNode& node = sc.nodes[ni];
        if (node.mesh < 0) continue;
        const fbx::Mesh& mesh = sc.meshes[(std::size_t)node.mesh];
        GameObject* host = goOf[ni];
        const bool skinned = !mesh.clusters.empty();
        const bool geomOffset = !OwnIsIdentity(node.geometryToNode);
        const Mat4 g2n = OwnToMat4(node.geometryToNode);
        const std::size_t parts = mesh.parts.size();
        int made = 0;
        for (std::size_t pi = 0; pi < parts; ++pi) {
            const fbx::Part& part = mesh.parts[pi];
            std::vector<std::array<int, 4>> jIdx; std::vector<std::array<float, 4>> jWt;
            std::vector<uint32_t> vi;
            Mesh bind = BuildOwnPart(mesh, part, skinned, skinned ? &jIdx : nullptr, skinned ? &jWt : nullptr, mesh.channels.empty() ? nullptr : &vi);
            if (bind.vertices.empty()) continue;
            GameObject* g = host;
            const int matIdx = OwnMaterialOfSlot(mesh, part.slot);
            if (made++ > 0 || parts > 1) {
                g = scene.CreateGameObject(host->name + "_" + (matIdx >= 0 ? sc.materials[(std::size_t)matIdx].name : ("mat" + std::to_string(pi))));
                g->transform->SetParent(host->transform, false);
            }
            auto* mr = g->AddComponent<MeshRenderer>();
            mr->doubleSided = true;
            ApplyOwnMaterial(*mr, sc, matIdx, path, texSerial);
            texSerial += 2;
            if (!skinned) {
                mr->mesh = geomOffset ? TransformedMesh(bind, g2n) : bind;
                AddOwnBlendShapes(mesh, vi, g, geomOffset ? &g2n : nullptr);
                continue;
            }
            mr->mesh = geomOffset ? TransformedMesh(bind, g2n) : bind;
            auto* sm = g->AddComponent<SkinnedMesh>();
            sm->bind = bind; sm->jointIdx = std::move(jIdx); sm->jointWt = std::move(jWt);
            for (const fbx::Cluster& cl : mesh.clusters) {
                GameObject* bg = cl.boneNode >= 0 ? goOf[(std::size_t)cl.boneNode] : nullptr;
                sm->joints.push_back(bg ? bg->transform : nullptr);
                sm->jointNames.push_back(bg ? bg->name : std::string());
                sm->inverseBind.push_back(OwnToMat4(cl.geometryToBone));
            }
            ValidateSkinJoints(*sm, path);
            if (g != host) g->transform->localPosition = Vec3{0, 0, 0};
            AddOwnBlendShapes(mesh, vi, g, nullptr);
        }
    }

    // Baked clips -> ModelAnimator
    std::unordered_map<long long, std::pair<std::string, int>> chanOf;   // (mesh, channel) -> (node name, shape index)
    for (std::size_t ni = 0; ni < sc.nodes.size(); ++ni) {
        if (sc.nodes[ni].mesh < 0) continue;
        const int mi = sc.nodes[ni].mesh;
        for (std::size_t c = 0; c < sc.meshes[(std::size_t)mi].channels.size(); ++c)
            chanOf.emplace((long long)mi * 100000 + (long long)c, std::make_pair(goOf[ni]->name, (int)c));
    }
    if (!sc.clips.empty()) {
        auto* ma = root->AddComponent<ModelAnimator>();
        std::vector<float> kt, c0, c1, c2, c3;   // one channel's keys at a time (kept from node to node: no new arrays)
        for (const fbx::Clip& fc : sc.clips) {
            ModelAnimator::Clip clip;
            std::string cn = fc.name;
            { std::size_t bar = cn.find_last_of('|'); if (bar != std::string::npos) cn = cn.substr(bar + 1); }
            if (cn.empty() || cn == "mixamo.com" || cn[0] == '(' || cn.rfind("Take ", 0) == 0 || cn.rfind("Scene", 0) == 0) {
                cn = std::filesystem::u8path(path).stem().u8string();
                if (sc.clips.size() > 1) cn += "_" + std::to_string(ma->clips.size());
            }
            clip.name = cn;
            for (const fbx::NodeAnim& bn : fc.nodes) {
                if (bn.node < 0 || (std::size_t)bn.node >= goOf.size() || !goOf[(std::size_t)bn.node]) continue;
                clip.nodes.push_back({goOf[(std::size_t)bn.node]->name, AnimationClip{}});
                AnimationClip& ac = clip.nodes.back().clip;
                ac.smoothKeys = false;
                // One array per channel, then each track gets all its keys at once; tracks are created in the same
                // order as one AddKey per channel and sample did (the track table ends up the same).
                auto put3 = [&](const std::vector<fbx::Key3>& keys, const char* x, const char* y, const char* z) {
                    kt.clear(); c0.clear(); c1.clear(); c2.clear();
                    for (const fbx::Key3& k : keys) { kt.push_back((float)k.time); c0.push_back((float)k.v[0]); c1.push_back((float)k.v[1]); c2.push_back((float)k.v[2]); }
                    ac.AddKeys(x, kt.data(), c0.data(), kt.size()); ac.AddKeys(y, kt.data(), c1.data(), kt.size()); ac.AddKeys(z, kt.data(), c2.data(), kt.size());
                };
                put3(bn.translation, "position.x", "position.y", "position.z");
                {
                    double prev[4] = {0, 0, 0, 1}; bool hasPrev = false;
                    kt.clear(); c0.clear(); c1.clear(); c2.clear(); c3.clear();
                    for (const fbx::Key4& k : bn.rotation) {
                        double q[4] = {k.v[0], k.v[1], k.v[2], k.v[3]};
                        if (hasPrev && q[0] * prev[0] + q[1] * prev[1] + q[2] * prev[2] + q[3] * prev[3] < 0.0) for (double& c : q) c = -c;
                        for (int a = 0; a < 4; ++a) prev[a] = q[a];
                        hasPrev = true;
                        kt.push_back((float)k.time); c0.push_back((float)q[0]); c1.push_back((float)q[1]); c2.push_back((float)q[2]); c3.push_back((float)q[3]);
                    }
                    ac.AddKeys("rotation.qx", kt.data(), c0.data(), kt.size()); ac.AddKeys("rotation.qy", kt.data(), c1.data(), kt.size());
                    ac.AddKeys("rotation.qz", kt.data(), c2.data(), kt.size()); ac.AddKeys("rotation.qw", kt.data(), c3.data(), kt.size());
                }
                put3(bn.scale, "scale.x", "scale.y", "scale.z");
                if (ac.Tracks().empty()) clip.nodes.pop_back();
            }
            for (const fbx::BlendAnim& ba : fc.blends) {
                auto it = chanOf.find((long long)ba.mesh * 100000 + ba.channel);
                if (it == chanOf.end() || ba.percent.empty()) continue;
                ModelAnimator::NodeClip* ncp = nullptr;
                for (auto& ncx : clip.nodes) if (ncx.node == it->second.first) { ncp = &ncx; break; }
                if (!ncp) { clip.nodes.push_back({it->second.first, AnimationClip{}}); ncp = &clip.nodes.back(); ncp->clip.smoothKeys = false; }
                const std::string track = "weights." + std::to_string(it->second.second);
                std::vector<float> bt, bv;
                for (const fbx::Key3& k : ba.percent) { bt.push_back((float)k.time); bv.push_back((float)k.v[0] * 0.01f); }
                ncp->clip.AddKeys(track, bt.data(), bv.data(), bt.size());
            }
            if (!clip.nodes.empty()) ma->clips.push_back(std::move(clip));
        }
        if (ma->clips.empty()) root->RemoveComponent<ModelAnimator>();
        else { ma->active = 0; ma->autoPlay = true; }
    }

    NormalizeImportedClips(root);
    AutoNormalizeImportScale(scene, root);
    if (ok) *ok = true;
    return root;
}

static GameObject* ImportModelSceneRaw(Scene& scene, const std::string& path, bool* ok) {
    using namespace gltf_detail;
    std::string p = Lower(path);
    bool isGltf = EndsWith(p, ".gltf") || EndsWith(p, ".glb");

    // FBX: our own reader only (okay::fbx), full hierarchy + skins + animations. A file it
    // refuses fails the import (no other FBX reader is asked).
    if (EndsWith(p, ".fbx")) {
        std::string ferr; bool rejected = false;
        if (GameObject* r = ImportFbxSceneGraph(scene, path, ok, &ferr, &rejected)) return r;
        if (rejected) {   // past a size/shape limit: fail the import (no fallback reader gets the hostile file)
            Log::Error("model import '", path, "': ", ferr);
            if (ok) *ok = false;
            return nullptr;
        }
    }
    // Non-glTF (OBJ / STL / PLY / Assimp formats): read through the seam, as a node
    // hierarchy with skins + animations where the format has them (FBX rigs etc.).
    // Falls through to the flattened single-mesh path on failure.
    if (!isGltf) {
        {
            ImportedModel im;
            if (ImportModelData(path, im, ImportBackend::Default, /*flatten=*/false, nullptr))
                if (GameObject* r = BuildSceneFromModel(scene, im, path)) { if (ok) *ok = true; return r; }
        }
        bool okm = false; std::string tex;
        Mesh m = ImportModel(path, &okm, &tex);
        if (!okm || m.vertices.empty()) { if (ok) *ok = false; return nullptr; }
        GameObject* go = scene.CreateGameObject(BaseName(path));
        auto* mr = go->AddComponent<MeshRenderer>();
        mr->mesh = m; mr->doubleSided = true;
        if (!tex.empty()) mr->texture = tex;
        AutoNormalizeImportScale(scene, go);
        if (ok) *ok = true;
        return go;
    }

    // glTF 1.0 (deprecated, a different layout: meshes, nodes and accessors are objects, not arrays): our own reader when it
    // is switched on (OKAY_OWN_GLTF1, docs/content/model_readers.md section glTF 1.0), through the same seam as the other formats.
    if (Gltf1Wanted(path)) {
        ImportedModel im; std::string e;
        if (ImportModelData(path, im, ImportBackend::Default, /*flatten=*/false, &e))
            if (GameObject* r = BuildSceneFromModel(scene, im, path)) { if (ok) *ok = true; return r; }
        Log::Error("model import '", path, "': ", e.empty() ? std::string("glTF 1.0: the scene could not be built") : e);
        if (ok) *ok = false;
        return nullptr;
    }
    g_readBy = "the own glTF reader";
    GltfDoc doc = LoadDoc(path);
    if (!doc.ok) { if (ok) *ok = false; return nullptr; }
    // Otherwise glTF 1.0 is refused, with a message saying how to switch our reader on.
    if (const JVal* asset = doc.root.Find("asset"))
        if (const JVal* ver = asset->Find("version"))
            if (ver->type == JVal::Str && !ver->str.empty() && ver->str[0] == '1') {
                Log::Error("model import '", path, "': glTF ", ver->str, " is not read by default (only glTF 2.0); our own glTF 1.0 reader is off by default: switch it on with OKAY_OWN_GLTF1=1 or -DOKAY_OWN_GLTF1=ON, or re-export the model as glTF 2.0");
                if (ok) *ok = false;
                return nullptr;
            }
    // A file that cannot be read without an extension we do not have would import as an empty scene: say so instead.
    if (const JVal* req = doc.root.Find("extensionsRequired"))
        for (const JVal& e : req->arr)
            if (e.type == JVal::Str && (e.str == "KHR_draco_mesh_compression" || e.str == "EXT_meshopt_compression")) {
                Log::Error("model import '", path, "': the glTF needs the extension ", e.str, ", which is not supported");
                if (ok) *ok = false;
                return nullptr;
            }
    const JVal* nodes = doc.root.Find("nodes");
    int nodeCount = nodes ? (int)nodes->arr.size() : 0;

    // The node graph must be a strict tree (glTF requires it): a node listed as a
    // child more than once, or "children" links that loop (a node under itself),
    // would build a hierarchy that never ends — every edge is applied below.
    {
        std::vector<int> parentOf(nodeCount, -1);
        for (int i = 0; i < nodeCount; ++i)
            if (const JVal* C = nodes->arr[i].Find("children"))
                for (const JVal& c : C->arr) {
                    int ci = c.Int(-1);
                    if (ci < 0 || ci >= nodeCount) continue;
                    if (parentOf[ci] >= 0) { if (ok) *ok = false; return nullptr; }   // two parents
                    parentOf[ci] = i;
                }
        std::vector<char> state(nodeCount, 0);   // 0 unseen, 1 on the current walk, 2 known acyclic
        std::vector<int> walk;
        for (int i = 0; i < nodeCount; ++i) {
            walk.clear();
            int j = i;
            while (j >= 0 && state[j] == 0) { state[j] = 1; walk.push_back(j); j = parentOf[j]; }
            if (j >= 0 && state[j] == 1) { if (ok) *ok = false; return nullptr; }   // came back round
            for (int w : walk) state[w] = 2;
        }
    }

    GameObject* root = scene.CreateGameObject(BaseName(path));
    if (nodeCount == 0) {   // no scene graph: just merge the meshes onto the root
        bool okm = false; std::string tex; Mesh m = LoadGLTF(path, &okm, &tex);
        if (okm && !m.vertices.empty()) {
            auto* mr = root->AddComponent<MeshRenderer>(); mr->mesh = m; mr->doubleSided = true;
            if (!tex.empty()) mr->texture = tex;
        }
        AutoNormalizeImportScale(scene, root);
        // A file that names no mesh at all (no nodes, no meshes) is an empty scene, as Assimp reads it; one that names
        // meshes none of which could be read is a failed import.
        // (Assimp also wants a scene: a file with no "scenes" at all, or a glTF 1.0 file whose meshes are an object, fails.)
        const JVal* ms = doc.root.Find("meshes");
        const JVal* scs = doc.root.Find("scenes");
        const bool namesNoMesh = !ms || (ms->type == JVal::Arr && ms->arr.empty());
        if (ok) *ok = okm || (namesNoMesh && scs && scs->type == JVal::Arr && !scs->arr.empty());
        return root;
    }

    auto vec = [](const JVal* a, int i, float d) {
        return (a && a->type == JVal::Arr && i < (int)a->arr.size()) ? (float)a->arr[i].Number(d) : d;
    };

    // One GameObject per glTF node, with its local TRS and mesh. Skinned meshes are
    // deferred to a second pass (they reference joint nodes that must all exist first).
    std::vector<GameObject*> go(nodeCount, nullptr);
    std::vector<int> skinnedNodes;
    // Names are made unique: clips and skins bind to nodes by name, and two
    // "Bone" nodes would otherwise animate / skin to the same one.
    std::unordered_map<std::string, int> nameUse;
    for (int i = 0; i < nodeCount; ++i) {
        const JVal& n = nodes->arr[i];
        std::string nm = n.Find("name") ? n.Find("name")->Text() : ("node" + std::to_string(i));
        if (nm.empty()) nm = "node" + std::to_string(i);
        if (int c = nameUse[nm]++) nm += "_" + std::to_string(c);
        GameObject* g = scene.CreateGameObject(nm);
        go[i] = g;
        const JVal* T = n.Find("translation");
        const JVal* R = n.Find("rotation");
        const JVal* S = n.Find("scale");
        g->transform->localPosition = { vec(T,0,0), vec(T,1,0), vec(T,2,0) };
        if (R) g->transform->localRotation = Quat{ vec(R,0,0), vec(R,1,0), vec(R,2,0), vec(R,3,1) };
        g->transform->localScale = { vec(S,0,1), vec(S,1,1), vec(S,2,1) };
        // A node may give its transform as a column-major 4x4 "matrix" instead
        // (exporters' Z-up -> Y-up and armature nodes often do): decompose it.
        if (const JVal* M = n.Find("matrix"); M && M->type == JVal::Arr && M->arr.size() == 16) {
            float m[16];
            for (int e = 0; e < 16; ++e) m[e] = (float)M->arr[e].Number(e % 5 == 0 ? 1.0 : 0.0);
            Vec3 c0{m[0], m[1], m[2]}, c1{m[4], m[5], m[6]}, c2{m[8], m[9], m[10]};
            Vec3 sc{c0.Magnitude(), c1.Magnitude(), c2.Magnitude()};
            // A mirrored matrix (negative determinant) keeps a proper rotation by
            // flipping one axis' scale.
            Vec3 cr = Vec3::Cross(c0, c1);
            if (cr.x * c2.x + cr.y * c2.y + cr.z * c2.z < 0.0f) sc.x = -sc.x;
            if (std::fabs(sc.x) > 1e-8f) c0 = c0 * (1.0f / sc.x);
            if (sc.y > 1e-8f) c1 = c1 * (1.0f / sc.y);
            if (sc.z > 1e-8f) c2 = c2 * (1.0f / sc.z);
            g->transform->localPosition = Vec3{m[12], m[13], m[14]};
            g->transform->localRotation = Quat::FromRotationMatrix(c0.x, c1.x, c2.x, c0.y, c1.y, c2.y, c0.z, c1.z, c2.z);
            g->transform->localScale = sc;
        }
        if (const JVal* M = n.Find("mesh")) {
            if (n.Find("skin")) {
                skinnedNodes.push_back(i);   // deformed in the skin pass below
            } else {
                int meshIdx = M->Int(-1);
                // Count distinct materials across this mesh's primitives.
                int pc = MeshPrimitiveCount(doc, meshIdx);
                std::set<int> matset;
                for (int pi = 0; pi < pc; ++pi) matset.insert(PrimitiveMaterial(doc, meshIdx, pi));
                if (pc <= 1 || matset.size() <= 1) {
                    // Single material: merge all primitives onto this node (as before).
                    Mesh mm = BuildMeshAt(doc, meshIdx);
                    if (!mm.vertices.empty()) {
                        auto* mr = g->AddComponent<MeshRenderer>(); mr->mesh = mm; mr->doubleSided = true;
                        ApplyGltfMaterial(*mr, ResolveMeshMaterial(doc, meshIdx, path));
                        AddGltfMorphTargets(doc, meshIdx, -1, g);
                    }
                } else {
                    // Multi-material: one child object per primitive, each with its
                    // own sub-mesh + material, so every texture comes through.
                    for (int pi = 0; pi < pc; ++pi) {
                        Mesh pm = BuildPrimitiveMesh(doc, meshIdx, pi);
                        if (pm.vertices.empty()) continue;
                        GameObject* part = scene.CreateGameObject(g->name + "_mat" + std::to_string(pi));
                        auto* mr = part->AddComponent<MeshRenderer>(); mr->mesh = pm; mr->doubleSided = true;
                        ApplyGltfMaterial(*mr, ResolveMaterial(doc, PrimitiveMaterial(doc, meshIdx, pi), path));
                        part->transform->SetParent(g->transform, false);
                        AddGltfMorphTargets(doc, meshIdx, pi, part);
                    }
                }
            }
        }
    }
    // Parent per children arrays; roots (no parent) hang under the import root.
    std::vector<bool> hasParent(nodeCount, false);
    for (int i = 0; i < nodeCount; ++i)
        if (const JVal* C = nodes->arr[i].Find("children"))
            for (const JVal& c : C->arr) {
                int ci = c.Int(-1);
                if (ci >= 0 && ci < nodeCount && go[ci]) { go[ci]->transform->SetParent(go[i]->transform, false); hasParent[ci] = true; }
            }
    for (int i = 0; i < nodeCount; ++i)
        if (go[i] && !hasParent[i]) go[i]->transform->SetParent(root->transform, false);

    // ---- Skinned meshes: a SkinnedMesh that deforms by the skin's joint nodes ----
    const JVal* skins = doc.root.Find("skins");
    for (int ni : skinnedNodes) {
        const JVal& n = nodes->arr[ni];
        int meshIdx = n.Find("mesh") ? n.Find("mesh")->Int(-1) : -1;
        int skinIdx = n.Find("skin") ? n.Find("skin")->Int(-1) : -1;
        if (!skins || skinIdx < 0 || skinIdx >= (int)skins->arr.size()) continue;
        const JVal& skin = skins->arr[skinIdx];
        const JVal* jts = skin.Find("joints");
        if (!jts) continue;

        // Shared by every material part of this skinned mesh.
        std::vector<Transform*> jointTs; std::vector<std::string> jointNs; std::vector<Mat4> invBind;
        for (const JVal& jn : jts->arr) {
            int jnode = jn.Int(-1);
            GameObject* jg = (jnode >= 0 && jnode < nodeCount) ? go[jnode] : nullptr;
            jointTs.push_back(jg ? jg->transform : nullptr);
            jointNs.push_back(jg ? jg->name : std::string{});   // for save/reload
        }
        if (const JVal* IB = skin.Find("inverseBindMatrices")) {
            int c = 0, cnt = 0; auto fv = ReadAccessor(doc, IB->Int(-1), c, cnt);
            for (int j = 0; j < cnt && c == 16; ++j) {   // MAT4 only
                Mat4 M; for (int e = 0; e < 16; ++e) M.m[e] = fv[(std::size_t)j*16 + e];
                invBind.push_back(M);
            }
        }
        if (invBind.size() != jointTs.size()) invBind.assign(jointTs.size(), Mat4::Identity());

        // One material: skin it on the node itself. Several: one child per
        // material, each its own SkinnedMesh over the same joints, so every
        // texture of a multi-material character comes through.
        int pc = MeshPrimitiveCount(doc, meshIdx);
        std::set<int> matset;
        for (int pi = 0; pi < pc; ++pi) matset.insert(PrimitiveMaterial(doc, meshIdx, pi));
        const bool split = pc > 1 && matset.size() > 1;
        for (int pi = split ? 0 : -1; pi < (split ? pc : 0); ++pi) {
            std::vector<std::array<int, 4>>   jIdx;
            std::vector<std::array<float, 4>> jWt;
            Mesh bind = BuildSkinnedMesh(doc, meshIdx, jIdx, jWt, pi);
            if (bind.vertices.empty()) continue;
            GameObject* host = go[ni];
            if (split) {
                host = scene.CreateGameObject(go[ni]->name + "_mat" + std::to_string(pi));
                host->transform->SetParent(go[ni]->transform, false);
            }
            auto* mr = host->AddComponent<MeshRenderer>();
            mr->mesh = bind; mr->doubleSided = true;
            ApplyGltfMaterial(*mr, split ? ResolveMaterial(doc, PrimitiveMaterial(doc, meshIdx, pi), path)
                                         : ResolveMeshMaterial(doc, meshIdx, path));
            auto* sm = host->AddComponent<SkinnedMesh>();
            sm->bind = bind; sm->jointIdx = std::move(jIdx); sm->jointWt = std::move(jWt);
            sm->joints = jointTs; sm->jointNames = jointNs; sm->inverseBind = invBind;
            ValidateSkinJoints(*sm, path);
            AddGltfMorphTargets(doc, meshIdx, pi, host);
        }
    }

    // glTF CUBICSPLINE output stores (in-tangent, value, out-tangent) per key. Bake it
    // into dense LINEAR keys with the spec's Hermite formula and the real tangents,
    // so overshoot, anticipation and eased holds play back as authored (taking only
    // the values and re-smoothing them flattened every exported F-curve). Each
    // segment gets ~60 samples per second (2..16). Rotations are renormalised.
    auto bakeCubic = [](const std::vector<float>& times, const std::vector<float>& vals, int oc,
                        bool quat, std::vector<float>& outT, std::vector<float>& outV) {
        outT.clear(); outV.clear();
        const int n = (int)times.size();
        if (n <= 0 || oc <= 0 || (int)vals.size() < n * 3 * oc) return;
        auto blk = [&](int k, int which) { return &vals[(std::size_t)(k * 3 + which) * oc]; };   // 0 in, 1 value, 2 out
        auto emit = [&](float t, const float* v) {
            std::size_t at = outV.size();
            outT.push_back(t); outV.insert(outV.end(), v, v + oc);
            if (quat && oc == 4) {
                float* q = &outV[at];
                float len = std::sqrt(q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3]);
                if (len > 1e-8f) for (int c = 0; c < 4; ++c) q[c] /= len;
            }
        };
        std::vector<float> tmp((std::size_t)oc);
        for (int k = 0; k < n; ++k) {
            emit(times[k], blk(k, 1));
            if (k + 1 >= n) break;
            const float td = times[k + 1] - times[k];
            if (!(td > 1e-6f)) continue;
            int sub = (int)std::ceil(td * 60.0f);
            sub = sub < 2 ? 2 : (sub > 16 ? 16 : sub);
            const float* v0 = blk(k, 1); const float* b0 = blk(k, 2);       // value, out-tangent of k
            const float* v1 = blk(k + 1, 1); const float* a1 = blk(k + 1, 0); // value, in-tangent of k+1
            for (int j = 1; j < sub; ++j) {
                const float u = (float)j / (float)sub, u2 = u * u, u3 = u2 * u;
                const float h00 = 2*u3 - 3*u2 + 1, h10 = u3 - 2*u2 + u, h01 = -2*u3 + 3*u2, h11 = u3 - u2;
                for (int c = 0; c < oc; ++c)
                    tmp[c] = h00 * v0[c] + h10 * td * b0[c] + h01 * v1[c] + h11 * td * a1[c];
                emit(times[k] + td * u, tmp.data());
            }
        }
    };

    // ALL animations -> a ModelAnimator clip library on the root (idle/walk/run...).
    // Each clip holds per-node TRS tracks (rotation as a quaternion so it plays back
    // exactly). At runtime PlayIndex pushes a clip's tracks onto an Animator per node.
    if (const JVal* anims = doc.root.Find("animations")) {
        if (!anims->arr.empty()) {
            auto* ma = root->AddComponent<ModelAnimator>();
            int ai = 0;
            for (const JVal& anim : anims->arr) {
                ModelAnimator::Clip clip;
                const JVal* nmv = anim.Find("name");
                clip.name = (nmv && nmv->type == JVal::Str && !nmv->str.empty()) ? nmv->str : ("clip" + std::to_string(ai));
                const JVal* chans = anim.Find("channels");
                const JVal* samps = anim.Find("samplers");
                if (chans && samps) {
                    for (const JVal& ch : chans->arr) {
                        const JVal* tgt = ch.Find("target"); if (!tgt) continue;
                        int node = tgt->Find("node") ? tgt->Find("node")->Int(-1) : -1;
                        std::string tpath = tgt->Find("path") ? tgt->Find("path")->Text() : "";
                        int si = ch.Find("sampler") ? ch.Find("sampler")->Int(-1) : -1;
                        if (node < 0 || node >= nodeCount || !go[node] || si < 0 || si >= (int)samps->arr.size()) continue;
                        const JVal& s = samps->arr[si];
                        int inAcc = s.Find("input") ? s.Find("input")->Int(-1) : -1;
                        int outAcc = s.Find("output") ? s.Find("output")->Int(-1) : -1;
                        int tc = 0, tn = 0; auto times = ReadAccessor(doc, inAcc, tc, tn);
                        int oc = 0, on = 0; auto vals = ReadAccessor(doc, outAcc, oc, on);
                        if (times.empty() || vals.empty()) continue;
                        // Morph weights: one scalar per target per key -> "weights.<i>".
                        if (tpath == "weights") {
                            int nw = tn > 0 ? (on * oc) / tn : 0;
                            std::string interpW = s.Find("interpolation") ? s.Find("interpolation")->Text() : "LINEAR";
                            const bool cub = interpW == "CUBICSPLINE";
                            if (cub) nw /= 3;
                            if (nw <= 0) continue;
                            const std::string& nodeName = go[node]->name;
                            ModelAnimator::NodeClip* ncp = nullptr;
                            for (auto& ncx : clip.nodes) if (ncx.node == nodeName) { ncp = &ncx; break; }
                            if (!ncp) { clip.nodes.push_back({nodeName, AnimationClip{}}); ncp = &clip.nodes.back(); }
                            ncp->clip.smoothKeys = false;   // LINEAR as authored; CUBICSPLINE baked below
                            if (cub) {
                                std::vector<float> bt, bv;
                                bakeCubic(times, vals, nw, false, bt, bv);
                                for (std::size_t k = 0; k < bt.size(); ++k)
                                    for (int w = 0; w < nw; ++w)
                                        ncp->clip.AddKey("weights." + std::to_string(w), bt[k], bv[k * nw + w]);
                                continue;
                            }
                            for (int k = 0; k < tn; ++k) {
                                int vb = k * nw;
                                if (vb + nw > on * oc) break;
                                for (int w = 0; w < nw; ++w)
                                    ncp->clip.AddKey("weights." + std::to_string(w), times[k], vals[(std::size_t)vb + w]);
                            }
                            continue;
                        }
                        // CUBICSPLINE stores 3 blocks per key (in-tangent, value, out-tangent);
                        // take the middle (value) block. LINEAR/STEP store one block per key.
                        std::string interp = s.Find("interpolation") ? s.Find("interpolation")->Text() : "LINEAR";
                        bool cubic = (interp == "CUBICSPLINE");
                        bool step  = (interp == "STEP");

                        const std::string& nodeName = go[node]->name;
                        ModelAnimator::NodeClip* ncp = nullptr;
                        for (auto& ncx : clip.nodes) if (ncx.node == nodeName) { ncp = &ncx; break; }
                        if (!ncp) { clip.nodes.push_back({nodeName, AnimationClip{}}); ncp = &clip.nodes.back(); }
                        AnimationClip& ac = ncp->clip;
                        // LINEAR / STEP data plays back exactly; CUBICSPLINE is baked
                        // with its tangents into dense linear keys (bakeCubic).
                        ac.smoothKeys = false;
                        float prevQ[4] = {0, 0, 0, 1}; bool haveQ = false;
                        auto put = [&](float t, const float* v) {
                            if (tpath == "translation" && oc >= 3) {
                                ac.AddKey("position.x", t, v[0]); ac.AddKey("position.y", t, v[1]); ac.AddKey("position.z", t, v[2]);
                            } else if (tpath == "scale" && oc >= 3) {
                                ac.AddKey("scale.x", t, v[0]); ac.AddKey("scale.y", t, v[1]); ac.AddKey("scale.z", t, v[2]);
                            } else if (tpath == "rotation" && oc >= 4) {
                                // Keep neighbouring keys in one hemisphere: the curves
                                // interpolate components, and a sign flip reads as a spin.
                                float q[4] = {v[0], v[1], v[2], v[3]};
                                if (haveQ && q[0]*prevQ[0] + q[1]*prevQ[1] + q[2]*prevQ[2] + q[3]*prevQ[3] < 0.0f)
                                    for (float& c : q) c = -c;
                                for (int c = 0; c < 4; ++c) prevQ[c] = q[c];
                                haveQ = true;
                                ac.AddKey("rotation.qx", t, q[0]); ac.AddKey("rotation.qy", t, q[1]);
                                ac.AddKey("rotation.qz", t, q[2]); ac.AddKey("rotation.qw", t, q[3]);
                            }
                        };
                        if (cubic) {
                            std::vector<float> bt, bv;
                            bakeCubic(times, vals, oc, tpath == "rotation", bt, bv);
                            for (std::size_t k = 0; k < bt.size(); ++k) put(bt[k], &bv[k * oc]);
                            continue;
                        }
                        for (int k = 0; k < tn; ++k) {
                            float t = times[k];
                            int vbase = k * oc;                           // value block for this key
                            if (vbase + oc > on * oc) break;              // guard against short data
                            // STEP: hold this value until just before the next key.
                            if (step && k > 0) {
                                int pbase = (k - 1) * oc;
                                float hold = t - std::fmax(1e-4f, (t - times[k - 1]) * 1e-3f);
                                if (hold > times[k - 1]) put(hold, &vals[pbase]);
                            }
                            put(t, &vals[vbase]);
                        }
                    }
                }
                ma->clips.push_back(std::move(clip));
                ++ai;
            }
            ma->active = 0; ma->autoPlay = true;
        }
    }
    // Nodes that name a mesh but nothing came out (the .bin file is missing, the data is malformed, only points or lines):
    // the import failed, it did not produce an empty model.
    {
        bool wantsMesh = false;
        for (int i = 0; i < nodeCount && !wantsMesh; ++i) wantsMesh = nodes->arr[i].Find("mesh") != nullptr;
        bool anyTriangles = false;   // any geometry: a points / lines primitive keeps its vertices and has no triangles
        for (const auto& up : scene.Objects()) {
            GameObject* g = up.get();
            if (!g || !g->IsSelfOrDescendantOf(root)) continue;
            if (auto* mr = g->GetComponent<MeshRenderer>()) { if (!mr->mesh.vertices.empty()) { anyTriangles = true; break; } }
            if (auto* sm = g->GetComponent<SkinnedMesh>()) { if (!sm->bind.vertices.empty()) { anyTriangles = true; break; } }
        }
        if (wantsMesh && !anyTriangles) {
            Log::Error("model import '", path, "': the glTF names meshes but none could be read (a missing .bin file or malformed accessors)");
            scene.Destroy(root);
            if (ok) *ok = false;
            return nullptr;
        }
    }
    NormalizeImportedClips(root);
    AutoNormalizeImportScale(scene, root);
    if (ok) *ok = true;
    return root;
}

// The model's own import settings (`<model>.okaymeta`): Normals / Smoothing angle / Import animations.
// Rigged (skinned) parts and meshes with blend shapes keep the file's normals: their vertices are shared with the
// skin weights and the shapes, so they are not rebuilt.
GameObject* ImportModelScene(Scene& scene, const std::string& path, bool* ok) {
    // One line per import, so the Console shows which reader read the file (an OKAY_OWN_<FMT> switch at work, or Assimp)
    // and how long it took: "model import 'x.lxo': read by the own LXO reader in 1.20 ms (whole import 3.40 ms)".
    g_readBy.clear(); g_readMs = -1.0;
    if (!IsModelFile(path)) {
        Log::Error("model import '", path, "': not a file (missing, or a folder)");
        if (ok) *ok = false;
        return nullptr;
    }
    const auto t0 = std::chrono::steady_clock::now();
    GameObject* root = ImportModelSceneRaw(scene, path, ok);
    {
        const double total = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        char times[96];
        if (g_readMs >= 0.0) std::snprintf(times, sizeof(times), "in %.2f ms (whole import %.2f ms)", g_readMs, total);
        else std::snprintf(times, sizeof(times), "in %.2f ms", total);
        if (root) Log::Info("model import '", path, "': read by ", g_readBy.empty() ? std::string("an unnamed reader") : g_readBy, " ", times);
        else Log::Info("model import '", path, "': no reader could read it (", times + 3, ")");
    }
    if (!root) return root;
    const ModelImportOptions opt = ReadModelImportOptions(path);
    if (opt.normals != ModelImportOptions::Import) {
        for (MeshRenderer* mr : root->GetComponentsInChildren<MeshRenderer>(true)) {
            GameObject* host = mr->gameObject;
            if (!host || host->GetComponent<SkinnedMesh>() || host->GetComponent<BlendShapes>()) continue;
            if (opt.normals == ModelImportOptions::Calculate) mr->mesh.ComputeAutoSmoothNormals(opt.smoothingAngle);
            else { mr->mesh.normals.clear(); mr->mesh.autoSmoothAngle = 0.0f; }   // flat: one normal per face
        }
    }
    if (!opt.importAnimations) {
        std::vector<GameObject*> all{root};
        root->ForEachDescendant([&](GameObject& g) { all.push_back(&g); });
        for (GameObject* g : all) {
            if (auto* ma = g->GetComponent<ModelAnimator>()) g->RemoveComponent(ma);
            if (auto* an = g->GetComponent<Animator>()) g->RemoveComponent(an);
        }
    }
    return root;
}

} // namespace okay
