#include "test_framework.hpp"
#include <algorithm>
#include "okay/Render/FbxReader.hpp"
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// Hostile-input tests for okay::fbx. Pure standard C++ (links no engine code), so CI can also build
// it with -fsanitize=address,undefined and run the bigger mutation fuzz:
//   OKAY_FBX_FUZZ_ITERS=20000 ./test_fbx_hostile
// Nothing here may crash, hang or allocate without bound; bad files must come back as "false".
using namespace okay;
namespace fs = std::filesystem;
typedef std::vector<std::uint8_t> Bytes;

static Bytes ReadAll(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return Bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
static void U32(Bytes& b, std::size_t at, std::uint32_t v) { std::memcpy(&b[at], &v, 4); }
static void Put32(Bytes& b, std::uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back((std::uint8_t)(v >> (8 * i))); }

static fbx::BuildOptions Tight() {   // tiny limits: a hostile file may not make the fuzz loop slow
    fbx::BuildOptions o;
    o.limits.maxArrayBytes = 16u << 20; o.limits.maxCorners = 1u << 20; o.limits.maxElements = 1u << 20;
    o.limits.maxNodes = 1u << 16; o.limits.maxBakedSamples = 2000;
    return o;
}
static bool Load(const Bytes& b, const fbx::BuildOptions& o = Tight(), std::string* err = nullptr) {
    fbx::Scene s;
    return fbx::LoadMemory(b.data(), b.size(), s, err, o);
}

static Bytes Header(std::uint32_t version = 7400) {
    const char* m = "Kaydara FBX Binary  ";
    Bytes b(m, m + 20); b.push_back(0); b.push_back(0x1A); b.push_back(0);
    Put32(b, version);
    return b;
}
// One binary record header (7.4 layout) + name, no properties.
static void Rec(Bytes& b, std::uint32_t end, std::uint32_t nprops, std::uint32_t plen, const char* name) {
    Put32(b, end); Put32(b, nprops); Put32(b, plen); b.push_back((std::uint8_t)std::strlen(name));
    b.insert(b.end(), name, name + std::strlen(name));
}

struct Rng { std::uint64_t s; std::uint32_t Next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return (std::uint32_t)(s >> 11); } };

static std::vector<std::string> Fixtures() {
    std::vector<std::string> v;
    for (auto& e : fs::directory_iterator(fs::path(OKAY_SOURCE_DIR) / "tests" / "assets" / "fbx")) if (e.path().extension() == ".fbx") v.push_back(e.path().string());
    std::sort(v.begin(), v.end());
    return v;
}

static void Truncation() {
    RUN_SUITE("truncated files");
    for (const std::string& f : Fixtures()) {
        const Bytes full = ReadAll(f);
        CHECK(Load(full, fbx::BuildOptions()));
        const bool ascii = full.size() > 4 && std::memcmp(full.data(), "; FBX", 5) == 0;
        std::size_t failedPrefixes = 0, tried = 0;
        for (std::size_t n = 0; n < full.size(); n += (full.size() > 4000 ? 7 : 1)) {   // every length for tiny files
            Bytes cut(full.begin(), full.begin() + (std::ptrdiff_t)n);
            ++tried;
            if (!Load(cut)) ++failedPrefixes;
        }
        // A cut binary file must be refused (the top-level records run past the end), apart from cuts
        // that land exactly between records. ASCII cuts may legitimately parse as a shorter document.
        if (!ascii) CHECK(failedPrefixes * 10 >= tried * 9);
    }
}

