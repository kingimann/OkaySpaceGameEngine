#pragma once
// Synthetic 3D GameStudio files (MDL3, MDL4, MDL5, MDL7 models and HMP4 / HMP5 terrains) for our own MDL and HMP readers.
// Built in memory, so nothing binary is committed. The MDL3 / MDL4 / MDL5 / HMP5 layouts follow Conitec's published
// descriptions ("3D GameStudio Programmer's Manual", May 2001, and the "Gamestudio file formats" page of the Gamestudio
// manual); the MDL7 layout follows our own analysis of three real files (docs/content/model_readers.md, section MDL). The
// expected values in the tests are worked out by hand from the numbers written here.
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace gs {

inline void P8(std::string& s, unsigned v) { s.push_back((char)(v & 255)); }
inline void P16(std::string& s, unsigned v) { P8(s, v); P8(s, v >> 8); }
inline void P32(std::string& s, std::uint32_t v) { for (int i = 0; i < 4; ++i) P8(s, v >> (8 * i)); }
inline void PF(std::string& s, float f) { std::uint32_t u; std::memcpy(&u, &f, 4); P32(s, u); }
inline void PStr(std::string& s, const std::string& t, std::size_t n) { std::string x = t; x.resize(n, '\0'); s += x; }
inline void Put32At(std::string& s, std::size_t o, std::uint32_t v) { for (int i = 0; i < 4; ++i) s[o + (std::size_t)i] = (char)((v >> (8 * i)) & 255); }

// ---- MDL3 / MDL4 / MDL5 --------------------------------------------------------------------------------------------------
// Four vertices, two triangles, four skin vertices.
//   MDL3 / MDL4: scale (0.5, 0.25, 2), offset (1, 2, 3), raw bytes (0,0,0) (2,0,0) (0,4,0) (2,4,1)
//                -> positions (1,2,3) (2,2,3) (1,3,3) (2,3,5); one 8-bit skin of 4 x 2 (type 0)
//                skin vertices (0,0) (3,0) (0,1) (3,1) -> u = (s + 0.5) / 4, v = (t + 0.5) / 2: (0.125,0.25) (0.875,0.25) (0.125,0.75) (0.875,0.75)
//   wordFrames (MDL4 / MDL5): the frame is of type 2 (unsigned shorts): raw (0,0,0) (4,0,0) (0,8,0) (4,8,2), scale halved, same positions
//   MDL5: the skin has its own size (type 12 = 888 RGB with mipmaps, 8 x 8), the header skin size is 0;
//         skin vertices (0,0) (7,0) (0,4) (7,4) -> (0.0625, 0.0625) (0.9375, 0.0625) (0.0625, 0.5625) (0.9375, 0.5625)
//   triangles (0,1,2) (1,3,2) in the file: clockwise, the reader reverses them
struct Mdl5Spec {
    char version = '3';
    bool wordFrames = false;
    int frames = 2;
    int skins = 1;
    int mdl5SkinType = 12;
};
struct Mdl5Offsets { std::size_t skins = 0, skinVerts = 0, tris = 0, frames = 0; };
inline std::string Mdl5(const Mdl5Spec& sp = Mdl5Spec{}, Mdl5Offsets* off = nullptr) {
    const bool v5 = sp.version == '5';
    const bool words = sp.wordFrames || v5;
    std::string o;
    o += "MDL"; o.push_back(sp.version); P32(o, 0);
    const float k = words ? 0.5f : 1.0f;
    PF(o, 0.5f * k); PF(o, 0.25f * k); PF(o, 2.0f * k);   // scale
    PF(o, 1.0f); PF(o, 2.0f); PF(o, 3.0f);               // offset
    P32(o, 0); PF(o, 0); PF(o, 0); PF(o, 0);             // pad, eye
    P32(o, (std::uint32_t)sp.skins);
    P32(o, v5 ? 0u : 4u); P32(o, v5 ? 0u : 2u);          // skin width / height (MDL3 / MDL4 only)
    P32(o, 4); P32(o, 2); P32(o, (std::uint32_t)sp.frames); P32(o, 4); P32(o, 0); P32(o, 0);
    if (off) off->skins = o.size();
    for (int s = 0; s < sp.skins; ++s) {
        if (!v5) { P32(o, 0); o.append(8, (char)7); }
        else {
            P32(o, (std::uint32_t)sp.mdl5SkinType); P32(o, 8); P32(o, 8);
            const int bpp = (sp.mdl5SkinType & 7) == 4 ? 3 : (sp.mdl5SkinType & 7) == 5 ? 4 : 2;
            std::size_t n = (std::size_t)bpp * 64;
            if (sp.mdl5SkinType & 8) n += (std::size_t)bpp * (16 + 4 + 1);
            o.append(n, (char)9);
        }
    }
    if (off) off->skinVerts = o.size();
    if (!v5) { P16(o, 0); P16(o, 0); P16(o, 3); P16(o, 0); P16(o, 0); P16(o, 1); P16(o, 3); P16(o, 1); }
    else { P16(o, 0); P16(o, 0); P16(o, 7); P16(o, 0); P16(o, 0); P16(o, 4); P16(o, 7); P16(o, 4); }
    if (off) off->tris = o.size();
    for (unsigned v : {0u, 1u, 2u, 0u, 1u, 2u}) P16(o, v);
    for (unsigned v : {1u, 3u, 2u, 1u, 3u, 2u}) P16(o, v);
    if (off) off->frames = o.size();
    const unsigned raw[4][3] = {{0, 0, 0}, {2, 0, 0}, {0, 4, 0}, {2, 4, 1}};
    for (int f = 0; f < sp.frames; ++f) {
        P32(o, words ? 2u : 0u);
        for (int b = 0; b < 2; ++b) { if (words) { P16(o, 0); P16(o, 0); P16(o, 0); P8(o, 0); P8(o, 0); } else { P8(o, 0); P8(o, 0); P8(o, 0); P8(o, 0); } }
        PStr(o, f == 0 ? "stand" : "walk", 16);
        for (const auto& r : raw) {
            const unsigned m = words ? 2u : 1u;
            const unsigned x = r[0] * m + (unsigned)f, y = r[1] * m, z = r[2] * m;
            if (words) { P16(o, x); P16(o, y); P16(o, z); P8(o, 0); P8(o, 0); } else { P8(o, x); P8(o, y); P8(o, z); P8(o, 0); }
        }
    }
    return o;
}

// ---- MDL7 ---------------------------------------------------------------------------------------------------------------
// Two groups.
//   group 0 "body": skins "red" (a material only: diffuse (1, 0, 0), the alpha word 0.5, specular (0, 0, 1), emissive
//   (0.1, 0.2, 0.3), power 20) and "wood" (a texture file name, tex/wood.png); 4 skin points (0,0) (1,0) (0,1) (1,1);
//   2 triangles, v (0,1,2) st (0,1,2) skin 0 and v (1,3,2) st (1,3,2) skin 1; vertices (0,0,0) (1,0,0) (0,1,0) (1,1,1);
//   one frame "bindpose" with no vertices.
//   group 1 (no name): no skins, 1 triangle v (0,1,2) with no skin points and skin 0, vertices (5,0,0) (6,0,0) (5,1,0).
struct Mdl7Spec {
    std::string texture = "tex/wood.png";
    int animatedFrameVerts = 0;   // > 0: group 0's frame carries this many frame vertices (not read; the reader notes them)
    int bones = 0;                // > 0: this count at offset 8 and that many 36-byte records before the first group (our guess at bones; the reader refuses it)
};
inline std::string Mdl7(const Mdl7Spec& sp = Mdl7Spec{}) {
    std::string o;
    o += "MDL7"; P32(o, 0); P32(o, (std::uint32_t)sp.bones); P32(o, 2); P32(o, 0); P32(o, 0); P32(o, 0);
    for (unsigned v : {36u, 28u, 16u, 68u, 8u, 16u, 26u, 26u, 68u, 24u}) P16(o, v);
    for (int b = 0; b < sp.bones; ++b) { P16(o, 0xFFFF); P16(o, 0); PStr(o, "bone", 20); PF(o, 0); PF(o, 0); PF(o, 0); }
    auto vert = [&](float x, float y, float z) { PF(o, x); PF(o, y); PF(o, z); P16(o, 0xFFFF); PF(o, 0); PF(o, 0); PF(o, -1); };
    auto tri = [&](unsigned a, unsigned b, unsigned c, int sa, int sb, int sc, unsigned mat) {
        P16(o, a); P16(o, b); P16(o, c); P16(o, (unsigned)sa & 0xFFFF); P16(o, (unsigned)sb & 0xFFFF); P16(o, (unsigned)sc & 0xFFFF); P32(o, mat);
    };
    // group 0
    std::size_t g0 = o.size();
    P32(o, 1); P32(o, 0); PStr(o, "body", 16);
    P32(o, 2); P32(o, 4); P32(o, 2); P32(o, 4); P32(o, 1);
    P32(o, 0x10); P32(o, 0); P32(o, 0); PStr(o, "red", 16);
    for (float f : {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 0.1f, 0.2f, 0.3f, 0.0f, 20.0f}) PF(o, f);
    P32(o, 7); P32(o, (std::uint32_t)sp.texture.size() + 1); P32(o, 1); PStr(o, "wood", 16);
    o += sp.texture; o.push_back('\0');
    for (float f : {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f}) PF(o, f);
    tri(0, 1, 2, 0, 1, 2, 0);
    tri(1, 3, 2, 1, 3, 2, 1);
    vert(0, 0, 0); vert(1, 0, 0); vert(0, 1, 0); vert(1, 1, 1);
    PStr(o, "bindpose", 16); P32(o, (std::uint32_t)sp.animatedFrameVerts); P32(o, 0);
    for (int i = 0; i < sp.animatedFrameVerts; ++i) vert(9, 9, 9);
    Put32At(o, g0 + 4, (std::uint32_t)(o.size() - g0));
    // group 1
    std::size_t g1 = o.size();
    P32(o, 1); P32(o, 0); PStr(o, "", 16);
    P32(o, 0); P32(o, 0); P32(o, 1); P32(o, 3); P32(o, 0);
    tri(0, 1, 2, -1, -1, -1, 0);
    vert(5, 0, 0); vert(6, 0, 0); vert(5, 1, 0);
    Put32At(o, g1 + 4, (std::uint32_t)(o.size() - g1));
    Put32At(o, 16, (std::uint32_t)o.size());
    return o;
}
namespace mdl7off { constexpr std::size_t kBones = 8, kGroups = 12, kDataSize = 16, kSizes = 28, kGroup0 = 48; }

// ---- HMP4 / HMP5 --------------------------------------------------------------------------------------------------------
// A 2 x 2 grid (cell 4 x 4, origin (-2, -2, 1), z scale 0.5), heights 0 2 4 6 -> z 1 2 3 4, one skin:
//   HMP5: type 2 (565) with its own size 2 x 2 (8 bytes); HMP4: type 2, the size in the header (2 x 2)
inline std::string Hmp45(char version, int skins = 1) {
    std::string o;
    o += "HMP"; o.push_back(version); P32(o, 2);
    PF(o, 4.0f / 65536.0f); PF(o, 4.0f / 65536.0f); PF(o, 0.5f);
    PF(o, -2.0f); PF(o, -2.0f); PF(o, 1.0f);
    P32(o, 0); PF(o, 4.0f); PF(o, 4.0f); PF(o, 2.0f);
    P32(o, (std::uint32_t)skins);
    P32(o, version == '4' ? 2u : 0u); P32(o, version == '4' ? 2u : 0u);
    P32(o, 4); P32(o, 0); P32(o, 1); P32(o, 0); P32(o, 0); P32(o, 0);
    for (int s = 0; s < skins; ++s) {
        P32(o, 2);
        if (version != '4') { P32(o, 2); P32(o, 2); }
        o.append(8, (char)3);
    }
    P32(o, 2); o.append(32, '\0');
    for (unsigned h : {0u, 2u, 4u, 6u}) { P16(o, h); P8(o, 0); P8(o, 0); }
    return o;
}

} // namespace gs
