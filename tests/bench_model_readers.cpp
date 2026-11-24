// Speed and memory of our own model readers (the ones behind the ImportModelData seam: Collada, 3DS, BVH, OFF, SMD,
// MD5, MS3D, ASE, 3MF, AMF, X3D, DXF, LWO, Blender, MD2, MD3, IrrMesh, B3D, X, AC3D, OGRE XML, XGL, Irr scene, NFF,
// Terragen, MDC, HMP, CSM, Q3D, RAW, IQM, COB, PMX, LWS, glTF 1.0, and OBJ / STL / PLY). Until 2026-11-24 it also measured
// Assimp on the same files (docs/content/own_readers_speed.md has those numbers); Assimp is removed, so only ours is measured.
// FBX has its own bench (bench_fbx_reader, ufbx is its other side); glTF 2.0 does not go through this seam (glTF 1.0 does: "gltf1").
//
// Not run by ctest: it prints numbers.
//
// Run:  bench_model_readers [--runs N] [--format F] [--quiet] [--digest | --import-digest | --compare | --dump | --only WHAT]
//                           <file | directory> ...
//       (a directory is searched recursively; with no file argument it reads $OKAY_MODEL_CORPUS)
//   --format F   only the files of one reader: the extension without the dot (dae, 3ds, x, mesh, zae, xml, ...), "ogre" for .mesh.xml, and
//                ac (.ac .acc .ac3d), md5 (.md5mesh .md5anim), q3d (.q3o .q3s), xgl (.xgl .zgl), nff (.nff .enff), lws (.lws .mot)
//   --runs N     at least N timed runs per measurement (default 7); small files run more often, until about 50 ms
//                have passed, so their median is not timer noise
//
// For every file it measures the median wall time and the peak heap of:
//   own-read      ImportModelData(path, Own)    our reader alone: file read and our reader's ImportedModel
//   import-own    ImportModelScene with every format switched to our reader (file read, reader, GameObjects,
//                 MeshRenderer / SkinnedMesh / clips): the full import through the seam
// The peak heap is the most bytes held at once during the call above what was held before it. It is counted by
// replacing malloc / free / realloc / calloc / memalign in this program (glibc only; they forward to glibc's own
// __libc_* functions), so C allocations, our C++ `new` and everything in the importer are counted the same way.
// On other C libraries the memory columns print "-".
//
// --digest         per file, a hash of every field of our reader's ImportedModel (bit for bit), to show a speed change
//                  did not change the output (run before and after, compare the lines)
// --import-digest  per file, a hash of the whole imported scene (our readers on), as SceneSerializer writes it (object
//                  guids, which are random, left out)
// --compare        per file our reader's ImportedModel in numbers (parts, triangles, vertices, nodes, materials, clips,
//                  bones, bounding box in world space), one tab-separated line, to compare two runs
// --dump           per file, our reader's result in words (nodes, materials, parts, notes)
// --only WHAT      own-read | import-own: that call once per file (for a profiler)
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
    // glTF 2.0 does not go through this seam; glTF 1.0 does (our own glTF 1.0 reader, slot 41)
    if (ext == ".gltf" || ext == ".glb") return GltfFileIsVersion1(p.string()) ? "gltf1" : std::string();
    if (ext.size() < 2 || ext == ".fbx") return std::string();
    if (!HasOwnReader(ext)) return std::string();
    // one row per reader: the extensions one reader takes share a key
    static const char* const kSame[][2] = {{".acc", "ac"}, {".ac3d", "ac"}, {".md5mesh", "md5"}, {".md5anim", "md5"}, {".q3o", "q3d"},
                                           {".q3s", "q3d"}, {".zgl", "xgl"}, {".enff", "nff"}, {".mot", "lws"}, {".bsp", "q3bsp"}, {".pk3", "q3bsp"}};
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
    std::string name, format; std::size_t bytes = 0;
    Measure own, impOwn;
};

std::string Mb(long long b) {
    if (b < 0) return "-";
    char s[32]; std::snprintf(s, sizeof(s), "%.2f", (double)b / (1024.0 * 1024.0)); return s;
}
std::string Ms(const Measure& m) {
    if (!m.ok) return "fail";
    char s[32]; std::snprintf(s, sizeof(s), m.ms < 10 ? "%.3f" : "%.1f", m.ms); return s;
}
std::string ShortName(const std::string& path) {
    // parent folder too: the corpus has the same file name in several folders
    return (fs::path(path).parent_path().filename() / fs::path(path).filename()).generic_string();
}

} // namespace

