// Hostile-input tests, a memory and time budget check, and a deterministic mutation fuzz for the own glTF 1.0 reader.
// Needs only okay/Render/ModelData.hpp and engine/src/Render/Gltf1Reader.cpp, so it builds on its own:
//   g++ -std=c++17 -O1 -g -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all
//       -Iengine/include -Iengine/src/Render -Itests -DOKAY_SOURCE_DIR='"."' tests/test_gltf1_hostile.cpp
//       engine/src/Render/Gltf1Reader.cpp -o gltf1_hostile
// OKAY_READER_FUZZ_ITERS=200000 for a long run. With OKAY_MODEL_CORPUS set (docs/model_corpus.md), the real glTF 1.0 files
// under glTF/ are read and fuzzed too.
#include "test_framework.hpp"
#include "reader_hostile_common.hpp"
#include "gltf1_test_build.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <new>

using namespace okay;
using namespace hostile;
using namespace gltf1build;

// ---- a counting allocator: bytes live now and the peak since the last reset ---------------------------------------------
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

static long g_ok = 0, g_refused = 0;

static void TryG(const std::string& bytes) {
    ImportedModel m; std::string err;
    const bool ok = ReadGltf1Buffer(bytes, "", m, &err);
    if (ok) {
        ++g_ok;
        CHECK(Sane(m) && !m.parts.empty() && !m.nodes.empty());
        for (const ImportedPart& p : m.parts) CHECK(!p.mesh.triangles.empty() && p.mesh.normals.size() == p.mesh.vertices.size());
    } else {
        ++g_refused;
        CHECK(!err.empty() && m.parts.empty() && m.nodes.empty());
    }
}

// Peak bytes the reader holds at once while reading `bytes` (the input is already allocated by the caller), and the time.
static std::size_t PeakWhileReading(const std::string& bytes, bool& ok, double& secs) {
    const std::size_t base = g_live.load();
    g_peak.store(base);
    const auto t0 = std::chrono::steady_clock::now();
    { ImportedModel m; std::string err; ok = ReadGltf1Buffer(bytes, "", m, &err); }
    secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return g_peak.load() - base;
}

