// Speed and memory of our own FBX reader (okay::fbx) against ufbx, on the same files.
//
// Run:  bench_fbx_reader [--runs N] [--quiet] [--digest] [--only WHAT] <file.fbx | directory> ...
//       (a directory is searched recursively for .fbx files; with no argument it reads $OKAY_MODEL_CORPUS and the
//        fixtures in tests/assets/fbx)
//
// For every file it measures, each as the median wall time over N runs (default 5; small files run more often, until
// about 50 ms have passed, so their median is not timer noise):
//   own-tree    okay::fbx::Parse      the element tree only (binary or ASCII)
//   own-load    okay::fbx::LoadMemory the tree plus the scene build (nodes, meshes, skins, baked clips): our reader's
//                                     own structures, which is what ufbx_load_memory produces for ufbx
//   ufbx-load   ufbx_load_memory      with the options the engine's ufbx path uses (ModelImport.cpp, LoadFbx)
//   imp-own / imp-ufbx                the full import through the seam, ImportModelScene, with our reader switched on
//                                     and off (file read, reader, GameObjects, MeshRenderer / SkinnedMesh / clips)
// and the PEAK heap bytes each of those needed above what was allocated before the call. The heap is counted by
// replacing malloc / free / realloc / calloc / memalign in this program (glibc only: they forward to glibc's own
// __libc_* functions), so ufbx's C allocations, our C++ `new` and everything in the importer are counted the same
// way. On other C libraries the memory columns print "-".
//
// Not part of ctest: it prints numbers, it does not check them. Correctness is checked by fbx_reader and
// model_corpus (docs/model_corpus.md). `--digest` prints, per file, a hash of every field our reader produced (bit for
// bit), to show that a speed change did not change the output. `--only own-tree|own-load|ufbx-load|import-own|
// import-ufbx` runs just that, once per file, for a profiler (valgrind --tool=callgrind). Large synthetic files: python3 scripts/gen_fbx_bench.py <dir>.
// Written for this project; the counting allocator and the statistics are our own.
#include "okay/Render/FbxReader.hpp"
#include "okay/Render/ModelImport.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Core/Log.hpp"
#include "ufbx.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#if defined(__GLIBC__) && !defined(__SANITIZE_ADDRESS__)
#include <malloc.h>
#define OKAY_BENCH_COUNT_HEAP 1
#endif

// ---- heap counting ------------------------------------------------------------------------------------------------
namespace {
std::atomic<long long> g_cur{0};
std::atomic<long long> g_peak{0};
inline void HeapAdd(void* p) {
#ifdef OKAY_BENCH_COUNT_HEAP
    if (!p) return;
    const long long now = g_cur.fetch_add((long long)malloc_usable_size(p), std::memory_order_relaxed) + (long long)malloc_usable_size(p);
    long long pk = g_peak.load(std::memory_order_relaxed);
    while (now > pk && !g_peak.compare_exchange_weak(pk, now, std::memory_order_relaxed)) {}
#else
    (void)p;
#endif
}
inline void HeapSub(void* p) {
#ifdef OKAY_BENCH_COUNT_HEAP
    if (p) g_cur.fetch_sub((long long)malloc_usable_size(p), std::memory_order_relaxed);
#else
    (void)p;
#endif
}
} // namespace

#ifdef OKAY_BENCH_COUNT_HEAP
extern "C" {
void* __libc_malloc(size_t);
void __libc_free(void*);
void* __libc_realloc(void*, size_t);
void* __libc_calloc(size_t, size_t);
void* __libc_memalign(size_t, size_t);

void* malloc(size_t n) { void* p = __libc_malloc(n); HeapAdd(p); return p; }
void free(void* p) { HeapSub(p); __libc_free(p); }
void* calloc(size_t a, size_t b) { void* p = __libc_calloc(a, b); HeapAdd(p); return p; }
void* realloc(void* old, size_t n) {
    const long long before = old ? (long long)malloc_usable_size(old) : 0;
    void* p = __libc_realloc(old, n);
    if (!p) return p;   // the old block is untouched (or n was 0 and it was freed: then p may be null too)
    g_cur.fetch_sub(before, std::memory_order_relaxed);
    HeapAdd(p);
    return p;
}
void* memalign(size_t a, size_t n) { void* p = __libc_memalign(a, n); HeapAdd(p); return p; }
void* aligned_alloc(size_t a, size_t n) { void* p = __libc_memalign(a, n); HeapAdd(p); return p; }
int posix_memalign(void** out, size_t a, size_t n) {
    void* p = __libc_memalign(a, n);
    if (!p) return 12;   // ENOMEM
    HeapAdd(p); *out = p; return 0;
}
}
#endif

