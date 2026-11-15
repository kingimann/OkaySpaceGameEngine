#pragma once
// Synthetic fixtures for the own PMX and LWS / MOT readers, built in memory (nothing binary is committed). Every number is chosen
// so it can be checked by hand; the comment above each builder says what the numbers are. Written from the structure of the two
// formats, not from any file or importer: no real .pmx / .lws file is in this repository.
#include "model_fixture_util.hpp"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace fx {

// ---- PMX writer ---------------------------------------------------------------------------------------------------
struct PmxOpts {
    float version = 2.0f;
    bool utf8 = false;
    int vsz = 2, tsz = 1, msz = 1, bsz = 2, mosz = 1, rsz = 1;
    int addUv = 0;
    bool qdef = false;      // make vertex 5 a QDEF vertex (needs version 2.1)
    bool flipMorph = false; // add a flip morph (2.1)
    std::string texName = "tex\\a.png";
    std::string boneName[3] = {"root", "child", "extra"};
    long long par[3] = {-1, 0, 1};   // the parent bone of each bone
};

struct PmxW {
    std::string s;
    PmxOpts o;
    explicit PmxW(const PmxOpts& op) : o(op) {}
    void U8(unsigned v) { s.push_back((char)(v & 255)); }
    void U16(unsigned v) { U8(v); U8(v >> 8); }
    void I32(std::int32_t v) { const std::uint32_t u = (std::uint32_t)v; U8(u); U8(u >> 8); U8(u >> 16); U8(u >> 24); }
    void F(float f) { std::uint32_t u; std::memcpy(&u, &f, 4); I32((std::int32_t)u); }
    void V3(float a, float b, float c) { F(a); F(b); F(c); }
    void V4(float a, float b, float c, float d) { F(a); F(b); F(c); F(d); }
    void Text(const std::string& t) {
        if (o.utf8) { I32((std::int32_t)t.size()); s += t; }
        else { I32((std::int32_t)t.size() * 2); for (unsigned char c : t) { U8(c); U8(0); } }
    }
    void Raw16(const std::vector<unsigned>& units) { I32((std::int32_t)units.size() * 2); for (unsigned u : units) U16(u); }
    // an index of `sz` bytes; -1 is written as all ones of that size
    void Idx(int sz, long long v) { if (sz == 1) U8((unsigned)v); else if (sz == 2) U16((unsigned)v); else I32((std::int32_t)v); }
    void Header() {
        s += "PMX ";
        F(o.version);
        U8(8);
        U8(o.utf8 ? 1 : 0); U8((unsigned)o.addUv); U8((unsigned)o.vsz); U8((unsigned)o.tsz); U8((unsigned)o.msz); U8((unsigned)o.bsz); U8((unsigned)o.mosz); U8((unsigned)o.rsz);
        Text("Mdl"); Text("M"); Text("comment"); Text("c");
    }
    void Vertex(float x, float y, float z, float nx, float ny, float nz, float u, float v, int wtype, long long b0, long long b1, long long b2, long long b3, float w0, float w1, float w2, float w3) {
        V3(x, y, z); V3(nx, ny, nz); F(u); F(v);
        for (int i = 0; i < o.addUv; ++i) V4(9, 9, 9, 9);
        U8((unsigned)wtype);
        if (wtype == 0) Idx(o.bsz, b0);
        else if (wtype == 1) { Idx(o.bsz, b0); Idx(o.bsz, b1); F(w0); }
        else if (wtype == 2 || wtype == 4) { Idx(o.bsz, b0); Idx(o.bsz, b1); Idx(o.bsz, b2); Idx(o.bsz, b3); F(w0); F(w1); F(w2); F(w3); }
        else { Idx(o.bsz, b0); Idx(o.bsz, b1); F(w0); V3(1, 2, 3); V3(4, 5, 6); V3(7, 8, 9); }   // SDEF: C, R0, R1
        F(1.0f);   // edge scale
    }
};

