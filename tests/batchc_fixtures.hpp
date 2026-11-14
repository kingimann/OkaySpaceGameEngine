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


// ---- IRRMESH: three buffers ----
// 1: a standard-type triangle (0,0,0), (2,0,0), (0,3,0): area 3, normal map material (diffuse ff8040c0, emissive 00102030,
//    shininess 0.75, texture "assets\\a.png", normal map "n.png"). 2: a 2tcoords unit quad as 2 triangles sharing 2 corners:
//    4 vertices, area 1, one vertex colour that is not white. 3: a tangents-type triangle (0,0,0), (1,0,0), (0,1,0), area 0.5,
//    with indexCount 4 (one stray index). Total: 3 parts, 4 triangles, area 4.5.
inline std::string BuildIrrMesh(const char* eol = "\n") {
    std::string e = eol;
    std::string s = std::string("<?xml version=\"1.0\"?>") + e + "<mesh xmlns=\"http://irrlicht.sourceforge.net/IRRMESH_09_2007\" version=\"1.0\">" + e +
        "<!-- synthetic -->" + e + "<buffer>" + e +
        "<material><enum name=\"Type\" value=\"normalmap_solid\" /><color name=\"Diffuse\" value=\"ff8040c0\" /><color name=\"Emissive\" value=\"00102030\" />"
        "<float name=\"Shininess\" value=\"0.75\" /><texture name=\"Texture1\" value=\"assets\\a.png\" /><texture name=\"Texture2\" value=\"n.png\" /></material>" + e +
        "<vertices type=\"standard\" vertexCount=\"3\">" + e +
        "0 0 0 0 0 1 ffffffff 0 0" + e + "2 0 0 0 0 1 ffffffff 1 0" + e + "0 3 0 0 0 1 ffffffff 0 1" + e + "</vertices>" + e +
        "<indices indexCount=\"3\">0 1 2</indices>" + e + "</buffer>" + e + "<buffer>" + e +
        "<material><enum name=\"Type\" value=\"solid\" /><texture name=\"Texture1\" value=\"b.jpg\" /></material>" + e +
        "<vertices type=\"2tcoords\" vertexCount=\"4\">" + e +
        "0 0 0 0 0 1 64ffffff 0 0 0.5 0.5" + e + "1 0 0 0 0 1 ffffffff 1 0 0.5 0.5" + e + "1 1 0 0 0 1 ffffffff 1 1 0.5 0.5" + e + "0 1 0 0 0 1 ffffffff 0 1 0.5 0.5" + e +
        "</vertices>" + e + "<indices indexCount=\"6\">" + e + "0 1 2 0 2 3" + e + "</indices>" + e + "</buffer>" + e + "<buffer>" + e +
        "<material><enum name=\"Type\" value=\"solid\" /></material>" + e +
        "<vertices type=\"tangents\" vertexCount=\"3\">" + e +
        "0 0 0 0 0 1 ffffffff 0 0 1 0 0 0 1 0" + e + "1 0 0 0 0 1 ffffffff 1 0 1 0 0 0 1 0" + e + "0 1 0 0 0 1 ffffffff 0 1 1 0 0 0 1 0" + e +
        "</vertices>" + e + "<indices indexCount=\"4\">0 1 2 0</indices>" + e + "</buffer>" + e + "</mesh>" + e;
    return s;
}
inline std::string ToUtf16le(const std::string& s) {
    std::string o = "\xFF\xFE";
    for (unsigned char c : s) { o.push_back((char)c); o.push_back('\0'); }
    return o;
}


// ---- B3D ----
// BB3D version 1: a texture "tex\\a.png"; one brush "mat" (colour 0.5 0.25 0.75, alpha 0.5, shininess 0.5, texture 0).
// NODE "root" (position (1,2,3), identity rotation) holds a mesh of one triangle (0,0,0) (1,0,0) (0,1,0), normals (0,0,1),
// uv (0,0) (1,0) (0,1), brush -1 in TRIS and 0 in MESH; NODE "bone1" (position (0,1,4), scale 2, rotation 90 degrees about Y,
// file order w x y z = 0.7071 0 0.7071 0) with BONE weights vertex 0: 1.0, vertex 1: 0.5, vertex 2: 0.25 and KEYS (position
// and rotation) at frames 0 and 30; NODE "bone2" with BONE vertex 1: 0.5; ANIM 30 frames at 30 per second.
struct B3dOpts { bool unweighted = false; bool twoTop = false; bool noAnim = false; bool noBone = false; };
inline std::string BuildB3d(const B3dOpts& o = {}) {
    auto I = [](int v) { std::string s; P32(s, (std::uint32_t)v); return s; };
    auto F = [](float v) { std::string s; PF(s, v); return s; };
    auto C = [](const char* t, const std::string& b) { std::string s = t; P32(s, (std::uint32_t)b.size()); return s + b; };
    auto Z = [](const std::string& n) { return n + std::string(1, '\0'); };
    auto V3 = [&](float a, float b, float c) { return F(a) + F(b) + F(c); };
    auto Q = [&](float w, float x, float y, float z) { return F(w) + F(x) + F(y) + F(z); };
    auto Node = [&](const std::string& name, const std::string& pos, const std::string& scl, const std::string& rot, const std::string& kids) { return C("NODE", Z(name) + pos + scl + rot + kids); };
    const float h = 0.70710678f;
    const std::string texs = C("TEXS", Z("tex\\a.png") + I(1) + I(2) + F(0) + F(0) + F(1) + F(1) + F(0));
    const std::string brus = C("BRUS", I(1) + Z("mat") + V3(0.5f, 0.25f, 0.75f) + F(0.5f) + F(0.5f) + I(1) + I(0) + I(0));
    std::string vrts = I(1) + I(1) + I(2);   // flags: normals; one set of 2 texture coordinates
    const float pos[3][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, uv[3][2] = {{0, 0}, {1, 0}, {0, 1}};
    for (int i = 0; i < 3; ++i) vrts += V3(pos[i][0], pos[i][1], pos[i][2]) + V3(0, 0, 1) + F(uv[i][0]) + F(uv[i][1]);
    const std::string mesh = C("MESH", I(0) + C("VRTS", vrts) + C("TRIS", I(-1) + I(0) + I(1) + I(2)));
    std::string bone1;
    if (!o.noBone) bone1 = C("BONE", I(0) + F(1.0f) + I(1) + F(0.5f) + (o.unweighted ? std::string() : I(2) + F(0.25f)));
    bone1 += C("KEYS", I(1 | 4) + I(0) + V3(0, 1, 4) + Q(1, 0, 0, 0) + I(30) + V3(0, 2, 4) + Q(h, 0, h, 0));
    const std::string b1 = Node("bone1", V3(0, 1, 4), V3(2, 2, 2), Q(h, 0, h, 0), bone1);
    const std::string b2 = Node("bone2", V3(0, 0, 0), V3(1, 1, 1), Q(1, 0, 0, 0), o.noBone ? std::string() : C("BONE", I(1) + F(0.5f)));
    std::string kids = mesh + b1 + b2;
    if (!o.noAnim) kids += C("ANIM", I(0) + I(30) + F(30.0f));
    std::string top = Node("root", V3(1, 2, 3), V3(1, 1, 1), Q(1, 0, 0, 0), kids);
    if (o.twoTop) top += Node("second", V3(0, 0, 0), V3(1, 1, 1), Q(1, 0, 0, 0), "");
    return C("BB3D", I(1) + texs + brus + top);
}

} // namespace fx