using namespace okay;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace {

struct Measure { double ms = -1; long long peak = -1; bool ok = false; };

// Median time of `fn` over at least `runs` runs (more for fast calls, up to 50 ms or 200 runs), then one more run with
// the heap peak recorded.
Measure Run(int runs, const std::function<bool()>& fn) {
    Measure m;
    std::vector<double> t;
    double total = 0;
    for (int k = 0; k < 200 && ((int)t.size() < runs || total < 50.0); ++k) {
        const auto a = Clock::now();
        const bool ok = fn();
        const double ms = std::chrono::duration<double, std::milli>(Clock::now() - a).count();
        if (!ok) return m;
        t.push_back(ms); total += ms;
    }
    std::sort(t.begin(), t.end());
    m.ms = t.size() % 2 ? t[t.size() / 2] : 0.5 * (t[t.size() / 2 - 1] + t[t.size() / 2]);
    m.ok = true;
#ifdef OKAY_BENCH_COUNT_HEAP
    const long long base = g_cur.load();
    g_peak.store(base);
    fn();
    m.peak = g_peak.load() - base;
#endif
    return m;
}

ufbx_load_opts UfbxOpts() {   // the same settings as LoadFbx in engine/src/Render/ModelImport.cpp
    const fbx::Limits lim;
    ufbx_load_opts o{};
    o.target_axes = ufbx_axes_right_handed_y_up;
    o.target_unit_meters = 1.0f;
    o.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
    o.inherit_mode_handling = UFBX_INHERIT_MODE_HANDLING_COMPENSATE;
    o.generate_missing_normals = true;
    o.ignore_missing_external_files = true;
    o.evaluate_skinning = false;
    o.node_depth_limit = (uint32_t)lim.maxNodeDepth;
    return o;
}

struct Row {
    std::string name; std::size_t bytes = 0; bool synthetic = false;
    Measure tree, own, ufbx, impOwn, impUfbx;
};

std::string Mb(long long b) {
    if (b < 0) return "-";
    char s[32]; std::snprintf(s, sizeof(s), "%.2f", (double)b / (1024.0 * 1024.0)); return s;
}
std::string Ms(const Measure& m) {
    if (!m.ok) return "fail";
    char s[32]; std::snprintf(s, sizeof(s), m.ms < 10 ? "%.3f" : "%.1f", m.ms); return s;
}
std::string Ratio(double a, double b) {
    if (a < 0 || b <= 0) return "-";
    char s[32]; std::snprintf(s, sizeof(s), "%.2fx", a / b); return s;
}

void Collect(const fs::path& p, std::vector<std::string>& out) {
    std::error_code ec;
    if (fs::is_directory(p, ec)) {
        for (auto it = fs::recursive_directory_iterator(p, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            std::string ext = it->path().extension().string();
            for (char& c : ext) c = (char)std::tolower((unsigned char)c);
            if (ext == ".fbx") out.push_back(it->path().string());
        }
    } else if (fs::is_regular_file(p, ec)) {
        out.push_back(p.string());
    }
}

// ---- --digest: a hash of every field our reader produced, bit for bit, so a speed change can be shown not to change
// the output (run before and after, compare the lines) ----
struct Hasher {
    std::uint64_t h = 1469598103934665603ull;
    void Bytes(const void* p, std::size_t n) { const auto* b = (const unsigned char*)p; for (std::size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; } }
    template <class T> void Pod(const T& v) { Bytes(&v, sizeof(T)); }
    void Str(const std::string& v) { Pod(v.size()); Bytes(v.data(), v.size()); }
    template <class T> void Vec(const std::vector<T>& v) { Pod(v.size()); if (!v.empty()) Bytes(v.data(), v.size() * sizeof(T)); }
    void M(const fbx::Mat& m) { Bytes(m.m, sizeof(m.m)); }
};
std::uint64_t Digest(const fbx::Scene& s) {
    Hasher h;
    h.Pod(s.nodes.size());
    for (const fbx::SceneNode& n : s.nodes) {
        h.Str(n.name); h.Pod(n.parent); h.Vec(n.children); h.Pod(n.translation); h.Pod(n.rotation); h.Pod(n.scale);
        h.M(n.local); h.M(n.world); h.M(n.geometryToNode); h.M(n.geometryToWorld); h.Pod(n.mesh);
    }
    h.Vec(s.roots);
    h.Pod(s.meshes.size());
    for (const fbx::Mesh& m : s.meshes) {
        h.Str(m.name); h.Pod(m.controlPoints); h.Pod(m.hasNormals); h.Pod(m.hasUV); h.Pod(m.hasColors);
        h.Pod(m.parts.size());
        for (const fbx::Part& p : m.parts) {
            h.Pod(p.slot); h.Pod(p.corners.size());
            for (const fbx::Corner& c : p.corners) { h.Pod(c.p); h.Pod(c.n); h.Pod(c.uv); h.Pod(c.color); h.Pod(c.cp); }
        }
        h.Vec(m.materials);
        h.Pod(m.clusters.size());
        for (const fbx::Cluster& c : m.clusters) { h.Pod(c.boneNode); h.M(c.geometryToBone); }
        h.Pod(m.skinWeights.size());
        for (const auto& w : m.skinWeights) { h.Pod(w.size()); for (const fbx::Influence& i : w) { h.Pod(i.cluster); h.Pod(i.weight); } }
        h.Pod(m.channels.size());
        for (const fbx::BlendChannel& c : m.channels) { h.Str(c.name); h.Pod(c.weight); h.Vec(c.indices); h.Vec(c.dp); h.Vec(c.dn); }
    }
    h.Pod(s.materials.size());
    for (const fbx::Material& m : s.materials) {
        h.Str(m.name); h.Pod(m.hasDiffuse); h.Pod(m.diffuse); h.Pod(m.hasEmissive); h.Pod(m.emissive); h.Pod(m.hasEmissiveFactor);
        h.Pod(m.emissiveFactor); h.Pod(m.hasRoughness); h.Pod(m.roughness); h.Pod(m.hasMetalness); h.Pod(m.metalness);
        h.Pod(m.diffuseTexture); h.Pod(m.normalTexture); h.Pod(m.aoTexture); h.Str(m.shaderSet);
    }
    h.Pod(s.textures.size());
    for (const fbx::Texture& t : s.textures) { h.Str(t.name); h.Str(t.fileName); h.Str(t.relativeFileName); h.Vec(t.content); }
    h.Pod(s.clips.size());
    for (const fbx::Clip& c : s.clips) {
        h.Str(c.name); h.Pod(c.duration); h.Pod(c.nodes.size());
        for (const fbx::NodeAnim& n : c.nodes) {
            h.Pod(n.node); h.Pod(n.translation.size());
            for (const fbx::Key3& k : n.translation) { h.Pod(k.time); h.Pod(k.v); }
            h.Pod(n.rotation.size());
            for (const fbx::Key4& k : n.rotation) { h.Pod(k.time); h.Pod(k.v); }
            h.Pod(n.scale.size());
            for (const fbx::Key3& k : n.scale) { h.Pod(k.time); h.Pod(k.v); }
        }
        h.Pod(c.blends.size());
        for (const fbx::BlendAnim& b : c.blends) { h.Pod(b.mesh); h.Pod(b.channel); h.Pod(b.percent.size()); for (const fbx::Key3& k : b.percent) { h.Pod(k.time); h.Pod(k.v); } }
    }
    const fbx::Settings& st = s.settings;
    h.Pod(st.upAxis); h.Pod(st.upSign); h.Pod(st.frontAxis); h.Pod(st.frontSign); h.Pod(st.coordAxis); h.Pod(st.coordSign);
    h.Pod(st.unitScaleFactor); h.Pod(st.appliedScale); h.Str(s.creator);
    return h.h;
}

} // namespace

int main(int argc, char** argv) {
    int runs = 5;
    bool quiet = false, digest = false;
    std::string only;   // --only own-tree|own-load|ufbx-load|import-own|import-ufbx: run that once per file (for a profiler)
    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--runs" && i + 1 < argc) runs = std::max(1, std::atoi(argv[++i]));
        else if (a == "--quiet") quiet = true;
        else if (a == "--digest") digest = true;
        else if (a == "--only" && i + 1 < argc) only = argv[++i];
        else if (a == "-h" || a == "--help") { std::printf("bench_fbx_reader [--runs N] [--quiet] <file.fbx|dir> ...\n"); return 0; }
        else Collect(a, files);
    }
    if (argc < 2 || files.empty()) {
        if (const char* c = std::getenv("OKAY_MODEL_CORPUS")) Collect(c, files);
#ifdef OKAY_SOURCE_DIR
        Collect(fs::path(OKAY_SOURCE_DIR) / "tests" / "assets" / "fbx", files);
#endif
    }
    std::sort(files.begin(), files.end());
    files.erase(std::unique(files.begin(), files.end()), files.end());
    if (files.empty()) { std::fprintf(stderr, "no .fbx files given\n"); return 1; }
    if (digest) {   // one line per file: name, then the hash of our reader's whole result (or the error)
        for (const std::string& path : files) {
            fbx::Scene sc; std::string e;
            if (fbx::LoadFile(path, sc, &e)) std::printf("%s %016llx\n", fs::path(path).filename().string().c_str(), (unsigned long long)Digest(sc));
            else std::printf("%s error: %s\n", fs::path(path).filename().string().c_str(), e.c_str());
        }
        return 0;
    }
    Log::Configure("error");   // the import path logs per file; keep the table readable

    std::vector<Row> rows;
    for (const std::string& path : files) {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        std::vector<std::uint8_t> data((std::size_t)std::max<std::streamoff>(0, f.tellg()));
        f.seekg(0);
        f.read((char*)data.data(), (std::streamsize)data.size());
        Row r;
        // parent folder too: the corpus has the same file name in several folders (ASCII and binary versions)
        r.name = (fs::path(path).parent_path().filename() / fs::path(path).filename()).generic_string();
        r.synthetic = fs::path(path).filename().string().rfind("bench_", 0) == 0;
        r.bytes = data.size();
        if (!only.empty()) {
            bool ok = false;
            if (only == "own-tree") { fbx::Document d; std::string e; ok = fbx::Parse(data.data(), data.size(), d, &e); }
            else if (only == "own-load") { fbx::Scene s; std::string e; ok = fbx::LoadMemory(data.data(), data.size(), s, &e); }
            else if (only == "ufbx-load") { const ufbx_load_opts uo = UfbxOpts(); ufbx_error e{}; ufbx_scene* s = ufbx_load_memory(data.data(), data.size(), &uo, &e); ok = s != nullptr; ufbx_free_scene(s); }
            else if (only == "import-own" || only == "import-ufbx") {
                fbx::SetOwnReaderEnabled(only == "import-own");
                Scene sc("bench"); ok = ImportModelScene(sc, path, &ok) && ok;
                fbx::SetOwnReaderEnabled(false);
            } else { std::fprintf(stderr, "unknown --only %s\n", only.c_str()); return 1; }
            std::printf("%s %s\n", r.name.c_str(), ok ? "ok" : "fail");
            continue;
        }
        r.tree = Run(runs, [&] { fbx::Document d; std::string e; return fbx::Parse(data.data(), data.size(), d, &e); });
        r.own = Run(runs, [&] { fbx::Scene s; std::string e; return fbx::LoadMemory(data.data(), data.size(), s, &e); });
        const ufbx_load_opts uo = UfbxOpts();
        r.ufbx = Run(runs, [&] {
            ufbx_error e{};
            ufbx_scene* s = ufbx_load_memory(data.data(), data.size(), &uo, &e);
            if (!s) return false;
            ufbx_free_scene(s);
            return true;
        });
        auto import = [&](bool own) {
            return [&, own] {
                fbx::SetOwnReaderEnabled(own);
                Scene sc("bench"); bool ok = false;
                GameObject* root = ImportModelScene(sc, path, &ok);
                fbx::SetOwnReaderEnabled(false);
                return ok && root;
            };
        };
        r.impOwn = Run(runs, import(true));
        r.impUfbx = Run(runs, import(false));
        if (!quiet)
            std::fprintf(stderr, "%-48s %8zu KB  own %s ms  ufbx %s ms\n", r.name.c_str(), r.bytes / 1024, Ms(r.own).c_str(), Ms(r.ufbx).c_str());
        rows.push_back(std::move(r));
    }

    if (!only.empty()) return 0;

    // ---- per-file table (markdown) ----
    std::printf("| File | KB | own tree ms | own load ms | ufbx load ms | time own/ufbx | own peak MB | ufbx peak MB | mem own/ufbx | import own ms | import ufbx ms | import own/ufbx | import own MB | import ufbx MB |\n");
    std::printf("|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n");
    for (const Row& r : rows) {
        std::printf("| %s | %zu | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s |\n", r.name.c_str(), r.bytes / 1024,
                    Ms(r.tree).c_str(), Ms(r.own).c_str(), Ms(r.ufbx).c_str(), r.own.ok && r.ufbx.ok ? Ratio(r.own.ms, r.ufbx.ms).c_str() : "-",
                    Mb(r.own.peak).c_str(), Mb(r.ufbx.peak).c_str(), r.own.ok && r.ufbx.ok ? Ratio((double)r.own.peak, (double)r.ufbx.peak).c_str() : "-",
                    Ms(r.impOwn).c_str(), Ms(r.impUfbx).c_str(), r.impOwn.ok && r.impUfbx.ok ? Ratio(r.impOwn.ms, r.impUfbx.ms).c_str() : "-",
                    Mb(r.impOwn.peak).c_str(), Mb(r.impUfbx.peak).c_str());
    }

    // ---- totals over the files both readers read ----
    for (int group = 0; group < 2; ++group) {
        double own = 0, ufbx = 0, io = 0, iu = 0, tree = 0; long long po = 0, pu = 0, ipo = 0, ipu = 0; std::size_t kb = 0; int n = 0;
        for (const Row& r : rows) {
            if (r.synthetic != (group == 1) || !r.own.ok || !r.ufbx.ok || !r.impOwn.ok || !r.impUfbx.ok) continue;
            ++n; kb += r.bytes / 1024; tree += r.tree.ms; own += r.own.ms; ufbx += r.ufbx.ms; io += r.impOwn.ms; iu += r.impUfbx.ms;
            po = std::max(po, r.own.peak); pu = std::max(pu, r.ufbx.peak); ipo = std::max(ipo, r.impOwn.peak); ipu = std::max(ipu, r.impUfbx.peak);
        }
        if (!n) continue;
        std::printf("\n%s, %d files, %zu KB: own tree %.1f ms, own load %.1f ms, ufbx load %.1f ms (%s); import own %.1f ms, import ufbx %.1f ms (%s); "
                    "largest peak: own %s MB, ufbx %s MB; import own %s MB, import ufbx %s MB\n",
                    group ? "Synthetic (bench_*)" : "Real files and fixtures", n, kb, tree, own, ufbx, Ratio(own, ufbx).c_str(), io, iu,
                    Ratio(io, iu).c_str(), Mb(po).c_str(), Mb(pu).c_str(), Mb(ipo).c_str(), Mb(ipu).c_str());
    }

    // ---- worst ratios, on files where ufbx needs at least 1 ms (below that the numbers are mostly fixed costs) ----
    std::vector<const Row*> big;
    for (const Row& r : rows) if (r.own.ok && r.ufbx.ok && r.ufbx.ms >= 1.0) big.push_back(&r);
    auto worst = [&](const char* label, const std::function<double(const Row&)>& key) {
        std::vector<const Row*> v = big;
        std::sort(v.begin(), v.end(), [&](const Row* a, const Row* b) { return key(*a) > key(*b); });
        std::printf("\nWorst %s (ufbx load >= 1 ms):\n", label);
        for (std::size_t i = 0; i < v.size() && i < 5; ++i) std::printf("  %-48s %.2fx\n", v[i]->name.c_str(), key(*v[i]));
    };
    worst("load time own/ufbx", [](const Row& r) { return r.own.ms / r.ufbx.ms; });
    worst("load peak memory own/ufbx", [](const Row& r) { return r.ufbx.peak > 0 ? (double)r.own.peak / (double)r.ufbx.peak : 0.0; });
    worst("import time own/ufbx", [](const Row& r) { return r.impUfbx.ok && r.impOwn.ok && r.impUfbx.ms > 0 ? r.impOwn.ms / r.impUfbx.ms : 0.0; });
    int fails = 0;
    for (const Row& r : rows) if (!r.own.ok || !r.ufbx.ok) { ++fails; std::printf("\nnot read: %s (own %s, ufbx %s)\n", r.name.c_str(), r.own.ok ? "ok" : "fail", r.ufbx.ok ? "ok" : "fail"); }
    std::printf("\n%zu files, %d not read by one of the readers. Runs per measurement: at least %d. Heap counting: %s.\n", rows.size(), fails, runs,
#ifdef OKAY_BENCH_COUNT_HEAP
                "on"
#else
                "off (not glibc)"
#endif
    );
    return 0;
}
