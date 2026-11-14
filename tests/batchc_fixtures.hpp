#pragma once
// Synthetic fixtures for the batch-C own readers (MD2, MD3, IRRMESH, B3D, DirectX .x), built in memory so nothing binary
// is committed. Shared by the per-reader tests and by the hostile-input / mutation-fuzz test. Every number is chosen so it
// can be checked by hand; see the comment above each builder.
#include "model_fixture_util.hpp"
#include <string>

namespace fx {

struct Md2Opts { int skinW = 64, skinH = 32; int numFrames = 1; bool skin = true; int badVertIdx = 0; int badStIdx = 0; };

// A tiny model: 4 vertices, scale (1,2,3), offset (10,20,30); triangles (0,1,2) and (0,2,3).
inline std::string BuildMd2(const Md2Opts& o = {}) {
    const int nXyz = 4, nSt = 4, nTris = 2;
    const int frameSize = 40 + 4 * nXyz;
    const int ofsSkins = 68, ofsSt = ofsSkins + (o.skin ? 64 : 0), ofsTris = ofsSt + 4 * nSt, ofsFrames = ofsTris + 12 * nTris;
    const int ofsEnd = ofsFrames + frameSize * o.numFrames;
    std::string s = "IDP2";
    for (int v : {8, o.skinW, o.skinH, frameSize, o.skin ? 1 : 0, nXyz, nSt, nTris, 0, o.numFrames, ofsSkins, ofsSt, ofsTris, ofsFrames, 0, ofsEnd}) fx::P32(s, (std::uint32_t)v);
    if (o.skin) fx::PStr(s, "players\\male\\skin.pcx", 64);
    const int st[4][2] = {{0, 0}, {64, 0}, {0, 32}, {32, 16}};
    for (auto& t : st) { fx::P16(s, (unsigned)t[0]); fx::P16(s, (unsigned)t[1]); }
    const int tris[2][6] = {{0, 1, 2, 0, 1, 2}, {0, 2, 3, 0, 2, 3}};
    for (auto& t : tris) for (int k = 0; k < 6; ++k) fx::P16(s, (unsigned)t[k]);
    if (o.badVertIdx) { s[(std::size_t)ofsTris + 12] = (char)o.badVertIdx; }   // second triangle's first vertex index
    if (o.badStIdx) { s[(std::size_t)ofsTris + 12 + 6] = (char)o.badStIdx; }
    for (int f = 0; f < o.numFrames; ++f) {
        for (float v : {1.0f, 2.0f, 3.0f, 10.0f, 20.0f, 30.0f}) fx::PF(s, v);
        fx::PStr(s, "frame" + std::to_string(f), 16);
        const int vtx[4][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        for (auto& v : vtx) { for (int k = 0; k < 3; ++k) s.push_back((char)(v[k] + f)); s.push_back(0); }
    }
    return s;
}



// ---- MD3: one textured triangle (and optionally an empty second surface and a tag) ----
// Vertices in 1/64 units: (0,0,0), (64,0,0) = (1,0,0), (0,128,0) = (0,2,0); stored normals (0,0,1), (1,0,0), (0,1,0);
// uv (0,0), (1,0), (0,1). A second frame moves every vertex by 64 (it must be ignored). The tag "tag_weapon" sits at
// (5,6,7) and its axes turn 90 degrees about Z (columns (0,1,0), (-1,0,0), (0,0,1)).
struct Md3Opts { bool tag = true; bool emptySurface = true; int numFrames = 2; bool shader = true; };
inline std::string BuildMd3(const Md3Opts& o = {}) {
    const int nTags = o.tag ? 1 : 0;
    // surface: 108 header, 1 triangle (12), shader (68), st (3*8), xyz (3*8*frames)
    auto surface = [&](const std::string& name, int nVerts, int nTris, bool withShader) {
        const int nShaders = withShader ? 1 : 0;
        const int oTris = 108, oShaders = oTris + 12 * nTris, oSt = oShaders + 68 * nShaders, oXyz = oSt + 8 * nVerts, oEnd = oXyz + 8 * nVerts * o.numFrames;
        std::string s = "IDP3"; PStr(s, name, 64);
        for (int v : {0, o.numFrames, nShaders, nVerts, nTris, oTris, oShaders, oSt, oXyz, oEnd}) P32(s, (std::uint32_t)v);
        if (nTris) { P32(s, 0); P32(s, 1); P32(s, 2); }
        if (withShader) { PStr(s, "models\\x\\tex.tga", 64); P32(s, 0); }
        const float uv[3][2] = {{0, 0}, {1, 0}, {0, 1}};
        for (int i = 0; i < nVerts; ++i) { PF(s, uv[i % 3][0]); PF(s, uv[i % 3][1]); }
        const int xyz[3][3] = {{0, 0, 0}, {64, 0, 0}, {0, 128, 0}};
        const unsigned nrm[3] = {0x0000u, 0x0040u, 0x4040u};
        for (int f = 0; f < o.numFrames; ++f)
            for (int i = 0; i < nVerts; ++i) { for (int k = 0; k < 3; ++k) P16(s, (unsigned)(xyz[i % 3][k] + 64 * f)); P16(s, nrm[i % 3]); }
        return s;
    };
    const std::string s0 = surface("body", 3, 1, o.shader), s1 = o.emptySurface ? surface("empty", 0, 0, false) : std::string();
    const int nSurf = o.emptySurface ? 2 : 1;
    const int ofsFrames = 108, ofsTags = ofsFrames + 56 * o.numFrames, ofsSurf = ofsTags + 112 * nTags * o.numFrames;
    std::string s = "IDP3"; P32(s, 15); PStr(s, "models/test/m.md3", 64);
    for (int v : {0, o.numFrames, nTags, nSurf, 0, ofsFrames, ofsTags, ofsSurf, ofsSurf + (int)(s0.size() + s1.size())}) P32(s, (std::uint32_t)v);
    s.append((std::size_t)(56 * o.numFrames), '\0');
    for (int f = 0; f < o.numFrames && nTags; ++f) {
        PStr(s, "tag_weapon", 64);
        for (float v : {5.0f, 6.0f, 7.0f, 0.0f, 1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}) PF(s, v);
    }
    return s + s0 + s1;
}

} // namespace fx
