// Hostile input, budgets and mutation fuzz for the own Q3BSP reader. Needs the reader source, the engine's inflate (ImageDecode) and
// okay/Render/ModelData.hpp; in the ctest build it links the engine. All files are SYNTHETIC (tests/q3bsp_fixtures.hpp).
//  1. Small files that ask for a lot (several models naming one 1.5 million triangle face, thousands of models naming thousands of
//     faces, patch grids of 801 x 801 control points) must finish in a few seconds, be refused (or read) with a reason that names the
//     budget, and never hold more than kCapBytes at once (a counting operator new measures the live peak).
//  2. A deterministic mutation sweep over the seed maps (OKAY_READER_FUZZ_ITERS raises it): a read is either sane or a refusal that
//     leaves nothing behind. The same sweep goes through the .pk3 path on disk.
// Build by hand under sanitizers: add -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all to the flags.
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include "reader_hostile_common.hpp"
#include "q3bsp_fixtures.hpp"
#include "ZipLite.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <new>
#include <string>

using namespace okay;
namespace fs = std::filesystem;

#include "counting_alloc.hpp"   // the counting allocator (g_live, g_peak), shared by the hostile-input tests

static constexpr std::size_t kCapBytes = 1024u * 1024u * 1024u;   // on top of the file itself (the worst allowed case holds 4 million vertices and triangles)
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_UNDEFINED__)
static constexpr double kMaxSeconds = 60.0;
#else
static constexpr double kMaxSeconds = 10.0;
#endif
static double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
static bool HasNote(const ImportedModel& m, const char* what) { for (const std::string& n : m.notes) if (n.find(what) != std::string::npos) return true; return false; }

struct Run { bool ok; double secs; std::size_t peakMB; std::string err; ImportedModel m; };
static Run Go(const char* what, const std::string& bytes) {
    Run r;
    const std::size_t base = g_live.load();
    g_peak.store(base);
    const double t = Now();
    r.ok = ReadQ3bspBuffer(bytes, r.m, &r.err);
    r.secs = Now() - t;
    r.peakMB = (g_peak.load() - base) >> 20;
    std::fprintf(stderr, "  %s: %s in %.2f s, peak %zu MB (file %zu MB)%s%s\n", what, r.ok ? "read" : "refused", r.secs, r.peakMB, bytes.size() >> 20, r.ok ? "" : ": ", r.ok ? "" : r.err.c_str());
    CHECK(r.secs < kMaxSeconds);
    CHECK(g_peak.load() - base < kCapBytes);
    CHECK(r.ok ? hostile::Sane(r.m) : (r.m.parts.empty() && r.m.nodes.empty() && !r.err.empty()));
    return r;
}

// A map with one polygon face of `tris` triangles that all use the same three vertices, and `models` models that each name that face.
static q3::Map BigFace(std::size_t tris, int models) {
    q3::Map m;
    m.tex = {{"t"}};
    m.v = {q3::Vt(0, 0, 0, 0, 0), q3::Vt(1, 0, 0, 1, 0), q3::Vt(0, 1, 0, 0, 1)};
    for (std::size_t i = 0; i < tris; ++i) { m.mv.push_back(0); m.mv.push_back(2); m.mv.push_back(1); }
    q3::Face f; f.tex = 0; f.type = 1; f.vertex = 0; f.nVerts = 3; f.meshvert = 0; f.nMeshverts = (std::int32_t)(tris * 3);
    m.f.push_back(f);
    for (int i = 0; i < models; ++i) m.m.push_back({0, 1});
    return m;
}
// One patch face with a w x h control grid, flat.
static q3::Map OnePatch(int w, int h) {
    q3::Map m;
    m.tex = {{"t"}};
    m.v.reserve((std::size_t)w * (std::size_t)h);
    for (int r = 0; r < h; ++r) for (int c = 0; c < w; ++c) m.v.push_back(q3::Vt((float)c, (float)r, 0, 0, 0));
    q3::Face f; f.tex = 0; f.type = 2; f.vertex = 0; f.nVerts = w * h; f.size0 = w; f.size1 = h;
    m.f.push_back(f);
    m.m = {{0, 1}};
    return m;
}

