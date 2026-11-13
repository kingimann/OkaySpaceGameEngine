// Hostile-input tests and a deterministic mutation fuzz for the own Blender (.blend) reader. Needs only
// okay/Render/ModelData.hpp, engine/src/Render/BlendReader.cpp and the in-tree inflate (Core/ImageDecode*.cpp), so
// it builds on its own:
//   g++ -std=c++17 -O1 -g -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all
//       -Iengine/include -Iengine/src/Core -Itests -DOKAY_SOURCE_DIR='"."' tests/test_blend_hostile.cpp
//       engine/src/Render/BlendReader.cpp engine/src/Core/ImageDecode.cpp engine/src/Core/ImageDecodeJpeg.cpp -o hostile
// OKAY_BLEND_FUZZ_ITERS=200000 for a long run. With OKAY_MODEL_CORPUS=<assimp checkout>/test the fuzz also mutates the
// real files in models/BLEND (and OKAY_BLEND_FUZZ_REAL_ITERS sets how many mutations per real file).
#include "test_framework.hpp"
#include "okay/Render/ModelData.hpp"
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace okay;

static const std::string kDir = std::string(OKAY_SOURCE_DIR) + "/tests/data/models/";
static std::string Slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

static bool Fin(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
static bool Bad(int line) { std::fprintf(stderr, "  Sane failed at condition %d\n", line); return false; }
// What the renderer and scene builder assume about any model a reader returns.
static bool Sane(const ImportedModel& m) {
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
            for (const auto& k : ch.rotation) if (!std::isfinite(k.time) || k.time < 0 || !std::isfinite(k.value.w)) return Bad(15);
        }
    return true;
}

static std::uint64_t g_state = 0x9E3779B97F4A7C15ull;
static std::uint64_t Rnd() { g_state ^= g_state << 13; g_state ^= g_state >> 7; g_state ^= g_state << 17; return g_state; }

static double g_slowest = 0;
// a successful read must be sane; a failed one must say why and leave nothing behind; none may take long
static void TryBlend(const std::string& bytes, bool flatten = false) {
    ImportedModel m; std::string err;
    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = ReadBlendBuffer(bytes, "", m, &err, flatten);
    const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (dt > g_slowest) g_slowest = dt;
    if (dt > 5.0) std::fprintf(stderr, "  slow read: %.2f s\n", dt);
    CHECK(dt < 20.0);
    if (ok) { CHECK(Sane(m)); }
    else { CHECK(!err.empty() && m.parts.empty() && m.nodes.empty()); }
}

static void P32(std::string& s, std::uint32_t v) { for (int i = 0; i < 4; ++i) s.push_back((char)((v >> (8 * i)) & 255)); }
// a hand written block header + data (little endian, 64 bit pointers)
static std::string Block(const char* code, const std::string& data, std::uint64_t old = 0x1000, int sdna = 0, std::uint32_t nr = 1) {
    std::string s(code, 4);
    P32(s, (std::uint32_t)data.size());
    for (int i = 0; i < 8; ++i) s.push_back((char)((old >> (8 * i)) & 255));
    P32(s, (std::uint32_t)sdna); P32(s, nr);
    return s + data;
}
static std::string Header() { return "BLENDER-v410"; }
static std::string Pad4(std::string s) { while (s.size() % 4) s.push_back('\0'); return s; }
// a minimal valid SDNA with one struct "Object" of the given fields (types: int, float, char, pointers via '*')
static std::string Sdna(const std::vector<std::pair<std::string, std::string>>& fields, std::uint32_t objSize, std::uint32_t claimedNames = 0xFFFFFFFFu) {
    std::string s = "SDNA";
    std::vector<std::string> names, types{"char", "int", "float", "Object"};
    for (auto& f : fields) names.push_back(f.second);
    s += "NAME"; P32(s, claimedNames == 0xFFFFFFFFu ? (std::uint32_t)names.size() : claimedNames);
    for (auto& n : names) { s += n; s.push_back('\0'); }
    s = Pad4(s);
    s += "TYPE"; P32(s, (std::uint32_t)types.size());
    for (auto& t : types) { s += t; s.push_back('\0'); }
    s = Pad4(s);
    s += "TLEN";
    const std::uint16_t lens[4] = {1, 4, 4, (std::uint16_t)objSize};
    for (auto l : lens) { s.push_back((char)(l & 255)); s.push_back((char)(l >> 8)); }
    s = Pad4(s);
    s += "STRC"; P32(s, 1);
    s.push_back(3); s.push_back(0); s.push_back((char)fields.size()); s.push_back(0);
    for (std::size_t i = 0; i < fields.size(); ++i) {
        const int ti = fields[i].first == "char" ? 0 : fields[i].first == "int" ? 1 : 2;
        s.push_back((char)ti); s.push_back(0); s.push_back((char)i); s.push_back(0);
    }
    return s;
}