int main(int argc, char** argv) {
    int runs = 7;
    bool quiet = false, digest = false, importDigest = false, compare = false, dump = false;
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
        else if (a == "--dump") dump = true;
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
            if (ownOk) {   // only when our reader reads it
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
    if (dump) {   // per file and per backend: nodes, materials and parts in words (to see what a --compare DIFF is)
        for (const std::string& path : files) {
            {
                ImportedModel m; std::string e;
                const char* who = "own";
                if (!ReadWith(path, ImportBackend::Own, m, &e)) { std::printf("%s %s: error: %s\n", ShortName(path).c_str(), who, e.c_str()); continue; }
                std::printf("%s %s: %zu node(s), %zu material(s), %zu part(s)\n", ShortName(path).c_str(), who, m.nodes.size(), m.materials.size(), m.parts.size());
                for (std::size_t i = 0; i < m.nodes.size(); ++i) std::printf("  node %zu '%s' parent %d\n", i, m.nodes[i].name.c_str(), m.nodes[i].parent);
                for (std::size_t i = 0; i < m.materials.size(); ++i) {
                    const ImportedMaterial& t = m.materials[i];
                    std::printf("  material %zu '%s' diffuse %s(%.3g %.3g %.3g %.3g) shininess %.3g texture '%s'\n", i, t.name.c_str(), t.hasDiffuse ? "" : "unset ",
                                t.diffuse.r, t.diffuse.g, t.diffuse.b, t.diffuse.a, t.shininess, t.texture.c_str());
                }
                for (std::size_t i = 0; i < m.parts.size(); ++i) {
                    const Mesh& me = m.parts[i].mesh;
                    // how many triangles wind the way their vertex normals point (a flipped winding or normal shows as a low share)
                    std::size_t agree = 0;
                    if (me.HasNormals())
                        for (std::size_t t = 0; t + 2 < me.triangles.size(); t += 3) {
                            const std::size_t a = (std::size_t)me.triangles[t], b = (std::size_t)me.triangles[t + 1], c = (std::size_t)me.triangles[t + 2];
                            const Vec3 fn = Vec3::Cross(me.vertices[b] - me.vertices[a], me.vertices[c] - me.vertices[a]);
                            const Vec3 vn = me.normals[a] + me.normals[b] + me.normals[c];
                            if (fn.x * vn.x + fn.y * vn.y + fn.z * vn.z > 0) ++agree;
                        }
                    std::printf("  part %zu node %d material %d: %zu vertices, %zu triangles, normals %s (%zu triangles wind with them), uvs %s\n", i, m.parts[i].node,
                                m.parts[i].material, me.vertices.size(), me.TriangleCount(), me.HasNormals() ? "yes" : "no", agree, me.uvs.empty() ? "no" : "yes");
                }
                for (const std::string& n : m.notes) std::printf("  note: %s\n", n.c_str());
            }
        }
        return 0;
    }
    if (compare) {
        for (const std::string& path : files) {
            ImportedModel a; std::string ea;
            const bool oa = ReadWith(path, ImportBackend::Own, a, &ea);
            std::printf("%s\t%s\town=%s", FormatOf(path).c_str(), ShortName(path).c_str(), oa ? "ok" : "fail");
            if (!oa) { std::printf("\t%s\n", ea.c_str()); continue; }
            const Stats s = StatsOf(a);
            std::printf("\tparts=%d\ttris=%d\tverts=%d\tnodes=%d\tmats=%d\tclips=%d\tbones=%d\tbox=(%.4g %.4g %.4g)-(%.4g %.4g %.4g)\n", s.parts, s.tris,
                        s.verts, s.nodes, s.mats, s.clips, s.bones, s.lo.x, s.lo.y, s.lo.z, s.hi.x, s.hi.y, s.hi.z);
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
            else if (only == "import-own") ok = ImportWith(path, ImportBackend::Own);
            else { std::fprintf(stderr, "unknown --only %s\n", only.c_str()); return 1; }
            std::printf("%s %s\n", r.name.c_str(), ok ? "ok" : "fail");
            continue;
        }
        r.own = Run(runs, [&] { ImportedModel im; return ReadWith(path, ImportBackend::Own, im); });
        if (r.own.ok) r.impOwn = Run(runs, [&] { return ImportWith(path, ImportBackend::Own); });
        if (!quiet)
            std::fprintf(stderr, "%-6s %-52s %8zu KB  own %s ms  import own %s\n", r.format.c_str(), r.name.c_str(), r.bytes / 1024,
                         Ms(r.own).c_str(), Ms(r.impOwn).c_str());
        rows.push_back(std::move(r));
    }
    if (!only.empty()) return 0;

    // ---- per-file table (markdown) ----
    std::printf("| Format | File | KB | own read ms | own peak MB | import own ms | import own MB |\n");
    std::printf("|---|---|---:|---:|---:|---:|---:|\n");
    for (const Row& r : rows)
        std::printf("| %s | %s | %zu | %s | %s | %s | %s |\n", r.format.c_str(), r.name.c_str(), r.bytes / 1024,
                    Ms(r.own).c_str(), Mb(r.own.peak).c_str(), Ms(r.impOwn).c_str(), Mb(r.impOwn.peak).c_str());

    // ---- per format: totals (time: the sum of the medians; memory: the largest peak and the sum of peaks) ----
    struct Agg { int files = 0, read = 0; double o = 0, io = 0; long long po = 0, so = 0, ipo = 0; };
    std::map<std::string, Agg> agg;
    for (const Row& r : rows) {
        Agg& g = agg[r.format];
        ++g.files;
        if (!r.own.ok) continue;
        ++g.read;
        g.o += r.own.ms; g.po = std::max(g.po, r.own.peak); g.so += r.own.peak;
        if (r.impOwn.ok) { g.io += r.impOwn.ms; g.ipo = std::max(g.ipo, r.impOwn.peak); }
    }
    std::printf("\n| Format | files | read | read own ms | largest peak own MB | summed peaks MB | import own ms | import peak own MB |\n");
    std::printf("|---|---:|---:|---:|---:|---:|---:|---:|\n");
    for (const auto& kv : agg) {
        const Agg& g = kv.second;
        std::printf("| %s | %d | %d | %.2f | %s | %s | %.2f | %s |\n", kv.first.c_str(), g.files, g.read, g.o, Mb(g.po).c_str(), Mb(g.so).c_str(), g.io, Mb(g.ipo).c_str());
    }
    std::printf("\n%zu files. Runs per measurement: at least %d (until 50 ms). Heap counting: %s.\n", rows.size(), runs,
#ifdef OKAY_BENCH_COUNT_HEAP
                "on"
#else
                "off (not glibc)"
#endif
    );
    return 0;
}