static void CraftedBinary() {
    RUN_SUITE("crafted binary: bad offsets, counts and sizes");
    // end offset past the end of the file
    { Bytes b = Header(); Rec(b, 0x7FFFFFF0u, 0, 0, "Objects"); CHECK(!Load(b)); }
    // end offset before the header finishes
    { Bytes b = Header(); Rec(b, 28, 0, 0, "Objects"); CHECK(!Load(b)); }
    // property count larger than the bytes available
    { Bytes b = Header(); Rec(b, 0, 0xFFFFFFFFu, 8, "Objects"); U32(b, 27, (std::uint32_t)b.size() + 8); b.resize(b.size() + 8); CHECK(!Load(b)); }
    // property list length longer than the record
    { Bytes b = Header(); Rec(b, 0, 1, 0xFFFFFFF0u, "X"); U32(b, 27, (std::uint32_t)b.size()); CHECK(!Load(b)); }
    // string property claiming 4 GB
    {
        Bytes b = Header(); Rec(b, 0, 1, 5, "X"); b.push_back('S'); Put32(b, 0xFFFFFFFFu); U32(b, 27, (std::uint32_t)b.size()); CHECK(!Load(b));
    }
    // array property claiming 4G doubles, raw
    {
        Bytes b = Header(); Rec(b, 0, 1, 13, "X"); b.push_back('d'); Put32(b, 0xFFFFFFFFu); Put32(b, 0); Put32(b, 8); U32(b, 27, (std::uint32_t)b.size() + 8); b.resize(b.size() + 8);
        CHECK(!Load(b));
    }
    // array property claiming 64M doubles, zlib, with a tiny stream
    {
        Bytes b = Header(); Rec(b, 0, 1, 13 + 8, "X"); b.push_back('d'); Put32(b, 1u << 26); Put32(b, 1); Put32(b, 8);
        const std::uint8_t z[8] = {0x78, 0x01, 0x03, 0x00, 0, 0, 0, 1}; b.insert(b.end(), z, z + 8);
        U32(b, 27, (std::uint32_t)b.size()); CHECK(!Load(b));
    }
    // unknown property type / encoding
    { Bytes b = Header(); Rec(b, 0, 1, 1, "X"); b.push_back('Q'); U32(b, 27, (std::uint32_t)b.size()); CHECK(!Load(b)); }
    { Bytes b = Header(); Rec(b, 0, 1, 13, "X"); b.push_back('d'); Put32(b, 0); Put32(b, 7); Put32(b, 0); U32(b, 27, (std::uint32_t)b.size()); CHECK(!Load(b)); }
    // wrong versions and magic
    { Bytes b = Header(100); CHECK(!Load(b)); }
    { Bytes b = Header(0xFFFFFFFFu); CHECK(!Load(b)); }
    { Bytes b(5, 0); CHECK(!Load(b)); Bytes e; CHECK(!Load(e)); }
    // deep nesting: 200000 chained records, no recursion blow-up
    {
        const std::uint32_t N = 200000;
        Bytes b = Header();
        const std::uint32_t total = 27 + 14 * N + 13 * (N - 1);
        for (std::uint32_t i = 0; i < N; ++i) Rec(b, total - 13 * i, 0, 0, "N");
        b.resize(b.size() + 13 * (N - 1), 0);
        std::string err; CHECK(!Load(b, Tight(), &err)); CHECK(err.find("deep") != std::string::npos);
    }
    // many empty records (element budget)
    {
        Bytes b = Header();
        fbx::BuildOptions o = Tight(); o.limits.maxElements = 1000;
        for (int i = 0; i < 5000; ++i) { Rec(b, (std::uint32_t)(b.size() + 14), 0, 0, "N"); }
        CHECK(!Load(b, o));
    }
}

// deflate stream: a literal 'a' then `n` copies of "length 258, distance 1" (fixed Huffman) -> 258n bytes
static Bytes Bomb(std::uint32_t n) {
    Bytes out = {0x78, 0x01};
    std::uint64_t acc = 0; int nb = 0;
    auto bitsLsb = [&](std::uint32_t v, int c) { acc |= (std::uint64_t)v << nb; nb += c; while (nb >= 8) { out.push_back((std::uint8_t)acc); acc >>= 8; nb -= 8; } };
    auto codeMsb = [&](std::uint32_t code, int len) { for (int i = len - 1; i >= 0; --i) bitsLsb((code >> i) & 1u, 1); };
    bitsLsb(1, 1); bitsLsb(1, 2);
    codeMsb(0x30 + 'a', 8);
    for (std::uint32_t i = 0; i < n; ++i) { codeMsb(0xC0 + 5, 8); codeMsb(0, 5); }
    codeMsb(0, 7);
    if (nb) bitsLsb(0, 8 - nb);
    out.insert(out.end(), {0, 0, 0, 0});
    return out;
}