// Offsets inside the fixture that tests need: the end of the morph list (everything before it is required, what follows is not read).
struct PmxMeta { std::size_t mainEnd = 0, vertCount = 0, faceCount = 0, texCount = 0, matCount = 0, boneCount = 0, morphCount = 0, firstMorph = 0, firstVertex = 0; };

// A model with 6 vertices, 2 materials, 3 bones, 5 (or 6) morphs and the empty display frame / rigid body / joint lists.
//   vertices (x y z, normal, uv, weights):
//     v0 (0,0,0)  n(0,0,-1) uv(0,0)  BDEF1 bone 0
//     v1 (1,0,0)  n(0,0,-1) uv(1,0)  BDEF1 bone 0
//     v2 (1,1,0)  n(0,0,-1) uv(1,1)  BDEF2 bones 0,1 weight 0.75 (bone 1 gets 0.25)
//     v3 (0,1,0)  n(0,0,-1) uv(0,1)  BDEF4 bones 0,1,-1,-1 weights 0.5,0.5,0,0
//     v4 (0,0,2)  n(0,1,0)  uv(0,0)  SDEF bones 1,0 weight 0.25 (as two-bone blend: bone 1 0.25, bone 0 0.75)
//     v5 (1,0,2)  n(0,1,0)  uv(1,0)  BDEF1 with bone -1 (no bone), or QDEF bones 2,0,-1,-1 weights 3,1,0,0 when opts.qdef
//   textures: "tex\\a.png"
//   materials: "Mat0" diffuse (1, 0.5, 0.25, 0.75) specular power 20 texture 0, 6 face indices; "Mat1" diffuse (0.5,0.5,0.5,1) no texture, 3 face indices
//   faces: Mat0: (0,1,2) (0,2,3); Mat1: (1,4,5)
//   bones: "root" at (1,2,3) no parent, tail bone 1; "child" at (4,6,10) parent 0, tail offset, IK (target 0, loop 5, limit 0.5, 1 link: bone 0 with limits);
//          "extra" at (0,0,0) parent 1, inherit rotation from bone 0 (weight 1), fixed axis, local axes, external parent key
//   morphs: vertex (v1, v2), group (morph 0), bone (bone 0), UV (v0), material (all materials), [flip (morph 0)]
inline std::string BuildPmx(const PmxOpts& o = PmxOpts(), PmxMeta* meta = nullptr) {
    PmxW w(o);
    w.Header();
    if (meta) { meta->vertCount = w.s.size(); }
    w.I32(6);
    if (meta) meta->firstVertex = w.s.size();
    w.Vertex(0, 0, 0, 0, 0, -1, 0, 0, 0, 0, -1, -1, -1, 1, 0, 0, 0);
    w.Vertex(1, 0, 0, 0, 0, -1, 1, 0, 0, 0, -1, -1, -1, 1, 0, 0, 0);
    w.Vertex(1, 1, 0, 0, 0, -1, 1, 1, 1, 0, 1, -1, -1, 0.75f, 0, 0, 0);
    w.Vertex(0, 1, 0, 0, 0, -1, 0, 1, 2, 0, 1, -1, -1, 0.5f, 0.5f, 0, 0);
    w.Vertex(0, 0, 2, 0, 1, 0, 0, 0, 3, 1, 0, -1, -1, 0.25f, 0, 0, 0);
    if (o.qdef) w.Vertex(1, 0, 2, 0, 1, 0, 1, 0, 4, 2, 0, -1, -1, 3, 1, 0, 0);
    else w.Vertex(1, 0, 2, 0, 1, 0, 1, 0, 0, -1, -1, -1, -1, 1, 0, 0, 0);
    if (meta) meta->faceCount = w.s.size();
    w.I32(9);
    const int faces[9] = {0, 1, 2, 0, 2, 3, 1, 4, 5};
    for (int f : faces) w.Idx(o.vsz, f);   // 1 and 2 byte vertex indices are unsigned; these are small
    if (meta) meta->texCount = w.s.size();
    w.I32(1);
    w.Text(o.texName);
    if (meta) meta->matCount = w.s.size();
    w.I32(2);
    for (int m = 0; m < 2; ++m) {
        w.Text(m == 0 ? "Mat0" : "Mat1"); w.Text(m == 0 ? "Mat0en" : "Mat1en");
        if (m == 0) w.V4(1, 0.5f, 0.25f, 0.75f); else w.V4(0.5f, 0.5f, 0.5f, 1);
        w.V3(0.1f, 0.2f, 0.3f);
        w.F(m == 0 ? 20.0f : 0.0f);
        w.V3(0.4f, 0.5f, 0.6f);
        w.U8(m == 0 ? 0x11 : 0x00);   // double sided + edge
        w.V4(0, 0, 0, 1); w.F(1.0f);
        w.Idx(o.tsz, m == 0 ? 0 : -1);   // texture
        w.Idx(o.tsz, -1); w.U8(0);       // sphere texture and mode
        w.U8(1); w.U8(3);                // shared toon 3
        w.Text(m == 0 ? "memo" : "");
        w.I32(m == 0 ? 6 : 3);
    }
    if (meta) meta->boneCount = w.s.size();
    w.I32(3);
    for (int b = 0; b < 3; ++b) {
        w.Text(o.boneName[b]); w.Text("e");
        if (b == 0) w.V3(1, 2, 3); else if (b == 1) w.V3(4, 6, 10); else w.V3(0, 0, 0);
        w.Idx(o.bsz, o.par[b]);
        w.I32(0);
        unsigned fl = 0x001E;   // rotatable, translatable, visible, enabled
        if (b == 0) fl |= 0x0001;
        if (b == 1) fl |= 0x0020;
        if (b == 2) fl |= 0x0100 | 0x0400 | 0x0800 | 0x2000;
        w.U16(fl);
        if (b == 0) w.Idx(o.bsz, 1); else w.V3(0, 1, 0);
        if (b == 2) { w.Idx(o.bsz, 0); w.F(1.0f); w.V3(0, 1, 0); w.V3(1, 0, 0); w.V3(0, 0, 1); w.I32(7); }
        if (b == 1) { w.Idx(o.bsz, 0); w.I32(5); w.F(0.5f); w.I32(1); w.Idx(o.bsz, 0); w.U8(1); w.V3(-1, -1, -1); w.V3(1, 1, 1); }
    }
    if (meta) meta->morphCount = w.s.size();
    w.I32(o.flipMorph ? 6 : 5);
    if (meta) meta->firstMorph = w.s.size();
    auto morphHead = [&](const char* n, int type, int count) { w.Text(n); w.Text(n); w.U8(1); w.U8((unsigned)type); w.I32(count); };
    morphHead("v", 1, 2); w.Idx(o.vsz, 1); w.V3(0, 0, 1); w.Idx(o.vsz, 2); w.V3(0, 0, 2);
    morphHead("g", 0, 1); w.Idx(o.mosz, 0); w.F(0.5f);
    morphHead("b", 2, 1); w.Idx(o.bsz, 0); w.V3(1, 0, 0); w.V4(0, 0, 0, 1);
    morphHead("u", 3, 1); w.Idx(o.vsz, 0); w.V4(0.1f, 0.2f, 0, 0);
    morphHead("m", 8, 1); w.Idx(o.msz, -1); w.U8(0); w.V4(1, 1, 1, 1); w.V3(0, 0, 0); w.F(0); w.V3(0, 0, 0); w.V4(0, 0, 0, 1); w.F(1); w.V4(0, 0, 0, 0); w.V4(0, 0, 0, 0); w.V4(0, 0, 0, 0);
    if (o.flipMorph) { morphHead("f", 9, 1); w.Idx(o.mosz, 0); w.F(1.0f); }
    if (meta) meta->mainEnd = w.s.size();
    w.I32(0); w.I32(0); w.I32(0);   // display frames, rigid bodies, joints
    return w.s;
}

