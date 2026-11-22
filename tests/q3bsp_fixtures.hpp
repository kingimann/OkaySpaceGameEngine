#pragma once
// SYNTHETIC Quake III Arena map files for the own Q3BSP reader tests, built in memory by a small writer that follows the public
// description of the format (144 byte header: "IBSP", version 46, 17 lumps of offset and length; records of 72 bytes (textures),
// 44 (vertexes), 4 (meshverts), 104 (faces), 40 (models)). No real map is in the repository.
#include "model_fixture_util.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace q3 {

struct Tex { std::string name; std::int32_t flags = 0, contents = 0; };
struct V { float p[3]; float st[2]; float n[3]; };
struct Face { std::int32_t tex = 0, type = 1, vertex = 0, nVerts = 0, meshvert = 0, nMeshverts = 0, size0 = 0, size1 = 0; };
struct Model { std::int32_t face = 0, nFaces = 0; };

struct Map {
    std::vector<Tex> tex; std::vector<V> v; std::vector<std::int32_t> mv; std::vector<Face> f; std::vector<Model> m;
    std::int32_t version = 46;
    std::string magic = "IBSP";
    bool noModels = false;

    std::string Build() const {
        std::string lumps[17];
        for (const Tex& t : tex) { fx::PStr(lumps[1], t.name, 64); fx::P32(lumps[1], (std::uint32_t)t.flags); fx::P32(lumps[1], (std::uint32_t)t.contents); }
        for (const V& x : v) {
            for (float c : x.p) fx::PF(lumps[10], c);
            fx::PF(lumps[10], x.st[0]); fx::PF(lumps[10], x.st[1]); fx::PF(lumps[10], 0.f); fx::PF(lumps[10], 0.f);   // lightmap coordinates
            for (float c : x.n) fx::PF(lumps[10], c);
            lumps[10] += std::string("\xff\xff\xff\xff", 4);
        }
        for (std::int32_t k : mv) fx::P32(lumps[11], (std::uint32_t)k);
        for (const Face& x : f) {
            fx::P32(lumps[13], (std::uint32_t)x.tex); fx::P32(lumps[13], (std::uint32_t)-1);
            fx::P32(lumps[13], (std::uint32_t)x.type); fx::P32(lumps[13], (std::uint32_t)x.vertex); fx::P32(lumps[13], (std::uint32_t)x.nVerts);
            fx::P32(lumps[13], (std::uint32_t)x.meshvert); fx::P32(lumps[13], (std::uint32_t)x.nMeshverts);
            fx::P32(lumps[13], (std::uint32_t)-1);                       // lightmap index
            lumps[13] += std::string(8 + 8 + 12 + 24, '\0');           // lightmap start, size, origin, vectors
            lumps[13] += std::string(12, '\0');                         // normal
            fx::P32(lumps[13], (std::uint32_t)x.size0); fx::P32(lumps[13], (std::uint32_t)x.size1);
        }
        if (!noModels) for (const Model& x : m) {
            lumps[7] += std::string(24, '\0');
            fx::P32(lumps[7], (std::uint32_t)x.face); fx::P32(lumps[7], (std::uint32_t)x.nFaces);
            lumps[7] += std::string(8, '\0');
        }
        std::string out = magic;
        fx::P32(out, (std::uint32_t)version);
        std::uint32_t off = 8 + 17 * 8;
        for (int i = 0; i < 17; ++i) { fx::P32(out, off); fx::P32(out, (std::uint32_t)lumps[i].size()); off += (std::uint32_t)lumps[i].size(); }
        for (int i = 0; i < 17; ++i) out += lumps[i];
        return out;
    }
};

inline V Vt(float x, float y, float z, float s, float t, float nx = 0, float ny = 0, float nz = 1) { return V{{x, y, z}, {s, t}, {nx, ny, nz}}; }