static void Bombs() {
    RUN_SUITE("decompression bombs");
    const Bytes z = Bomb(4u << 20);                        // ~7 MB of stream -> ~1 GB if let loose
    std::vector<std::uint8_t> out;
    const auto t0 = std::chrono::steady_clock::now();
    CHECK(!fbx::Inflate(z.data(), z.size(), out, 1u << 20));   // stops at the limit
    CHECK(out.size() <= (1u << 20));
    const Bytes tiny = Bomb(100);
    CHECK(fbx::Inflate(tiny.data(), tiny.size(), out, 1u << 20) && out.size() == 1 + 100 * 258);
    // as an FBX array claiming 8 bytes but carrying a bigger stream
    {
        Bytes b = Header(); Rec(b, 0, 1, 13 + (std::uint32_t)tiny.size(), "X"); b.push_back('d'); Put32(b, 1); Put32(b, 1); Put32(b, (std::uint32_t)tiny.size());
        b.insert(b.end(), tiny.begin(), tiny.end()); U32(b, 27, (std::uint32_t)b.size());
        CHECK(!Load(b));
    }
    // the whole-file array budget is shared: many tiny arrays add up
    {
        fbx::BuildOptions o = Tight(); o.limits.maxArrayBytes = 4096;
        Bytes b = Header();
        for (int i = 0; i < 20; ++i) {
            const std::uint32_t start = (std::uint32_t)b.size();
            Rec(b, 0, 1, 13 + 800, "X"); b.push_back('d'); Put32(b, 100); Put32(b, 0); Put32(b, 800); b.resize(b.size() + 800);
            U32(b, start, (std::uint32_t)b.size());
        }
        CHECK(!Load(b, o));
    }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    CHECK(ms < 20000);
}

static void CraftedAscii() {
    RUN_SUITE("crafted ASCII");
    auto A = [](const std::string& s) { return Bytes(s.begin(), s.end()); };
    CHECK(!Load(A("Objects: {")));                       // unterminated block
    CHECK(!Load(A("Objects: \"unterminated")));
    CHECK(!Load(A("Vertices: *99999999999999 { a: 1,2,3 }")));   // absurd array count
    CHECK(!Load(A("Vertices: *3 { a: 1,2,zz }")));
    CHECK(!Load(A("Vertices: *3 { b: 1 }")));
    CHECK(!Load(A("}")));
    {   // deep brace nesting
        std::string s; for (int i = 0; i < 200000; ++i) s += "a: {\n";
        std::string err; CHECK(!Load(A(s), Tight(), &err)); CHECK(err.find("deep") != std::string::npos);
    }
    {   // an enormous flat list
        fbx::BuildOptions o = Tight(); o.limits.maxArrayBytes = 1u << 16;
        std::string s = "Vertices: *100000 {\n a: "; for (int i = 0; i < 100000; ++i) s += "1,"; s += "1\n}\n";
        CHECK(!Load(A(s), o));
    }
    {   // an empty document parses, but there is no scene in it
        fbx::Document d; std::string err;
        CHECK(fbx::Parse((const std::uint8_t*)"; just a comment\n", 17, d, &err) && d.root.children.empty());
        CHECK(!Load(A("; just a comment\n")));
    }
    {
        fbx::Scene s; std::string err;
        CHECK(!fbx::LoadMemory((const std::uint8_t*)"; x\n", 4, s, &err, Tight()) && !err.empty());
    }
}

static std::string ModelChain(int n, bool cycle) {
    std::string s = "Objects: {\n";
    for (int i = 1; i <= n; ++i) s += "\tModel: " + std::to_string(i) + ", \"Model::M" + std::to_string(i) + "\", \"Null\" {\n\t}\n";
    s += "}\nConnections: {\n";
    for (int i = 2; i <= n; ++i) s += "\tC: \"OO\"," + std::to_string(i) + "," + std::to_string(i - 1) + "\n";
    if (cycle) s += "\tC: \"OO\",1," + std::to_string(n) + "\n";
    s += "\tC: \"OO\",1,0\n}\n";
    if (cycle) s = s.substr(0, s.rfind("\tC: \"OO\",1,0")) + "}\n";
    return s;
}