// A PMX with n vertices (a fan of triangles over a grid), one material, no bones. For the memory and budget checks.
inline std::string BuildPmxGrid(int nVerts, int nTris, int nMats = 1, int nBones = 0, int boneNameLen = 1) {
    PmxOpts o; o.vsz = 4; o.bsz = 2; o.tsz = 1;
    PmxW w(o);
    w.Header();
    w.I32(nVerts);
    for (int i = 0; i < nVerts; ++i) w.Vertex((float)(i % 1000), (float)(i / 1000), 0, 0, 0, 1, 0, 0, nBones ? 0 : 0, nBones ? 0 : -1, -1, -1, -1, 1, 0, 0, 0);
    w.I32(nTris * 3);
    for (int t = 0; t < nTris; ++t) { w.Idx(4, t % (nVerts - 2)); w.Idx(4, t % (nVerts - 2) + 1); w.Idx(4, t % (nVerts - 2) + 2); }
    w.I32(0);
    w.I32(nMats);
    for (int m = 0; m < nMats; ++m) {
        w.Text("m"); w.Text("");
        w.V4(1, 1, 1, 1); w.V3(0, 0, 0); w.F(0); w.V3(0, 0, 0); w.U8(0); w.V4(0, 0, 0, 1); w.F(1);
        w.Idx(1, -1); w.Idx(1, -1); w.U8(0); w.U8(1); w.U8(0); w.Text("");
        w.I32(nMats == 1 ? nTris * 3 : (m < nTris ? 3 : 0));
    }
    w.I32(nBones);
    for (int b = 0; b < nBones; ++b) { w.Text(std::string((std::size_t)boneNameLen, 'n')); w.Text(""); w.V3((float)b, 0, 0); w.Idx(2, -1); w.I32(0); w.U16(0x0001); w.Idx(2, -1); }
    w.I32(0);
    return w.s;
}

