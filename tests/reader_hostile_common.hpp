#pragma once
// Shared by the standalone hostile-input tests of the own OFF / SMD / MD5 / MS3D / ASE readers: a deterministic random
// generator, file slurp, the "is this model sane" check every reader's output must pass, and a mutation helper.
#include "okay/Render/ModelData.hpp"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace hostile {
using namespace okay;

inline std::uint64_t& State() { static std::uint64_t s = 0x9E3779B97F4A7C15ull; return s; }
inline std::uint64_t Rnd() { std::uint64_t& g = State(); g ^= g << 13; g ^= g >> 7; g ^= g << 17; return g; }
inline std::string Slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
inline bool Fin(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
inline bool Bad(int line) { std::fprintf(stderr, "  Sane failed at condition %d\n", line); return false; }

// What the renderer and scene builder assume about any model a reader returns.
inline bool Sane(const ImportedModel& m) {
    for (std::size_t i = 0; i < m.nodes.size(); ++i) {
        if (m.nodes[i].parent >= (int)i) return Bad(1);
        if (!Fin(m.nodes[i].position) || !Fin(m.nodes[i].scale)) return Bad(2);
        const Quat& q = m.nodes[i].rotation;
        if (!std::isfinite(q.x) || !std::isfinite(q.y) || !std::isfinite(q.z) || !std::isfinite(q.w)) return Bad(3);
    }
    for (const ImportedPart& p : m.parts) {
        const Mesh& me = p.mesh;
        if (me.triangles.size() % 3) return Bad(4);
        for (int t : me.triangles) if (t < 0 || t >= (int)me.vertices.size()) return Bad(5);
        for (const Vec3& v : me.vertices) if (!Fin(v)) return Bad(6);
        for (const Vec3& v : me.normals) if (!Fin(v)) return Bad(7);
        if (!me.normals.empty() && me.normals.size() != me.vertices.size()) return Bad(8);
        if (!me.uvs.empty() && me.uvs.size() != me.vertices.size()) return Bad(9);
        if (p.material >= (int)m.materials.size()) return Bad(10);
        if (!m.nodes.empty() && (p.node < 0 || p.node >= (int)m.nodes.size())) return Bad(11);
        if (!p.bones.empty()) {
            if (p.jointIdx.size() != me.vertices.size() || p.jointWt.size() != me.vertices.size()) return Bad(16);
            for (const auto& ji : p.jointIdx) for (int j : ji) if (j < 0 || j >= (int)p.bones.size()) return Bad(17);
            for (const auto& jw : p.jointWt) for (float w : jw) if (!std::isfinite(w) || w < 0) return Bad(18);
            for (const ImportedBone& b : p.bones) for (int k = 0; k < 16; ++k) if (!std::isfinite(b.inverseBind.m[k])) return Bad(19);
        }
    }
    for (const ImportedClip& c : m.clips)
        for (const ImportedChannel& ch : c.channels) {
            bool known = false;
            for (const ImportedNode& n : m.nodes) if (n.name == ch.node) { known = true; break; }
            if (!known) return Bad(12);
            for (const auto& k : ch.position) if (!std::isfinite(k.time) || k.time < 0 || !Fin(k.value)) return Bad(13);
            for (const auto& k : ch.scale) if (!std::isfinite(k.time) || k.time < 0 || !Fin(k.value)) return Bad(14);
            for (const auto& k : ch.rotation) if (!std::isfinite(k.time) || k.time < 0 || !std::isfinite(k.value.w) || !std::isfinite(k.value.x) || !std::isfinite(k.value.y) || !std::isfinite(k.value.z)) return Bad(15);
        }
    return true;
}

// One random byte-level edit of `s` (flip, replace, insert, delete, stomp a 16/32 bit value).
inline void MutateBinary(std::string& s) {
    if (s.empty()) return;
    const std::size_t at = Rnd() % s.size();
    switch (Rnd() % 6) {
    case 0: s[at] = (char)(Rnd() & 255); break;
    case 1: s[at] = (char)(s[at] ^ (1 << (Rnd() % 8))); break;
    case 2: if (at + 4 <= s.size()) { const std::uint32_t v = Rnd() % 3 == 0 ? 0xFFFFFFFFu : (std::uint32_t)(Rnd() % 300); for (int i = 0; i < 4; ++i) s[at + (std::size_t)i] = (char)((v >> (8 * i)) & 255); } break;
    case 3: s.insert(at, std::string(1 + Rnd() % 8, (char)(Rnd() & 255))); break;
    case 4: s.erase(at, 1 + Rnd() % 16); break;
    default: if (at + 2 <= s.size()) { const unsigned v = (unsigned)(Rnd() % 4 == 0 ? 0xFFFF : Rnd() % 100); s[at] = (char)(v & 255); s[at + 1] = (char)(v >> 8); } break;
    }
}
// One random text-level edit, drawing replacement characters from `alphabet`.
inline void MutateText(std::string& s, const std::string& alphabet) {
    if (s.empty()) return;
    const std::size_t at = Rnd() % s.size();
    switch (Rnd() % 5) {
    case 0: s[at] = alphabet[Rnd() % alphabet.size()]; break;
    case 1: s.insert(at, std::string(1 + Rnd() % 6, alphabet[Rnd() % alphabet.size()])); break;
    case 2: s.erase(at, 1 + Rnd() % 12); break;
    case 3: { const std::size_t b = Rnd() % s.size(); s.insert(at, s.substr(b, 1 + Rnd() % 24)); break; }   // duplicate a piece
    default: s[at] = (char)(Rnd() & 255); break;
    }
}
inline long Iters(long def) {
    if (const char* e = std::getenv("OKAY_READER_FUZZ_ITERS")) return std::atol(e);
    return def;
}

} // namespace hostile