// The map most tests read. Textures: 0 "textures/base/wall", 1 "textures/common/nodraw" (flag 0x80), 2 "textures/base/floor".
// Faces: 0 a 64 x 64 polygon quad at z = 0 (two triangles, clockwise as Quake III stores them, normal +z) with texture 0;
// 1 a mesh (type 3) triangle at z = 10 with texture 2; 2 a nodraw quad (left out); 3 a flat 3 x 3 patch at z = 20 (control points
// 0, 32, 64 in x and y, stored normals +z) with texture 0; 4 a billboard (type 4, not read);
// 5 a polygon quad at x = 100 (model 1) with texture 2. Model 0 holds faces 0..4, model 1 holds face 5.
inline Map Basic() {
    Map m;
    m.tex = {{"textures/base/wall"}, {"textures/common/nodraw", 0x80}, {"textures/base/floor"}};
    // face 0: vertices 0..3
    m.v.push_back(Vt(0, 0, 0, 0, 0)); m.v.push_back(Vt(64, 0, 0, 1, 0)); m.v.push_back(Vt(64, 64, 0, 1, 1)); m.v.push_back(Vt(0, 64, 0, 0, 1));
    m.mv = {0, 2, 1, 0, 3, 2};
    m.f.push_back({0, 1, 0, 4, 0, 6});
    // face 1: vertices 4..6, one triangle at z = 10 (clockwise seen from +z: (0,0) -> (0,64) -> (64,0))
    m.v.push_back(Vt(0, 0, 10, 0, 0)); m.v.push_back(Vt(0, 64, 10, 0, 1)); m.v.push_back(Vt(64, 0, 10, 1, 0));
    m.mv.insert(m.mv.end(), {0, 1, 2});
    m.f.push_back({2, 3, 4, 3, 6, 3});
    // face 2: nodraw quad, vertices 7..10
    m.v.push_back(Vt(0, 0, 30, 0, 0)); m.v.push_back(Vt(64, 0, 30, 1, 0)); m.v.push_back(Vt(64, 64, 30, 1, 1)); m.v.push_back(Vt(0, 64, 30, 0, 1));
    m.mv.insert(m.mv.end(), {0, 2, 1, 0, 3, 2});
    m.f.push_back({1, 1, 7, 4, 9, 6});
    // face 3: patch, vertices 11..19, row r (y), column c (x)
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) m.v.push_back(Vt(32.f * (float)c, 32.f * (float)r, 20, 0.5f * (float)c, 0.5f * (float)r));
    { Face p; p.tex = 0; p.type = 2; p.vertex = 11; p.nVerts = 9; p.size0 = 3; p.size1 = 3; m.f.push_back(p); }
    // face 4: a billboard
    m.v.push_back(Vt(0, 0, 40, 0, 0));
    { Face b; b.tex = 0; b.type = 4; b.vertex = 20; b.nVerts = 1; m.f.push_back(b); }
    // face 5: a quad at x = 100, vertices 21..24
    m.v.push_back(Vt(100, 0, 0, 0, 0)); m.v.push_back(Vt(164, 0, 0, 1, 0)); m.v.push_back(Vt(164, 64, 0, 1, 1)); m.v.push_back(Vt(100, 64, 0, 0, 1));
    m.mv.insert(m.mv.end(), {0, 2, 1, 0, 3, 2});
    m.f.push_back({2, 1, 21, 4, 15, 6});
    m.m = {{0, 5}, {5, 1}};
    return m;
}

inline std::string Pk3(const std::string& bsp, bool withTexture = true, bool deflate = false) {
    std::vector<fx::ZipEntry> es;
    fx::ZipEntry s; s.name = "scripts/x.shader"; s.data = "// nothing\n"; es.push_back(s);
    fx::ZipEntry b; b.name = "maps/test.bsp"; b.data = bsp; b.deflate = deflate; es.push_back(b);
    if (withTexture) { fx::ZipEntry t; t.name = "textures/base/wall.tga"; t.data = "fake"; es.push_back(t); }
    return fx::MakeZip(es);
}

} // namespace q3