static void Hierarchies() {
    RUN_SUITE("hostile scene graphs");
    auto A = [](const std::string& s) { return Bytes(s.begin(), s.end()); };
    fbx::Scene s; std::string err;
    CHECK(fbx::LoadMemory(A(ModelChain(1000, false)).data(), ModelChain(1000, false).size(), s, &err) && s.nodes.size() == 1000);
    if (s.nodes.size() == 1000) { CHECK(s.nodes[999].parent == 998 && s.roots.size() == 1); }
    const std::string deep = ModelChain(5000, false);   // deeper than maxNodeDepth: refused, not recursed
    CHECK(!fbx::LoadMemory((const std::uint8_t*)deep.data(), deep.size(), s, &err));
    const std::string cyc = ModelChain(50, true);       // every model is the parent of another: a pure cycle
    CHECK(fbx::LoadMemory((const std::uint8_t*)cyc.data(), cyc.size(), s, &err));
    CHECK(s.nodes.size() == 50 && s.roots.size() == 1);
    for (std::size_t i = 0; i < s.nodes.size(); ++i) CHECK(s.nodes[i].parent < (int)i);
    // a model that is its own parent, and parents that do not exist
    const std::string odd = "Objects: {\n Model: 1, \"Model::A\", \"Null\" {\n }\n}\nConnections: {\n C: \"OO\",1,1\n C: \"OO\",1,77\n C: \"OO\",5,6\n C: \"OP\",1,2\n}\n";
    CHECK(fbx::LoadMemory((const std::uint8_t*)odd.data(), odd.size(), s, &err) && s.nodes.size() == 1 && s.nodes[0].parent == -1);
    // a giant n-gon (100k corners): must stay fast (fan fallback) and produce n-2 triangles
    {
        const int n = 100000;
        std::string t = "Objects: {\n Geometry: 2, \"Geometry::G\", \"Mesh\" {\n  Vertices: *" + std::to_string(n * 3) + " {\n   a: ";
        for (int i = 0; i < n; ++i) { t += std::to_string(std::cos(i * 6.2831853 / n)) + "," + std::to_string(std::sin(i * 6.2831853 / n)) + ",0"; if (i + 1 < n) t += ","; }
        t += "\n  }\n  PolygonVertexIndex: *" + std::to_string(n) + " {\n   a: ";
        for (int i = 0; i < n; ++i) { t += std::to_string(i == n - 1 ? -(n) : i); if (i + 1 < n) t += ","; }
        t += "\n  }\n }\n Model: 1, \"Model::A\", \"Mesh\" {\n }\n}\nConnections: {\n C: \"OO\",2,1\n C: \"OO\",1,0\n}\n";
        const auto t0 = std::chrono::steady_clock::now();
        fbx::BuildOptions o = fbx::BuildOptions();
        CHECK(fbx::LoadMemory((const std::uint8_t*)t.data(), t.size(), s, &err, o));
        CHECK(!s.meshes.empty() && s.meshes[0].triangleCount() == (std::uint32_t)(n - 2));
        CHECK(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - t0).count() < 20);
    }
    {   // 100000 models in one parent chain: refused by the depth cap (never walked recursively); the shipped limits
        const std::string chain = ModelChain(100000, false);
        std::string e2; fbx::BuildOptions def;
        CHECK(!fbx::LoadMemory((const std::uint8_t*)chain.data(), chain.size(), s, &e2, def));
        CHECK(e2.find("too deep") != std::string::npos);
        const std::string ok2 = ModelChain((int)def.limits.maxNodeDepth, false);   // exactly at the cap: accepted
        CHECK(fbx::LoadMemory((const std::uint8_t*)ok2.data(), ok2.size(), s, &e2, def) && s.nodes.size() == def.limits.maxNodeDepth);
    }
    // polygon indices out of range, NaN weights, NaN keys
    {
        const std::string t = "Objects: {\n Geometry: 2, \"Geometry::G\", \"Mesh\" {\n  Vertices: *9 { a: 0,0,0,1,0,0,0,1,0 }\n  PolygonVertexIndex: *6 { a: 0,1,-3,0,1,-9999 }\n }\n"
                              " Model: 1, \"Model::A\", \"Mesh\" {\n }\n}\nConnections: {\n C: \"OO\",2,1\n C: \"OO\",1,0\n}\n";
        CHECK(fbx::LoadMemory((const std::uint8_t*)t.data(), t.size(), s, &err));
        CHECK(!s.meshes.empty() && s.meshes[0].triangleCount() == 1);
    }
}

