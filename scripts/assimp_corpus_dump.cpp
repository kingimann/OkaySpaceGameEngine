// Prints what Assimp reads from a model file in the same TSV shape as `test_model_corpus --list`, so the two can
// be compared with scripts/compare_model_corpus.py. Developer tool for docs/model_corpus.md: not part of any build.
//
//   g++ -O1 -std=c++17 scripts/assimp_corpus_dump.cpp -I<assimp>/include -I<assimp build>/include \
//       <assimp build>/lib/libassimp.a <assimp build>/contrib/zlib/libzlibstatic.a -o assimp_corpus_dump
//   assimp_corpus_dump <test dir> <file under it> ...        one line per file; only Triangulate is applied
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>

struct Box { float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f}; double area = 0; int verts = 0, tris = 0, meshes = 0, nodes = 0; bool uv = false, nrm = false; };

static void Walk(const aiScene* sc, const aiNode* n, const aiMatrix4x4& parent, Box& b) {
    const aiMatrix4x4 w = parent * n->mTransformation;
    ++b.nodes;
    for (unsigned i = 0; i < n->mNumMeshes; ++i) {
        const aiMesh* m = sc->mMeshes[n->mMeshes[i]];
        if (m->mNumVertices == 0) continue;
        ++b.meshes; b.verts += (int)m->mNumVertices;
        if (m->HasTextureCoords(0)) b.uv = true;
        if (m->HasNormals()) b.nrm = true;
        for (unsigned f = 0; f < m->mNumFaces; ++f) {
            if (m->mFaces[f].mNumIndices != 3) continue;
            ++b.tris;
            const aiVector3D a = w * m->mVertices[m->mFaces[f].mIndices[0]], c = w * m->mVertices[m->mFaces[f].mIndices[1]], d = w * m->mVertices[m->mFaces[f].mIndices[2]];
            b.area += 0.5 * ((c - a) ^ (d - a)).Length();
        }
        for (unsigned v = 0; v < m->mNumVertices; ++v) {
            const aiVector3D p = w * m->mVertices[v];
            const float a[3] = {p.x, p.y, p.z};
            for (int k = 0; k < 3; ++k) { b.lo[k] = std::min(b.lo[k], a[k]); b.hi[k] = std::max(b.hi[k], a[k]); }
        }
    }
    for (unsigned c = 0; c < n->mNumChildren; ++c) Walk(sc, n->mChildren[c], w, b);
}

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    const std::string dir = argv[1];
    for (int i = 2; i < argc; ++i) {
        const std::string rel = argv[i];
        Assimp::Importer imp;
        const aiScene* sc = imp.ReadFile((dir + "/" + rel).c_str(), aiProcess_Triangulate);
        if (!sc || !sc->mRootNode) { std::printf("assimp\t%s\tFAIL\t%s\n", rel.c_str(), imp.GetErrorString()); continue; }
        Box b; Walk(sc, sc->mRootNode, aiMatrix4x4(), b);
        std::set<std::string> tex;
        static const aiTextureType types[] = {aiTextureType_DIFFUSE, aiTextureType_SPECULAR, aiTextureType_AMBIENT, aiTextureType_EMISSIVE, aiTextureType_HEIGHT,
            aiTextureType_NORMALS, aiTextureType_SHININESS, aiTextureType_OPACITY, aiTextureType_LIGHTMAP, aiTextureType_BASE_COLOR, aiTextureType_NORMAL_CAMERA,
            aiTextureType_EMISSION_COLOR, aiTextureType_METALNESS, aiTextureType_DIFFUSE_ROUGHNESS, aiTextureType_AMBIENT_OCCLUSION, aiTextureType_UNKNOWN};
        for (unsigned m = 0; m < sc->mNumMaterials; ++m)
            for (aiTextureType t : types)
                for (unsigned k = 0; k < sc->mMaterials[m]->GetTextureCount(t); ++k) {
                    aiString p; if (sc->mMaterials[m]->GetTexture(t, k, &p) == AI_SUCCESS) tex.insert(p.C_Str());
                }
        double dur = 0;
        for (unsigned a = 0; a < sc->mNumAnimations; ++a) {
            const aiAnimation* an = sc->mAnimations[a];
            const double tps = an->mTicksPerSecond > 0 ? an->mTicksPerSecond : 1.0;
            for (unsigned c = 0; c < an->mNumChannels; ++c) {
                const aiNodeAnim* ch = an->mChannels[c];
                if (ch->mNumPositionKeys) dur = std::max(dur, ch->mPositionKeys[ch->mNumPositionKeys - 1].mTime / tps);
                if (ch->mNumRotationKeys) dur = std::max(dur, ch->mRotationKeys[ch->mNumRotationKeys - 1].mTime / tps);
                if (ch->mNumScalingKeys) dur = std::max(dur, ch->mScalingKeys[ch->mNumScalingKeys - 1].mTime / tps);
            }
        }
        if (b.verts == 0) for (int k = 0; k < 3; ++k) b.lo[k] = b.hi[k] = 0;
        std::printf("assimp\t%s\tok\tarea=%.4f\tmeshes=%d\ttris=%d\tclips=%u\tdur=%.3f\tmat=%u\tbbox=%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\tverts=%d\ttex=%zu\tnodes=%d\tuv=%d\tnrm=%d\n",
                    rel.c_str(), b.area, b.meshes, b.tris, sc->mNumAnimations, dur, sc->mNumMaterials, b.lo[0], b.lo[1], b.lo[2], b.hi[0], b.hi[1], b.hi[2],
                    b.verts, tex.size(), b.nodes, b.uv ? 1 : 0, b.nrm ? 1 : 0);
    }
    return 0;
}
