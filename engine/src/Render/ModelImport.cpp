#include "okay/Render/ModelImport.hpp"
#include "okay/Render/Gltf.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/Animator.hpp"
#include "okay/Components/SkinnedMesh.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Math/Mat4.hpp"
#include <array>
#include <cctype>
#include <vector>

#ifdef OKAY_HAVE_ASSIMP
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
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
    return ".obj .gltf .glb .fbx .dae .stl .ply .3ds .blend";
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
static Mesh ViaAssimp(const std::string& path, bool* ok) {
    Assimp::Importer imp;
    const aiScene* sc = imp.ReadFile(path,
        aiProcess_Triangulate | aiProcess_GenSmoothNormals |
        aiProcess_JoinIdenticalVertices | aiProcess_FlipUVs | aiProcess_PreTransformVertices);
    Mesh m;
    if (!sc || !sc->mRootNode || sc->mNumMeshes == 0) { if (ok) *ok = false; return m; }
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
    if (ok) *ok = !m.vertices.empty();
    return m;
}
#endif

Mesh ImportModel(const std::string& path, bool* ok, std::string* outTexture) {
    std::string p = Lower(path);
    if (EndsWith(p, ".obj"))                          return Mesh::LoadOBJ(path, ok, outTexture);
    if (EndsWith(p, ".gltf") || EndsWith(p, ".glb"))  return LoadGLTF(path, ok);
#ifdef OKAY_HAVE_ASSIMP
    return ViaAssimp(path, ok);
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

GameObject* ImportModelScene(Scene& scene, const std::string& path, bool* ok) {
    using namespace gltf_detail;
    std::string p = Lower(path);
    bool isGltf = EndsWith(p, ".gltf") || EndsWith(p, ".glb");

    // Non-glTF (OBJ / Assimp): bring it in as one mesh object.
    if (!isGltf) {
        bool okm = false; std::string tex;
        Mesh m = ImportModel(path, &okm, &tex);
        if (!okm || m.vertices.empty()) { if (ok) *ok = false; return nullptr; }
        GameObject* go = scene.CreateGameObject(BaseName(path));
        auto* mr = go->AddComponent<MeshRenderer>();
        mr->mesh = m; mr->doubleSided = true;
        if (!tex.empty()) mr->texture = tex;
        if (ok) *ok = true;
        return go;
    }

    GltfDoc doc = LoadDoc(path);
    if (!doc.ok) { if (ok) *ok = false; return nullptr; }
    const JVal* nodes = doc.root.Find("nodes");
    int nodeCount = nodes ? (int)nodes->arr.size() : 0;

    GameObject* root = scene.CreateGameObject(BaseName(path));
    if (nodeCount == 0) {   // no scene graph: just merge the meshes onto the root
        bool okm = false; Mesh m = LoadGLTF(path, &okm);
        if (okm && !m.vertices.empty()) { auto* mr = root->AddComponent<MeshRenderer>(); mr->mesh = m; mr->doubleSided = true; }
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
                Mesh mm = BuildMeshAt(doc, M->Int(-1));
                if (!mm.vertices.empty()) { auto* mr = g->AddComponent<MeshRenderer>(); mr->mesh = mm; mr->doubleSided = true; }
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

    // First animation -> an Animator per targeted node (TRS tracks; rotation as a
    // quaternion so it plays back exactly, no euler conversion).
    if (const JVal* anims = doc.root.Find("animations")) {
        if (!anims->arr.empty()) {
            const JVal& anim = anims->arr[0];
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
                    Animator* an = go[node]->GetComponent<Animator>();
                    if (!an) { an = go[node]->AddComponent<Animator>(); an->clip.loop = true; an->playing = true; }
                    int keys = tn < on ? tn : on;
                    for (int k = 0; k < keys; ++k) {
                        float t = times[k];
                        if (tpath == "translation" && oc >= 3) {
                            an->clip.AddKey("position.x", t, vals[k*oc+0]);
                            an->clip.AddKey("position.y", t, vals[k*oc+1]);
                            an->clip.AddKey("position.z", t, vals[k*oc+2]);
                        } else if (tpath == "scale" && oc >= 3) {
                            an->clip.AddKey("scale.x", t, vals[k*oc+0]);
                            an->clip.AddKey("scale.y", t, vals[k*oc+1]);
                            an->clip.AddKey("scale.z", t, vals[k*oc+2]);
                        } else if (tpath == "rotation" && oc >= 4) {
                            an->clip.AddKey("rotation.qx", t, vals[k*oc+0]);
                            an->clip.AddKey("rotation.qy", t, vals[k*oc+1]);
                            an->clip.AddKey("rotation.qz", t, vals[k*oc+2]);
                            an->clip.AddKey("rotation.qw", t, vals[k*oc+3]);
                        }
                    }
                }
            }
        }
    }
    if (ok) *ok = true;
    return root;
}

} // namespace okay
