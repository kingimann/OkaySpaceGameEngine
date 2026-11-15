// Hostile input, mutation fuzz, worst-case memory and real files for the batch-E own readers: MDC, HMP, CSM. Needs only the
// reader sources and okay/Render/ModelData.hpp, so it builds on its own, e.g.
//   g++ -std=c++17 -O1 -g -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all -DOKAY_STANDALONE_TEST
//       -Iengine/include -Iengine/src/Core -Itests tests/test_batche_hostile.cpp engine/src/Render/{Mdc,Hmp,Csm}Reader.cpp -o hostile
// OKAY_BATCHE_FUZZ_ITERS=200000 for a long run (per reader). The fixtures are SYNTHETIC. With OKAY_MODEL_CORPUS set to the "test"
// directory of an Assimp 5.4.3 checkout (docs/model_corpus.md) the four real files of these formats are read and compared with
// numbers worked out independently (a small script over the raw bytes), and fuzzed.
//  Worst allowed file for each reader: a counting operator new measures the live peak and the time must stay small.
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include "batche_fixtures.hpp"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <new>
#include <string>
#include <vector>

using namespace okay;

// ---- a counting allocator: bytes live now and the peak since the last reset ------------------------------------------
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
static void CountedFree(void* q) noexcept {
    if (!q) return;
    void* raw = static_cast<char*>(q) - 16;
    g_live.fetch_sub(*static_cast<std::size_t*>(raw));
    std::free(raw);
}
void* operator new(std::size_t n) { return CountedAlloc(n); }
void* operator new[](std::size_t n) { return CountedAlloc(n); }
void operator delete(void* p) noexcept { CountedFree(p); }
void operator delete[](void* p) noexcept { CountedFree(p); }
void operator delete(void* p, std::size_t) noexcept { CountedFree(p); }
void operator delete[](void* p, std::size_t) noexcept { CountedFree(p); }

// The most memory the reader may hold at once, on top of the input it was given, for the worst file its budgets allow. The numbers
// are what was measured (printed on every run) with room for another allocator; a reader that keeps a second copy of the
// input or builds per-corner tables of a wrong size goes over.
static constexpr std::size_t kMdcCap = 256u * 1024u * 1024u, kHmpCap = 128u * 1024u * 1024u, kCsmCap = 96u * 1024u * 1024u;
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_UNDEFINED__)
static constexpr double kMaxSeconds = 20.0;   // a sanitizer build is several times slower; the limit for the normal build is below
#else
static constexpr double kMaxSeconds = 2.0;
#endif

static long Iters() { const char* e = std::getenv("OKAY_BATCHE_FUZZ_ITERS"); return e ? std::atol(e) : 3000; }
static double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
static bool HasNote(const ImportedModel& m, const char* what) { for (const std::string& n : m.notes) if (n.find(what) != std::string::npos) return true; return false; }