// ASCII scene with one animated model. `stacks` takes share ONE layer and ONE curve node (the cheap-file /
// expensive-bake shape), the curve spans `spanSec` seconds with `keys` keys; plus `extraCurves` bare curves.
static std::string AnimText(int stacks, int layers, int keys, double spanSec, int extraCurves = 0) {
    std::string s = "Objects: {\n\tModel: 1, \"Model::M\", \"Null\" {\n\t}\n";
    for (int i = 0; i < stacks; ++i) s += "\tAnimationStack: " + std::to_string(100 + i) + ", \"AnimStack::T" + std::to_string(i) + "\", \"\" {\n\t}\n";
    for (int i = 0; i < layers; ++i) s += "\tAnimationLayer: " + std::to_string(10000 + i) + ", \"AnimLayer::L" + std::to_string(i) + "\", \"\"\n";
    s += "\tAnimationCurveNode: 2, \"AnimCurveNode::T\", \"\" {\n\t}\n\tAnimationCurve: 3, \"AnimCurve::x\", \"\" {\n\t\tKeyTime: *" + std::to_string(keys) + " {\n\t\t\ta: ";
    for (int i = 0; i < keys; ++i) s += (i ? "," : "") + std::to_string((long long)(spanSec * 46186158000.0 * i / (keys > 1 ? keys - 1 : 1)));
    s += "\n\t\t}\n\t\tKeyValueFloat: *" + std::to_string(keys) + " {\n\t\t\ta: ";
    for (int i = 0; i < keys; ++i) s += (i ? "," : "") + std::string(i & 1 ? "1" : "0");
    s += "\n\t\t}\n\t}\n";
    for (int i = 0; i < extraCurves; ++i) s += "\tAnimationCurve: " + std::to_string(500000 + i) + ", \"AnimCurve::c\", \"\" {\n\t}\n";
    s += "}\nConnections: {\n\tC: \"OO\", 1, 0\n\tC: \"OP\", 2, 1, \"Lcl Translation\"\n\tC: \"OP\", 3, 2, \"d|X\"\n";
    if (layers) { s += "\tC: \"OO\", 2, 10000\n"; for (int i = 0; i < stacks; ++i) s += "\tC: \"OO\", 10000, " + std::to_string(100 + i) + "\n"; }
    s += "}\n";
    return s;
}
static bool LoadText(const std::string& t, const fbx::BuildOptions& o, std::string* err, fbx::Scene* out = nullptr) {
    fbx::Scene s;
    const bool ok = fbx::LoadMemory((const std::uint8_t*)t.data(), t.size(), out ? *out : s, err, o);
    return ok;
}

static void AnimationBudgets() {
    RUN_SUITE("hostile animation: stacks, layers, curves, keys and the whole-file bake budget");
    std::string err;
    fbx::BuildOptions def;   // the shipped limits
    // sane: 3 takes sharing one curve node load and bake 3 clips
    { fbx::Scene s; CHECK(LoadText(AnimText(3, 1, 4, 10.0), def, &err, &s) && s.clips.size() == 3); }
    // number of stacks
    { err.clear(); CHECK(!LoadText(AnimText((int)def.limits.maxAnimStacks + 1, 1, 2, 1.0), def, &err)); CHECK(err.find("animation stacks") != std::string::npos); }
    // number of layers
    { err.clear(); CHECK(!LoadText(AnimText(1, (int)def.limits.maxAnimLayers + 1, 2, 1.0), def, &err)); CHECK(err.find("animation layers") != std::string::npos); }
    // number of curves: one more than the limit, each a few bytes in the file
    { err.clear(); CHECK(!LoadText(AnimText(1, 1, 2, 1.0, (int)def.limits.maxAnimCurves), def, &err)); CHECK(err.find("animation curves") != std::string::npos); }
    { fbx::BuildOptions o = def; o.limits.maxAnimCurves = 50; err.clear(); CHECK(!LoadText(AnimText(1, 1, 2, 1.0, 60), o, &err)); CHECK(err.find("animation curves") != std::string::npos); }
    // keys: one curve with more than the whole-file limit (and a crafted claimed count of 10 million)
    { fbx::BuildOptions o = def; o.limits.maxAnimKeys = 1000; err.clear(); CHECK(!LoadText(AnimText(1, 1, 2000, 1.0), o, &err)); CHECK(err.find("animation keys") != std::string::npos); }
    {
        const auto t0 = std::chrono::steady_clock::now();
        err.clear(); CHECK(!LoadText(AnimText(1, 1, (int)def.limits.maxAnimKeys + 1, 1.0), def, &err)); CHECK(err.find("animation keys") != std::string::npos);
        CHECK(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - t0).count() < 60);
    }
    { err.clear(); CHECK(!LoadText("Objects: {\n AnimationCurve: 3, \"AnimCurve::x\", \"\" {\n  KeyTime: *10000000 {\n   a: 1,2\n  }\n  KeyValueFloat: *10000000 {\n   a: 1,2\n  }\n }\n}\n", Tight(), &err)); }
    // aggregate bake budget: every stack alone is within the per-clip cap, together they are not
    {
        fbx::BuildOptions o = Tight(); o.limits.maxBakedSamples = 20000; o.limits.maxTotalBakedSamples = 100000; o.limits.maxAnimStacks = 1000;
        fbx::Scene s; CHECK(LoadText(AnimText(3, 1, 2, 1000.0), o, &err, &s) && s.clips.size() == 3);   // 3 x 20000 samples: inside 100000
        CHECK(s.clips.size() == 3 && s.clips[0].nodes.size() == 1 && s.clips[0].nodes[0].translation.size() == 20000);
        err.clear(); CHECK(!LoadText(AnimText(50, 1, 2, 1000.0), o, &err)); CHECK(err.find("bake budget") != std::string::npos);
        // with the shipped limits: 128 takes of a long clip would be 128 x 20000 evaluations = 2.6M (inside 4M), so it loads...
        fbx::Scene big; CHECK(LoadText(AnimText((int)def.limits.maxAnimStacks, 1, 2, 1000.0), def, &err, &big) && big.clips.size() == def.limits.maxAnimStacks);
        // ...and what is baked in total never passes the budget, whatever the file asks for
        std::size_t total = 0; for (const fbx::Clip& c : big.clips) for (const fbx::NodeAnim& n : c.nodes) total += n.translation.size();
        CHECK(total <= def.limits.maxTotalBakedSamples);
    }
}

