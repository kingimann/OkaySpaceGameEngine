#pragma once
// SYNTHETIC Silo (.sib) files for the own SIB reader tests, built in memory by a writer that follows the structure worked out
// from the one sample file (docs/own_sib_reader.md): chunks of a four letter tag, a 32 bit size and the bytes.
#include "model_fixture_util.hpp"
#include <string>
#include <vector>

namespace sibfx {

inline std::string Chunk(const char* tag, const std::string& body) { std::string s(tag, 4); fx::P32(s, (std::uint32_t)body.size()); return s + body; }
inline std::string U16(const std::string& ascii) { std::string s; for (char c : ascii) { s.push_back(c); s.push_back('\0'); } return s; }
inline std::string LenStr(const std::string& ascii) { std::string s; fx::P32(s, (std::uint32_t)(ascii.size() * 2)); return s + U16(ascii); }
inline std::string Ints(const std::vector<std::int32_t>& v) { std::string s; for (std::int32_t x : v) fx::P32(s, (std::uint32_t)x); return s; }

inline std::string Matr(float r, float g, float b, const std::string& name, const std::string& tex) {
    std::string s;
    for (float f : {r, g, b, 0.f, 0.6f, 0.6f, 0.6f, 0.f, 0.1f, 0.1f, 0.1f, 0.f, 0.f, 0.f, 0.f, 0.f}) fx::PF(s, f);
    fx::P32(s, 0x7f);
    s += LenStr(name) + LenStr(tex);
    return Chunk("MATR", s);
}

struct FaceDef { std::vector<int> v; std::vector<std::pair<float, float>> uv; };
struct Shape { std::string name; std::vector<float> pos; std::vector<FaceDef> faces; std::vector<std::int32_t> fama = {0, 0}; bool uv = true; bool mirror = false; };

inline std::string Shp(const Shape& sh) {
    std::string vrts, facs, ftvs;
    for (float f : sh.pos) fx::PF(vrts, f);
    int i = 0;
    for (const FaceDef& f : sh.faces) {
        fx::P32(facs, (std::uint32_t)f.v.size());
        for (int v : f.v) fx::P32(facs, (std::uint32_t)v);
        fx::P32(ftvs, (std::uint32_t)i++); fx::P32(ftvs, (std::uint32_t)f.uv.size());
        for (auto& p : f.uv) { fx::PF(ftvs, p.first); fx::PF(ftvs, p.second); }
    }
    std::string body;
    body += Chunk("DINF", Ints({0, 1, 1, 0, 0}));
    body += Chunk("PINF", std::string(32, '\0'));
    body += Chunk("AXIS", std::string(48, '\0'));
    body += Chunk("SNAM", U16(sh.name));
    body += Chunk("MIRP", std::string(28, '\0'));
    body += Chunk("IMRP", Ints({0, sh.mirror ? 1 : 0}));
    body += Chunk("VRTS", vrts) + Chunk("VMIR", "") + Chunk("EDGS", "") + Chunk("ECRS", "") + Chunk("TXSM", "");
    body += Chunk("FACS", facs) + Chunk("FAHS", "");
    if (sh.uv) body += Chunk("FTVS", ftvs);
    body += Chunk("FMIR", "") + Chunk("FAMA", Ints(sh.fama));
    return Chunk("SHAP", body);
}

inline std::string File(const std::vector<std::string>& chunks) {
    std::string s = Chunk("HEAD", Ints({1}));
    for (const std::string& c : chunks) s += c;
    return s;
}

// A unit cube from (0,0,0) to (1,1,1): six quads, counter clockwise seen from outside, with the unit square as texture coordinates on every face.
inline Shape Cube() {
    Shape s; s.name = "Cube";
    s.pos = {0,0,0, 1,0,0, 1,1,0, 0,1,0, 0,0,1, 1,0,1, 1,1,1, 0,1,1};
    const std::vector<std::pair<float, float>> q = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    s.faces = {{{0, 3, 2, 1}, q}, {{4, 5, 6, 7}, q}, {{0, 1, 5, 4}, q}, {{3, 7, 6, 2}, q}, {{0, 4, 7, 3}, q}, {{1, 2, 6, 5}, q}};
    s.fama = {0, 1, 3, 0};   // faces 0..2 material 1, faces 3..5 material 0
    return s;
}
// An L shaped hexagon in z = 0 (three unit squares, area 3), counter clockwise: concave, so it needs ear clipping.
inline Shape LShape() {
    Shape s; s.name = "L";
    s.pos = {0,0,0, 2,0,0, 2,1,0, 1,1,0, 1,2,0, 0,2,0};
    s.faces = {{{0, 1, 2, 3, 4, 5}, {{0, 0}, {2, 0}, {2, 1}, {1, 1}, {1, 2}, {0, 2}}}};
    s.fama = {0, -1};
    return s;
}

} // namespace sibfx
