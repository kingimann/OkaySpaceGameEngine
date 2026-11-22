#pragma once
// SYNTHETIC fixtures for the batch-E own readers (Q3D, RAW), written by hand from the format descriptions (no real file, no
// importer output; the Q3D layout follows the corpus files where the description is open, docs/content/model_readers.md). Expected numbers in the tests are computed by hand from these.
#include <cstdint>
#include <cstring>
#include <string>

namespace fx2 {

// Writer with a chosen byte order.
struct QW {
    std::string s; bool be;
    explicit QW(bool bigEndian) : be(bigEndian) {}
    void U8(unsigned v) { s.push_back((char)v); }
    void U16(unsigned v) { if (be) { U8(v >> 8); U8(v & 255); } else { U8(v & 255); U8(v >> 8); } }
    void I32(std::uint32_t v) { if (be) for (int i = 3; i >= 0; --i) U8((v >> (8 * i)) & 255); else for (int i = 0; i < 4; ++i) U8((v >> (8 * i)) & 255); }
    void F(float f) { std::uint32_t u; std::memcpy(&u, &f, 4); I32(u); }
    void Str(const std::string& t) { s += t; s.push_back('\0'); }
};

// One mesh of five vertices: the unit quad z = 0 (vertices 0 to 3, counter-clockwise from above) and the point (0, 0, 2) (vertex 4).
//   face 0: the quad 0 1 2 3, material 0 ("red", transparency 0.25), texture coordinate indices 0 1 2 3
//   face 1: the triangle 0 1 4, material 1 ("blue"), texture coordinate indices 0 1 2
// Four texture coordinates (0,0) (1,0) (1,1) (0,1) when `tex` (one 2x1 texture, 6 pixel bytes, is in the file then too).
// Areas: quad 1, triangle 1. Triangles after triangulation: 2 + 1.
inline std::string BuildQ3d(bool be = false, bool scene = false, bool tex = true) {
    QW w(be);
    w.s += scene ? "quick3Ds" : "quick3Do";
    w.s += "30";
    w.I32(1); w.I32(2); w.I32(tex ? 1 : 0);
    w.U8('m');
    w.I32(5);
    const float v[5][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 0, 2}};
    for (const auto& p : v) { w.F(p[0]); w.F(p[1]); w.F(p[2]); }
    w.I32(2);                       // faces
    w.U16(4); w.U16(3);             // shapes
    for (int i : {0, 1, 2, 3, 0, 1, 4}) w.I32((std::uint32_t)i);
    w.I32(0); w.I32(1);             // materials
    w.I32(0);                       // normals
    w.I32(tex ? 4 : 0);             // texture coordinate count: always there (the coordinates only when the header names textures)
    if (tex) {
        const float t[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        for (const auto& p : t) { w.F(p[0]); w.F(p[1]); }
        for (int i : {0, 1, 2, 3, 0, 1, 2}) w.I32((std::uint32_t)i);
    }
    for (int i = 0; i < 9; ++i) w.F(0.0f);      // centre of mass, bounding box (6 floats)
    w.U8('c');
    // name, ambient, diffuse, specular, transparency, texture index (-1 = none)
    w.Str("red");  for (float f : {0.1f, 0.1f, 0.1f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f}) w.F(f); w.F(0.25f); w.I32(tex ? 0u : 0xFFFFFFFFu);
    w.Str("blue"); for (float f : {0.1f, 0.1f, 0.1f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f}) w.F(f); w.F(0.0f); w.I32(0xFFFFFFFFu);
    if (tex) { w.U8('t'); w.Str("tex"); w.I32(2); w.I32(1); for (int i = 0; i < 6; ++i) w.U8(i * 40); }
    if (scene) { w.U8('s'); for (int i = 0; i < 64; ++i) w.U8(0); }
    return w.s;
}

// Two groups: "wing" (one triangle) and an unnamed first triangle; a texture-name line; a coloured line; comments and blank lines.
//   default group "RAW": (0,0,0) (1,0,0) (0,1,0)            area 0.5
//   group "wing":        (0,0,0) (2,0,0) (0,2,0)            area 2
//   line with a name:    (0,0,0) (0,0,3) (3,0,0) wood.tga   group "wood.tga", area 4.5
//   colour line:         1 0 0 then (0,0,0) (0,0,2) (2,0,0) joins group "wood.tga", area 2
inline std::string BuildRaw(const char* eol = "\n") {
    std::string e = eol, s;
    s += "# a comment" + e;
    s += "0 0 0  1 0 0  0 1 0" + e;
    s += e;
    s += "wing" + e;
    s += "0 0 0 2 0 0 0 2 0" + e;
    s += "0 0 0   0 0 3   3 0 0 wood.tga" + e;
    s += "1 0 0  0 0 0  0 0 2  2 0 0" + e;
    return s;
}

} // namespace fx2
