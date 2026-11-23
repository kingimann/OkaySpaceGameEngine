// Hostile input, budgets and mutation fuzz for the own OpenGEX and MDL readers (MDL: Quake 1, Half-Life 1, 3D GameStudio MDL3 to MDL7). Needs only the two reader sources and
// okay/Render/ModelData.hpp, so it builds on its own, e.g.
//   g++ -std=c++17 -O1 -g -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all -DOKAY_STANDALONE_TEST
//       -Iengine/include -Iengine/src/Render -Iengine/src/Core -Itests tests/test_e4_hostile.cpp engine/src/Render/OgexReader.cpp engine/src/Render/MdlReader.cpp -o e4_hostile
// OKAY_READER_FUZZ_ITERS=200000 for a long run (per seed file). Everything here is SYNTHETIC.
//  1. Small hostile files that ask for a lot (deeply nested OpenDDL, a million structures, 16 million values, a mesh used by many
//     nodes, sequences that share animation data, the largest Quake model the caps allow) must finish in a few seconds, be refused
//     (or read) with a reason, and never hold more than kCapBytes at once (a counting operator new measures the live peak).
//  2. A deterministic mutation sweep over every seed file: a read is either sane or a refusal that leaves nothing behind.
#include "test_framework.hpp"
#include <algorithm>
#include "model_fixture_util.hpp"
#include "reader_hostile_common.hpp"
#include "alloc_budget.hpp"
#include "e4_fixtures.hpp"
#include "gs_fixtures.hpp"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

using namespace okay;

// The most memory any case below may hold at once, on top of the file text itself. The sanitizer build is slower and bigger.
static constexpr std::size_t kCapBytes = 640u * 1024u * 1024u;
static constexpr double kMaxSeconds = budget::kSanitizerBuild ? 40.0 : 8.0;