int main() {
    RUN_SUITE("gltf1_hostile");
    std::vector<std::string> seeds = {QuadJson(), QuadBinary()};
    if (const char* c = std::getenv("OKAY_MODEL_CORPUS")) {   // the real files, when available
        namespace fs = std::filesystem;
        for (const char* name : {"glTF/BoxTextured-glTF-Embedded/BoxTextured.gltf", "glTF/BoxTextured-glTF-Binary/BoxTextured.glb"}) {
            const std::string s = Slurp((fs::path(c) / name).string());
            if (!s.empty()) { seeds.push_back(s); std::printf("  real file %s added (%zu bytes)\n", name, s.size()); }
        }
    }
    for (const std::string& s : seeds) { ImportedModel m; std::string err; CHECK(ReadGltf1Buffer(s, "", m, &err)); }

    // every strict prefix of every seed is refused, never crashes
    {   long refusedPrefixes = 0, total = 0;
        for (const std::string& s : seeds)
            for (std::size_t cut = 0; cut < s.size(); ++cut) {
                ImportedModel m; std::string err;
                const bool ok = ReadGltf1Buffer(s.substr(0, cut), "", m, &err);
                ++total;
                if (!ok) ++refusedPrefixes;
                else CHECK(Sane(m));
            }
        // A binary prefix that ends inside the body after the scene's data reads (the header's length is longer than the
        // file: noted, not refused); every JSON prefix that ends before the closing brace is refused.
        std::printf("  prefixes: %ld of %ld refused\n", refusedPrefixes, total);
        const std::size_t lastBrace = seeds[0].rfind('}');   // a prefix up to the closing brace is incomplete JSON
        for (std::size_t cut = 0; cut <= lastBrace; ++cut) { ImportedModel m; std::string err; CHECK(!ReadGltf1Buffer(seeds[0].substr(0, cut), "", m, &err)); }
    }

    // hostile shapes
    auto with = [](const std::string& from, const std::string& to) { std::string j = QuadJson(); const std::size_t at = j.find(from); if (at != std::string::npos) j.replace(at, from.size(), to); return j; };
    TryG(std::string(1000000, '['));                                                   // deep nesting: refused before parsing
    TryG(std::string(127, '[') + std::string(127, ']'));                               // allowed depth, not an object
    TryG(with("\"count\": 6", "\"count\": 50000000"));                                 // a huge count against a 12-byte view
    TryG(with("\"count\": 6", "\"count\": 18446744073709551615"));
    TryG(with("\"byteOffset\": 128, \"byteLength\": 12", "\"byteOffset\": 4294967295, \"byteLength\": 12"));
    TryG(with("\"byteOffset\": 128, \"byteLength\": 12", "\"byteOffset\": 128, \"byteLength\": 18446744073709551615"));
    TryG(with("\"byteStride\": 12", "\"byteStride\": 255"));
    TryG(with("\"byteStride\": 12", "\"byteStride\": 256"));
    TryG(with("\"byteStride\": 12", "\"byteStride\": -12"));
    TryG(with("\"byteStride\": 12", "\"byteStride\": 1.5"));
    TryG(with("\"type\": \"VEC3\" },\n    \"nor\"", "\"type\": \"MAT4\" },\n    \"nor\""));
    TryG(with("\"children\": [\"child\"]", "\"children\": [\"root\"]"));             // a node under itself
    TryG(with("\"meshes\": [\"m\"] }", "\"meshes\": [\"m\", \"m\", \"m\", \"m\"] }"));   // the same mesh four times: fine
    TryG(with("\"mode\": 4", "\"mode\": 4294967296"));
    TryG(with("\"material\": \"mat\"", "\"material\": 7"));
    TryG(with("\"asset\": { \"version\": \"1.0\" }", "\"asset\": { \"version\": 1 }"));
    {   std::string j = QuadJson();                                                    // a NUL byte in the JSON
        j[j.find("\"Root\"") + 1] = '\0';
        TryG(j); }
    {   // many nodes in a long chain: walked with a stack, no recursion
        std::string nodes;
        const int n = 20000;
        for (int i = 0; i < n; ++i) nodes += "\"n" + std::to_string(i) + "\": { " + (i + 1 < n ? "\"children\": [\"n" + std::to_string(i + 1) + "\"]" : std::string("\"meshes\": [\"m\"]")) + " },";
        std::string j = QuadJson();
        const std::string from = "\"root\": { \"name\": \"Root\", \"translation\": [1, 2, 3], \"children\": [\"child\"] },";
        j.replace(j.find(from), from.size(), nodes + from);
        j.replace(j.find("\"nodes\": [\"root\"]"), std::string("\"nodes\": [\"root\"]").size(), "\"nodes\": [\"n0\"]");
        const auto t0 = std::chrono::steady_clock::now();
        TryG(j);
        CHECK(std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 10.0); }

    // memory and time budget: a refusal holds almost nothing; a large valid file stays within its budget
    {   bool ok = true; double secs = 0;
        const std::string huge = with("\"count\": 6", "\"count\": 50000000");
        const std::size_t refused = PeakWhileReading(huge, ok, secs);
        std::printf("  huge count against a small view: %s, peak %zu KB, %.3f s\n", ok ? "read" : "refused", refused >> 10, secs);
        CHECK(!ok && refused < 2u * 1024u * 1024u);
        // 1,000,000 triangles (a grid of 1001 x 501 vertices in one binary body): read, peak under 256 MB, under 10 s
        std::string body;
        const int w = 1001, h = 501;
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) { PutF(body, (float)x); PutF(body, (float)y); PutF(body, 0); }
        const std::size_t posBytes = body.size();
        for (int y = 0; y + 1 < h; ++y) for (int x = 0; x + 1 < w; ++x) {
            const std::uint32_t a = (std::uint32_t)(y * w + x), b = a + 1, c = a + (std::uint32_t)w, d = c + 1;
            for (std::uint32_t v : {a, b, d, a, d, c}) PutU32(body, v);
        }
        const std::size_t idxCount = (std::size_t)(w - 1) * (h - 1) * 6;
        const std::string json = std::string("{ \"asset\": { \"version\": \"1.0\" }, \"scene\": \"s\", \"scenes\": { \"s\": { \"nodes\": [\"n\"] } }, ") +
            "\"nodes\": { \"n\": { \"meshes\": [\"m\"] } }, \"meshes\": { \"m\": { \"primitives\": [ { \"attributes\": { \"POSITION\": \"p\" }, \"indices\": \"i\", \"material\": \"x\" } ] } }, " +
            "\"accessors\": { \"p\": { \"bufferView\": \"vp\", \"byteOffset\": 0, \"componentType\": 5126, \"count\": " + std::to_string(w * h) + ", \"type\": \"VEC3\" }, " +
            "\"i\": { \"bufferView\": \"vi\", \"byteOffset\": 0, \"componentType\": 5125, \"count\": " + std::to_string(idxCount) + ", \"type\": \"SCALAR\" } }, " +
            "\"bufferViews\": { \"vp\": { \"buffer\": \"binary_glTF\", \"byteOffset\": 0, \"byteLength\": " + std::to_string(posBytes) + " }, " +
            "\"vi\": { \"buffer\": \"binary_glTF\", \"byteOffset\": " + std::to_string(posBytes) + ", \"byteLength\": " + std::to_string(idxCount * 4) + " } }, " +
            "\"buffers\": { \"binary_glTF\": { \"uri\": \"data:,\" } } }";
        const std::string bin = Binary(json, body);
        const std::size_t big = PeakWhileReading(bin, ok, secs);
        std::printf("  1,000,000 triangles: %s, peak %zu MB, %.2f s\n", ok ? "read" : "refused", big >> 20, secs);
        CHECK(ok && big < 256u * 1024u * 1024u);
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_UNDEFINED__)
        CHECK(secs < 60.0);
