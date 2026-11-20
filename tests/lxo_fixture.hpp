#pragma once
// A SYNTHETIC Modo object (.lxo, an IFF FORM of type LXOB) built in memory, for the LXO side of our LWO reader
// (engine/src/Render/LwoReader.cpp). Every byte is written here from the public description of the LXO format and its
// LWO2-style geometry chunks (docs/content/model_readers/own_lwo_reader.md, section "LXO"); no file of any other project is copied.
//
// The object: a unit cube (8 points, 6 quads) in one layer whose LAYR flags have bit 0 set (Modo writes that on visible
// layers) and a parent of -1. Polygons 0-2 carry the material tag "Shell", 3-5 "Glass" (PTAG MATR), and every polygon also
// a PART tag (not read). NORM vertex maps: per point, the direction from the cube's centre; per polygon corner (VMAD), the
// four corners of polygon 0 point along -z (file space). The shader tree: a mask (id 5) that selects the material tag
// "Shell" holds an advancedMaterial (id 6: red, diffuse amount 0.5, transparency 0.25, smoothing off); the base material
// (id 7: white, 0.8, smoothing angle 100 degrees, so the cube's 90 degree edges are smoothed) sits under the render item (id 9). Items the reader skips: a mesh, a camera,
// the render item, and one ACTN chunk.
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace fx {
namespace lxo {

inline void B16(std::string& s, unsigned v) { s.push_back((char)(v >> 8)); s.push_back((char)v); }
inline void B32(std::string& s, std::uint32_t v) { for (int i = 3; i >= 0; --i) s.push_back((char)(v >> (8 * i))); }
inline void BF(std::string& s, float f) { std::uint32_t v; std::memcpy(&v, &f, 4); B32(s, v); }
inline void BS0(std::string& s, const std::string& x) { s += x; s.push_back(0); if ((x.size() + 1) & 1) s.push_back(0); }
inline std::string Chunk(const char* id, const std::string& body) { std::string s = id; B32(s, (std::uint32_t)body.size()); s += body; if (body.size() & 1) s.push_back(0); return s; }
inline std::string Sub(const char* id, const std::string& body) { std::string s = id; B16(s, (unsigned)body.size()); s += body; if (body.size() & 1) s.push_back(0); return s; }
inline std::string Form(const std::string& chunks) { std::string s = "FORM"; B32(s, (std::uint32_t)(chunks.size() + 4)); s += "LXOB"; s += chunks; return s; }

// the channel names, by position (CHAN refers to them by number)
inline const std::vector<std::string>& Channels() {
    static const std::vector<std::string> c{"unknown", "enable", "diffAmt", "tranAmt", "smooth", "smAngle", "visible"};
    return c;
}
inline unsigned Ch(const char* name) { const auto& c = Channels(); for (unsigned i = 0; i < c.size(); ++i) if (c[i] == name) return i; return 0; }
inline std::string ChanF(const char* name, float v) { std::string b; B16(b, Ch(name)); B16(b, 2); BF(b, v); return Sub("CHAN", b); }
inline std::string ChanI(const char* name, std::uint32_t v) { std::string b; B16(b, Ch(name)); B16(b, 1); B32(b, v); return Sub("CHAN", b); }
inline std::string ChanS(const char* name, const std::string& v) { std::string b; BS0(b, name); BS0(b, v); return Sub("CHNS", b); }
inline std::string Colour(float r, float g, float b) {
    std::string x; BS0(x, "diffCol"); B16(x, 2); B16(x, 3);
    BS0(x, "R"); BF(x, r); BS0(x, "G"); BF(x, g); BS0(x, "B"); BF(x, b);
    return Sub("CHNV", x);
}
inline std::string Link(std::uint32_t parent) { std::string b; BS0(b, "parent"); B32(b, parent); B32(b, 0); return Sub("LINK", b); }
inline std::string Item(const char* type, const std::string& name, std::uint32_t id, const std::string& subs) {
    std::string b; BS0(b, type); BS0(b, name); B32(b, id); b += subs; return Chunk("ITEM", b);
}

inline const float (&CubePoints())[8][3] {
    static const float p[8][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}};
    return p;
}
inline const int (&CubeQuads())[6][4] {
    static const int q[6][4] = {{3, 2, 1, 0}, {4, 5, 6, 7}, {0, 1, 5, 4}, {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7}};
    return q;
}

struct Opts {
    bool normals = true;    // the NORM vertex maps
    bool shader = true;     // CHNM and the material and mask items
};

inline std::string BuildLxo(const Opts& o = Opts()) {
    std::string c;
    { std::string v; B32(v, 2); B32(v, 1); BS0(v, "synthetic"); c += Chunk("VRSN", v); }
    { std::string t; BS0(t, "Shell"); BS0(t, "Glass"); BS0(t, "Part"); c += Chunk("TAGS", t); }
    if (o.shader) { std::string n; B32(n, (std::uint32_t)Channels().size()); for (const std::string& s : Channels()) BS0(n, s); c += Chunk("CHNM", n); }
    {   // LAYR as Modo writes it: index, flags (bit 0 set), pivot, empty name, parent -1, then fields the reader does not read
        std::string l; B16(l, 0); B16(l, 5); BF(l, 0); BF(l, 0); BF(l, 0); BS0(l, "");
        B16(l, 0xFFFF); BF(l, 2.0f); BF(l, 0.0873f); BF(l, 1); BF(l, 1); BF(l, 1); B32(l, 0);
        c += Chunk("LAYR", l);
    }
    { std::string p; for (const auto& pt : CubePoints()) for (float f : pt) BF(p, f); c += Chunk("PNTS", p); }
    { std::string b; for (int i = 0; i < 6; ++i) BF(b, i < 3 ? 0.0f : 1.0f); c += Chunk("BBOX", b); }
    {   std::string p = "FACE";
        for (const auto& q : CubeQuads()) { B16(p, 4); for (int k = 0; k < 4; ++k) B16(p, (unsigned)q[k]); }
        c += Chunk("POLS", p);
    }
    if (o.normals) {
        std::string m = "NORM"; B16(m, 3); BS0(m, "vert_normals");
        for (unsigned i = 0; i < 8; ++i) {
            const float* p = CubePoints()[i];
            const float d[3] = {p[0] - 0.5f, p[1] - 0.5f, p[2] - 0.5f};
            const float l = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            B16(m, i); BF(m, d[0] / l); BF(m, d[1] / l); BF(m, d[2] / l);
        }
        c += Chunk("VMAP", m);
        std::string d = "NORM"; B16(d, 3); BS0(d, "vert_normals");
        for (int k = 0; k < 4; ++k) { B16(d, (unsigned)CubeQuads()[0][k]); B16(d, 0); BF(d, 0); BF(d, 0); BF(d, -1); }
        c += Chunk("VMAD", d);
    }
    { std::string t = "MATR"; for (unsigned i = 0; i < 6; ++i) { B16(t, i); B16(t, i < 3 ? 0u : 1u); } c += Chunk("PTAG", t); }
    { std::string t = "PART"; for (unsigned i = 0; i < 6; ++i) { B16(t, i); B16(t, 2); } c += Chunk("PTAG", t); }
    c += Item("mesh", "", 0, ChanI("visible", 1));
    c += Item("camera", "", 1, std::string());
    if (o.shader) {
        c += Item("mask", "", 5, Link(9) + ChanI("enable", 1) + ChanS("ptag", "Shell") + ChanS("ptyp", "Material"));
        c += Item("advancedMaterial", "", 6, Link(5) + ChanI("enable", 1) + Colour(1, 0, 0) + ChanF("diffAmt", 0.5f) + ChanF("tranAmt", 0.25f) + ChanF("smooth", 0.0f) + ChanF("smAngle", 1.0f));
        c += Item("advancedMaterial", "Base Material", 7, Link(9) + Colour(1, 1, 1) + ChanF("diffAmt", 0.8f) + ChanF("smooth", 1.0f) + ChanF("smAngle", 1.7453293f));
    }
    c += Item("polyRender", "", 9, std::string());
    { std::string a; BS0(a, "setup"); BS0(a, "setup"); B32(a, 1); c += Chunk("ACTN", a); }
    return Form(c);
}

} // namespace lxo
} // namespace fx
