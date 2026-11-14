// Hostile input and mutation fuzz for the batch-C own readers: MD2, MD3, IRRMESH, B3D, DirectX .x. Needs only the reader
// sources and okay/Render/ModelData.hpp (and the engine's Core/ImageDecode*.cpp for the compressed .x), so it builds on its
// own, e.g.
//   g++ -std=c++17 -O1 -g -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all -DOKAY_STANDALONE_TEST
//       -Iengine/include -Iengine/src/Core -Itests tests/test_batchc_hostile.cpp engine/src/Render/<readers>.cpp
//       engine/src/Core/ImageDecode.cpp engine/src/Core/ImageDecodeJpeg.cpp -o hostile
// OKAY_BATCHC_FUZZ_ITERS=200000 for a long run (per reader). All fixtures here are SYNTHETIC.
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include "batchc_fixtures.hpp"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace okay;

static long Iters() { const char* e = std::getenv("OKAY_BATCHC_FUZZ_ITERS"); return e ? std::atol(e) : 3000; }
static double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

// A read must either give a sane model or fail with a message and leave nothing behind.
template <class F> static void Try(F&& read, const std::string& bytes) {
    ImportedModel m; std::string err;
    const bool ok = read(bytes, m, err);
    if (ok) { CHECK(fx::Sane(m)); }
    else { CHECK(!err.empty() && m.parts.empty() && m.nodes.empty()); }
}
static std::string Mutate(const std::string& src) {
    std::string s = src;
    const int kind = (int)(fx::Rnd() % 6);
    if (s.empty()) return s;
    const std::size_t p = (std::size_t)(fx::Rnd() % s.size());
    switch (kind) {
    case 0: s[p] = (char)(fx::Rnd() & 255); break;
    case 1: s[p] ^= (char)(1u << (fx::Rnd() % 8)); break;
    case 2: s.erase(p, (std::size_t)(fx::Rnd() % 16) + 1); break;
    case 3: { std::string ins; for (int i = (int)(fx::Rnd() % 8) + 1; i > 0; --i) ins.push_back((char)(fx::Rnd() & 255)); s.insert(p, ins); break; }
    case 4: { const char* t[] = {"0", "-1", "99999999999", "1e308", "nan", "-0", "4294967295", "2147483648", " ", ",", "-", "e", "."}; s.replace(p, std::min<std::size_t>(3, s.size() - p), t[fx::Rnd() % 13]); break; }
    default: s.resize(p); break;
    }
    return s;
}
static void Put32(std::string& s, std::size_t at, std::uint32_t v) { if (at + 4 <= s.size()) std::memcpy(&s[at], &v, 4); }
// Every truncation, then a mutation sweep.
template <class F> static void Sweep(const char* name, F&& read, const std::string& fixture, long iters) {
    const double t0 = Now();
    for (std::size_t n = 0; n < fixture.size(); ++n) Try(read, fixture.substr(0, n));
    for (long i = 0; i < iters; ++i) Try(read, Mutate(fixture));
    std::fprintf(stderr, "  %s: %zu truncations + %ld mutations in %.1f s\n", name, fixture.size(), iters, Now() - t0);
}