static void Mutate(Bytes& b, Rng& r) {
    if (b.empty()) return;
    const int kind = (int)(r.Next() % 7);
    const std::size_t at = r.Next() % b.size();
    switch (kind) {
        case 0: b[at] ^= (std::uint8_t)(1u << (r.Next() % 8)); break;
        case 1: b[at] = (std::uint8_t)r.Next(); break;
        case 2: if (at + 4 <= b.size()) { const std::uint32_t pick[6] = {0, 0xFFFFFFFFu, 0x7FFFFFFFu, 0x80000000u, 1, 0x00010000u}; std::memcpy(&b[at], &pick[r.Next() % 6], 4); } break;
        case 3: b.resize(at); break;                                                              // truncate
        case 4: { const std::size_t len = std::min<std::size_t>(r.Next() % 64 + 1, b.size() - at); b.erase(b.begin() + (std::ptrdiff_t)at, b.begin() + (std::ptrdiff_t)(at + len)); } break;
        case 5: { const std::size_t len = std::min<std::size_t>(r.Next() % 64 + 1, b.size() - at); Bytes c(b.begin() + (std::ptrdiff_t)at, b.begin() + (std::ptrdiff_t)(at + len)); b.insert(b.begin() + (std::ptrdiff_t)(r.Next() % b.size()), c.begin(), c.end()); } break;
        default: for (int k = 0; k < 8 && at + (std::size_t)k < b.size(); ++k) b[at + (std::size_t)k] = (std::uint8_t)r.Next(); break;
    }
}

static void Fuzz() {
    RUN_SUITE("mutation fuzz");
    long iters = 1500;
    if (const char* e = std::getenv("OKAY_FBX_FUZZ_ITERS")) iters = std::atol(e);
    const auto t0 = std::chrono::steady_clock::now();
    long ok = 0, bad = 0;
    Rng r{0x9E3779B97F4A7C15ull};
    for (const std::string& f : Fixtures()) {
        const Bytes base = ReadAll(f);
        for (long i = 0; i < iters; ++i) {
            Bytes b = base;
            const int n = 1 + (int)(r.Next() % 4);
            for (int k = 0; k < n; ++k) Mutate(b, r);
            if (Load(b)) ++ok; else ++bad;
        }
    }
    std::cout << "  fuzz: " << ok << " accepted, " << bad << " rejected, "
              << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count() << " ms\n";
    CHECK(ok + bad == (long)Fixtures().size() * iters);
    CHECK(bad > 0 && ok > 0);   // the mutations bite, and some still load
}

int main() {
    Truncation();
    CraftedBinary();
    Bombs();
    CraftedAscii();
    Hierarchies();
    AnimationBudgets();
    Fuzz();
    TEST_MAIN_RESULT();
}
