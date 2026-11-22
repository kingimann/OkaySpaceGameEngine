// The own readers of the "Assimp gap" batch: the OGRE binary mesh (OgreXmlReader.cpp, ReadOgreBinary*) and ZAE (ZaeReader.cpp,
// COLLADA in a ZIP). SYNTHETIC fixtures built in memory (tests/gap_fixtures.hpp), checked against hand-computed numbers and, for
// the binary mesh, against what the XML reader gives for its XML twin (the same mesh, batchd_fixtures.hpp). Then the hostile part:
// every truncation of each fixture, a mutation sweep (OKAY_GAP_FUZZ_ITERS raises it), and small files whose counts claim gigabytes,
// each read under a counting operator new with a memory cap and a time limit; every model a reader returns must be sane.
// Real files are checked by model_corpus. Builds on its own with the reader sources (-DOKAY_STANDALONE_TEST), also under
// -fsanitize=address,undefined.
#include "test_framework.hpp"
#include "batchd_fixtures.hpp"
#include "gap_fixtures.hpp"
#include "okay/Render/ModelData.hpp"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
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
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { try { return CountedAlloc(n); } catch (...) { return nullptr; } }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { try { return CountedAlloc(n); } catch (...) { return nullptr; } }
void operator delete(void* p) noexcept { CountedFree(p); }
void operator delete[](void* p) noexcept { CountedFree(p); }
void operator delete(void* p, std::size_t) noexcept { CountedFree(p); }
void operator delete[](void* p, std::size_t) noexcept { CountedFree(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { CountedFree(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { CountedFree(p); }

static double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
static bool HasNote(const ImportedModel& m, const char* what) { for (const std::string& n : m.notes) if (n.find(what) != std::string::npos) return true; return false; }
static std::string Slurp(const fs::path& p) { std::ifstream f(p, std::ios::binary); return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); }
static void WriteFile(const fs::path& p, const std::string& s) { std::ofstream f(p, std::ios::binary); f.write(s.data(), (std::streamsize)s.size()); }

static bool SameBytes(const void* a, const void* b, std::size_t n) { return n == 0 || std::memcmp(a, b, n) == 0; }
// Two models hold the same parts, materials and nodes, bit for bit.
static bool SameModel(const ImportedModel& a, const ImportedModel& b) {
    if (a.parts.size() != b.parts.size() || a.materials.size() != b.materials.size() || a.nodes.size() != b.nodes.size()) return false;
    for (std::size_t i = 0; i < a.parts.size(); ++i) {
        const Mesh &x = a.parts[i].mesh, &y = b.parts[i].mesh;
        if (x.name != y.name || x.triangles != y.triangles || x.vertices.size() != y.vertices.size() || x.normals.size() != y.normals.size() || x.uvs.size() != y.uvs.size()) return false;
        if (!SameBytes(x.vertices.data(), y.vertices.data(), x.vertices.size() * sizeof(Vec3))) return false;
        if (!SameBytes(x.normals.data(), y.normals.data(), x.normals.size() * sizeof(Vec3))) return false;
        if (!SameBytes(x.uvs.data(), y.uvs.data(), x.uvs.size() * sizeof(Vec2))) return false;
        if (a.parts[i].material != b.parts[i].material) return false;
    }
    for (std::size_t i = 0; i < a.materials.size(); ++i)
        if (a.materials[i].name != b.materials[i].name || a.materials[i].texture != b.materials[i].texture || a.materials[i].hasDiffuse != b.materials[i].hasDiffuse) return false;
    return true;
}

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_UNDEFINED__)
static constexpr double kMaxSeconds = 20.0;
#else
static constexpr double kMaxSeconds = 2.0;
#endif
static constexpr std::size_t kCapBytes = 64u * 1024u * 1024u;   // small inputs: nothing may take more than this at once

// One hostile read: within the time and memory caps, and a sane model or a refusal with a message.
template <class F> static void Hostile(F&& read) {
    const std::size_t base = g_live.load();
    g_peak.store(base);
    const double t = Now();
    ImportedModel m; std::string e;
    const bool ok = read(m, e);
    CHECK(Now() - t < kMaxSeconds);
    CHECK(g_peak.load() - base < kCapBytes);
    if (ok) CHECK(fx::Sane(m) && m.backend == "own");
    else CHECK(!e.empty() && m.parts.empty());
}

static void OgreBinary() {
    const std::vector<std::string> scripts{fx::BuildOgreMaterial()};
    ImportedModel x, b; std::string err;
    CHECK(ReadOgreXmlBuffer(fx::BuildOgreXml(), "dir/", x, &err, &scripts));
    CHECK(ReadOgreBinaryBuffer(fx::BuildOgreBin(), "dir/", b, &err, &scripts));
    CHECK(b.backend == "own" && fx::Sane(b));
    CHECK(SameModel(b, x));   // the binary twin reads to the very same parts and materials
    CHECK(b.parts.size() == 2 && fx::TriCount(b) == 3 && b.parts[0].mesh.name == "left" && b.parts[1].mesh.name == "Mat/B");
    CHECK_NEAR(fx::Area(b), 10.0, 1e-5);
    CHECK(b.materials.size() == 2 && b.materials[0].texture == "dir/tex_a.png" && b.materials[0].hasDiffuse);
    CHECK(b.notes == x.notes);   // same skeleton name ("x.skeleton") in both, so the notes agree too
    CHECK(HasNote(b, "skeleton 'x.skeleton'"));

    {   // 32-bit indices: the same result
        fx::OgreBinOpts o; o.index32 = true;
        ImportedModel m; CHECK(ReadOgreBinaryBuffer(fx::BuildOgreBin(o), "dir/", m, &err, &scripts)); CHECK(SameModel(m, x));
    }
    {   // a fan 0 1 2 3 is the list (0 1 2) (0 2 3)
        fx::OgreBinOpts o; o.op0 = 6; o.stripIndices = true;
        ImportedModel m; CHECK(ReadOgreBinaryBuffer(fx::BuildOgreBin(o), "dir/", m, &err, &scripts)); CHECK(SameModel(m, x));
    }
    {   // a strip 0 1 3 2 covers the same square, with the strip's alternating winding
        fx::OgreBinOpts o; o.op0 = 5; o.stripIndices = true;
        ImportedModel m; CHECK(ReadOgreBinaryBuffer(fx::BuildOgreBin(o), "dir/", m, &err, &scripts));
        CHECK(fx::TriCount(m) == 3); CHECK_NEAR(fx::Area(m), 10.0, 1e-5);
        CHECK(m.parts[0].mesh.triangles.size() == 6);
    }
    {   // a line submesh is skipped with a note
        fx::OgreBinOpts o; o.op0 = 2;
        ImportedModel m; CHECK(ReadOgreBinaryBuffer(fx::BuildOgreBin(o), "", m, &err)); CHECK(m.parts.size() == 1 && HasNote(m, "lines, points"));
        CHECK(HasNote(m, "no .material script"));
    }
    {   // a second texture coordinate set is counted, as the XML reader counts it (per vertex)
        fx::OgreBinOpts o; o.extraUvSet = true;
        ImportedModel m; CHECK(ReadOgreBinaryBuffer(fx::BuildOgreBin(o), "", m, &err)); CHECK(HasNote(m, "4 texture coordinate set(s) after the first"));
        CHECK(m.parts[0].mesh.uvs.size() == 4 && m.parts[0].mesh.uvs[2].x == 1.0f && m.parts[0].mesh.uvs[2].y == 1.0f);
    }
    {   // a trailing chunk whose length runs past the end: the submeshes are kept, with a note
        fx::OgreBinOpts o; o.badTailLength = 0x7FFFFFF0u;
        ImportedModel m; CHECK(ReadOgreBinaryBuffer(fx::BuildOgreBin(o), "", m, &err)); CHECK(m.parts.size() == 2 && HasNote(m, "the rest of the file was not read"));
    }
    {   // another version line is read with a note; a different serializer, a big-endian file and no header are refused
        fx::OgreBinOpts o; o.version = "[MeshSerializer_v1.41]";
        ImportedModel m; CHECK(ReadOgreBinaryBuffer(fx::BuildOgreBin(o), "", m, &err)); CHECK(HasNote(m, "was not checked against a real file"));
        o.version = "[Serializer_v1.10]";
        CHECK(!ReadOgreBinaryBuffer(fx::BuildOgreBin(o), "", m, &err) && err.find("not an OGRE binary mesh") != std::string::npos);
        std::string be = fx::BuildOgreBin(); be[0] = 0x10; be[1] = 0x00;
        CHECK(!ReadOgreBinaryBuffer(be, "", m, &err) && err.find("big-endian") != std::string::npos);
        CHECK(!ReadOgreBinaryBuffer("", "", m, &err));
        CHECK(!ReadOgreBinaryBuffer(fx::BuildOgreXml(), "", m, &err));
    }
    {   // the path version reads the .material files next to the mesh
        const fs::path dir = fs::temp_directory_path() / "okay_gap_ogre";
        std::error_code ec; fs::remove_all(dir, ec); fs::create_directories(dir);
        WriteFile(dir / "a.mesh", fx::BuildOgreBin());
        WriteFile(dir / "a.material", fx::BuildOgreMaterial());
        ImportedModel m; CHECK(ReadOgreBinaryFile((dir / "a.mesh").string(), m, &err));
        CHECK(m.materials.size() == 2 && m.materials[0].hasDiffuse && m.materials[0].texture == (dir / "tex_a.png").string());
        CHECK(!ReadOgreBinaryFile((dir / "missing.mesh").string(), m, &err));
        fs::remove_all(dir, ec);
    }
}

static void Zae() {
    std::string err;
    {   // the manifest names sub/doc%20one.dae; the decoy at the top is not read; the image is inside the archive
        ImportedModel m;
        CHECK(ReadZaeBuffer(fx::BuildZae(), m, &err));
        CHECK(m.backend == "own" && fx::Sane(m) && fx::TriCount(m) == 1);
        CHECK_NEAR(fx::Area(m), 2.0, 1e-5);
        CHECK(m.materials.size() == 1 && m.materials[0].texture.empty());   // the buffer version writes nothing
        CHECK(HasNote(m, "'sub/doc one.dae'") && HasNote(m, "read from memory"));
        ImportedModel d; CHECK(ReadColladaBuffer(fx::BuildMiniDae(), "", d, &err));
        CHECK(fx::TriCount(d) == 1 && d.parts[0].mesh.vertices.size() == m.parts[0].mesh.vertices.size());
    }
    {   // no manifest: the .dae nearest the top
        ImportedModel m;
        CHECK(ReadZaeBuffer(fx::BuildZae(false, false), m, &err) && fx::TriCount(m) == 1);
        CHECK(!ReadZaeBuffer(fx::BuildZae(false, true), m, &err));   // the decoy is now nearest the top and has nothing to draw
    }
    {   // a manifest that names a missing file: the first .dae, with a note
        std::vector<fx::ZipEntry> es{{"manifest.xml", fx::BuildManifest("nothere.dae")}, {"a/b.dae", fx::BuildMiniDae("")}};
        ImportedModel m; CHECK(ReadZaeBuffer(fx::MakeZip(es), m, &err)); CHECK(HasNote(m, "not in the archive") && fx::TriCount(m) == 1);
        CHECK(m.materials.size() == 1 && m.materials[0].hasDiffuse);
    }
    {   // refusals: not a zip, a zip without a .dae, a manifest that climbs out of the archive and nothing else
        ImportedModel m;
        CHECK(!ReadZaeBuffer("<COLLADA/>", m, &err) && err.find("ZIP") != std::string::npos);
        CHECK(!ReadZaeBuffer(fx::MakeZip({{"readme.txt", "hi"}}), m, &err) && err.find("no .dae") != std::string::npos);
        CHECK(!ReadZaeBuffer(fx::MakeZip({{"manifest.xml", fx::BuildManifest("../../etc/x.dae")}}), m, &err));
    }
    {   // the path version writes the archive's image next to the .zae and points the material at it; a texture not in the
        // archive is looked up next to the .zae
        const fs::path dir = fs::temp_directory_path() / "okay_gap_zae";
        std::error_code ec; fs::remove_all(dir, ec); fs::create_directories(dir);
        WriteFile(dir / "duck.zae", fx::BuildZae());
        ImportedModel m; CHECK(ReadZaeFile((dir / "duck.zae").string(), m, &err));
        const fs::path side = dir / "duck_zae_tex a.png";
        CHECK(m.materials.size() == 1 && m.materials[0].texture == side.string());
        CHECK(fs::exists(side) && Slurp(side).compare(0, 4, "\x89PNG") == 0);
        CHECK(HasNote(m, "written next to the file"));
        ImportedModel again; CHECK(ReadZaeFile((dir / "duck.zae").string(), again, &err) && again.materials[0].texture == side.string());   // a second read keeps it
        WriteFile(dir / "out.zae", fx::MakeZip({{"x.dae", fx::BuildMiniDae("../pics/t.png")}}));
        ImportedModel o; CHECK(ReadZaeFile((dir / "out.zae").string(), o, &err));
        CHECK(o.materials.size() == 1 && o.materials[0].texture == (dir / "../pics/t.png").string() && HasNote(o, "not in the archive"));
        fs::remove_all(dir, ec);
    }
}

static int FuzzIters() { const char* e = std::getenv("OKAY_GAP_FUZZ_ITERS"); const int n = e ? std::atoi(e) : 0; return n > 0 ? n : 1500; }

static void HostileOgre() {
    const std::string base = fx::BuildOgreBin();
    for (std::size_t n = 0; n < base.size(); ++n)   // every truncation
        Hostile([&](ImportedModel& m, std::string& e) { return ReadOgreBinaryBuffer(base.substr(0, n), "", m, &e); });
    for (int i = 0; i < FuzzIters(); ++i) {
        std::string s = base;
        const int edits = 1 + (int)(fx::Rnd() % 4);
        for (int k = 0; k < edits; ++k) {
            const std::size_t at = fx::Rnd() % s.size();
            switch (fx::Rnd() % 4) {
            case 0: s[at] = (char)(fx::Rnd() & 255); break;
            case 1: if (at + 4 <= s.size()) { const std::uint32_t v = fx::Rnd() % 2 ? 0xFFFFFFFFu : (std::uint32_t)(fx::Rnd() % 70000); std::memcpy(&s[at], &v, 4); } break;
            case 2: s.erase(at, 1 + fx::Rnd() % 8); break;
            default: s.insert(at, std::string(1 + fx::Rnd() % 6, (char)(fx::Rnd() & 255))); break;
            }
            if (s.empty()) s = "x";
        }
        Hostile([&](ImportedModel& m, std::string& e) { return ReadOgreBinaryBuffer(s, "", m, &e); });
    }
    // counts that claim gigabytes in a file of a few bytes
    auto header = [] { std::string s; fx::P16(s, 0x1000); s += "[MeshSerializer_v1.8]\n"; fx::OChunk(s, 0x3000, 0); s.push_back(0); return s; };
    {   std::string s = header(); fx::OChunk(s, 0x5000, 0); fx::P32(s, 7000000);   // 7 million vertices, no data
        fx::OChunk(s, 0x5100, 0); fx::OChunk(s, 0x5110, 0); for (unsigned v : {0u, 2u, 1u, 0u, 0u}) fx::P16(s, v);
        fx::OChunk(s, 0x5200, 0); fx::P16(s, 0); fx::P16(s, 12); fx::OChunk(s, 0x5210, 0);
        Hostile([&](ImportedModel& m, std::string& e) { const bool r = ReadOgreBinaryBuffer(s, "", m, &e); CHECK(!r); return r; }); }
    {   std::string s = header(); fx::OChunk(s, 0x4000, 0); s += "m\n"; s.push_back(1); fx::P32(s, 0xFFFFFFFFu); s.push_back(1);   // 4 billion indices
        Hostile([&](ImportedModel& m, std::string& e) { const bool r = ReadOgreBinaryBuffer(s, "", m, &e); CHECK(!r); return r; }); }
    {   std::string s = header(); fx::OChunk(s, 0x4000, 0); s += std::string(100000, 'a');   // a text field without its end
        Hostile([&](ImportedModel& m, std::string& e) { const bool r = ReadOgreBinaryBuffer(s, "", m, &e); CHECK(!r); return r; }); }
    {   // positions outside their vertex, and positions that name a missing buffer: refused before anything is allocated from the count
        std::string s = header(); fx::OChunk(s, 0x5000, 0); fx::P32(s, 1);
        fx::OChunk(s, 0x5100, 0); fx::OChunk(s, 0x5110, 0); for (unsigned v : {0u, 2u, 1u, 8u, 0u}) fx::P16(s, v);
        fx::OChunk(s, 0x5200, 0); fx::P16(s, 0); fx::P16(s, 12); fx::OChunk(s, 0x5210, 0); s += std::string(12, '\0');
        ImportedModel m; std::string e; CHECK(!ReadOgreBinaryBuffer(s, "", m, &e) && e.find("no 32-bit float positions inside a vertex buffer") != std::string::npos);
        std::string t = header(); fx::OChunk(t, 0x5000, 0); fx::P32(t, 1);
        fx::OChunk(t, 0x5100, 0); fx::OChunk(t, 0x5110, 0); for (unsigned v : {3u, 2u, 1u, 0u, 0u}) fx::P16(t, v);
        CHECK(!ReadOgreBinaryBuffer(t, "", m, &e) && e.find("no 32-bit float positions inside a vertex buffer") != std::string::npos);
    }
}

static void HostileZae() {
    const std::string base = fx::BuildZae();
    for (std::size_t n = 0; n < base.size(); n += 1 + n / 64)
        Hostile([&](ImportedModel& m, std::string& e) { return ReadZaeBuffer(base.substr(0, n), m, &e); });
    for (int i = 0; i < FuzzIters(); ++i) {
        std::string s = base;
        const std::size_t at = fx::Rnd() % s.size();
        if (fx::Rnd() % 2) s[at] = (char)(fx::Rnd() & 255);
        else if (at + 4 <= s.size()) { const std::uint32_t v = fx::Rnd() % 2 ? 0xFFFFFFFFu : (std::uint32_t)(fx::Rnd() % 100000); std::memcpy(&s[at], &v, 4); }
        Hostile([&](ImportedModel& m, std::string& e) { return ReadZaeBuffer(s, m, &e); });
    }
    {   // a deflated entry that claims 300 MB (the zip bomb guard), and a manifest with millions of elements
        fx::ZipEntry big{"a.dae", ""}; big.rawSet = true; big.deflate = true; big.raw = fx::DeflateRepeat('<', 200); big.usize = 300u * 1024u * 1024u; big.crc = 0;
        const std::string z = fx::MakeZip({big});
        Hostile([&](ImportedModel& m, std::string& e) { const bool r = ReadZaeBuffer(z, m, &e); CHECK(!r); return r; });
        std::string many = "<dae_root>"; for (int k = 0; k < 200000; ++k) many += "<a/>"; many += "</dae_root>";
        const std::string z2 = fx::MakeZip({{"manifest.xml", many}, {"x.dae", fx::BuildMiniDae("")}});
        Hostile([&](ImportedModel& m, std::string& e) { return ReadZaeBuffer(z2, m, &e); });
    }
}

int main() {
    RUN_SUITE("gap_readers");
    OgreBinary();
    Zae();
    HostileOgre();
    HostileZae();
    TEST_MAIN_RESULT();
}
