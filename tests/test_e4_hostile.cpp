// Hostile input, budgets and mutation fuzz for the own OpenGEX and MDL readers. Needs only the two reader sources and
// okay/Render/ModelData.hpp, so it builds on its own, e.g.
//   g++ -std=c++17 -O1 -g -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all -DOKAY_STANDALONE_TEST
//       -Iengine/include -Iengine/src/Render -Iengine/src/Core -Itests tests/test_e4_hostile.cpp engine/src/Render/OgexReader.cpp engine/src/Render/MdlReader.cpp -o e4_hostile
// OKAY_READER_FUZZ_ITERS=200000 for a long run (per seed file). Everything here is SYNTHETIC.
//  1. Small hostile files that ask for a lot (deeply nested OpenDDL, a million structures, 16 million values, a mesh used by many
//     nodes, sequences that share animation data, the largest Quake model the caps allow) must finish in a few seconds, be refused
//     (or read) with a reason, and never hold more than kCapBytes at once (a counting operator new measures the live peak).
//  2. A deterministic mutation sweep over every seed file: a read is either sane or a refusal that leaves nothing behind.
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include "reader_hostile_common.hpp"
#include "e4_fixtures.hpp"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <new>
#include <string>

using namespace okay;

// ---- a counting allocator: bytes live now and the peak since the last reset ----------------------------------------------
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
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { try { return CountedAlloc(n); } catch (...) { return nullptr; } }   // std::stable_sort takes its buffer this way
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { try { return CountedAlloc(n); } catch (...) { return nullptr; } }
void operator delete(void* p, const std::nothrow_t&) noexcept { CountedFree(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { CountedFree(p); }
void operator delete(void* p) noexcept { CountedFree(p); }
void operator delete[](void* p) noexcept { CountedFree(p); }
void operator delete(void* p, std::size_t) noexcept { CountedFree(p); }
void operator delete[](void* p, std::size_t) noexcept { CountedFree(p); }

// The most memory any case below may hold at once, on top of the file text itself. The sanitizer build is slower and bigger.
static constexpr std::size_t kCapBytes = 640u * 1024u * 1024u;
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_UNDEFINED__)
static constexpr double kMaxSeconds = 40.0;
#else
static constexpr double kMaxSeconds = 8.0;
#endif

static double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
static bool HasNote(const ImportedModel& m, const char* what) { for (const std::string& n : m.notes) if (n.find(what) != std::string::npos) return true; return false; }

struct Run { bool ok; double secs; std::size_t peakMB; std::string err; ImportedModel m; };
template <class F> static Run Measure(const char* what, F&& f) {
    Run r;
    const std::size_t base = g_live.load();
    g_peak.store(base);
    const double t = Now();
    r.ok = f(r.m, r.err);
    r.secs = Now() - t;
    const std::size_t extra = g_peak.load() - base;
    r.peakMB = extra >> 20;
    std::fprintf(stderr, "  %s: %s in %.2f s, peak %zu MB%s%s\n", what, r.ok ? "read" : "refused", r.secs, r.peakMB, r.ok ? "" : ": ", r.ok ? "" : r.err.c_str());
    CHECK(r.secs < kMaxSeconds);
    CHECK(extra < kCapBytes);
    CHECK(r.ok ? hostile::Sane(r.m) : (r.m.parts.empty() && r.m.nodes.empty() && !r.err.empty()));
    return r;
}
static Run OgexRun(const char* what, const std::string& t, const std::string& dir = "") {
    return Measure(what, [&](ImportedModel& m, std::string& e) { return ReadOgexBuffer(t, dir, m, &e); });
}
static Run MdlRun(const char* what, const std::string& t) {
    return Measure(what, [&](ImportedModel& m, std::string& e) { return ReadMdlBuffer(t, "", m, &e); });
}
static std::string Repeat(const std::string& s, std::size_t n) { std::string r; r.reserve(s.size() * n); for (std::size_t i = 0; i < n; ++i) r += s; return r; }
static void P32(std::string& s, std::int32_t v) { for (int i = 0; i < 4; ++i) s.push_back((char)(((std::uint32_t)v >> (8 * i)) & 255)); }

static void OgexBudgets() {
    const std::string geo = "GeometryObject $g {Mesh {VertexArray (attrib = \"position\") {float[3] {{0,0,0},{1,0,0},{0,1,0}}} IndexArray {unsigned_int32[3] {{0,1,2}}}}}\n";
    {   // 1,000,000 open structures: no recursion, refused at the nesting depth
        Run r = OgexRun("1,000,000 open structures", Repeat("N{", 1000000));
        CHECK(!r.ok && r.err.find("nested deeper") != std::string::npos);
    }
    {   // 1,100,000 empty structures: over the structure budget
        Run r = OgexRun("1,100,000 structures", Repeat("Node{}", 1100000));
        CHECK(!r.ok && r.err.find("too many structures") != std::string::npos);
    }
    {   // a node tree of 250,000 nodes in a chain deeper than the depth budget, and a flat one over the node budget
        Run r = OgexRun("250,000 nested nodes", Repeat("Node{", 250000) + Repeat("}", 250000));
        CHECK(!r.ok && r.err.find("nested deeper") != std::string::npos);
        Run f = OgexRun("200,001 sibling nodes", Repeat("Node{}", 200001) + geo + "GeometryNode {ObjectRef {ref {$g}}}");
        CHECK(!f.ok && f.err.find("nodes (budget)") != std::string::npos);
    }
    {   // 16,000,001 numbers in one data list
        Run r = OgexRun("16,000,001 values", "Node { float {" + Repeat("0,", 16000000) + "0} }");
        CHECK(!r.ok && r.err.find("too many data values") != std::string::npos);
    }
    {   // an index array of 12.3 million entries: over the 4,000,000 triangle budget
        Run r = OgexRun("12,300,000 indices", "GeometryObject $g {Mesh {VertexArray (attrib = \"position\") {float[3] {{0,0,0},{1,0,0},{0,1,0}}} IndexArray {unsigned_int8 {" + Repeat("0,", 12300000) + "0}}}}\nGeometryNode {ObjectRef {ref {$g}}}");
        CHECK(!r.ok && (r.err.find("triangles") != std::string::npos));
    }
    {   // 2,100,000 vertices over the vertex budget
        Run r = OgexRun("2,100,000 vertices", "GeometryObject $g {Mesh {VertexArray (attrib = \"position\") {float[3] {" + Repeat("{0,0,0},", 2100000) + "{0,0,0}}} }}\nGeometryNode {ObjectRef {ref {$g}}}");
        CHECK(!r.ok && r.err.find("vertices") != std::string::npos);
    }
    {   // one mesh of 100,000 triangles named by 50 nodes: 5,000,000 placed triangles, over the budget (the file is about 2 MB)
        std::string idx = "unsigned_int8[3] {";
        for (int i = 0; i < 100000; ++i) idx += std::string(i ? "," : "") + "{0,1,2}";
        idx += "}";
        const std::string t = "GeometryObject $g {Mesh {VertexArray (attrib = \"position\") {float[3] {{0,0,0},{1,0,0},{0,1,0}}} IndexArray {" + idx + "}}}\n" + Repeat("GeometryNode {ObjectRef {ref {$g}}}\n", 50);
        Run r = OgexRun("one mesh, 50 nodes", t);
        CHECK(!r.ok && r.err.find("placed in the scene") != std::string::npos);
        Run ok = OgexRun("one mesh, 30 nodes", "GeometryObject $g {Mesh {VertexArray (attrib = \"position\") {float[3] {{0,0,0},{1,0,0},{0,1,0}}} IndexArray {" + idx + "}}}\n" + Repeat("GeometryNode {ObjectRef {ref {$g}}}\n", 30));
        CHECK(ok.ok && ok.m.parts.size() == 30);
    }
    {   // a million references to bones that do not exist, and a skin with a count of 4294967295 per vertex
        std::string t = "GeometryObject $g {Mesh {VertexArray (attrib = \"position\") {float[3] {{0,0,0},{1,0,0},{0,1,0}}} IndexArray {unsigned_int8[3] {{0,1,2}}}\n"
                        " Skin {BoneRefArray {ref {" + Repeat("$nobody,", 4000) + "$nobody}} BoneCountArray {unsigned_int32 {4294967295, 4294967295, 4294967295}} BoneIndexArray {unsigned_int32 {0}} BoneWeightArray {float {1}}}}}\n"
                        "GeometryNode {ObjectRef {ref {$g}}}";
        Run r = OgexRun("hostile skin", t);
        CHECK(r.ok && r.m.parts[0].bones.empty());
    }
    {   // 4,000 distinct animation clips each with a track, and 1,000,000 tracks
        std::string t = "Node {Translation %t {float[3] {{0,0,0}}}\n";
        for (int c = 0; c < 4100; ++c) t += "Animation (clip = " + std::to_string(c) + ") {Track (target = %t) {Time {Key {float {0}}} Value {Key {float[3] {{1,2,3}}}}}}\n";
        t += "}\n" + std::string("GeometryObject $g {Mesh {VertexArray (attrib = \"position\") {float[3] {{0,0,0},{1,0,0},{0,1,0}}}}}\nGeometryNode {ObjectRef {ref {$g}}}");
        Run r = OgexRun("4,100 clips", t);
        CHECK(r.ok && r.m.clips.size() <= 4096 && HasNote(r.m, "more than 4096 clips"));
    }
    {   // an animation with 4,100,000 keys on one track (a one-axis scale: two numbers a key, so the value budget is not reached first)
        const std::string times = Repeat("0,", 4100000) + "0", vals = Repeat("1,", 4100000) + "1";
        Run r = OgexRun("4,100,001 keys on one track", "Node {Scale %s (kind = \"x\") {float {1}}\n"
                        " Animation {Track (target = %s) {Time {Key {float {" + times + "}}} Value {Key {float {" + vals + "}}}}}}\n" + geo + "GeometryNode {ObjectRef {ref {$g}}}");
        CHECK(!r.ok && r.err.find("animation keys") != std::string::npos);
    }
    {   // numbers at and past what a float can hold, and a texture path that is 3 MB long
        Run r = OgexRun("huge coordinates", "GeometryObject $g {Mesh {VertexArray (attrib = \"position\") {float[3] {{1e15,0,0},{0,1e15,0},{0,0,1e15}}} IndexArray {unsigned_int8[3] {{0,1,2}}}}}\nGeometryNode {ObjectRef {ref {$g}} Scale {float[3] {{1e15,1e15,1e15}}} }");
        CHECK(!r.ok || hostile::Sane(r.m));
        Run p = OgexRun("3 MB texture path", "Material $m {Texture (attrib = \"diffuse\") {string {\"" + std::string(3000000, 'a') + "\"}}}\n" + geo + "GeometryNode {ObjectRef {ref {$g}}}", "dir/");
        CHECK(p.ok || !p.err.empty());
    }
}

static void MdlBudgets() {
    {   // the largest Quake model the caps allow: 200,000 vertices, 400,000 triangles
        std::string f = e4::QuakeMdl();
        e4::QuakeOffsets o;
        e4::QuakeMdl(&o);
        std::string b = f.substr(0, o.skins);
        b.replace(60, 4, std::string((const char*)"\x40\x0D\x03\x00", 4));   // numverts 200000
        b.replace(64, 4, std::string((const char*)"\x80\x1A\x06\x00", 4));   // numtris 400000
        b.replace(68, 4, std::string((const char*)"\x01\x00\x00\x00", 4));
        P32(b, 0); b.append(8, 'x');
        for (int v = 0; v < 200000; ++v) { P32(b, 0); P32(b, v % 4); P32(b, v % 2); }
        for (int t = 0; t < 400000; ++t) { P32(b, 1); P32(b, t % 200000); P32(b, (t * 7 + 1) % 200000); P32(b, (t * 13 + 2) % 200000); }
        P32(b, 0); b.append(8, 0); b.append(16, 'f');
        for (int v = 0; v < 200000; ++v) { b.push_back((char)(v & 255)); b.push_back((char)((v >> 8) & 255)); b.push_back((char)((v * 3) & 255)); b.push_back(0); }
        Run r = MdlRun("largest Quake model", b);
        CHECK(r.ok && r.m.parts.size() == 1 && !r.m.parts[0].mesh.triangles.empty());
    }
    {   // counts at and past the caps, with tiny files
        std::string f = e4::QuakeMdl();
        for (int field : {48, 60, 64, 68}) for (std::int32_t v : {200001, 400001, 0x7FFFFFFF, (std::int32_t)0xFFFFFFFFu}) {
            std::string t = f;
            for (int i = 0; i < 4; ++i) t[(std::size_t)field + (std::size_t)i] = (char)(((std::uint32_t)v >> (8 * i)) & 255);
            MdlRun("quake count past caps", t);
        }
    }
    e4::HlOffsets o;
    const std::string hl = e4::HalfLifeMdl(1.0f, &o);
    {   // 4,096 sequences that all name the same animation data and each claim 65,536 frames: refused by the key budget, not by running out of memory
        std::string b = hl.substr(0, o.seq);
        for (int s = 0; s < 4096; ++s) {
            std::string e = hl.substr(o.seq, 176);
            e[56] = 0; e[57] = 0; e[58] = 1; e[59] = 0;   // numframes 65536
            const std::int32_t anim = (std::int32_t)(o.seq + 4096 * 176);
            for (int i = 0; i < 4; ++i) e[124 + (std::size_t)i] = (char)(((std::uint32_t)anim >> (8 * i)) & 255);
            b += e;
        }
        const std::size_t animAt = b.size();
        b += hl.substr(o.anim, 12) + hl.substr(o.anim + 12, 12);
        // bone 1's channel 5 offset points at 258 runs of 255 frames (valid 1): 65,790 frames
        const std::size_t rle = b.size();
        for (int r = 0; r < 258; ++r) { b.push_back((char)1); b.push_back((char)255); b.push_back((char)(r & 127)); b.push_back(0); }
        const std::uint16_t off = (std::uint16_t)(rle - (animAt + 12));
        b[animAt + 12 + 10] = (char)(off & 255); b[animAt + 12 + 11] = (char)(off >> 8);
        // header: numseq 4096
        b[164] = 0; b[165] = 0x10;
        Run r = MdlRun("4,096 sequences of 65,536 frames", b);
        CHECK(!r.ok && r.err.find("animation keys") != std::string::npos);
    }
    {   // triangle command lists far over the triangle budget: 70 strips of 32,767 vertices
        std::string b = hl.substr(0, o.cmds);
        for (int s = 0; s < 70; ++s) {
            b.push_back((char)0xFF); b.push_back((char)0x7F);
            for (int i = 0; i < 32767; ++i) { b.push_back((char)(i % 3)); b.push_back(0); b.push_back(0); b.push_back(0); b.push_back((char)(i & 255)); b.push_back((char)((i >> 8) & 127)); b.push_back((char)(s & 255)); b.push_back(0); }
        }
        b.push_back(0); b.push_back(0);
        b += hl.substr(o.seq);   // the rest (its offsets are now wrong, which a refusal or a skipped sequence must handle)
        Run r = MdlRun("70 strips of 32,767 vertices", b);
        CHECK(!r.ok);
    }
    {   // 4,096 meshes pointing at one command list, each welded separately
        std::string b = hl;
        for (int i = 0; i < 4; ++i) b[o.model + 72 + (std::size_t)i] = (char)(i == 1 ? 0x10 : 0);   // nummesh 4096 (0x1000)
        // the mesh table is 20 bytes each: point it at a region of the file that holds zeros or the sequence table; every mesh then has its own bad data
        Run r = MdlRun("4,096 meshes", b);
        CHECK(r.ok || !r.err.empty());
    }
}

// A read must either give a sane model or fail with a message and leave nothing behind.
static long g_ok = 0, g_refused = 0;
static void Try(bool ogex, const std::string& bytes) {
    ImportedModel m; std::string err;
    const bool ok = ogex ? ReadOgexBuffer(bytes, "", m, &err) : ReadMdlBuffer(bytes, "", m, &err);
    if (ok) { ++g_ok; CHECK(hostile::Sane(m) && !m.nodes.empty() && !m.parts.empty()); }
    else { ++g_refused; CHECK(!err.empty() && m.parts.empty() && m.nodes.empty() && m.clips.empty() && m.materials.empty()); }
}
static void Sweep(const char* name, bool ogex, const std::string& seed, long iters) {
    g_ok = g_refused = 0;
    const double t0 = Now();
    // ogex seeds are text: edit with characters that matter to the grammar
    const std::string alphabet = "{}[](),=$%\"' \n0123456789.-+eExXabfnultr;/*\\";
    for (long i = 0; i < iters; ++i) {
        std::string s = seed;
        const int edits = 1 + (int)(hostile::Rnd() % 3);
        for (int k = 0; k < edits; ++k) { if (ogex) hostile::MutateText(s, alphabet); else hostile::MutateBinary(s); }
        Try(ogex, s);
    }
    std::fprintf(stderr, "  %s: %ld mutations in %.1f s: %ld read, %ld refused\n", name, iters, Now() - t0, g_ok, g_refused);
    CHECK(g_ok > 0 && g_refused > 0);   // the sweep must reach both kinds of result, or it tests nothing
}

int main() {
    RUN_SUITE("e4_hostile");
    OgexBudgets();
    MdlBudgets();
    const long iters = hostile::Iters(4000);
    std::fprintf(stderr, "  mutation iterations per seed: %ld (OKAY_READER_FUZZ_ITERS)\n", iters);
    Sweep("ogex quad", true, e4::OgexQuad(), iters);
    Sweep("ogex skin", true, e4::OgexSkin(), iters);
    Sweep("ogex anim", true, e4::OgexAnim(), iters);
    Sweep("quake mdl", false, e4::QuakeMdl(), iters);
    Sweep("quake mdl, group first", false, e4::QuakeMdl(nullptr, true), iters / 2);
    Sweep("half-life mdl", false, e4::HalfLifeMdl(), iters);
    Sweep("half-life mdl, flipped normal", false, e4::HalfLifeMdl(-1.0f), iters / 2);
    TEST_MAIN_RESULT();
}