// A read must either give a sane model or fail with a message and leave nothing behind.
template <class F> static void Try(F&& read, const std::string& bytes) {
    ImportedModel m; std::string err;
    const bool ok = read(bytes, m, err);
    if (ok) { CHECK(fx::Sane(m)); }
    else { CHECK(!err.empty() && m.parts.empty() && m.nodes.empty() && m.clips.empty()); }
}
static std::string Mutate(const std::string& src, bool text) {
    std::string s = src;
    if (s.empty()) return s;
    const std::size_t p = (std::size_t)(fx::Rnd() % s.size());
    const int kind = (int)(fx::Rnd() % 7);
    switch (kind) {
    case 0: s[p] = (char)(fx::Rnd() & 255); break;
    case 1: s[p] ^= (char)(1u << (fx::Rnd() % 8)); break;
    case 2: s.erase(p, (std::size_t)(fx::Rnd() % 16) + 1); break;
    case 3: { std::string ins; for (int i = (int)(fx::Rnd() % 8) + 1; i > 0; --i) ins.push_back((char)(fx::Rnd() & 255)); s.insert(p, ins); break; }
    case 4:
        if (text) { const char* t[] = {"0", "-1", "99999999999", "1e308", "nan", "-0", "4294967295", "2147483648", " ", ",", "-", "e", ".", "inf", "0x7fffffff", "1e999", "$"}; s.replace(p, std::min<std::size_t>(3, s.size() - p), t[fx::Rnd() % 17]); }
        else { const std::uint32_t v = fx::Rnd() % 3 == 0 ? 0xFFFFFFFFu : fx::Rnd() % 3 == 0 ? 0x7FFFFFFFu : (std::uint32_t)(fx::Rnd() % 300); if (p + 4 <= s.size()) fx::Put32(s, p, v); }
        break;
    case 5: if (p + 2 <= s.size()) { const unsigned v = fx::Rnd() % 3 == 0 ? 0xFFFFu : (unsigned)(fx::Rnd() % 100); s[p] = (char)(v & 255); s[p + 1] = (char)(v >> 8); } break;
    default: s.resize(p); break;
    }
    return s;
}
template <class F> static void Sweep(const char* name, F&& read, const std::string& fixture, long iters, bool text, bool truncations = true) {
    const double t0 = Now();
    if (truncations) for (std::size_t n = 0; n < fixture.size(); ++n) Try(read, fixture.substr(0, n));
    for (long i = 0; i < iters; ++i) Try(read, Mutate(fixture, text));
    std::fprintf(stderr, "  %s: %s%ld mutations in %.1f s\n", name, truncations ? "every truncation + " : "", iters, Now() - t0);
}

static bool ReadMdc(const std::string& b, ImportedModel& m, std::string& e) { return ReadMdcBuffer(b, m, &e); }
static bool ReadHmp(const std::string& b, ImportedModel& m, std::string& e) { return ReadHmpBuffer(b, m, &e); }
static bool ReadCsm(const std::string& b, ImportedModel& m, std::string& e) { return ReadCsmBuffer(b, m, &e); }

static std::string Slurp(const std::string& p) { std::ifstream f(p, std::ios::binary); return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); }

// Peak bytes held at once while reading, and the time taken; the input is already allocated by the caller.
template <class F> static void Worst(const char* what, std::size_t cap, F&& f, bool expectOk) {
    const std::size_t base = g_live.load();
    g_peak.store(base);
    const double t = Now();
    const bool ok = f();
    const double s = Now() - t;
    const std::size_t extra = g_peak.load() - base;
    std::fprintf(stderr, "  %s: %s in %.2f s, peak %zu MB\n", what, ok ? "read" : "refused", s, extra >> 20);
    CHECK(ok == expectOk);
    CHECK(s < kMaxSeconds);
    CHECK(extra < cap);
}

