#pragma once
// Synthetic fixtures for the batch-E3 own readers (IQM, COB, NDO), built in memory so nothing binary is committed. Every number is
// chosen so it can be checked by hand; the comment above each builder says what the numbers are. The files are written from the
// structure of each format (IQM specification text; COB structure learnt from hex dumps and text views of real files), NOT copied
// from any real file.
#include "model_fixture_util.hpp"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace fx {

inline void Put32(std::string& s, std::size_t off, std::uint32_t v) { for (int i = 0; i < 4; ++i) s[off + (std::size_t)i] = (char)((v >> (8 * i)) & 255); }
inline std::uint32_t Get32(const std::string& s, std::size_t off) { std::uint32_t v = 0; for (int i = 3; i >= 0; --i) v = (v << 8) | (unsigned char)s[off + (std::size_t)i]; return v; }
inline void PutF(std::string& s, float f) { std::uint32_t u; std::memcpy(&u, &f, 4); P32(s, u); }
inline void PutU8(std::string& s, unsigned v) { s.push_back((char)v); }
// header field number i (0 = version) of an IQM file
inline std::uint32_t IqmH(const std::string& s, int i) { return Get32(s, 16 + 4 * (std::size_t)i); }
inline void IqmSetH(std::string& s, int i, std::uint32_t v) { Put32(s, 16 + 4 * (std::size_t)i, v); }
enum IqmHdr { kVersion = 0, kFilesize, kFlags, kNumText, kOfsText, kNumMeshes, kOfsMeshes, kNumVA, kNumVerts, kOfsVA, kNumTris, kOfsTris, kOfsAdj, kNumJoints, kOfsJoints,
              kNumPoses, kOfsPoses, kNumAnims, kOfsAnims, kNumFrames, kNumFrameCh, kOfsFrames, kOfsBounds };