static void Budgets() {
    {   // 1,500,000 triangles named by 2 models: 3,000,000 placed, read; by 3 models: 4,500,000, over the 4,000,000 triangle budget
        Run ok = Go("1.5 M triangles x 2 models", BigFace(1500000, 2).Build());
        CHECK(ok.ok && fx::TriCount(ok.m) == 3000000 && ok.m.nodes.size() == 2);
        Run bad = Go("1.5 M triangles x 3 models", BigFace(1500000, 3).Build());
        CHECK(!bad.ok && bad.err.find("triangles (the budget)") != std::string::npos);
    }
    {   // 4,001 models each naming all of 2,000 billboard faces: 8,002,000 face visits
        q3::Map m;
        m.tex = {{"t"}};
        m.v = {q3::Vt(0, 0, 0, 0, 0)};
        for (int i = 0; i < 2000; ++i) { q3::Face b; b.type = 4; b.vertex = 0; b.nVerts = 1; m.f.push_back(b); }
        for (int i = 0; i < 4001; ++i) m.m.push_back({0, 2000});
        Run r = Go("8,002,000 face visits", m.Build());
        CHECK(!r.ok && r.err.find("face visits") != std::string::npos);
        m.m.resize(3000);
        Run ok = Go("6,000,000 face visits of billboards", m.Build());
        CHECK(!ok.ok && ok.err.find("no drawable") != std::string::npos);   // every face is a billboard: nothing to draw, and it is said
    }
    {   // a patch grid of 801 x 801 control points: 160,000 patches, 11.5 million triangles
        Run r = Go("801 x 801 patch grid", OnePatch(801, 801).Build());
        CHECK(!r.ok && r.err.find("triangles (the budget") != std::string::npos);
        Run ok = Go("201 x 201 patch grid", OnePatch(201, 201).Build());
        CHECK(ok.ok && fx::TriCount(ok.m) == 100 * 100 * 72);
        Run big = Go("1025 x 1 patch grid", OnePatch(1025, 1).Build());   // fewer than 3 rows: a bad grid, not a crash
        CHECK(!big.ok && big.err.find("no drawable") != std::string::npos);
        Run huge = Go("size 2,000,001 x 1,000,001 claims", [] { q3::Map m = OnePatch(3, 3); m.f[0].size0 = 2000001; m.f[0].size1 = 1000001; return m.Build(); }());
        CHECK(!huge.ok);
    }
    {   // negative and extreme numbers in every field of the face record
        for (int field = 0; field < 8; ++field) {
            q3::Map m = q3::Basic();
            for (std::int32_t v : {-1, 2147483647, (std::int32_t)0x80000000}) {
                q3::Map a = m;
                q3::Face& f = a.f[(std::size_t)(field == 7 ? 3 : field % 2)];
                std::int32_t* fields[8] = {&f.tex, &f.type, &f.vertex, &f.nVerts, &f.meshvert, &f.nMeshverts, &f.size0, &f.size1};
                *fields[field] = v;
                Run r = Go("extreme face field", a.Build());
                (void)r;
            }
        }
    }
    {   // NaN, infinity and huge coordinates and normals are made finite
        q3::Map m = q3::Basic();
        const float inf = 1e38f * 10, nan = inf - inf;
        m.v[0].p[0] = nan; m.v[1].p[1] = inf; m.v[2].n[2] = nan; m.v[3].st[0] = inf; m.v[4].p[2] = 3e30f;
        Run r = Go("NaN and infinity in the vertexes", m.Build());
        CHECK(r.ok);
    }
}