// A PMX whose first material covers `bigTris` triangles over `nVerts` vertices and which then has `smallMats` materials of one triangle each
// (no bones). The reader welds per material: a map that is cleared again and again after one big part would cost its bucket count each time.
inline std::string BuildPmxManyMaterials(int nVerts, int bigTris, int smallMats) {
    PmxOpts o; o.vsz = 4; o.bsz = 2; o.tsz = 1;
    PmxW w(o);
    w.Header();
    w.I32(nVerts);
    for (int i = 0; i < nVerts; ++i) w.Vertex((float)(i % 1000), (float)(i / 1000), 0, 0, 0, 1, 0, 0, 0, -1, -1, -1, -1, 1, 0, 0, 0);
    const int tris = bigTris + smallMats;
    w.I32(tris * 3);
    for (int t = 0; t < tris; ++t) { const int a = (int)(((long long)t * 7) % (nVerts - 2)); w.Idx(4, a); w.Idx(4, a + 1); w.Idx(4, a + 2); }
    w.I32(0);
    w.I32(1 + smallMats);
    for (int m = 0; m < 1 + smallMats; ++m) {
        w.Text("m"); w.Text("");
        w.V4(1, 1, 1, 1); w.V3(0, 0, 0); w.F(0); w.V3(0, 0, 0); w.U8(0); w.V4(0, 0, 0, 1); w.F(1);
        w.Idx(1, -1); w.Idx(1, -1); w.U8(0); w.U8(1); w.U8(0); w.Text("");
        w.I32(m == 0 ? bigTris * 3 : 3);
    }
    w.I32(0);
    w.I32(0);
    return w.s;
}

