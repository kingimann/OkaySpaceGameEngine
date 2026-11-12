// The ONLY file that includes Assimp. Reads a model with Assimp and hands back
// okay::ImportedModel (our own types); see okay/Render/ModelData.hpp. Built only
// with -DOKAY_USE_ASSIMP=ON.
#include "ModelImportInternal.hpp"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/config.h>
#include <cctype>
#include <cstdio>
#include <unordered_map>

namespace okay {
namespace assimp_backend {

namespace {

// Convert assimp's row-major matrix to the engine's column-major Mat4.
Mat4 AiToMat4(const aiMatrix4x4& a) {
    Mat4 r;
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col)
            r.at(col, row) = a[row][col];
    return r;
}

// Resolve a material texture of the given TYPE to a file the renderer can load:
// an external reference resolves next to the model; an embedded texture (the
// common case for FBX) is written out as a "<model>_<tag><mi>.<ext>" sidecar —
// the same convention the glTF importer uses ("tex" keeps the legacy name).
std::string TexByType(const aiScene* sc, unsigned mi, const std::string& modelPath,
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

std::string DiffuseTexture(const aiScene* sc, unsigned mi, const std::string& modelPath) {
    std::string t = TexByType(sc, mi, modelPath, aiTextureType_DIFFUSE, "tex");
    if (t.empty()) t = TexByType(sc, mi, modelPath, aiTextureType_BASE_COLOR, "tex");  // PBR exports
    return t;
}

// Every material field the importer used to read straight off the aiMaterial.
ImportedMaterial ReadMaterial(const aiScene* sc, unsigned mi, const std::string& modelPath) {
    ImportedMaterial out;
    if (mi >= sc->mNumMaterials) return out;
    const aiMaterial* mat = sc->mMaterials[mi];
    aiString nm; if (mat->Get(AI_MATKEY_NAME, nm) == AI_SUCCESS) out.name = nm.C_Str();
    out.texture = DiffuseTexture(sc, mi, modelPath);
    std::string n = TexByType(sc, mi, modelPath, aiTextureType_NORMALS, "nrm");
    if (n.empty()) n = TexByType(sc, mi, modelPath, aiTextureType_HEIGHT, "nrm");   // OBJ/FBX "bump" slot
    out.normalMap = n;
    std::string s = TexByType(sc, mi, modelPath, aiTextureType_SPECULAR, "spec");
    if (s.empty()) s = TexByType(sc, mi, modelPath, aiTextureType_SHININESS, "spec");
    out.specularMap = s;
    std::string a = TexByType(sc, mi, modelPath, aiTextureType_LIGHTMAP, "ao");     // assimp's AO slot
    if (a.empty()) a = TexByType(sc, mi, modelPath, aiTextureType_AMBIENT_OCCLUSION, "ao");
    out.aoMap = a;
    aiColor4D dc;
    if (mat->Get(AI_MATKEY_COLOR_DIFFUSE, dc) == AI_SUCCESS) { out.hasDiffuse = true; out.diffuse = Color(dc.r, dc.g, dc.b, 1.0f); }
    aiColor4D ec;
    if (mat->Get(AI_MATKEY_COLOR_EMISSIVE, ec) == AI_SUCCESS) { out.hasEmissive = true; out.emissive = Color(ec.r, ec.g, ec.b, 1.0f); }
    float shin = 0.0f;
    if (mat->Get(AI_MATKEY_SHININESS, shin) == AI_SUCCESS) out.shininess = shin;
    float metal = 0.0f;
    if (mat->Get(AI_MATKEY_METALLIC_FACTOR, metal) == AI_SUCCESS) out.metallic = metal;
    float rough = -1.0f;
    if (mat->Get(AI_MATKEY_ROUGHNESS_FACTOR, rough) == AI_SUCCESS) out.roughness = rough;
    return out;
}

// Geometry of one aiMesh, untransformed (the node hierarchy carries the transforms).
Mesh MeshFromAi(const aiMesh* am) {
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

} // namespace

std::string Extensions() {
    std::string out;
    Assimp::Importer imp; aiString exts; imp.GetExtensionList(exts);   // "*.3ds;*.dae;..."
    std::string e = exts.C_Str(), cur;
    auto flush = [&]() {
        if (cur.size() > 1 && cur[0] == '.' && (" " + out + " ").find(" " + cur + " ") == std::string::npos) out += (out.empty() ? "" : " ") + cur;
        cur.clear();
    };
    for (char c : e) {
        if (c == ';') flush();
        else if (c != '*') cur += (char)std::tolower((unsigned char)c);
    }
    flush();
    return out;
}

bool Read(const std::string& path, bool flatten, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    out.backend = "assimp";
    Assimp::Importer imp;
    const aiScene* sc = nullptr;
    if (flatten) {
        sc = imp.ReadFile(path,
            aiProcess_Triangulate | aiProcess_GenSmoothNormals |
            aiProcess_JoinIdenticalVertices | aiProcess_FlipUVs | aiProcess_PreTransformVertices);
    } else {
        // Fold FBX pivot helper nodes into the real nodes — otherwise every joint
        // explodes into $AssimpFbx$_Translation/_Rotation/... chains.
        imp.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
        sc = imp.ReadFile(path,
            aiProcess_Triangulate | aiProcess_GenSmoothNormals |
            aiProcess_JoinIdenticalVertices | aiProcess_FlipUVs |
            aiProcess_LimitBoneWeights);   // <=4 weights per vertex (SkinnedMesh blends 4)
    }
    if (!sc || !sc->mRootNode || sc->mNumMeshes == 0) {
        if (err) *err = imp.GetErrorString() && *imp.GetErrorString() ? imp.GetErrorString() : "Assimp found no meshes";
        return false;
    }

    if (flatten) {
        // One part per aiMesh, vertices already in world space; the first diffuse
        // texture found (materials are scanned in order and the scan stops there).
        out.nodes.push_back(ImportedNode{});
        out.materials.resize(sc->mNumMaterials);
        for (unsigned mi = 0; mi < sc->mNumMaterials; ++mi) {
            std::string t = DiffuseTexture(sc, mi, path);
            if (!t.empty()) { out.materials[mi].texture = t; break; }
        }
        for (unsigned mi = 0; mi < sc->mNumMeshes; ++mi) {
            ImportedPart part;
            part.node = 0;
            part.material = (int)sc->mMeshes[mi]->mMaterialIndex;
            part.mesh = MeshFromAi(sc->mMeshes[mi]);
            out.parts.push_back(std::move(part));
        }
        return true;
    }

    // Node hierarchy, parent-first. Meshes hang on their node in order.
    std::vector<const aiNode*> aiNodes;
    {
        // Pre-order depth-first, children in order.
        struct Walker {
            static void Go(const aiNode* n, int parent, std::vector<const aiNode*>& list, ImportedModel& out) {
                ImportedNode node;
                node.name = n->mName.C_Str();
                node.parent = parent;
                aiVector3D s, t; aiQuaternion r;
                n->mTransformation.Decompose(s, r, t);
                node.position = Vec3{t.x, t.y, t.z};
                node.rotation = Quat{r.x, r.y, r.z, r.w};
                node.scale    = Vec3{s.x, s.y, s.z};
                const int me = (int)out.nodes.size();
                out.nodes.push_back(std::move(node));
                list.push_back(n);
                for (unsigned c = 0; c < n->mNumChildren; ++c) Go(n->mChildren[c], me, list, out);
            }
        };
        Walker::Go(sc->mRootNode, -1, aiNodes, out);
    }

    std::unordered_map<std::string, int> nodeByName;   // first node of a name (what bones / channels refer to)
    for (std::size_t i = 0; i < out.nodes.size(); ++i)
        if (!out.nodes[i].name.empty() && !nodeByName.count(out.nodes[i].name)) nodeByName[out.nodes[i].name] = (int)i;

    out.materials.resize(sc->mNumMaterials);
    std::vector<char> matDone(sc->mNumMaterials, 0);
    for (std::size_t ni = 0; ni < aiNodes.size(); ++ni) {
        const aiNode* n = aiNodes[ni];
        for (unsigned i = 0; i < n->mNumMeshes; ++i) {
            const aiMesh* am = sc->mMeshes[n->mMeshes[i]];
            ImportedPart part;
            part.node = (int)ni;
            part.mesh = MeshFromAi(am);   // may be empty; the scene builder skips those
            part.material = (int)am->mMaterialIndex;
            if (am->mMaterialIndex < sc->mNumMaterials && !matDone[am->mMaterialIndex]) {
                out.materials[am->mMaterialIndex] = ReadMaterial(sc, am->mMaterialIndex, path);
                matDone[am->mMaterialIndex] = 1;
            }
            if (am->mNumBones > 0) {
                part.jointIdx.assign(part.mesh.vertices.size(), std::array<int, 4>{0, 0, 0, 0});
                part.jointWt.assign(part.mesh.vertices.size(), std::array<float, 4>{0, 0, 0, 0});
                for (unsigned b = 0; b < am->mNumBones; ++b) {
                    const aiBone* bone = am->mBones[b];
                    ImportedBone ib; ib.name = bone->mName.C_Str(); ib.inverseBind = AiToMat4(bone->mOffsetMatrix);
                    part.bones.push_back(std::move(ib));
                    for (unsigned w = 0; w < bone->mNumWeights; ++w) {
                        unsigned v = bone->mWeights[w].mVertexId;
                        float   wt = bone->mWeights[w].mWeight;
                        if (v >= part.jointWt.size() || wt <= 0.0f) continue;
                        // Keep the 4 strongest influences (assimp already limits to 4).
                        auto& ws = part.jointWt[v]; auto& is = part.jointIdx[v];
                        int slot = 0;
                        for (int k = 1; k < 4; ++k) if (ws[k] < ws[slot]) slot = k;
                        if (wt > ws[slot]) { ws[slot] = wt; is[slot] = (int)b; }
                    }
                }
            }
            out.parts.push_back(std::move(part));
        }
    }

    for (unsigned a = 0; a < sc->mNumAnimations; ++a) {
        const aiAnimation* an = sc->mAnimations[a];
        const double tps = an->mTicksPerSecond > 0.0 ? an->mTicksPerSecond : 25.0;
        ImportedClip clip;
        std::string cn = an->mName.C_Str();
        // FBX tools often emit "Armature|Walk" style names — keep the last segment.
        { std::size_t bar = cn.find_last_of('|'); if (bar != std::string::npos) cn = cn.substr(bar + 1); }
        clip.name = cn.empty() ? ("clip" + std::to_string(a)) : cn;
        for (unsigned c = 0; c < an->mNumChannels; ++c) {
            const aiNodeAnim* ch = an->mChannels[c];
            ImportedChannel ic; ic.node = ch->mNodeName.C_Str();
            for (unsigned k = 0; k < ch->mNumPositionKeys; ++k) {
                const aiVector3D& v = ch->mPositionKeys[k].mValue;
                ic.position.push_back({(float)(ch->mPositionKeys[k].mTime / tps), Vec3{v.x, v.y, v.z}});
            }
            for (unsigned k = 0; k < ch->mNumRotationKeys; ++k) {
                const aiQuaternion& q = ch->mRotationKeys[k].mValue;
                ic.rotation.push_back({(float)(ch->mRotationKeys[k].mTime / tps), Quat{q.x, q.y, q.z, q.w}});
            }
            for (unsigned k = 0; k < ch->mNumScalingKeys; ++k) {
                const aiVector3D& v = ch->mScalingKeys[k].mValue;
                ic.scale.push_back({(float)(ch->mScalingKeys[k].mTime / tps), Vec3{v.x, v.y, v.z}});
            }
            clip.channels.push_back(std::move(ic));
        }
        out.clips.push_back(std::move(clip));
    }
    return true;
}

} // namespace assimp_backend
} // namespace okay