static void MdcWorst() {
    // The largest file the budgets allow: one surface of 500,000 vertices and 500,000 triangles (about 14 MB). Every vertex is
    // used, positions and uvs vary, the packed normals vary, so welding gets no shortcut.
    const std::uint32_t nv = 500000, nt = 500000;
    std::string o;
    o += "IDPC"; fx::P32(o, 2); fx::PStr(o, "big", 64); fx::P32(o, 0);
    fx::P32(o, 1); fx::P32(o, 0); fx::P32(o, 1); fx::P32(o, 0);
    fx::P32(o, 112); fx::P32(o, 168); fx::P32(o, 168); fx::P32(o, 168);
    const std::size_t endAt = o.size(); fx::P32(o, 0);
    for (int k = 0; k < 10; ++k) fx::PF(o, 0.0f);
    fx::PStr(o, "f", 16);
    const std::uint32_t oTris = 124, oSh = oTris + 12 * nt, oSt = oSh + 68, oXn = oSt + 8 * nv, oXc = oXn + 8 * nv, oFb = oXc, oFc = oFb + 2, oEnd = oFc + 2;
    fx::P32(o, 7); fx::PStr(o, "s", 64); fx::P32(o, 0); fx::P32(o, 0); fx::P32(o, 1); fx::P32(o, 1); fx::P32(o, nv); fx::P32(o, nt);
    fx::P32(o, oTris); fx::P32(o, oSh); fx::P32(o, oSt); fx::P32(o, oXn); fx::P32(o, oXc); fx::P32(o, oFb); fx::P32(o, oFc); fx::P32(o, oEnd);
    for (std::uint32_t i = 0; i < nt; ++i) { fx::P32(o, i % nv); fx::P32(o, (i * 7u + 1u) % nv); fx::P32(o, (i * 13u + 5u) % nv); }
    fx::PStr(o, "textures/x", 64); fx::P32(o, 0);
    for (std::uint32_t i = 0; i < nv; ++i) { fx::PF(o, (float)(i % 1000) / 1000.0f); fx::PF(o, (float)(i / 1000) / 1000.0f); }
    for (std::uint32_t i = 0; i < nv; ++i) { fx::PS16(o, (int)(i * 31u % 20000u)); fx::PS16(o, (int)(i * 17u % 20000u)); fx::PS16(o, (int)(i * 7u % 20000u)); fx::PS16(o, (int)(i % 65536u)); }
    fx::PS16(o, 0); fx::PS16(o, -1);
    fx::Put32(o, endAt, (std::uint32_t)o.size());
    std::fprintf(stderr, "  mdc worst-case file: %zu bytes\n", o.size());
    ImportedModel m; std::string err;
    Worst("mdc 500k vertices / 500k triangles", kMdcCap, [&] { return ReadMdcBuffer(o, m, &err); }, true);
    CHECK(fx::Sane(m) && !m.parts.empty());
    // one triangle more than the budget: refused before any table is built
    fx::Put32(o, 168 + 88, nt + 1);   // the surface header is at 112 + 56 (one frame) = 168; numTris is 88 bytes into it
    ImportedModel m2; std::string err2;
    Worst("mdc 500k + 1 triangles", 8u * 1024u * 1024u, [&] { return ReadMdcBuffer(o, m2, &err2); }, false);
    CHECK(err2.find("triangle budget") != std::string::npos);
}
static void HmpWorst() {
    fx::HmpSpec sp; sp.nx = 1024; sp.ny = 1024;
    std::vector<std::uint16_t> hs((std::size_t)sp.nx * sp.ny);
    for (std::size_t i = 0; i < hs.size(); ++i) hs[i] = (std::uint16_t)((i * 2654435761u) >> 16);
    const std::string o = fx::BuildHmp(sp, &hs);
    std::fprintf(stderr, "  hmp worst-case file: %zu bytes\n", o.size());
    ImportedModel m; std::string err;
    Worst("hmp 1024 x 1024 grid", kHmpCap, [&] { return ReadHmpBuffer(o, m, &err); }, true);
    CHECK(fx::Sane(m) && fx::TriCount(m) == 2u * 1023u * 1023u);
    fx::HmpSpec sp2; sp2.nx = 1025; sp2.ny = 1024;   // one column more than the budget (1,049,600 points)
    std::vector<std::uint16_t> hs2((std::size_t)sp2.nx * sp2.ny, 7);
    const std::string o2 = fx::BuildHmp(sp2, &hs2);
    ImportedModel m2; std::string err2;
    Worst("hmp 1025 x 1024 grid", 8u * 1024u * 1024u, [&] { return ReadHmpBuffer(o2, m2, &err2); }, false);
    CHECK(err2.find("budget") != std::string::npos);
}
static void CsmWorst() {
    // 1000 markers x 2000 frames = 2,000,000 keys, exactly the budget
    std::string t = "$Rate 120\n$Order\n";
    for (int i = 0; i < 1000; ++i) t += "marker" + std::to_string(i) + " ";
    t += "\n$Points\n";
    for (int f = 1; f <= 2000; ++f) { t += std::to_string(f); for (int i = 0; i < 1000; ++i) t += " " + std::to_string((i * 7 + f) % 1000) + ".5 " + std::to_string(i) + " " + std::to_string(f % 50); t += "\n"; }
    std::fprintf(stderr, "  csm worst-case file: %zu bytes\n", t.size());
    ImportedModel m; std::string err;
    Worst("csm 1000 markers x 2000 frames", kCsmCap, [&] { return ReadCsmBuffer(t, m, &err); }, true);
    CHECK(fx::Sane(m) && m.clips[0].channels.size() == 1000 && m.clips[0].channels[0].position.size() == 2000);
    const std::size_t at = t.find("\n$Points\n") + 9;
    std::string t2 = t.substr(0, at); t2 += "2001"; for (int i = 0; i < 1000; ++i) t2 += " 0 0 0"; t2 += "\n" + t.substr(at);
    ImportedModel m2; std::string err2;
    std::string moved = t.substr(0, at);
    for (int f = 1; f <= 2001; ++f) { moved += std::to_string(f); for (int i = 0; i < 1000; ++i) moved += " 0 0 0"; moved += "\n"; }
    Worst("csm 2001 frames", 100u * 1024u * 1024u, [&] { return ReadCsmBuffer(moved, m2, &err2); }, false);
    CHECK(err2.find("key budget") != std::string::npos);
}

