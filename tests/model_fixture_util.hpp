#pragma once
// Helpers shared by the batch-B reader tests (3MF / AMF / X3D / DXF / LWO): a model sanity check, small zip and
// deflate writers for synthetic packages, a deterministic random source. Synthetic fixtures are built in memory
// so nothing binary is committed.
#include "okay/Render/ModelData.hpp"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace fx {

inline bool Fin(const okay::Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
inline bool Bad(int line) { std::fprintf(stderr, "  Sane failed at condition %d\n", line); return false; }
// What the renderer and scene builder assume about any model a reader returns.
inline bool Sane(const okay::ImportedModel& m) {
    for (std::size_t i = 0; i < m.nodes.size(); ++i) {
        if (m.nodes[i].parent >= (int)i) return Bad(1);
        if (!Fin(m.nodes[i].position) || !Fin(m.nodes[i].scale)) return Bad(2);
        const okay::Quat& q = m.nodes[i].rotation;
        if (!std::isfinite(q.x) || !std::isfinite(q.y) || !std::isfinite(q.z) || !std::isfinite(q.w)) return Bad(3);
    }
    for (const okay::ImportedPart& p : m.parts) {
        const okay::Mesh& me = p.mesh;
        if (me.triangles.size() % 3) return Bad(4);
        for (int t : me.triangles) if (t < 0 || t >= (int)me.vertices.size()) return Bad(5);
        for (const okay::Vec3& v : me.vertices) if (!Fin(v)) return Bad(6);
        for (const okay::Vec3& v : me.normals) if (!Fin(v)) return Bad(7);
        if (!me.normals.empty() && me.normals.size() != me.vertices.size()) return Bad(8);
        if (!me.uvs.empty() && me.uvs.size() != me.vertices.size()) return Bad(9);
        for (const okay::Vec2& v : me.uvs) if (!std::isfinite(v.x) || !std::isfinite(v.y)) return Bad(16);
        if (p.material >= (int)m.materials.size()) return Bad(10);
        if (!m.nodes.empty() && (p.node < 0 || p.node >= (int)m.nodes.size())) return Bad(11);
    }
    for (const okay::ImportedMaterial& mt : m.materials)
        if (!std::isfinite(mt.diffuse.r + mt.diffuse.g + mt.diffuse.b + mt.diffuse.a) || !std::isfinite(mt.shininess)) return Bad(12);
    return true;
}

inline std::uint64_t& State() { static std::uint64_t s = 0x9E3779B97F4A7C15ull; return s; }
inline std::uint64_t Rnd() { std::uint64_t& g = State(); g ^= g << 13; g ^= g >> 7; g ^= g << 17; return g; }

inline std::uint32_t Crc32(const std::string& s) {
    std::uint32_t c = 0xFFFFFFFFu;
    for (unsigned char ch : s) { c ^= ch; for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1; }
    return c ^ 0xFFFFFFFFu;
}
inline void P16(std::string& s, unsigned v) { s.push_back((char)(v & 255)); s.push_back((char)(v >> 8)); }
inline void P32(std::string& s, std::uint32_t v) { for (int i = 0; i < 4; ++i) s.push_back((char)((v >> (8 * i)) & 255)); }

// Deflate with one fixed-Huffman block: literals only, or (literal run then `repeat` matches of 258 bytes at distance 1).
struct BitW {
    std::string out; unsigned acc = 0; int n = 0;
    void Bits(unsigned v, int cnt) { for (int i = 0; i < cnt; ++i) { acc |= ((v >> i) & 1u) << n; if (++n == 8) { out.push_back((char)acc); acc = 0; n = 0; } } }
    void Huff(unsigned code, int len) { for (int i = len - 1; i >= 0; --i) { acc |= ((code >> i) & 1u) << n; if (++n == 8) { out.push_back((char)acc); acc = 0; n = 0; } } }
    void Lit(int v) { if (v < 144) Huff(0x30 + v, 8); else if (v < 256) Huff(0x190 + v - 144, 9); else if (v < 280) Huff(v - 256, 7); else Huff(0xC0 + v - 280, 8); }
    void Flush() { if (n) { out.push_back((char)acc); acc = 0; n = 0; } }
};
inline std::string DeflateLiterals(const std::string& s) {
    BitW w; w.Bits(1, 1); w.Bits(1, 2);
    for (unsigned char c : s) w.Lit(c);
    w.Lit(256); w.Flush();
    return w.out;
}
// `first` literal bytes then `matches` x 258 copies of the last byte (length 258 = symbol 285, distance 1 = code 0).
inline std::string DeflateRepeat(char first, std::size_t matches) {
    BitW w; w.Bits(1, 1); w.Bits(1, 2);
    w.Lit((unsigned char)first);
    for (std::size_t i = 0; i < matches; ++i) { w.Lit(285); w.Huff(0, 5); }
    w.Lit(256); w.Flush();
    return w.out;
}

struct ZipEntry { std::string name, data; bool deflate = false; std::string raw; std::uint32_t usize = 0, crc = 0; bool rawSet = false; };
// A zip with stored / fixed-deflate entries. For hostile cases an entry can carry arbitrary compressed bytes and sizes.
inline std::string MakeZip(const std::vector<ZipEntry>& es) {
    std::string out, cd;
    for (const ZipEntry& e : es) {
        std::string comp = e.rawSet ? e.raw : (e.deflate ? DeflateLiterals(e.data) : e.data);
        const std::uint32_t usize = e.rawSet ? e.usize : (std::uint32_t)e.data.size();
        const std::uint32_t crc = e.rawSet ? e.crc : Crc32(e.data);
        const unsigned method = e.deflate || (e.rawSet && e.deflate) ? 8 : 0;
        const std::uint32_t off = (std::uint32_t)out.size();
        P32(out, 0x04034b50u); P16(out, 20); P16(out, 0); P16(out, method); P16(out, 0); P16(out, 0x21);
        P32(out, crc); P32(out, (std::uint32_t)comp.size()); P32(out, usize); P16(out, (unsigned)e.name.size()); P16(out, 0);
        out += e.name; out += comp;
        P32(cd, 0x02014b50u); P16(cd, 20); P16(cd, 20); P16(cd, 0); P16(cd, method); P16(cd, 0); P16(cd, 0x21);
        P32(cd, crc); P32(cd, (std::uint32_t)comp.size()); P32(cd, usize); P16(cd, (unsigned)e.name.size()); P16(cd, 0); P16(cd, 0); P16(cd, 0); P16(cd, 0); P32(cd, 0); P32(cd, off);
        cd += e.name;
    }
    const std::uint32_t cdOff = (std::uint32_t)out.size();
    out += cd;
    P32(out, 0x06054b50u); P16(out, 0); P16(out, 0); P16(out, (unsigned)es.size()); P16(out, (unsigned)es.size());
    P32(out, (std::uint32_t)cd.size()); P32(out, cdOff); P16(out, 0);
    return out;
}

} // namespace fx

