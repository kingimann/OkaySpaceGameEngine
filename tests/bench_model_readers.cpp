// Speed and memory of our own model readers (the ones behind the ImportModelData seam: Collada, 3DS, BVH, OFF, SMD,
// MD5, MS3D, ASE, 3MF, AMF, X3D, DXF, LWO, Blender, MD2, MD3, IrrMesh, B3D, X, AC3D, OGRE XML, XGL, Irr scene, NFF,
// Terragen, MDC, HMP, CSM, Q3D, RAW, IQM, COB, PMX, LWS, and OBJ / STL / PLY) against Assimp, on the same files.
// FBX has its own bench (bench_fbx_reader, ufbx is its other side); glTF does not go through this seam.
//
// Build with -DOKAY_USE_ASSIMP=ON (without Assimp only our side is measured). Not run by ctest: it prints numbers.
//
// Run:  bench_model_readers [--runs N] [--format F] [--quiet] [--digest | --import-digest | --compare | --only WHAT]
//                           <file | directory> ...
//       (a directory is searched recursively; with no file argument it reads $OKAY_MODEL_CORPUS)
//   --format F   only the files of one reader: the extension without the dot (dae, 3ds, x, mesh, zae, xml, ...), "ogre" for .mesh.xml, and
//                ac (.ac .acc .ac3d), md5 (.md5mesh .md5anim), q3d (.q3o .q3s), xgl (.xgl .zgl), nff (.nff .enff), lws (.lws .mot)
//   --runs N     at least N timed runs per measurement (default 7); small files run more often, until about 50 ms
//                have passed, so their median is not timer noise
//
// For every file it measures the median wall time and the peak heap of:
//   own-read      ImportModelData(path, Own)    our reader alone: file read and our reader's ImportedModel
//   assimp-read   ImportModelData(path, Assimp) Assimp's ReadFile with the engine's post-process steps, then its
//                                               conversion to the same ImportedModel
//   import-own    ImportModelScene with every format switched to our reader (file read, reader, GameObjects,
//                 MeshRenderer / SkinnedMesh / clips): the full import through the seam
//   import-assimp the same with every format switched to Assimp
// The peak heap is the most bytes held at once during the call above what was held before it. It is counted by
// replacing malloc / free / realloc / calloc / memalign in this program (glibc only; they forward to glibc's own
// __libc_* functions), so Assimp's allocations, our C++ `new` and everything in the importer are counted the same way.
// On other C libraries the memory columns print "-".
//
// --digest         per file, a hash of every field of our reader's ImportedModel (bit for bit), to show a speed change
//                  did not change the output (run before and after, compare the lines)
// --import-digest  per file, a hash of the whole imported scene (our readers on), as SceneSerializer writes it (object
//                  guids, which are random, left out)
// --compare        per file our reader's and Assimp's ImportedModel side by side (parts, triangles, vertices, nodes,
//                  materials, clips, bones, bounding box in world space, missing node names), "DIFF" where they disagree
// --only WHAT      own-read | assimp-read | import-own | import-assimp: that call once per file (for a profiler)
//
// Written for this project (the counting allocator follows bench_fbx_reader, which is our own code).
#include "okay/Render/ModelData.hpp"
#include "okay/Render/ModelImport.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Core/Log.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/wait.h>
#include <unistd.h>
#define OKAY_BENCH_FORK 1
#endif

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
    const long long sz = (long long)malloc_usable_size(p);
    const long long now = g_cur.fetch_add(sz, std::memory_order_relaxed) + sz;
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
    if (!p) return p;
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

// Median time of `fn` over at least `runs` runs (more for fast calls, until 50 ms or 200 runs), then one more run
// with the heap peak recorded.
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

std::string Lower(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }

// The format key of a file ("" = no own reader): the extension without its dot, "ogre" for .mesh.xml.
std::string FormatOf(const fs::path& p) {
    const std::string name = Lower(p.filename().string());
    if (name.size() >= 9 && name.compare(name.size() - 9, 9, ".mesh.xml") == 0) return "ogre";
    const std::string ext = Lower(p.extension().string());
    if (ext.size() < 2 || ext == ".fbx" || ext == ".gltf" || ext == ".glb") return std::string();
    if (!HasOwnReader(ext)) return std::string();
    // one row per reader: the extensions one reader takes share a key
    static const char* const kSame[][2] = {{".acc", "ac"}, {".ac3d", "ac"}, {".md5mesh", "md5"}, {".md5anim", "md5"}, {".q3o", "q3d"},
                                           {".q3s", "q3d"}, {".zgl", "xgl"}, {".enff", "nff"}, {".mot", "lws"}};
    for (const auto& k : kSame) if (ext == k[0]) return k[1];
    return ext.substr(1);
}

