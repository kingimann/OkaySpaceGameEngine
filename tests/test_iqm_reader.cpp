// Our own IQM reader (engine/src/Render/IqmReader.cpp). SYNTHETIC fixture built in memory (BuildIqm in e3_fixtures.hpp) checked against
// hand-computed numbers; hostile files (every header count 0xFFFFFFFF, offsets past the end, cyclic joint parents, truncation at every
// length, random byte flips); the per-file budgets; a counting operator new for the worst file the budgets allow; and, when the
// environment variable OKAY_E3_SAMPLES names a folder holding a real mrfixit.iqm, that file too (without it that part says "skipped").
// Builds on its own with -DOKAY_STANDALONE_TEST (reader source only).
#include "test_framework.hpp"
#include "e3_fixtures.hpp"
#include "okay/Render/ModelData.hpp"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
static void CountedFree(void* q) noexcept { if (!q) return; void* raw = static_cast<char*>(q) - 16; g_live.fetch_sub(*static_cast<std::size_t*>(raw)); std::free(raw); }
void* operator new(std::size_t n) { return CountedAlloc(n); }
void* operator new[](std::size_t n) { return CountedAlloc(n); }
void operator delete(void* p) noexcept { CountedFree(p); }
void operator delete[](void* p) noexcept { CountedFree(p); }
void operator delete(void* p, std::size_t) noexcept { CountedFree(p); }
void operator delete[](void* p, std::size_t) noexcept { CountedFree(p); }

static constexpr std::size_t kWorstCap = 50u * 1024u * 1024u;   // about 2x the measured worst case (24 MB)
static constexpr std::size_t kReuseCap = 50u * 1024u * 1024u;   // about 2x the measured 24 MB
static bool HasNote(const ImportedModel& m, const char* what) { for (const std::string& n : m.notes) if (n.find(what) != std::string::npos) return true; return false; }
static bool Read(const std::string& b, ImportedModel& m, std::string* e = nullptr) { std::string er; const bool ok = ReadIqmBuffer(b, "dir/", m, e ? e : &er); return ok; }


