#pragma once
// Synthetic fixtures for the batch-E own readers (MDC, HMP, CSM), built in memory so nothing binary is committed. Every number is
// chosen so the expected result can be worked out by hand; the comment above each builder says what the numbers are. The layouts
// are written from the format descriptions the readers cite (docs/content/model_readers/own_mdc_reader.md, own_hmp_reader.md, own_csm_reader.md).
#include "model_fixture_util.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace fx {

inline void PS16(std::string& s, int v) { P16(s, (unsigned)(v & 0xFFFF)); }
inline void Put32(std::string& s, std::size_t at, std::uint32_t v) { for (int i = 0; i < 4; ++i) s[at + (std::size_t)i] = (char)((v >> (8 * i)) & 255); }
inline std::uint32_t Get32(const std::string& s, std::size_t at) { std::uint32_t v = 0; for (int i = 0; i < 4; ++i) v |= (std::uint32_t)(unsigned char)s[at + (std::size_t)i] << (8 * i); return v; }

// ======================= MDC =======================
// One surface "quad": 4 vertices at (0,0,0) (1,0,0) (1,1,0) (0,1,0) (stored as 1/64 units: 64 = 1.0), all with the packed normal 0
// (= (0,0,1)), texture coordinates (0,0) (0.5,0) (0.5,0.25) (0,0.25), two triangles stored clockwise as Quake III does:
// (0,2,1) and (0,3,2), which read as (0,1,2) and (0,2,3) counter clockwise. One shader "textures\base\a.tga". Three frames: frame 0
// is base frame 0 with no compressed frame; frames 1 and 2 are compressed frames 0 and 1. Compressed frame 0 holds the offsets
// (+10, 0, 0) (0, -20, 0) (0, 0, +2) (0, 0, 0) in units of 0.05 (so +0.5, -1.0, +0.1, 0) with 127 as zero, compressed frame 1 is all zero
// offsets. Two tags: "tag_head" at (64,128,192)/64 = (1,2,3) and "tag_weapon" at (-32,0,16)/64 = (-0.5,0,0.25).
// `compFirst`: frame 0 uses compressed frame 0 (so the positions get the offsets above).
struct MdcSpec {
    std::string name = "testmodel";
    bool compFirst = false;
    int frames = 3;
    int tags = 2;
    std::string shader = "textures\\base\\a.tga";
    bool secondSurface = false;   // a second surface "tri" (3 vertices, 1 triangle) with its own shader
};
inline std::string BuildMdc(const MdcSpec& sp = MdcSpec{}) {
    struct Surf { std::string name, shader; std::vector<std::array<int, 3>> pos; std::vector<std::array<float, 2>> uv; std::vector<std::array<int, 3>> tris; };
    std::vector<Surf> surfs;
    surfs.push_back(Surf{"quad", sp.shader, {{0, 0, 0}, {64, 0, 0}, {64, 64, 0}, {0, 64, 0}}, {{0, 0}, {0.5f, 0}, {0.5f, 0.25f}, {0, 0.25f}}, {{0, 2, 1}, {0, 3, 2}}});
    if (sp.secondSurface) surfs.push_back(Surf{"tri", "models/b", {{0, 0, 64}, {64, 0, 64}, {0, 64, 64}}, {{0, 0}, {1, 0}, {0, 1}}, {{0, 2, 1}}});
    const int tags = sp.tags;
    std::string o;
    o += "IDPC"; P32(o, 2); PStr(o, sp.name, 64);
    P32(o, 0);                                  // flags
    P32(o, (std::uint32_t)sp.frames); P32(o, (std::uint32_t)tags); P32(o, (std::uint32_t)surfs.size()); P32(o, 0);
    const std::uint32_t ofsFrames = 112, ofsTagNames = ofsFrames + 56u * (std::uint32_t)sp.frames, ofsTags = ofsTagNames + 64u * (std::uint32_t)tags;
    const std::uint32_t ofsSurf = ofsTags + 12u * (std::uint32_t)sp.frames * (std::uint32_t)tags;
    P32(o, ofsFrames); P32(o, ofsTagNames); P32(o, ofsTags); P32(o, ofsSurf);
    const std::size_t endAt = o.size(); P32(o, 0);   // ofsEnd, patched below
    for (int f = 0; f < sp.frames; ++f) { for (int k = 0; k < 6; ++k) PF(o, 0.0f); for (int k = 0; k < 3; ++k) PF(o, 0.0f); PF(o, 1.0f); PStr(o, "frame" + std::to_string(f), 16); }
    const char* tagNames[] = {"tag_head", "tag_weapon"};
    for (int t = 0; t < tags; ++t) PStr(o, tagNames[t % 2], 64);
    const int tagPos[2][3] = {{64, 128, 192}, {-32, 0, 16}};
    for (int f = 0; f < sp.frames; ++f) for (int t = 0; t < tags; ++t) { for (int k = 0; k < 3; ++k) PS16(o, tagPos[t % 2][k] + (f ? 7 : 0)); PS16(o, 1); PS16(o, 2); PS16(o, 3); }
    for (const Surf& s : surfs) {
        const std::size_t base = o.size();
        const std::uint32_t nv = (std::uint32_t)s.pos.size(), nt = (std::uint32_t)s.tris.size();
        const std::uint32_t nComp = (std::uint32_t)(sp.frames > 1 ? sp.frames - 1 : 0);
        const std::uint32_t oTris = 124, oSh = oTris + 12 * nt, oSt = oSh + 68, oXn = oSt + 8 * nv, oXc = oXn + 8 * nv, oFb = oXc + 4 * nComp * nv, oFc = oFb + 2 * (std::uint32_t)sp.frames, oEnd = oFc + 2 * (std::uint32_t)sp.frames;
        P32(o, 7); PStr(o, s.name, 64); P32(o, 0);
        P32(o, nComp); P32(o, 1); P32(o, 1); P32(o, nv); P32(o, nt);
        P32(o, oTris); P32(o, oSh); P32(o, oSt); P32(o, oXn); P32(o, oXc); P32(o, oFb); P32(o, oFc); P32(o, oEnd);
        for (const auto& t : s.tris) for (int k = 0; k < 3; ++k) P32(o, (std::uint32_t)t[(std::size_t)k]);
        PStr(o, s.shader, 64); P32(o, 0);
        for (const auto& u : s.uv) { PF(o, u[0]); PF(o, u[1]); }
        for (const auto& p : s.pos) { for (int k = 0; k < 3; ++k) PS16(o, p[(std::size_t)k]); PS16(o, 0); }
        for (std::uint32_t c = 0; c < nComp; ++c)
            for (std::uint32_t v = 0; v < nv; ++v) {
                int d[3] = {0, 0, 0};
                if (c == 0 && &s == &surfs[0]) { if (v == 0) d[0] = 10; if (v == 1) d[1] = -20; if (v == 2) d[2] = 2; }
                for (int k = 0; k < 3; ++k) o.push_back((char)(127 + d[k]));
                o.push_back((char)5);   // the compressed normal index
            }
        for (int f = 0; f < sp.frames; ++f) PS16(o, 0);
        for (int f = 0; f < sp.frames; ++f) PS16(o, f == 0 ? (sp.compFirst ? 0 : -1) : f - 1);
        (void)base;
    }
    Put32(o, endAt, (std::uint32_t)o.size());
    return o;
}
// Offsets into BuildMdc's output (default spec, one surface) used by the hostile tests to patch a field.
namespace mdcoff {
constexpr std::size_t kNumFrames = 76, kNumTags = 80, kNumSurfaces = 84, kOfsFrames = 92, kOfsTagNames = 96, kOfsTags = 100, kOfsSurfaces = 104, kOfsEnd = 108;
// within a surface header (add the surface offset)
constexpr std::size_t sNumComp = 72, sNumBase = 76, sNumShaders = 80, sNumVerts = 84, sNumTris = 88, sOfsTris = 92, sOfsShaders = 96, sOfsSt = 100, sOfsXyzN = 104, sOfsXyzC = 108, sOfsFrBase = 112, sOfsFrComp = 116, sOfsEnd = 120;
inline std::size_t Surface(const std::string& file) { return Get32(file, kOfsSurfaces); }
} // namespace mdcoff