// Little-endian float and fixed-size text field writers (MD2, MD3, B3D, DirectX .x fixtures).
namespace fx {
inline void PF(std::string& s, float f) { std::uint32_t v; std::memcpy(&v, &f, 4); P32(s, v); }
inline void PStr(std::string& s, const std::string& t, std::size_t len) { std::string x = t.substr(0, len); x.resize(len, '\0'); s += x; }
inline bool Near(float a, float b, float e = 1e-4f) { return std::fabs(a - b) < e; }
inline double Area(const okay::ImportedModel& m) {
    double a = 0;
    for (const okay::ImportedPart& p : m.parts)
        for (std::size_t i = 0; i + 2 < p.mesh.triangles.size(); i += 3) {
            const okay::Vec3 &x = p.mesh.vertices[(std::size_t)p.mesh.triangles[i]], &y = p.mesh.vertices[(std::size_t)p.mesh.triangles[i + 1]], &z = p.mesh.vertices[(std::size_t)p.mesh.triangles[i + 2]];
            const double ux = y.x - x.x, uy = y.y - x.y, uz = y.z - x.z, vx = z.x - x.x, vy = z.y - x.y, vz = z.z - x.z;
            const double cx = uy * vz - uz * vy, cy = uz * vx - ux * vz, cz = ux * vy - uy * vx;
            a += 0.5 * std::sqrt(cx * cx + cy * cy + cz * cz);
        }
    return a;
}
inline std::size_t TriCount(const okay::ImportedModel& m) { std::size_t n = 0; for (const okay::ImportedPart& p : m.parts) n += p.mesh.triangles.size() / 3; return n; }
} // namespace fx