// ---- LWS / MOT ----------------------------------------------------------------------------------------------------
inline std::string Env(const std::vector<std::pair<double, double>>& keys) {   // (time, value) pairs as one envelope block
    std::string s = "{ Envelope\n  " + std::to_string(keys.size()) + "\n";
    for (const auto& k : keys) s += "  Key " + std::to_string(k.second) + " " + std::to_string(k.first) + " 3 0 0 0 0 0 0\n";
    s += "  Behaviors 1 1\n}\n";
    return s;
}
inline std::string Chan(int c, const std::vector<std::pair<double, double>>& keys) { return "Channel " + std::to_string(c) + "\n" + Env(keys); }

// A version 3 scene with four items, in file order (Tri comes first and names the null that comes second as its parent):
//   item 0 object   LoadObjectLayer 1 10000000 Objects/Tri.lwo  (id 0x10000000, explicit), ParentItem 10000001
//                   x: t0 -> 0, t1 -> 2   y: t0 -> 5   z: t0 -> 1   heading: t0 -> 0, t1 -> pi/2   scale x: t0 -> 3
//   item 1 null    AddNullObject Pivot  (implicit id 0x10000001: the second object item), x: t0 -> 10
//   item 2 camera  AddCamera 30000000   y: t0 -> 1
//   item 3 light   AddLight 20000000, LightName Sun   (an envelope outside a motion channel: LightIntensity)
// The scene runs at 24 frames per second.
inline std::string BuildLws() {
    const double pi2 = 1.57079632679489662;
    std::string s = "LWSC\n3\n\nFirstFrame 0\nFramesPerSecond 24\n";
    s += "LoadObjectLayer 1 10000000 Objects/Tri.lwo\nShowObject 7 -1\nObjectMotion\nNumChannels 9\n";
    s += Chan(0, {{0, 0}, {1, 2}}) + Chan(1, {{0, 5}}) + Chan(2, {{0, 1}}) + Chan(3, {{0, 0}, {1, pi2}}) + Chan(6, {{0, 3}});
    s += "ParentItem 10000001\n";
    s += "AddNullObject Pivot\nObjectMotion\nNumChannels 9\n" + Chan(0, {{0, 10}});
    s += "AddCamera 30000000\nCameraMotion\nNumChannels 9\n" + Chan(1, {{0, 1}});
    s += "AddLight 20000000\nLightName Sun\nLightColor 1 1 1\nLightIntensity\n" + Env({{0, 1}}) + "LightMotion\nNumChannels 9\n";
    return s;
}
inline std::string BuildMot() {
    return "LWMO\n3\nNumChannels 9\n" + Chan(0, {{0, 1}, {0.5, 2}, {1, 4}}) + Chan(1, {{0, 0}}) + "Channel 2\n" + Env({{0, 0}});
}

// A loader that returns a one-triangle model for any path and counts how it was asked.
struct FakeLoad {
    std::vector<std::string> paths;
    int fails = 0;
    okay::IrrMeshLoader Fn(std::size_t tris = 1, std::size_t bones = 0) {
        return [this, tris, bones](const std::string& p, okay::ImportedModel& m, std::string* e) {
            paths.push_back(p);
            if (p.find("missing") != std::string::npos) { ++fails; if (e) *e = "no such file"; return false; }
            okay::ImportedNode n; n.name = "lwo";
            m.nodes.push_back(n);
            okay::ImportedPart part;
            for (std::size_t i = 0; i < tris; ++i) {
                const float x = (float)(i % 1000), y = (float)(i / 1000);
                part.mesh.vertices.push_back(okay::Vec3{x, y, 0}); part.mesh.vertices.push_back(okay::Vec3{x + 1, y, 0}); part.mesh.vertices.push_back(okay::Vec3{x, y + 1, 0});
                part.mesh.triangles.push_back((int)(3 * i)); part.mesh.triangles.push_back((int)(3 * i + 1)); part.mesh.triangles.push_back((int)(3 * i + 2));
            }
            part.node = 0;
            part.bones.resize(bones);
            for (auto& b : part.bones) b.name = "bone";
            m.parts.push_back(std::move(part));
            return true;
        };
    }
};

} // namespace fx
