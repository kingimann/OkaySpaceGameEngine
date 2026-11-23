// Our own MDL reader for two of the formats that use the .mdl extension in games: the Quake 1 model ("IDPO", version 6) and the
// Half-Life 1 "studio" model ("IDST", version 10). Written from memory of the two structures (header, bones, body parts, models,
// meshes, triangle command lists, texture table, skin families, sequences, run-length coded animation values); no document was
// consulted, so the layouts are unverified against any real file. No code from any other importer was read, copied or ported.
// The 3D GameStudio models are read too: MDL3, MDL4 and MDL5 from Conitec's published descriptions (the "3D GameStudio
// Programmer's Manual" of May 2001 and the "Gamestudio file formats" page of the Gamestudio manual), and MDL7, which is not
// publicly documented, from our own byte-by-byte analysis of three real files (docs/content/model_readers.md says which field
// was seen where and what is a guess). For those no SDK, header or importer source of any project was read.
// Bounds-checked, capped, no recursion. See okay/Render/ModelData.hpp and docs/content/model_readers.md (section MDL).
#include "AffineInverse.hpp"
#include "BinReader.hpp"
#include "GameStudioSkin.hpp"
#include "ReaderCommon.hpp"
#include "SceneRefPath.hpp"
#include "TextReaderUtil.hpp"
#include "okay/Render/ModelData.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace okay {

