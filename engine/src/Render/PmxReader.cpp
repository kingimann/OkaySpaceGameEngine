// Our own PMX (MikuMikuDance model, PMX 2.0 and 2.1, binary) reader. Written from the public PMX specification text
// (the header with its eight "globals", the text, vertex, face, texture, material, bone and morph records with the index
// sizes the header chooses) and from reasoning about that structure; no code, table or snippet of any other importer was
// read. Reads the vertices (four weight types, QDEF as four weights), the faces, the materials as one part each, the
// texture table, and the bones as a skeleton. Morphs are walked and checked, not imported (the engine has no morph type).
// Every count and index comes from the file and is checked before use; nothing is allocated from a count that the file
// is too short to hold. Bounds-checked, capped, no recursion. See okay/Render/ModelData.hpp and docs/own_pmx_reader.md.
#include "BinReader.hpp"
#include "SceneRefPath.hpp"
#include "TextReaderUtil.hpp"
#include "okay/Render/ModelData.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace okay {

namespace {
namespace pmx {

using detail::bin::Br;

constexpr std::size_t kMaxFileBytes = 256u * 1024u * 1024u;
constexpr std::size_t kMaxVerts = 2000000, kMaxFaceIndices = 12000000;   // 4,000,000 triangles
constexpr std::size_t kMaxOutVerts = 4000000, kMaxOutTris = 4000000;
constexpr std::size_t kMaxBones = 100000, kMaxMorphs = 100000, kMaxMaterials = 100000, kMaxTextures = 100000;
constexpr std::size_t kMaxMorphOffsets = 8000000, kMaxIkLinks = 4096;
constexpr std::size_t kMaxBonePartProduct = 1000000;   // bones x parts: every part carries the whole bone list
constexpr std::size_t kMaxText = 1u << 20;
constexpr std::size_t kMaxBoneName = 64;   // longer bone names are cut: every part carries a copy of every name
constexpr double kMaxCoord = 1e15;

bool Fail(std::string* err, const std::string& m) { if (err) *err = "PMX: " + m; return false; }

struct Header {
    float version = 0;
    bool utf8 = false;
    int addUv = 0;
    int vsz = 4, tsz = 4, msz = 4, bsz = 4, mosz = 4, rsz = 4;   // vertex, texture, material, bone, morph, rigid body index sizes
};

// A signed index of `sz` bytes (-1 = none). With `isSigned` false (vertex indices) 1 and 2 byte values are read unsigned; a 4 byte
// value that is negative then reads as -1 (invalid).
std::int64_t Idx(Br& b, int sz, bool isSigned) {
    if (sz == 1) { const std::uint8_t v = b.U8(); return isSigned ? (std::int64_t)(std::int8_t)v : (std::int64_t)v; }
    if (sz == 2) { const std::uint16_t v = b.U16(); return isSigned ? (std::int64_t)(std::int16_t)v : (std::int64_t)v; }
    const std::int32_t v = b.I32();
    return v;
}

void AppendUtf8(std::string& s, unsigned cp) {
    if (cp < 0x80) s.push_back((char)cp);
    else if (cp < 0x800) { s.push_back((char)(0xC0 | (cp >> 6))); s.push_back((char)(0x80 | (cp & 0x3F))); }
    else if (cp < 0x10000) { s.push_back((char)(0xE0 | (cp >> 12))); s.push_back((char)(0x80 | ((cp >> 6) & 0x3F))); s.push_back((char)(0x80 | (cp & 0x3F))); }
    else { s.push_back((char)(0xF0 | (cp >> 18))); s.push_back((char)(0x80 | ((cp >> 12) & 0x3F))); s.push_back((char)(0x80 | ((cp >> 6) & 0x3F))); s.push_back((char)(0x80 | (cp & 0x3F))); }
}

// One PMX text: an int32 byte length, then that many bytes of UTF-16LE or UTF-8. A lone UTF-16 surrogate becomes U+FFFD.
bool ReadText(Br& b, bool utf8, std::string& out, std::string& why) {
    out.clear();
    const std::int32_t len = b.I32();
    if (b.bad) { why = "the file ends inside a text length"; return false; }
    if (len < 0 || (std::size_t)len > kMaxText) { why = "a text of " + std::to_string((long long)len) + " bytes (the limit is " + std::to_string(kMaxText) + ")"; return false; }
    if (!b.Has((std::uint64_t)len)) { why = "a text runs past the end of the file"; return false; }
    const unsigned char* p = b.d + b.pos;
    if (utf8) out.assign((const char*)p, (std::size_t)len);
    else {
        if (len % 2) { why = "a UTF-16 text with an odd number of bytes"; return false; }
        for (std::int32_t i = 0; i < len; i += 2) {
            unsigned cp = (unsigned)p[i] | ((unsigned)p[i + 1] << 8);
            if (cp >= 0xD800 && cp <= 0xDBFF) {
                if (i + 3 < len) {
                    const unsigned lo = (unsigned)p[i + 2] | ((unsigned)p[i + 3] << 8);
                    if (lo >= 0xDC00 && lo <= 0xDFFF) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); i += 2; }
                    else cp = 0xFFFD;
                } else cp = 0xFFFD;
            } else if (cp >= 0xDC00 && cp <= 0xDFFF) cp = 0xFFFD;
            AppendUtf8(out, cp);
        }
    }
    b.pos += (std::size_t)len;
    return true;
}