// A small file whose meshes all name the same tiny range: `nMeshes` meshes of `verts` vertices each (one position array of `verts` vertices, one triangle),
// `nJoints` joints with names `nameLen` characters long. The file is a few hundred KB however many parts it would make.
static std::string BuildReusing(std::uint32_t nMeshes, std::uint32_t verts, std::uint32_t nJoints, std::size_t nameLen) {
    std::string text(1, '\0');
    std::vector<std::uint32_t> jn;
    for (std::uint32_t j = 0; j < nJoints; ++j) { jn.push_back((std::uint32_t)text.size()); std::string n = std::to_string(j); n.resize(nameLen, 'n'); text += n; text.push_back('\0'); }
    std::string s;
    s.append("INTERQUAKEMODEL", 15); s.push_back('\0'); s.resize(124, '\0');
    const std::uint32_t ofsText = (std::uint32_t)s.size(); s += text; while (s.size() % 4) s.push_back('\0');
    const std::uint32_t ofsM = (std::uint32_t)s.size();
    for (std::uint32_t m = 0; m < nMeshes; ++m) { fx::P32(s, 0); fx::P32(s, 0); fx::P32(s, 0); fx::P32(s, verts); fx::P32(s, 0); fx::P32(s, 1); }
    const std::uint32_t ofsVA = (std::uint32_t)s.size();
    fx::P32(s, 0); fx::P32(s, 0); fx::P32(s, 7); fx::P32(s, 3); const std::size_t vaOfs = s.size(); fx::P32(s, 0);
    const std::uint32_t ofsT = (std::uint32_t)s.size(); fx::P32(s, 0); fx::P32(s, 1); fx::P32(s, 2);
    const std::uint32_t ofsJ = (std::uint32_t)s.size();
    for (std::uint32_t j = 0; j < nJoints; ++j) { fx::P32(s, jn[j]); fx::P32(s, (std::uint32_t)-1); for (float v : {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f, 1.f}) fx::PutF(s, v); }
    const std::uint32_t ofsP = (std::uint32_t)s.size();
    for (std::uint32_t i = 0; i < verts; ++i) { fx::PutF(s, (float)(i % 97)); fx::PutF(s, (float)(i / 97)); fx::PutF(s, 0.0f); }
    fx::Put32(s, vaOfs, ofsP);
    const std::uint32_t vals[27] = {2, (std::uint32_t)s.size(), 0, (std::uint32_t)text.size(), ofsText, nMeshes, ofsM, 1, verts, ofsVA, 1, ofsT, 0, nJoints, ofsJ, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    for (int i = 0; i < 27; ++i) fx::IqmSetH(s, i, vals[i]);
    return s;
}

int main() {
    RUN_SUITE("iqm_reader");
    {   // ---- the hand-built file ----
        const std::string f = fx::BuildIqm();
        ImportedModel m; std::string err;
        CHECK(ReadIqmBuffer(f, "dir/", m, &err));
        if (!err.empty()) std::fprintf(stderr, "  %s\n", err.c_str());
        CHECK(m.backend == "own" && fx::Sane(m));
        CHECK(m.nodes.size() == 3 && m.nodes[0].name == "IQM" && m.nodes[1].name == "root" && m.nodes[2].name == "arm" && m.nodes[1].parent == 0 && m.nodes[2].parent == 1);
        CHECK(fx::Near(m.nodes[1].position.x, 1) && fx::Near(m.nodes[2].position.y, 2) && fx::Near(m.nodes[2].rotation.z, 0.70710678f) && fx::Near(m.nodes[2].rotation.w, 0.70710678f));
        CHECK(m.parts.size() == 1);
        const ImportedPart& p = m.parts[0];
        CHECK(p.mesh.name == "quad" && p.mesh.vertices.size() == 4 && p.mesh.triangles.size() == 6 && p.node == 0);
        const int tri[6] = {0, 1, 2, 0, 2, 3};
        for (int i = 0; i < 6; ++i) CHECK(p.mesh.triangles[(std::size_t)i] == tri[i]);
        CHECK(fx::Near(p.mesh.vertices[2].x, 2) && fx::Near(p.mesh.vertices[2].y, 3) && fx::Near(p.mesh.vertices[2].z, 0));
        CHECK(p.mesh.uvs.size() == 4 && fx::Near(p.mesh.uvs[2].x, 1) && fx::Near(p.mesh.uvs[2].y, 1) && fx::Near(p.mesh.uvs[3].x, 0) && fx::Near(p.mesh.uvs[3].y, 1));
        CHECK(p.mesh.normals.size() == 4 && fx::Near(p.mesh.normals[1].z, 1));
        CHECK_NEAR(fx::Area(m), 6.0, 1e-5);
        // skin
        CHECK(p.bones.size() == 2 && p.bones[0].name == "root" && p.bones[1].name == "arm" && p.jointIdx.size() == 4 && p.jointWt.size() == 4);
        CHECK(p.jointIdx[0][0] == 0 && fx::Near(p.jointWt[0][0], 1.0f) && p.jointIdx[2][0] == 0 && p.jointIdx[2][1] == 1);
        CHECK(fx::Near(p.jointWt[2][0], 128.0f / 255.0f) && fx::Near(p.jointWt[2][1], 127.0f / 255.0f));
        CHECK(fx::Near(p.bones[0].inverseBind.MultiplyPoint(Vec3{1, 0, 0}).Magnitude(), 0));        // root's world origin is (1,0,0)
        CHECK(fx::Near(p.bones[1].inverseBind.MultiplyPoint(Vec3{1, 2, 0}).Magnitude(), 0));        // arm's world origin is (1,2,0)
        const Vec3 a = p.bones[1].inverseBind.MultiplyPoint(Vec3{1, 3, 0});                          // one unit along the arm's own x axis (turned to world +y)
        CHECK(fx::Near(a.x, 1) && fx::Near(a.y, 0) && fx::Near(a.z, 0));
        // material: the name is an image: its texture is resolved inside the folder
        CHECK(m.materials.size() == 1 && m.materials[0].name == "red.png" && m.materials[0].texture == "dir/red.png" && p.material == 0);
        // animation
        CHECK(m.clips.size() == 1 && m.clips[0].name == "wave" && m.clips[0].channels.size() == 1);
        const ImportedChannel& c = m.clips[0].channels[0];
        CHECK(c.node == "arm" && c.position.size() == 3 && c.scale.size() == 3 && c.rotation.empty());
        const float tx[3] = {0, 1, 2}, sx[3] = {1, 2, 3};
        for (int i = 0; i < 3; ++i) {
            CHECK(fx::Near(c.position[(std::size_t)i].time, 0.1f * (float)i) && fx::Near(c.position[(std::size_t)i].value.x, tx[i]) && fx::Near(c.position[(std::size_t)i].value.y, 2));
            CHECK(fx::Near(c.scale[(std::size_t)i].value.x, sx[i]) && fx::Near(c.scale[(std::size_t)i].value.y, 1));
        }
        CHECK(HasNote(m, "Z-up"));
    }
    {   // a mesh without normals gets smooth ones; a file without blend arrays is not skinned
        std::string f = fx::BuildIqm();
        const std::uint32_t ofsVA = fx::IqmH(f, fx::kOfsVA);
        fx::Put32(f, ofsVA + 20 * 2, 99);            // the normal array becomes an unknown kind
        fx::Put32(f, ofsVA + 20 * 3, 98); fx::Put32(f, ofsVA + 20 * 4, 97);
        ImportedModel m;
        CHECK(Read(f, m) && m.parts[0].mesh.normals.size() == 4 && fx::Near(std::fabs(m.parts[0].mesh.normals[0].z), 1) && m.parts[0].bones.empty() && HasNote(m, "not skinned") && HasNote(m, "no normals"));
    }
    {   // cyclic joint parents are cut; an out-of-range parent goes to the root; the nodes stay parent first
        std::string f = fx::BuildIqm();
        const std::uint32_t oj = fx::IqmH(f, fx::kOfsJoints);
        fx::Put32(f, oj + 4, 1);          // root's parent = arm (arm's parent is root): a loop
        ImportedModel m;
        CHECK(Read(f, m) && fx::Sane(m) && m.nodes.size() == 3 && HasNote(m, "loop"));
        fx::Put32(f, oj + 4, 0xFFFFFF00u); fx::Put32(f, oj + 48 + 4, 7);
        CHECK(Read(f, m) && fx::Sane(m) && m.nodes.size() == 3 && HasNote(m, "out of range"));
        fx::Put32(f, oj + 4, 0); fx::Put32(f, oj + 48 + 4, 1);   // self parents
        CHECK(Read(f, m) && fx::Sane(m));
    }
    {   // refusals with a reason
        ImportedModel m; std::string e;
        CHECK(!Read(std::string(), m, &e) && e.find("IQM") == 0);
        CHECK(!Read("INTERQUAKEMODEL", m, &e));
        std::string f = fx::BuildIqm();
        std::string g = f; g[0] = 'X';
        CHECK(!Read(g, m, &e) && e.find("not an IQM") != std::string::npos);
        g = f; fx::IqmSetH(g, fx::kVersion, 1);
        CHECK(!Read(g, m, &e) && e.find("version") != std::string::npos);
        g = f.substr(0, f.size() - 10);
        CHECK(!Read(g, m, &e) && e.find("truncated") != std::string::npos);
        g = f; fx::Put32(g, fx::IqmH(f, fx::kOfsMeshes) + 8, 0xFFFFFFFFu);   // mesh first_vertex way out
        CHECK(!Read(g, m, &e) && e.find("mesh") != std::string::npos);
        g = f; fx::Put32(g, fx::IqmH(f, fx::kOfsAnims) + 8, 99);              // anim count 99 > frames
        CHECK(!Read(g, m, &e) && e.find("frames") != std::string::npos);
        g = f; fx::Put32(g, fx::IqmH(f, fx::kOfsVA) + 8, 77);                  // unknown data format
        CHECK(!Read(g, m, &e));
        g = f; fx::Put32(g, fx::IqmH(f, fx::kOfsVA) + 16, (std::uint32_t)f.size() - 8);   // position array runs past the end
        CHECK(!Read(g, m, &e) && e.find("outside the file") != std::string::npos);
    }
    {   // texture paths: a model may only name files inside its own folder (the text block holds "red.png" at ot + 6)
        const std::string f = fx::BuildIqm();
        const std::uint32_t ot = fx::IqmH(f, fx::kOfsText);
        auto withName = [&](const char* nm) { std::string g = f; const std::size_t L = std::strlen(nm); for (std::size_t i = 0; i <= L; ++i) g[ot + 6 + i] = nm[i]; return g; };   // up to 7 characters: it must fit over "red.png"
        for (const char* bad : {"/x.png", "../.png", "C:/.png"}) {
            ImportedModel m;
            CHECK(Read(withName(bad), m) && m.materials.size() == 1 && m.materials[0].name == bad && m.materials[0].texture.empty() && HasNote(m, "refused"));
        }
        ImportedModel m;
        CHECK(Read(withName("a/b.png"), m) && m.materials[0].texture == "dir/a/b.png");
    }
    {   // hostile: every header count and offset set to extreme values
        const std::string f = fx::BuildIqm();
        int refused = 0, read = 0;
        for (int i = 3; i <= 22; ++i) for (std::uint32_t v : {0xFFFFFFFFu, 0x7FFFFFFFu, 0x80000000u, 0x10000u, 0u, (std::uint32_t)f.size(), (std::uint32_t)f.size() + 1}) {
            std::string g = f; fx::IqmSetH(g, i, v);
            ImportedModel m; std::string e;
            const bool ok = Read(g, m, &e);
            if (ok) { CHECK(fx::Sane(m)); ++read; } else { CHECK(!e.empty() && m.parts.empty() && m.nodes.empty()); ++refused; }
        }
        std::fprintf(stderr, "  header sweep: %d refused, %d read\n", refused, read);
        CHECK(refused > 100);
        // truncation at every length
        for (std::size_t n = 0; n < f.size(); ++n) { ImportedModel m; std::string e; const bool ok = Read(f.substr(0, n), m, &e); CHECK(!ok || fx::Sane(m)); if (n < 124) CHECK(!ok); }
        // random byte flips
        fx::State() = 0x1234567;
        int iters = 4000;
        if (const char* e = std::getenv("OKAY_BATCHD_FUZZ_ITERS")) iters = std::atoi(e) > 0 ? std::atoi(e) : iters;
        for (int it = 0; it < iters; ++it) {
            std::string g = f;
            const int flips = 1 + (int)(fx::Rnd() % 4);
            for (int k = 0; k < flips; ++k) g[(std::size_t)(fx::Rnd() % g.size())] ^= (char)(1u << (fx::Rnd() % 8));
            ImportedModel m;
            const bool ok = Read(g, m);
            if (ok) CHECK(fx::Sane(m));
        }
    }
    {   // budgets
        ImportedModel m; std::string e;
        std::string f = fx::BuildIqm();
        std::string g = f; fx::IqmSetH(g, fx::kNumVerts, 4000001);
        CHECK(!Read(g, m, &e) && e.find("too many vertices") != std::string::npos);
        g = f; fx::IqmSetH(g, fx::kNumTris, 4000001);
        CHECK(!Read(g, m, &e) && e.find("too many triangles") != std::string::npos);
        g = f; fx::IqmSetH(g, fx::kNumJoints, 10001);
        CHECK(!Read(g, m, &e) && e.find("too many joints") != std::string::npos);
        g = f; fx::IqmSetH(g, fx::kNumMeshes, 10001);
        CHECK(!Read(g, m, &e) && e.find("too many meshes") != std::string::npos);
        g = f; fx::IqmSetH(g, fx::kNumAnims, 10001);
        CHECK(!Read(g, m, &e) && e.find("too many animations") != std::string::npos);
        // frames x poses over the budget with no frame data at all (all channels constant): 2 poses x 250001 frames
        g = f; fx::IqmSetH(g, fx::kNumFrames, 250001); fx::IqmSetH(g, fx::kNumFrameCh, 0);
        fx::Put32(g, fx::IqmH(f, fx::kOfsAnims) + 8, 250001);
        // the pose masks must be 0 for num_framechannels 0
        fx::Put32(g, fx::IqmH(f, fx::kOfsPoses) + 4, 0); fx::Put32(g, fx::IqmH(f, fx::kOfsPoses) + 88 + 4, 0);
        CHECK(!Read(g, m, &e) && e.find("joint frames") != std::string::npos);
        // just under it is read: 2 poses x 249999 frames
        fx::Put32(g, fx::IqmH(f, fx::kOfsAnims) + 8, 249999);
        fx::IqmSetH(g, fx::kNumFrames, 249999);
        CHECK(Read(g, m, &e) && fx::Sane(m));
        // meshes x joints over the budget
        std::string h = f; fx::IqmSetH(h, fx::kNumMeshes, 10000); fx::IqmSetH(h, fx::kNumJoints, 100);
        CHECK(!Read(h, m, &e));
    }
    {   // memory amplification: many meshes naming one tiny range. The budget is on the OUTPUT (summed over meshes), and the bone list is copied per part
        const std::string big = BuildReusing(10000, 400, 0, 4);   // 10,000 x 400 = 4,000,000 output vertices from a tiny file
        std::size_t base = g_live.load(); g_peak.store(base);
        ImportedModel m; std::string e;
        CHECK(!Read(big, m, &e) && e.find("output vertices") != std::string::npos);
        std::fprintf(stderr, "  10,000 meshes x 400 vertices (%zu KB file): refused, peak %zu KB beyond the input\n", big.size() >> 10, (g_peak.load() - base) >> 10);
        CHECK(g_peak.load() - base < 4u * 1024u * 1024u);
        // the largest such file that is allowed: 1,000,000 output vertices (2,500 meshes x 400)
        const std::string ok = BuildReusing(2500, 400, 0, 4);
        base = g_live.load(); g_peak.store(base);
        CHECK(Read(ok, m, &e) && m.parts.size() == 2500);
        const std::size_t peak = (g_peak.load() - base) >> 20;
        std::fprintf(stderr, "  2,500 meshes x 400 vertices (1,000,000 output vertices): read, peak %zu MB beyond the input\n", peak);
        CHECK(g_peak.load() - base < kReuseCap);
        CHECK(!Read(BuildReusing(2501, 400, 0, 4), m, &e) && e.find("output vertices") != std::string::npos);
        // triangles: 1,000,001 meshes would be needed: the mesh cap and the vertex budget come first, so check the message of the one that fires
        // the bone list: 100 joints with 255-character names x 1,000 meshes = 100,000 bone entries (the entry budget) but 27 MB of names
        const std::string names = BuildReusing(1000, 4, 100, 255);
        base = g_live.load(); g_peak.store(base);
        CHECK(!Read(names, m, &e) && e.find("name bytes") != std::string::npos);
        CHECK(g_peak.load() - base < 4u * 1024u * 1024u);
        CHECK(!Read(BuildReusing(1001, 4, 100, 4), m, &e) && e.find("meshes x joints") != std::string::npos);
        CHECK(Read(BuildReusing(1000, 4, 100, 4), m, &e));   // short names: the same shape passes (blend arrays are absent, so no bone lists: rigid parts)
    }
    {   // a joint with zero scale cannot be inverted: identity inverse bind and a note, not a silent wrong matrix
        std::string f = fx::BuildIqm();
        const std::uint32_t oj = fx::IqmH(f, fx::kOfsJoints);
        fx::Put32(f, oj + 48 + 36, 0);   // arm: scale.x = 0.0f
        ImportedModel m;
        CHECK(Read(f, m) && HasNote(m, "zero scale") && m.parts[0].bones.size() == 2);
        const Mat4& I = m.parts[0].bones[1].inverseBind;
        for (int k = 0; k < 16; ++k) CHECK(fx::Near(I.m[k], (k % 5 == 0) ? 1.0f : 0.0f));
    }
    {   // the worst file the budgets allow: 100 joints x 5000 frames = 500,000 joint frames, every channel animated; memory is counted
        const std::uint32_t nj = 100, nf = 5000;
        std::string s;
        s.append("INTERQUAKEMODEL", 15); s.push_back('\0'); s.resize(124, '\0');
        const std::uint32_t ofsJ = (std::uint32_t)s.size();
        for (std::uint32_t j = 0; j < nj; ++j) { fx::P32(s, 0); fx::P32(s, (std::uint32_t)(j ? (int)j - 1 : -1)); for (float v : {0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f, 1.f}) fx::PutF(s, v); }
        const std::uint32_t ofsP = (std::uint32_t)s.size();
        for (std::uint32_t j = 0; j < nj; ++j) { fx::P32(s, (std::uint32_t)(j ? (int)j - 1 : -1)); fx::P32(s, 0x3FF); for (int k = 0; k < 10; ++k) fx::PutF(s, k == 6 ? 1.0f : 0.0f); for (int k = 0; k < 10; ++k) fx::PutF(s, 0.0001f); }
        const std::uint32_t ofsA = (std::uint32_t)s.size();
        fx::P32(s, 0); fx::P32(s, 0); fx::P32(s, nf); fx::PutF(s, 30.0f); fx::P32(s, 0);
        const std::uint32_t ofsF = (std::uint32_t)s.size();
        s.append((std::size_t)nf * nj * 10 * 2, '\x11');
        const std::uint32_t vals[27] = {2, (std::uint32_t)s.size(), 0, 0, 124, 0, 0, 0, 0, 0, 0, 0, 0, nj, ofsJ, nj, ofsP, 1, ofsA, nf, nj * 10, ofsF, 0, 0, 0, 0, 0};
        for (int i = 0; i < 27; ++i) fx::IqmSetH(s, i, vals[i]);
        const std::size_t base = g_live.load();
        g_peak.store(base);
        const auto t0 = std::chrono::steady_clock::now();
        ImportedModel m; std::string e;
        const bool ok = Read(s, m, &e);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        const std::size_t extra = g_peak.load() - base;
        std::fprintf(stderr, "  worst allowed file (%zu MB): %s in %.2f s, peak %zu MB beyond the input\n", s.size() >> 20, ok ? "read" : e.c_str(), secs, extra >> 20);
        CHECK(ok && m.clips.size() == 1 && m.clips[0].channels.size() == nj);
        CHECK(extra < kWorstCap);
        CHECK(secs < 20.0);
        // one frame more is refused before any key is made
        fx::IqmSetH(s, fx::kNumFrames, nf + 1);
        fx::Put32(s, ofsA + 8, nf + 1);
        const std::size_t base2 = g_live.load();
        g_peak.store(base2);
        CHECK(!Read(s, m, &e));   // the frame table no longer fits in the file, or the budget is over
        CHECK(g_peak.load() - base2 < 8u * 1024u * 1024u);
    }
    {   // a real file, when one is at hand
        const char* dir = std::getenv("OKAY_E3_SAMPLES");
        const fs::path p = dir ? fs::path(dir) / "mrfixit.iqm" : fs::path();
        if (dir && fs::exists(p)) {
            ImportedModel m; std::string e;
            CHECK(ReadIqmFile(p.string(), m, &e) && fx::Sane(m));
            // the numbers in the file's own header: 1861 vertices, 2988 triangles, 75 joints, 101 frames of one animation, 2 meshes
            std::size_t v = 0; for (const ImportedPart& q : m.parts) v += q.mesh.vertices.size();
            CHECK(m.parts.size() == 2 && v == 1861 && fx::TriCount(m) == 2988 && m.nodes.size() == 76 && m.parts[0].bones.size() == 75);
            { std::size_t full = 0; for (const ImportedChannel& ch : m.clips[0].channels) if (ch.rotation.size() == 101) ++full; CHECK(m.clips.size() == 1 && full > 30); }
            CHECK(m.materials.size() == 2 && m.materials[0].name == "Body.jpg" && m.materials[1].name == "Head.jpg");
            // every skin weight set sums to one
            for (const ImportedPart& q : m.parts) for (const auto& w : q.jointWt) CHECK(fx::Near(w[0] + w[1] + w[2] + w[3], 1.0f, 1e-3f));
            std::fprintf(stderr, "  real file mrfixit.iqm read: %zu vertices, %zu triangles, %zu channels\n", v, fx::TriCount(m), m.clips[0].channels.size());
        } else std::fprintf(stderr, "  real mrfixit.iqm: skipped (set OKAY_E3_SAMPLES to a folder that has it)\n");
    }
    TEST_MAIN_RESULT();
}
