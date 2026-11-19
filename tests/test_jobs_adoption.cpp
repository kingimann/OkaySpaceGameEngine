// The places that use the job system (AssetDatabase::Scan hashing, GenerateLodChain levels) give the same
// result with 0, 1 and 4 workers, and with the parallel switch off. Also prints before/after times (not checked:
// they depend on the machine). Usage: test_jobs_adoption
#include "test_framework.hpp"
#include "okay/Core/AssetDatabase.hpp"
#include "okay/Core/Jobs.hpp"
#include "okay/Optimize/MeshSimplify.hpp"
#include "okay/Render/Mesh.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

using namespace okay;
namespace fs = std::filesystem;

namespace {
double NowMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string MakeProject(int files, int bytes) {
    const fs::path root = fs::temp_directory_path() / "okay_jobs_adoption_proj";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "art", ec);
    std::uint32_t x = 12345u;
    for (int i = 0; i < files; ++i) {
        std::string data((std::size_t)bytes, '\0');
        for (char& c : data) { x = x * 1664525u + 1013904223u; c = (char)(x >> 24); }
        std::ofstream(root / "art" / ("file" + std::to_string(i) + ".png"), std::ios::binary) << data;
    }
    return root.string();
}

// path -> "hash:size", the part of a scan that must not depend on the worker count.
std::map<std::string, std::string> ScanOnce(const std::string& root, bool parallel, double* ms) {
    std::error_code ec;
    for (auto& e : fs::recursive_directory_iterator(root, ec))
        if (e.path().extension() == ".okaymeta") fs::remove(e.path(), ec);   // fresh scan: every file is hashed
    AssetDatabase db;
    ScanOptions o;
    o.parallelHash = parallel;
    o.logWarnings = false;
    const double t0 = NowMs();
    db.Scan(root, o);
    if (ms) *ms = NowMs() - t0;
    std::map<std::string, std::string> out;
    for (const AssetRecord* r : db.All()) out[r->path] = std::to_string(r->hash) + ":" + std::to_string(r->size);
    return out;
}

void TestScanDeterminism() {
    RUN_SUITE("scan hashing: same result with 0, 1, 4 workers");
    const std::string root = MakeProject(48, 400 * 1024);
    JobSystem& js = JobSystem::Global();
    js.Configure(0);
    double t0ms = 0;
    const auto base = ScanOnce(root, true, &t0ms);
    CHECK(base.size() == 48);
    for (const auto& kv : base) CHECK(kv.second.find(":0") == std::string::npos);
    double bestSeq = t0ms;
    for (int w : {1, 4}) {
        js.Configure(w);
        CHECK(ScanOnce(root, true, nullptr) == base);
        CHECK(ScanOnce(root, false, nullptr) == base);
    }
    js.Configure(4);
    double par = 1e9;
    for (int i = 0; i < 3; ++i) { double t; ScanOnce(root, true, &t); if (t < par) par = t; }
    for (int i = 0; i < 3; ++i) { double t; ScanOnce(root, false, &t); if (t < bestSeq) bestSeq = t; }
    std::printf("  [numbers] scan of 48 files x 400 KB: one thread %.1f ms, 4 workers %.1f ms\n", bestSeq, par);
    js.Configure(0);
    std::error_code ec;
    fs::remove_all(root, ec);
}

bool SameMesh(const Mesh& a, const Mesh& b) {
    if (a.vertices.size() != b.vertices.size() || a.triangles.size() != b.triangles.size()) return false;
    for (std::size_t i = 0; i < a.vertices.size(); ++i)
        if (a.vertices[i].x != b.vertices[i].x || a.vertices[i].y != b.vertices[i].y || a.vertices[i].z != b.vertices[i].z) return false;
    return a.triangles == b.triangles;
}

bool SameChain(const LodChain& a, const LodChain& b) {
    if (a.levels.size() != b.levels.size()) return false;
    for (std::size_t i = 0; i < a.levels.size(); ++i) {
        if (a.levels[i].triangles != b.levels[i].triangles || a.levels[i].error != b.levels[i].error ||
            a.levels[i].switchDistance != b.levels[i].switchDistance || !SameMesh(a.levels[i].mesh, b.levels[i].mesh)) return false;
    }
    return true;
}

void TestLodDeterminism() {
    RUN_SUITE("LOD chain: same result with 0, 1, 4 workers");
    const Mesh m = Mesh::Sphere(1.0f, 90, 120);
    const std::vector<float> ratios = {1.0f, 0.5f, 0.25f, 0.12f, 0.06f};
    JobSystem& js = JobSystem::Global();
    js.Configure(0);
    LodChainOptions opt;
    const LodChain base = GenerateLodChain(m, ratios, opt);
    CHECK(base.levels.size() == ratios.size());
    for (int w : {1, 4}) {
        js.Configure(w);
        CHECK(SameChain(GenerateLodChain(m, ratios, opt), base));
        LodChainOptions off = opt; off.parallel = false;
        CHECK(SameChain(GenerateLodChain(m, ratios, off), base));
    }
    js.Configure(4);
    double par = 1e9, seq = 1e9;
    for (int i = 0; i < 3; ++i) { double t0 = NowMs(); GenerateLodChain(m, ratios, opt); par = std::min(par, NowMs() - t0); }
    LodChainOptions off = opt; off.parallel = false;
    for (int i = 0; i < 3; ++i) { double t0 = NowMs(); GenerateLodChain(m, ratios, off); seq = std::min(seq, NowMs() - t0); }
    std::printf("  [numbers] LOD chain of %d triangles, 5 levels: one thread %.1f ms, 4 workers %.1f ms\n", (int)m.TriangleCount(), seq, par);
    js.Configure(0);
}
} // namespace

int main() {
    TestScanDeterminism();
    TestLodDeterminism();
    TEST_MAIN_RESULT();
}