using budget::Now;
using budget::Run;
using hostile::HasNote;
template <class F> static Run Measure(const char* what, F&& f) { return budget::Measure(what, f, kCapBytes, kMaxSeconds); }
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
    {   // a skinned mesh with 4,096 bone references used by 50,000 nodes: the bone list was rebuilt and copied per node (20 GB)
        std::string refs;
        for (int i = 0; i < 4095; ++i) refs += "$nobody,";
        refs += "$nobody";
        const std::string t = "GeometryObject $g {Mesh {VertexArray (attrib = \"position\") {float[3] {{0,0,0},{1,0,0},{0,1,0}}} IndexArray {unsigned_int8[3] {{0,1,2}}}\n"
                              " Skin {Skeleton {BoneRefArray {ref {" + refs + "}}} BoneCountArray {unsigned_int8 {1,1,1}} BoneIndexArray {unsigned_int16 {0,1,2}} BoneWeightArray {float {1,1,1}}}}}\n"
                              + Repeat("GeometryNode {ObjectRef {ref {$g}}}\n", 50000);
        Run r = OgexRun("skinned mesh, 4,096 bones, 50,000 nodes", t);
        CHECK(!r.ok && r.err.find("bones x placements") != std::string::npos);
        CHECK(r.peakMB < 300);
    }
    {   // a mesh of 500,000 vertices and one triangle used by 100,000 nodes: a vertex-sized table was allocated per node
        const std::string t = "GeometryObject $g {Mesh {VertexArray (attrib = \"position\") {float[3] {" + Repeat("{0,0,0},", 499999) + "{0,0,0}}} IndexArray {unsigned_int32[3] {{0,1,2}}}}}\n" + Repeat("GeometryNode {ObjectRef {ref {$g}}}\n", 100000);
        Run r = OgexRun("500,000-vertex mesh, 100,000 nodes", t);
        CHECK(r.ok && r.m.parts.size() == 100000);
    }
    {   // 20,000 transform structures on one node, each animated by its own track with its own key times: quadratic lookups
        std::string t = "Node {\n";
        for (int i = 0; i < 20000; ++i) t += "Translation %t" + std::to_string(i) + " {float[3] {{0,0,0}}}\n";
        t += " Animation {\n";
        for (int i = 0; i < 20000; ++i) t += "Track (target = %t" + std::to_string(i) + ") {Time {Key {float {" + std::to_string(2 * i) + "," + std::to_string(2 * i + 1) + "}}} Value {Key {float[3] {{0,0,0},{1,1,1}}}}}\n";
        t += "}}\nGeometryObject $g {Mesh {VertexArray (attrib = \"position\") {float[3] {{0,0,0},{1,0,0},{0,1,0}}} IndexArray {unsigned_int8[3] {{0,1,2}}}}}\nGeometryNode {ObjectRef {ref {$g}}}";
        Run r = OgexRun("20,000 transforms, 20,000 tracks", t);
        CHECK(r.ok || !r.err.empty());
    }
    {   // 256 transform structures and one track of 300,000 keys: 77 million samples of 256 transforms
        std::string times, vals;
        for (int i = 0; i < 300000; ++i) { times += std::to_string(i) + ","; vals += "{0,0,0},"; }
        std::string t = "Node {\n";
        for (int i = 0; i < 256; ++i) t += "Translation %t" + std::to_string(i) + " {float[3] {{0,0,0}}}\n";
        t += " Animation {Track (target = %t0) {Time {Key {float {" + times + "0}}} Value {Key {float[3] {" + vals + "{0,0,0}}}}}}}\n";
        t += "GeometryObject $g {Mesh {VertexArray (attrib = \"position\") {float[3] {{0,0,0},{1,0,0},{0,1,0}}} IndexArray {unsigned_int8[3] {{0,1,2}}}}}\nGeometryNode {ObjectRef {ref {$g}}}";
        Run r = OgexRun("300,000 keys x 256 transforms", t);
        CHECK(!r.ok && r.err.find("samples x transforms") != std::string::npos);
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
    {   // 1,024 body parts naming one model of 4,096 meshes that all name one command list of 32,767 vertices with bad indices: 1.4e11 steps
        // from about 400 KB if nothing bounds the work
        Run r = MdlRun("shared offsets and bad indices", e4::HlStress(1, 1024, 4096, 32767, 60000));
        CHECK(!r.ok && r.err.find("budget") != std::string::npos);
    }
    {   // 4,096 meshes x 1,024 bones: each part would carry its own copy of the bone list (4,000,000 entries)
        std::string t = e4::HlStress(1024, 1, 4096, 3, 0);
        Run r = MdlRun("4,096 parts x 1,024 bones", t);
        CHECK(!r.ok && r.err.find("parts x bones") != std::string::npos);
        CHECK(r.peakMB < 200);
        Run ok = MdlRun("500 parts x 1,024 bones", e4::HlStress(1024, 1, 500, 3, 0));
        CHECK(ok.ok && ok.m.parts.size() == 500);
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

// A finite node matrix whose column length overflows a float when squared (one element 0x60000000, about 3.7e19; found by
// the mutation fuzz seeded with the real Example.ogex): the read must refuse it or give finite numbers, never inf / NaN.
static void OgexHugeMatrix() {
    std::string s = e4::OgexQuad();
    const std::string from = "Translation (kind = \"xyz\") {float[3] {{4, 5, 6}}}";
    const std::size_t at = s.find(from);
    CHECK(at != std::string::npos);
    if (at == std::string::npos) return;
    s.replace(at, from.size(), "Transform {float[16] {{1, 0x60000000, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 4, 5, 6, 1}}}");
    ImportedModel m; std::string err;
    const bool ok = ReadOgexBuffer(s, "", m, &err);
    std::fprintf(stderr, "  huge node matrix: %s%s\n", ok ? "read" : "refused: ", ok ? "" : err.c_str());
    CHECK(ok ? hostile::Sane(m) : !err.empty());
}

// 3D GameStudio models: the largest MDL3 the caps allow (1,000,000 vertices, 2,000,000 triangles) and an MDL7 of 65,536 groups.
static void GameStudioBudgets() {
    {
        std::string o;
        o += "MDL3"; P32(o, 0);
        for (float f : {1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f}) { std::uint32_t u; std::memcpy(&u, &f, 4); P32(o, (std::int32_t)u); }
        P32(o, 0); P32(o, 0); P32(o, 0); P32(o, 0);
        const std::int32_t nv = 1000000, nt = 2000000;
        P32(o, 0); P32(o, 0); P32(o, 0); P32(o, nv); P32(o, nt); P32(o, 1); P32(o, 0); P32(o, 0); P32(o, 0);
        for (std::int32_t t = 0; t < nt; ++t) { const std::int32_t a = (t * 7) % (nv - 2); for (std::int32_t v : {a, a + 1, a + 2}) { o.push_back((char)(v & 255)); o.push_back((char)((v >> 8) & 255)); } o.append(6, '\0'); }
        P32(o, 0); o.append(8 + 16, '\0');
        for (std::int32_t v = 0; v < nv; ++v) { o.push_back((char)(v & 255)); o.push_back((char)((v >> 8) & 255)); o.push_back((char)((v >> 16) & 255)); o.push_back(0); }
        const Run r = MdlRun("MDL3 at the vertex and triangle caps", o);
        CHECK(r.ok || !r.err.empty());
    }
    {
        gs::Mdl7Spec sp;
        const std::string one = gs::Mdl7(sp);
        // repeat group 1 (the small one) until there are 65,536 groups
        const std::size_t g1 = 48 + (std::size_t)(std::uint8_t)one[52] + ((std::size_t)(std::uint8_t)one[53] << 8);
        std::string g = one.substr(0, g1);
        const std::string grp = one.substr(g1);
        for (int i = 0; i < 65535; ++i) g += grp;
        gs::Put32At(g, gs::mdl7off::kGroups, 65536);
        gs::Put32At(g, gs::mdl7off::kDataSize, (std::uint32_t)g.size());
        const Run r = MdlRun("MDL7 with 65,536 groups", g);
        CHECK(r.ok);
        gs::Put32At(g, gs::mdl7off::kGroups, 65537);
        ImportedModel m; std::string err;
        CHECK(!ReadMdlBuffer(g, "", m, &err) && err.find("group count") != std::string::npos);
    }
}

int main() {
    RUN_SUITE("e4_hostile");
    GameStudioBudgets();
    OgexHugeMatrix();
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
    { gs::Mdl5Spec sp; Sweep("gamestudio mdl3", false, gs::Mdl5(sp), iters); }
    { gs::Mdl5Spec sp; sp.version = '4'; sp.wordFrames = true; Sweep("gamestudio mdl4, word frames", false, gs::Mdl5(sp), iters / 2); }
    { gs::Mdl5Spec sp; sp.version = '5'; Sweep("gamestudio mdl5", false, gs::Mdl5(sp), iters); }
    Sweep("gamestudio mdl7", false, gs::Mdl7(), iters);
    { gs::Mdl7Spec sp; sp.bones = 1; sp.animatedFrameVerts = 2; Sweep("gamestudio mdl7, bones and frames", false, gs::Mdl7(sp), iters / 2); }
    // With OKAY_MODEL_CORPUS set (docs/content/model_corpus.md: the "test" folder of an Assimp 5.4.3 checkout), real files are
    // read whole, then fuzzed the same way (a smaller sweep: they are larger).
    if (const char* c = std::getenv("OKAY_MODEL_CORPUS")) {
        const std::string root = std::string(c) + "/";
        const struct { const char* path; bool ogex; } real[] = {
            {"models/OpenGEX/Example.ogex", true}, {"models/OpenGEX/animation_example.ogex", true}, {"models/OpenGEX/collada.ogex", true},
            {"models/MDL/MDL (HL1)/man.mdl", false}, {"models/MDL/MDL (HL1)/multiple_roots.mdl", false},
            {"models/MDL/MDL (HL1)/chrome_sphere.mdl", false}, {"models-nonbsd/MDL/IDPO (Quake1)/steg.mdl", false},
            {"models/MDL/MDL5 (3DGS A5)/PhosphoricAcid_MDl5.mdl", false}, {"models/MDL/MDL7 (3DGS A7)/PhosphoricAcid_MDl7.mdl", false},
            {"models/MDL/MDL7 (3DGS A7)/Sphere_DiffPinkBlueSpec_Alpha90.mdl", false}, {"models-nonbsd/MDL/MDL7 (3DGS A7)/broadleavedtreeD.mdl", false}};
        for (const auto& r : real) {
            const std::string bytes = hostile::Slurp(root + r.path);
            if (bytes.empty()) { std::fprintf(stderr, "  real file %s: not found, skipped\n", r.path); continue; }
            ImportedModel m; std::string err;
            const bool ok = r.ogex ? ReadOgexBuffer(bytes, "", m, &err) : ReadMdlBuffer(bytes, "", m, &err);
            if (!ok) std::fprintf(stderr, "  real file %s refused: %s\n", r.path, err.c_str());
            CHECK(ok && hostile::Sane(m));
            for (std::size_t cut = 0; cut < bytes.size(); cut += 1 + bytes.size() / 512) Try(r.ogex, bytes.substr(0, cut));   // prefixes
            Sweep(r.path, r.ogex, bytes, std::max(200L, iters / 4));
        }
    }
    TEST_MAIN_RESULT();
}
