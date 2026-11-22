// One uniform hostile-input sweep over the six own readers that came last: glTF 1.0 (JSON and binary), OpenGEX, Quake 1 and
// Half-Life MDL, Quake III BSP, IQM, COB (ASCII and binary). Each reader also has its own deeper test (test_gltf1_hostile,
// test_e4_hostile, test_q3bsp_hostile, test_iqm_reader, test_cob_reader); this file runs the SAME battery over all of them so none is
// left out. Built from the reader sources alone, e.g.
//   g++ -std=c++17 -O1 -g1 -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all -DOKAY_STANDALONE_TEST
//       -Iengine/include -Iengine/src/Render -Iengine/src/Core -Itests tests/test_own_readers_sweep.cpp engine/src/Render/{Gltf1,Ogex,Mdl,Q3Bsp,Iqm,Cob}Reader.cpp
// Per seed file: every truncation; every aligned 32-bit word (and the words one and two bytes off) set to 13 lying values (0, 1, 2^31-1,
// 2^31, 2^32-1, 2^32-2, 65536, the file size, size+1, size-4, a quiet NaN, +infinity, -1.0f); every single bit flip (small files); block
// copies that make tables overlap or point backwards; empty, header-only and magic-only input; random garbage with and without the
// right magic; and, for the text formats, deep nesting, huge counts and non-finite numbers. Every read must: finish inside the time
// limit, keep a counting operator new under kCapBytes, and either succeed with a model that passes hostile::Sane (finite numbers,
// in-range vertex, material, node and bone indices, parents first) and stays under the output caps, or fail with a non-empty reason
// and an empty model. SYNTHETIC input only. OKAY_READER_FUZZ_ITERS scales the random part.
#include "test_framework.hpp"
#include "reader_hostile_common.hpp"
#include "model_fixture_util.hpp"
#include "e3_fixtures.hpp"
#include "e4_fixtures.hpp"
#include "q3bsp_fixtures.hpp"
#include "gltf1_test_build.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <new>
#include <string>

using namespace okay;