#else
        CHECK(secs < 10.0);
#endif
    }

    // review fixes: one decode budget for the whole file, checked before allocating; degenerate triangles count
    {   // (1) the same mesh listed four times, each primitive 20,000,000 equal (degenerate) byte indices over 3 vertices
        const std::uint64_t n = 20000000;
        std::string body;
        for (int i = 0; i < 9; ++i) PutF(body, (float)(i % 3));   // 3 vertices
        body.append((std::size_t)n, '\0');                        // indices, all 0
        const std::string json = std::string("{ \"asset\": { \"version\": \"1.0\" }, \"scene\": \"s\", \"scenes\": { \"s\": { \"nodes\": [\"n\"] } }, ") +
            "\"nodes\": { \"n\": { \"meshes\": [\"m\", \"m\", \"m\", \"m\"] } }, \"meshes\": { \"m\": { \"primitives\": [ { \"attributes\": { \"POSITION\": \"p\" }, \"indices\": \"i\", \"material\": \"x\" } ] } }, " +
            "\"accessors\": { \"p\": { \"bufferView\": \"vp\", \"byteOffset\": 0, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\" }, " +
            "\"i\": { \"bufferView\": \"vi\", \"byteOffset\": 0, \"componentType\": 5121, \"count\": " + std::to_string(n) + ", \"type\": \"SCALAR\" } }, " +
            "\"bufferViews\": { \"vp\": { \"buffer\": \"binary_glTF\", \"byteOffset\": 0, \"byteLength\": 36 }, \"vi\": { \"buffer\": \"binary_glTF\", \"byteOffset\": 36, \"byteLength\": " + std::to_string(n) + " } }, " +
            "\"buffers\": { \"binary_glTF\": { \"uri\": \"data:,\" } } }";
        const std::string bin = Binary(json, body);
        bool ok = true; double secs = 0;
        const std::size_t peak = PeakWhileReading(bin, ok, secs);
        std::printf("  one mesh listed 4 times, 20,000,000 degenerate indices each: %s, peak %zu MB, %.2f s\n", ok ? "read" : "refused", peak >> 20, secs);
        CHECK(!ok && peak < 192u * 1024u * 1024u);
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_UNDEFINED__)
        CHECK(secs < 60.0);
#else
        CHECK(secs < 5.0);
#endif
    }
    {   // (2) a 20,000,000-element byte accessor of 4 components (80,000,000 values) is refused before anything is allocated
        const std::uint64_t count = 20000000;
        std::string body((std::size_t)(count * 4), '\1');
        const std::string json = std::string("{ \"asset\": { \"version\": \"1.0\" }, \"scene\": \"s\", \"scenes\": { \"s\": { \"nodes\": [\"n\"] } }, ") +
            "\"nodes\": { \"n\": { \"meshes\": [\"m\"] } }, \"meshes\": { \"m\": { \"primitives\": [ { \"attributes\": { \"POSITION\": \"p\" }, \"material\": \"x\" } ] } }, " +
            "\"accessors\": { \"p\": { \"bufferView\": \"vp\", \"byteOffset\": 0, \"componentType\": 5120, \"count\": " + std::to_string(count) + ", \"type\": \"VEC4\" } }, " +
            "\"bufferViews\": { \"vp\": { \"buffer\": \"binary_glTF\", \"byteOffset\": 0, \"byteLength\": " + std::to_string(count * 4) + " } }, " +
            "\"buffers\": { \"binary_glTF\": { \"uri\": \"data:,\" } } }";
        const std::string bin = Binary(json, body);
        body = std::string();
        bool ok = true; double secs = 0;
        const std::size_t peak = PeakWhileReading(bin, ok, secs);
        // the body is copied into the buffer table once (80 MB); nothing near the 320 MB of 80,000,000 floats is allocated
        std::printf("  80,000,000-value byte accessor: %s, peak %zu MB\n", ok ? "read" : "refused", peak >> 20);
        CHECK(!ok && peak < 96u * 1024u * 1024u);
    }

    // ---------------- mutation fuzz ----------------
    {
        const long iters = Iters(8000);
        const long before = g_ok;
        const std::string alphabet = "0123456789.-+eE{}[]\":, \n";
        for (long it = 0; it < iters; ++it) {
            std::string s = seeds[Rnd() % seeds.size()];
            const int ops = 1 + (int)(Rnd() % 4);
            const bool binary = s.size() >= 4 && s.compare(0, 4, "glTF") == 0;
            for (int o = 0; o < ops; ++o) { if (binary && Rnd() % 2) MutateBinary(s); else MutateText(s, alphabet); }
            TryG(s);
        }
        std::printf("  fuzz: %ld iterations, %ld read, %ld refused (all runs)\n", iters, g_ok, g_refused);
        CHECK(g_ok > before);   // some damaged files still read: the fuzz reaches the mesh code
        CHECK(g_refused > 0);
    }
    TEST_MAIN_RESULT();
}
