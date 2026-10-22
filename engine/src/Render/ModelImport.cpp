#include <set>
#include "okay/Render/ModelImport.hpp"
#include "okay/Render/Gltf.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/Animator.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Components/SkinnedMesh.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/FootIK.hpp"
#include "okay/Components/HumanoidRetarget.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Math/Mat4.hpp"
#include "ufbx.h"
#include <filesystem>
#include <fstream>
#include <array>
#include <cctype>
#include <cmath>
#include <functional>
#include <unordered_map>
#include <vector>

#ifdef OKAY_HAVE_ASSIMP
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/config.h>
#endif

namespace okay {

bool AssimpAvailable() {
#ifdef OKAY_HAVE_ASSIMP
    return true;
#else
    return false;
#endif
}

std::string ImportableExtensions() {
#ifdef OKAY_HAVE_ASSIMP
    // Built-in (.obj .gltf .glb .fbx) + everything this Assimp build can read.
    static const std::string list = [] {
        std::string out = ".obj .gltf .glb .fbx";
        Assimp::Importer imp; aiString exts; imp.GetExtensionList(exts);   // "*.3ds;*.dae;..."
        std::string e = exts.C_Str(), cur;
        auto flush = [&]() {
            if (cur.size() > 1 && cur[0] == '.' && (" " + out + " ").find(" " + cur + " ") == std::string::npos) out += " " + cur;
            cur.clear();
        };
        for (char c : e) {
            if (c == ';') flush();
            else if (c != '*') cur += (char)std::tolower((unsigned char)c);
        }
        flush();
        return out;
    }();
    return list;
#else
    return ".obj .gltf .glb .fbx";   // FBX is built in (ufbx)
#endif
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
// Imported clips: every node track spans the whole clip (see ModelAnimator::NormalizeClip).
static void NormalizeImportedClips(GameObject* root) {
    if (!root) return;
    if (auto* ma = root->GetComponent<ModelAnimator>())
        for (auto& c : ma->clips) ModelAnimator::NormalizeClip(c);
}

#ifdef OKAY_HAVE_ASSIMP
// Resolve a material texture of the given TYPE to a file the renderer can load:
// an external reference resolves next to the model; an embedded texture (the
// common case for FBX) is written out as a "<model>_<tag><mi>.<ext>" sidecar —
// the same convention the glTF importer uses ("tex" keeps the legacy name).
static std::string AssimpTexByType(const aiScene* sc, unsigned mi, const std::string& modelPath,
                                   aiTextureType type, const char* tag) {
    if (mi >= sc->mNumMaterials) return std::string();
    aiString texPath;
    if (sc->mMaterials[mi]->GetTexture(type, 0, &texPath) != AI_SUCCESS)
        return std::string();
    std::string tp = texPath.C_Str();
    if (tp.empty()) return std::string();
    if (const aiTexture* emb = sc->GetEmbeddedTexture(tp.c_str())) {
        // mHeight == 0 -> compressed blob (png/jpg per achFormatHint); mWidth is
        // then the byte size. Raw-BGRA embeds (mHeight > 0) are rare; skipped.
        if (emb->mHeight == 0 && emb->pcData && emb->mWidth > 0) {
            std::string ext = emb->achFormatHint[0] ? emb->achFormatHint : "png";
            std::string base = modelPath;
            std::size_t dot = base.find_last_of('.');
            if (dot != std::string::npos) base = base.substr(0, dot);
            std::string out = base + "_" + tag + std::to_string(mi) + "." + ext;
            if (std::FILE* f = std::fopen(out.c_str(), "wb")) {
                std::fwrite(emb->pcData, 1, emb->mWidth, f);
                std::fclose(f);
                return out;
            }
        }
        return std::string();
    }
    for (char& c : tp) if (c == '\\') c = '/';
    bool abs = tp.size() > 1 && (tp[0] == '/' || tp[1] == ':');
    std::string dir;
    { std::size_t s = modelPath.find_last_of("/\\"); if (s != std::string::npos) dir = modelPath.substr(0, s + 1); }
    return abs ? tp : dir + tp;
}

static std::string AssimpMaterialTexture(const aiScene* sc, unsigned mi, const std::string& modelPath) {
    std::string t = AssimpTexByType(sc, mi, modelPath, aiTextureType_DIFFUSE, "tex");
    if (t.empty()) t = AssimpTexByType(sc, mi, modelPath, aiTextureType_BASE_COLOR, "tex");  // PBR exports
    return t;
}

// Pull the WHOLE material onto a MeshRenderer — albedo, normal, specular/gloss
// and AO maps, plus diffuse/emissive colors and shininess/metallic/roughness
// factors — so a downloaded FBX lights like it does in other engines instead of
// arriving as flat white. Mirrors ApplyGltfMaterial for the Assimp formats.
static void ApplyAssimpMaterial(const aiScene* sc, unsigned mi, const std::string& modelPath,
                                MeshRenderer* mr) {
    if (!sc || mi >= sc->mNumMaterials || !mr) return;
    const aiMaterial* mat = sc->mMaterials[mi];
    std::string t = AssimpMaterialTexture(sc, mi, modelPath);
    if (!t.empty()) mr->texture = t;
    std::string n = AssimpTexByType(sc, mi, modelPath, aiTextureType_NORMALS, "nrm");
    if (n.empty()) n = AssimpTexByType(sc, mi, modelPath, aiTextureType_HEIGHT, "nrm");   // OBJ/FBX "bump" slot
    if (!n.empty()) mr->normalMap = n;
    std::string s = AssimpTexByType(sc, mi, modelPath, aiTextureType_SPECULAR, "spec");
    if (s.empty()) s = AssimpTexByType(sc, mi, modelPath, aiTextureType_SHININESS, "spec");
    if (!s.empty()) mr->specularMap = s;
    std::string a = AssimpTexByType(sc, mi, modelPath, aiTextureType_LIGHTMAP, "ao");     // assimp's AO slot
    if (a.empty()) a = AssimpTexByType(sc, mi, modelPath, aiTextureType_AMBIENT_OCCLUSION, "ao");
    if (!a.empty()) mr->aoMap = a;
    // Scalar factors. The diffuse COLOR only applies when there's no albedo texture
    // (textured FBX materials often carry a black/grey diffuse that would tint the
    // texture to mud).
    aiColor4D dc;
    if (mr->texture.empty() && mat->Get(AI_MATKEY_COLOR_DIFFUSE, dc) == AI_SUCCESS)
        if (dc.r + dc.g + dc.b > 0.02f)
            mr->color = Color(dc.r, dc.g, dc.b, mr->color.a);
    aiColor4D ec;
    if (mat->Get(AI_MATKEY_COLOR_EMISSIVE, ec) == AI_SUCCESS)
        if (ec.r + ec.g + ec.b > 0.02f)
            mr->emissive = Color(ec.r, ec.g, ec.b, 1.0f);
    float shin = 0.0f;
    if (mat->Get(AI_MATKEY_SHININESS, shin) == AI_SUCCESS && shin > 1.0f) {
        mr->shininess = shin > 128.0f ? 128.0f : shin;
        if (mr->specular <= 0.0f) mr->specular = 0.4f;
    }
    float metal = 0.0f;
    if (mat->Get(AI_MATKEY_METALLIC_FACTOR, metal) == AI_SUCCESS && metal > 0.0f)
        mr->metallic = metal > 1.0f ? 1.0f : metal;
    float rough = -1.0f;
    if (mat->Get(AI_MATKEY_ROUGHNESS_FACTOR, rough) == AI_SUCCESS && rough >= 0.0f) {
        float smooth = 1.0f - (rough > 1.0f ? 1.0f : rough);   // same mapping as glTF
        mr->shininess = 4.0f + smooth * 124.0f;
        mr->specular  = 0.15f + smooth * 0.85f;
    }
}

static std::string AssimpDiffuseTexture(const aiScene* sc, const std::string& modelPath) {
    for (unsigned mi = 0; mi < sc->mNumMaterials; ++mi) {
        std::string t = AssimpMaterialTexture(sc, mi, modelPath);
        if (!t.empty()) return t;
    }
    return std::string();
}

static Mesh ViaAssimp(const std::string& path, bool* ok, std::string* outTexture = nullptr) {
    Assimp::Importer imp;
    const aiScene* sc = imp.ReadFile(path,
        aiProcess_Triangulate | aiProcess_GenSmoothNormals |
        aiProcess_JoinIdenticalVertices | aiProcess_FlipUVs | aiProcess_PreTransformVertices);
    Mesh m;
    if (!sc || !sc->mRootNode || sc->mNumMeshes == 0) { if (ok) *ok = false; return m; }
    if (outTexture) *outTexture = AssimpDiffuseTexture(sc, path);
    for (unsigned mi = 0; mi < sc->mNumMeshes; ++mi) {
        const aiMesh* am = sc->mMeshes[mi];
        int base = (int)m.vertices.size();
        for (unsigned v = 0; v < am->mNumVertices; ++v) {
            m.vertices.push_back(Vec3{am->mVertices[v].x, am->mVertices[v].y, am->mVertices[v].z});
            if (am->HasNormals())
                m.normals.push_back(Vec3{am->mNormals[v].x, am->mNormals[v].y, am->mNormals[v].z});
            if (am->HasTextureCoords(0))
                m.uvs.push_back(Vec2{am->mTextureCoords[0][v].x, am->mTextureCoords[0][v].y});
        }
        for (unsigned fi = 0; fi < am->mNumFaces; ++fi) {
            const aiFace& f = am->mFaces[fi];
            if (f.mNumIndices == 3)
                for (int k = 0; k < 3; ++k) m.triangles.push_back(base + (int)f.mIndices[k]);
        }
    }
    if (m.normals.size() != m.vertices.size()) m.normals.clear();
    if (m.uvs.size()     != m.vertices.size()) m.uvs.clear();
    Mesh::NormalizeImportScale(m);   // cm/mm exports (Meshy FBX etc.) land at a usable size
    if (ok) *ok = !m.vertices.empty();
    return m;
}
#endif

Mesh ImportModel(const std::string& path, bool* ok, std::string* outTexture) {
    std::string p = Lower(path);
    if (EndsWith(p, ".obj"))                          return Mesh::LoadOBJ(path, ok, outTexture);
    if (EndsWith(p, ".gltf") || EndsWith(p, ".glb"))  return LoadGLTF(path, ok, outTexture);
    if (EndsWith(p, ".fbx")) {
        bool fok = false;
        Mesh m = ImportFbxMerged(path, &fok, outTexture);
        if (fok) { if (ok) *ok = true; return m; }
    }
#ifdef OKAY_HAVE_ASSIMP
    return ViaAssimp(path, ok, outTexture);
#else
    if (ok) *ok = false;
    return Mesh{};   // format needs Assimp, which this build wasn't compiled with
#endif
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
        int base = (int)mesh.vertices.size(), pc = 0, pn = 0;
        if (const JVal* P = attr->Find("POSITION")) {
            auto pos = ReadAccessor(doc, P->Int(-1), pc, pn);
            for (int i = 0; i < pn; ++i) mesh.vertices.push_back(Vec3{pos[i*3+0], pos[i*3+1], pos[i*3+2]});
        }
        if (const JVal* N = attr->Find("NORMAL")) {
            int nc = 0, nn = 0; auto nor = ReadAccessor(doc, N->Int(-1), nc, nn);
            for (int i = 0; i < nn; ++i) mesh.normals.push_back(Vec3{nor[i*3+0], nor[i*3+1], nor[i*3+2]});
        }
        if (const JVal* T = attr->Find("TEXCOORD_0")) {
            int tc = 0, tn = 0; auto uv = ReadAccessor(doc, T->Int(-1), tc, tn);
            for (int i = 0; i < tn; ++i) mesh.uvs.push_back(Vec2{uv[i*2+0], uv[i*2+1]});
        }
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
                    if (w > 0.0f) inf[ni++] = {w, (int)(jv[set][(std::size_t)i * jc[set] + k] + 0.5f)};
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
        if (const JVal* I = prim.Find("indices")) {
            int ic = 0, in = 0; auto id = ReadAccessor(doc, I->Int(-1), ic, in);
            for (int i = 0; i < in; ++i) mesh.triangles.push_back(base + (int)(id[i] + 0.5f));
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

// Apply a resolved glTF material to a MeshRenderer: albedo + normal-map textures,
// base-color tint and emissive color, and a metallic-roughness -> Blinn-Phong
// approximation (metallic drives the specular strength; smoother = tighter highlight).
static void ApplyGltfMaterial(MeshRenderer& mr, const gltf_detail::GltfMat& gm) {
    if (!gm.baseColorTex.empty()) mr.texture   = gm.baseColorTex;
    if (!gm.normalTex.empty())    mr.normalMap = gm.normalTex;
    if (!gm.aoTex.empty())        mr.aoMap     = gm.aoTex;
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

#ifdef OKAY_HAVE_ASSIMP
// Convert assimp's row-major matrix to the engine's column-major Mat4.
static Mat4 AiToMat4(const aiMatrix4x4& a) {
    Mat4 r;
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col)
            r.at(col, row) = a[row][col];
    return r;
}

// Geometry of one aiMesh, untransformed (the node hierarchy carries the transforms).
static Mesh MeshFromAi(const aiMesh* am) {
    Mesh m;
    for (unsigned v = 0; v < am->mNumVertices; ++v) {
        m.vertices.push_back(Vec3{am->mVertices[v].x, am->mVertices[v].y, am->mVertices[v].z});
        if (am->HasNormals())
            m.normals.push_back(Vec3{am->mNormals[v].x, am->mNormals[v].y, am->mNormals[v].z});
        if (am->HasTextureCoords(0))
            m.uvs.push_back(Vec2{am->mTextureCoords[0][v].x, am->mTextureCoords[0][v].y});
    }
    for (unsigned fi = 0; fi < am->mNumFaces; ++fi) {
        const aiFace& f = am->mFaces[fi];
        if (f.mNumIndices == 3)
            for (int k = 0; k < 3; ++k) m.triangles.push_back((int)f.mIndices[k]);
    }
    if (m.normals.size() != m.vertices.size()) m.normals.clear();
    if (m.uvs.size()     != m.vertices.size()) m.uvs.clear();
    return m;
}

// Import via Assimp PRESERVING the node hierarchy, skins and animations — so FBX
// (Mixamo / Meshy AI rigs), Collada etc. come in animated, mirroring the glTF branch:
// one GameObject per node, SkinnedMesh from bones, ModelAnimator clips from channels.
// Returns nullptr when the file can't be loaded (caller falls back / reports).
static GameObject* ImportAssimpSceneGraph(Scene& scene, const std::string& path, bool* ok) {
    Assimp::Importer imp;
    // Fold FBX pivot helper nodes into the real nodes — otherwise every joint
    // explodes into $AssimpFbx$_Translation/_Rotation/... chains.
    imp.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
    const aiScene* sc = imp.ReadFile(path,
        aiProcess_Triangulate | aiProcess_GenSmoothNormals |
        aiProcess_JoinIdenticalVertices | aiProcess_FlipUVs |
        aiProcess_LimitBoneWeights);   // ≤4 weights per vertex (SkinnedMesh blends 4)
    if (!sc || !sc->mRootNode || sc->mNumMeshes == 0) { if (ok) *ok = false; return nullptr; }

    GameObject* root = scene.CreateGameObject(BaseName(path));
    std::unordered_map<std::string, GameObject*> byName;
    struct MeshSite { GameObject* go; const aiMesh* am; };
    std::vector<MeshSite> sites;

    std::unordered_map<std::string, int> nameUse;   // later duplicates get a suffix (first keeps the name bones/channels use)
    std::function<void(const aiNode*, Transform*)> build = [&](const aiNode* n, Transform* parent) {
        std::string nm = n->mName.C_Str();
        std::string objName = nm.empty() ? "node" : nm;
        if (int c = nameUse[objName]++) objName += "_" + std::to_string(c);
        GameObject* g = scene.CreateGameObject(objName);
        aiVector3D s, t; aiQuaternion r;
        n->mTransformation.Decompose(s, r, t);
        g->transform->localPosition = Vec3{t.x, t.y, t.z};
        g->transform->localRotation = Quat{r.x, r.y, r.z, r.w};
        g->transform->localScale    = Vec3{s.x, s.y, s.z};
        g->transform->SetParent(parent, false);
        if (!nm.empty() && !byName.count(nm)) byName[nm] = g;
        for (unsigned i = 0; i < n->mNumMeshes; ++i) {
            GameObject* host = g;
            if (i > 0) {   // extra meshes on this node get child objects
                host = scene.CreateGameObject(g->name + "_m" + std::to_string(i));
                host->transform->SetParent(g->transform, false);
            }
            sites.push_back({host, sc->mMeshes[n->mMeshes[i]]});
        }
        for (unsigned c = 0; c < n->mNumChildren; ++c) build(n->mChildren[c], g->transform);
    };
    build(sc->mRootNode, root->transform);

    for (const MeshSite& site : sites) {
        Mesh bind = MeshFromAi(site.am);
        if (bind.vertices.empty()) continue;
        auto* mr = site.go->AddComponent<MeshRenderer>();
        mr->mesh = bind; mr->doubleSided = true;
        ApplyAssimpMaterial(sc, site.am->mMaterialIndex, path, mr);   // full material, not just albedo

        if (site.am->mNumBones == 0) continue;
        auto* sm = site.go->AddComponent<SkinnedMesh>();
        sm->bind = bind;
        sm->jointIdx.assign(bind.vertices.size(), std::array<int, 4>{0, 0, 0, 0});
        sm->jointWt.assign(bind.vertices.size(), std::array<float, 4>{0, 0, 0, 0});
        for (unsigned b = 0; b < site.am->mNumBones; ++b) {
            const aiBone* bone = site.am->mBones[b];
            std::string bn = bone->mName.C_Str();
            auto it = byName.find(bn);
            sm->joints.push_back(it != byName.end() ? it->second->transform : nullptr);
            sm->jointNames.push_back(bn);
            sm->inverseBind.push_back(AiToMat4(bone->mOffsetMatrix));
            for (unsigned w = 0; w < bone->mNumWeights; ++w) {
                unsigned v = bone->mWeights[w].mVertexId;
                float   wt = bone->mWeights[w].mWeight;
                if (v >= sm->jointWt.size() || wt <= 0.0f) continue;
                // Keep the 4 strongest influences (assimp already limits to 4).
                auto& ws = sm->jointWt[v]; auto& is = sm->jointIdx[v];
                int slot = 0;
                for (int k = 1; k < 4; ++k) if (ws[k] < ws[slot]) slot = k;
                if (wt > ws[slot]) { ws[slot] = wt; is[slot] = (int)b; }
            }
        }
    }

    // Animations -> a ModelAnimator clip library on the root (same as the glTF path).
    if (sc->mNumAnimations > 0) {
        auto* ma = root->AddComponent<ModelAnimator>();
        for (unsigned a = 0; a < sc->mNumAnimations; ++a) {
            const aiAnimation* an = sc->mAnimations[a];
            double tps = an->mTicksPerSecond > 0.0 ? an->mTicksPerSecond : 25.0;
            ModelAnimator::Clip clip;
            std::string cn = an->mName.C_Str();
            // FBX tools often emit "Armature|Walk" style names — keep the last segment.
            { std::size_t bar = cn.find_last_of('|'); if (bar != std::string::npos) cn = cn.substr(bar + 1); }
            clip.name = cn.empty() ? ("clip" + std::to_string(a)) : cn;
            for (unsigned c = 0; c < an->mNumChannels; ++c) {
                const aiNodeAnim* ch = an->mChannels[c];
                std::string nodeName = ch->mNodeName.C_Str();
                if (nodeName.empty() || !byName.count(nodeName)) continue;
                clip.nodes.push_back({nodeName, AnimationClip{}});
                AnimationClip& ac = clip.nodes.back().clip;
                ac.smoothKeys = false;   // sampled data: play back linearly, as authored
                for (unsigned k = 0; k < ch->mNumPositionKeys; ++k) {
                    float t = (float)(ch->mPositionKeys[k].mTime / tps);
                    const aiVector3D& v = ch->mPositionKeys[k].mValue;
                    ac.AddKey("position.x", t, v.x);
                    ac.AddKey("position.y", t, v.y);
                    ac.AddKey("position.z", t, v.z);
                }
                // Keep quaternion keys on one hemisphere: the Animator lerps the raw
                // components, and a sign flip between neighbours reads as a wild spin.
                aiQuaternion prev; bool hasPrev = false;
                for (unsigned k = 0; k < ch->mNumRotationKeys; ++k) {
                    float t = (float)(ch->mRotationKeys[k].mTime / tps);
                    aiQuaternion q = ch->mRotationKeys[k].mValue;
                    if (hasPrev && (q.x*prev.x + q.y*prev.y + q.z*prev.z + q.w*prev.w) < 0.0f) {
                        q.x = -q.x; q.y = -q.y; q.z = -q.z; q.w = -q.w;
                    }
                    prev = q; hasPrev = true;
                    ac.AddKey("rotation.qx", t, q.x);
                    ac.AddKey("rotation.qy", t, q.y);
                    ac.AddKey("rotation.qz", t, q.z);
                    ac.AddKey("rotation.qw", t, q.w);
                }
                for (unsigned k = 0; k < ch->mNumScalingKeys; ++k) {
                    float t = (float)(ch->mScalingKeys[k].mTime / tps);
                    const aiVector3D& v = ch->mScalingKeys[k].mValue;
                    ac.AddKey("scale.x", t, v.x);
                    ac.AddKey("scale.y", t, v.y);
                    ac.AddKey("scale.z", t, v.z);
                }
                if (ac.Tracks().empty()) clip.nodes.pop_back();
            }
            if (!clip.nodes.empty()) ma->clips.push_back(std::move(clip));
        }
        if (!ma->clips.empty()) { ma->active = 0; ma->autoPlay = true; }
    }

    NormalizeImportedClips(root);
    AutoNormalizeImportScale(scene, root);
    if (ok) *ok = true;
    return root;
}
#endif // OKAY_HAVE_ASSIMP

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

ufbx_scene* LoadFbx(const std::string& path, std::string* err) {
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
    ufbx_error e{};
    ufbx_scene* sc = ufbx_load_file(path.c_str(), &opts, &e);
    if (!sc && err) { char buf[512]; ufbx_format_error(buf, sizeof(buf), &e); *err = buf; }
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
};

// Build one material part of a mesh: triangulated, deduplicated, optionally
// with skin influences (indices into the skin's clusters).
Mesh BuildFbxPart(const ufbx_mesh* mesh, const ufbx_mesh_part& part, const ufbx_skin_deformer* skin,
                  std::vector<std::array<int, 4>>* jIdx, std::vector<std::array<float, 4>>* jWt) {
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
            if (skin) {
                const uint32_t vi = mesh->vertex_indices.data[ix];
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
    }
    out.triangles.reserve(indices.size());
    for (uint32_t ix : indices) out.triangles.push_back((int)ix);
    return out;
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

static GameObject* ImportFbxSceneGraph(Scene& scene, const std::string& path, bool* ok, std::string* err) {
    ufbx_scene* sc = LoadFbx(path, err);
    if (!sc) { if (ok) *ok = false; return nullptr; }

    GameObject* root = scene.CreateGameObject(BaseName(path));
    std::vector<GameObject*> goOf(sc->nodes.count, nullptr);   // by node typed_id
    std::unordered_map<std::string, int> nameUse;
    int texSerial = 0;

    // Hierarchy: one GameObject per FBX node (names made unique: clips and
    // skins refer to nodes by name).
    std::function<void(const ufbx_node*, Transform*)> build = [&](const ufbx_node* n, Transform* parent) {
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
        for (const ufbx_node* c : n->children) build(c, g->transform);
    };
    for (const ufbx_node* c : sc->root_node->children) build(c, root->transform);

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
            Mesh bind = BuildFbxPart(mesh, part, skin, skin ? &jIdx : nullptr, skin ? &jWt : nullptr);
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
        }
    }

    // Animation takes -> a ModelAnimator clip library on the root.
    if (sc->anim_stacks.count > 0) {
        auto* ma = root->AddComponent<ModelAnimator>();
        for (const ufbx_anim_stack* st : sc->anim_stacks) {
            ufbx_bake_opts bo{};
            bo.trim_start_time = true;
            bo.resample_rate = 30.0;
            ufbx_error be{};
            ufbx_baked_anim* bake = ufbx_bake_anim(sc, st->anim, &bo, &be);
            if (!bake) continue;
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

GameObject* ImportModelScene(Scene& scene, const std::string& path, bool* ok) {
    using namespace gltf_detail;
    std::string p = Lower(path);
    bool isGltf = EndsWith(p, ".gltf") || EndsWith(p, ".glb");

    // FBX: built in (ufbx), full hierarchy + skins + animations. Assimp (when
    // compiled in) is the fallback for files ufbx can't read.
    if (EndsWith(p, ".fbx")) {
        std::string ferr;
        if (GameObject* r = ImportFbxSceneGraph(scene, path, ok, &ferr)) return r;
    }
    // Non-glTF (OBJ / Assimp): bring it in as one mesh object.
    if (!isGltf) {
#ifdef OKAY_HAVE_ASSIMP
        // Assimp formats: import as a node hierarchy with skins + animations (FBX
        // rigs etc.). Falls through to the flattened single-mesh path on failure.
        if (!EndsWith(p, ".obj"))
            if (GameObject* r = ImportAssimpSceneGraph(scene, path, ok))
                return r;
#endif
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
            for (int j = 0; j < cnt; ++j) {
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
        }
    }

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
                        // LINEAR / STEP data plays back exactly; CUBICSPLINE keeps
                        // the smooth curve (its tangents aren't imported).
                        ac.smoothKeys = cubic;
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
                        for (int k = 0; k < tn; ++k) {
                            float t = times[k];
                            int vbase = (cubic ? (k * 3 + 1) : k) * oc;   // value block for this key
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
        if (auto* sm = g->GetComponent<SkinnedMesh>()) {
            ++r.skinnedMeshes;
            sm->ResolveJoints();
            for (Transform* t : sm->joints) if (t) bones.insert(t);
            r.missingBones += sm->MissingJoints();
            r.unweightedVertices += sm->UnweightedVertices();
            r.maxInfluences = std::max(r.maxInfluences, sm->MaxInfluences());
        }
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
        r.clipNames = ma->ClipNames();
    }
    if (r.missingBones)        r.warnings.push_back(std::to_string(r.missingBones) + " bone(s) referenced by a skin weren't found");
    if (r.unweightedVertices)  r.warnings.push_back(std::to_string(r.unweightedVertices) + " vertices have no bone weights (they won't move)");
    if (r.skeletonRoots > 1)   r.warnings.push_back("the skeleton is split into " + std::to_string(r.skeletonRoots) + " separate bone trees");
    if (r.skinnedMeshes == 0 && r.clips > 0 && r.meshes > 0)
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
    }
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
static void AutoMapLocomotion(ModelAnimator* ma) {
    if (!ma) return;
    auto low = [](std::string v) { for (auto& c : v) c = (char)std::tolower((unsigned char)c); return v; };
    for (const auto& c : ma->clips) {
        std::string n = low(c.name);
        auto has = [&](const char* k) { return n.find(k) != std::string::npos; };
        if (ma->idleClip.empty() && (has("idle") || has("stand") || has("breath"))) ma->idleClip = c.name;
        else if (ma->walkClip.empty() && has("walk")) ma->walkClip = c.name;
        else if (ma->runClip.empty() && (has("run") || has("sprint") || has("jog"))) ma->runClip = c.name;
        else if (ma->jumpClip.empty() && (has("jump") || has("leap"))) ma->jumpClip = c.name;
        else if (ma->fallClip.empty() && (has("fall") || has("air") || has("drop"))) ma->fallClip = c.name;
        else if (ma->landClip.empty() && has("land")) ma->landClip = c.name;
    }
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
        for (const ModelAnimator::NodeClip& nc : c.nodes) {
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
    char buf[160];
    std::snprintf(buf, sizeof(buf), " (%d/%d bone tracks matched%s)", matched, total, retarget ? ", retargeted to this model's proportions" : "");
    say("Added " + std::to_string(added) + " clip" + (added == 1 ? "" : "s") + ": " + names + buf);
    return added;
}

GameObject* AttachCharacterModel(Scene& scene, GameObject* player,
                                 const std::string& path, std::string* outLog) {
    if (!player || !player->transform) return nullptr;
    bool ok = false;
    GameObject* root = ImportModelScene(scene, path, &ok);
    if (!ok || !root || !root->transform) return nullptr;

    // World bounds of the imported subtree (the import root sits at the origin).
    Vec3 lo{0, 0, 0}, hi{0, 0, 0}; bool any = false;
    for (const auto& up : scene.Objects()) {
        GameObject* go = up.get();
        if (!go || !go->IsSelfOrDescendantOf(root)) continue;
        auto* mr = go->GetComponent<MeshRenderer>();
        if (!mr || mr->mesh.vertices.empty()) continue;
        Vec3 blo, bhi; mr->mesh.Bounds(blo, bhi);
        Mat4 l2w = go->transform->LocalToWorldMatrix();
        for (int c = 0; c < 8; ++c) {
            Vec3 w = l2w.MultiplyPoint({c & 1 ? bhi.x : blo.x, c & 2 ? bhi.y : blo.y, c & 4 ? bhi.z : blo.z});
            if (!any) { lo = hi = w; any = true; }
            else {
                lo.x = std::fmin(lo.x, w.x); hi.x = std::fmax(hi.x, w.x);
                lo.y = std::fmin(lo.y, w.y); hi.y = std::fmax(hi.y, w.y);
                lo.z = std::fmin(lo.z, w.z); hi.z = std::fmax(hi.z, w.z);
            }
        }
    }

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
    // The engine's character bodies face LOCAL -Z; most exported models face +Z.
    root->transform->localRotation = Quat::Euler({0.0f, 180.0f, 0.0f});

    // Locomotion: auto-switch idle/walk/run from how fast the player moves, with
    // the clips mapped by name (idle/stand, walk, run/sprint/jog).
    ModelAnimator* ma = root->GetComponent<ModelAnimator>();
    if (!ma)
        for (const auto& up : scene.Objects())
            if (up->IsSelfOrDescendantOf(root))
                if ((ma = up->GetComponent<ModelAnimator>())) break;
    std::string mapped;
    bool genuineIdle = false, genuineWalk = false;
    if (ma && !ma->clips.empty()) {
        auto low = [](std::string v) { for (auto& c : v) c = (char)std::tolower((unsigned char)c); return v; };
        for (const auto& c : ma->clips) {
            std::string n = low(c.name);
            auto has = [&](const char* k) { return n.find(k) != std::string::npos; };
            if (ma->idleClip.empty() && (has("idle") || has("stand") || has("breath"))) ma->idleClip = c.name;
            else if (ma->walkClip.empty() && has("walk")) ma->walkClip = c.name;
            else if (ma->runClip.empty() && (has("run") || has("sprint") || has("jog"))) ma->runClip = c.name;
            else if (ma->jumpClip.empty() && (has("jump") || has("leap"))) ma->jumpClip = c.name;
            else if (ma->fallClip.empty() && (has("fall") || has("air") || has("drop"))) ma->fallClip = c.name;
            else if (ma->landClip.empty() && has("land")) ma->landClip = c.name;
        }
        genuineIdle = !ma->idleClip.empty();
        genuineWalk = !ma->walkClip.empty();
    }
    // Retarget instead of clips when the file doesn't carry a usable locomotion
    // set (a single Mixamo dance, a rigged model with no takes at all...): the
    // Character stays ON in drive-external mode and a HumanoidRetarget plays
    // the BUILT-IN animations on the imported skeleton — so the model walks,
    // runs, jumps, crouches and gestures out of the box. Only when enough
    // humanoid bones are recognizable (decided HERE so the clip path below —
    // including its animated re-grounding — runs whenever retarget doesn't).
    bool wantRetarget = !(ma && !ma->clips.empty() && genuineIdle && genuineWalk);
    if (wantRetarget) {
        wantRetarget = false;
        if (player->GetComponent<Character>()) {
            HumanoidRetarget probe;
            probe.gameObject = player;
            wantRetarget = probe.DetectAndWire(scene, root) >= 8;
        }
    }
    if (ma && !ma->clips.empty() && !wantRetarget) {
        if (ma->idleClip.empty()) ma->idleClip = ma->clips.front().name;  // something is better than T-pose
        if (ma->walkClip.empty()) ma->walkClip = !ma->runClip.empty() ? ma->runClip : ma->idleClip;
        ma->driveByMovement = true;
        ma->smoothLocomotion = true;
        // The controller moves the capsule — the clips must animate in place, or
        // their baked forward travel slides the model out of the collider.
        ma->inPlace = true;
        mapped = "idle='" + ma->idleClip + "' walk='" + ma->walkClip + "'" +
                 (ma->runClip.empty() ? "" : " run='" + ma->runClip + "'");

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
            Vec3 alo{0, 0, 0}, ahi{0, 0, 0}; bool aany = false;
            for (const auto& up : scene.Objects()) {
                GameObject* g = up.get();
                if (!g || !g->IsSelfOrDescendantOf(root)) continue;
                auto* mr = g->GetComponent<MeshRenderer>();
                if (!mr || mr->mesh.vertices.empty()) continue;
                Vec3 blo, bhi; mr->mesh.Bounds(blo, bhi);
                Mat4 l2w = g->transform->LocalToWorldMatrix();
                for (int c = 0; c < 8; ++c) {
                    Vec3 w = l2w.MultiplyPoint({c & 1 ? bhi.x : blo.x, c & 2 ? bhi.y : blo.y, c & 4 ? bhi.z : blo.z});
                    if (!aany) { alo = ahi = w; aany = true; }
                    else {
                        alo.x = std::fmin(alo.x, w.x); ahi.x = std::fmax(ahi.x, w.x);
                        alo.y = std::fmin(alo.y, w.y); ahi.y = std::fmax(ahi.y, w.y);
                        alo.z = std::fmin(alo.z, w.z); ahi.z = std::fmax(ahi.z, w.z);
                    }
                }
            }
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
        if (auto* ch = player->GetComponent<Character>()) {
            HumanoidRetarget* rt = player->GetComponent<HumanoidRetarget>();
            if (!rt) rt = player->AddComponent<HumanoidRetarget>();
            rt->Rewire();
            int found = rt->DetectAndWire(scene, root);
            retargeted = true;
            ch->driveExternal = true;
            // The retarget owns the pose: no locomotion switching and no
            // auto-play (the import turns clip 0 on for the T-pose fix; here
            // it would fight the retarget every frame). One-shots still work.
            if (ma) { ma->driveByMovement = false; ma->inPlace = true; ma->autoPlay = false; }
            mapped += (mapped.empty() ? "" : ", ") + std::string("built-in animations retargeted (") +
                      std::to_string(found) + "/15 bones)";
        }
    }

    // Foot IK, wired automatically: detect the model's leg bones by name (Mixamo
    // etc.) and plant the feet on slopes/steps with the full setup — the same
    // out-of-the-box treatment the Humanoid template gets.
    if (!player->GetComponent<FootIK>()) {
        FootIK probe;
        if (probe.DetectHumanoidBones(scene, root) >= 6) {
            auto* fik = player->AddComponent<FootIK>();
            fik->leftHipName = probe.leftHipName;   fik->leftKneeName = probe.leftKneeName;
            fik->leftFootName = probe.leftFootName;
            fik->rightHipName = probe.rightHipName; fik->rightKneeName = probe.rightKneeName;
            fik->rightFootName = probe.rightFootName;
            fik->pelvisName = probe.pelvisName;
            fik->useRaycast = true; fik->weight = 1.0f;
            fik->adjustPelvis = true; fik->plantDown = true; fik->alignToGround = true;
            fik->maxPelvisShift = 0.35f;
            mapped += mapped.empty() ? "foot IK wired" : ", foot IK wired";
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

    if (outLog) {
        *outLog = "Character model '" + root->name + "' on " + player->name +
                  " (height " + std::to_string(want).substr(0, 4) + "u" +
                  (mapped.empty() ? ", no animation clips" : ", " + mapped) + ")";
    }
    return root;
}

} // namespace okay