// ---- IQM ----------------------------------------------------------------------------------------------------------------------------
// text: "", "quad", "red.png", "root", "arm", "wave". One mesh "quad" (material "red.png") of 4 vertices (0,0,0) (2,0,0) (2,3,0) (0,3,0), normals
// (0,0,1), uv (0,0) (1,0) (1,1) (0,1), 2 triangles (0,1,2) (0,2,3). Blend: v0 v1 fully on joint 0; v2 v3: joint 0 weight 128, joint 1 weight 127 (of 255).
// Joints: "root" at (1,0,0), "arm" its child at (0,2,0) rotated 90 degrees about Z. Poses: root constant; arm animates translate x (mask bit 0:
// offset 0 scale 0.5) and scale x (mask bit 7: offset 1 scale 0.25). One animation "wave" of 3 frames at 10 fps: raw (0,0) (2,4) (4,8) = tx 0 1 2,
// sx 1 2 3. Totals: 1 part, 4 vertices, 2 triangles, 3 nodes (IQM, root, arm), 2 bones, 1 clip with the channel "arm" (3 position keys, 3 scale keys).
inline std::string BuildIqm() {
    std::string text(1, '\0');
    auto add = [&](const char* s) { const std::uint32_t o = (std::uint32_t)text.size(); text += s; text.push_back('\0'); return o; };
    const std::uint32_t oQuad = add("quad"), oRed = add("red.png"), oRoot = add("root"), oArm = add("arm"), oWave = add("wave");
    std::string meshes, va, tris, joints, poses, anims, frames, data;
    const std::uint32_t nv = 4;
    // vertex data blocks first (offsets patched below once the table sizes are known)
    std::string pos, nrm, uv, bi, bw;
    const float P[4][3] = {{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}, U[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    for (int i = 0; i < 4; ++i) { for (int k = 0; k < 3; ++k) PutF(pos, P[i][k]); PutF(nrm, 0); PutF(nrm, 0); PutF(nrm, 1); PutF(uv, U[i][0]); PutF(uv, U[i][1]); }
    const unsigned I[4][4] = {{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 1, 0, 0}, {0, 1, 0, 0}}, W[4][4] = {{255, 0, 0, 0}, {255, 0, 0, 0}, {128, 127, 0, 0}, {128, 127, 0, 0}};
    for (int i = 0; i < 4; ++i) for (int k = 0; k < 4; ++k) { PutU8(bi, I[i][k]); PutU8(bw, W[i][k]); }
    const std::uint32_t idx[6] = {0, 1, 2, 0, 2, 3};
    for (std::uint32_t v : idx) P32(tris, v);
    auto joint = [&](std::uint32_t name, int parent, float tx, float ty, float tz, float qx, float qy, float qz, float qw) {
        P32(joints, name); P32(joints, (std::uint32_t)parent);
        PutF(joints, tx); PutF(joints, ty); PutF(joints, tz); PutF(joints, qx); PutF(joints, qy); PutF(joints, qz); PutF(joints, qw);
        PutF(joints, 1); PutF(joints, 1); PutF(joints, 1);
    };
    const float h = 0.70710678f;
    joint(oRoot, -1, 1, 0, 0, 0, 0, 0, 1);
    joint(oArm, 0, 0, 2, 0, 0, 0, h, h);
    auto pose = [&](int parent, std::uint32_t mask, const float off[10], const float sc[10]) {
        P32(poses, (std::uint32_t)parent); P32(poses, mask);
        for (int k = 0; k < 10; ++k) PutF(poses, off[k]);
        for (int k = 0; k < 10; ++k) PutF(poses, sc[k]);
    };
    const float off0[10] = {1, 0, 0, 0, 0, 0, 1, 1, 1, 1}, sc0[10] = {0};
    const float off1[10] = {0, 2, 0, 0, 0, h, h, 1, 1, 1}, sc1[10] = {0.5f, 0, 0, 0, 0, 0, 0, 0.25f, 0, 0};
    pose(-1, 0, off0, sc0);
    pose(0, 1u | (1u << 7), off1, sc1);
    const unsigned fr[3][2] = {{0, 0}, {2, 4}, {4, 8}};
    for (auto& r : fr) for (unsigned v : r) P16(frames, v);
    P32(anims, oWave); P32(anims, 0); P32(anims, 3); PutF(anims, 10.0f); P32(anims, 1);
    // layout
    std::string s;
    s.append("INTERQUAKEMODEL", 15); s.push_back('\0');
    s.resize(124, '\0');
    const std::uint32_t ofsText = (std::uint32_t)s.size(); s += text;
    while (s.size() % 4) s.push_back('\0');
    const std::uint32_t ofsMeshes = (std::uint32_t)s.size();
    P32(meshes, oQuad); P32(meshes, oRed); P32(meshes, 0); P32(meshes, nv); P32(meshes, 0); P32(meshes, 2);
    s += meshes;
    const std::uint32_t ofsVA = (std::uint32_t)s.size();
    s.append(5 * 20, '\0');
    const std::uint32_t ofsTris = (std::uint32_t)s.size(); s += tris;
    const std::uint32_t ofsJoints = (std::uint32_t)s.size(); s += joints;
    const std::uint32_t ofsPoses = (std::uint32_t)s.size(); s += poses;
    const std::uint32_t ofsAnims = (std::uint32_t)s.size(); s += anims;
    const std::uint32_t ofsFrames = (std::uint32_t)s.size(); s += frames;
    while (s.size() % 4) s.push_back('\0');
    std::uint32_t o[5];
    const std::string* blocks[5] = {&pos, &uv, &nrm, &bi, &bw};
    for (int i = 0; i < 5; ++i) { o[i] = (std::uint32_t)s.size(); s += *blocks[i]; }
    // vertex array records: type, flags, format, size, offset
    const std::uint32_t types[5] = {0, 1, 2, 4, 5}, fmts[5] = {7, 7, 7, 1, 1}, sizes[5] = {3, 2, 3, 4, 4};
    for (int i = 0; i < 5; ++i) { Put32(s, ofsVA + 20 * (std::size_t)i, types[i]); Put32(s, ofsVA + 20 * (std::size_t)i + 4, 0); Put32(s, ofsVA + 20 * (std::size_t)i + 8, fmts[i]); Put32(s, ofsVA + 20 * (std::size_t)i + 12, sizes[i]); Put32(s, ofsVA + 20 * (std::size_t)i + 16, o[i]); }
    const std::uint32_t vals[27] = {2, (std::uint32_t)s.size(), 0, (std::uint32_t)text.size(), ofsText, 1, ofsMeshes, 5, nv, ofsVA, 2, ofsTris, 0, 2, ofsJoints, 2, ofsPoses, 1, ofsAnims, 3, 2, ofsFrames, 0, 0, 0, 0, 0};
    for (int i = 0; i < 27; ++i) IqmSetH(s, i, vals[i]);
    return s;
}

// ---- COB ----------------------------------------------------------------------------------------------------------------------------
// Group "G" (id 10, parent 0), world matrix: scale 2, translation (10,0,0). Polygon object "P" (id 11, parent 10), world matrix: scale 2, translation (10,0,5).
// 5 vertices (0,0,0) (1,0,0) (1,1,0) (0,1,0) (2,0,0); 4 texture vertices (0,0) (1,0) (1,1) (0,1); face 0: quad <0,0> <1,1> <2,2> <3,3> material 0;
// face 1: triangle <1,1> <4,0> <2,2> material 1. Materials of P: mat# 0 rgb (1,0,0) alpha 1, faceted, texture "tex.png" (a "texture map" shader);
// mat# 1 rgb (0,0,1) alpha 0.5, auto 40. After y/z swap: node G at (10,0,0) scale 2; node P local position (0,2.5,0) scale 1; 2 parts (quad = 2 triangles of area 1;
// triangle of area 0.5). Binary and ASCII builders hold the same scene.
inline std::string CobAscii() {
    std::string s = "Caligari V00.01ALH             \n";
    s += "Grou V0.01 Id 10 Parent 0 Size 00000100\nName G\ncenter 10 0 0\nx axis 1 0 0\ny axis 0 1 0\nz axis 0 0 1\nTransform\n2 0 0 10\n0 2 0 0\n0 0 2 0\n0 0 0 1\n";
    s += "Unit V0.01 Id 12 Parent 10 Size 00000009\nUnits 2\n";
    s += "PolH V0.08 Id 11 Parent 10 Size 00000999\nName P\ncenter 10 0 5\nx axis 1 0 0\ny axis 0 1 0\nz axis 0 0 1\nTransform\n2 0 0 10\n0 2 0 0\n0 0 2 5\n0 0 0 1\n";
    s += "World Vertices 5\n0 0 0\n1 0 0\n1 1 0\n0 1 0\n2 0 0\n";
    s += "Texture Vertices 4\n0 0\n1 0\n1 1\n0 1\n";
    s += "Faces 2\nFace verts 4 flags 0 mat 0\n<0,0> <1,1> <2,2> <3,3> \nFace verts 3 flags 0 mat 1\n<1,1> <4,0> <2,2> \nDrawFlags 0\n";
    s += "Mat1 V0.08 Id 20 Parent 11 Size 00000100\nmat# 0\nshader: phong  facet: faceted\nrgb 1,0,0\nalpha 1  ka 0  ks 0.1  exp 0  ior 1\n";
    s += "ShBx V0.04 Id 21 Parent 20 Size 00000100\nShader class: color\nShader name: \"texture map\" (caligari texture)\nNumber of parameters: 2\nfile name: string \"tex.png\"\nS repeat: float 1\nFlags: 3\n";
    s += "Mat1 V0.08 Id 22 Parent 11 Size 00000100\nmat# 1\nshader: phong  facet: auto40\nrgb 0,0,1\nalpha 0.5  ka 0  ks 0.1  exp 0  ior 1\n";
    s += "END V1.00 Id 0 Parent 0 Size 0\n";
    return s;
}
inline void CobChunk(std::string& s, const char* type, int id, int parent, const std::string& data) {
    s.append(type, 4); P16(s, 0); P16(s, 1); P32(s, (std::uint32_t)id); P32(s, (std::uint32_t)parent); P32(s, (std::uint32_t)data.size()); s += data;
}
inline std::string CobObjHead(const char* name, const float m[12]) {
    std::string d;
    P16(d, 0); P16(d, (unsigned)std::strlen(name)); d += name;
    const float cax[12] = {m[3], m[7], m[11], 1, 0, 0, 0, 1, 0, 0, 0, 1};
    for (float f : cax) PutF(d, f);
    for (int i = 0; i < 12; ++i) PutF(d, m[i]);
    return d;
}
inline std::string CobBinary() {
    std::string s = "Caligari V00.01BLH             \n";
    const float gm[12] = {2, 0, 0, 10, 0, 2, 0, 0, 0, 0, 2, 0}, pm[12] = {2, 0, 0, 10, 0, 2, 0, 0, 0, 0, 2, 5};
    CobChunk(s, "Grou", 10, 0, CobObjHead("G", gm));
    { std::string u; P16(u, 2); CobChunk(s, "Unit", 12, 10, u); }
    std::string p = CobObjHead("P", pm);
    P32(p, 5);
    const float V[5][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {2, 0, 0}};
    for (auto& v : V) for (float f : v) PutF(p, f);
    P32(p, 4);
    const float U[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    for (auto& u : U) for (float f : u) PutF(p, f);
    P32(p, 2);
    auto face = [&](int n, int mat, const int (*c)[2]) { PutU8(p, 0); P16(p, (unsigned)n); P16(p, (unsigned)mat); for (int i = 0; i < n; ++i) { P32(p, (std::uint32_t)c[i][0]); P32(p, (std::uint32_t)c[i][1]); } };
    const int f0[4][2] = {{0, 0}, {1, 1}, {2, 2}, {3, 3}}, f1[3][2] = {{1, 1}, {4, 0}, {2, 2}};
    face(4, 0, f0); face(3, 1, f1);
    P32(p, 0);   // DrawFlags
    CobChunk(s, "PolH", 11, 10, p);
    auto mat = [&](int idx, float r, float g, float b, float a) {
        std::string m; P16(m, (unsigned)idx); PutU8(m, 'p'); PutU8(m, idx == 0 ? 'f' : 'a'); PutU8(m, 40);
        PutF(m, r); PutF(m, g); PutF(m, b); PutF(m, a); PutF(m, 0); PutF(m, 0.1f); PutF(m, 0); PutF(m, 1);
        return m;
    };
    CobChunk(s, "Mat1", 20, 11, mat(0, 1, 0, 0, 1));
    {
        std::string sh;
        PutU8(sh, 0); P16(sh, 16); sh += "caligari texture"; P32(sh, 2);
        P16(sh, 9); sh += "file name"; P32(sh, 10); P16(sh, 7); sh += "tex.png";
        P16(sh, 8); sh += "S repeat"; P32(sh, 3); PutF(sh, 1);
        P16(sh, 3);
        CobChunk(s, "ShBx", 21, 20, sh);
    }
    CobChunk(s, "Mat1", 22, 11, mat(1, 0, 0, 1, 0.5f));
    CobChunk(s, "END ", 0, 0, std::string());
    return s;
}

} // namespace fx