// ---- the real files (when OKAY_MODEL_CORPUS points at the test directory of an Assimp 5.4.3 checkout) -------------------
static void RealFiles() {
    const char* root = std::getenv("OKAY_MODEL_CORPUS");
    if (!root || !*root) { std::fprintf(stderr, "  real files: OKAY_MODEL_CORPUS not set, skipped\n"); return; }
    const std::string r = std::string(root) + "/";
    {   // spider.mdc: 19 surfaces, 922 vertices and 1368 triangles in the file, 56 of them with zero area
        const std::string b = Slurp(r + "models/MDC/spider.mdc");
        if (b.empty()) std::fprintf(stderr, "  real files: spider.mdc not found, skipped\n");
        else {
            ImportedModel m; std::string err;
            CHECK(ReadMdcBuffer(b, m, &err) && fx::Sane(m));
            CHECK(m.parts.size() == 19 && m.materials.size() == 19 && fx::TriCount(m) == 1368 - 56 && m.nodes.size() == 1);
            CHECK(m.materials[0].texture == "Augentex" && m.parts[0].mesh.name == "Duplicate05");
            std::size_t v = 0; for (const ImportedPart& p : m.parts) v += p.mesh.vertices.size();
            CHECK(v > 0 && v <= 922 * 3);
            // the first vertex of the first surface: (-3805, 1100, 1160) / 64
            bool found = false;
            for (const Vec3& p : m.parts[0].mesh.vertices) if (fx::Near(p.x, -3805.0f / 64) && fx::Near(p.y, 1100.0f / 64) && fx::Near(p.z, 1160.0f / 64)) found = true;
            CHECK(found);
            // the stored normals agree with the winding: counter clockwise triangles point the way their vertex normals do
            std::size_t agree = 0, total = 0;
            for (const ImportedPart& p : m.parts)
                for (std::size_t t = 0; t + 2 < p.mesh.triangles.size(); t += 3) {
                    const Vec3 a = p.mesh.vertices[(std::size_t)p.mesh.triangles[t]], bb = p.mesh.vertices[(std::size_t)p.mesh.triangles[t + 1]], c = p.mesh.vertices[(std::size_t)p.mesh.triangles[t + 2]];
                    const Vec3 fn = Vec3::Cross(bb - a, c - a);
                    for (int k = 0; k < 3; ++k) { const Vec3 n = p.mesh.normals[(std::size_t)p.mesh.triangles[t + (std::size_t)k]]; ++total; if (fn.x * n.x + fn.y * n.y + fn.z * n.z > 0) ++agree; }
                }
            std::fprintf(stderr, "  spider.mdc: %zu surfaces, %zu triangles, normals agree with the winding for %.1f%% of corners\n", m.parts.size(), fx::TriCount(m), 100.0 * (double)agree / (double)total);
            CHECK(total > 0 && (double)agree / (double)total > 0.95);
            Sweep("real spider.mdc", ReadMdc, b, 150, false, false);
        }
    }
    for (const char* name : {"models/HMP/terrain.hmp", "models-nonbsd/HMP/terrain_withtexture.hmp"}) {
        const std::string b = Slurp(r + name);
        if (b.empty()) { std::fprintf(stderr, "  real files: %s not found, skipped\n", name); continue; }
        ImportedModel m; std::string err;
        CHECK(ReadHmpBuffer(b, m, &err) && fx::Sane(m));
        CHECK(m.parts.size() == 1 && m.parts[0].mesh.vertices.size() == 1089 && fx::TriCount(m) == 2048);
        // 33 x 33 points, 5 apart from -80, heights z = h * 0.00079346913844347 - 34 with h from 0 to 65535: -34 to 18
        float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
        for (const Vec3& v : m.parts[0].mesh.vertices) { lo[0] = std::fmin(lo[0], v.x); hi[0] = std::fmax(hi[0], v.x); lo[1] = std::fmin(lo[1], v.y); hi[1] = std::fmax(hi[1], v.y); lo[2] = std::fmin(lo[2], v.z); hi[2] = std::fmax(hi[2], v.z); }
        CHECK(fx::Near(lo[0], -80, 1e-3f) && fx::Near(hi[0], 80, 1e-3f) && fx::Near(lo[1], -80, 1e-3f) && fx::Near(hi[1], 80, 1e-3f) && fx::Near(lo[2], -34, 1e-3f) && fx::Near(hi[2], 18, 1e-2f));
        const bool skin = std::strstr(name, "withtexture") != nullptr;
        CHECK(HasNote(m, "skin") == skin);
        Sweep(name, ReadHmp, b, skin ? 100 : 2000, false, !skin);
    }
    {   // ThomasFechten.csm: 43 markers, 2439 frames at 120 per second
        const std::string b = Slurp(r + "models/CSM/ThomasFechten.csm");
        if (b.empty()) std::fprintf(stderr, "  real files: ThomasFechten.csm not found, skipped\n");
        else {
            ImportedModel m; std::string err;
            CHECK(ReadCsmBuffer(b, m, &err) && fx::Sane(m));
            CHECK(m.nodes.size() == 44 && m.clips.size() == 1 && m.clips[0].channels.size() == 43 && m.nodes[1].name == "LFHD" && m.nodes[43].name == "RMT5");
            std::size_t keys = 0; for (const ImportedChannel& c : m.clips[0].channels) keys += c.position.size();
            CHECK(keys == 43u * 2439u && !HasNote(m, "skipped") && !HasNote(m, "do not match"));
            const ImportedKeyV3& k0 = m.clips[0].channels[0].position[0];
            CHECK(fx::Near(k0.value.x, 26.327553f) && fx::Near(k0.value.y, 157.341293f, 1e-3f) && fx::Near(k0.value.z, 1656.915527f, 1e-2f) && k0.time == 0.0f);
            CHECK(fx::Near(m.clips[0].channels[0].position[2438].time, 2438.0f / 120.0f, 1e-3f));
            // a head-high marker really is up: the head markers' z (up in the file) is the largest of all markers
            Sweep("real ThomasFechten.csm head (200 lines)", ReadCsm, b.substr(0, b.find('\n', b.find("$Points") + 200 * 400)), 150, true, false);
        }
    }
}

int main() {
    RUN_SUITE("batche_hostile");
    const long it = Iters();
    CHECK(true);
    Sweep("mdc", ReadMdc, fx::BuildMdc(), it, false);
    { fx::MdcSpec sp; sp.compFirst = true; sp.secondSurface = true; Sweep("mdc comp first, 2 surfaces", ReadMdc, fx::BuildMdc(sp), it, false); }
    Sweep("hmp", ReadHmp, fx::BuildHmp(), it, false);
    { fx::HmpSpec sp; sp.skin = true; sp.frames = 2; Sweep("hmp skin", ReadHmp, fx::BuildHmp(sp), it, false); }
    Sweep("csm", ReadCsm, fx::BuildCsm(), it, true);
    Sweep("csm crlf", ReadCsm, fx::BuildCsm("\r\n"), it / 2, true);
    MdcWorst();
    HmpWorst();
    CsmWorst();
    RealFiles();
    TEST_MAIN_RESULT();
}
