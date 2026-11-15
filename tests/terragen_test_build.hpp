#pragma once
// Builds Terragen terrain (.ter) files in memory for the reader tests, following the published "Terragen Terrain file
// specification": a 16-byte identifier, then 4-byte-aligned chunks with no length field. Each helper appends one chunk.
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace terbuild {

inline void Put16(std::string& s, std::uint16_t v) { s.push_back((char)(v & 0xff)); s.push_back((char)(v >> 8)); }
inline void Put32(std::string& s, std::uint32_t v) { for (int k = 0; k < 4; ++k) s.push_back((char)((v >> (8 * k)) & 0xff)); }
inline void PutF(std::string& s, float f) { std::uint32_t u; std::memcpy(&u, &f, 4); Put32(s, u); }

inline std::string Header() { return std::string("TERRAGENTERRAIN "); }
inline std::string Dim(const char* tag, std::uint16_t v) { std::string s(tag, 4); Put16(s, v); Put16(s, 0); return s; }   // SIZE, XPTS, YPTS
inline std::string Scal(float x, float y, float z) { std::string s("SCAL"); PutF(s, x); PutF(s, y); PutF(s, z); return s; }
inline std::string Crad(float km) { std::string s("CRAD"); PutF(s, km); return s; }
inline std::string Crvm(std::uint32_t mode) { std::string s("CRVM"); Put32(s, mode); return s; }
// ALTW: HeightScale, BaseHeight, the elevations row by row, then padding to 4 bytes unless `pad` is false.
inline std::string Altw(std::int16_t heightScale, std::int16_t baseHeight, const std::vector<std::int16_t>& e, bool pad = true) {
    std::string s("ALTW");
    Put16(s, (std::uint16_t)heightScale); Put16(s, (std::uint16_t)baseHeight);
    for (std::int16_t v : e) Put16(s, (std::uint16_t)v);
    if (pad) while (s.size() % 4) s.push_back('\0');
    return s;
}
inline std::string Eof() { return std::string("EOF "); }

// A complete square or rectangular file: SIZE (shortest side - 1), XPTS and YPTS, SCAL, ALTW and EOF.
inline std::string Grid(int x, int y, std::int16_t heightScale, std::int16_t baseHeight, const std::vector<std::int16_t>& e,
                        float scale = 30.0f) {
    const int shortest = x < y ? x : y;
    return Header() + Dim("SIZE", (std::uint16_t)(shortest - 1)) + Dim("XPTS", (std::uint16_t)x) + Dim("YPTS", (std::uint16_t)y) +
           Scal(scale, scale, scale) + Altw(heightScale, baseHeight, e) + Eof();
}

} // namespace terbuild