namespace {
namespace mdl {

using detail::bin::Br;
namespace rc = detail::rc;

constexpr std::size_t kMaxFileBytes = 256u * 1024u * 1024u;
// Quake 1
constexpr std::int32_t kMaxQVerts = 200000, kMaxQTris = 400000, kMaxQSkins = 4096, kMaxQFrames = 100000, kMaxSkinDim = 16384;
// Half-Life 1
constexpr std::int32_t kMaxBones = 1024, kMaxBodyParts = 1024, kMaxModelsPerPart = 1024, kMaxMeshes = 4096, kMaxTextures = 4096, kMaxSeqs = 4096,
                       kMaxModelVerts = 200000, kMaxSkinRef = 4096, kMaxFramesPerSeq = 65536;
constexpr std::size_t kMaxOutVerts = 2000000, kMaxOutTris = 2000000, kMaxKeys = 4000000;
// File-wide work budget: every triangle-command vertex read (good or bad), every command and every mesh visited counts, so a hostile
// file that points many meshes or body parts at the same data cannot multiply the work. Meshes and (parts x bones) are capped file-wide.
constexpr std::size_t kMaxWork = 16000000, kMaxFileMeshes = 4096, kMaxPartBones = 1000000;

bool Fail(std::string* err, const std::string& m) { if (err) *err = "MDL: " + m; return false; }

bool Reasonable(float v) { return std::isfinite(v) && std::fabs(v) <= 1e9f; }

// ---- Quake 1 -----------------------------------------------------------------------------------------------------------
bool ReadQuake(Br& b, const std::string& baseDir, ImportedModel& out, std::string* err) {
    (void)baseDir;
    b.pos = 4;
    const std::int32_t version = b.I32();
    if (version != 6) return Fail(err, "Quake model of version " + std::to_string(version) + " (6 is read)");
    float scale[3], trans[3];
    for (float& f : scale) f = b.F32();
    for (float& f : trans) f = b.F32();
    b.F32();                       // bounding radius
    b.Skip(12);                    // eye position
    const std::int32_t numSkins = b.I32(), skinW = b.I32(), skinH = b.I32(), numVerts = b.I32(), numTris = b.I32(), numFrames = b.I32();
    b.Skip(12);                    // synctype, flags, size
    if (b.bad) return Fail(err, "file too short for the header");
    for (int k = 0; k < 3; ++k) if (!Reasonable(scale[k]) || !Reasonable(trans[k])) return Fail(err, "the scale or origin is not finite or is out of range");
    if (numVerts <= 0 || numVerts > kMaxQVerts) return Fail(err, "bad vertex count " + std::to_string(numVerts));
    if (numTris <= 0 || numTris > kMaxQTris) return Fail(err, "bad triangle count " + std::to_string(numTris));
    if (numFrames <= 0 || numFrames > kMaxQFrames) return Fail(err, "bad frame count " + std::to_string(numFrames));
    if (numSkins < 0 || numSkins > kMaxQSkins) return Fail(err, "bad skin count " + std::to_string(numSkins));
    if (skinW <= 0 || skinH <= 0 || skinW > kMaxSkinDim || skinH > kMaxSkinDim) return Fail(err, "bad skin size " + std::to_string(skinW) + " x " + std::to_string(skinH));
    const std::uint64_t skinBytes = (std::uint64_t)skinW * (std::uint64_t)skinH;

    // skins: a single picture, or a group of pictures with times; only their size matters here (the pictures are palette indices)
    std::int32_t groupedSkins = 0;
    for (std::int32_t s = 0; s < numSkins; ++s) {
        const std::int32_t group = b.I32();
        if (b.bad) return Fail(err, "the file ends inside the skin list");
        if (group == 0) { if (!b.Has(skinBytes)) return Fail(err, "the file ends inside a skin"); b.Skip(skinBytes); }
        else {
            const std::int32_t nb = b.I32();
            if (b.bad || nb <= 0 || nb > 4096) return Fail(err, "a skin group with a bad picture count");
            ++groupedSkins;
            if (!b.Has(4ull * (std::uint64_t)nb)) return Fail(err, "the file ends inside a skin group");
            b.Skip(4ull * (std::uint64_t)nb);
            if (!b.Fits((std::uint64_t)nb, skinBytes)) return Fail(err, "the file ends inside a skin group");
            b.Skip((std::uint64_t)nb * skinBytes);
        }
    }
    // texture coordinates: onseam, s, t
    if (!b.Fits((std::uint64_t)numVerts, 12)) return Fail(err, "the file ends inside the texture coordinates");
    struct St { std::int32_t seam, s, t; };
    std::vector<St> st((std::size_t)numVerts);
    for (St& v : st) { v.seam = b.I32(); v.s = b.I32(); v.t = b.I32(); }
    // triangles: facesfront, three vertex indices
    if (!b.Fits((std::uint64_t)numTris, 16)) return Fail(err, "the file ends inside the triangle list");
    struct Tr { std::int32_t front, v[3]; };
    std::vector<Tr> tr((std::size_t)numTris);
    for (Tr& t : tr) { t.front = b.I32(); for (std::int32_t& x : t.v) x = b.I32(); }
    // the first frame: a simple frame (type 0), or a group (type 1: count, bounds, times, then simple frames without the type word)
    const std::int32_t ftype = b.I32();
    if (b.bad) return Fail(err, "the file has no frames");
    if (ftype != 0) {
        const std::int32_t gn = b.I32();
        if (gn <= 0 || gn > kMaxQFrames) return Fail(err, "a frame group with a bad frame count");
        b.Skip(8);
        if (!b.Has(4ull * (std::uint64_t)gn)) return Fail(err, "the file ends inside a frame group");
        b.Skip(4ull * (std::uint64_t)gn);
    }
    b.Skip(8);                     // bounding box min and max (4 bytes each)
    const std::string frameName = b.Str(16);
    if (!b.Fits((std::uint64_t)numVerts, 4)) return Fail(err, "the file ends inside the first frame");
    rc::Soup soup;
    soup.pos.resize((std::size_t)numVerts);
    rc::ReadQuakeFrameVerts(b, scale, trans, soup.pos);
    if (b.bad) return Fail(err, "the file ends inside the first frame");

    int badVert = 0;
    soup.ci.reserve((std::size_t)numTris * 3);
    soup.cuv.reserve((std::size_t)numTris * 3);
    for (const Tr& t : tr) {
        if (t.v[0] < 0 || t.v[1] < 0 || t.v[2] < 0 || t.v[0] >= numVerts || t.v[1] >= numVerts || t.v[2] >= numVerts) { ++badVert; continue; }
        for (int k : {2, 1, 0}) {   // Quake winds clockwise: the order is reversed
            const St& v = st[(std::size_t)t.v[k]];
            // a vertex on the seam belongs to the back half of the skin when the triangle faces away
            double s = (double)v.s;
            if (v.seam != 0 && t.front == 0) s += (double)skinW * 0.5;
            soup.ci.push_back((std::uint32_t)t.v[k]);
            soup.cuv.push_back(Vec2{(float)((s + 0.5) / (double)skinW), (float)(((double)v.t + 0.5) / (double)skinH)});
        }
    }
    ImportedPart part;
    rc::BuildStats stats;
    if (!rc::BuildMesh(soup, 180.0, part.mesh, &stats)) return Fail(err, "too many output vertices");
    if (part.mesh.triangles.empty()) return Fail(err, "the file has no usable triangles");
    part.mesh.name = frameName.empty() ? "MDL" : frameName;
    if (!rc::PartInRange(part)) return Fail(err, "a vertex is out of range");
    ImportedNode root;
    root.name = "MDL";
    out.nodes.push_back(root);
    ImportedMaterial mat;
    mat.name = "MDL skin";
    mat.hasDiffuse = true;
    mat.diffuse = Color{1, 1, 1, 1};
    out.materials.push_back(mat);
    part.node = 0;
    part.material = 0;
    out.parts.push_back(std::move(part));
    rc::ApplyUpAxis(out, true, false);   // Quake is Z-up
    out.backend = "own";
    auto note = [&](const std::string& s) { out.notes.push_back("MDL (Quake): " + s); };
    if (numFrames > 1) note(std::to_string(numFrames) + " frames of vertex animation: only the first frame is read (the engine has no vertex animation)");
    if (numSkins > 0) note(std::to_string(numSkins) + " embedded skin(s), " + std::to_string(skinW) + " x " + std::to_string(skinH) + " 8-bit palette pictures, are not extracted: no texture");
    if (groupedSkins) note(std::to_string(groupedSkins) + " animated skin group(s)");
    note("the stored vertex normals (Quake's table of directions) are not read: smooth normals were generated");
    if (badVert) note(std::to_string(badVert) + " triangle(s) with an out-of-range vertex index skipped");
    if (stats.degenerate) note(std::to_string(stats.degenerate) + " degenerate triangle(s) skipped");
    return true;
}

// ---- Half-Life 1 studio models -----------------------------------------------------------------------------------------
Quat EulerQuat(double rx, double ry, double rz) {
    // rotation about X (roll), then Y (pitch), then Z (yaw): q = qz * qy * qx
    const double sr = std::sin(rx * 0.5), cr = std::cos(rx * 0.5), sp = std::sin(ry * 0.5), cp = std::cos(ry * 0.5), sy = std::sin(rz * 0.5), cy = std::cos(rz * 0.5);
    return Quat{(float)(sr * cp * cy - cr * sp * sy), (float)(cr * sp * cy + sr * cp * sy), (float)(cr * cp * sy - sr * sp * cy), (float)(cr * cp * cy + sr * sp * sy)};
}
rc::M4 QuatMatrix(double x, double y, double z, double w) {
    rc::M4 r;
    SetRotationFromUnitQuat(r, x, y, z, w);
    return r;
}

struct Bone { std::string name; std::int32_t parent = -1; double value[6] = {}, scale[6] = {}; };

bool ReadHalfLife(Br& b, const std::string& baseDir, ImportedModel& out, std::string* err) {
    (void)baseDir;
    b.pos = 4;
    const std::int32_t version = b.I32();
    if (version != 10) return Fail(err, "studio model of version " + std::to_string(version) + " (10, Half-Life 1, is read; 44 to 49 are Half-Life 2 / Source models)");
    b.Skip(64);                    // name
    const std::int32_t length = b.I32();
    b.Skip(5 * 12);                // eye position, min, max, bbmin, bbmax
    b.I32();                       // flags
    const std::int32_t numBones = b.I32(), boneIndex = b.I32();
    b.Skip(8);                     // bone controllers
    b.Skip(8);                     // hit boxes
    const std::int32_t numSeq = b.I32(), seqIndex = b.I32();
    const std::int32_t numSeqGroups = b.I32();
    b.I32();                       // seqgroupindex
    const std::int32_t numTex = b.I32(), texIndex = b.I32();
    b.I32();                       // texturedataindex
    const std::int32_t numSkinRef = b.I32(), numSkinFamilies = b.I32(), skinIndex = b.I32();
    const std::int32_t numBody = b.I32(), bodyIndex = b.I32();
    b.Skip(8 + 16 + 8);            // attachments, sounds, transitions
    if (b.bad || b.pos != 244) return Fail(err, "file too short for the header");
    (void)length;
    if (numBones <= 0 || numBones > kMaxBones) return Fail(err, "bad bone count " + std::to_string(numBones));
    if (numBody < 0 || numBody > kMaxBodyParts) return Fail(err, "bad body part count " + std::to_string(numBody));
    if (numTex < 0 || numTex > kMaxTextures) return Fail(err, "bad texture count " + std::to_string(numTex));
    if (numSeq < 0 || numSeq > kMaxSeqs) return Fail(err, "bad sequence count " + std::to_string(numSeq));
    if (numSkinRef < 0 || numSkinRef > kMaxSkinRef || numSkinFamilies < 0 || numSkinFamilies > 1024) return Fail(err, "bad skin reference counts");
    if (numBody == 0) return Fail(err, "the file has no body parts (a model with only sequences or textures)");

    // ---- bones ----
    std::vector<Bone> bones((std::size_t)numBones);
    if (boneIndex < 0 || !b.Seek((std::uint64_t)boneIndex) || !b.Fits((std::uint64_t)numBones, 112)) return Fail(err, "the bone table lies outside the file");
    for (std::int32_t i = 0; i < numBones; ++i) {
        Bone& bn = bones[(std::size_t)i];
        bn.name = b.Str(32);
        bn.parent = b.I32();
        b.Skip(4 + 24);            // flags, bone controllers
        for (double& v : bn.value) v = (double)b.F32();
        for (double& v : bn.scale) v = (double)b.F32();
        if (bn.parent < -1 || bn.parent >= numBones) return Fail(err, "bone " + std::to_string(i) + " names a parent outside the bone table");
        for (int k = 0; k < 6; ++k) if (!std::isfinite(bn.value[k]) || !std::isfinite(bn.scale[k]) || std::fabs(bn.value[k]) > 1e9 || std::fabs(bn.scale[k]) > 1e9) return Fail(err, "a bone with a value or scale that is not finite or is out of range");
    }
    // skeleton nodes, parents first (a parent loop is cut at the bone that closes it)
    std::vector<int> par((std::size_t)numBones), order;
    for (std::int32_t i = 0; i < numBones; ++i) par[(std::size_t)i] = bones[(std::size_t)i].parent;
    const int loops = detail::txt::OrderParentsFirst(par, order);
    ImportedNode root;
    root.name = "MDL";
    out.nodes.push_back(root);
    std::vector<int> nodeOf((std::size_t)numBones, 0);
    std::vector<std::string> nodeName((std::size_t)numBones);
    std::vector<rc::M4> world((std::size_t)numBones);
    {
        std::unordered_map<std::string, int> used;
        used["MDL"] = 1;
        for (int bi : order) {
            const Bone& bn = bones[(std::size_t)bi];
            std::string nm = bn.name.empty() ? "bone" + std::to_string(bi) : bn.name;
            if (used.count(nm)) { int k = 1; while (used.count(nm + "_" + std::to_string(k))) ++k; nm += "_" + std::to_string(k); }
            used[nm] = 1;
            nodeName[(std::size_t)bi] = nm;
            ImportedNode nd;
            nd.name = nm;
            nd.parent = par[(std::size_t)bi] < 0 ? 0 : nodeOf[(std::size_t)par[(std::size_t)bi]];
            nd.position = Vec3{(float)bn.value[0], (float)bn.value[1], (float)bn.value[2]};
            nd.rotation = EulerQuat(bn.value[3], bn.value[4], bn.value[5]);
            const rc::M4 local = rc::Mul(rc::Translate(bn.value[0], bn.value[1], bn.value[2]), QuatMatrix(nd.rotation.x, nd.rotation.y, nd.rotation.z, nd.rotation.w));
            world[(std::size_t)bi] = par[(std::size_t)bi] < 0 ? local : rc::Mul(world[(std::size_t)par[(std::size_t)bi]], local);
            if (!rc::Finite(world[(std::size_t)bi])) return Fail(err, "bone transforms that multiply out of range");
            nodeOf[(std::size_t)bi] = (int)out.nodes.size();
            out.nodes.push_back(std::move(nd));
        }
    }
    int singular = 0;
    std::vector<ImportedBone> iBones((std::size_t)numBones);
    std::vector<rc::M4> invWorld((std::size_t)numBones);
    for (std::int32_t i = 0; i < numBones; ++i) {
        iBones[(std::size_t)i].name = nodeName[(std::size_t)i];
        if (rc::InvAffine(world[(std::size_t)i], invWorld[(std::size_t)i])) { Mat4 m; for (int k = 0; k < 16; ++k) m.m[k] = (float)invWorld[(std::size_t)i].m[k]; iBones[(std::size_t)i].inverseBind = m; }
        else { ++singular; iBones[(std::size_t)i].inverseBind = Mat4::Identity(); }
    }

    // ---- textures (names and sizes; the pictures are embedded 8-bit palette images, not extracted) ----
    struct Tex { std::int32_t w = 0, h = 0; };
    std::vector<Tex> tex((std::size_t)numTex);
    if (numTex > 0) {
        if (texIndex < 0 || !b.Seek((std::uint64_t)texIndex) || !b.Fits((std::uint64_t)numTex, 80)) return Fail(err, "the texture table lies outside the file");
        for (std::int32_t i = 0; i < numTex; ++i) {
            ImportedMaterial m;
            m.name = b.Str(64);
            b.I32();               // flags
            tex[(std::size_t)i].w = b.I32();
            tex[(std::size_t)i].h = b.I32();
            b.I32();               // data offset
            m.hasDiffuse = true;
            m.diffuse = Color{1, 1, 1, 1};
            out.materials.push_back(std::move(m));
        }
    }
    // skin family 0: mesh skin reference -> texture
    std::vector<int> skinTab;
    if (numSkinRef > 0 && numSkinFamilies > 0) {
        if (skinIndex < 0 || !b.Seek((std::uint64_t)skinIndex) || !b.Fits((std::uint64_t)numSkinRef, 2)) return Fail(err, "the skin table lies outside the file");
        for (std::int32_t i = 0; i < numSkinRef; ++i) skinTab.push_back((int)b.I16());
    }

    // ---- body parts: the first model of each is read ----
    struct BodyPart { std::int32_t nModels, modelIndex; };
    std::vector<BodyPart> parts((std::size_t)numBody);
    if (bodyIndex < 0 || !b.Seek((std::uint64_t)bodyIndex) || !b.Fits((std::uint64_t)numBody, 76)) return Fail(err, "the body part table lies outside the file");
    for (BodyPart& bp : parts) { b.Skip(64); bp.nModels = b.I32(); b.I32(); bp.modelIndex = b.I32(); }
    std::size_t outVerts = 0, outTris = 0;
    int badIdx = 0, badBone = 0, flipped = 0, skippedModels = 0, noTexMesh = 0;
    bool fatalVerts = false;
    std::unordered_set<std::int32_t> seenModels;
    std::size_t work = 0, fileMeshes = 0, partBones = 0;
    int sharedModels = 0;
    for (const BodyPart& bp : parts) {
        if (bp.nModels < 0 || bp.nModels > kMaxModelsPerPart) return Fail(err, "a body part with a bad model count");
        if (bp.nModels == 0) continue;
        skippedModels += bp.nModels - 1;
        if (!seenModels.insert(bp.modelIndex).second) { ++sharedModels; continue; }   // a body part naming a model that was already read: read once
        if (bp.modelIndex < 0 || !b.Seek((std::uint64_t)bp.modelIndex) || !b.Has(112)) return Fail(err, "a model lies outside the file");
        b.Skip(64 + 4 + 4);        // name, type, bounding radius
        const std::int32_t nMesh = b.I32(), meshIndex = b.I32(), nVerts = b.I32(), vertInfo = b.I32(), vertIdx = b.I32(), nNorms = b.I32(), normInfo = b.I32(), normIdx = b.I32();
        if (nMesh < 0 || nMesh > kMaxMeshes || nVerts < 0 || nVerts > kMaxModelVerts || nNorms < 0 || nNorms > kMaxModelVerts) return Fail(err, "a model with a bad mesh, vertex or normal count");
        if (nMesh == 0) continue;
        fileMeshes += (std::size_t)nMesh;
        if (fileMeshes > kMaxFileMeshes) return Fail(err, "more than " + std::to_string(kMaxFileMeshes) + " meshes in the file (budget)");
        work += (std::size_t)nVerts + (std::size_t)nNorms;
        if (work > kMaxWork) return Fail(err, "more than " + std::to_string(kMaxWork) + " units of work (budget)");
        if (nVerts == 0) return Fail(err, "a model with meshes and no vertices");
        // the vertex and normal tables, and the bone of each
        std::vector<Vec3> vtx((std::size_t)nVerts), nrm((std::size_t)nNorms);
        std::vector<std::uint8_t> vbone((std::size_t)nVerts), nbone((std::size_t)nNorms);
        if (vertIdx < 0 || !b.Seek((std::uint64_t)vertIdx) || !b.Fits((std::uint64_t)nVerts, 12)) return Fail(err, "the vertex table lies outside the file");
        for (Vec3& v : vtx) { v.x = b.F32(); v.y = b.F32(); v.z = b.F32(); if (!Reasonable(v.x) || !Reasonable(v.y) || !Reasonable(v.z)) return Fail(err, "a vertex that is not finite or is out of range"); }
        if (vertInfo < 0 || !b.Seek((std::uint64_t)vertInfo) || !b.Fits((std::uint64_t)nVerts, 1)) return Fail(err, "the vertex bone table lies outside the file");
        for (std::uint8_t& v : vbone) v = b.U8();
        if (nNorms > 0) {
            if (normIdx < 0 || !b.Seek((std::uint64_t)normIdx) || !b.Fits((std::uint64_t)nNorms, 12)) return Fail(err, "the normal table lies outside the file");
            for (Vec3& v : nrm) { v.x = b.F32(); v.y = b.F32(); v.z = b.F32(); if (!Reasonable(v.x) || !Reasonable(v.y) || !Reasonable(v.z)) return Fail(err, "a normal that is not finite or is out of range"); }
            if (normInfo < 0 || !b.Seek((std::uint64_t)normInfo) || !b.Fits((std::uint64_t)nNorms, 1)) return Fail(err, "the normal bone table lies outside the file");
            for (std::uint8_t& v : nbone) v = b.U8();
        }
        struct MeshHdr { std::int32_t nTris, triIndex, skinRef; };
        std::vector<MeshHdr> meshes((std::size_t)nMesh);
        if (meshIndex < 0 || !b.Seek((std::uint64_t)meshIndex) || !b.Fits((std::uint64_t)nMesh, 20)) return Fail(err, "the mesh table lies outside the file");
        for (MeshHdr& m : meshes) { m.nTris = b.I32(); m.triIndex = b.I32(); m.skinRef = b.I32(); b.Skip(8); }
        for (std::size_t mi = 0; mi < meshes.size(); ++mi) {
            const MeshHdr& mh = meshes[mi];
            work += 16;
            if (mh.triIndex < 0 || !b.Seek((std::uint64_t)mh.triIndex)) return Fail(err, "a triangle command list starts outside the file");
            int texi = -1;
            if (mh.skinRef >= 0 && (std::size_t)mh.skinRef < skinTab.size()) texi = skinTab[(std::size_t)mh.skinRef];
            if (texi < 0 || texi >= numTex) { texi = -1; ++noTexMesh; }
            const bool haveUv = texi >= 0 && tex[(std::size_t)texi].w > 0 && tex[(std::size_t)texi].h > 0;
            ImportedPart part;
            std::unordered_map<std::uint64_t, int> weld;
            std::vector<Vec3> geoN;   // stored normal per output vertex (for the winding check)
            auto corner = [&](std::uint16_t vi, std::uint16_t ni, std::int16_t s, std::int16_t t, int& idx) -> bool {
                const std::uint64_t key = (std::uint64_t)vi | ((std::uint64_t)ni << 16) | ((std::uint64_t)(std::uint16_t)s << 32) | ((std::uint64_t)(std::uint16_t)t << 48);
                auto it = weld.find(key);
                if (it != weld.end()) { idx = it->second; return true; }
                if (vi >= nVerts || ni >= nNorms) { ++badIdx; return false; }
                if (outVerts >= kMaxOutVerts) { fatalVerts = true; return false; }
                ++outVerts;
                int vb = vbone[vi];
                if (vb >= numBones) { ++badBone; vb = 0; }
                double px, py, pz;
                rc::Apply(world[(std::size_t)vb], vtx[vi].x, vtx[vi].y, vtx[vi].z, px, py, pz);
                int nb = nbone[ni];
                if (nb >= numBones) { ++badBone; nb = 0; }
                const rc::M4& wn = world[(std::size_t)nb];
                const double nx = wn.at(0, 0) * nrm[ni].x + wn.at(1, 0) * nrm[ni].y + wn.at(2, 0) * nrm[ni].z;
                const double ny = wn.at(0, 1) * nrm[ni].x + wn.at(1, 1) * nrm[ni].y + wn.at(2, 1) * nrm[ni].z;
                const double nz = wn.at(0, 2) * nrm[ni].x + wn.at(1, 2) * nrm[ni].y + wn.at(2, 2) * nrm[ni].z;
                const double nl = std::sqrt(nx * nx + ny * ny + nz * nz);
                part.mesh.vertices.push_back(Vec3{(float)px, (float)py, (float)pz});
                part.mesh.normals.push_back(nl > 1e-12 ? Vec3{(float)(nx / nl), (float)(ny / nl), (float)(nz / nl)} : Vec3{0, 0, 1});
                if (haveUv) part.mesh.uvs.push_back(Vec2{(float)s / (float)tex[(std::size_t)texi].w, (float)t / (float)tex[(std::size_t)texi].h});
                part.jointIdx.push_back(std::array<int, 4>{vb, 0, 0, 0});
                part.jointWt.push_back(std::array<float, 4>{1, 0, 0, 0});
                idx = (int)part.mesh.vertices.size() - 1;
                weld.emplace(key, idx);
                return true;
            };
            // the command list: a count (positive: a triangle strip, negative: a fan; 0 ends it) and that many vertices of four shorts
            for (;;) {
                const std::int16_t cnt = b.I16();
                if (b.bad) return Fail(err, "a triangle command list that ends inside the file");
                if (cnt == 0) break;
                const std::uint32_t n = (std::uint32_t)(cnt < 0 ? -(int)cnt : (int)cnt);
                work += (std::size_t)n + 1;
                if (work > kMaxWork) return Fail(err, "more than " + std::to_string(kMaxWork) + " units of work in triangle commands (budget)");
                if (!b.Fits(n, 8)) return Fail(err, "a triangle command list that ends inside the file");
                std::vector<int> v(n, -1);
                std::vector<char> ok(n, 0);
                for (std::uint32_t i = 0; i < n; ++i) {
                    const std::uint16_t vi = b.U16(), ni = b.U16();
                    const std::int16_t s = b.I16(), t = b.I16();
                    int idx = -1;
                    if (corner(vi, ni, s, t, idx)) { v[i] = idx; ok[i] = 1; }
                    else if (fatalVerts) return Fail(err, "more than " + std::to_string(kMaxOutVerts) + " output vertices (budget)");
                }
                const std::size_t trisHere = n >= 2 ? n - 2 : 0;
                if (outTris + trisHere > kMaxOutTris) return Fail(err, "more than " + std::to_string(kMaxOutTris) + " triangles (budget)");
                for (std::uint32_t i = 0; i + 2 < n; ++i) {
                    int a, bb, c;
                    if (cnt > 0) {   // strip: the stored order is clockwise (Half-Life's viewer culls with CW as front), so each triangle is reversed
                        if (i % 2 == 0) { a = (int)i + 2; bb = (int)i + 1; c = (int)i; } else { a = (int)i + 2; bb = (int)i; c = (int)i + 1; }
                    } else { a = (int)i + 2; bb = (int)i + 1; c = 0; }
                    if (!ok[(std::size_t)a] || !ok[(std::size_t)bb] || !ok[(std::size_t)c]) continue;
                    part.mesh.triangles.push_back(v[(std::size_t)a]); part.mesh.triangles.push_back(v[(std::size_t)bb]); part.mesh.triangles.push_back(v[(std::size_t)c]);
                    ++outTris;
                }
            }
            if (part.mesh.triangles.empty()) continue;
            // check the winding against the stored normals: if most triangles face against their normals, the winding is flipped
            {
                std::size_t pos = 0, neg = 0;
                const Mesh& me = part.mesh;
                for (std::size_t t = 0; t + 2 < me.triangles.size(); t += 3) {
                    const Vec3 &p0 = rc::TriCorner(me, t, 0), &p1 = rc::TriCorner(me, t, 1), &p2 = rc::TriCorner(me, t, 2);
                    const Vec3 fn = Vec3::Cross(p1 - p0, p2 - p0);
                    const Vec3 an = me.normals[(std::size_t)me.triangles[t]] + me.normals[(std::size_t)me.triangles[t + 1]] + me.normals[(std::size_t)me.triangles[t + 2]];
                    const double d = (double)fn.x * an.x + (double)fn.y * an.y + (double)fn.z * an.z;
                    if (d > 0) ++pos; else if (d < 0) ++neg;
                }
                if (neg > 2 * pos) { for (std::size_t t = 0; t + 2 < part.mesh.triangles.size(); t += 3) std::swap(part.mesh.triangles[t + 1], part.mesh.triangles[t + 2]); ++flipped; }
            }
            partBones += iBones.size();
            if (partBones > kMaxPartBones) return Fail(err, "more than " + std::to_string(kMaxPartBones) + " bone entries over all parts (budget: parts x bones)");
            part.bones = iBones;
            part.node = 0;
            part.material = texi;
            part.mesh.name = "mesh" + std::to_string(out.parts.size());
            if (!rc::PartInRange(part)) return Fail(err, "a vertex is out of range after its bone transform");
            out.parts.push_back(std::move(part));
        }
    }
    if (out.parts.empty()) return Fail(err, "the file has no triangles");

    // ---- sequences: one clip each (the first blend), from the run-length coded bone values ----
    int externalSeq = 0, multiBlend = 0, badSeq = 0, clipsMade = 0;
    std::size_t keys = 0;
    if (numSeq > 0) {
        if (seqIndex < 0 || !b.Seek((std::uint64_t)seqIndex) || !b.Fits((std::uint64_t)numSeq, 176)) return Fail(err, "the sequence table lies outside the file");
        struct Seq { std::string label; float fps; std::int32_t frames, blends, animIndex, group; };
        std::vector<Seq> seqs((std::size_t)numSeq);
        for (Seq& s : seqs) {
            s.label = b.Str(32);
            s.fps = b.F32();
            b.Skip(4 * 4);                         // flags, activity, actweight, numevents
            b.I32();                               // eventindex
            s.frames = b.I32();
            b.Skip(4 * 4);                         // pivots, motiontype, motionbone
            b.Skip(12 + 4 + 4 + 24);               // linear movement, auto move offsets, bounding box
            s.blends = b.I32();
            s.animIndex = b.I32();
            b.Skip(8 + 8 + 8 + 4);                 // blend types, starts, ends, parent
            s.group = b.I32();
            b.Skip(4 * 4);                         // entry node, exit node, node flags, next sequence
        }
        for (std::size_t si = 0; si < seqs.size(); ++si) {
            const Seq& s = seqs[si];
            if (s.group != 0) { ++externalSeq; continue; }
            if (s.frames <= 0 || s.frames > kMaxFramesPerSeq || s.blends <= 0 || s.animIndex < 0) { ++badSeq; continue; }
            if (s.blends > 1) ++multiBlend;
            const std::size_t F = (std::size_t)s.frames;
            if (keys + F * (std::size_t)numBones > kMaxKeys) return Fail(err, "more than " + std::to_string(kMaxKeys) + " animation keys over all sequences (budget)");
            const float fps = std::isfinite(s.fps) && s.fps >= 0.001f && s.fps <= 100000.0f ? s.fps : 30.0f;
            ImportedClip clip;
            clip.name = s.label.empty() ? "sequence" + std::to_string(si) : s.label;
            bool bad = false;
            for (std::int32_t bi = 0; bi < numBones && !bad; ++bi) {
                const std::uint64_t base = (std::uint64_t)s.animIndex + 12ull * (std::uint64_t)bi;
                if (base + 12 > b.n) { bad = true; break; }
                std::uint16_t off[6];
                for (int j = 0; j < 6; ++j) off[j] = (std::uint16_t)((std::uint16_t)b.d[base + 2 * (std::uint64_t)j] | ((std::uint16_t)b.d[base + 2 * (std::uint64_t)j + 1] << 8));
                bool any = false;
                for (int j = 0; j < 6; ++j) if (off[j]) any = true;
                if (!any) continue;
                // each animated channel: decode every frame in one pass over its runs
                std::vector<double> val(6 * F);
                for (int j = 0; j < 6 && !bad; ++j) {
                    const Bone& bn = bones[(std::size_t)bi];
                    if (off[j] == 0) { for (std::size_t f = 0; f < F; ++f) val[(std::size_t)j * F + f] = bn.value[j]; continue; }
                    std::uint64_t at = base + off[j];
                    std::size_t f = 0;
                    while (f < F) {
                        if (at + 2 > b.n) { bad = true; break; }
                        const unsigned valid = b.d[at], total = b.d[at + 1];
                        if (total == 0 || at + 2 + 2ull * (valid ? valid : 1) > b.n) { bad = true; break; }
                        for (unsigned k = 0; k < total && f < F; ++k, ++f) {
                            const unsigned idx = valid == 0 ? 0 : (k < valid ? k : valid - 1);
                            const std::uint64_t p = at + 2 + 2ull * idx;
                            const std::int16_t raw = valid == 0 ? (std::int16_t)0 : (std::int16_t)(std::uint16_t)(b.d[p] | (b.d[p + 1] << 8));
                            val[(std::size_t)j * F + f] = bn.value[j] + (double)raw * bn.scale[j];
                        }
                        at += 2 + 2ull * valid;
                    }
                }
                if (bad) break;
                ImportedChannel ch;
                ch.node = nodeName[(std::size_t)bi];
                Quat prev{0, 0, 0, 1};
                for (std::size_t f = 0; f < F; ++f) {
                    const float t = (float)((double)f / (double)fps);
                    Quat q = EulerQuat(val[3 * F + f], val[4 * F + f], val[5 * F + f]);
                    if (f > 0 && Quat::Dot(prev, q) < 0) q = Quat{-q.x, -q.y, -q.z, -q.w};
                    prev = q;
                    ch.position.push_back({t, Vec3{(float)val[f], (float)val[F + f], (float)val[2 * F + f]}});
                    ch.rotation.push_back({t, q});
                }
                keys += F;
                clip.channels.push_back(std::move(ch));
            }
            if (bad) { ++badSeq; continue; }
            if (!clip.channels.empty()) { out.clips.push_back(std::move(clip)); ++clipsMade; }
        }
    }

    rc::ApplyUpAxis(out, true, false);   // Half-Life is Z-up
    out.backend = "own";
    auto note = [&](const std::string& s) { out.notes.push_back("MDL (Half-Life): " + s); };
    if (numTex == 0) note("the model has no textures in this file (Half-Life keeps them in a separate \"<name>T.mdl\" file)");
    else note(std::to_string(numTex) + " embedded 8-bit palette texture(s) are not extracted: the materials carry the texture names and no texture file");
    if (sharedModels) note(std::to_string(sharedModels) + " body part(s) name a model that another part already named: read once");
    if (skippedModels) note(std::to_string(skippedModels) + " alternative sub-model(s) of the body parts not read (the first sub-model of each part is)");
    if (numSkinFamilies > 1) note(std::to_string(numSkinFamilies - 1) + " further skin famil(ies) not read");
    if (externalSeq) note(std::to_string(externalSeq) + " sequence(s) live in external sequence group files (name01.mdl ...) and were not read");
    if (multiBlend) note(std::to_string(multiBlend) + " blended sequence(s): only the first animation of each blend is read");
    if (badSeq) note(std::to_string(badSeq) + " sequence(s) with bad or truncated animation data skipped");
    if (numSeqGroups > 1 && !externalSeq) note("sequence groups beyond the first are not read");
    if (loops) note(std::to_string(loops) + " bone parent loop(s) were cut");
    if (singular) note(std::to_string(singular) + " bone(s) with a singular bind transform got an identity inverse bind");
    if (badIdx) note(std::to_string(badIdx) + " triangle corner(s) with a vertex or normal index outside the model skipped");
    if (badBone) note(std::to_string(badBone) + " vertex or normal(s) naming a bone outside the table were bound to the first bone");
    if (flipped) note(std::to_string(flipped) + " mesh(es) had their winding flipped to agree with the stored normals");
    if (noTexMesh) note(std::to_string(noTexMesh) + " mesh(es) name a texture that does not exist: no texture coordinates");
    note("bone controllers, hit boxes, attachments, events, sounds and pivots are not read");
    (void)clipsMade;
    return true;
}

// ---- 3D GameStudio MDL3 / MDL4 / MDL5 -------------------------------------------------------------------------------------
// Conitec's published layout (docs/content/model_readers.md, section MDL): an 84-byte header (magic, an unused word, scale,
// offset, an unused word and vector, skin count, skin width and height (MDL3 / MDL4), vertex, triangle, frame and skin-vertex
// counts, flags, a last word), then the skins, the skin vertices (two shorts, pixels), the triangles (three vertex and three
// skin-vertex shorts), the frames (a type word, a bounding box, a 16-byte name, packed positions: bytes for type 0, unsigned
// shorts for type 2) and, in MDL4, bones that the manual calls "future expansion" (not read).
constexpr std::int32_t kMaxGsVerts = 1000000, kMaxGsTris = 2000000, kMaxGsSkins = 4096, kMaxGsFrames = 100000, kMaxGsSkinVerts = 2000000;
constexpr std::uint64_t kMaxGsSkinPixels = 64ull * 1024 * 1024;

bool ReadGameStudio5(Br& b, ImportedModel& out, std::string* err) {
    const char ver = (char)b.d[3];
    const std::string name = std::string("MDL") + ver;
    auto fail = [&](const std::string& m) { return Fail(err, "(3D GameStudio " + name + ") " + m); };
    b.pos = 4;
    b.I32();                        // not used
    float scale[3], offset[3];
    for (float& f : scale) f = b.F32();
    for (float& f : offset) f = b.F32();
    b.Skip(16);                     // an unused word and an unused vector (the eye position of the Quake header)
    const std::int32_t numSkins = b.I32(), hdrSkinW = b.I32(), hdrSkinH = b.I32(), numVerts = b.I32(), numTris = b.I32(), numFrames = b.I32(), numSkinVerts = b.I32();
    b.Skip(8);                      // flags, numbones (MDL4) or unused
    if (b.bad) return fail("file too short for the header");
    for (int k = 0; k < 3; ++k) if (!Reasonable(scale[k]) || !Reasonable(offset[k])) return fail("the scale or offset is not finite or is out of range");
    if (numVerts <= 0 || numVerts > kMaxGsVerts) return fail("bad vertex count " + std::to_string(numVerts));
    if (numTris <= 0 || numTris > kMaxGsTris) return fail("bad triangle count " + std::to_string(numTris));
    if (numFrames <= 0 || numFrames > kMaxGsFrames) return fail("bad frame count " + std::to_string(numFrames));
    if (numSkins < 0 || numSkins > kMaxGsSkins) return fail("bad skin count " + std::to_string(numSkins));
    if (numSkinVerts < 0 || numSkinVerts > kMaxGsSkinVerts) return fail("bad skin vertex count " + std::to_string(numSkinVerts));
    const bool ownSkinSize = ver == '5';
    if (!ownSkinSize && numSkins > 0 && (hdrSkinW <= 0 || hdrSkinH <= 0 || hdrSkinW > kMaxSkinDim || hdrSkinH > kMaxSkinDim))
        return fail("bad skin size " + std::to_string(hdrSkinW) + " x " + std::to_string(hdrSkinH));
    // skins: only their sizes matter (the pictures are not extracted); the first skin's size measures the skin vertices
    std::int32_t uvW = ownSkinSize ? 0 : hdrSkinW, uvH = ownSkinSize ? 0 : hdrSkinH;
    for (std::int32_t s = 0; s < numSkins; ++s) {
        const std::int32_t type = b.I32();
        std::int32_t w = hdrSkinW, h = hdrSkinH;
        if (ownSkinSize) { w = b.I32(); h = b.I32(); }
        if (b.bad) return fail("the file ends inside the skin list");
        if (w <= 0 || h <= 0 || w > kMaxSkinDim || h > kMaxSkinDim || (std::uint64_t)w * (std::uint64_t)h > kMaxGsSkinPixels) return fail("skin " + std::to_string(s) + " has a bad size");
        const std::uint64_t n = detail::gsskin::PixelBytes(type, w, h);
        if (n == 0) return fail("skin " + std::to_string(s) + " has type " + std::to_string(type) + ", whose size is not known (types 0, 2, 3, 4 and 5, plus 8 with mipmaps, are)");
        if (!b.Has(n)) return fail("the file ends inside skin " + std::to_string(s));
        b.Skip(n);
        if (s == 0 && ownSkinSize) { uvW = w; uvH = h; }
    }
    if (!b.Fits((std::uint64_t)numSkinVerts, 4)) return fail("the file ends inside the skin vertices");
    std::vector<std::array<std::int16_t, 2>> st((std::size_t)numSkinVerts);
    for (auto& v : st) { v[0] = b.I16(); v[1] = b.I16(); }
    if (!b.Fits((std::uint64_t)numTris, 12)) return fail("the file ends inside the triangle list");
    struct Tr { std::int16_t v[3], t[3]; };
    std::vector<Tr> tr((std::size_t)numTris);
    for (Tr& t : tr) { for (std::int16_t& x : t.v) x = b.I16(); for (std::int16_t& x : t.t) x = b.I16(); }
    // the first frame
    const std::int32_t ftype = b.I32();
    if (b.bad) return fail("the file has no frames");
    if (ftype != 0 && ftype != 2) return fail("frame type " + std::to_string(ftype) + " (0, bytes, and 2, words, are known)");
    const std::uint64_t vsz = ftype == 0 ? 4 : 8;
    b.Skip(2 * vsz);                // bounding box
    const std::string frameName = b.Str(16);
    if (b.bad || !b.Fits((std::uint64_t)numVerts, vsz)) return fail("the file ends inside the first frame");
    rc::Soup soup;
    soup.pos.resize((std::size_t)numVerts);
    for (Vec3& p : soup.pos) {
        float r[3];
        for (float& x : r) x = ftype == 0 ? (float)b.U8() : (float)b.U16();
        if (ftype == 0) b.U8(); else b.Skip(2);   // the normal index (and a spare byte): not read
        p = Vec3{r[0] * scale[0] + offset[0], r[1] * scale[1] + offset[1], r[2] * scale[2] + offset[2]};
    }
    if (b.bad) return fail("the file ends inside the first frame");
    const bool haveUv = uvW > 0 && uvH > 0;
    int badVert = 0, badSt = 0;
    soup.ci.reserve((std::size_t)numTris * 3);
    if (haveUv) soup.cuv.reserve((std::size_t)numTris * 3);
    for (const Tr& t : tr) {
        if (t.v[0] < 0 || t.v[1] < 0 || t.v[2] < 0 || t.v[0] >= numVerts || t.v[1] >= numVerts || t.v[2] >= numVerts) { ++badVert; continue; }
        for (int k : {2, 1, 0}) {   // the files wind clockwise (all real files have a negative signed volume): reversed
            soup.ci.push_back((std::uint32_t)t.v[k]);
            if (!haveUv) continue;
            Vec2 uv{0, 0};
            if (t.t[k] >= 0 && t.t[k] < numSkinVerts) {
                const auto& s = st[(std::size_t)t.t[k]];
                uv = Vec2{(float)(((double)s[0] + 0.5) / (double)uvW), (float)(((double)s[1] + 0.5) / (double)uvH)};
            } else ++badSt;
            soup.cuv.push_back(uv);
        }
    }
    ImportedPart part;
    rc::BuildStats stats;
    if (!rc::BuildMesh(soup, 180.0, part.mesh, &stats, kMaxOutVerts)) return fail("too many output vertices");
    if (part.mesh.triangles.empty()) return fail("the file has no usable triangles");
    part.mesh.name = frameName.empty() ? "MDL" : frameName;
    if (!rc::PartInRange(part)) return fail("a vertex is out of range");
    ImportedNode root;
    root.name = "MDL";
    out.nodes.push_back(root);
    ImportedMaterial mat;
    mat.name = "MDL skin";
    mat.hasDiffuse = true;
    mat.diffuse = Color{1, 1, 1, 1};
    out.materials.push_back(mat);
    part.node = 0;
    part.material = 0;
    out.parts.push_back(std::move(part));
    rc::ApplyUpAxis(out, true, false);   // GameStudio is Z-up
    out.backend = "own";
    auto note = [&](const std::string& s) { out.notes.push_back("MDL (3D GameStudio " + name + "): " + s); };
    if (numFrames > 1) note(std::to_string(numFrames) + " frames of vertex animation: only the first frame is read (the engine has no vertex animation)");
    if (numSkins > 0) note(std::to_string(numSkins) + " embedded skin(s) are not extracted (a material names a texture file, it cannot carry pixels): no texture");
    else note("no skin: the skin vertices cannot be measured, so there are no texture coordinates");
    note("the stored vertex normals (an index into a table of directions) are not read: smooth normals were generated");
    if (badVert) note(std::to_string(badVert) + " triangle(s) with an out-of-range vertex index skipped");
    if (badSt) note(std::to_string(badSt) + " corner(s) with an out-of-range skin vertex index got the texture coordinate (0, 0)");
    if (stats.degenerate) note(std::to_string(stats.degenerate) + " degenerate triangle(s) skipped");
    return true;
}

// ---- 3D GameStudio MDL7 ----------------------------------------------------------------------------------------------------
// NOT publicly documented (Conitec's manual says so and points to an SDK, which was not used). The layout below was worked out
// byte by byte from three real files (docs/content/model_readers.md, section MDL, says which field was seen where):
//   header (48 bytes): "MDL7", a version word, bone count, group count, data size (= the file size), two lump sizes, then ten
//     16-bit record sizes: bone, skin, colour, material, skin point, triangle, main vertex, frame vertex, bone transform, frame;
//   bones (bone size each; no file had any, so where they sit is a guess that the group sizes check);
//   groups: type (1 = a mesh), the group's byte size (header included), a 16-byte name, the counts of skins, skin points,
//     triangles, vertices and frames, then the skins (type, width, height, a 16-byte name; type & 7 = 7: `width` bytes of a
//     texture file name; type & 0x10: a material follows: four colours of four floats and a power), the skin points (two
//     floats), the triangles (three vertex shorts, then a skin set: three skin-point shorts and a 32-bit skin index), the
//     vertices (three floats, a bone short, three normal floats) and the frames (a 16-byte name, a frame-vertex count and a
//     bone-transform count, then those records).
// The skins of all groups form one list, which the triangles' skin indices point into.
constexpr std::int32_t kMaxGsGroups = 65536, kMaxGsBones = 65536;
constexpr std::size_t kMaxGsRecord = 4096;

bool ReadGameStudio7(Br& b, const std::string& baseDir, ImportedModel& out, std::string* err) {
    auto fail = [&](const std::string& m) { return Fail(err, "(3D GameStudio MDL7) " + m); };
    if (b.n < 48) return fail("file too short for the header");
    b.pos = 4;
    b.I32();   // a version word, 0 in every file seen
    const std::int32_t numBones = b.I32(), numGroups = b.I32();
    const std::uint32_t dataSize = b.U32();
    const std::uint32_t entLump = b.U32(), medLump = b.U32();
    std::uint16_t sz[10];
    for (std::uint16_t& v : sz) v = b.U16();
    const std::size_t boneSz = sz[0], skinSz = sz[1], matSz = sz[3], stSz = sz[4], triSz = sz[5], vertSz = sz[6], fvSz = sz[7], btSz = sz[8], frameSz = sz[9];
    if (numGroups <= 0 || numGroups > kMaxGsGroups) return fail("bad group count " + std::to_string(numGroups));
    if (numBones < 0 || numBones > kMaxGsBones) return fail("bad bone count " + std::to_string(numBones));
    if (dataSize > b.n) return fail("the data size in the header (" + std::to_string(dataSize) + " bytes) is larger than the file");
    if (skinSz < 12 || skinSz > kMaxGsRecord || stSz < 8 || stSz > kMaxGsRecord || triSz < 16 || triSz > kMaxGsRecord || vertSz < 12 || vertSz > kMaxGsRecord ||
        frameSz < 24 || frameSz > kMaxGsRecord || fvSz > kMaxGsRecord || btSz > kMaxGsRecord || boneSz > kMaxGsRecord || matSz > kMaxGsRecord || (matSz != 0 && matSz < 68))
        return fail("a record size in the header is out of range (skin " + std::to_string(skinSz) + ", skin point " + std::to_string(stSz) + ", triangle " +
                    std::to_string(triSz) + ", vertex " + std::to_string(vertSz) + ", frame " + std::to_string(frameSz) + ")");
    const std::size_t end = dataSize ? (std::size_t)dataSize : b.n;
    std::uint64_t pos = 48 + (std::uint64_t)numBones * boneSz;
    if (pos > end) return fail("the file ends inside the bone list");

    ImportedNode root;
    root.name = "MDL";
    out.nodes.push_back(root);
    std::vector<std::int32_t> skinMaterial;   // skin index -> material index
    int badTri = 0, badSt = 0, badSkin = 0, animFrames = 0, otherGroups = 0, embedded = 0, bonesRef = 0;
    std::size_t outVerts = 0, work = 0;
    rc::BuildStats stats;
    for (std::int32_t g = 0; g < numGroups; ++g) {
        if (end - pos < 44) return fail("the file ends inside the header of group " + std::to_string(g));
        Br gb(b.d, end);
        gb.pos = (std::size_t)pos;
        const std::int32_t type = gb.I32();
        const std::uint32_t gsize = gb.U32();
        const std::string gname = gb.Str(16);
        const std::int32_t nSkins = gb.I32(), nSt = gb.I32(), nTri = gb.I32(), nVert = gb.I32(), nFrames = gb.I32();
        if (gsize < 44 || gsize > end - pos) return fail("group " + std::to_string(g) + " has a bad byte size " + std::to_string(gsize));
        const std::size_t gend = (std::size_t)pos + gsize;
        pos += gsize;
        Br c(b.d, gend);
        c.pos = gb.pos;
        if (nSkins < 0 || nSkins > kMaxGsSkins || nSt < 0 || nTri < 0 || nVert < 0 || nFrames < 0 || nVert > kMaxGsVerts || nTri > kMaxGsTris || nSt > kMaxGsSkinVerts || nFrames > kMaxGsFrames)
            return fail("group " + std::to_string(g) + " has a bad count");
        // skins (also for a group that is not a mesh: the list is shared)
        for (std::int32_t s = 0; s < nSkins; ++s) {
            if (skinMaterial.size() >= (std::size_t)kMaxGsSkins) return fail("more than " + std::to_string(kMaxGsSkins) + " skins");
            const std::size_t at = c.pos;
            const std::int32_t st = c.I32(), w = c.I32(), h = c.I32();
            std::string sname;
            if (skinSz >= 28) sname = c.Str(16);
            c.pos = at;
            c.Skip(skinSz);
            if (c.bad) return fail("the file ends inside a skin of group " + std::to_string(g));
            ImportedMaterial m;
            m.name = sname.empty() ? "skin " + std::to_string(skinMaterial.size()) : sname;
            m.hasDiffuse = true;
            m.diffuse = Color{1, 1, 1, 1};
            const std::int32_t base = st & 7;
            if (st < 0 || st > 0x1F) return fail("skin " + std::to_string(skinMaterial.size()) + " has an unknown type " + std::to_string(st));
            if (base == 7) {   // an external texture: `w` bytes of file name
                if (w < 0 || (std::uint64_t)w > c.Left()) return fail("the file ends inside a texture file name");
                std::string tex = c.Str((std::size_t)w);
                if (!tex.empty()) {
                    std::string resolved, why;
                    if (detail::ResolveInsideDir(baseDir, tex, resolved, why)) m.texture = resolved;
                    else out.notes.push_back("MDL (3D GameStudio MDL7): skin \"" + m.name + "\": texture \"" + tex + "\" not used: " + why);
                }
            } else if (base == 6) {   // a compressed DDS picture of `w` bytes (as in the WMB texture list): skipped
                if (w < 0 || (std::uint64_t)w > c.Left()) return fail("the file ends inside a DDS skin");
                c.Skip((std::uint64_t)w);
                ++embedded;
            } else if (w != 0 || h != 0) {   // embedded pixels: skipped
                if (w <= 0 || h <= 0 || w > kMaxSkinDim || h > kMaxSkinDim) return fail("skin " + std::to_string(skinMaterial.size()) + " has a bad size");
                const std::uint64_t n = detail::gsskin::PixelBytes(st & 0xF, w, h);
                if (n == 0) return fail("skin " + std::to_string(skinMaterial.size()) + " has type " + std::to_string(st) + ", whose size is not known");
                if (!c.Has(n)) return fail("the file ends inside a skin picture");
                c.Skip(n);
                ++embedded;
            }
            if (st & 0x10) {   // a material: diffuse, the second colour (its fourth value is the opacity), specular, emissive, power
                if (matSz < 68 || !c.Has(matSz)) return fail("the file ends inside a material");
                const std::size_t mat = c.pos;
                float f[17];
                for (float& x : f) { x = c.F32(); if (!std::isfinite(x) || std::fabs(x) > 1e6f) x = 0; }
                c.pos = mat + matSz;
                auto clamp01 = [](float x) { return x < 0 ? 0.0f : x > 1 ? 1.0f : x; };
                const float alpha = f[7] > 0 && f[7] <= 1 ? f[7] : 1.0f;
                m.diffuse = Color{clamp01(f[0]), clamp01(f[1]), clamp01(f[2]), alpha};
                if (f[12] != 0 || f[13] != 0 || f[14] != 0) { m.hasEmissive = true; m.emissive = Color{clamp01(f[12]), clamp01(f[13]), clamp01(f[14]), 1}; }
                if (f[16] > 0) m.shininess = std::min(f[16], 10000.0f);
            }
            skinMaterial.push_back((std::int32_t)out.materials.size());
            out.materials.push_back(std::move(m));
        }
        if (type != 1) { ++otherGroups; continue; }
        if (!c.Fits((std::uint64_t)nSt, stSz)) return fail("the file ends inside the skin points of group " + std::to_string(g));
        const std::size_t stAt = c.pos;
        c.Skip((std::uint64_t)nSt * stSz);
        if (!c.Fits((std::uint64_t)nTri, triSz)) return fail("the file ends inside the triangles of group " + std::to_string(g));
        const std::size_t triAt = c.pos;
        c.Skip((std::uint64_t)nTri * triSz);
        if (!c.Fits((std::uint64_t)nVert, vertSz)) return fail("the file ends inside the vertices of group " + std::to_string(g));
        const std::size_t vertAt = c.pos;
        c.Skip((std::uint64_t)nVert * vertSz);
        // frames: only counted (an animation frame carries frame vertices or bone transforms)
        for (std::int32_t f = 0; f < nFrames; ++f) {
            const std::size_t at = c.pos;
            c.Skip(16);
            const std::int32_t nfv = c.I32(), nbt = c.I32();
            c.pos = at;
            c.Skip(frameSz);
            if (c.bad || nfv < 0 || nbt < 0) return fail("a bad frame in group " + std::to_string(g));
            if (!c.Fits((std::uint64_t)nfv, fvSz)) return fail("the file ends inside a frame of group " + std::to_string(g));
            c.Skip((std::uint64_t)nfv * fvSz);
            if (!c.Fits((std::uint64_t)nbt, btSz)) return fail("the file ends inside a frame of group " + std::to_string(g));
            c.Skip((std::uint64_t)nbt * btSz);
            if (nfv > 0 || nbt > 0) ++animFrames;
        }
        work += (std::size_t)nTri + (std::size_t)nVert;
        if (work > kMaxWork) return fail("over the work budget");
        // positions
        std::vector<Vec3> basePos((std::size_t)nVert);
        Br v(b.d, gend);
        v.pos = vertAt;
        for (std::int32_t i = 0; i < nVert; ++i) {
            const std::size_t at = v.pos;
            float p[3];
            for (float& x : p) { x = v.F32(); if (!Reasonable(x)) return fail("a vertex of group " + std::to_string(g) + " is not finite or is out of range"); }
            if (vertSz >= 14 && numBones > 0 && v.I16() >= 0) ++bonesRef;
            v.pos = at + vertSz;
            basePos[(std::size_t)i] = Vec3{p[0], p[1], p[2]};
        }
        // one soup per skin used
        std::vector<std::int32_t> order;
        std::vector<rc::Soup> soups;
        std::vector<std::unordered_map<std::uint32_t, std::uint32_t>> remaps;   // group vertex -> position in that soup (only the used ones are copied)
        std::vector<char> uvOk;
        Br t(b.d, gend);
        t.pos = triAt;
        for (std::int32_t i = 0; i < nTri; ++i) {
            const std::size_t at = t.pos;
            std::int16_t vi[3], si[3];
            for (std::int16_t& x : vi) x = t.I16();
            for (std::int16_t& x : si) x = t.I16();
            const std::int32_t skin = t.I32();
            t.pos = at + triSz;
            if (vi[0] < 0 || vi[1] < 0 || vi[2] < 0 || vi[0] >= nVert || vi[1] >= nVert || vi[2] >= nVert) { ++badTri; continue; }
            std::int32_t mat = -1;
            if (skin >= 0 && (std::size_t)skin < skinMaterial.size()) mat = skinMaterial[(std::size_t)skin];
            else if (!(skin == -1 && skinMaterial.empty())) ++badSkin;
            std::size_t k = 0;
            while (k < order.size() && order[k] != mat) ++k;
            if (k == order.size()) { order.push_back(mat); soups.emplace_back(); remaps.emplace_back(); uvOk.push_back(1); }
            rc::Soup& s = soups[k];
            for (int corner : {2, 1, 0}) {   // the files wind clockwise (their stored normals agree with the reversed order): reversed
                auto it = remaps[k].find((std::uint32_t)vi[corner]);
                if (it == remaps[k].end()) { it = remaps[k].emplace((std::uint32_t)vi[corner], (std::uint32_t)s.pos.size()).first; s.pos.push_back(basePos[(std::size_t)vi[corner]]); }
                s.ci.push_back(it->second);
                Vec2 uv{0, 0};
                if (si[corner] >= 0 && si[corner] < nSt) {
                    Br u(b.d, gend);
                    u.pos = stAt + (std::size_t)si[corner] * stSz;
                    const float uu = u.F32(), vv = u.F32();
                    if (std::isfinite(uu) && std::isfinite(vv) && std::fabs(uu) < 1e8f && std::fabs(vv) < 1e8f) uv = Vec2{uu, vv};
                    else ++badSt;
                } else uvOk[k] = 0;
                s.cuv.push_back(uv);
            }
        }
        ImportedNode node;
        node.name = gname.empty() ? "group " + std::to_string(g) : gname;
        node.parent = 0;
        out.nodes.push_back(node);
        const int nodeIdx = (int)out.nodes.size() - 1;
        for (std::size_t k = 0; k < soups.size(); ++k) {
            if (!uvOk[k]) soups[k].cuv.clear();
            ImportedPart part;
            if (!rc::BuildMesh(soups[k], 180.0, part.mesh, &stats, kMaxOutVerts - std::min(outVerts, kMaxOutVerts))) return fail("over the budget of " + std::to_string(kMaxOutVerts) + " vertices");
            if (part.mesh.triangles.empty()) continue;
            outVerts += part.mesh.vertices.size();
            if (!rc::PartInRange(part)) return fail("a vertex is out of range");
            part.mesh.name = node.name;
            part.node = nodeIdx;
            part.material = order[k];
            out.parts.push_back(std::move(part));
        }
    }
    if (out.parts.empty()) return fail("the file has no usable triangles");
    rc::ApplyUpAxis(out, true, false);   // GameStudio is Z-up
    out.backend = "own";
    auto note = [&](const std::string& s) { out.notes.push_back("MDL (3D GameStudio MDL7): " + s); };
    note("the layout of MDL7 is not published: it was worked out from three real files (docs/content/model_readers.md); the stored normals are not read (smooth normals were generated), texture coordinates are used as stored");
    if (numBones > 0) note(std::to_string(numBones) + " bone(s) not read (no file with bones was available to work out their layout): the model is static" + (bonesRef ? ", " + std::to_string(bonesRef) + " vertex(es) name a bone" : std::string()));
    if (animFrames) note(std::to_string(animFrames) + " animation frame(s) not read: the base vertices are used");
    if (embedded) note(std::to_string(embedded) + " embedded skin picture(s) not extracted (a material names a texture file, it cannot carry pixels)");
    if (otherGroups) note(std::to_string(otherGroups) + " group(s) of a type other than 1 (a mesh) skipped");
    if (entLump || medLump) note("the entity and editor lumps are not read");
    if (badTri) note(std::to_string(badTri) + " triangle(s) with an out-of-range vertex index skipped");
    if (badSkin) note(std::to_string(badSkin) + " triangle(s) name a skin that does not exist: no material");
    if (badSt) note(std::to_string(badSt) + " corner(s) with a bad texture coordinate got (0, 0)");
    if (stats.degenerate) note(std::to_string(stats.degenerate) + " degenerate triangle(s) skipped");
    return true;
}

} // namespace mdl
} // namespace