int main() {
    RUN_SUITE("blend_hostile");
    const char* names[] = {"blend_attr_41.blend", "blend_attr_36.blend", "blend_attr_be32.blend", "blend_attr_41_gz.blend", "blend_skin_anim.blend"};
    std::vector<std::string> fixtures;
    for (const char* n : names) { fixtures.push_back(Slurp(kDir + n)); CHECK(!fixtures.back().empty()); }

    // ---------------- every truncation of every fixture ----------------
    for (const std::string& f : fixtures)
        for (std::size_t cut = 0; cut < f.size(); cut += (cut < 64 ? 1 : 13)) { TryBlend(f.substr(0, cut)); if (cut % 5 == 0) TryBlend(f.substr(0, cut), true); }

    // ---------------- container ----------------
    {
        ImportedModel m; std::string err;
        TryBlend(std::string());
        TryBlend("BLENDER");
        TryBlend("BLENDER-v410");
        TryBlend("BLENDER-V410" + std::string(100, '\xff'));
        TryBlend("BLENDER_v999" + std::string(100, '\0'));
        TryBlend("BLENDER-x410" + std::string(100, '\0'));
        TryBlend("BLENDER-v4a0" + std::string(100, '\0'));
        CHECK(!ReadBlendBuffer("BLENDER17-01v0500" + std::string(300, '\0'), "", m, &err) && err.find("5.0") != std::string::npos);
        // a block size of -1 / 2 GB, a block that runs past the file, a file that ends without ENDB / DNA
        { std::string s = Header(); s += Block("DNA1", Sdna({{"int", "x"}}, 4)); TryBlend(s); }
        { std::string s = Header(); s += Block("OB\0\0", std::string(16, 'x')); s[12 + 4] = (char)0xFF; s[12 + 5] = (char)0xFF; s[12 + 6] = (char)0xFF; s[12 + 7] = (char)0xFF; TryBlend(s); }
        { std::string s = Header(); s += Block("OB\0\0", std::string(16, 'x')); s[12 + 7] = (char)0x7F; TryBlend(s); }
        { std::string s = Header() + Block("DNA1", Sdna({{"int", "x"}}, 4)) + Block("ENDB", ""); ImportedModel mm; CHECK(!ReadBlendBuffer(s, "", mm, &err) && err.find("no objects") != std::string::npos); }
        // SDNA claims 4 billion names / structs / fields, has a field type out of range, a struct size that does not match its fields
        { std::string s = Header() + Block("DNA1", Sdna({{"int", "x"}}, 4, 0xFFFFFFF0u)) + Block("ENDB", ""); TryBlend(s); }
        { std::string d = Sdna({{"int", "x"}}, 4); d[d.find("STRC") + 8] = (char)0xFF; d[d.find("STRC") + 9] = (char)0xFF; TryBlend(Header() + Block("DNA1", d) + Block("ENDB", "")); }
        { std::string d = Sdna({{"int", "x"}}, 400); TryBlend(Header() + Block("DNA1", d) + Block("ENDB", "")); }
        { std::string d = Sdna({{"int", "x[2147483647]"}}, 4); TryBlend(Header() + Block("DNA1", d) + Block("ENDB", "")); }
        { std::string d = Sdna({{"int", "x[99999][99999]"}, {"int", "y[99999][99999]"}}, 4); TryBlend(Header() + Block("DNA1", d) + Block("ENDB", "")); }
        { std::string d = Sdna({{"int", "*x[1000000][1000000]"}}, 4); TryBlend(Header() + Block("DNA1", d) + Block("ENDB", "")); }
        { std::string d = Sdna({{"int", "x[0]"}, {"int", "y[-3]"}, {"int", "z["}, {"int", "(*f"}, {"float", "***p"}}, 4); TryBlend(Header() + Block("DNA1", d) + Block("ENDB", "")); }
        // an object block whose data is shorter than its struct; an object whose parent is itself; a block with nr = 4 billion
        {
            const std::string d = Sdna({{"int", "type"}, {"float", "loc[3]"}, {"char", "*parent"}}, 4 + 12 + 8);
            std::string ob(8, '\0');   // too short
            TryBlend(Header() + Block("DNA1", d) + Block("OB\0\0", ob, 0x2000, 0) + Block("ENDB", ""));
            std::string self(24, '\0');
            const std::uint64_t a = 0x2000; for (int i = 0; i < 8; ++i) self[16 + i] = (char)((a >> (8 * i)) & 255);   // parent = itself
            TryBlend(Header() + Block("DNA1", d) + Block("OB\0\0", self, 0x2000, 0, 0xFFFFFFFFu) + Block("ENDB", ""));
            TryBlend(Header() + Block("DNA1", d) + Block("OB\0\0", self, 0x2000, 0) + Block("OB\0\0", self, 0x2000, 0) + Block("ENDB", ""));
            TryBlend(Header() + Block("DNA1", d) + Block("OB\0\0", self, 0x2000, 7) + Block("ENDB", ""));              // struct index out of range
            TryBlend(Header() + Block("DNA1", d) + Block("OB\0\0", self, 0x2000, -1) + Block("ENDB", ""));
        }
        // a gzip stream that claims a huge size, a stored-block decompression bomb of 40 MiB of zeros, a stream cut short
        {
            std::string gz("\x1f\x8b\x08\x00\0\0\0\0\0\0", 10);
            std::string body(40u * 1024u * 1024u, '\0');
            for (std::size_t off = 0; off < body.size(); off += 65535) {
                const std::size_t n = std::min<std::size_t>(65535, body.size() - off);
                gz.push_back(off + n >= body.size() ? 1 : 0);
                gz.push_back((char)(n & 255)); gz.push_back((char)(n >> 8)); gz.push_back((char)(~n & 255)); gz.push_back((char)((~n >> 8) & 255));
                gz.append(body, off, n);
            }
            P32(gz, 0); P32(gz, (std::uint32_t)body.size());
            ImportedModel mm;
            CHECK(!ReadBlendBuffer(gz, "", mm, &err) && err.find("not a .blend") != std::string::npos);   // inflates, then is not a blend file
            std::string wrongSize = gz; wrongSize[wrongSize.size() - 1] = 0x7f;
            CHECK(!ReadBlendBuffer(wrongSize, "", mm, &err));
            CHECK(!ReadBlendBuffer(gz.substr(0, gz.size() / 2), "", mm, &err));
        }
    }

    // ---------------- structured corruption of the fixtures ----------------
    {
        // Every block header field and a random spread of data words stomped one at a time.
        for (const std::string& f : fixtures) {
            if (f.size() > 2 && (unsigned char)f[0] == 0x1f) continue;   // gzip: the block layout is not visible
            const bool le = f[8] == 'v';
            const std::size_t ps = f[7] == '-' ? 8 : 4, hs = 16 + ps;
            std::vector<std::size_t> heads;
            for (std::size_t pos = 12; pos + hs <= f.size();) {
                heads.push_back(pos);
                std::uint32_t sz = 0;
                for (int i = 0; i < 4; ++i) sz |= (std::uint32_t)(unsigned char)f[pos + 4 + (std::size_t)(le ? i : 3 - i)] << (8 * i);
                pos += hs + sz;
            }
            const std::uint32_t vals[] = {0, 1, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu, 4, 0xFFFFu};
            for (std::size_t h : heads)
                for (std::size_t field : {(std::size_t)4, 8 + ps, 12 + ps}) {      // size, sdna index, count
                    for (std::uint32_t v : vals) {
                        std::string s = f;
                        for (int i = 0; i < 4; ++i) s[h + field + (std::size_t)i] = (char)((v >> (8 * (le ? i : 3 - i))) & 255);
                        TryBlend(s);
                    }
                }
            // pointer scramble: every aligned word replaced by the address of another block
            std::vector<std::uint64_t> addrs;
            for (std::size_t h : heads) { std::uint64_t a = 0; for (std::size_t i = 0; i < ps; ++i) a |= (std::uint64_t)(unsigned char)f[h + 8 + i] << (8 * (le ? i : ps - 1 - i)); addrs.push_back(a); }
            for (int round = 0; round < 400; ++round) {
                std::string s = f;
                for (int k = 0; k < 6; ++k) {
                    const std::size_t at = (std::size_t)(Rnd() % ((f.size() - ps) / ps)) * ps;
                    const std::uint64_t a = Rnd() % 5 == 0 ? Rnd() : addrs[Rnd() % addrs.size()];
                    for (std::size_t i = 0; i < ps; ++i) s[at + i] = (char)((a >> (8 * (le ? i : ps - 1 - i))) & 255);
                }
                TryBlend(s, round % 7 == 0);
            }
        }
    }

    // ---------------- mutation fuzz ----------------
    auto mutate = [&](std::string s) {
        const int ops = 1 + (int)(Rnd() % 5);
        for (int o = 0; o < ops && !s.empty(); ++o) {
            const std::size_t at = Rnd() % s.size();
            switch (Rnd() % 7) {
            case 0: s[at] = (char)(Rnd() & 255); break;
            case 1: s[at] = (char)(s[at] ^ (1 << (Rnd() % 8))); break;
            case 2: if (at + 4 <= s.size()) { const std::uint32_t v = Rnd() % 3 == 0 ? 0xFFFFFFFFu : (std::uint32_t)(Rnd() % 300); std::memcpy(&s[at], &v, 4); } break;
            case 3: s.insert(at, std::string(1 + Rnd() % 8, (char)(Rnd() & 255))); break;
            case 4: s.erase(at, 1 + Rnd() % 16); break;
            case 5: if (at + 4 <= s.size()) { const std::uint32_t pick[] = {0x7F800000u, 0xFF800000u, 0x7FC00000u, 0x7F7FFFFFu, 0x4F000000u, 0xCF000000u, 0x5F000000u}; std::memcpy(&s[at], &pick[Rnd() % 7], 4); } break;   // inf, -inf, NaN, max float, 2^31, -2^31, 2^63
            default: if (at + 2 <= s.size()) { const std::uint16_t v = (std::uint16_t)(Rnd() % 4 == 0 ? 0xFFFF : Rnd() % 100); std::memcpy(&s[at], &v, 2); } break;
            }
        }
        return s;
    };
    {
        long iters = 4000;
        if (const char* e = std::getenv("OKAY_BLEND_FUZZ_ITERS")) iters = std::atol(e);
        for (long it = 0; it < iters; ++it) TryBlend(mutate(fixtures[Rnd() % fixtures.size()]), it % 5 == 0);
        std::printf("  fuzz: %ld mutations of the fixtures\n", iters);
    }
    if (const char* corpus = std::getenv("OKAY_MODEL_CORPUS")) {
        long iters = 300;
        if (const char* e = std::getenv("OKAY_BLEND_FUZZ_REAL_ITERS")) iters = std::atol(e);
        const char* real[] = {"models/BLEND/BlenderDefault_248.blend", "models/BLEND/BlenderDefault_269.blend", "models/BLEND/box.blend", "models/BLEND/yxa_1.blend",
                              "models/BLEND/TorusLightsCams_250_compressed.blend", "models/BLEND/test_279.blend", "models-nonbsd/BLEND/Bob.blend", "models/BLEND/CubeHierarchy_248.blend",
                              "models-nonbsd/BLEND/fleurOptonl.blend", "models/BLEND/HUMAN.blend"};
        long total = 0;
        for (const char* r : real) {
            const std::string f = Slurp(std::string(corpus) + "/" + r);
            if (f.empty()) continue;
            for (std::size_t cut = 0; cut < f.size(); cut += f.size() / 150 + 1) TryBlend(f.substr(0, cut));
            for (long it = 0; it < iters; ++it) { TryBlend(mutate(f), it % 5 == 0); ++total; }
        }
        std::printf("  fuzz: %ld mutations of the real files\n", total);
    }
    std::printf("  slowest single read: %.3f s\n", g_slowest);

    TEST_MAIN_RESULT();
}
