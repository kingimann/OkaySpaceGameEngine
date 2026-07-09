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
#include "okay/Math/Quat.hpp"
#include "okay/Math/Mat4.hpp"
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
    return ".obj .gltf .glb .fbx .dae .stl .ply .3ds .blend .x .md5mesh .smd .ms3d .lwo .dxf .off .ac .b3d";
#else
    return ".obj .gltf .glb";
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
static Mesh BuildSkinnedMesh(const gltf_detail::GltfDoc& doc, int meshIndex,
                             std::vector<std::array<int, 4>>& jIdx,
                             std::vector<std::array<float, 4>>& jWt) {
    using namespace gltf_detail;
    Mesh mesh; jIdx.clear(); jWt.clear();
    const JVal* meshes = doc.root.Find("meshes");
    if (!meshes || meshIndex < 0 || meshIndex >= (int)meshes->arr.size()) return mesh;
    const JVal* prims = meshes->arr[meshIndex].Find("primitives");
    if (!prims) return mesh;
    for (const JVal& prim : prims->arr) {
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
        std::vector<float> jv, wv; int jc = 0, jn = 0, wc = 0, wn = 0;
        if (const JVal* J = attr->Find("JOINTS_0"))  jv = ReadAccessor(doc, J->Int(-1), jc, jn);
        if (const JVal* W = attr->Find("WEIGHTS_0")) wv = ReadAccessor(doc, W->Int(-1), wc, wn);
        for (int i = 0; i < pn; ++i) {
            std::array<int, 4>   idx{0, 0, 0, 0};
            std::array<float, 4> w{1, 0, 0, 0};   // default: fully bound to joint 0
            if (jc >= 4 && i < jn) for (int k = 0; k < 4; ++k) idx[k] = (int)(jv[i*jc+k] + 0.5f);
            if (wc >= 4 && i < wn) for (int k = 0; k < 4; ++k) w[k] = wv[i*wc+k];
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

    std::function<void(const aiNode*, Transform*)> build = [&](const aiNode* n, Transform* parent) {
        std::string nm = n->mName.C_Str();
        GameObject* g = scene.CreateGameObject(nm.empty() ? "node" : nm);
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

    AutoNormalizeImportScale(scene, root);
    if (ok) *ok = true;
    return root;
}
#endif // OKAY_HAVE_ASSIMP

GameObject* ImportModelScene(Scene& scene, const std::string& path, bool* ok) {
    using namespace gltf_detail;
    std::string p = Lower(path);
    bool isGltf = EndsWith(p, ".gltf") || EndsWith(p, ".glb");

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
    for (int i = 0; i < nodeCount; ++i) {
        const JVal& n = nodes->arr[i];
        std::string nm = n.Find("name") ? n.Find("name")->Text() : ("node" + std::to_string(i));
        GameObject* g = scene.CreateGameObject(nm.empty() ? ("node" + std::to_string(i)) : nm);
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

        std::vector<std::array<int, 4>>   jIdx;
        std::vector<std::array<float, 4>> jWt;
        Mesh bind = BuildSkinnedMesh(doc, meshIdx, jIdx, jWt);
        if (bind.vertices.empty()) continue;

        auto* mr = go[ni]->AddComponent<MeshRenderer>();
        mr->mesh = bind; mr->doubleSided = true;
        ApplyGltfMaterial(*mr, ResolveMeshMaterial(doc, meshIdx, path));   // texture animated characters too
        auto* sm = go[ni]->AddComponent<SkinnedMesh>();
        sm->bind = bind; sm->jointIdx = jIdx; sm->jointWt = jWt;

        for (const JVal& jn : jts->arr) {
            int jnode = jn.Int(-1);
            GameObject* jg = (jnode >= 0 && jnode < nodeCount) ? go[jnode] : nullptr;
            sm->joints.push_back(jg ? jg->transform : nullptr);
            sm->jointNames.push_back(jg ? jg->name : std::string{});   // for save/reload
        }
        if (const JVal* IB = skin.Find("inverseBindMatrices")) {
            int c = 0, cnt = 0; auto fv = ReadAccessor(doc, IB->Int(-1), c, cnt);
            for (int j = 0; j < cnt; ++j) {
                Mat4 M; for (int e = 0; e < 16; ++e) M.m[e] = fv[(std::size_t)j*16 + e];
                sm->inverseBind.push_back(M);
            }
        } else {
            sm->inverseBind.assign(sm->joints.size(), Mat4::Identity());
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

                        const std::string& nodeName = go[node]->name;
                        ModelAnimator::NodeClip* ncp = nullptr;
                        for (auto& ncx : clip.nodes) if (ncx.node == nodeName) { ncp = &ncx; break; }
                        if (!ncp) { clip.nodes.push_back({nodeName, AnimationClip{}}); ncp = &clip.nodes.back(); }
                        AnimationClip& ac = ncp->clip;
                        for (int k = 0; k < tn; ++k) {
                            float t = times[k];
                            int vbase = (cubic ? (k * 3 + 1) : k) * oc;   // value block for this key
                            if (vbase + oc > on * oc) break;              // guard against short data
                            if (tpath == "translation" && oc >= 3) {
                                ac.AddKey("position.x", t, vals[vbase+0]);
                                ac.AddKey("position.y", t, vals[vbase+1]);
                                ac.AddKey("position.z", t, vals[vbase+2]);
                            } else if (tpath == "scale" && oc >= 3) {
                                ac.AddKey("scale.x", t, vals[vbase+0]);
                                ac.AddKey("scale.y", t, vals[vbase+1]);
                                ac.AddKey("scale.z", t, vals[vbase+2]);
                            } else if (tpath == "rotation" && oc >= 4) {
                                ac.AddKey("rotation.qx", t, vals[vbase+0]);
                                ac.AddKey("rotation.qy", t, vals[vbase+1]);
                                ac.AddKey("rotation.qz", t, vals[vbase+2]);
                                ac.AddKey("rotation.qw", t, vals[vbase+3]);
                            }
                        }
                    }
                }
                ma->clips.push_back(std::move(clip));
                ++ai;
            }
            ma->active = 0; ma->autoPlay = true;
        }
    }
    AutoNormalizeImportScale(scene, root);
    if (ok) *ok = true;
    return root;
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
    if (ma && !ma->clips.empty()) {
        auto low = [](std::string v) { for (auto& c : v) c = (char)std::tolower((unsigned char)c); return v; };
        for (const auto& c : ma->clips) {
            std::string n = low(c.name);
            auto has = [&](const char* k) { return n.find(k) != std::string::npos; };
            if (ma->idleClip.empty() && (has("idle") || has("stand") || has("breath"))) ma->idleClip = c.name;
            else if (ma->walkClip.empty() && has("walk")) ma->walkClip = c.name;
            else if (ma->runClip.empty() && (has("run") || has("sprint") || has("jog"))) ma->runClip = c.name;
        }
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

    // Hide the default blocky body: the Character stops animating and its mesh +
    // part rig stop rendering — the imported model IS the character now.
    if (auto* ch = player->GetComponent<Character>()) {
        ch->enabled = false;
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