// ======================= HMP =======================
// 3 columns x 3 rows. Scale z 0.5, origin (-1,-2,10), grid step (2,3). Heights (row major, row 0 first):
//   0 2 4 / 6 8 10 / 12 14 16   so  z = 10 + 0.5*h = 10 + c + 3r  (a plane), x = -1 + 2c, y = -2 + 3r.
// The plane's normal is (-1/3, -2/3, 2/3). Eight triangles, texture coordinates u = c/2, v = 1 - r/2.
struct HmpSpec {
    int nx = 3, ny = 3;
    bool skin = false;          // one 2 x 2 skin of type 4 (12 pixel bytes)
    int skinType = 4;
    int frames = 1;
    char version = '7';
    float stepX = 2.0f, stepY = 3.0f;
};
inline std::string BuildHmp(const HmpSpec& sp = HmpSpec{}, const std::vector<std::uint16_t>* heights = nullptr) {
    std::string o;
    o += "HMP"; o.push_back(sp.version); P32(o, 0);
    PF(o, 0.01f); PF(o, 0.01f); PF(o, 0.5f);          // scale
    PF(o, -1.0f); PF(o, -2.0f); PF(o, 10.0f);         // origin
    PF(o, 0.0f);                                      // bounding radius
    PF(o, sp.stepX); PF(o, sp.stepY); PF(o, (float)sp.nx);
    P32(o, sp.skin ? 1u : 0u); P32(o, 0); P32(o, 0);
    P32(o, (std::uint32_t)(sp.nx * sp.ny)); P32(o, 0); P32(o, (std::uint32_t)sp.frames);
    P32(o, 0); P32(o, 0); P32(o, 0);
    if (sp.skin) { P32(o, (std::uint32_t)sp.skinType); P32(o, 2); P32(o, 2); for (int i = 0; i < 12; ++i) o.push_back((char)(i * 20)); }
    P32(o, 2); for (int i = 0; i < 8; ++i) P32(o, 0);   // the 36 byte frame header
    for (int r = 0; r < sp.ny; ++r)
        for (int c = 0; c < sp.nx; ++c) {
            const std::size_t i = (std::size_t)r * (std::size_t)sp.nx + (std::size_t)c;
            const unsigned h = heights ? (*heights)[i] : (unsigned)(2 * c + 6 * r);
            P16(o, h); o.push_back((char)0); o.push_back((char)64);
        }
    return o;
}
namespace hmpoff { constexpr std::size_t kNumX = 44, kNumSkins = 48, kNumVerts = 60, kNumFrames = 68; }

// ======================= CSM =======================
// Three markers A, B, C; rate 50; frames 1, 2 and 4 (a gap): the keys are at (frame - 1) / 50 = 0, 0.02 and 0.06 seconds.
//   frame 1: A (0,0,0)   B (10,0,0)  C (1,2,3)
//   frame 2: A (0,1,0)   B (10,1,0)  C (1,2,4)
//   frame 4: A (0,3,0)   B (10,3,0)  C (1,2,6)
// The names of the markers are spread over two lines of the $Order section.
inline std::string BuildCsm(const char* eol = "\n") {
    const std::string n = eol;
    return "$Filename t.csm" + n + "$Date 2024/01/02" + n + "$Time 10:00:00" + n + "$Actor Tester" + n + n + "$Comments" + n + "a comment" + n + n +
           "$FirstFrame 1" + n + "$LastFrame 4" + n + "$Rate 50.0" + n + n + "$Order" + n + "A B" + n + "C" + n + n + "$Points" + n +
           "1  0 0 0  10 0 0  1 2 3" + n + "2  0 1 0  10 1 0  1 2 4" + n + "4  0 3 0  10 3 0  1 2 6" + n;
}

} // namespace fx
