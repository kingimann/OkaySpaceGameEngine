#include <set>
#include <type_traits>
#include <unordered_map>
#include "okay/Render/ModelImport.hpp"
#include "okay/Core/Log.hpp"
#include <cstdlib>
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
#include "ufbx.h"
#include "okay/Render/FbxReader.hpp"
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
#include "ModelImportInternal.hpp"

namespace okay {

static std::string Lower(std::string s);

bool AssimpAvailable() {
#ifdef OKAY_HAVE_ASSIMP
    return true;
#else
    return false;
#endif
}

std::string ImportableExtensions() {
    // Built in: OBJ, glTF, FBX (ufbx) and our own STL / PLY readers, plus everything
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
    if ((" " + list + " ").find(" .dae ") == std::string::npos && EffectiveImportBackend(".dae") == ImportBackend::Own)
        return list + " .dae";
    return list;
}

static std::string Lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
static bool EndsWith(const std::string& s, const char* suf) {
    std::string t = suf;
    return s.size() >= t.size() && s.compare(s.size() - t.size(), t.size(), t) == 0;
}
static Mesh ImportFbxMerged(const std::string& path, bool* ok, std::string* outTexture);   // (FBX section below)
static Mesh ImportFbxMergedOwn(const std::string& path, bool* ok, std::string* outTexture);   // our own reader (okay::fbx)
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


Mesh ImportModel(const std::string& path, bool* ok, std::string* outTexture) {
    std::string p = Lower(path);
    if (EndsWith(p, ".obj") && EffectiveImportBackend(".obj") != ImportBackend::Assimp)
        return Mesh::LoadOBJ(path, ok, outTexture);
    if (EndsWith(p, ".gltf") || EndsWith(p, ".glb"))  return LoadGLTF(path, ok, outTexture);
    if (EndsWith(p, ".fbx")) {
        bool fok = false;
        Mesh m = ImportFbxMerged(path, &fok, outTexture);
        if (fok) { if (ok) *ok = true; return m; }
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

static std::string BaseName(const std::string& path) {
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
        if (g.indexed) {
            for (int i = 0; i < g.in; ++i) mesh.triangles.push_back(base + (int)(g.idx[i] + 0.5f));
        } else {
            for (int i = 0; i + 2 < pn; i += 3) {
                mesh.triangles.push_back(base + i); mesh.triangles.push_back(base + i + 1); mesh.triangles.push_back(base + i + 2);
            }
        }
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

// 0 = .obj, 1 = .stl, 2 = .ply, 3 = .dae; -1 = no own reader.
int OwnSlot(const std::string& ext) {
    const std::string e = Lower(ext);
    if (e == ".obj") return 0;
    if (e == ".stl") return 1;
    if (e == ".ply") return 2;
    if (e == ".dae") return 3;
    return -1;
}
std::atomic<int> g_backendChoice[4] = {{0}, {0}, {0}, {0}};   // ImportBackend per own-reader format

// Our Collada reader is opt-in: run-time SetImportBackend, else OKAY_OWN_COLLADA=0/1, else the CMake option.
bool OwnColladaOptedIn() {
    bool v = kOwnColladaDefault;
    if (const char* e = std::getenv("OKAY_OWN_COLLADA")) { if (e[0] == '1') v = true; else if (e[0] == '0') v = false; }
    return v;
}

std::string ExtensionOf(const std::string& path) {
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
    if (s == 3) {   // Collada: off by default; Assimp reads it when the build has it
        if (OwnColladaOptedIn()) return ImportBackend::Own;
        return AssimpAvailable() ? ImportBackend::Assimp : ImportBackend::Default;
    }
    if (s == 0 || kPreferOwnDefault || !AssimpAvailable()) return ImportBackend::Own;
    return ImportBackend::Assimp;
}

bool ImportModelData(const std::string& path, ImportedModel& out, ImportBackend which, bool flatten, std::string* err) {
    out = ImportedModel{};
    // `flatten` matters to Assimp and to our Collada reader (the others have no node transforms).
    const std::string ext = ExtensionOf(path);
    ImportBackend use = which == ImportBackend::Default ? EffectiveImportBackend(ext) : which;
    if (use == ImportBackend::Assimp) {
#ifdef OKAY_HAVE_ASSIMP
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
        bool good = s == 0 ? ReadObjFile(path, out, &e1) : s == 1 ? ReadStlFile(path, out, &e1) : s == 2 ? ReadPlyFile(path, out, &e1)
                                                                                                         : ReadColladaFile(path, out, &e1, flatten);
        if (good) {
            for (const std::string& n : out.notes) Log::Warning("model import '", path, "': ", n);
            return true;
        }
        // Our reader said no. With the default choice, give Assimp a go when the build has it.
        if (which == ImportBackend::Default && AssimpAvailable()) {
#ifdef OKAY_HAVE_ASSIMP
            if (assimp_backend::Read(path, flatten, out, nullptr)) return true;
#endif
        }
        out = ImportedModel{};
        if (err) *err = e1;
        return false;
    }
    if (err) *err = ExtensionOf(path) == ".dae"
        ? "no Collada reader enabled: our own reader is off by default (set OKAY_OWN_COLLADA=1, build with -DOKAY_OWN_COLLADA=ON, or call SetImportBackend(\".dae\", ImportBackend::Own)); the Assimp one needs -DOKAY_USE_ASSIMP=ON"
        : "no reader for this format in this build";
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
    if (model.parts.empty()) return nullptr;
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
// FBX (built in, via the vendored ufbx — no Assimp needed). Mixamo characters,
// Blender/Maya/3ds Max exports: node hierarchy, meshes split per material,
// skinning (top 4 weights per vertex), PBR/Phong materials with external or
// embedded textures, and every animation take baked to per-node TRS keys.
// Converted on load to Y-up, meters (ufbx does the axis/unit math).
// ============================================================================
namespace {

Mat4 UfbxToMat4(const ufbx_matrix& a) {
    Mat4 r = Mat4::Identity();
    r.at(0, 0) = (float)a.m00; r.at(0, 1) = (float)a.m10; r.at(0, 2) = (float)a.m20;
    r.at(1, 0) = (float)a.m01; r.at(1, 1) = (float)a.m11; r.at(1, 2) = (float)a.m21;
    r.at(2, 0) = (float)a.m02; r.at(2, 1) = (float)a.m12; r.at(2, 2) = (float)a.m22;
    r.at(3, 0) = (float)a.m03; r.at(3, 1) = (float)a.m13; r.at(3, 2) = (float)a.m23;
    return r;
}
std::string UfbxStr(const ufbx_string& s) { return std::string(s.data, s.length); }

// Hardening: the ufbx path enforces the same numbers as okay::fbx::Limits (one source of truth).
// The node hierarchy depth is capped inside ufbx itself (node_depth_limit; its default of 0 lets a
// flat file declare an arbitrarily deep Model chain), and the node / animation counts are checked
// right after loading, before anything is built from them. `limitHit` is set when the file was
// refused for size or shape rather than because it could not be read.
ufbx_scene* LoadFbx(const std::string& path, std::string* err, bool* limitHit = nullptr) {
    const fbx::Limits lim;
    ufbx_load_opts opts{};
    opts.target_axes = ufbx_axes_right_handed_y_up;
    opts.target_unit_meters = 1.0f;
    opts.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;   // bake units/axes into the data, not extra nodes
    // Maya/MotionBuilder scale-inheritance modes the engine's transforms don't
    // model: ufbx folds them into the transforms (helper nodes only if it must).
    opts.inherit_mode_handling = UFBX_INHERIT_MODE_HANDLING_COMPENSATE;
    opts.generate_missing_normals = true;
    opts.ignore_missing_external_files = true;
    opts.evaluate_skinning = false;
    opts.node_depth_limit = (uint32_t)lim.maxNodeDepth;
    if (limitHit) *limitHit = false;
    ufbx_error e{};
    ufbx_scene* sc = ufbx_load_file(path.c_str(), &opts, &e);
    if (!sc) {
        if (err) { char buf[512]; ufbx_format_error(buf, sizeof(buf), &e); *err = buf; }
        if (limitHit && (e.type == UFBX_ERROR_NODE_DEPTH_LIMIT || e.type == UFBX_ERROR_MEMORY_LIMIT || e.type == UFBX_ERROR_ALLOCATION_LIMIT)) *limitHit = true;
        return nullptr;
    }
    std::string why;
    if (sc->nodes.count > lim.maxNodes) why = "too many nodes";
    else if (sc->anim_stacks.count > lim.maxAnimStacks) why = "too many animation stacks";
    else if (sc->anim_layers.count > lim.maxAnimLayers) why = "too many animation layers";
    else if (sc->anim_curves.count > lim.maxAnimCurves) why = "too many animation curves";
    else {
        std::size_t keys = 0;
        for (const ufbx_anim_curve* c : sc->anim_curves) { keys += c->keyframes.count; if (keys > lim.maxAnimKeys) { why = "too many animation keys"; break; } }
    }
    if (!why.empty()) {
        ufbx_free_scene(sc);
        if (err) *err = "FBX rejected: " + why;
        if (limitHit) *limitHit = true;
        return nullptr;
    }
    return sc;
}

// A texture as a file the renderer can load: the referenced file when it's on
// disk (next to the model, then the absolute path), else the embedded bytes
// written beside the model.
std::string FbxTexturePath(const ufbx_texture* tex, const std::string& modelPath, int serial) {
    if (!tex) return {};
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = fs::path(modelPath).parent_path();
    for (const ufbx_string* s : {&tex->filename, &tex->relative_filename, &tex->absolute_filename}) {
        if (!s->length) continue;
        fs::path p(UfbxStr(*s));
        if (fs::exists(p, ec)) return p.string();
        if (fs::exists(dir / p, ec)) return (dir / p).string();
        if (fs::exists(dir / p.filename(), ec)) return (dir / p.filename()).string();
        if (fs::exists(dir / "textures" / p.filename(), ec)) return (dir / "textures" / p.filename()).string();
    }
    if (tex->content.size > 0 && tex->content.data) {
        std::string ext = fs::path(UfbxStr(tex->filename)).extension().string();
        if (ext.empty()) {
            const unsigned char* d = (const unsigned char*)tex->content.data;
            ext = (tex->content.size > 2 && d[0] == 0xFF && d[1] == 0xD8) ? ".jpg" : ".png";
        }
        const fs::path out = dir / (fs::path(modelPath).stem().string() + "_tex" + std::to_string(serial) + ext);
        if (!fs::exists(out, ec)) {
            std::ofstream of(out, std::ios::binary);
            if (!of) return {};
            of.write((const char*)tex->content.data, (std::streamsize)tex->content.size);
        }
        return out.string();
    }
    return {};
}

void ApplyFbxMaterial(MeshRenderer& mr, const ufbx_material* m, const std::string& modelPath, int serial) {
    if (!m) return;
    const ufbx_material_map& bc = m->pbr.base_color.has_value || m->pbr.base_color.texture ? m->pbr.base_color : m->fbx.diffuse_color;
    if (const ufbx_texture* t = bc.texture ? bc.texture : m->fbx.diffuse_color.texture) {
        std::string p = FbxTexturePath(t, modelPath, serial);
        if (!p.empty()) mr.texture = p;
    }
    if (bc.has_value) {
        // With a texture the factor is a tint; Mixamo exports often carry a grey
        // 0.8 diffuse factor that would just darken the texture, so only use it
        // untextured.
        if (mr.texture.empty())
            mr.color = Color((float)bc.value_vec4.x, (float)bc.value_vec4.y, (float)bc.value_vec4.z, 1.0f);
    }
    const ufbx_material_map& nm = m->pbr.normal_map.texture ? m->pbr.normal_map : m->fbx.normal_map;
    if (nm.texture) { std::string p = FbxTexturePath(nm.texture, modelPath, serial + 1000); if (!p.empty()) mr.normalMap = p; }
    if (m->pbr.emission_color.has_value) {
        const float f = m->pbr.emission_factor.has_value ? (float)m->pbr.emission_factor.value_real : 1.0f;
        const ufbx_vec4& e = m->pbr.emission_color.value_vec4;
        if (e.x + e.y + e.z > 0.0) mr.emissive = Color((float)e.x * f, (float)e.y * f, (float)e.z * f, 1.0f);
    }
    if (m->pbr.roughness.has_value || m->pbr.metalness.has_value) {
        const float rough = m->pbr.roughness.has_value ? (float)m->pbr.roughness.value_real : 0.6f;
        const float metal = m->pbr.metalness.has_value ? (float)m->pbr.metalness.value_real : 0.0f;
        const float smooth = 1.0f - std::fmin(1.0f, std::fmax(0.0f, rough));
        mr.metallic = metal;
        mr.shininess = 4.0f + smooth * 124.0f;
        mr.specular = 0.15f + smooth * 0.85f;
        mr.reflectivity = metal * smooth;
    }
}

// One vertex as ufbx_generate_indices dedups it (plain-old-data, compared bytewise).
struct FbxVert {
    float px, py, pz, nx, ny, nz, u, v;
    int   j[4];
    float w[4];
    uint32_t vi;   ///< control point (for blend shape offsets)
};

// Build one material part of a mesh: triangulated, deduplicated, optionally
// with skin influences (indices into the skin's clusters).
Mesh BuildFbxPart(const ufbx_mesh* mesh, const ufbx_mesh_part& part, const ufbx_skin_deformer* skin,
                  std::vector<std::array<int, 4>>* jIdx, std::vector<std::array<float, 4>>* jWt,
                  std::vector<uint32_t>* outVi = nullptr) {
    Mesh out;
    std::vector<uint32_t> tri(mesh->max_face_triangles * 3);
    std::vector<FbxVert> verts;
    verts.reserve(part.num_triangles * 3);
    for (uint32_t fi : part.face_indices) {
        const ufbx_face face = mesh->faces.data[fi];
        const uint32_t nt = ufbx_triangulate_face(tri.data(), tri.size(), mesh, face);
        for (uint32_t k = 0; k < nt * 3; ++k) {
            const uint32_t ix = tri[k];
            FbxVert fv{};
            const ufbx_vec3 p = ufbx_get_vertex_vec3(&mesh->vertex_position, ix);
            fv.px = (float)p.x; fv.py = (float)p.y; fv.pz = (float)p.z;
            if (mesh->vertex_normal.exists) {
                const ufbx_vec3 n = ufbx_get_vertex_vec3(&mesh->vertex_normal, ix);
                fv.nx = (float)n.x; fv.ny = (float)n.y; fv.nz = (float)n.z;
            }
            if (mesh->vertex_uv.exists) {
                const ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, ix);
                fv.u = (float)uv.x; fv.v = 1.0f - (float)uv.y;   // FBX UVs are bottom-up
            }
            fv.w[0] = 1.0f;
            fv.vi = mesh->vertex_indices.data[ix];
            if (skin) {
                const uint32_t vi = fv.vi;
                if (vi < skin->vertices.count) {
                    const ufbx_skin_vertex sv = skin->vertices.data[vi];
                    float sum = 0.0f;
                    fv.w[0] = 0.0f;
                    const uint32_t nw = sv.num_weights < 4 ? sv.num_weights : 4;   // ufbx sorts by weight
                    for (uint32_t wi = 0; wi < nw; ++wi) {
                        const ufbx_skin_weight sw = skin->weights.data[sv.weight_begin + wi];
                        fv.j[wi] = (int)sw.cluster_index; fv.w[wi] = (float)sw.weight; sum += fv.w[wi];
                    }
                    if (sum > 1e-6f) for (float& w : fv.w) w /= sum;   // (no weights: stays at the bind pose)
                }
            }
            verts.push_back(fv);
        }
    }
    if (verts.empty()) return out;
    std::vector<uint32_t> indices(verts.size());
    ufbx_vertex_stream stream{verts.data(), verts.size(), sizeof(FbxVert)};
    ufbx_error e{};
    const size_t nv = ufbx_generate_indices(&stream, 1, indices.data(), indices.size(), nullptr, &e);
    if (e.type != UFBX_ERROR_NONE) return out;
    const bool hasN = mesh->vertex_normal.exists, hasUV = mesh->vertex_uv.exists;
    out.vertices.reserve(nv);
    for (size_t i = 0; i < nv; ++i) {
        const FbxVert& v = verts[i];
        out.vertices.push_back(Vec3{v.px, v.py, v.pz});
        if (hasN) out.normals.push_back(Vec3{v.nx, v.ny, v.nz});
        if (hasUV) out.uvs.push_back(Vec2{v.u, v.v});
        if (jIdx) { jIdx->push_back({v.j[0], v.j[1], v.j[2], v.j[3]}); jWt->push_back({v.w[0], v.w[1], v.w[2], v.w[3]}); }
        if (outVi) outVi->push_back(v.vi);
    }
    out.triangles.reserve(indices.size());
    for (uint32_t ix : indices) out.triangles.push_back((int)ix);
    return out;
}

// The mesh's blend shape channels (every deformer, in order) as BlendShapes on
// `g`, for one built part whose vertices came from control points `vi`. Offsets
// go through `lin` (the geometry-to-node offset for unskinned meshes). An
// unskinned mesh also gets a pass-through skin so the renderers see it change.
void AddFbxBlendShapes(const ufbx_mesh* mesh, const std::vector<uint32_t>& vi, GameObject* g, const Mat4* lin) {
    if (!mesh || !g || mesh->blend_deformers.count == 0) return;
    BlendShapes* bs = nullptr;
    for (const ufbx_blend_deformer* bd : mesh->blend_deformers)
        for (const ufbx_blend_channel* ch : bd->channels) {
            const ufbx_blend_shape* sh = ch->target_shape;
            if (!sh && ch->keyframes.count) sh = ch->keyframes.data[ch->keyframes.count - 1].shape;
            std::vector<Vec3> dp(vi.size(), Vec3{0, 0, 0}), dn;
            bool hasN = sh && sh->normal_offsets.count == sh->num_offsets && sh->num_offsets > 0;
            if (hasN) dn.assign(vi.size(), Vec3{0, 0, 0});
            if (sh) {
                // Control point -> its offset slot, then out to every split vertex.
                std::unordered_map<uint32_t, uint32_t> slot;
                for (size_t k = 0; k < sh->num_offsets && k < sh->offset_vertices.count; ++k) slot[sh->offset_vertices.data[k]] = (uint32_t)k;
                for (size_t v = 0; v < vi.size(); ++v) {
                    auto it = slot.find(vi[v]);
                    if (it == slot.end()) continue;
                    const ufbx_vec3 o = sh->position_offsets.data[it->second];
                    float ow = it->second < sh->offset_weights.count ? (float)sh->offset_weights.data[it->second] : 1.0f;
                    Vec3 d{(float)o.x * ow, (float)o.y * ow, (float)o.z * ow};
                    dp[v] = lin ? lin->MultiplyVector(d) : d;
                    if (hasN) {
                        const ufbx_vec3 n = sh->normal_offsets.data[it->second];
                        Vec3 dnn{(float)n.x * ow, (float)n.y * ow, (float)n.z * ow};
                        dn[v] = lin ? lin->MultiplyVector(dnn) : dnn;
                    }
                }
            }
            if (!bs) bs = g->AddComponent<BlendShapes>();
            std::string nm = UfbxStr(ch->name);
            if (std::size_t dot = nm.find_last_of('.'); dot != std::string::npos && dot + 1 < nm.size()) nm = nm.substr(dot + 1);
            if (nm.empty()) nm = "shape" + std::to_string(bs->shapes.size());
            bs->AddShape(nm, dp, dn, (float)ch->weight);
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

Mesh TransformedMesh(Mesh m, const Mat4& t) {
    for (auto& v : m.vertices) v = t.MultiplyPoint(v);
    for (auto& n : m.normals) { n = t.MultiplyVector(n); const float l = n.Magnitude(); if (l > 1e-8f) n = n * (1.0f / l); }
    return m;
}
bool IsIdentity(const ufbx_matrix& m) {
    const double id[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    const double* v = &m.m00;
    for (int i = 0; i < 12; ++i) if (std::fabs(v[i] - id[i]) > 1e-6) return false;
    return true;
}

} // namespace

// Every mesh flattened into one (world space) — the single-mesh ImportModel path.
static Mesh ImportFbxMerged(const std::string& path, bool* ok, std::string* outTexture) {
    Mesh merged;
    if (fbx::OwnReaderEnabled()) {   // our reader first (opt-in); ufbx below if it could not read the file
        bool ownOk = false;
        Mesh m = ImportFbxMergedOwn(path, &ownOk, outTexture);
        if (ownOk) { if (ok) *ok = true; return m; }
    }
    ufbx_scene* sc = LoadFbx(path, nullptr);
    if (!sc) { if (ok) *ok = false; return merged; }
    for (const ufbx_node* node : sc->nodes) {
        if (!node->mesh) continue;
        const Mat4 g2w = UfbxToMat4(node->geometry_to_world);
        for (const ufbx_mesh_part& part : node->mesh->material_parts) {
            Mesh m = TransformedMesh(BuildFbxPart(node->mesh, part, nullptr, nullptr, nullptr), g2w);
            const int base = (int)merged.vertices.size();
            const bool keepN = merged.normals.size() == merged.vertices.size() && m.normals.size() == m.vertices.size();
            const bool keepUV = merged.uvs.size() == merged.vertices.size() && m.uvs.size() == m.vertices.size();
            merged.vertices.insert(merged.vertices.end(), m.vertices.begin(), m.vertices.end());
            if (keepN) merged.normals.insert(merged.normals.end(), m.normals.begin(), m.normals.end()); else merged.normals.clear();
            if (keepUV) merged.uvs.insert(merged.uvs.end(), m.uvs.begin(), m.uvs.end()); else merged.uvs.clear();
            for (int t : m.triangles) merged.triangles.push_back(base + t);
            if (outTexture && outTexture->empty() && part.index < node->mesh->materials.count) {
                MeshRenderer tmp; ApplyFbxMaterial(tmp, node->mesh->materials.data[part.index], path, 0);
                *outTexture = tmp.texture;
            }
        }
    }
    ufbx_free_scene(sc);
    if (ok) *ok = !merged.vertices.empty();
    return merged;
}

// `rejected` is set when the file was refused for going past a size/shape limit (fbx::Limits): the
// caller must then fail the import instead of falling back to another reader that would take the
// same hostile file.
static GameObject* ImportFbxSceneGraph(Scene& scene, const std::string& path, bool* ok, std::string* err, bool* rejected) {
    if (fbx::OwnReaderEnabled())
        if (GameObject* r = ImportFbxSceneGraphOwn(scene, path, ok, err)) return r;
    ufbx_scene* sc = LoadFbx(path, err, rejected);
    if (!sc) { if (ok) *ok = false; return nullptr; }

    GameObject* root = scene.CreateGameObject(BaseName(path));
    std::vector<GameObject*> goOf(sc->nodes.count, nullptr);   // by node typed_id
    std::unordered_map<std::string, int> nameUse;
    int texSerial = 0;

    // Hierarchy: one GameObject per FBX node (names made unique: clips and
    // skins refer to nodes by name). Iterative (explicit stack, depth-first in child order),
    // so the depth of the file never becomes call depth.
    {
        std::vector<std::pair<const ufbx_node*, Transform*>> todo;
        for (std::size_t i = sc->root_node->children.count; i-- > 0;) todo.push_back({sc->root_node->children.data[i], root->transform});
        while (!todo.empty()) {
            const ufbx_node* n = todo.back().first; Transform* parent = todo.back().second; todo.pop_back();
            std::string nm = UfbxStr(n->name);
            if (nm.empty()) nm = n->mesh ? "mesh" : "node";
            if (int c = nameUse[nm]++) nm += "_" + std::to_string(c);
            GameObject* g = scene.CreateGameObject(nm);
            goOf[n->typed_id] = g;
            const ufbx_transform& t = n->local_transform;
            g->transform->localPosition = Vec3{(float)t.translation.x, (float)t.translation.y, (float)t.translation.z};
            g->transform->localRotation = Quat{(float)t.rotation.x, (float)t.rotation.y, (float)t.rotation.z, (float)t.rotation.w};
            g->transform->localScale    = Vec3{(float)t.scale.x, (float)t.scale.y, (float)t.scale.z};
            g->transform->SetParent(parent, false);
            for (std::size_t i = n->children.count; i-- > 0;) todo.push_back({n->children.data[i], g->transform});
        }
    }

    // Meshes (after the hierarchy, so skins can find every bone).
    for (const ufbx_node* node : sc->nodes) {
        if (!node->mesh || !goOf[node->typed_id]) continue;
        const ufbx_mesh* mesh = node->mesh;
        GameObject* host = goOf[node->typed_id];
        const ufbx_skin_deformer* skin = mesh->skin_deformers.count ? mesh->skin_deformers.data[0] : nullptr;
        if (skin && skin->clusters.count == 0) skin = nullptr;
        const bool geomOffset = !IsIdentity(node->geometry_to_node);
        const Mat4 g2n = UfbxToMat4(node->geometry_to_node);
        const size_t parts = mesh->material_parts.count;
        int made = 0;
        for (size_t pi = 0; pi < parts; ++pi) {
            const ufbx_mesh_part& part = mesh->material_parts.data[pi];
            if (part.num_triangles == 0) continue;
            std::vector<std::array<int, 4>> jIdx; std::vector<std::array<float, 4>> jWt;
            std::vector<uint32_t> vi;
            Mesh bind = BuildFbxPart(mesh, part, skin, skin ? &jIdx : nullptr, skin ? &jWt : nullptr,
                                     mesh->blend_deformers.count ? &vi : nullptr);
            if (bind.vertices.empty()) continue;
            GameObject* g = host;
            if (made++ > 0 || (parts > 1)) {   // one child per material when there are several
                const ufbx_material* mat = part.index < mesh->materials.count ? mesh->materials.data[part.index] : nullptr;
                g = scene.CreateGameObject(host->name + "_" + (mat ? UfbxStr(mat->name) : ("mat" + std::to_string(pi))));
                g->transform->SetParent(host->transform, false);
            }
            auto* mr = g->AddComponent<MeshRenderer>();
            mr->doubleSided = true;
            ApplyFbxMaterial(*mr, part.index < mesh->materials.count ? mesh->materials.data[part.index] : nullptr, path, texSerial);
            texSerial += 2;
            if (!skin) {
                mr->mesh = geomOffset ? TransformedMesh(bind, g2n) : bind;
                AddFbxBlendShapes(mesh, vi, g, geomOffset ? &g2n : nullptr);
                continue;
            }
            // Skinned: vertices stay in geometry space; each cluster's
            // geometry_to_bone is its inverse bind. The renderer shows the
            // bind pose (in node space) until the first Skin().
            mr->mesh = geomOffset ? TransformedMesh(bind, g2n) : bind;
            auto* sm = g->AddComponent<SkinnedMesh>();
            sm->bind = bind; sm->jointIdx = std::move(jIdx); sm->jointWt = std::move(jWt);
            for (const ufbx_skin_cluster* cl : skin->clusters) {
                GameObject* bg = cl->bone_node ? goOf[cl->bone_node->typed_id] : nullptr;
                sm->joints.push_back(bg ? bg->transform : nullptr);
                sm->jointNames.push_back(bg ? bg->name : std::string());
                // geometry_to_bone maps the SKINNED geometry (this part's mesh,
                // which may sit under a child object) into bone space.
                sm->inverseBind.push_back(UfbxToMat4(cl->geometry_to_bone));
            }
            if (g != host) {   // the part's object is a child of the mesh node: same space
                g->transform->localPosition = Vec3{0, 0, 0};
            }
            AddFbxBlendShapes(mesh, vi, g, nullptr);
        }
    }

    // Animation takes -> a ModelAnimator clip library on the root.
    // Blend channel element -> (mesh node name, shape index), for weight tracks.
    std::unordered_map<uint32_t, std::pair<std::string, int>> chanOf;
    for (const ufbx_node* node : sc->nodes) {
        if (!node->mesh || !goOf[node->typed_id]) continue;
        int k = 0;
        for (const ufbx_blend_deformer* bd : node->mesh->blend_deformers)
            for (const ufbx_blend_channel* ch : bd->channels)
                chanOf.emplace(ch->element_id, std::make_pair(goOf[node->typed_id]->name, k++));
    }
    if (sc->anim_stacks.count > 0) {
        auto* ma = root->AddComponent<ModelAnimator>();
        // Whole-file bake budget (a hostile file can point any number of stacks at the same curves):
        // baked keys over all stacks, and each bake may not allocate more than what is left.
        const std::size_t bakeKeyBudget = fbx::Limits().maxTotalBakedSamples;
        const std::size_t kBakedKeyBytes = 40;   // sizeof(ufbx_baked_quat), the largest baked key
        std::size_t bakedKeys = 0;
        for (const ufbx_anim_stack* st : sc->anim_stacks) {
            ufbx_bake_opts bo{};
            bo.trim_start_time = true;
            bo.resample_rate = 30.0;
            bo.result_allocator.memory_limit = std::max<std::size_t>(1, (bakeKeyBudget - bakedKeys) * kBakedKeyBytes);   // 0 would mean unlimited
            ufbx_error be{};
            ufbx_baked_anim* bake = ufbx_bake_anim(sc, st->anim, &bo, &be);
            if (!bake) {
                if (be.type == UFBX_ERROR_MEMORY_LIMIT || be.type == UFBX_ERROR_ALLOCATION_LIMIT) {
                    if (err) *err = "FBX rejected: animation bake budget exceeded";
                    if (rejected) *rejected = true;
                    ufbx_free_scene(sc);
                    scene.Destroy(root);
                    if (ok) *ok = false;
                    return nullptr;
                }
                continue;
            }
            for (const ufbx_baked_node& bn : bake->nodes) bakedKeys += bn.translation_keys.count + bn.rotation_keys.count + bn.scale_keys.count;
            for (const ufbx_baked_element& be3 : bake->elements) for (const ufbx_baked_prop& bp : be3.props) bakedKeys += bp.keys.count;
            if (bakedKeys > bakeKeyBudget) {
                ufbx_free_baked_anim(bake);
                if (err) *err = "FBX rejected: animation bake budget exceeded";
                if (rejected) *rejected = true;
                ufbx_free_scene(sc);
                scene.Destroy(root);
                if (ok) *ok = false;
                return nullptr;
            }
            ModelAnimator::Clip clip;
            std::string cn = UfbxStr(st->name);
            { std::size_t bar = cn.find_last_of('|'); if (bar != std::string::npos) cn = cn.substr(bar + 1); }
            if (cn.empty() || cn == "mixamo.com" || cn[0] == '(' || cn.rfind("Take ", 0) == 0 || cn.rfind("Scene", 0) == 0) {
                cn = std::filesystem::path(path).stem().string();   // Mixamo: the file name says what it is
                if (sc->anim_stacks.count > 1) cn += "_" + std::to_string(ma->clips.size());
            }
            clip.name = cn;
            for (const ufbx_baked_node& bn : bake->nodes) {
                if (bn.typed_id >= goOf.size() || !goOf[bn.typed_id]) continue;
                clip.nodes.push_back({goOf[bn.typed_id]->name, AnimationClip{}});
                AnimationClip& ac = clip.nodes.back().clip;
                ac.smoothKeys = false;   // baked for linear playback
                for (const ufbx_baked_vec3& k : bn.translation_keys) {
                    const float t = (float)k.time;
                    ac.AddKey("position.x", t, (float)k.value.x); ac.AddKey("position.y", t, (float)k.value.y); ac.AddKey("position.z", t, (float)k.value.z);
                }
                ufbx_quat prev{0, 0, 0, 1}; bool hasPrev = false;
                for (const ufbx_baked_quat& k : bn.rotation_keys) {
                    ufbx_quat q = k.value;
                    if (hasPrev && q.x * prev.x + q.y * prev.y + q.z * prev.z + q.w * prev.w < 0.0) { q.x = -q.x; q.y = -q.y; q.z = -q.z; q.w = -q.w; }
                    prev = q; hasPrev = true;
                    const float t = (float)k.time;
                    ac.AddKey("rotation.qx", t, (float)q.x); ac.AddKey("rotation.qy", t, (float)q.y);
                    ac.AddKey("rotation.qz", t, (float)q.z); ac.AddKey("rotation.qw", t, (float)q.w);
                }
                for (const ufbx_baked_vec3& k : bn.scale_keys) {
                    const float t = (float)k.time;
                    ac.AddKey("scale.x", t, (float)k.value.x); ac.AddKey("scale.y", t, (float)k.value.y); ac.AddKey("scale.z", t, (float)k.value.z);
                }
                if (ac.Tracks().empty()) clip.nodes.pop_back();
            }
            // Blend shape channels' DeformPercent (0..100) -> "weights.<i>" on the mesh node.
            for (const ufbx_baked_element& be2 : bake->elements) {
                auto it = chanOf.find(be2.element_id);
                if (it == chanOf.end()) continue;
                for (const ufbx_baked_prop& bp : be2.props) {
                    if (UfbxStr(bp.name) != "DeformPercent" || bp.keys.count == 0) continue;
                    ModelAnimator::NodeClip* ncp = nullptr;
                    for (auto& ncx : clip.nodes) if (ncx.node == it->second.first) { ncp = &ncx; break; }
                    if (!ncp) { clip.nodes.push_back({it->second.first, AnimationClip{}}); ncp = &clip.nodes.back(); ncp->clip.smoothKeys = false; }
                    const std::string track = "weights." + std::to_string(it->second.second);
                    for (const ufbx_baked_vec3& k : bp.keys) ncp->clip.AddKey(track, (float)k.time, (float)k.value.x * 0.01f);
                }
            }
            ufbx_free_baked_anim(bake);
            if (!clip.nodes.empty()) ma->clips.push_back(std::move(clip));
        }
        if (ma->clips.empty()) root->RemoveComponent<ModelAnimator>();
        else { ma->active = 0; ma->autoPlay = true; }
    }

    ufbx_free_scene(sc);
    NormalizeImportedClips(root);
    AutoNormalizeImportScale(scene, root);
    if (ok) *ok = true;
    return root;
}

// ============================================================================
// FBX via OUR reader (okay::fbx, Render/FbxReader). Side by side with the ufbx
// path above and OFF by default: selected by -DOKAY_OWN_FBX=ON, the OKAY_OWN_FBX
// environment variable, or okay::fbx::SetOwnReaderEnabled(true). It produces the
// same scene data (hierarchy, per-material meshes, skins, blend shapes, baked
// clips) so the rest of the engine does not care which reader ran. On failure
// the ufbx path runs instead.
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
    const fs::path dir = fs::path(modelPath).parent_path();
    for (const std::string* s : {&tex->fileName, &tex->relativeFileName}) {
        if (s->empty()) continue;
        fs::path p(*s);
        if (fs::exists(p, ec)) return p.string();
        if (fs::exists(dir / p, ec)) return (dir / p).string();
        // Windows paths in the file ("C:\art\x.png") on a POSIX host: use the part after the last separator.
        std::string base = *s; const std::size_t sl = base.find_last_of("/\\"); if (sl != std::string::npos) base = base.substr(sl + 1);
        if (!base.empty()) {
            if (fs::exists(dir / base, ec)) return (dir / base).string();
            if (fs::exists(dir / "textures" / base, ec)) return (dir / "textures" / base).string();
        }
    }
    if (!tex->content.empty()) {
        std::string ext = fs::path(tex->fileName).extension().string();
        if (ext.empty()) ext = (tex->content.size() > 2 && tex->content[0] == 0xFF && tex->content[1] == 0xD8) ? ".jpg" : ".png";
        const fs::path out = dir / (fs::path(modelPath).stem().string() + "_tex" + std::to_string(serial) + ext);
        if (!fs::exists(out, ec)) {
            std::ofstream of(out, std::ios::binary);
            if (!of) return {};
            of.write((const char*)tex->content.data(), (std::streamsize)tex->content.size());
        }
        return out.string();
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

// One material part: deduplicated vertices + triangle list (first occurrence order, like the ufbx path).
Mesh BuildOwnPart(const fbx::Mesh& mesh, const fbx::Part& part, bool skinned,
                  std::vector<std::array<int, 4>>* jIdx, std::vector<std::array<float, 4>>* jWt,
                  std::vector<uint32_t>* outVi) {
    Mesh out;
    std::vector<FbxVert> verts;
    verts.reserve(part.corners.size());
    for (const fbx::Corner& c : part.corners) {
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
    if (verts.empty()) return out;
    std::unordered_map<std::string, uint32_t> seen;
    seen.reserve(verts.size());
    std::vector<uint32_t> indices(verts.size());
    std::vector<uint32_t> unique;
    for (std::size_t i = 0; i < verts.size(); ++i) {
        auto ins = seen.emplace(std::string((const char*)&verts[i], sizeof(FbxVert)), (uint32_t)unique.size());
        if (ins.second) unique.push_back((uint32_t)i);
        indices[i] = ins.first->second;
    }
    const bool hasN = mesh.hasNormals, hasUV = mesh.hasUV;
    for (uint32_t u : unique) {
        const FbxVert& v = verts[u];
        out.vertices.push_back(Vec3{v.px, v.py, v.pz});
        if (hasN) out.normals.push_back(Vec3{v.nx, v.ny, v.nz});
        if (hasUV) out.uvs.push_back(Vec2{v.u, v.v});
        if (jIdx) { jIdx->push_back({v.j[0], v.j[1], v.j[2], v.j[3]}); jWt->push_back({v.w[0], v.w[1], v.w[2], v.w[3]}); }
        if (outVi) outVi->push_back(v.vi);
    }
    for (uint32_t ix : indices) out.triangles.push_back((int)ix);
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

static Mesh ImportFbxMergedOwn(const std::string& path, bool* ok, std::string* outTexture) {
    Mesh merged;
    fbx::Scene sc; std::string err;
    if (!fbx::LoadFile(path, sc, &err)) { if (ok) *ok = false; return merged; }
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
    return merged;
}

static GameObject* ImportFbxSceneGraphOwn(Scene& scene, const std::string& path, bool* ok, std::string* err) {
    fbx::Scene sc;
    if (!fbx::LoadFile(path, sc, err)) { if (ok) *ok = false; return nullptr; }
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
        for (const fbx::Clip& fc : sc.clips) {
            ModelAnimator::Clip clip;
            std::string cn = fc.name;
            { std::size_t bar = cn.find_last_of('|'); if (bar != std::string::npos) cn = cn.substr(bar + 1); }
            if (cn.empty() || cn == "mixamo.com" || cn[0] == '(' || cn.rfind("Take ", 0) == 0 || cn.rfind("Scene", 0) == 0) {
                cn = std::filesystem::path(path).stem().string();
                if (sc.clips.size() > 1) cn += "_" + std::to_string(ma->clips.size());
            }
            clip.name = cn;
            for (const fbx::NodeAnim& bn : fc.nodes) {
                if (bn.node < 0 || (std::size_t)bn.node >= goOf.size() || !goOf[(std::size_t)bn.node]) continue;
                clip.nodes.push_back({goOf[(std::size_t)bn.node]->name, AnimationClip{}});
                AnimationClip& ac = clip.nodes.back().clip;
                ac.smoothKeys = false;
                for (const fbx::Key3& k : bn.translation) {
                    const float t = (float)k.time;
                    ac.AddKey("position.x", t, (float)k.v[0]); ac.AddKey("position.y", t, (float)k.v[1]); ac.AddKey("position.z", t, (float)k.v[2]);
                }
                double prev[4] = {0, 0, 0, 1}; bool hasPrev = false;
                for (const fbx::Key4& k : bn.rotation) {
                    double q[4] = {k.v[0], k.v[1], k.v[2], k.v[3]};
                    if (hasPrev && q[0] * prev[0] + q[1] * prev[1] + q[2] * prev[2] + q[3] * prev[3] < 0.0) for (double& c : q) c = -c;
                    for (int a = 0; a < 4; ++a) prev[a] = q[a];
                    hasPrev = true;
                    const float t = (float)k.time;
                    ac.AddKey("rotation.qx", t, (float)q[0]); ac.AddKey("rotation.qy", t, (float)q[1]);
                    ac.AddKey("rotation.qz", t, (float)q[2]); ac.AddKey("rotation.qw", t, (float)q[3]);
                }
                for (const fbx::Key3& k : bn.scale) {
                    const float t = (float)k.time;
                    ac.AddKey("scale.x", t, (float)k.v[0]); ac.AddKey("scale.y", t, (float)k.v[1]); ac.AddKey("scale.z", t, (float)k.v[2]);
                }
                if (ac.Tracks().empty()) clip.nodes.pop_back();
            }
            for (const fbx::BlendAnim& ba : fc.blends) {
                auto it = chanOf.find((long long)ba.mesh * 100000 + ba.channel);
                if (it == chanOf.end() || ba.percent.empty()) continue;
                ModelAnimator::NodeClip* ncp = nullptr;
                for (auto& ncx : clip.nodes) if (ncx.node == it->second.first) { ncp = &ncx; break; }
                if (!ncp) { clip.nodes.push_back({it->second.first, AnimationClip{}}); ncp = &clip.nodes.back(); ncp->clip.smoothKeys = false; }
                const std::string track = "weights." + std::to_string(it->second.second);
                for (const fbx::Key3& k : ba.percent) ncp->clip.AddKey(track, (float)k.time, (float)k.v[0] * 0.01f);
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

    // FBX: built in (ufbx), full hierarchy + skins + animations. Assimp (when
    // compiled in) is the fallback for files ufbx can't read.
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

    GltfDoc doc = LoadDoc(path);
    if (!doc.ok) { if (ok) *ok = false; return nullptr; }
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
        if (ok) *ok = okm;
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
    NormalizeImportedClips(root);
    AutoNormalizeImportScale(scene, root);
    if (ok) *ok = true;
    return root;
}

// The model's own import settings (`<model>.okaymeta`): Normals / Smoothing angle / Import animations.
// Rigged (skinned) parts and meshes with blend shapes keep the file's normals: their vertices are shared with the
// skin weights and the shapes, so they are not rebuilt.
GameObject* ImportModelScene(Scene& scene, const std::string& path, bool* ok) {
    GameObject* root = ImportModelSceneRaw(scene, path, ok);
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

// ---- Import report ----------------------------------------------------------
ModelReport AnalyzeModel(Scene& scene, GameObject* root) {
    ModelReport r;
    if (!root || !root->transform) return r;
    std::vector<GameObject*> sub;
    for (const auto& up : scene.Objects())
        if (up && up->IsSelfOrDescendantOf(root)) sub.push_back(up.get());
    r.objects = (int)sub.size();
    std::set<Transform*> bones;
    for (GameObject* g : sub) {
        if (auto* mr = g->GetComponent<MeshRenderer>()) {
            ++r.meshes;
            r.vertices += (int)mr->mesh.vertices.size();
            r.triangles += (int)mr->mesh.triangles.size() / 3;
        }
        auto* sm = g->GetComponent<SkinnedMesh>();
        // A morph-only mesh's pass-through skin (its one joint is itself) isn't a rig.
        if (sm && sm->joints.size() == 1 && sm->joints[0] == g->transform) sm = nullptr;
        if (sm) {
            ++r.skinnedMeshes;
            sm->ResolveJoints();
            for (Transform* t : sm->joints) if (t) bones.insert(t);
            r.missingBones += sm->MissingJoints();
            r.unweightedVertices += sm->UnweightedVertices();
            r.maxInfluences = std::max(r.maxInfluences, sm->MaxInfluences());
        }
        if (auto* bs = g->GetComponent<BlendShapes>()) r.blendShapes += (int)bs->shapes.size();
    }
    r.bones = (int)bones.size();
    // Skeleton roots: bones whose parent isn't a bone. One connected skeleton
    // has exactly one (the hips / root bone).
    for (Transform* b : bones) {
        Transform* p = b->Parent();
        if (!p || !bones.count(p)) {
            ++r.skeletonRoots;
            if (r.rootBone.empty() && b->gameObject) r.rootBone = b->gameObject->name;
        }
        if (b->gameObject && !b->gameObject->IsSelfOrDescendantOf(root))
            r.warnings.push_back("bone '" + b->gameObject->name + "' sits outside the model");
    }
    if (auto* ma = root->GetComponent<ModelAnimator>()) {
        r.clips = ma->ClipCount();
        for (const auto& c : ma->clips) for (const auto& nc : c.nodes) r.keys += nc.clip.KeyCount();
        r.keysBeforeReduction = ma->lastOptimizeBefore;
        r.clipNames = ma->ClipNames();
    }
    if (r.missingBones)        r.warnings.push_back(std::to_string(r.missingBones) + " bone(s) referenced by a skin weren't found");
    if (r.unweightedVertices)  r.warnings.push_back(std::to_string(r.unweightedVertices) + " vertices have no bone weights (they won't move)");
    if (r.skeletonRoots > 1)   r.warnings.push_back("the skeleton is split into " + std::to_string(r.skeletonRoots) + " separate bone trees");
    if (r.skinnedMeshes == 0 && r.blendShapes == 0 && r.clips > 0 && r.meshes > 0)
        r.warnings.push_back("animations but no skin: parts move rigidly (no mesh deformation)");
    return r;
}

std::string ModelReport::Summary() const {
    std::string s = std::to_string(meshes) + " mesh" + (meshes == 1 ? "" : "es") + ", " +
                    std::to_string(vertices) + " verts";
    if (skinnedMeshes)
        s += ", skinned (" + std::to_string(bones) + " bones" +
             (rootBone.empty() ? "" : ", root '" + rootBone + "'") +
             ", up to " + std::to_string(maxInfluences) + " weights/vertex)";
    if (clips) {
        s += ", " + std::to_string(clips) + " clip" + (clips == 1 ? "" : "s") + ":";
        for (int i = 0; i < (int)clipNames.size() && i < 5; ++i) s += (i ? ", " : " ") + clipNames[i];
        if ((int)clipNames.size() > 5) s += ", ...";
        if (keysBeforeReduction > keys && keysBeforeReduction > 0)
            s += " (" + std::to_string(keys) + " keys, " +
                 std::to_string((int)std::lround(100.0 * (double)(keysBeforeReduction - keys) / (double)keysBeforeReduction)) +
                 "% removed as redundant)";
    }
    if (blendShapes) s += ", " + std::to_string(blendShapes) + " blend shape" + (blendShapes == 1 ? "" : "s");
    return s;
}

// ---- Adding clips from another file (Mixamo animation downloads) ----------
// "mixamorig9:LeftUpLeg" / "Armature|LeftUpLeg" / "Left_Up_Leg" -> "leftupleg".
static std::string NormBoneName(const std::string& n) {
    std::string s = n;
    const std::size_t cut = s.find_last_of(":|");
    if (cut != std::string::npos) s = s.substr(cut + 1);
    std::string o;
    for (char c : s) if (std::isalnum((unsigned char)c)) o += (char)std::tolower((unsigned char)c);
    if (o.rfind("mixamorig", 0) == 0) o = o.substr(9);
    return o;
}
static GameObject* FindHips(GameObject* root, const std::vector<GameObject*>& sub) {
    for (GameObject* g : sub) { std::string k = NormBoneName(g->name); if (k == "hips" || k == "pelvis") return g; }
    for (GameObject* g : sub) { std::string k = NormBoneName(g->name); if (k.find("hips") != std::string::npos || k.find("pelvis") != std::string::npos) return g; }
    (void)root; return nullptr;
}
static std::vector<GameObject*> Subtree(Scene& scene, GameObject* root) {
    std::vector<GameObject*> out;
    for (const auto& up : scene.Objects()) if (up->IsSelfOrDescendantOf(root)) out.push_back(up.get());
    return out;
}
// Hips height above the model root, in the root's own (unscaled) space.
static float RestHipsHeight(GameObject* root, GameObject* hips) {
    if (!root || !hips) return 0.0f;
    Vec3 p = root->transform->LocalToWorldMatrix().Inverse().MultiplyPoint(hips->transform->Position());
    return std::fabs(p.y);
}
// Lower-case words of a clip name, split at separators, digits and camelCase,
// after any "Armature|" prefix: "Armature|RunningJump_2" -> running, jump.
static std::vector<std::string> ClipNameWords(const std::string& raw) {
    std::size_t cut = raw.find_last_of('|');
    std::vector<std::string> out;
    std::string cur;
    char prev = 0;
    for (char c : raw.substr(cut == std::string::npos ? 0 : cut + 1)) {
        bool alpha = std::isalpha((unsigned char)c) != 0;
        if (!alpha || (std::isupper((unsigned char)c) && std::islower((unsigned char)prev))) {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        }
        if (alpha) cur += (char)std::tolower((unsigned char)c);
        prev = c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// Map idle/walk/run/jump/fall/land and the crouch / prone clips from the clip
// names, where not set yet.
void AutoMapLocomotion(ModelAnimator* ma) {
    if (!ma) return;
    // By whole words of the clip name, an exact name first ("Walk" before
    // "Walk Back"): "Stairs" isn't "air", and a "Running Jump" is a jump.
    static const std::vector<std::string> kWords[6] = {
        {"idle", "stand", "standing", "breathing"}, {"walk", "walking"},
        {"run", "running", "sprint", "sprinting", "jog", "jogging"},
        {"jump", "jumping", "leap"}, {"fall", "falling", "air", "inair", "drop"}, {"land", "landing"}};
    std::string* slot[6] = {&ma->idleClip, &ma->walkClip, &ma->runClip, &ma->jumpClip, &ma->fallClip, &ma->landClip};
    std::string* stanceSlot[5] = {&ma->crouchClip, &ma->crouchWalkClip, &ma->proneClip, &ma->crawlClip, &ma->getUpClip};
    std::string* dirSlot[4] = {&ma->strafeLeftClip, &ma->strafeRightClip, &ma->walkBackClip, &ma->runBackClip};
    auto is = [&](int role, const std::string& w) {
        for (const std::string& k : kWords[role]) if (k == w) return true;
        return false;
    };
    auto any = [](const std::vector<std::string>& ws, std::initializer_list<const char*> ks) {
        for (const std::string& w : ws) for (const char* k : ks) if (w == k) return true;
        return false;
    };
    std::vector<std::vector<std::string>> words;
    for (const auto& c : ma->clips) words.push_back(ClipNameWords(c.name));
    std::vector<bool> used(ma->clips.size(), false);
    for (std::size_t ci = 0; ci < ma->clips.size(); ++ci) {   // clips already given a state
        for (const std::string* sl : slot) if (*sl == ma->clips[ci].name) used[ci] = true;
        for (const std::string* sl : stanceSlot) if (*sl == ma->clips[ci].name) used[ci] = true;
        for (const std::string* sl : dirSlot) if (*sl == ma->clips[ci].name) used[ci] = true;
    }
    // Crouch / prone clips, and changes between stances ("Crouch To Stand", "Get Up").
    auto crouchy = [&](std::size_t ci) { return any(words[ci], {"crouch", "crouched", "crouching", "sneak", "sneaking", "squat", "squatting"}); };
    auto proney  = [&](std::size_t ci) { return any(words[ci], {"prone", "lying", "crawl", "crawling"}); };
    auto change  = [&](std::size_t ci) { return any(words[ci], {"to", "up", "down"}); };
    auto moving  = [&](std::size_t ci) { return any(words[ci], {"walk", "walking", "move", "moving", "forward", "run", "running"}); };
    auto crawl   = [&](std::size_t ci) { return any(words[ci], {"crawl", "crawling"}); };
    auto getUp   = [&](std::size_t ci) {
        return (any(words[ci], {"up"}) && any(words[ci], {"get", "getting", "stand", "standing"})) ||
               (proney(ci) && any(words[ci], {"to"}) && any(words[ci], {"stand", "standing", "idle"}));
    };
    // Back / left / right clips are kept out of the forward slots, unless the model turns
    // out to have no directional clips: then it maps exactly as it did before they existed.
    auto mapForward = [&](bool skipDir) {
    for (int pass = 0; pass < 2; ++pass)
        for (int role = 0; role < 6; ++role)
            for (std::size_t ci = 0; ci < ma->clips.size() && slot[role]->empty(); ++ci) {
                if (used[ci]) continue;
                bool hit = false, air = false;
                if (pass == 0) {
                    std::string whole;
                    for (const std::string& w : words[ci]) whole += w;
                    hit = is(role, whole);
                } else {
                    for (const std::string& w : words[ci]) {
                        hit = hit || is(role, w);
                        air = air || is(3, w) || is(4, w) || is(5, w);
                    }
                }
                // A crouch walk, a drop to prone or a get-up is a stance clip (below).
                const bool stance = crouchy(ci) || proney(ci) || getUp(ci);
                const bool backward = any(words[ci], {"back", "backward", "backwards"});
                const bool sideways = any(words[ci], {"left", "right", "strafe", "strafing"});
                if (hit && !stance && !(skipDir && (backward || sideways)) && !(role < 3 && air)) { *slot[role] = ma->clips[ci].name; used[ci] = true; }
            }
    };
    mapForward(true);
    const std::function<bool(std::size_t)> roles[5] = {
        [&](std::size_t ci) { return crouchy(ci) && !change(ci) && !moving(ci) && !any(words[ci], {"sneak", "sneaking"}); },
        [&](std::size_t ci) { return crouchy(ci) && !change(ci) && (moving(ci) || any(words[ci], {"sneak", "sneaking"})); },
        [&](std::size_t ci) { return proney(ci) && !change(ci) && !crawl(ci) && !moving(ci); },
        [&](std::size_t ci) { return (crawl(ci) || (proney(ci) && moving(ci))) && !change(ci); },
        getUp};
    for (int role = 0; role < 5; ++role)
        for (std::size_t ci = 0; ci < ma->clips.size() && stanceSlot[role]->empty(); ++ci)
            if (!used[ci] && roles[role](ci)) { *stanceSlot[role] = ma->clips[ci].name; used[ci] = true; }
    // "Walk Back" / "Run Back" are locomotion. "Back Flip" and similar tricks are not.
    auto locomotes = [&](std::size_t ci) { return any(words[ci], {"walk", "walking", "run", "running", "strafe", "strafing", "move", "moving"}); };
    auto trick = [&](std::size_t ci) { return any(words[ci], {"flip", "flipping", "dodge", "roll", "rolling", "attack", "kick", "punch"}); };
    auto back = [&](std::size_t ci) {
        return any(words[ci], {"back", "backward", "backwards"}) && locomotes(ci) && !trick(ci);
    };
    auto left = [&](std::size_t ci) { return any(words[ci], {"left"}); };
    auto right = [&](std::size_t ci) { return any(words[ci], {"right"}); };
    auto runny = [&](std::size_t ci) { return any(words[ci], {"run", "running", "sprint", "sprinting"}); };
    auto side = [&](std::size_t ci) { return any(words[ci], {"strafe", "strafing", "sidestep", "side", "walk", "walking"}); };
    const std::function<bool(std::size_t)> dirRoles[4] = {
        [&](std::size_t ci) { return left(ci) && side(ci) && !back(ci); },
        [&](std::size_t ci) { return right(ci) && side(ci) && !back(ci); },
        [&](std::size_t ci) { return back(ci) && !runny(ci); },
        [&](std::size_t ci) { return back(ci) && runny(ci); },
    };
    for (int role = 0; role < 4; ++role)
        for (std::size_t ci = 0; ci < ma->clips.size() && dirSlot[role]->empty(); ++ci)
            if (!used[ci] && dirRoles[role](ci)) { *dirSlot[role] = ma->clips[ci].name; used[ci] = true; }
    if (!ma->HasDirectionalClips()) mapForward(false);
}

std::string SuggestUnassignedSlot(const ModelAnimator& ma, int slot) {
    if (slot < 0 || slot >= ModelAnimator::SlotCount) return {};
    if (!ma.SlotClip(slot).empty()) return {};
    ModelAnimator scratch;
    scratch.clips.resize(ma.clips.size());
    for (std::size_t i = 0; i < ma.clips.size(); ++i) scratch.clips[i].name = ma.clips[i].name;
    for (int s = 0; s < ModelAnimator::SlotCount; ++s) scratch.SlotClip(s) = ma.SlotClip(s);
    AutoMapLocomotion(&scratch);
    return scratch.SlotClip(slot);
}

// Switch a retargeted model's locomotion between its clips and the built-in
// animation to match which slots have a clip: none = pure built-in (the
// HumanoidRetarget owns every frame), any = clips drive the slots they fill and
// the built-in animation covers the rest. Inert for a model that isn't retargeted.
void SettleCharacterLocomotion(ModelAnimator* ma) {
    if (!ma || !ma->BuiltinRigPresent()) return;
    bool any = false;
    for (int s = 0; s < ModelAnimator::SlotCount; ++s) any = any || ma->SlotFilled(s);
    if (any && !ma->driveByMovement) ma->smoothLocomotion = true;
    ma->driveByMovement = any;
    ma->inPlace = true;     // the controller moves the capsule: clips only cycle the limbs
    ma->autoPlay = false;   // the slots decide what plays (a clip 0 on the first frame would show the wrong pose)
}

// "idle='Idle' walk='Walking'" - the clip names in the slots a model's clips fill.
static std::string SlotSummary(const ModelAnimator* ma) {
    std::string out;
    if (!ma) return out;
    for (int s = 0; s < ModelAnimator::SlotCount; ++s) {
        if (!ma->SlotFilled(s)) continue;
        std::string label = ModelAnimator::SlotName(s);
        for (char& c : label) c = (char)std::tolower((unsigned char)c);
        out += (out.empty() ? "" : " ") + label + "='" + ma->SlotClip(s) + "'";
    }
    return out;
}

// Put the built-in animation on the model under `root` (the player's Character in
// drive-external mode + a HumanoidRetarget wired to the bones). Returns how many
// of the 15 bones matched, or -1 when the player has no Character.
static int EnableBuiltinFallback(Scene& scene, GameObject* player, GameObject* root) {
    auto* ch = player->GetComponent<Character>();
    if (!ch) return -1;
    HumanoidRetarget* rt = player->GetComponent<HumanoidRetarget>();
    if (!rt) rt = player->AddComponent<HumanoidRetarget>();
    rt->Rewire();
    const int found = rt->DetectAndWire(scene, root);
    ch->driveExternal = true;
    ch->enabled = true;
    if (auto* mr = player->GetComponent<MeshRenderer>()) mr->enabled = false;
    for (const auto& up : scene.Objects())   // the blocky part rig
        if (up->name == "Rig" && up->transform && up->transform->Parent() == player->transform) up->active = false;
    return found;
}

// One clip through the rest poses (see "Rest poses" in AddAnimationsFromFile):
// sampled at every key time, each mapped target bone gets the local rotation
// that puts it at (source root-space rotation) * conv; the hips also get the
// source's travel away from its rest, scaled by `ratio` into the target's
// parent space. Other position and scale tracks keep the target's lengths.
static void RetargetByRest(const ModelAnimator::Clip& c, ModelAnimator::Clip& out, GameObject* src,
                           GameObject* modelRoot, const std::vector<GameObject*>& mapped,
                           const std::unordered_map<GameObject*, GameObject*>& srcOf,
                           const std::unordered_map<GameObject*, Quat>& conv,
                           GameObject* sHips, GameObject* tHips, float ratio) {
    std::unordered_map<std::string, const AnimationClip*> track;   // source node -> its clip
    std::vector<float> times;
    for (const ModelAnimator::NodeClip& nc : c.nodes) {
        track[nc.node] = &nc.clip;
        for (const auto& kv : nc.clip.Tracks()) for (const auto& k : kv.second.Keys()) times.push_back(k.time);
    }
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end(), [](float a, float b) { return b - a < 1e-5f; }), times.end());

    float t = 0.0f;
    std::unordered_map<GameObject*, Quat> sAnim, tAnim;
    std::function<Quat(Transform*)> S = [&](Transform* x) -> Quat {   // source root-space, at t
        if (!x || x == src->transform) return Quat::Identity;
        auto f = sAnim.find(x->gameObject);
        if (f != sAnim.end()) return f->second;
        Vec3 p = x->localPosition, sc = x->localScale; Quat q = x->localRotation;
        auto it = track.find(x->gameObject->name);
        if (it != track.end()) ModelAnimator::EvalClipInto(*it->second, t, p, q, sc);
        return sAnim[x->gameObject] = S(x->Parent()) * q;
    };
    std::function<Quat(Transform*)> T = [&](Transform* x) -> Quat {   // target root-space, at t
        if (!x || x == modelRoot->transform) return Quat::Identity;
        auto f = tAnim.find(x->gameObject);
        if (f != tAnim.end()) return f->second;
        auto m = srcOf.find(x->gameObject);
        Quat r = m != srcOf.end() ? S(m->second->transform) * conv.at(x->gameObject) : T(x->Parent()) * x->localRotation;
        return tAnim[x->gameObject] = r;
    };
    // Hips travel: source parent space -> root -> target parent space.
    auto parentRot = [](GameObject* g, GameObject* root) {
        Quat r = Quat::Identity;
        for (Transform* x = g->transform->Parent(); x && x != root->transform; x = x->Parent()) r = x->localRotation * r;
        return r;
    };
    const AnimationClip* hipsTrack = nullptr;
    if (sHips && tHips && srcOf.count(tHips)) {
        auto it = track.find(sHips->name);
        if (it != track.end() && it->second->HasTrack("position.x")) hipsTrack = it->second;
    }
    const Quat hipsTo = (tHips ? parentRot(tHips, modelRoot).Inverse() : Quat::Identity) *
                        (sHips ? parentRot(sHips, src) : Quat::Identity);

    std::vector<AnimationClip> clips(mapped.size());
    std::vector<Quat> prev(mapped.size());
    for (float tm : times) {
        t = tm; sAnim.clear(); tAnim.clear();
        for (std::size_t i = 0; i < mapped.size(); ++i) {
            GameObject* g = mapped[i];
            Quat q = (T(g->transform->Parent()).Inverse() * T(g->transform)).Normalized();
            if (tm != times.front() && Quat::Dot(prev[i], q) < 0.0f) q = Quat{-q.x, -q.y, -q.z, -q.w};
            prev[i] = q;
            AnimationClip& ac = clips[i];
            ac.smoothKeys = false;
            ac.AddKey("rotation.qx", t, q.x); ac.AddKey("rotation.qy", t, q.y);
            ac.AddKey("rotation.qz", t, q.z); ac.AddKey("rotation.qw", t, q.w);
        }
        if (hipsTrack) {
            Vec3 p = sHips->transform->localPosition, sc; Quat q;
            ModelAnimator::EvalClipInto(*hipsTrack, t, p, q, sc);
            Vec3 d = hipsTo * ((p - sHips->transform->localPosition) * ratio);
            Vec3 tp = tHips->transform->localPosition + d;
            AnimationClip& ac = clips[std::find(mapped.begin(), mapped.end(), tHips) - mapped.begin()];
            ac.AddKey("position.x", t, tp.x); ac.AddKey("position.y", t, tp.y); ac.AddKey("position.z", t, tp.z);
        }
    }
    for (std::size_t i = 0; i < mapped.size(); ++i) out.nodes.push_back({mapped[i]->name, std::move(clips[i])});
}

int AddAnimationsFromFile(Scene& scene, GameObject* modelRoot, const std::string& path, std::string* outLog) {
    auto say = [&](const std::string& m) { if (outLog) *outLog = m; };
    if (!modelRoot) { say("Pick the model to add animations to."); return 0; }
    Scene tmp;
    bool ok = false;
    GameObject* src = ImportModelScene(tmp, path, &ok);
    if (!ok || !src) { say("Couldn't read " + BaseName(path) + "."); return 0; }
    ModelAnimator* sma = src->GetComponent<ModelAnimator>();
    if (!sma || sma->clips.empty()) { say(BaseName(path) + " has no animations in it."); return 0; }

    // Target bones by normalized name.
    std::vector<GameObject*> tsub = Subtree(scene, modelRoot);
    std::unordered_map<std::string, std::string> tName;   // norm -> real name in this model
    for (GameObject* g : tsub) tName.emplace(NormBoneName(g->name), g->name);
    ModelAnimator* ma = modelRoot->GetComponent<ModelAnimator>();
    if (!ma) for (GameObject* g : tsub) if ((ma = g->GetComponent<ModelAnimator>())) break;
    if (!ma) ma = modelRoot->AddComponent<ModelAnimator>();

    // Proportions: same skeleton (Mixamo clip downloaded for THIS character) ->
    // copy everything; different -> rotations + scaled hips motion.
    std::vector<GameObject*> ssub = Subtree(tmp, src);
    GameObject* sHips = FindHips(src, ssub);
    GameObject* tHips = FindHips(modelRoot, tsub);
    // Compare the hips' offset in their parent's space: the same space the hips
    // position track lives in (so the ratio scales it directly).
    float sh = sHips ? sHips->transform->localPosition.Magnitude() : 0.0f;
    float th = tHips ? tHips->transform->localPosition.Magnitude() : 0.0f;
    if (sh < 1e-5f || th < 1e-5f) { sh = RestHipsHeight(src, sHips); th = RestHipsHeight(modelRoot, tHips); }
    float ratio = (sh > 1e-5f && th > 1e-5f) ? th / sh : 1.0f;
    const bool retarget = std::fabs(ratio - 1.0f) > 0.03f;
    const std::string sHipsKey = sHips ? NormBoneName(sHips->name) : std::string();

    // Rest poses. When they differ (an A-pose rig, other joint axes), local
    // rotations can't be copied: each bone instead gets the source bone's
    // root-space rotation away from ITS rest, put on this model's rest after
    // turning that bone to aim where the source's did (so a T-pose clip
    // brings an A-pose rig's arms down to the same place, not 45 deg lower).
    std::unordered_map<std::string, GameObject*> sByKey;
    for (GameObject* g : ssub) if (g != src) sByKey.emplace(NormBoneName(g->name), g);
    std::unordered_map<GameObject*, GameObject*> srcOf;   // target bone -> source bone
    std::vector<GameObject*> mapped;                      // the same, in scene order
    bool sameRest = true;
    for (GameObject* g : tsub) {
        auto it = g == modelRoot ? sByKey.end() : sByKey.find(NormBoneName(g->name));
        if (it == sByKey.end()) continue;
        srcOf[g] = it->second;
        mapped.push_back(g);
        if (std::fabs(Quat::Dot(g->transform->localRotation, it->second->transform->localRotation)) < 0.9998f)
            sameRest = false;
    }
    // Avatar slots: when the names don't line up (a Mixamo clip on a mannequin
    // rig), a humanoid's bones pair up by what they are instead: hips with hips,
    // left forearm with left forearm, the spine chains link for link from the
    // hips (the shorter chain's extra links keep their rest), the necks. Only
    // when both skeletons are humanoids (Animation/HumanoidBones.hpp), and
    // always through the rest poses below: different rigs, different rests.
    int bySlot = 0;
    {
        const humanoid::BoneMap tm = humanoid::MapHumanoidBones(modelRoot), sm = humanoid::MapHumanoidBones(src);
        if (tm.humanoid && sm.humanoid) {
            std::unordered_set<GameObject*> taken;
            for (const auto& kv : srcOf) taken.insert(kv.second);
            auto link = [&](GameObject* t, GameObject* sb) {
                if (!t || !sb || t == modelRoot || srcOf.count(t) || taken.count(sb)) return;
                srcOf[t] = sb; taken.insert(sb); ++bySlot;
            };
            for (int b = 0; b < humanoid::BoneCount; ++b) if (b != humanoid::Torso) link(tm.bone[b], sm.bone[b]);
            for (std::size_t i = 0; i < tm.spine.size() && i < sm.spine.size(); ++i) link(tm.spine[i], sm.spine[i]);
            link(tm.neck, sm.neck);
            if (!tHips) tHips = tm.bone[humanoid::Hips];
            if (!sHips) sHips = sm.bone[humanoid::Hips];
        }
        if (bySlot) {
            sameRest = false;
            mapped.clear();   // scene order again
            for (GameObject* g : tsub) if (srcOf.count(g)) mapped.push_back(g);
        }
    }
    auto rootRot = [](GameObject* g, GameObject* root) {
        Quat r = Quat::Identity;
        for (Transform* t = g->transform; t && t != root->transform; t = t->Parent()) r = t->localRotation * r;
        return r;
    };
    const Mat4 sInv = src->transform->LocalToWorldMatrix().Inverse();
    const Mat4 tInv = modelRoot->transform->LocalToWorldMatrix().Inverse();
    std::unordered_map<GameObject*, Quat> aim;   // turns a target bone's rest to aim like the source's
    std::function<Quat(GameObject*)> Aim = [&](GameObject* g) -> Quat {
        auto f = aim.find(g);
        if (f != aim.end()) return f->second;
        Quat a = Quat::Identity;
        Transform* pt = g->transform->Parent();
        if (pt && srcOf.count(pt->gameObject)) a = Aim(pt->gameObject);   // a leaf follows its parent
        for (Transform* ct : g->transform->Children()) {
            auto c = srcOf.find(ct->gameObject);
            if (c == srcOf.end()) continue;
            Vec3 td = tInv.MultiplyPoint(ct->Position()) - tInv.MultiplyPoint(g->transform->Position());
            Vec3 sd = sInv.MultiplyPoint(c->second->transform->Position()) - sInv.MultiplyPoint(srcOf[g]->transform->Position());
            if (td.SqrMagnitude() > 1e-10f && sd.SqrMagnitude() > 1e-10f) a = Quat::FromToRotation(td.Normalized(), sd.Normalized());
            break;
        }
        return aim[g] = a;
    };
    // Target root-space rotation = source root-space rotation * conv.
    std::unordered_map<GameObject*, Quat> conv;
    if (!sameRest)
        for (const auto& pr : srcOf)
            conv[pr.first] = rootRot(pr.second, src).Inverse() * Aim(pr.first) * rootRot(pr.first, modelRoot);

    std::unordered_set<std::string> srcNames;   // source bones that drive a bone of this model
    for (const auto& kv : srcOf) srcNames.insert(kv.second->name);
    int added = 0, matched = 0, total = 0;
    std::string names;
    for (const ModelAnimator::Clip& c : sma->clips) {
        ModelAnimator::Clip out;
        out.name = c.name;
        for (int n = 2; ; ++n) {   // unique within the target library
            bool clash = false;
            for (const auto& e : ma->clips) if (e.name == out.name) { clash = true; break; }
            if (!clash) break;
            out.name = c.name + " " + std::to_string(n);
        }
        out.events = c.events;
        if (!sameRest) {   // see "Rest poses" above
            for (const ModelAnimator::NodeClip& nc : c.nodes) { ++total; matched += (int)srcNames.count(nc.node); }
            RetargetByRest(c, out, src, modelRoot, mapped, srcOf, conv, sHips, tHips, ratio);
        } else for (const ModelAnimator::NodeClip& nc : c.nodes) {
            ++total;
            const std::string key = NormBoneName(nc.node);
            auto it = tName.find(key);
            if (it == tName.end()) continue;
            ++matched;
            const bool isHips = !sHipsKey.empty() && key == sHipsKey;
            if (!retarget) { out.nodes.push_back({it->second, nc.clip}); continue; }
            AnimationClip ac; ac.smoothKeys = false;
            for (const auto& kv : nc.clip.Tracks()) {
                const bool pos = kv.first.rfind("position.", 0) == 0, scl = kv.first.rfind("scale.", 0) == 0;
                if (scl || (pos && !isHips)) continue;   // other bones keep this model's lengths
                for (const auto& k : kv.second.Keys()) ac.AddKey(kv.first, k.time, pos ? k.value * ratio : k.value);
            }
            if (!ac.Tracks().empty()) out.nodes.push_back({it->second, std::move(ac)});
        }
        if (out.nodes.empty()) continue;
        ModelAnimator::NormalizeClip(out);
        names += (names.empty() ? "" : ", ") + out.name;
        ma->clips.push_back(std::move(out));
        ++added;
    }
    if (!added) { say("None of the bones in " + BaseName(path) + " match this model's skeleton."); return 0; }
    AutoMapLocomotion(ma);
    SettleCharacterLocomotion(ma);   // a retargeted player starts using the new clips in their slots
    char buf[200];
    std::snprintf(buf, sizeof(buf), " (%d/%d bone tracks matched%s)", matched, total,
                  bySlot ? ", paired by humanoid bone and converted to this model's rest pose" :
                  !sameRest ? ", converted to this model's rest pose" : retarget ? ", retargeted to this model's proportions" : "");
    say("Added " + std::to_string(added) + " clip" + (added == 1 ? "" : "s") + ": " + names + buf);
    return added;
}

int CountHumanoidBones(Scene&, GameObject* modelRoot, std::string* why, const std::string* byHand) {
    const humanoid::BoneMap m = humanoid::MapHumanoidBones(modelRoot, byHand);
    if (!m.humanoid) { if (why) *why = m.why; return 0; }
    return m.found;
}

int EnableBuiltinAnimations(Scene& scene, GameObject* modelRoot, std::string* outLog, const std::string* byHand) {
    auto say = [&](const std::string& m) { if (outLog) *outLog = m; };
    if (!modelRoot || !modelRoot->transform) { say("Select an imported model first."); return 0; }
    if (auto* mr = modelRoot->GetComponent<MeshRenderer>(); mr && !mr->mesh.vertices.empty() &&
        !modelRoot->GetComponent<Character>()) {
        say("'" + modelRoot->name + "' is a single merged mesh with no skeleton - use Rig Model (Humanoid) instead.");
        return 0;
    }
    std::string why;
    const int bones = CountHumanoidBones(scene, modelRoot, &why, byHand);
    if (bones < 8) {
        say("'" + modelRoot->name + "' isn't a humanoid skeleton" +
            (why.empty() ? " (" + std::to_string(bones) + "/15 bones recognised)" : ": " + why) +
            ". Rig Model (Humanoid) can bind its mesh to the built-in skeleton instead.");
        return bones;
    }
    auto* ch = modelRoot->GetComponent<Character>();
    const bool addedCh = !ch;
    if (!ch) ch = modelRoot->AddComponent<Character>();
    ch->driveExternal = true;
    ch->separateParts = false;
    ch->anim = 1;   // idle
    auto* rt = modelRoot->GetComponent<HumanoidRetarget>();
    if (!rt) rt = modelRoot->AddComponent<HumanoidRetarget>();
    rt->Rewire();
    for (int b = 0; b < humanoid::BoneCount; ++b) rt->boneMap[b] = byHand ? byHand[b] : std::string();
    const int found = rt->DetectAndWire(scene, modelRoot);
    if (found < 8) {
        if (addedCh) ch->enabled = false;
        say("Only " + std::to_string(found) + "/15 humanoid bones matched on '" + modelRoot->name + "'.");
        return found;
    }
    // The retarget owns the pose: the model's own clips stay for one-shots but
    // mustn't auto-play over it every frame.
    for (const auto& up : scene.Objects())
        if (up->IsSelfOrDescendantOf(modelRoot))
            if (auto* ma = up->GetComponent<ModelAnimator>()) { ma->autoPlay = false; ma->driveByMovement = false; }
    HumanoidLibrary::EnsureAnimator(modelRoot);
    say("Built-in animations on '" + modelRoot->name + "' (" + std::to_string(found) + "/15 bones matched)");
    return found;
}

// Bounds of every mesh under `root` as currently posed, in the space `toSpace`
// maps world into. Skinned meshes are deformed here from their bind pose and
// joints: with GPU skinning the renderer's mesh holds the raw bind vertices
// (centimetres on Mixamo rigs, before the armature's scale), so measuring it
// sized characters 100x off.
static bool SubtreeBounds(Scene& scene, GameObject* root, const Mat4& toSpace, Vec3& lo, Vec3& hi) {
    bool any = false;
    auto grow = [&](const Vec3& w) {
        if (!any) { lo = hi = w; any = true; return; }
        lo.x = std::fmin(lo.x, w.x); hi.x = std::fmax(hi.x, w.x);
        lo.y = std::fmin(lo.y, w.y); hi.y = std::fmax(hi.y, w.y);
        lo.z = std::fmin(lo.z, w.z); hi.z = std::fmax(hi.z, w.z);
    };
    for (const auto& up : scene.Objects()) {
        GameObject* g = up.get();
        if (!g || !g->transform || !g->IsSelfOrDescendantOf(root)) continue;
        auto* mr = g->GetComponent<MeshRenderer>();
        if (!mr) continue;
        auto* sm = g->GetComponent<SkinnedMesh>();
        if (sm) sm->ResolveJoints();
        if (sm && sm->Ready() && sm->jointIdx.size() == sm->bind.vertices.size() &&
            sm->jointWt.size() == sm->bind.vertices.size()) {
            // World position per vertex: sum of w * jointWorld * inverseBind * v.
            std::vector<Mat4> skin(sm->joints.size());
            for (std::size_t j = 0; j < skin.size(); ++j) {
                Mat4 jw = sm->joints[j] ? sm->joints[j]->LocalToWorldMatrix() : g->transform->LocalToWorldMatrix();
                Mat4 ib = j < sm->inverseBind.size() ? sm->inverseBind[j] : Mat4::Identity();
                skin[j] = toSpace * jw * ib;
            }
            Mat4 rigid = toSpace * g->transform->LocalToWorldMatrix();
            for (std::size_t v = 0; v < sm->bind.vertices.size(); ++v) {
                const Vec3& p = sm->bind.vertices[v];
                Vec3 acc{0, 0, 0}; float wsum = 0.0f;
                for (int k = 0; k < 4; ++k) {
                    float w = sm->jointWt[v][k]; int j = sm->jointIdx[v][k];
                    if (w <= 1e-4f || j < 0 || j >= (int)skin.size()) continue;
                    acc = acc + skin[j].MultiplyPoint(p) * w; wsum += w;
                }
                grow(wsum > 1e-4f ? acc * (1.0f / wsum) : rigid.MultiplyPoint(p));
            }
            continue;
        }
        if (mr->mesh.vertices.empty()) continue;
        Vec3 blo, bhi; mr->mesh.Bounds(blo, bhi);
        Mat4 m = toSpace * g->transform->LocalToWorldMatrix();
        for (int c = 0; c < 8; ++c)
            grow(m.MultiplyPoint({c & 1 ? bhi.x : blo.x, c & 2 ? bhi.y : blo.y, c & 4 ? bhi.z : blo.z}));
    }
    return any;
}

// Which way an imported humanoid faces, read from its skeleton: the
// character's right side (right thigh/arm/foot minus the left one) crossed
// with up. Flattened, in `toSpace` coordinates. False when the rig has no
// recognisable left/right pairs (static meshes, non-humanoids).
static bool DetectModelForward(Scene& scene, GameObject* root, const Mat4& toSpace, Vec3& fwd) {
    Vec3 pos[15]; int best[15];
    for (int i = 0; i < 15; ++i) best[i] = -1;
    for (const auto& up : scene.Objects()) {
        GameObject* g = up.get();
        if (!g || !g->transform || g == root || !g->IsSelfOrDescendantOf(root)) continue;
        int sc = 0;
        int b = HumanoidRetarget::ClassifyBone(g->name, sc);
        if (b < 3 || b >= 15 || sc <= best[b]) continue;
        best[b] = sc;
        pos[b] = toSpace.MultiplyPoint(g->transform->Position());
    }
    const int pairs[4][2] = {{9, 12}, {3, 6}, {11, 14}, {4, 7}};   // thighs, upper arms, feet, forearms
    Vec3 right{0, 0, 0}; int n = 0;
    for (const auto& p : pairs)
        if (best[p[0]] >= 0 && best[p[1]] >= 0) { right = right + (pos[p[1]] - pos[p[0]]); ++n; }
    if (!n) return false;
    float len = std::sqrt(right.x * right.x + right.z * right.z);
    if (len < 1e-6f) return false;
    // right = forward x up  =>  forward = up x right = (r.z, 0, -r.x).
    fwd = {right.z / len, 0.0f, -right.x / len};
    // Exported characters almost always face an axis: snap when close so a
    // slightly asymmetric bind pose doesn't leave the model a few degrees off.
    float a = std::atan2(fwd.x, fwd.z), q = 1.5707963f;
    float snapped = std::round(a / q) * q;
    if (std::fabs(a - snapped) < 0.35f) fwd = {std::sin(snapped), 0.0f, std::cos(snapped)};
    return true;
}

// Turn the model about the player's vertical axis by `fix` (player-local).
// When the model IS the player (a controller added straight onto a model),
// its children turn instead, so the controller keeps owning the root.
static void TurnModel(GameObject* player, GameObject* modelRoot, const Quat& fix) {
    if (modelRoot != player) {
        modelRoot->transform->localPosition = fix * modelRoot->transform->localPosition;
        modelRoot->transform->localRotation = (fix * modelRoot->transform->localRotation).Normalized();
        return;
    }
    for (Transform* c : player->transform->Children()) {
        if (!c || !c->gameObject || c->gameObject->GetComponent<Camera>()) continue;
        c->localPosition = fix * c->localPosition;
        c->localRotation = (fix * c->localRotation).Normalized();
    }
}

bool ModelBounds(Scene& scene, GameObject* root, Vec3& lo, Vec3& hi) {
    return root && SubtreeBounds(scene, root, Mat4::Identity(), lo, hi);
}

GameObject* FindCharacterModelRoot(Scene& scene, GameObject* player) {
    if (!player || !player->transform) return nullptr;
    // How much `root` looks like the character model, or -1 when it isn't one.
    // A held hat is often the first mesh child; a skinned rig or a real
    // hierarchy outranks that prop so a model swap removes the old model.
    auto scoreModel = [&](GameObject* root) {
        int score = 0, nodes = 0;
        bool mesh = false;
        for (const auto& up : scene.Objects()) {
            GameObject* g = up.get();
            if (!g || !g->IsSelfOrDescendantOf(root)) continue;
            if (g->GetComponent<Camera>()) return -1;
            ++nodes;
            if (g->GetComponent<SkinnedMesh>()) score += 100;
            if (g->GetComponent<ModelAnimator>()) score += 40;
            if (g != player) if (auto* mr = g->GetComponent<MeshRenderer>(); mr && !mr->mesh.vertices.empty()) mesh = true;
        }
        if (!mesh && score == 0) return -1;
        if (mesh) score += 1;
        if (nodes > 1) score += std::min(nodes, 30);   // a rig beats a single prop
        return score;
    };
    GameObject* best = nullptr;
    int bestScore = -1;
    for (Transform* c : player->transform->Children()) {
        GameObject* g = c ? c->gameObject : nullptr;
        if (!g || g->name == "Rig" || !g->active) continue;
        const int sc = scoreModel(g);
        if (sc > bestScore) { bestScore = sc; best = g; }
    }
    if (best) return best;
    // A controller put straight onto an imported model: the player IS the model.
    if (!player->GetComponent<Character>())
        for (const auto& up : scene.Objects())
            if (up.get() != player && up->IsSelfOrDescendantOf(player) &&
                (up->GetComponent<SkinnedMesh>() || up->GetComponent<MeshRenderer>()))
                return player;
    return nullptr;
}

int FaceCharacterModel(Scene& scene, GameObject* player, GameObject* modelRoot, float turnDegrees) {
    if (!player || !player->transform) return -1;
    if (!modelRoot) modelRoot = FindCharacterModelRoot(scene, player);
    if (!modelRoot || !modelRoot->transform) return -1;
    if (turnDegrees != 0.0f) {   // manual turn
        TurnModel(player, modelRoot, Quat::Euler(0.0f, turnDegrees, 0.0f));
        return (int)std::lround(turnDegrees);
    }
    Mat4 toPlayer = player->transform->LocalToWorldMatrix().Inverse();
    Vec3 f;
    if (!DetectModelForward(scene, modelRoot, toPlayer, f)) return -1;
    // Controllers move a character along its LOCAL -Z: the rotation taking
    // the model's forward onto -Z (LookRotation maps +Z onto its argument).
    Quat fix = Quat::LookRotation(Vec3{-f.x, 0.0f, -f.z}).Inverse();
    TurnModel(player, modelRoot, fix);
    float deg = std::atan2(-f.x, -f.z) * 57.29578f;   // how far it was off
    return (int)std::lround(std::fabs(deg));
}

std::string FitCharacterCollider(Scene& scene, GameObject* player, GameObject* modelRoot) {
    if (!player || !player->transform) return {};
    if (!modelRoot) modelRoot = FindCharacterModelRoot(scene, player);
    float h = 1.8f, base = 0.0f, side = 0.6f;
    if (auto* ch = player->GetComponent<Character>()) h = 1.8f * (ch->height > 0.1f ? ch->height : 1.0f);
    Vec3 lo, hi;
    Mat4 toPlayer = player->transform->LocalToWorldMatrix().Inverse();
    if (modelRoot && SubtreeBounds(scene, modelRoot, toPlayer, lo, hi) && hi.y - lo.y > 1e-3f) {
        h = hi.y - lo.y; base = lo.y;
        // The narrower horizontal extent is the body's depth (a T-pose's arms
        // stretch the other one): a person-shaped column, never a wide slab.
        float d = std::fmin(hi.x - lo.x, hi.z - lo.z);
        side = std::fmin(std::fmax(d, 0.3f * h), 0.45f * h);
    } else {
        side = h / 3.0f;
    }
    std::string what;
    if (auto* box = player->GetComponent<BoxCollider3D>()) {
        box->size = {side, h, side};
        box->offset = {0.0f, base + h * 0.5f, 0.0f};
        what = "box collider fitted";
    } else if (auto* cap = player->GetComponent<CapsuleCollider3D>()) {
        cap->radius = side * 0.5f; cap->height = h;
        cap->offset = {0.0f, base + h * 0.5f, 0.0f};
        what = "capsule collider fitted";
    } else if (!player->GetComponent<Collider3D>()) {
        auto* cap2 = player->AddComponent<CapsuleCollider3D>();
        cap2->radius = side * 0.5f; cap2->height = h;
        cap2->offset = {0.0f, base + h * 0.5f, 0.0f};
        what = "capsule collider added";
    } else {
        what = "kept its collider";
    }
    if (!player->GetComponent<Rigidbody3D>()) { player->AddComponent<Rigidbody3D>(); what += " + rigidbody added"; }
    char buf[64]; std::snprintf(buf, sizeof(buf), " (%.2f x %.2fu)", side, h);
    return what + buf;
}

// The walk / run speeds of the controller moving `player` (false: none known).
static bool ControllerSpeeds(GameObject* player, float& walk, float& run) {
    if (auto* c = player->GetComponent<ThirdPersonController>())       { walk = c->walkSpeed; run = c->runSpeed; return true; }
    if (auto* c = player->GetComponent<ThirdPersonShooterController>()) { walk = c->walkSpeed; run = c->runSpeed; return true; }
    if (auto* c = player->GetComponent<FirstPersonController>())       { walk = c->walkSpeed; run = c->runSpeed; return true; }
    if (auto* c = player->GetComponent<ClickToMoveController>())       { walk = c->walkSpeed; run = c->runSpeed; return true; }
    if (auto* c = player->GetComponent<TopDownController>())           { walk = c->walkSpeed; run = c->runSpeed; return true; }
    if (auto* c = player->GetComponent<CharacterController3D>())       { walk = c->speed;     run = c->runSpeed; return true; }
    if (auto* c = player->GetComponent<NPCController>())               { walk = c->moveSpeed; run = c->runSpeed; return true; }
    if (auto* c = player->GetComponent<Platformer25DController>())     { walk = c->walkSpeed; run = c->runSpeed; return true; }
    if (auto* c = player->GetComponent<TopDownShooterController>())    { walk = c->walkSpeed; run = c->runSpeed; return true; }
    return false;
}

GameObject* AttachCharacterModel(Scene& scene, GameObject* player,
                                 const std::string& path, std::string* outLog) {
    if (!player || !player->transform) return nullptr;
    bool ok = false;
    GameObject* root = ImportModelScene(scene, path, &ok);
    if (!ok || !root || !root->transform) return nullptr;
    // A swap: the previous model goes, or it stays inside the new one (frozen, or
    // still animating on its own) and keeps the old wiring busy.
    if (GameObject* old = FindCharacterModelRoot(scene, player); old && old != player)
        scene.Destroy(old);

    // World bounds of the imported subtree (the import root sits at the origin).
    Vec3 lo{0, 0, 0}, hi{0, 0, 0};
    bool any = SubtreeBounds(scene, root, Mat4::Identity(), lo, hi);

    // Size the model to the character's height and stand its feet on the player's
    // origin (the controllers/capsules all treat the origin as ground level).
    float want = 1.8f;
    if (auto* ch = player->GetComponent<Character>())
        want = 1.8f * (ch->height > 0.1f ? ch->height : 1.0f);
    float h = any ? (hi.y - lo.y) : 0.0f;
    float s = h > 1e-4f ? want / h : 1.0f;
    root->transform->localScale = root->transform->localScale * s;
    root->transform->SetParent(player->transform, /*worldPositionStays=*/false);
    root->transform->localPosition = {0.0f, any ? -lo.y * s : 0.0f, 0.0f};
    // The engine's character bodies face LOCAL -Z. Read which way THIS model
    // faces from its skeleton (exporters disagree: Mixamo/glTF +Z, some
    // Blender / other exporters use -Z or +X); a rig without recognisable
    // left/right bones falls back to the common +Z.
    int faced = FaceCharacterModel(scene, player, root, 0.0f);
    if (faced < 0) root->transform->localRotation = Quat::Euler({0.0f, 180.0f, 0.0f}) * root->transform->localRotation;

    // Locomotion: auto-switch idle/walk/run from how fast the player moves, with
    // the clips mapped by name (idle/stand, walk, run/sprint/jog).
    ModelAnimator* ma = root->GetComponent<ModelAnimator>();
    if (!ma)
        for (const auto& up : scene.Objects())
            if (up->IsSelfOrDescendantOf(root))
                if ((ma = up->GetComponent<ModelAnimator>())) break;
    std::string mapped;
    if (ma && !ma->clips.empty()) AutoMapLocomotion(ma);
    // A humanoid skeleton (enough recognisable bones, on a player with a Character)
    // gets the BUILT-IN animations on every locomotion slot its clips don't fill:
    // the Character stays ON in drive-external mode and a HumanoidRetarget plays
    // the engine's walk, run, jump, crouch... on the imported bones, while the
    // clips the file does carry (a Mixamo walk, an idle) play in their own slots.
    // A file with no usable clips at all is the plain built-in case. Without a
    // humanoid skeleton the clips drive locomotion alone. Decided HERE so the clip
    // path below (including its animated re-grounding) runs when it should.
    bool wantRetarget = false;
    if (player->GetComponent<Character>()) {
        HumanoidRetarget probe;
        probe.gameObject = player;
        wantRetarget = probe.DetectAndWire(scene, root) >= 8;
    }
    const bool hasIdleClip = ma && ma->SlotFilled(ModelAnimator::SlotIdle);
    if (ma && !ma->clips.empty() && (!wantRetarget || hasIdleClip)) {
      if (!wantRetarget) {
        if (ma->idleClip.empty()) ma->idleClip = ma->clips.front().name;  // something is better than T-pose
        if (ma->walkClip.empty()) ma->walkClip = !ma->runClip.empty() ? ma->runClip : ma->idleClip;
        ma->driveByMovement = true;
        ma->smoothLocomotion = true;
        // The blend is fully "walk" at the walk threshold and fully "run" at the
        // run threshold: put them at the speeds this player actually moves.
        float walk = 0.0f, run = 0.0f;
        if (ControllerSpeeds(player, walk, run) && walk > 0.0f && run > walk) {
            ma->walkThreshold = walk; ma->runThreshold = run;
        }
        // The controller moves the capsule — the clips must animate in place, or
        // their baked forward travel slides the model out of the collider.
        ma->inPlace = true;
        mapped = "idle='" + ma->idleClip + "' walk='" + ma->walkClip + "'" +
                 (ma->runClip.empty() ? "" : " run='" + ma->runClip + "'");
        for (const auto& [label, clip] : {std::pair<const char*, const std::string*>{"crouch", &ma->crouchClip},
                                          {"crouch walk", &ma->crouchWalkClip}, {"prone", &ma->proneClip},
                                          {"crawl", &ma->crawlClip}, {"get up", &ma->getUpClip}})
            if (!clip->empty()) mapped += std::string(" ") + label + "='" + *clip + "'";
      }

        // Re-ground and re-center on the ANIMATED pose. Some rigs' clips sit at
        // a constant offset from the bind pose (glTF CesiumMan-style skeleton
        // roots), so grounding on the bind pose parked the playing model a metre
        // outside its capsule with its feet in the air — the "glitchy character"
        // report. Pose the mapped idle clip's first frame, measure, fold the
        // offset into the root, then restore the bind pose.
        int ci = ma->FindClip(ma->idleClip); if (ci < 0) ci = 0;
        struct SavedTRS { Transform* tr; Vec3 p, s; Quat r; };
        std::vector<SavedTRS> savedPose;
        for (const auto& nc : ma->clips[ci].nodes)
            for (const auto& up : scene.Objects()) {
                GameObject* g = up.get();
                if (!g || g->name != nc.node || !g->IsSelfOrDescendantOf(root) || !g->transform) continue;
                Transform* tr = g->transform;
                savedPose.push_back({tr, tr->localPosition, tr->localScale, tr->localRotation});
                Vec3 p = tr->localPosition, sc = tr->localScale; Quat rq = tr->localRotation;
                ModelAnimator::EvalClipInto(nc.clip, 0.0f, p, rq, sc);
                tr->localPosition = p; tr->localScale = sc; tr->localRotation = rq;
                break;
            }
        if (!savedPose.empty()) {
            // Deform skinned meshes onto the posed joints, so the measurement sees
            // the pose (the renderer mesh is otherwise still the bind mesh).
            for (const auto& up : scene.Objects())
                if (up->IsSelfOrDescendantOf(root))
                    if (auto* sm = up->GetComponent<SkinnedMesh>()) { sm->ResolveJoints(); sm->Skin(); }
            Vec3 alo{0, 0, 0}, ahi{0, 0, 0};
            bool aany = SubtreeBounds(scene, root, Mat4::Identity(), alo, ahi);
            if (aany) {
                Vec3 pw = player->transform->Position();
                Vec3 dWorld{pw.x - (alo.x + ahi.x) * 0.5f, pw.y - alo.y, pw.z - (alo.z + ahi.z) * 0.5f};
                // Fold the world-space correction into the root's local position
                // (the root's parent is the player — rotate the delta into its space).
                Mat4 pInv = player->transform->LocalToWorldMatrix().Inverse();
                Vec3 dLocal = pInv.MultiplyVector(dWorld);
                root->transform->localPosition = root->transform->localPosition + dLocal;
            }
            for (const SavedTRS& sp : savedPose) {
                sp.tr->localPosition = sp.p;
                sp.tr->localScale    = sp.s;
                sp.tr->localRotation = sp.r;
            }
            for (const auto& up : scene.Objects())   // deform back to the bind pose
                if (up->IsSelfOrDescendantOf(root))
                    if (auto* sm = up->GetComponent<SkinnedMesh>()) sm->Skin();
        }
    }

    // Retarget path: the built-in animation system drives the imported
    // skeleton. Added BEFORE FootIK so its LateUpdate writes the pose first
    // and the feet plant on top of it.
    bool retargeted = false;
    if (wantRetarget) {
        const int found = EnableBuiltinFallback(scene, player, root);
        retargeted = found >= 0;
        if (retargeted) {
            SettleCharacterLocomotion(ma);
            mapped = SlotSummary(ma) + (mapped.empty() ? "" : ", ") + "built-in animations retargeted (" +
                     std::to_string(found) + "/15 bones)";
        }
    }

    // Foot IK, wired automatically: detect the model's leg bones by name (Mixamo
    // etc.) and plant the feet on slopes/steps with the full setup — the same
    // out-of-the-box treatment the Humanoid template gets. One already on the
    // player is re-pointed at THIS model: the Humanoid template's drives the
    // blocky part rig (and rebuilds it, visible, when the scene loads), and after
    // a model swap it would still be bending the old model's legs.
    {
        FootIK probe;
        FootIK* fik = player->GetComponent<FootIK>();
        if (probe.DetectHumanoidBones(scene, root) >= 6) {
            const bool fresh = !fik;
            if (fresh) {
                fik = player->AddComponent<FootIK>();
                fik->useRaycast = true; fik->weight = 1.0f;
                fik->adjustPelvis = true; fik->plantDown = true; fik->alignToGround = true;
                fik->maxPelvisShift = 0.35f;
            }
            fik->DetectHumanoidBones(scene, root);
            if (!fresh) fik->Start();   // resolve the new names now: it may be running already
            fik->orient = true;   // turn the thigh and shin with the joints, or the skinned leg tears at the knee
            fik->hipLimit = IKJointLimit::Hip(); fik->kneeLimit = IKJointLimit::Knee();
            // Each sole's normal in its own foot bone's axes, off the bind pose: a
            // Blender / glTF foot points its Y down the foot, so tilting a fixed
            // local +Y onto the slope stood those feet on their toes; and a
            // mirrored rig's right foot isn't the left one's axis.
            for (const auto& up : scene.Objects()) {
                if (!up->IsSelfOrDescendantOf(root)) continue;
                if (up->name == fik->leftFootName)
                    fik->footUpAxis = (up->transform->Rotation().Inverse() * Vec3::Up).Normalized();
                else if (up->name == fik->rightFootName)
                    fik->footUpAxisR = (up->transform->Rotation().Inverse() * Vec3::Up).Normalized();
            }
            mapped += mapped.empty() ? "foot IK wired" : ", foot IK wired";
        } else if (fik) {
            player->RemoveComponent<FootIK>();   // no legs to plant on this model
        }
    }

    // Hide the default blocky body — the imported model IS the character now.
    // Clip path: the Character stops animating entirely. Retarget path: it stays
    // ON (drive-external computes the pose for the HumanoidRetarget) but renders
    // nothing. Both hide the baked mesh and any part rig.
    if (auto* ch = player->GetComponent<Character>()) {
        ch->enabled = retargeted;
        if (auto* mr = player->GetComponent<MeshRenderer>()) mr->enabled = false;
        for (const auto& up : scene.Objects())
            if (up->name == "Rig" && up->transform && up->transform->Parent() == player->transform)
                up->active = false;
    }

    // Physics: a player added from Add Component (or an old scene) may have no
    // body at all, and the template's box was sized for the blocky character.
    // Fit (or add) the collider + rigidbody around the model actually shown.
    std::string phys = FitCharacterCollider(scene, player, root);

    // Look-At IK is on for every imported player: the head (and spine, when
    // one was found) is wired, and it waits until a script gives it something
    // to look at, so the head does not stare at the world origin.
    {
        const humanoid::BoneMap bm = humanoid::MapHumanoidBones(root, nullptr);
        GameObject* head = bm.bone[humanoid::Head];
        GameObject* torso = bm.bone[humanoid::Torso];
        if (head) {
            LookAtIK* look = player->GetComponent<LookAtIK>();
            if (!look) look = player->AddComponent<LookAtIK>();
            look->weight = 1.0f;
            look->ikActive = true;
            look->aimAtPoint = false;
            look->chainNames.clear();
            if (torso) look->chainNames.push_back(torso->name);
            look->chainNames.push_back(head->name);
            look->chain.clear();
            mapped += mapped.empty() ? "look-at IK on" : ", look-at IK on";
        }
    }

    // The controllers' default 4.5 m/s "walk" is a jog: a downloaded walk clip is
    // made for about 1.4 m/s, so at 4.5 the stride match picks the RUN clip for
    // plain W and Shift plays it at its fastest. Bring a default speed down to
    // the pace the file's clip can show (its playback rate stays inside the
    // 0.6x-1.6x stride limits); speeds the user has already changed are kept.
    // The fitted values are remembered per player: a later swap refits a speed
    // still at them, and one the user set back to the default since is left alone.
    if (ma && ma->strideMatch && !ma->rootMotion) {
        static std::unordered_map<std::uint64_t, std::array<float, 2>> sFitted;  // {walk, run}; 0 = not fitted
        const std::array<float, 2> prev = sFitted[player->InstanceId()];
        std::array<float, 2> now = prev;
        auto fit = [&](float& walk, float& run, float dWalk, float dRun) {
            bool changed = false;
            if (ma->SlotFilled(ModelAnimator::SlotWalk) && walk == (prev[0] > 0.0f ? prev[0] : dWalk)) {
                walk = now[0] = 1.45f * ma->SlotClipSpeed(ModelAnimator::SlotWalk); changed = true;
            }
            if (ma->SlotFilled(ModelAnimator::SlotRun) && run == (prev[1] > 0.0f ? prev[1] : dRun)) {
                run = now[1] = 1.25f * ma->SlotClipSpeed(ModelAnimator::SlotRun); changed = true;
            }
            return changed;
        };
        bool fitted = false;
        // Each controller type's own defaults are the "untouched" speeds.
        auto fitC = [&](auto* c) {
            using T = std::remove_pointer_t<decltype(c)>;
            if (c) { const T d; fitted = fit(c->walkSpeed, c->runSpeed, d.walkSpeed, d.runSpeed) || fitted; }
        };
        fitC(player->GetComponent<ThirdPersonController>());
        fitC(player->GetComponent<ThirdPersonShooterController>());
        fitC(player->GetComponent<FirstPersonController>());
        fitC(player->GetComponent<ClickToMoveController>());
        fitC(player->GetComponent<TopDownController>());
        fitC(player->GetComponent<Platformer25DController>());
        fitC(player->GetComponent<TopDownShooterController>());
        sFitted[player->InstanceId()] = now;
        if (fitted) {
            mapped += mapped.empty() ? "move speeds fitted to the clips" : ", move speeds fitted to the clips";
            // The clip-only path keys its walk/run blend to the controller speeds.
            float w = 0.0f, r = 0.0f;
            if (!wantRetarget && ControllerSpeeds(player, w, r) && w > 0.0f && r > w) {
                ma->walkThreshold = w; ma->runThreshold = r;
            }
        }
    }

    if (outLog) {
        *outLog = "Character model '" + root->name + "' on " + player->name +
                  " (height " + std::to_string(want).substr(0, 4) + "u" +
                  (faced > 0 ? ", turned " + std::to_string(faced) + " deg to face forward" : "") +
                  (mapped.empty() ? ", no animation clips" : ", " + mapped) +
                  (phys.empty() ? "" : ", " + phys) + ")";
    }
    return root;
}

bool IsPlayableAvatar(GameObject* go) {
    if (!go) return false;
    return go->GetComponent<PlayerController>()
        || go->GetComponent<FirstPersonController>()
        || go->GetComponent<ThirdPersonController>()
        || go->GetComponent<ThirdPersonShooterController>()
        || go->GetComponent<TopDownController>()
        || go->GetComponent<TopDownShooterController>()
        || go->GetComponent<Platformer25DController>()
        || go->GetComponent<ClickToMoveController>()
        || go->GetComponent<CharacterController3D>();
}

GameObject* MakePlayableCharacter(Scene& scene, const std::string& path, std::string* outLog,
                                  GameObject* onto) {
    if (onto && IsPlayableAvatar(onto)) {
        GameObject* model = AttachCharacterModel(scene, onto, path, outLog);
        return model ? onto : nullptr;
    }
    GameObject* player = Templates::AddThirdPersonPlayer(scene, {0.0f, 1.0f, 0.0f});
    if (!player) return nullptr;
    GameObject* model = AttachCharacterModel(scene, player, path, outLog);
    if (!model) { scene.Destroy(player); return nullptr; }
    // One played character. Other active avatars are switched off, not deleted,
    // so the template player they overlapped stays in the scene for undo.
    std::string parked;
    for (const auto& up : scene.Objects()) {
        GameObject* g = up.get();
        if (!g || g == player || !g->active || g->IsSelfOrDescendantOf(player)) continue;
        if (!IsPlayableAvatar(g)) continue;
        g->active = false;
        if (!parked.empty()) parked += ", ";
        parked += "'" + g->name + "'";
    }
    if (outLog && !parked.empty())
        *outLog += " Deactivated " + parked + " so this character is the one that's played.";
    return player;
}

const char* CharacterAnimationSourceName(int source) {
    static const char* const n[] = {"None", "Imported clips", "Built-in retargeted", "Mixed"};
    return source >= 0 && source < 4 ? n[source] : n[0];
}

bool DescribeCharacterModel(Scene& scene, GameObject* player, CharacterModelInfo& out) {
    out = CharacterModelInfo{};
    if (!player || !player->transform) return false;
    GameObject* root = FindCharacterModelRoot(scene, player);
    if (!root || root == player) return false;
    out.valid = true;
    out.model = root->name;
    ModelAnimator* ma = root->GetComponent<ModelAnimator>();
    if (!ma)
        for (const auto& up : scene.Objects())
            if (up->IsSelfOrDescendantOf(root) && (ma = up->GetComponent<ModelAnimator>())) break;
    out.animator = ma;
    auto* ch = player->GetComponent<Character>();
    auto* hr = player->GetComponent<HumanoidRetarget>();
    out.characterOff = ch && !ch->enabled;
    out.builtinAvailable = ch && ch->enabled && ch->driveExternal && hr && hr->weight > 0.0f;
    out.mappedBones = hr && hr->Wired() && hr->MappedBones() > 0 ? hr->MappedBones() : humanoid::MapHumanoidBones(root, hr ? hr->boneMap : nullptr).found;
    for (int s = 0; s < ModelAnimator::SlotCount; ++s) {
        CharacterModelInfo::Slot sl;
        sl.slot = s;
        sl.label = ModelAnimator::SlotName(s);
        if (ma) sl.clip = ma->SlotClip(s);
        if (ma && ma->driveByMovement && ma->SlotFilled(s)) { sl.source = 1; ++out.importedSlots; }
        else if (out.builtinAvailable && (!ma || !ma->driveByMovement || ma->builtinFallback)) { sl.source = 2; ++out.builtinSlots; }
        out.slots.push_back(std::move(sl));
    }
    out.source = out.importedSlots && out.builtinSlots ? 3 : (out.importedSlots ? 1 : (out.builtinSlots ? 2 : 0));
    return true;
}

std::string FixCharacterAnimations(Scene& scene, GameObject* player) {
    CharacterModelInfo before;
    if (!DescribeCharacterModel(scene, player, before)) return "No imported character model on '" + (player ? player->name : std::string()) + "'.";
    GameObject* root = FindCharacterModelRoot(scene, player);
    ModelAnimator* ma = before.animator;
    int cleared = 0;
    if (ma) {
        for (int s = 0; s < ModelAnimator::SlotCount; ++s)   // a slot naming a clip the model doesn't have
            if (!ma->SlotClip(s).empty() && ma->FindClip(ma->SlotClip(s)) < 0) { ma->SlotClip(s).clear(); ++cleared; }
        AutoMapLocomotion(ma);
    }
    int found = 0;
    if (player->GetComponent<Character>()) {
        HumanoidRetarget probe;
        probe.gameObject = player;
        if (auto* hr = player->GetComponent<HumanoidRetarget>())   // bones the user mapped by hand count
            for (int b = 0; b < humanoid::BoneCount; ++b) probe.boneMap[b] = hr->boneMap[b];
        found = probe.DetectAndWire(scene, root);
    }
    if (found >= 8) {
        EnableBuiltinFallback(scene, player, root);
        if (ma) { ma->builtinFallback = true; SettleCharacterLocomotion(ma); }
    } else if (ma && !ma->clips.empty()) {   // no humanoid skeleton: the clips alone
        if (ma->idleClip.empty()) ma->idleClip = ma->clips.front().name;
        if (ma->walkClip.empty()) ma->walkClip = !ma->runClip.empty() ? ma->runClip : ma->idleClip;
        ma->driveByMovement = true; ma->inPlace = true;
    }
    CharacterModelInfo after;
    DescribeCharacterModel(scene, player, after);
    std::string log = "Animations on '" + after.model + "': " + CharacterAnimationSourceName(after.source) + " (" +
                      std::to_string(after.importedSlots) + " slot" + (after.importedSlots == 1 ? "" : "s") + " from clips, " +
                      std::to_string(after.builtinSlots) + " built-in";
    if (cleared) log += ", " + std::to_string(cleared) + " stale slot" + (cleared == 1 ? "" : "s") + " cleared";
    return log + ")";
}

} // namespace okay