// ======================= MD2 =======================
static bool ReadMd2(const std::string& b, ImportedModel& m, std::string& e) { return ReadMd2Buffer(b, "", m, &e); }
static void Md2() {
    const std::size_t kFrame0 = 68 + 64 + 16 + 24;   // header, one skin, 4 texture coordinates, 2 triangles: the first frame's scale starts here
    const std::string good = fx::BuildMd2();
    {   ImportedModel m; std::string e; CHECK(ReadMd2(good, m, e) && fx::Sane(m)); }
    Sweep("md2", ReadMd2, good, Iters());
    // every header field set to the extremes, one at a time (offsets and counts from 0 to 0xFFFFFFFF)
    for (std::size_t f = 1; f < 17; ++f)
        for (std::uint32_t v : {0u, 1u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu, 0xFFFFFFFEu, 1000000u, 1000001u, 4000001u}) {
            std::string s = good; Put32(s, f * 4, v); Try(ReadMd2, s);
        }
    // counts at the caps with a file that is far too small: refused without allocating
    {   std::string s = good; Put32(s, 24, 1000000u); Try(ReadMd2, s);
        ImportedModel m; std::string e; CHECK(!ReadMd2(s, m, e)); }
    // NaN, infinity and a huge scale in the first frame
    for (std::uint32_t bits : {0x7FC00000u, 0x7F800000u, 0xFF800000u, 0x7F7FFFFFu}) {
        std::string s = good; Put32(s, kFrame0, bits); Try(ReadMd2, s);
        s = good; Put32(s, kFrame0 + 12, bits); Try(ReadMd2, s);
    }
    // vertex coordinates at the largest legal scale stay finite after the arithmetic
    {   std::string s = good; const float big = 1e9f; std::uint32_t u; std::memcpy(&u, &big, 4);
        Put32(s, kFrame0, u); Put32(s, kFrame0 + 4, u); Put32(s, kFrame0 + 8, u);
        ImportedModel m; std::string e; CHECK(ReadMd2(s, m, e) && fx::Sane(m)); }
}

// ======================= MD3 =======================
static bool ReadMd3(const std::string& b, ImportedModel& m, std::string& e) { return ReadMd3Buffer(b, m, &e); }
static void Md3() {
    const std::string good = fx::BuildMd3();
    {   ImportedModel m; std::string e; CHECK(ReadMd3(good, m, e) && fx::Sane(m)); }
    Sweep("md3", ReadMd3, good, Iters());
    const std::uint32_t ext[] = {0u, 1u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu, 0xFFFFFFFEu, 4096u, 4097u, 100000u, 100001u, 2000000u, 2000001u, 4000001u};
    for (std::size_t f = 2; f < 27; ++f) for (std::uint32_t v : ext) { std::string s = good; Put32(s, f * 4, v); Try(ReadMd3, s); }
    // the surface header (the first surface starts after the header, 2 frames and the tag of each frame)
    const std::size_t surf = 108 + 56 * 2 + 112 * 2;
    for (std::size_t f = 0; f < 10; ++f) for (std::uint32_t v : ext) { std::string s = good; Put32(s, surf + 4 + 64 + f * 4, v); Try(ReadMd3, s); }   // flags .. offsetEnd
    // NaN / inf / huge in a tag and in the texture coordinates
    for (std::uint32_t bits : {0x7FC00000u, 0x7F800000u, 0xFF800000u, 0x7F7FFFFFu}) {
        for (std::size_t k = 0; k < 12; ++k) { std::string s = good; Put32(s, 108 + 56 * 2 + 64 + k * 4, bits); Try(ReadMd3, s); }
        for (std::size_t k = 0; k < 6; ++k) { std::string s = good; Put32(s, surf + 188 + k * 4, bits); Try(ReadMd3, s); }
    }
}