static void Sweeps() {
    const std::string seeds[] = {q3::Basic().Build(), [] { q3::Map m = q3::Basic(); for (int k = 11; k < 20; ++k) m.v[(std::size_t)k].n[2] = -1; return m.Build(); }()};
    const long iters = hostile::Iters(20000);
    long reads = 0, refusals = 0;
    for (const std::string& seed : seeds) {
        for (std::size_t at = 0; at + 4 <= seed.size(); at += 4)   // every aligned 32-bit word at an extreme
            for (std::uint32_t v : {0xFFFFFFFFu, 0x7FFFFFFFu, 0x80000000u, 0u, 0x7F800000u, 0xFF800000u, 0x7FC00000u}) {
                std::string s = seed;
                for (int i = 0; i < 4; ++i) s[at + (std::size_t)i] = (char)((v >> (8 * i)) & 255);
                ImportedModel m; std::string err;
                const bool ok = ReadQ3bspBuffer(s, m, &err);
                CHECK(ok ? hostile::Sane(m) : (m.parts.empty() && m.nodes.empty() && !err.empty()));
                (ok ? reads : refusals)++;
            }
        for (long i = 0; i < iters; ++i) {
            std::string s = seed;
            const int edits = 1 + (int)(hostile::Rnd() % 4);
            for (int k = 0; k < edits; ++k) hostile::MutateBinary(s);
            ImportedModel m; std::string err;
            const bool ok = ReadQ3bspBuffer(s, m, &err);
            CHECK(ok ? hostile::Sane(m) : (m.parts.empty() && m.nodes.empty() && !err.empty()));
            (ok ? reads : refusals)++;
        }
    }
    std::fprintf(stderr, "  mutation sweep: %ld read, %ld refused\n", reads, refusals);
    // the .pk3 path: bytes mutated inside the archive (the zip reader checks sizes and the checksum)
    const fs::path dir = fs::temp_directory_path() / "okay_q3bsp_hostile";
    fs::create_directories(dir);
    const std::string zipSeed = q3::Pk3(q3::Basic().Build(), true, true);
    long zr = 0, zf = 0;
    for (long i = 0; i < std::min<long>(iters / 20, 2000); ++i) {
        std::string s = zipSeed;
        for (int k = 0; k < 1 + (int)(hostile::Rnd() % 3); ++k) hostile::MutateBinary(s);
        { std::ofstream f(dir / "m.pk3", std::ios::binary); f.write(s.data(), (std::streamsize)s.size()); }
        ImportedModel m; std::string err;
        const bool ok = ReadQ3bspFile((dir / "m.pk3").string(), m, &err);
        CHECK(ok ? hostile::Sane(m) : (m.parts.empty() && m.nodes.empty() && !err.empty()));
        (ok ? zr : zf)++;
    }
    std::fprintf(stderr, "  pk3 mutation sweep: %ld read, %ld refused\n", zr, zf);
    // a zip bomb: one entry that inflates to far more than the directory says
    {
        fx::ZipEntry e; e.name = "maps/b.bsp"; e.rawSet = true; e.deflate = true; e.raw = fx::DeflateRepeat('A', 40000); e.usize = 1000; e.crc = 0;
        { const std::string z = fx::MakeZip({e}); std::ofstream f(dir / "bomb.pk3", std::ios::binary); f.write(z.data(), (std::streamsize)z.size()); }
        ImportedModel m; std::string err;
        CHECK(!ReadQ3bspFile((dir / "bomb.pk3").string(), m, &err) && err.find("ZIP") != std::string::npos);
    }
    std::error_code ec; fs::remove_all(dir, ec);
}

// With OKAY_MODEL_CORPUS set (docs/content/model_corpus.md: the "test" folder of an Assimp 5.4.3 checkout), the one real map of the
// corpus (models-nonbsd/PK3/SGDTT3.pk3) is read whole, then its .bsp is taken out of the archive and fuzzed the same way.
static void RealMap() {
    const char* c = std::getenv("OKAY_MODEL_CORPUS");
    if (!c) return;
    const std::string path = std::string(c) + "/models-nonbsd/PK3/SGDTT3.pk3";
    const std::string bytes = hostile::Slurp(path);
    if (bytes.empty()) { std::fprintf(stderr, "  real map: %s not found, skipped\n", path.c_str()); return; }
    ImportedModel whole; std::string err;
    CHECK(ReadQ3bspFile(path, whole, &err) && hostile::Sane(whole));
    detail::zip::Archive zip;
    CHECK(zip.Open(bytes));
    std::string bsp;
    for (std::size_t i = 0; i < zip.entries.size(); ++i) {
        const std::string& n = zip.entries[i].name;
        if (n.size() > 9 && n.compare(0, 5, "maps/") == 0 && n.compare(n.size() - 4, 4, ".bsp") == 0) { CHECK(zip.Extract((int)i, bsp)); break; }
    }
    CHECK(!bsp.empty());
    if (bsp.empty()) return;
    long reads = 0, refusals = 0;
    const long iters = std::max(500L, hostile::Iters(20000) / 4);
    for (long i = 0; i < iters; ++i) {
        std::string s = bsp;
        const int edits = 1 + (int)(hostile::Rnd() % 4);
        for (int k = 0; k < edits; ++k) hostile::MutateBinary(s);
        ImportedModel m; std::string e;
        const bool ok = ReadQ3bspBuffer(s, m, &e);
        CHECK(ok ? hostile::Sane(m) : (m.parts.empty() && m.nodes.empty() && !e.empty()));
        (ok ? reads : refusals)++;
    }
    for (std::size_t cut = 0; cut < bsp.size(); cut += 1 + bsp.size() / 256) {   // prefixes
        ImportedModel m; std::string e;
        const bool ok = ReadQ3bspBuffer(bsp.substr(0, cut), m, &e);
        CHECK(ok ? hostile::Sane(m) : !e.empty());
    }
    std::fprintf(stderr, "  real map (%zu byte .bsp): %ld mutations, %ld read, %ld refused\n", bsp.size(), iters, reads, refusals);
}

int main() {
    RUN_SUITE("q3bsp_hostile");
    Budgets();
    Sweeps();
    RealMap();
    TEST_MAIN_RESULT();
}