void Collect(const fs::path& p, const std::string& onlyFormat, std::vector<std::string>& out) {
    std::error_code ec;
    auto take = [&](const fs::path& f) {
        const std::string fmt = FormatOf(f);
        if (!fmt.empty() && (onlyFormat.empty() || fmt == onlyFormat)) out.push_back(f.string());
    };
    if (fs::is_directory(p, ec)) {
        for (auto it = fs::recursive_directory_iterator(p, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
            if (it->is_regular_file(ec)) take(it->path());
    } else if (fs::is_regular_file(p, ec)) {
        take(p);
    }
}

// ---- --digest: every field of an ImportedModel, bit for bit ----
struct Hasher {
    std::uint64_t h = 1469598103934665603ull;
    void Bytes(const void* p, std::size_t n) { const auto* b = (const unsigned char*)p; for (std::size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; } }
    template <class T> void Pod(const T& v) { Bytes(&v, sizeof(T)); }
    void Str(const std::string& v) { Pod(v.size()); Bytes(v.data(), v.size()); }
    template <class T> void Vec(const std::vector<T>& v) { Pod(v.size()); if (!v.empty()) Bytes(v.data(), v.size() * sizeof(T)); }
};
std::uint64_t Digest(const ImportedModel& m) {
    Hasher h;
    h.Pod(m.nodes.size());
    for (const ImportedNode& n : m.nodes) { h.Str(n.name); h.Pod(n.parent); h.Pod(n.position); h.Pod(n.rotation); h.Pod(n.scale); }
    h.Pod(m.parts.size());
    for (const ImportedPart& p : m.parts) {
        const Mesh& x = p.mesh;
        h.Str(x.name); h.Vec(x.vertices); h.Vec(x.triangles); h.Vec(x.uvs); h.Vec(x.triColors); h.Vec(x.polyFace); h.Vec(x.cornerUVs);
        h.Vec(x.seams); h.Pod(x.creases.size()); for (const EdgeCrease& c : x.creases) { h.Pod(c.a); h.Pod(c.b); h.Pod(c.w); }
        h.Vec(x.vertexColors); h.Vec(x.triSlot); h.Vec(x.slotColors); h.Vec(x.hiddenPos); h.Vec(x.hiddenUV); h.Vec(x.hiddenSlot);
        h.Vec(x.hiddenPoly); h.Vec(x.hiddenColor); h.Vec(x.normals); h.Pod(x.autoSmoothAngle);
        h.Pod(p.node); h.Pod(p.material);
        h.Pod(p.bones.size()); for (const ImportedBone& b : p.bones) { h.Str(b.name); h.Bytes(b.inverseBind.m, sizeof(b.inverseBind.m)); }
        h.Vec(p.jointIdx); h.Vec(p.jointWt);
    }
    h.Pod(m.materials.size());
    for (const ImportedMaterial& t : m.materials) {
        h.Str(t.name); h.Str(t.texture); h.Str(t.normalMap); h.Str(t.specularMap); h.Str(t.aoMap);
        h.Pod(t.hasDiffuse); h.Pod(t.diffuse); h.Pod(t.hasEmissive); h.Pod(t.emissive); h.Pod(t.shininess); h.Pod(t.metallic); h.Pod(t.roughness);
    }
    h.Pod(m.clips.size());
    for (const ImportedClip& c : m.clips) {
        h.Str(c.name); h.Pod(c.channels.size());
        for (const ImportedChannel& ch : c.channels) { h.Str(ch.node); h.Vec(ch.position); h.Vec(ch.scale); h.Vec(ch.rotation); }
    }
    h.Str(m.backend);
    h.Pod(m.notes.size()); for (const std::string& n : m.notes) h.Str(n);
    return h.h;
}

void SetAll(ImportBackend b) { SetImportBackend("*", b); }

bool ReadWith(const std::string& path, ImportBackend b, ImportedModel& out, std::string* err = nullptr) {
    SetAll(b);   // the meshes an Irrlicht or LightWave scene names are read through the seam with the default choice
    const bool ok = ImportModelData(path, out, b, false, err);
    SetAll(ImportBackend::Default);
    return ok;
}
// Assimp 5.4.3 crashes on some corpus files (RAW/WithColor.raw and RAW/WithTexture.raw: a node's child list holds a bad
// pointer). Read the file once in a child process first; a file that kills the child is reported as "crash" and not
// measured. 0 = read, 1 = refused, 2 = crashed.
int ProbeAssimp(const std::string& path) {
#ifdef OKAY_BENCH_FORK
    std::fflush(nullptr);
    const pid_t pid = fork();
    if (pid == 0) { ImportedModel im; SetImportBackend("*", ImportBackend::Assimp); _exit(ImportModelData(path, im, ImportBackend::Assimp, false, nullptr) ? 0 : 1); }
    if (pid < 0) return 0;
    int st = 0;
    if (waitpid(pid, &st, 0) != pid) return 0;
    if (WIFEXITED(st)) return WEXITSTATUS(st) == 0 ? 0 : 1;
    return 2;
#else
    (void)path; return 0;
#endif
}

bool ImportWith(const std::string& path, ImportBackend b) {
    SetAll(b);
    Scene sc("bench"); bool ok = false;
    GameObject* root = ImportModelScene(sc, path, &ok);
    SetAll(ImportBackend::Default);
    return ok && root;
}

// ---- --compare: what a backend's ImportedModel holds, in world space ----
struct Stats {
    int parts = 0, tris = 0, verts = 0, nodes = 0, mats = 0, clips = 0, bones = 0;
    bool any = false; Vec3 lo{0, 0, 0}, hi{0, 0, 0};
    std::set<std::string> names;
};
Stats StatsOf(const ImportedModel& im) {
    Stats s;
    s.parts = (int)im.parts.size(); s.nodes = (int)im.nodes.size(); s.mats = (int)im.materials.size(); s.clips = (int)im.clips.size();
    std::vector<Mat4> world(im.nodes.size());
    for (std::size_t i = 0; i < im.nodes.size(); ++i) {
        const ImportedNode& n = im.nodes[i];
        const Mat4 l = Mat4::TRS(n.position, n.rotation, n.scale);
        world[i] = n.parent >= 0 && (std::size_t)n.parent < i ? world[(std::size_t)n.parent] * l : l;
        s.names.insert(n.name);
    }
    std::set<std::string> bones;
    for (const ImportedPart& p : im.parts) {
        s.tris += p.mesh.TriangleCount(); s.verts += (int)p.mesh.vertices.size();
        for (const ImportedBone& b : p.bones) bones.insert(b.name);
        const Mat4 w = p.node >= 0 && (std::size_t)p.node < world.size() ? world[(std::size_t)p.node] : Mat4::Identity();
        for (const Vec3& v : p.mesh.vertices) {
            const Vec3 q = w.MultiplyPoint(v);
            if (!s.any) { s.lo = s.hi = q; s.any = true; }
            else { s.lo = Vec3{std::fmin(s.lo.x, q.x), std::fmin(s.lo.y, q.y), std::fmin(s.lo.z, q.z)}; s.hi = Vec3{std::fmax(s.hi.x, q.x), std::fmax(s.hi.y, q.y), std::fmax(s.hi.z, q.z)}; }
        }
    }
    s.bones = (int)bones.size();
    return s;
}

struct Row {
    std::string name, format; std::size_t bytes = 0; bool assimpCrash = false;
    Measure own, assimp, impOwn, impAssimp;
};

std::string Mb(long long b) {
    if (b < 0) return "-";
    char s[32]; std::snprintf(s, sizeof(s), "%.2f", (double)b / (1024.0 * 1024.0)); return s;
}
std::string Ms(const Measure& m, bool crash = false) {
    if (crash) return "crash";
    if (!m.ok) return "fail";
    char s[32]; std::snprintf(s, sizeof(s), m.ms < 10 ? "%.3f" : "%.1f", m.ms); return s;
}
std::string Ratio(double a, double b) {
    if (a < 0 || b <= 0) return "-";
    char s[32]; std::snprintf(s, sizeof(s), "%.2fx", a / b); return s;
}

std::string ShortName(const std::string& path) {
    // parent folder too: the corpus has the same file name in several folders
    return (fs::path(path).parent_path().filename() / fs::path(path).filename()).generic_string();
}

} // namespace

int main(int argc, char** argv) {
    int runs = 7;
    bool quiet = false, digest = false, importDigest = false, compare = false;
    std::string only, onlyFormat;
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--runs" && i + 1 < argc) runs = std::max(1, std::atoi(argv[++i]));
        else if (a == "--format" && i + 1 < argc) onlyFormat = Lower(argv[++i]);
        else if (a == "--quiet") quiet = true;
        else if (a == "--digest") digest = true;
        else if (a == "--import-digest") importDigest = true;
        else if (a == "--compare") compare = true;
        else if (a == "--only" && i + 1 < argc) only = argv[++i];
        else if (a == "-h" || a == "--help") {
            std::printf("bench_model_readers [--runs N] [--format F] [--quiet] [--digest|--import-digest|--compare|--only WHAT] <file|dir> ...\n");
            return 0;
        } else args.push_back(a);
    }
    std::vector<std::string> files;
    for (const std::string& a : args) Collect(a, onlyFormat, files);
    if (args.empty())
        if (const char* c = std::getenv("OKAY_MODEL_CORPUS")) Collect(c, onlyFormat, files);
    std::sort(files.begin(), files.end());
    files.erase(std::unique(files.begin(), files.end()), files.end());
    if (files.empty()) { std::fprintf(stderr, "no model files with an own reader given (and OKAY_MODEL_CORPUS not set)\n"); return 1; }
    Log::Configure("error");   // our readers log every note as a warning: keep the output (and the timing) clean
    const bool haveAssimp = AssimpAvailable();

    if (digest) {   // one line per file: format, name, then the hash of our reader's whole result (or the error)
        for (const std::string& path : files) {
            ImportedModel im; std::string e;
            if (ReadWith(path, ImportBackend::Own, im, &e))
                std::printf("%s %s %016llx\n", FormatOf(path).c_str(), ShortName(path).c_str(), (unsigned long long)Digest(im));
            else std::printf("%s %s error: %s\n", FormatOf(path).c_str(), ShortName(path).c_str(), e.c_str());
        }
        return 0;
    }
    if (importDigest) {   // one line per file: a hash of the whole imported scene (our readers on), as the scene file writes it
        for (const std::string& path : files) {
            ImportedModel probe;
            const bool ownOk = ReadWith(path, ImportBackend::Own, probe);
            std::string text = "-";
            bool ok = false;
            if (ownOk) {   // only when our reader reads it: otherwise the scene import would fall back to Assimp
                SetAll(ImportBackend::Own);
                Scene sc("digest");
                ImportModelScene(sc, path, &ok);
                SetAll(ImportBackend::Default);
                text.clear();
                const std::string all = SceneSerializer::Serialize(sc);
                std::size_t at = 0;
                while (at < all.size()) {   // every line but the random object guids
                    std::size_t e = all.find('\n', at); if (e == std::string::npos) e = all.size();
                    const std::size_t b = all.find_first_not_of(' ', at);
                    if (!(b < e && all.compare(b, 5, "guid ") == 0)) text.append(all, at, e - at + 1);
                    at = e + 1;
                }
            }
            Hasher h; h.Str(text);
            std::printf("%s %s %s %016llx\n", FormatOf(path).c_str(), ShortName(path).c_str(), ownOk ? (ok ? "ok" : "fail") : "own-refused", (unsigned long long)h.h);
        }
        return 0;
    }
    if (compare) {
        if (!haveAssimp) { std::fprintf(stderr, "--compare needs a build with OKAY_USE_ASSIMP=ON\n"); return 1; }
        auto box = [](const Stats& s) {
            char b[160]; std::snprintf(b, sizeof(b), "(%.4g %.4g %.4g)-(%.4g %.4g %.4g)", s.lo.x, s.lo.y, s.lo.z, s.hi.x, s.hi.y, s.hi.z); return std::string(b);
        };
        for (const std::string& path : files) {
            ImportedModel a, b; std::string ea, eb;
            const bool crash = ProbeAssimp(path) == 2;
            const bool oa = ReadWith(path, ImportBackend::Own, a, &ea), ob = !crash && ReadWith(path, ImportBackend::Assimp, b, &eb);
            std::printf("%s\t%s\town=%s\tassimp=%s", FormatOf(path).c_str(), ShortName(path).c_str(), oa ? "ok" : "fail", crash ? "crash" : ob ? "ok" : "fail");
            if (!oa || !ob) { std::printf("\t%s\n", (oa == ob) ? "both-fail" : "DIFF read"); continue; }
            const Stats s = StatsOf(a), t = StatsOf(b);
            std::string diff;
            auto cmp = [&](const char* what, int x, int y) { std::printf("\t%s=%d/%d", what, x, y); if (x != y) diff += std::string(" ") + what; };
            cmp("parts", s.parts, t.parts); cmp("tris", s.tris, t.tris); cmp("verts", s.verts, t.verts); cmp("nodes", s.nodes, t.nodes);
            cmp("mats", s.mats, t.mats); cmp("clips", s.clips, t.clips); cmp("bones", s.bones, t.bones);
            // bounding box: relative to its size (1e-3 of the diagonal)
            const double d = std::max(1e-9, (double)(t.hi - t.lo).Magnitude());
            const double e = std::max({std::fabs(s.lo.x - t.lo.x), std::fabs(s.lo.y - t.lo.y), std::fabs(s.lo.z - t.lo.z),
                                       std::fabs(s.hi.x - t.hi.x), std::fabs(s.hi.y - t.hi.y), std::fabs(s.hi.z - t.hi.z)}) / d;
            std::printf("\tbox=%s/%s", box(s).c_str(), box(t).c_str());
            if (s.any != t.any || e > 1e-3) diff += " box";
            int missing = 0; for (const std::string& n : t.names) if (!s.names.count(n)) ++missing;
            std::printf("\tassimp-names-not-ours=%d", missing);
            std::printf("\t%s\n", diff.empty() ? "same" : ("DIFF" + diff).c_str());
        }
        return 0;
    }

    std::vector<Row> rows;
    for (const std::string& path : files) {
        Row r;
        r.name = ShortName(path); r.format = FormatOf(path);
        std::error_code ec; r.bytes = (std::size_t)fs::file_size(path, ec);
        if (!only.empty()) {
            bool ok = false; ImportedModel im;
            if (only == "own-read") ok = ReadWith(path, ImportBackend::Own, im);
            else if (only == "assimp-read") ok = ReadWith(path, ImportBackend::Assimp, im);
            else if (only == "import-own") ok = ImportWith(path, ImportBackend::Own);
            else if (only == "import-assimp") ok = ImportWith(path, ImportBackend::Assimp);
            else { std::fprintf(stderr, "unknown --only %s\n", only.c_str()); return 1; }
            std::printf("%s %s\n", r.name.c_str(), ok ? "ok" : "fail");
            continue;
        }
        r.own = Run(runs, [&] { ImportedModel im; return ReadWith(path, ImportBackend::Own, im); });
        r.assimpCrash = haveAssimp && ProbeAssimp(path) == 2;
        if (haveAssimp && !r.assimpCrash) r.assimp = Run(runs, [&] { ImportedModel im; return ReadWith(path, ImportBackend::Assimp, im); });
        // the full import with our reader only when our reader reads the file (else the seam would fall back to Assimp)
        if (r.own.ok) r.impOwn = Run(runs, [&] { return ImportWith(path, ImportBackend::Own); });
        if (haveAssimp && r.assimp.ok) r.impAssimp = Run(runs, [&] { return ImportWith(path, ImportBackend::Assimp); });
        if (!quiet)
            std::fprintf(stderr, "%-6s %-52s %8zu KB  own %s ms  assimp %s ms  import own %s  assimp %s\n", r.format.c_str(), r.name.c_str(), r.bytes / 1024,
                         Ms(r.own).c_str(), Ms(r.assimp).c_str(), Ms(r.impOwn).c_str(), Ms(r.impAssimp).c_str());
        rows.push_back(std::move(r));
    }
    if (!only.empty()) return 0;

    // ---- per-file table (markdown) ----
    std::printf("| Format | File | KB | own read ms | Assimp read ms | time own/A | own peak MB | Assimp peak MB | mem own/A | import own ms | import Assimp ms | import own/A | import own MB | import Assimp MB |\n");
    std::printf("|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n");
    for (const Row& r : rows) {
        const bool both = r.own.ok && r.assimp.ok, ib = r.impOwn.ok && r.impAssimp.ok;
        std::printf("| %s | %s | %zu | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s |\n", r.format.c_str(), r.name.c_str(), r.bytes / 1024,
                    Ms(r.own).c_str(), Ms(r.assimp, r.assimpCrash).c_str(), both ? Ratio(r.own.ms, r.assimp.ms).c_str() : "-",
                    Mb(r.own.peak).c_str(), Mb(r.assimp.peak).c_str(), both ? Ratio((double)r.own.peak, (double)r.assimp.peak).c_str() : "-",
                    Ms(r.impOwn).c_str(), Ms(r.impAssimp, r.assimpCrash).c_str(), ib ? Ratio(r.impOwn.ms, r.impAssimp.ms).c_str() : "-",
                    Mb(r.impOwn.peak).c_str(), Mb(r.impAssimp.peak).c_str());
    }

    // ---- per format, over the files both read: totals, and the files where ours loses ----
    // time: the sum of the medians; memory: the largest peak (what decides whether a big file fits) and the sum of peaks.
    // "met" = ours is not slower in total and its largest peak and summed peaks are not higher, for the read and for the import.
    struct Agg { int files = 0, both = 0, ownOnly = 0, assimpOnly = 0, slower = 0, heavier = 0;
                 double o = 0, a = 0, io = 0, ia = 0; long long po = 0, pa = 0, so = 0, sa = 0, ipo = 0, ipa = 0, iso = 0, isa = 0; };
    std::map<std::string, Agg> agg;
    for (const Row& r : rows) {
        Agg& g = agg[r.format];
        ++g.files;
        if (r.own.ok && !r.assimp.ok) ++g.ownOnly;
        if (!r.own.ok && r.assimp.ok) ++g.assimpOnly;
        if (!(r.own.ok && r.assimp.ok && r.impOwn.ok && r.impAssimp.ok)) continue;
        ++g.both;
        g.o += r.own.ms; g.a += r.assimp.ms; g.io += r.impOwn.ms; g.ia += r.impAssimp.ms;
        g.po = std::max(g.po, r.own.peak); g.pa = std::max(g.pa, r.assimp.peak); g.so += r.own.peak; g.sa += r.assimp.peak;
        g.ipo = std::max(g.ipo, r.impOwn.peak); g.ipa = std::max(g.ipa, r.impAssimp.peak); g.iso += r.impOwn.peak; g.isa += r.impAssimp.peak;
        if (r.own.ms > r.assimp.ms) ++g.slower;
        if (r.own.peak > r.assimp.peak) ++g.heavier;
    }
    std::printf("\n| Format | files | both read | only ours | only Assimp | read own ms | read Assimp ms | time | largest peak own MB | Assimp MB | summed peaks own/A | memory | import own ms | import Assimp ms | import time | import peak own MB | Assimp MB | import memory | files slower | files heavier |\n");
    std::printf("|---|---:|---:|---:|---:|---:|---:|---|---:|---:|---:|---|---:|---:|---|---:|---:|---|---:|---:|\n");
    for (const auto& kv : agg) {
        const Agg& g = kv.second;
        const bool tm = g.both && g.o <= g.a, mm = g.both && g.po <= g.pa && g.so <= g.sa;
        const bool itm = g.both && g.io <= g.ia, imm = g.both && g.ipo <= g.ipa && g.iso <= g.isa;
        auto verdict = [&](bool met, double x, double y) {
            if (!g.both) return std::string("-");
            return std::string(met ? "met " : "NOT met ") + Ratio(x, y);
        };
        std::printf("| %s | %d | %d | %d | %d | %.2f | %.2f | %s | %s | %s | %s | %s | %.2f | %.2f | %s | %s | %s | %s | %d | %d |\n", kv.first.c_str(), g.files, g.both, g.ownOnly, g.assimpOnly,
                    g.o, g.a, verdict(tm, g.o, g.a).c_str(), Mb(g.po).c_str(), Mb(g.pa).c_str(), Ratio((double)g.so, (double)g.sa).c_str(),
                    verdict(mm, (double)g.po, (double)g.pa).c_str(), g.io, g.ia, verdict(itm, g.io, g.ia).c_str(), Mb(g.ipo).c_str(), Mb(g.ipa).c_str(),
                    verdict(imm, (double)g.ipo, (double)g.ipa).c_str(), g.slower, g.heavier);
    }
    for (const Row& r : rows) if (r.assimpCrash) std::printf("\nAssimp crashed (not measured; ours %s): %s\n", r.own.ok ? "reads it" : "refuses it", r.name.c_str());
    std::printf("\n%zu files. Runs per measurement: at least %d (until 50 ms). Assimp: %s. Heap counting: %s.\n", rows.size(), runs, haveAssimp ? "on" : "not built",
#ifdef OKAY_BENCH_COUNT_HEAP
                "on"
#else
                "off (not glibc)"
#endif
    );
    return 0;
}