bool ReadMdlBuffer(const std::string& bytes, const std::string& baseDir, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    bool ok;
    detail::bin::Br b(bytes);
    if (b.n < 8) { out = ImportedModel{}; return mdl::Fail(err, "file too short"); }
    if (std::memcmp(b.d, "IDPO", 4) == 0) ok = mdl::ReadQuake(b, baseDir, out, err);
    else if (std::memcmp(b.d, "IDST", 4) == 0) ok = mdl::ReadHalfLife(b, baseDir, out, err);
    else if (std::memcmp(b.d, "IDSQ", 4) == 0) ok = mdl::Fail(err, "this is a Half-Life sequence group file (IDSQ): it holds animation only, open the model file it belongs to");
    else if (std::memcmp(b.d, "MDL", 3) == 0 && (b.d[3] == '3' || b.d[3] == '4' || b.d[3] == '5')) ok = mdl::ReadGameStudio5(b, out, err);
    else if (std::memcmp(b.d, "MDL7", 4) == 0) ok = mdl::ReadGameStudio7(b, baseDir, out, err);
    else if (std::memcmp(b.d, "MDL", 3) == 0 && b.d[3] >= '0' && b.d[3] <= '9') ok = mdl::Fail(err, std::string("3D GameStudio ") + std::string((const char*)b.d, 4) + " models are not supported by our reader (MDL3, MDL4, MDL5 and MDL7 are)");
    else ok = mdl::Fail(err, "not a supported MDL file (the magic is neither IDPO, Quake 1, nor IDST, Half-Life 1)");
    if (!ok) out = ImportedModel{};
    return ok;
}

bool ReadMdlFile(const std::string& path, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    std::string bytes;
    if (!detail::txt::ReadFileCapped(path, mdl::kMaxFileBytes, bytes, err)) return false;
    std::string dir;
    const std::size_t sl = path.find_last_of("/\\");
    if (sl != std::string::npos) dir = path.substr(0, sl + 1);
    return ReadMdlBuffer(bytes, dir, out, err);
}

} // namespace okay