// ======================= IRRMESH =======================
static bool ReadIrr(const std::string& b, ImportedModel& m, std::string& e) { return ReadIrrMeshBuffer(b, "", m, &e); }
static std::string Rep(std::string s, const std::string& from, const std::string& to) { const std::size_t p = s.find(from); if (p != std::string::npos) s.replace(p, from.size(), to); return s; }
static void Irr() {
    const std::string good = fx::BuildIrrMesh();
    {   ImportedModel m; std::string e; CHECK(ReadIrr(good, m, e) && fx::Sane(m)); }
    Sweep("irrmesh", ReadIrr, good, Iters());
    Sweep("irrmesh (UTF-16)", ReadIrr, fx::ToUtf16le(good), Iters() / 4);
    // counts at, past and far past the caps, with tiny text: refused without allocating
    for (const char* c : {"0", "-1", "4000000", "4000001", "99999999999", "1e308", "nan", "1.5", "", "x"}) {
        Try(ReadIrr, Rep(good, "vertexCount=\"3\"", std::string("vertexCount=\"") + c + "\""));
        Try(ReadIrr, Rep(good, "indexCount=\"3\"", std::string("indexCount=\"") + c + "\""));
    }
    // not-a-number, infinity, overflow and hex-float tokens in positions, normals, uv and indices
    for (const char* t : {"nan", "inf", "-inf", "1e999", "1e308", "0x10", "--1", "1.2.3", "+", "e5", "4294967296", "-1", "99999999999999999999"}) {
        Try(ReadIrr, Rep(good, "2 0 0 0 0 1 ffffffff 1 0", std::string(t) + " 0 0 0 0 1 ffffffff 1 0"));
        Try(ReadIrr, Rep(good, "2 0 0 0 0 1 ffffffff 1 0", std::string("2 0 0 ") + t + " 0 1 ffffffff 1 0"));
        Try(ReadIrr, Rep(good, "2 0 0 0 0 1 ffffffff 1 0", std::string("2 0 0 0 0 1 ffffffff ") + t + " 0"));
        Try(ReadIrr, Rep(good, "0 1 2 0 2 3", std::string("0 1 2 0 2 ") + t));
    }
    // colours: too long, not hex, empty
    for (const char* c : {"fffffffff", "gg", "", "-1", "0x1"}) Try(ReadIrr, Rep(good, "ff8040c0", c));
    // hostile structure: an entity bomb, deep nesting, 20,000 empty buffers, a 3 MB token, many attributes
    Try(ReadIrr, "<!DOCTYPE m [<!ENTITY a \"aaaaaaaaaa\"><!ENTITY b \"&a;&a;&a;&a;&a;&a;&a;&a;&a;&a;\">]><mesh><buffer/></mesh>");
    { std::string s = "<mesh>"; for (int i = 0; i < 5000; ++i) s += "<buffer>"; Try(ReadIrr, s); }
    { std::string s = "<mesh>"; for (int i = 0; i < 20000; ++i) s += "<buffer><vertices type=\"standard\" vertexCount=\"0\"/></buffer>"; s += "</mesh>"; Try(ReadIrr, s); }
    { std::string s = "<mesh><buffer><vertices type=\"standard\" vertexCount=\"1\">" + std::string(3u << 20, '7') + "</vertices></buffer></mesh>"; Try(ReadIrr, s); }
    { std::string s = "<mesh><buffer><material>"; for (int i = 0; i < 50000; ++i) s += "<texture name=\"Texture3\" value=\"x\"/>"; s += "</material></buffer></mesh>"; Try(ReadIrr, s); }
    // a big valid mesh: 60,000 vertices and 100,000 triangles read in reasonable time
    {
        std::string s = "<mesh><buffer><vertices type=\"standard\" vertexCount=\"60000\">";
        for (int i = 0; i < 60000; ++i) s += std::to_string(i % 300) + " " + std::to_string(i / 300) + " 0 0 0 1 ffffffff 0 0\n";
        s += "</vertices><indices indexCount=\"300000\">";
        for (int i = 0; i < 100000; ++i) s += std::to_string((i * 7) % 60000) + " " + std::to_string((i * 13 + 1) % 60000) + " " + std::to_string((i * 29 + 2) % 60000) + " ";
        s += "</indices></buffer></mesh>";
        const double t0 = Now(); ImportedModel m; std::string e; const bool ok = ReadIrr(s, m, e);
        CHECK(ok ? fx::Sane(m) : !e.empty());
        std::fprintf(stderr, "  irrmesh: 60,000 vertices read (ok=%d) in %.2f s\n", (int)ok, Now() - t0);
    }
}

int main() {
    RUN_SUITE("batchc_hostile");
    Md2();
    Md3();
    Irr();
    TEST_MAIN_RESULT();
}