static std::atomic<std::size_t> g_live{0}, g_peak{0};
static void* CountedAlloc(std::size_t n) {
    void* raw = std::malloc(n + 16);
    if (!raw) throw std::bad_alloc();
    *static_cast<std::size_t*>(raw) = n;
    const std::size_t now = g_live.fetch_add(n) + n;
    std::size_t p = g_peak.load();
    while (now > p && !g_peak.compare_exchange_weak(p, now)) {}
    return static_cast<char*>(raw) + 16;
}
static void CountedFree(void* q) noexcept { if (!q) return; void* raw = static_cast<char*>(q) - 16; g_live.fetch_sub(*static_cast<std::size_t*>(raw)); std::free(raw); }
void* operator new(std::size_t n) { return CountedAlloc(n); }
void* operator new[](std::size_t n) { return CountedAlloc(n); }
// std::stable_sort and friends take their scratch space from the nothrow forms; without these the counted delete would free a plain block
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { try { return CountedAlloc(n); } catch (...) { return nullptr; } }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { try { return CountedAlloc(n); } catch (...) { return nullptr; } }
void operator delete(void* p, const std::nothrow_t&) noexcept { CountedFree(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { CountedFree(p); }
void operator delete(void* p) noexcept { CountedFree(p); }
void operator delete[](void* p) noexcept { CountedFree(p); }
void operator delete(void* p, std::size_t) noexcept { CountedFree(p); }
void operator delete[](void* p, std::size_t) noexcept { CountedFree(p); }

static constexpr std::size_t kCapBytes = 256u * 1024u * 1024u;   // live peak above the start of one read; these fixtures are a few KB
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_UNDEFINED__)
static constexpr double kMaxSeconds = 20.0;
#else
static constexpr double kMaxSeconds = 5.0;
#endif
static constexpr std::size_t kMaxVerts = 4000000, kMaxTris = 4000000, kMaxNodes = 1000000, kMaxBones = 1000000, kMaxParts = 100000;

struct Reader { const char* name; std::function<bool(const std::string&, ImportedModel&, std::string*)> read; };
static double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

static long g_reads = 0, g_refused = 0, g_worstMB = 0;
static double g_worstSec = 0;

// One read with every assertion. `what` names the case for the failure line.
static bool One(const Reader& r, const std::string& bytes, const char* what) {
    ImportedModel m; std::string err;
    const std::size_t base = g_live.load();
    g_peak.store(base);
    const double t = Now();
    const bool ok = r.read(bytes, m, &err);
    const double secs = Now() - t;
    const std::size_t peak = g_peak.load() - base;
    ++g_reads;
    if ((long)(peak >> 20) > g_worstMB) g_worstMB = (long)(peak >> 20);
    if (secs > g_worstSec) g_worstSec = secs;
    if (secs >= kMaxSeconds || peak >= kCapBytes) std::fprintf(stderr, "  %s / %s: %.2f s, peak %zu MB\n", r.name, what, secs, peak >> 20);
    CHECK(secs < kMaxSeconds);
    CHECK(peak < kCapBytes);
    if (ok) {
        std::size_t v = 0, tr = 0, b = 0;
        for (const ImportedPart& p : m.parts) { v += p.mesh.vertices.size(); tr += p.mesh.triangles.size() / 3; b += p.bones.size(); }
        const bool sane = hostile::Sane(m);
        if (!sane) std::fprintf(stderr, "  %s / %s: read, but the model is not sane\n", r.name, what);
        CHECK(sane);
        CHECK(v <= kMaxVerts && tr <= kMaxTris && m.nodes.size() <= kMaxNodes && b <= kMaxBones && m.parts.size() <= kMaxParts);
        return true;
    }
    ++g_refused;
    const bool clean = !err.empty() && m.parts.empty() && m.nodes.empty();
    if (!clean) std::fprintf(stderr, "  %s / %s: refused with %s, %zu parts, %zu nodes\n", r.name, what, err.empty() ? "no reason" : "a reason", m.parts.size(), m.nodes.size());
    CHECK(clean);
    return false;
}

static void Put32(std::string& s, std::size_t off, std::uint32_t v) { for (int i = 0; i < 4; ++i) s[off + (std::size_t)i] = (char)((v >> (8 * i)) & 255); }

static void SweepBinary(const Reader& r, const std::string& f) {
    CHECK(One(r, f, "seed"));   // the seed itself must read
    const std::uint32_t sz = (std::uint32_t)f.size();
    const std::uint32_t lies[] = {0u, 1u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu, 0xFFFFFFFEu, 0x10000u, sz, sz + 1, sz - 4, 0x7FC00000u, 0x7F800000u, 0xBF800000u};
    for (std::size_t n = 0; n < f.size(); ++n) One(r, f.substr(0, n), "truncation");
    for (std::size_t at = 0; at + 4 <= f.size(); at += 1) {
        if (at % 4 > 2) continue;   // aligned, +1 and +2 off
        for (std::uint32_t v : lies) { std::string g = f; Put32(g, at, v); One(r, g, "word stomp"); }
    }
    if (f.size() <= 4096) for (std::size_t at = 0; at < f.size(); ++at) for (int bit = 0; bit < 8; ++bit) { std::string g = f; g[at] = (char)(g[at] ^ (1 << bit)); One(r, g, "bit flip"); }
    // block copies: a table over another table, a header over the body, a body over the header (overlap and backwards offsets)
    hostile::State() = 0x5151515151ull;
    const long iters = hostile::Iters(1500);
    for (long i = 0; i < iters; ++i) {
        std::string g = f;
        const std::size_t len = 4 + hostile::Rnd() % 64, from = hostile::Rnd() % g.size(), to = hostile::Rnd() % g.size();
        const std::string piece = g.substr(from, len);
        const std::size_t n = std::min(piece.size(), g.size() - to);
        g.replace(to, n, piece.substr(0, n));
        if (i % 3 == 0) hostile::MutateBinary(g);
        One(r, g, "block copy");
    }
    // several lying words at once
    for (long i = 0; i < iters; ++i) {
        std::string g = f;
        const int k = 2 + (int)(hostile::Rnd() % 4);
        for (int j = 0; j < k; ++j) { const std::size_t at = (hostile::Rnd() % (g.size() / 4)) * 4; Put32(g, at, lies[hostile::Rnd() % (sizeof lies / sizeof lies[0])]); }
        One(r, g, "multi stomp");
    }
}

static void SweepText(const Reader& r, const std::string& f, const std::string& alphabet) {
    CHECK(One(r, f, "seed"));
    for (std::size_t n = 0; n < f.size(); n += 1 + f.size() / 600) One(r, f.substr(0, n), "truncation");
    hostile::State() = 0x7777ull;
    const long iters = hostile::Iters(1500);
    for (long i = 0; i < iters; ++i) {
        std::string g = f;
        const int edits = 1 + (int)(hostile::Rnd() % 4);
        for (int k = 0; k < edits; ++k) hostile::MutateText(g, alphabet);
        One(r, g, "text mutation");
    }
    // numbers swapped for lying ones, one digit run at a time
    const char* nums[] = {"-1", "0", "2147483647", "2147483648", "4294967295", "4294967296", "1e999", "-1e999", "nan", "inf", "1e-999", "99999999999999999999"};
    std::size_t done = 0;
    for (std::size_t p = 0; p < f.size() && done < 400; ++p) {
        if (!std::isdigit((unsigned char)f[p]) || (p && (std::isalnum((unsigned char)f[p - 1]) || f[p - 1] == '_'))) continue;
        std::size_t q = p; while (q < f.size() && (std::isdigit((unsigned char)f[q]) || f[q] == '.')) ++q;
        for (const char* n : nums) { std::string g = f; g.replace(p, q - p, n); One(r, g, "lying number"); }
        p = q; ++done;
    }
}

static void Garbage(const Reader& r, const std::string& magic, std::size_t headerLen) {
    One(r, std::string(), "empty");
    One(r, magic, "magic only");
    One(r, std::string(headerLen, '\0'), "zero header");
    One(r, std::string(headerLen, '\xff'), "0xFF header");
    One(r, magic + std::string(headerLen, '\xff'), "magic then 0xFF");
    One(r, magic + std::string(1 << 16, '\0'), "magic then 64 KB of zero");
    hostile::State() = 0xABCDEFull;
    const long iters = hostile::Iters(1500) / 3;
    for (long i = 0; i < iters; ++i) {
        std::string g((std::size_t)(hostile::Rnd() % 4096), '\0');
        for (char& c : g) c = (char)(hostile::Rnd() & 255);
        One(r, g, "garbage");
        std::string h = magic + g;
        One(r, h, "magic and garbage");
        if (h.size() > magic.size() + 32) { for (int k = 0; k < 8; ++k) Put32(h, magic.size() + (std::size_t)k * 4, (std::uint32_t)(hostile::Rnd() % 3 ? hostile::Rnd() % 64 : 0xFFFFFFFFu)); One(r, h, "magic, small counts, garbage"); }
    }
}

static void Nesting(const Reader& r, const std::string& open, const std::string& close, const std::string& prefix, const std::string& suffix, const char* what) {
    for (std::size_t depth : {100u, 10000u, 1000000u}) {
        std::string g = prefix;
        for (std::size_t i = 0; i < depth; ++i) g += open;
        for (std::size_t i = 0; i < depth; ++i) g += close;
        g += suffix;
        One(r, g, what);
        std::string h = prefix;
        for (std::size_t i = 0; i < depth; ++i) h += open;   // never closed
        One(r, h, what);
    }
}

int main() {
    RUN_SUITE("own_readers_sweep");
    const Reader gltf{"glTF 1.0", [](const std::string& b, ImportedModel& m, std::string* e) { return ReadGltf1Buffer(b, "", m, e); }};
    const Reader ogex{"OpenGEX", [](const std::string& b, ImportedModel& m, std::string* e) { return ReadOgexBuffer(b, "", m, e); }};
    const Reader mdl{"MDL", [](const std::string& b, ImportedModel& m, std::string* e) { return ReadMdlBuffer(b, "", m, e); }};
    const Reader q3{"Q3BSP", [](const std::string& b, ImportedModel& m, std::string* e) { return ReadQ3bspBuffer(b, m, e); }};
    const Reader iqm{"IQM", [](const std::string& b, ImportedModel& m, std::string* e) { return ReadIqmBuffer(b, "", m, e); }};
    const Reader cob{"COB", [](const std::string& b, ImportedModel& m, std::string* e) { return ReadCobBuffer(b, "", m, e); }};

    std::fprintf(stderr, "binary glTF\n");   SweepBinary(gltf, gltf1build::QuadBinary());
    std::fprintf(stderr, "glTF JSON\n");     SweepText(gltf, gltf1build::QuadJson(), "{}[]\":,0123456789.-eE tfnul\\");
    std::fprintf(stderr, "OpenGEX\n");       SweepText(ogex, e4::OgexQuad(), "{}()[]=,;$%0123456789.-eExf \n\"ab");
    SweepText(ogex, e4::OgexAnim(), "{}()[]=,;$%0123456789.-eExf \n\"ab");
    SweepText(ogex, e4::OgexSkin(), "{}()[]=,;$%0123456789.-eExf \n\"ab");
    std::fprintf(stderr, "Quake MDL\n");     SweepBinary(mdl, e4::QuakeMdl()); SweepBinary(mdl, e4::QuakeMdl(nullptr, true)); SweepBinary(mdl, e4::QuakePyramidMdl());
    std::fprintf(stderr, "Half-Life MDL\n"); SweepBinary(mdl, e4::HalfLifeMdl());
    std::fprintf(stderr, "Q3BSP\n");         SweepBinary(q3, q3::Basic().Build());
    std::fprintf(stderr, "IQM\n");           SweepBinary(iqm, fx::BuildIqm());
    std::fprintf(stderr, "COB binary\n");    SweepBinary(cob, fx::CobBinary());
    std::fprintf(stderr, "COB ASCII\n");     SweepText(cob, fx::CobAscii(), "0123456789.-:\n \"ABCabcxyz");

    std::fprintf(stderr, "garbage, empty and header-only\n");
    Garbage(gltf, "glTF", 20); Garbage(mdl, "IDPO", 84); Garbage(mdl, "IDST", 244); Garbage(q3, "IBSP", 144);
    Garbage(iqm, std::string("INTERQUAKEMODEL\0", 16), 124); Garbage(cob, "Caligari ", 64); Garbage(cob, "Caligari V00.01BLH", 64); Garbage(ogex, "Metric", 16);

    std::fprintf(stderr, "deep nesting\n");
    Nesting(ogex, "Node {", "}", "", "", "ogex nested nodes");
    Nesting(ogex, "Node ", "", "", "", "ogex open nodes");
    Nesting(gltf, "{\"a\":", "}", "{\"asset\":{\"version\":\"1.0\"},\"x\":", "}", "gltf nested objects");
    Nesting(gltf, "[", "]", "{\"asset\":{\"version\":\"1.0\"},\"x\":", "}", "gltf nested arrays");
    {   // a node tree that is a long ring, and a node that is its own child (glTF 1.0 children name each other)
        std::string j = "{\"asset\":{\"version\":\"1.0\"},\"scene\":\"s\",\"scenes\":{\"s\":{\"nodes\":[\"n0\"]}},\"nodes\":{";
        for (int i = 0; i < 20000; ++i) { if (i) j += ","; j += "\"n" + std::to_string(i) + "\":{\"children\":[\"n" + std::to_string((i + 1) % 20000) + "\"]}"; }
        j += "}}";
        One(gltf, j, "glTF node ring of 20000");
        j = "{\"asset\":{\"version\":\"1.0\"},\"scene\":\"s\",\"scenes\":{\"s\":{\"nodes\":[\"n0\"]}},\"nodes\":{\"n0\":{\"children\":[\"n0\",\"n0\",\"n0\"]}}}";
        One(gltf, j, "glTF node that is its own child");
    }
    {   // OpenGEX non-finite and huge values in a real structure
        const std::string s = e4::OgexQuad();
        for (const char* bad : {"nan", "inf", "-inf", "1e999", "-1e999", "1e-999", "0x7fffffff", "99999999999999999999", "-1"}) {
            std::string g = s; const std::size_t p = g.find("1.0"); if (p != std::string::npos) { g.replace(p, 3, bad); One(ogex, g, "ogex lying value"); }
        }
    }
    {   // IQM: an animation framerate that is positive but so small that frame / rate is infinity (bug found by the word stomp: key times must be finite)
        std::string f = fx::BuildIqm();
        const std::uint32_t oa = fx::IqmH(f, fx::kOfsAnims);
        for (std::uint32_t bits : {1u, 0x00800000u, 0x2F800000u /* 2^-32 */}) { std::string g = f; Put32(g, oa + 12, bits); CHECK(One(iqm, g, "iqm tiny framerate")); }
    }
    std::fprintf(stderr, "  %ld reads, %ld refused, worst %.2f s, worst peak %ld MB\n", g_reads, g_refused, g_worstSec, g_worstMB);
    CHECK(g_refused > 1000 && g_reads - g_refused > 100);   // the sweep really exercised both outcomes
    TEST_MAIN_RESULT();
}