bool FiniteCoord(float f) { return std::isfinite(f) && std::fabs((double)f) <= kMaxCoord; }

struct Vtx { Vec3 p, n; Vec2 uv; int bi[4] = {-1, -1, -1, -1}; float bw[4] = {0, 0, 0, 0}; };
struct Mat { std::string name; ImportedMaterial m; std::int64_t tex = -1; std::uint32_t surfaceIdx = 0; };
struct Bn { std::string name; Vec3 p; std::int64_t parent = -1; };

// Smallest size in bytes of one record, used to refuse a count the rest of the file cannot hold before anything is reserved.
bool Count(Br& b, const char* what, std::int32_t c, std::size_t budget, std::uint64_t minRec, std::string& why) {
    if (b.bad) { why = std::string("the file ends inside the ") + what + " count"; return false; }
    if (c < 0 || (std::size_t)c > budget) { why = std::string(what) + " count " + std::to_string((long long)c) + " is over the limit of " + std::to_string(budget) + " (budget)"; return false; }
    if (!b.Fits((std::uint64_t)c, minRec)) { why = std::string(what) + " count " + std::to_string((long long)c) + " is more than the rest of the file can hold"; return false; }
    return true;
}

} // namespace pmx
} // namespace

static bool ReadPmxImpl(const std::string& bytes, const std::string& baseDir, ImportedModel& out, std::string* err) {
    using namespace pmx;
    Br b(bytes);
    std::string why;
    // ---- header ----
    if (bytes.size() < 9 || std::memcmp(bytes.data(), "PMX ", 4) != 0) return Fail(err, "not a PMX file (no \"PMX \" signature)");
    b.pos = 4;
    Header h;
    h.version = b.F32();
    if (!(h.version >= 2.0f - 1e-4f && h.version <= 2.1f + 1e-4f)) return Fail(err, "unsupported version " + std::to_string(h.version) + " (only 2.0 and 2.1 are read)");
    const bool v21 = h.version > 2.05f;
    const std::uint8_t gc = b.U8();
    if (gc < 8) return Fail(err, "the header lists only " + std::to_string((int)gc) + " globals (8 are needed)");
    if (!b.Has(gc)) return Fail(err, "the file ends inside the header globals");
    std::uint8_t g[8];
    for (int i = 0; i < 8; ++i) g[i] = b.U8();
    b.Skip((std::uint64_t)gc - 8);   // globals a later version added: skipped
    if (g[0] > 1) return Fail(err, "text encoding " + std::to_string((int)g[0]) + " (0 is UTF-16LE, 1 is UTF-8)");
    if (g[1] > 4) return Fail(err, "more than 4 additional UV sets (" + std::to_string((int)g[1]) + ")");
    for (int i = 2; i < 8; ++i) if (g[i] != 1 && g[i] != 2 && g[i] != 4) return Fail(err, "an index size of " + std::to_string((int)g[i]) + " bytes (only 1, 2 and 4 exist)");
    h.utf8 = g[0] == 1; h.addUv = g[1]; h.vsz = g[2]; h.tsz = g[3]; h.msz = g[4]; h.bsz = g[5]; h.mosz = g[6]; h.rsz = g[7];

    std::string modelName, modelNameEn, comment, commentEn;   // the comments are read to find the next field, and not kept
    if (!ReadText(b, h.utf8, modelName, why) || !ReadText(b, h.utf8, modelNameEn, why) || !ReadText(b, h.utf8, comment, why) || !ReadText(b, h.utf8, commentEn, why)) return Fail(err, "model info: " + why);

    // ---- vertices ----
    const std::int32_t nvRaw = b.I32();
    const std::uint64_t minVtx = 12 + 12 + 8 + 16 * (std::uint64_t)h.addUv + 1 + (std::uint64_t)h.bsz + 4;
    if (!Count(b, "vertex", nvRaw, kMaxVerts, minVtx, why)) return Fail(err, why);
    const std::size_t nv = (std::size_t)nvRaw;
    std::vector<Vtx> verts(nv);
    int badWeights = 0, badNormals = 0, badUvs = 0, sdef = 0;
    for (std::size_t i = 0; i < nv; ++i) {
        Vtx& v = verts[i];
        v.p = Vec3{b.F32(), b.F32(), b.F32()};
        v.n = Vec3{b.F32(), b.F32(), b.F32()};
        v.uv = Vec2{b.F32(), b.F32()};
        b.Skip(16 * (std::uint64_t)h.addUv);
        const std::uint8_t wt = b.U8();
        if (b.bad) return Fail(err, "the file ends inside vertex " + std::to_string(i));
        auto bone = [&]() { return (int)std::max<std::int64_t>(-1, std::min<std::int64_t>(Idx(b, h.bsz, true), 2147483646)); };
        if (wt == 0) { v.bi[0] = bone(); v.bw[0] = 1.0f; }
        else if (wt == 1) { v.bi[0] = bone(); v.bi[1] = bone(); const float w = b.F32(); v.bw[0] = w; v.bw[1] = 1.0f - w; if (!(w == w)) ++badWeights; }
        else if (wt == 2 || (wt == 4 && v21)) { for (int k = 0; k < 4; ++k) v.bi[k] = bone(); for (int k = 0; k < 4; ++k) v.bw[k] = b.F32(); }
        else if (wt == 3) { v.bi[0] = bone(); v.bi[1] = bone(); const float w = b.F32(); v.bw[0] = w; v.bw[1] = 1.0f - w; if (!(w == w)) ++badWeights; b.Skip(36); ++sdef; }
        else return Fail(err, "vertex " + std::to_string(i) + " has weight type " + std::to_string((int)wt) + (wt == 4 ? " (QDEF needs version 2.1)" : " (only 0 to 4 exist)"));
        b.F32();   // edge scale
        if (b.bad) return Fail(err, "the file ends inside vertex " + std::to_string(i));
        if (!FiniteCoord(v.p.x) || !FiniteCoord(v.p.y) || !FiniteCoord(v.p.z)) return Fail(err, "vertex " + std::to_string(i) + " has a position that is not a finite number (or is over 1e15)");
        if (!std::isfinite(v.n.x) || !std::isfinite(v.n.y) || !std::isfinite(v.n.z) || std::fabs(v.n.x) > 1e15f || std::fabs(v.n.y) > 1e15f || std::fabs(v.n.z) > 1e15f) { v.n = Vec3{0, 0, 0}; ++badNormals; }
        if (!std::isfinite(v.uv.x) || !std::isfinite(v.uv.y) || std::fabs(v.uv.x) > 1e15f || std::fabs(v.uv.y) > 1e15f) { v.uv = Vec2{0, 0}; ++badUvs; }
        for (int k = 0; k < 4; ++k) if (!std::isfinite(v.bw[k]) || std::fabs(v.bw[k]) > 1e15f) { v.bw[k] = 0.0f; ++badWeights; }
    }

    // ---- faces ----
    const std::int32_t niRaw = b.I32();
    if (!Count(b, "face index", niRaw, kMaxFaceIndices, (std::uint64_t)h.vsz, why)) return Fail(err, why);
    if (niRaw % 3 != 0) return Fail(err, "the face index count " + std::to_string((long long)niRaw) + " is not a multiple of 3");
    const std::size_t ni = (std::size_t)niRaw;
    std::vector<std::uint32_t> faces(ni);
    for (std::size_t i = 0; i < ni; ++i) { const std::int64_t v = Idx(b, h.vsz, false); faces[i] = v < 0 ? 0xFFFFFFFFu : (std::uint32_t)v; }
    if (b.bad) return Fail(err, "the file ends inside the face list");

    // ---- textures ----
    const std::int32_t ntRaw = b.I32();
    if (!Count(b, "texture", ntRaw, kMaxTextures, 4, why)) return Fail(err, why);
    std::vector<std::string> textures((std::size_t)ntRaw);
    for (std::string& t : textures) if (!ReadText(b, h.utf8, t, why)) return Fail(err, "texture table: " + why);

    // ---- materials ----
    const std::int32_t nmRaw = b.I32();
    const std::uint64_t minMat = 4 + 4 + 16 + 12 + 4 + 12 + 1 + 16 + 4 + 2 * (std::uint64_t)h.tsz + 1 + 1 + 1 + 4 + 4;
    if (!Count(b, "material", nmRaw, kMaxMaterials, minMat, why)) return Fail(err, why);
    std::vector<Mat> mats((std::size_t)nmRaw);
    std::uint64_t surfaceTotal = 0;
    int badTexRefs = 0, refusedTex = 0, doubleSided = 0, spheres = 0, toons = 0, edges = 0;
    std::string firstRefused;
    for (std::size_t i = 0; i < mats.size(); ++i) {
        Mat& m = mats[i];
        std::string nameEn, memo;
        if (!ReadText(b, h.utf8, m.name, why) || !ReadText(b, h.utf8, nameEn, why)) return Fail(err, "material " + std::to_string(i) + ": " + why);
        if (m.name.empty()) m.name = nameEn;
        float dif[4], spec[3], amb[3], edge[4];
        for (float& f : dif) f = b.F32();
        for (float& f : spec) f = b.F32();
        const float specPower = b.F32();
        for (float& f : amb) f = b.F32();
        const std::uint8_t flags = b.U8();
        for (float& f : edge) f = b.F32();
        b.F32();   // edge size
        m.tex = Idx(b, h.tsz, true);
        const std::int64_t sphereTex = Idx(b, h.tsz, true);
        const std::uint8_t sphereMode = b.U8();
        const std::uint8_t toonFlag = b.U8();
        if (toonFlag == 0) Idx(b, h.tsz, true); else b.U8();
        if (!ReadText(b, h.utf8, memo, why)) return Fail(err, "material " + std::to_string(i) + ": " + why);
        const std::int32_t surf = b.I32();
        if (b.bad) return Fail(err, "the file ends inside material " + std::to_string(i));
        if (surf < 0 || surf % 3 != 0) return Fail(err, "material " + std::to_string(i) + " covers " + std::to_string((long long)surf) + " face indices (not a multiple of 3, or negative)");
        surfaceTotal += (std::uint64_t)surf;
        if (surfaceTotal > ni) return Fail(err, "the materials cover " + std::to_string((unsigned long long)surfaceTotal) + " face indices, but the file has " + std::to_string(ni));
        m.surfaceIdx = (std::uint32_t)surf;
        for (float f : dif) if (!std::isfinite(f)) return Fail(err, "material " + std::to_string(i) + " has a colour that is not a finite number");
        if (!std::isfinite(specPower) || !std::isfinite(spec[0] + spec[1] + spec[2] + amb[0] + amb[1] + amb[2] + edge[0] + edge[1] + edge[2] + edge[3])) return Fail(err, "material " + std::to_string(i) + " has a value that is not a finite number");
        m.m.name = m.name;
        m.m.hasDiffuse = true;
        m.m.diffuse = Color{std::min(1.0f, std::max(0.0f, dif[0])), std::min(1.0f, std::max(0.0f, dif[1])), std::min(1.0f, std::max(0.0f, dif[2])), std::min(1.0f, std::max(0.0f, dif[3]))};
        if (specPower > 0) m.m.shininess = std::min(specPower, 1e6f);
        if (flags & 1) ++doubleSided;
        if (flags & 16) ++edges;
        if (sphereTex >= 0 && sphereMode != 0) ++spheres;
        if (toonFlag == 0) ++toons;
        // the diffuse texture, kept only when its name stays inside the model's folder
        if (m.tex >= (std::int64_t)textures.size()) { ++badTexRefs; m.tex = -1; }
        if (m.tex >= 0 && !textures[(std::size_t)m.tex].empty()) {
            std::string resolved, whyNot;
            if (detail::ResolveInsideDir(baseDir, textures[(std::size_t)m.tex], resolved, whyNot)) m.m.texture = resolved;
            else { ++refusedTex; if (firstRefused.empty()) firstRefused = textures[(std::size_t)m.tex] + " (" + whyNot + ")"; }
        }
    }

    // ---- bones ----
    const std::int32_t nbRaw = b.I32();
    if (!Count(b, "bone", nbRaw, kMaxBones, 4 + 4 + 12 + (std::uint64_t)h.bsz + 4 + 2 + 1, why)) return Fail(err, why);
    const std::size_t nb = (std::size_t)nbRaw;
    std::vector<Bn> bones(nb);
    int ikBones = 0, inheritBones = 0, fixedAxisBones = 0, localAxisBones = 0, extParentBones = 0, badBoneRefs = 0, selfParents = 0, badParents = 0;
    for (std::size_t i = 0; i < nb; ++i) {
        Bn& bn = bones[i];
        std::string nameEn;
        if (!ReadText(b, h.utf8, bn.name, why) || !ReadText(b, h.utf8, nameEn, why)) return Fail(err, "bone " + std::to_string(i) + ": " + why);
        if (bn.name.empty()) bn.name = nameEn;
        if (bn.name.size() > kMaxBoneName) bn.name.resize(kMaxBoneName);
        bn.p = Vec3{b.F32(), b.F32(), b.F32()};
        bn.parent = Idx(b, h.bsz, true);
        b.I32();   // deform layer
        const std::uint16_t fl = b.U16();
        if (b.bad) return Fail(err, "the file ends inside bone " + std::to_string(i));
        if (!FiniteCoord(bn.p.x) || !FiniteCoord(bn.p.y) || !FiniteCoord(bn.p.z)) return Fail(err, "bone " + std::to_string(i) + " has a position that is not a finite number (or is over 1e15)");
        if (fl & 0x0001) { const std::int64_t t = Idx(b, h.bsz, true); if (t >= (std::int64_t)nb) ++badBoneRefs; }
        else b.Skip(12);
        if (fl & 0x0300) { const std::int64_t t = Idx(b, h.bsz, true); b.F32(); if (t >= (std::int64_t)nb) ++badBoneRefs; ++inheritBones; }
        if (fl & 0x0400) { b.Skip(12); ++fixedAxisBones; }
        if (fl & 0x0800) { b.Skip(24); ++localAxisBones; }
        if (fl & 0x2000) { b.I32(); ++extParentBones; }
        if (fl & 0x0020) {
            ++ikBones;
            const std::int64_t target = Idx(b, h.bsz, true);
            b.I32(); b.F32();
            const std::int32_t links = b.I32();
            if (b.bad) return Fail(err, "the file ends inside the IK data of bone " + std::to_string(i));
            if (target >= (std::int64_t)nb) ++badBoneRefs;
            if (links < 0 || (std::size_t)links > kMaxIkLinks) return Fail(err, "bone " + std::to_string(i) + " has " + std::to_string((long long)links) + " IK links (the limit is " + std::to_string(kMaxIkLinks) + ")");
            if (!b.Fits((std::uint64_t)links, (std::uint64_t)h.bsz + 1)) return Fail(err, "the IK links of bone " + std::to_string(i) + " run past the end of the file");
            for (std::int32_t k = 0; k < links; ++k) {
                const std::int64_t lb = Idx(b, h.bsz, true);
                const std::uint8_t lim = b.U8();
                if (lim) b.Skip(24);
                if (lb >= (std::int64_t)nb) ++badBoneRefs;
            }
        }
        if (b.bad) return Fail(err, "the file ends inside bone " + std::to_string(i));
        if (bn.parent < -1 || bn.parent >= (std::int64_t)nb) { ++badParents; bn.parent = -1; }
        else if (bn.parent == (std::int64_t)i) { ++selfParents; bn.parent = -1; }
    }

    // ---- morphs: walked and checked, not imported ----
    std::int32_t nmoRaw = b.I32();
    if (b.bad) return Fail(err, "the file ends before the morph count");
    std::size_t morphsRead = 0, morphOffsets = 0, badMorphRefs = 0, unknownMorph = 0;
    std::size_t morphKinds[11] = {0};
    bool morphListOk = true;
    if (!Count(b, "morph", nmoRaw, kMaxMorphs, 4 + 4 + 1 + 1 + 4, why)) return Fail(err, why);
    for (std::int32_t mi = 0; morphListOk && mi < nmoRaw; ++mi) {
        std::string n1, n2;
        if (!ReadText(b, h.utf8, n1, why) || !ReadText(b, h.utf8, n2, why)) return Fail(err, "morph " + std::to_string(mi) + ": " + why);
        b.U8();   // panel
        const std::uint8_t type = b.U8();
        const std::int32_t cnt = b.I32();
        if (b.bad) return Fail(err, "the file ends inside morph " + std::to_string(mi));
        const std::uint64_t vsz = (std::uint64_t)h.vsz, msz = (std::uint64_t)h.mosz, bsz = (std::uint64_t)h.bsz, mat = (std::uint64_t)h.msz, rsz = (std::uint64_t)h.rsz;
        std::uint64_t rec = 0;
        switch (type) {
            case 0: rec = msz + 4; break;
            case 1: rec = vsz + 12; break;
            case 2: rec = bsz + 28; break;
            case 3: case 4: case 5: case 6: case 7: rec = vsz + 16; break;
            case 8: rec = mat + 1 + 112; break;
            case 9: rec = v21 ? msz + 4 : 0; break;
            case 10: rec = v21 ? rsz + 1 + 24 : 0; break;
            default: rec = 0;
        }
        if (rec == 0) { ++unknownMorph; morphListOk = false; break; }   // the size of its offsets is unknown: nothing after it can be found
        if (cnt < 0 || morphOffsets + (std::size_t)cnt > kMaxMorphOffsets) return Fail(err, "morph " + std::to_string(mi) + " has " + std::to_string((long long)cnt) + " offsets (negative, or over the file's limit of " + std::to_string(kMaxMorphOffsets) + " morph offsets in all) (budget)");
        if (!b.Fits((std::uint64_t)cnt, rec)) return Fail(err, "the " + std::to_string((long long)cnt) + " offsets of morph " + std::to_string(mi) + " run past the end of the file");
        for (std::int32_t k = 0; k < cnt; ++k) {
            switch (type) {
                case 0: case 9: { const std::int64_t r = Idx(b, h.mosz, true); b.F32(); if (r < 0 || r >= nmoRaw) ++badMorphRefs; break; }
                case 1: { const std::int64_t r = Idx(b, h.vsz, false); b.Skip(12); if (r < 0 || r >= (std::int64_t)nv) ++badMorphRefs; break; }
                case 2: { const std::int64_t r = Idx(b, h.bsz, true); b.Skip(28); if (r < 0 || r >= (std::int64_t)nb) ++badMorphRefs; break; }
                case 8: { const std::int64_t r = Idx(b, h.msz, true); b.Skip(113); if (r < -1 || r >= (std::int64_t)mats.size()) ++badMorphRefs; break; }
                case 10: { const std::int64_t r = Idx(b, h.rsz, true); b.Skip(25); if (r < 0) ++badMorphRefs; break; }
                default: { const std::int64_t r = Idx(b, h.vsz, false); b.Skip(16); if (r < 0 || r >= (std::int64_t)nv) ++badMorphRefs; break; }
            }
        }
        if (b.bad) return Fail(err, "the file ends inside morph " + std::to_string(mi));
        morphOffsets += (std::size_t)cnt;
        ++morphsRead;
        ++morphKinds[type];
    }

    // ---- skeleton: nodes, parents first ----
    out.nodes.clear();
    {
        ImportedNode root;
        root.name = !modelName.empty() ? modelName : (!modelNameEn.empty() ? modelNameEn : std::string("PMX"));
        out.nodes.push_back(root);
    }
    std::vector<int> nodeOf(nb, 0);
    std::vector<std::string> boneNodeName(nb);
    std::vector<Vec3> worldPos(nb);   // bone positions as read, with z mirrored (left-handed to right-handed)
    int loops = 0;
    if (nb > 0) {
        std::vector<int> par(nb);
        for (std::size_t i = 0; i < nb; ++i) { par[i] = (int)bones[i].parent; worldPos[i] = Vec3{bones[i].p.x, bones[i].p.y, -bones[i].p.z}; }
        std::vector<int> order;
        loops = detail::txt::OrderParentsFirst(par, order);
        std::unordered_map<std::string, int> used;
        used[out.nodes[0].name] = 1;
        for (int bi : order) {
            std::string nm = bones[(std::size_t)bi].name.empty() ? "bone" + std::to_string(bi) : bones[(std::size_t)bi].name;
            int& u = used[nm];
            if (u++) { std::string cand; do { cand = nm + "_" + std::to_string(u - 1); ++u; } while (used.count(cand)); nm = cand; used[nm] = 1; }   // the new name must be unused too ("a", "a_1", "a")
            boneNodeName[(std::size_t)bi] = nm;
            ImportedNode nd;
            nd.name = nm;
            const int p = par[(std::size_t)bi];
            nd.parent = p < 0 ? 0 : nodeOf[(std::size_t)p];
            const Vec3 pp = p < 0 ? Vec3{0, 0, 0} : worldPos[(std::size_t)p];
            const Vec3 wp = worldPos[(std::size_t)bi];
            nd.position = Vec3{(float)((double)wp.x - pp.x), (float)((double)wp.y - pp.y), (float)((double)wp.z - pp.z)};
            nodeOf[(std::size_t)bi] = (int)out.nodes.size();
            out.nodes.push_back(std::move(nd));
        }
    }

    // ---- materials and parts: one part per material, in the order of the face list ----
    for (const Mat& m : mats) out.materials.push_back(m.m);
    std::vector<ImportedBone> skin;
    if (nb > 0) {
        skin.resize(nb);
        for (std::size_t i = 0; i < nb; ++i) { skin[i].name = boneNodeName[i]; skin[i].inverseBind = Mat4::Translate(Vec3{-worldPos[i].x, -worldPos[i].y, -worldPos[i].z}); }
    }
    std::size_t outVerts = 0, outTris = 0, partsWithTris = 0;
    std::size_t cursor = 0;
    int badTris = 0, degenerate = 0, badNormalParts = 0, unassigned = 0, badBoneIdx = 0, emptyParts = 0;
    bool needRootBone = false;
    std::vector<ImportedPart> parts;
    for (std::size_t mi = 0; mi < mats.size(); ++mi) {
        const std::size_t end = cursor + mats[mi].surfaceIdx;
        ImportedPart part;
        std::unordered_map<std::uint32_t, int> weld;   // a new map per part: clearing one that grew large would cost its bucket count again for every material
        bool badNormal = false;
        for (std::size_t f = cursor; f + 2 < end; f += 3) {
            const std::uint32_t t[3] = {faces[f], faces[f + 1], faces[f + 2]};
            if (t[0] >= nv || t[1] >= nv || t[2] >= nv) { ++badTris; continue; }
            if (t[0] == t[1] || t[1] == t[2] || t[0] == t[2]) { ++degenerate; continue; }
            int idx[3];
            for (int k = 0; k < 3; ++k) {
                auto it = weld.find(t[k]);
                if (it != weld.end()) { idx[k] = it->second; continue; }
                if (++outVerts > kMaxOutVerts) return Fail(err, "more than " + std::to_string(kMaxOutVerts) + " output vertices (budget)");
                const Vtx& v = verts[t[k]];
                idx[k] = (int)part.mesh.vertices.size();
                weld.emplace(t[k], idx[k]);
                part.mesh.vertices.push_back(Vec3{v.p.x, v.p.y, -v.p.z});
                Vec3 n{v.n.x, v.n.y, -v.n.z};
                const float nm = n.Magnitude();
                if (!(nm > 1e-6f) || !std::isfinite(nm) || nm > 1e15f) { badNormal = true; n = Vec3{0, 1, 0}; } else n = n * (1.0f / nm);
                part.mesh.normals.push_back(n);
                part.mesh.uvs.push_back(v.uv);
                if (nb > 0) {
                    std::array<int, 4> ji{0, 0, 0, 0};
                    std::array<float, 4> jw{0, 0, 0, 0};
                    int cnt = 0;
                    float used = 0;
                    for (int q = 0; q < 4; ++q) {
                        const int bone = v.bi[q];
                        const float w = v.bw[q];
                        if (bone < 0 || !(w > 0.0f)) continue;
                        if ((std::size_t)bone >= nb) { ++badBoneIdx; continue; }
                        int slot = -1;
                        for (int s = 0; s < cnt; ++s) if (ji[(std::size_t)s] == bone) slot = s;
                        if (slot >= 0) { jw[(std::size_t)slot] += w; used += w; }
                        else if (cnt < 4) { ji[(std::size_t)cnt] = bone; jw[(std::size_t)cnt] = w; ++cnt; used += w; }
                    }
                    if (cnt == 0 || !(used > 0.0f) || !std::isfinite(used)) { needRootBone = true; ++unassigned; ji = {(int)nb, 0, 0, 0}; jw = {1, 0, 0, 0}; }
                    else for (float& w : jw) w /= used;
                    part.jointIdx.push_back(ji);
                    part.jointWt.push_back(jw);
                }
            }
            // the faces are clockwise in the left-handed file: after mirroring z the winding is reversed to counter clockwise
            part.mesh.triangles.push_back(idx[0]);
            part.mesh.triangles.push_back(idx[2]);
            part.mesh.triangles.push_back(idx[1]);
            if (++outTris > kMaxOutTris) return Fail(err, "more than " + std::to_string(kMaxOutTris) + " output triangles (budget)");
        }
        cursor = end;
        if (part.mesh.triangles.empty()) { ++emptyParts; continue; }
        part.mesh.name = mats[mi].name;
        if (badNormal) { part.mesh.ComputeSmoothNormals(); ++badNormalParts; }
        part.node = 0;
        part.material = (int)mi;
        parts.push_back(std::move(part));
        ++partsWithTris;
    }
    // faces no material covers are drawn without one
    if (cursor < ni) {
        ImportedPart part;
        std::unordered_map<std::uint32_t, int> weld;
        bool badNormal = false;
        for (std::size_t f = cursor; f + 2 < ni; f += 3) {
            const std::uint32_t t[3] = {faces[f], faces[f + 1], faces[f + 2]};
            if (t[0] >= nv || t[1] >= nv || t[2] >= nv) { ++badTris; continue; }
            if (t[0] == t[1] || t[1] == t[2] || t[0] == t[2]) { ++degenerate; continue; }
            int idx[3];
            for (int k = 0; k < 3; ++k) {
                auto it = weld.find(t[k]);
                if (it != weld.end()) { idx[k] = it->second; continue; }
                if (++outVerts > kMaxOutVerts) return Fail(err, "more than " + std::to_string(kMaxOutVerts) + " output vertices (budget)");
                const Vtx& v = verts[t[k]];
                idx[k] = (int)part.mesh.vertices.size();
                weld.emplace(t[k], idx[k]);
                part.mesh.vertices.push_back(Vec3{v.p.x, v.p.y, -v.p.z});
                Vec3 n{v.n.x, v.n.y, -v.n.z};
                const float nm = n.Magnitude();
                if (!(nm > 1e-6f) || !std::isfinite(nm) || nm > 1e15f) { badNormal = true; n = Vec3{0, 1, 0}; } else n = n * (1.0f / nm);
                part.mesh.normals.push_back(n);
                part.mesh.uvs.push_back(v.uv);
                if (nb > 0) {   // no material means no useful skin either: the part follows the root
                    part.jointIdx.push_back(std::array<int, 4>{(int)nb, 0, 0, 0});
                    part.jointWt.push_back(std::array<float, 4>{1, 0, 0, 0});
                    needRootBone = true;
                }
            }
            part.mesh.triangles.push_back(idx[0]);
            part.mesh.triangles.push_back(idx[2]);
            part.mesh.triangles.push_back(idx[1]);
            if (++outTris > kMaxOutTris) return Fail(err, "more than " + std::to_string(kMaxOutTris) + " output triangles (budget)");
        }
        if (!part.mesh.triangles.empty()) {
            part.mesh.name = "unassigned";
            if (badNormal) { part.mesh.ComputeSmoothNormals(); ++badNormalParts; }
            part.node = 0;
            parts.push_back(std::move(part));
            ++partsWithTris;
        }
    }
    if (parts.empty()) return Fail(err, "the file has no usable triangle");
    if (nb > 0) {
        if (needRootBone) { ImportedBone rb; rb.name = out.nodes[0].name; rb.inverseBind = Mat4::Identity(); skin.push_back(rb); }
        if ((nb + 1) * partsWithTris > kMaxBonePartProduct) return Fail(err, std::to_string(nb) + " bones in " + std::to_string(partsWithTris) + " parts is over the limit of " + std::to_string(kMaxBonePartProduct) + " bone entries (every part carries the whole bone list) (budget)");
        for (ImportedPart& p : parts) p.bones = skin;
    }
    for (ImportedPart& p : parts) out.parts.push_back(std::move(p));
    out.backend = "own";

    // ---- notes ----
    auto note = [&](const std::string& s) { out.notes.push_back("PMX: " + s); };
    note("version " + std::to_string(h.version).substr(0, 3) + ", " + (h.utf8 ? "UTF-8" : "UTF-16LE") + " text; the file is left-handed: z is mirrored and the winding reversed; texture coordinates are kept as written (top-left origin); units are as written (MikuMikuDance models are small)");
    if (loops) note(std::to_string(loops) + " bone parent loop(s) were cut");
    if (selfParents) note(std::to_string(selfParents) + " bone(s) that name themselves as their parent were made roots");
    if (badParents) note(std::to_string(badParents) + " bone(s) with a parent index that does not exist were made roots");
    if (badBoneRefs) note(std::to_string(badBoneRefs) + " bone reference(s) (tail, inherit, IK) to bones that do not exist");
    if (nb > 0) note(std::to_string(nb) + " bone(s) as nodes: positions only (no bone has a rest rotation); inherit-rotation / inherit-move (" + std::to_string(inheritBones) + "), fixed axes (" + std::to_string(fixedAxisBones) + "), local axes (" + std::to_string(localAxisBones) + "), external parents (" + std::to_string(extParentBones) + ") and IK chains (" + std::to_string(ikBones) + ") are not applied");
    if (badWeights) note(std::to_string(badWeights) + " vertex weight(s) that are not finite numbers were set to zero");
    if (sdef) note(std::to_string(sdef) + " SDEF vertex(es) read as two-bone linear blend (the C, R0 and R1 vectors are ignored)");
    if (badBoneIdx) note(std::to_string(badBoneIdx) + " vertex bone reference(s) to bones that do not exist ignored");
    if (unassigned) note(std::to_string(unassigned) + " vertex(es) without a valid bone stay in place (tied to the root node)");
    if (badNormals || badNormalParts) note(std::to_string(badNormals) + " vertex normal(s) not finite, " + std::to_string(badNormalParts) + " part(s) with zero normals got smooth normals");
    if (badUvs) note(std::to_string(badUvs) + " texture coordinate(s) not finite set to 0");
    if (badTris) note(std::to_string(badTris) + " triangle(s) with a vertex index past the vertex list skipped");
    if (degenerate) note(std::to_string(degenerate) + " degenerate triangle(s) skipped");
    if (emptyParts) note(std::to_string(emptyParts) + " material(s) without usable triangles");
    if (cursor < ni) note("the face list is longer than the materials cover: the rest is one part with no material");
    if (badTexRefs) note(std::to_string(badTexRefs) + " material(s) naming a texture that is not in the texture table");
    if (refusedTex) note(std::to_string(refusedTex) + " texture(s) refused (a model may only name files inside its own folder); first: " + firstRefused);
    if (!mats.empty()) note("only the diffuse texture is imported; ambient and specular colours, edge, sphere maps (" + std::to_string(spheres) + "), toon (" + std::to_string(toons) + "), double-sided (" + std::to_string(doubleSided) + ") flags are not");
    if (morphsRead || unknownMorph || nmoRaw > 0) {
        std::string s = std::to_string(morphsRead) + " morph(s) read and checked (" + std::to_string(morphOffsets) + " offsets; group " + std::to_string(morphKinds[0]) + ", vertex " + std::to_string(morphKinds[1]) + ", bone " + std::to_string(morphKinds[2]) + ", UV " + std::to_string(morphKinds[3] + morphKinds[4] + morphKinds[5] + morphKinds[6] + morphKinds[7]) + ", material " + std::to_string(morphKinds[8]) + ", flip " + std::to_string(morphKinds[9]) + ", impulse " + std::to_string(morphKinds[10]) + ") but not imported: the engine has no morph type";
        if (badMorphRefs) s += "; " + std::to_string(badMorphRefs) + " morph offset(s) point at a vertex, bone, material or morph that does not exist";
        if (unknownMorph) s += "; a morph of an unknown type stopped the walk";
        note(s);
    }
    note("display frames, rigid bodies, joints and soft bodies are not read; there is no animation in a PMX file (motions are separate .vmd files)");
    return true;
}

bool ReadPmxBuffer(const std::string& bytes, const std::string& baseDir, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    const bool ok = ReadPmxImpl(bytes, baseDir, out, err);
    if (!ok) out = ImportedModel{};
    return ok;
}

bool ReadPmxFile(const std::string& path, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    std::string bytes;
    if (!detail::txt::ReadFileCapped(path, pmx::kMaxFileBytes, bytes, err)) return false;
    std::string dir;
    const std::size_t sl = path.find_last_of("/\\");
    if (sl != std::string::npos) dir = path.substr(0, sl + 1);
    return ReadPmxBuffer(bytes, dir, out, err);
}

} // namespace okay
