// Budgets, memory and where a scene may point, for the own PMX and LWS readers. A counting operator new measures the live peak:
//  1. small hostile files that claim gigabytes (counts of 2,000,000 vertices, 12,000,000 face indices, 100,000 bones, ...) are refused
//     at once, with a reason that names the budget, and never hold more than kCapBytes;
//  2. files that are within the file's own size but over the output budget (more than 4,000,000 triangles, bones x parts, envelope keys,
//     items, object files, one object placed so often that the scene would hold millions of triangles) are refused the same way;
//  3. a real, large, legal file is read within the cap;
//  4. the LWS reader asks its loader only for files inside the scene's folder, on disk too (a symbolic link that leads out is refused).
// All fixtures are SYNTHETIC. Builds on its own with the two reader sources (-DOKAY_STANDALONE_TEST), also under the sanitizers.
#include "test_framework.hpp"
#include "pmxlws_fixtures.hpp"
#include "okay/Render/ModelData.hpp"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <new>
#include <string>
#include <vector>

using namespace okay;
namespace fs = std::filesystem;

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
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { try { return CountedAlloc(n); } catch (...) { return nullptr; } }   // std::stable_sort asks for its buffer this way
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { try { return CountedAlloc(n); } catch (...) { return nullptr; } }
void operator delete(void* p) noexcept { CountedFree(p); }
void operator delete[](void* p) noexcept { CountedFree(p); }
void operator delete(void* p, std::size_t) noexcept { CountedFree(p); }
void operator delete[](void* p, std::size_t) noexcept { CountedFree(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { CountedFree(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { CountedFree(p); }

// The most memory any case below may add at once (the budgets are 4,000,000 triangles / vertices per file; a refusal comes before much more).
static constexpr std::size_t kCapBytes = 640u * 1024u * 1024u;
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_UNDEFINED__)
static constexpr double kMaxSeconds = 20.0;   // a sanitizer build is several times slower
#else
static constexpr double kMaxSeconds = 3.0;
#endif

static double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
static bool HasNote(const ImportedModel& m, const char* what) { for (const std::string& n : m.notes) if (n.find(what) != std::string::npos) return true; return false; }
static void Put32(std::string& s, std::size_t off, std::uint32_t v) { for (int i = 0; i < 4; ++i) s[off + (std::size_t)i] = (char)((v >> (8 * i)) & 255); }

struct Run { bool ok; double secs; std::size_t peakMB; };
template <class F> static Run Measure(const char* what, double limit, F&& f) {
    const std::size_t base = g_live.load();
    g_peak.store(base);
    const double t = Now();
    const bool ok = f();
    const double s = Now() - t;
    const std::size_t extra = g_peak.load() - base;
    std::fprintf(stderr, "  %s: %s in %.2f s, peak %zu MB\n", what, ok ? "read" : "refused", s, extra >> 20);
    CHECK(s < limit);
    CHECK(extra < kCapBytes);
    return Run{ok, s, extra >> 20};
}

static void PmxHostile() {
    fx::PmxMeta meta;
    const std::string base = fx::BuildPmx(fx::PmxOpts(), &meta);
    struct Case { const char* name; std::size_t off; std::uint32_t v; const char* why; };
    const Case cases[] = {
        {"2,000,000 vertices claimed", meta.vertCount, 2000000u, "more than the rest of the file"},
        {"2,000,001 vertices claimed", meta.vertCount, 2000001u, "(budget)"},
        {"0xFFFFFFFF vertices", meta.vertCount, 0xFFFFFFFFu, "vertex count"},
        {"12,000,000 face indices claimed", meta.faceCount, 12000000u, "more than the rest of the file"},
        {"12,000,003 face indices claimed", meta.faceCount, 12000003u, "(budget)"},
        {"100,000 bones claimed", meta.boneCount, 100000u, "more than the rest of the file"},
        {"100,000 morphs claimed", meta.morphCount, 100000u, "more than the rest of the file"},
        {"100,000 materials claimed", meta.matCount, 100000u, "more than the rest of the file"},
        {"100,000 textures claimed", meta.texCount, 100000u, "more than the rest of the file"},
    };
    for (const Case& c : cases) {
        std::string t = base;
        Put32(t, c.off, c.v);
        ImportedModel m; std::string err;
        const Run r = Measure(c.name, kMaxSeconds, [&] { return ReadPmxBuffer(t, "", m, &err); });
        CHECK(!r.ok && err.find(c.why) != std::string::npos && m.parts.empty());
        CHECK(r.peakMB < 8);   // a file of a few hundred bytes must not make the reader reserve anything big
    }
    {   // a big legal file: 300,000 vertices, 500,000 triangles
        const std::string s = fx::BuildPmxGrid(300000, 500000);
        std::fprintf(stderr, "  PMX grid file: %zu bytes\n", s.size());
        ImportedModel m; std::string err;
        const Run r = Measure("300k vertices / 500k triangles", kMaxSeconds * 4, [&] { return ReadPmxBuffer(s, "", m, &err); });
        CHECK(r.ok && fx::TriCount(m) == 500000 && fx::Sane(m));
    }
    {   // exactly 4,000,000 triangles (the largest legal face list, 48 MB over 4 vertices) is read within the cap
        const std::string s = fx::BuildPmxGrid(4, 4000000);
        ImportedModel m; std::string err;
        const Run r = Measure("4,000,000 triangles", 60, [&] { return ReadPmxBuffer(s, "", m, &err); });
        CHECK(r.ok && fx::TriCount(m) == 4000000);
    }
    {   // more than 4,000,000 triangles: the face list is over its budget of 12,000,000 indices
        const std::string s = fx::BuildPmxGrid(4, 4000001);
        std::fprintf(stderr, "  PMX over-budget triangles file: %zu bytes\n", s.size());
        ImportedModel m; std::string err;
        const Run r = Measure("4,000,001 triangles", 60, [&] { return ReadPmxBuffer(s, "", m, &err); });
        CHECK(!r.ok && err.find("(budget)") != std::string::npos);
    }
    {   // 2000 bones in 2500 parts: every part carries the whole bone list (5,000,000 entries)
        const std::string s = fx::BuildPmxGrid(10, 2500, 2500, 2000);
        ImportedModel m; std::string err;
        const Run r = Measure("2000 bones x 2500 parts", kMaxSeconds, [&] { return ReadPmxBuffer(s, "", m, &err); });
        CHECK(!r.ok && err.find("bone entries") != std::string::npos && err.find("(budget)") != std::string::npos);
        const std::string ok = fx::BuildPmxGrid(10, 100, 100, 100);   // 100 bones x 100 parts is fine
        ImportedModel m2; std::string e2;
        CHECK(ReadPmxBuffer(ok, "", m2, &e2) && m2.parts.size() == 100 && m2.parts[0].bones.size() == 100);
    }
    {   // one material of 300,000 triangles, then 99,000 materials of one triangle: 99,001 parts, each with its own weld map
        const std::string s = fx::BuildPmxManyMaterials(400000, 300000, 99000);
        ImportedModel m; std::string err;
        const Run r = Measure("1 big + 99,000 small materials", kMaxSeconds * 3, [&] { return ReadPmxBuffer(s, "", m, &err); });
        CHECK(r.ok && m.parts.size() == 99001 && fx::TriCount(m) == 399000);
    }
    {   // a very long text: 1 MiB + 1 is over the limit
        std::string t = base;
        Put32(t, 17, (1u << 20) + 1);
        ImportedModel m; std::string err;
        CHECK(!ReadPmxBuffer(t, "", m, &err) && err.find("text of") != std::string::npos);
    }
}

static std::string Nulls(int n) { std::string s = "LWSC\n3\n"; for (int i = 0; i < n; ++i) s += "AddNullObject n\n"; return s; }

static void LwsHostile() {
    {   // 4,000,001 envelope keys
        std::string s = "LWSC\n3\nAddNullObject A\nChannel 0\n{ Envelope\n 1\n";
        s.reserve(50u * 1024u * 1024u);
        for (int i = 0; i < 4000001; ++i) s += "Key 1 0 3\n";
        s += "}\n";
        std::fprintf(stderr, "  LWS key file: %zu bytes\n", s.size());
        ImportedModel m; std::string err;
        const Run r = Measure("4,000,001 keys", 60, [&] { return ReadLwsBuffer(s, "", m, &err); });
        CHECK(!r.ok && err.find("(budget)") != std::string::npos && m.nodes.empty());
    }
    {   // a key count of 0xFFFFFFFF: refused before one key is read
        ImportedModel m; std::string err;
        const Run r = Measure("4294967295 keys declared", kMaxSeconds, [&] { return ReadLwsBuffer("LWSC\n3\nAddNullObject A\nChannel 0\n{ Envelope\n 4294967295\n Key 1 0 3\n}\n", "", m, &err); });
        CHECK(!r.ok && err.find("(budget)") != std::string::npos);
    }
    {   // 100,001 items
        const std::string s = Nulls(100001);
        ImportedModel m; std::string err;
        const Run r = Measure("100,001 items", kMaxSeconds, [&] { return ReadLwsBuffer(s, "", m, &err); });
        CHECK(!r.ok && err.find("(budget)") != std::string::npos);
        const std::string ok = Nulls(100000);
        ImportedModel m2; std::string e2;
        const Run r2 = Measure("100,000 items", kMaxSeconds * 4, [&] { return ReadLwsBuffer(ok, "", m2, &e2); });
        CHECK(r2.ok && m2.nodes.size() == 100001 && fx::Sane(m2));
    }
    {   // 257 different object files
        std::string s = "LWSC\n3\n";
        for (int i = 0; i < 257; ++i) s += "LoadObject o" + std::to_string(i) + ".lwo\n";
        fx::FakeLoad fake; const IrrMeshLoader l = fake.Fn();
        ImportedModel m; std::string err;
        const Run r = Measure("257 object files", kMaxSeconds, [&] { return ReadLwsBuffer(s, "d/", m, &err, &l); });
        CHECK(!r.ok && err.find("different object files") != std::string::npos && fake.paths.size() == 256);
    }
    {   // one object of 100,000 triangles (300,000 vertices, not welded) placed 50 times: 15,000,000 placed vertices from a file of a few hundred bytes
        std::string s = "LWSC\n3\n";
        for (int i = 0; i < 50; ++i) s += "LoadObject big.lwo\n";
        fx::FakeLoad fake; const IrrMeshLoader l = fake.Fn(100000);
        ImportedModel m; std::string err;
        const Run r = Measure("one 100k-triangle object placed 50 times", kMaxSeconds * 4, [&] { return ReadLwsBuffer(s, "d/", m, &err, &l); });
        CHECK(!r.ok && err.find("(budget)") != std::string::npos && fake.paths.size() == 1);   // the file is asked for once
        std::string s2 = "LWSC\n3\n";
        for (int i = 0; i < 12; ++i) s2 += "LoadObject big.lwo\n";
        ImportedModel m2; std::string e2;
        CHECK(ReadLwsBuffer(s2, "d/", m2, &e2, &l) && fx::TriCount(m2) == 1200000);   // 12 placements (3,600,000 vertices) are still inside the budget of 4,000,000
    }
    {   // lines that are too long are skipped (here a 20 MB line, and one of exactly 65537 bytes), not tokenised
        std::string s = "LWSC\n3\nAddNullObject A\n" + std::string(20u * 1024u * 1024u, 'x') + "\n" + std::string(65537, 'y') + "\nAddNullObject B\n";
        ImportedModel m; std::string err;
        const Run r = Measure("20 MB line", kMaxSeconds, [&] { return ReadLwsBuffer(s, "", m, &err); });
        CHECK(r.ok && m.nodes.size() == 3 && HasNote(m, "2 line(s) longer than 65536 bytes skipped"));
    }
    {   // huge numbers everywhere: the nodes and keys stay finite
        const std::string s = "LWSC\n3\nAddNullObject A\nObjectMotion\nNumChannels 9\n" +
            fx::Chan(0, {{-1e7, 1e15}, {1e7, -1e15}}) + fx::Chan(3, {{0, 1e15}, {1, -1e15}}) + fx::Chan(4, {{0, 123456789.0}}) + fx::Chan(6, {{0, 1e15}, {2, 1e-15}});
        ImportedModel m; std::string err;
        CHECK(ReadLwsBuffer(s, "", m, &err) && fx::Sane(m) && m.clips.size() == 1);
    }
    {   // a scene that names itself (the scene's own file), through the loader seam's rule and through the reader's
        fx::FakeLoad fake; const IrrMeshLoader l = fake.Fn();
        ImportedModel m; std::string err;
        CHECK(!ReadLwsBuffer("LWSC\n3\nLoadObject self.lws\nLoadObject ./self.LWS\nLoadObject sub/../self.lws\n", "d/", m, &err, &l) && fake.paths.empty());
    }
}

static void WriteFile(const fs::path& p, const std::string& s) { std::ofstream f(p, std::ios::binary); f.write(s.data(), (std::streamsize)s.size()); }

static void LwsPathsOnDisk() {
    const fs::path root = fs::temp_directory_path() / "okay_lws_paths";
    std::error_code ec;
    fs::remove_all(root, ec);
    const fs::path scene = root / "scene", outside = root / "outside";
    fs::create_directories(scene / "Objects");
    fs::create_directories(outside);
    WriteFile(outside / "x.lwo", "x");
    WriteFile(scene / "Objects" / "ok.lwo", "x");
    WriteFile(scene / "s.lws", fx::BuildLws());
    bool link = true;
    fs::create_directory_symlink(outside, scene / "link", ec);
    if (ec) link = false;   // no symbolic links here (Windows without the right): that one case is skipped
    std::vector<std::string> asked;
    IrrMeshLoader loader = [&](const std::string& p, ImportedModel& m, std::string*) {
        asked.push_back(p);
        ImportedPart part; part.mesh.vertices = {Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{0, 1, 0}}; part.mesh.triangles = {0, 1, 2};
        m.parts.push_back(part); m.nodes.push_back(ImportedNode{});
        return true;
    };
    // the file version of the reader, with a real scene file in a real folder
    {
        ImportedModel m; std::string err;
        asked.clear();
        CHECK(ReadLwsFile((scene / "s.lws").string(), m, &err, &loader) && asked.size() == 1 && asked[0].find("Objects") != std::string::npos);
    }
    auto readOne = [&](const std::string& ref, ImportedModel& m, std::string& err) {
        asked.clear();
        return ReadLwsBuffer("LWSC\n3\nLoadObject Objects/ok.lwo\nLoadObject " + ref + "\n", scene.string() + "/", m, &err, &loader);
    };
    const std::string refs[] = {(outside / "x.lwo").string(), "../outside/x.lwo", "Objects/../../outside/x.lwo", "..\\outside\\x.lwo", "/etc/passwd", "C:/x.lwo", "\\\\host\\share\\x.lwo", "//host/x.lwo"};
    for (const std::string& ref : refs) {
        ImportedModel m; std::string err;
        const bool ok = readOne(ref, m, err);
        CHECK(ok && asked.size() == 1 && asked[0].find("ok.lwo") != std::string::npos && HasNote(m, "refused"));   // the one inside is read; the other is never asked for
    }
    if (link) { ImportedModel m; std::string err; CHECK(readOne("link/x.lwo", m, err) && asked.size() == 1 && HasNote(m, "refused")); }
    { ImportedModel m; std::string err; CHECK(readOne("Objects/ok.lwo", m, err) && asked.size() == 1 && m.parts.size() == 2); }   // the same file twice is asked for once
    fs::remove_all(root, ec);
}

int main() {
    RUN_SUITE("pmxlws_hostile");
    PmxHostile();
    LwsHostile();
    LwsPathsOnDisk();
    TEST_MAIN_RESULT();
}
