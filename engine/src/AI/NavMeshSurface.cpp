#include <cmath>
#include "okay/Components/NavMeshSurface.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Debug/DebugUtil.hpp"
#include "okay/Scene/Scene.hpp"

#include <cstring>
#include <filesystem>
#include <map>
#include <vector>

namespace okay {

namespace {
std::string& AssetBaseDir() { static std::string dir; return dir; }
std::string FileName(const std::string& p) {
    const std::size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? p : p.substr(s + 1);
}
bool Exists(const std::string& p) {
    std::error_code ec;
    return !p.empty() && std::filesystem::is_regular_file(std::filesystem::u8path(p), ec);
}
} // namespace

void NavMeshSurface::SetAssetBaseDir(const std::string& dir) { AssetBaseDir() = dir; }

NavMeshSurface::~NavMeshSurface() { CancelBake(); }

std::string NavMeshSurface::ResolvedPath() const {
    if (navPath.empty()) return std::string();
    if (Exists(navPath)) return navPath;
    const bool relative = !std::filesystem::u8path(navPath).is_absolute();
    if (!sceneDir.empty()) {
        const std::string next = sceneDir + FileName(navPath);
        if (Exists(next)) return next;
        if (relative && Exists(sceneDir + navPath)) return sceneDir + navPath;
    }
    if (relative && !AssetBaseDir().empty()) {
        const std::string inProject = AssetBaseDir() + "/" + navPath;
        if (Exists(inProject)) return inProject;
    }
    return navPath;
}


namespace {
// What a bake was made from, as plain numbers: equal keys mean equal bake input, bit for bit.
void PutF(std::vector<std::uint32_t>& k, float f) { std::uint32_t u; std::memcpy(&u, &f, 4); k.push_back(u); }
void PutV(std::vector<std::uint32_t>& k, const Vec3& v) { PutF(k, v.x); PutF(k, v.y); PutF(k, v.z); }
std::vector<std::uint32_t> BakeKey(const NavBakeInput& in, const NavMeshBuildSettings& s) {
    std::vector<std::uint32_t> k;
    PutF(k, s.agentRadius); PutF(k, s.agentHeight); PutF(k, s.maxSlope); PutF(k, s.maxStep); PutF(k, s.cellSize); PutF(k, s.cellHeight);
    k.push_back(in.hasBounds ? 1u : 0u); PutV(k, in.boundsMin); PutV(k, in.boundsMax);
    k.push_back((std::uint32_t)in.boxes.size());
    for (const auto& b : in.boxes) { PutV(k, b.center); PutV(k, b.half); PutF(k, b.rot.x); PutF(k, b.rot.y); PutF(k, b.rot.z); PutF(k, b.rot.w); k.push_back((std::uint32_t)b.area); }
    k.push_back((std::uint32_t)in.meshes.size());   // Mesh Collider triangles, already in world space
    for (const auto& m : in.meshes) {
        k.push_back((std::uint32_t)m.area); k.push_back(m.twoSided ? 1u : 0u); k.push_back((std::uint32_t)m.corners.size());
        for (const Vec3& c : m.corners) PutV(k, c);
    }
    k.push_back((std::uint32_t)in.maxMeshTriangles); k.push_back((std::uint32_t)((std::uint64_t)in.maxMeshTriangles >> 32));
    k.push_back((std::uint32_t)in.maxMeshColumnVisits); k.push_back((std::uint32_t)((std::uint64_t)in.maxMeshColumnVisits >> 32));
    k.push_back((std::uint32_t)in.spheres.size());
    for (const auto& b : in.spheres) { PutV(k, b.center); PutF(k, b.radius); k.push_back((std::uint32_t)b.area); }
    k.push_back((std::uint32_t)in.capsules.size());
    for (const auto& b : in.capsules) { PutV(k, b.a); PutV(k, b.b); PutF(k, b.radius); k.push_back((std::uint32_t)b.area); }
    k.push_back((std::uint32_t)in.terrains.size());
    for (const auto& t : in.terrains) {
        for (int i = 0; i < 16; ++i) PutF(k, t.l2w.m[i]);
        PutF(k, t.size); k.push_back((std::uint32_t)t.dim); k.push_back((std::uint32_t)t.area);
        PutV(k, t.aabbMin); PutV(k, t.aabbMax);
        k.push_back((std::uint32_t)t.heights.size());
        for (float h : t.heights) PutF(k, h);
    }
    return k;
}

struct BakeRecord { std::vector<std::uint32_t> key; std::uintmax_t size = 0; std::int64_t time = 0; };
std::string NormPath(const std::string& p) {
    std::error_code ec;
    const auto c = std::filesystem::weakly_canonical(std::filesystem::u8path(p), ec);
    return ec ? p : c.generic_u8string();
}
debugutil::SpinLock& BakeRecordLock() { static debugutil::SpinLock l; return l; }
std::map<std::string, BakeRecord>& BakeRecords() { static std::map<std::string, BakeRecord> r; return r; }

bool FileStamp(const std::string& path, std::uintmax_t& size, std::int64_t& time) {
    std::error_code ec;
    const auto fp = std::filesystem::u8path(path);
    size = std::filesystem::file_size(fp, ec);
    if (ec) return false;
    const auto t = std::filesystem::last_write_time(fp, ec);
    if (ec) return false;
    time = (std::int64_t)t.time_since_epoch().count();
    return true;
}
}  // namespace

const NavMesh* NavMeshSurface::Mesh() {
    if (!m_mesh && !navPath.empty() && m_triedPath != navPath) {
        m_triedPath = navPath;
        std::string err;
        if (!Load(std::string(), &err)) Log::Warning("Nav Mesh Surface: ", err);
    }
    return (m_mesh && !m_mesh->Empty()) ? m_mesh.get() : nullptr;
}

void NavMeshSurface::SetMesh(std::unique_ptr<NavMesh> mesh) { m_mesh = std::move(mesh); m_bakeKey.clear(); DropCarve(); }

void NavMeshSurface::Clear() { m_mesh.reset(); m_bakeKey.clear(); DropCarve(); m_triedPath.clear(); m_stats = NavBakeStats{}; }

void NavMeshSurface::DropCarve() {
    if (m_carved || !m_carveVolumes.empty()) ++m_carveGen;
    m_carved.reset(); m_carveVolumes.clear(); m_carvedPolys = 0;
}

const NavMesh* NavMeshSurface::ApplyCarving(const std::vector<NavCarveVolume>& volumes) {
    const NavMesh* base = Mesh();
    if (!base) return nullptr;
    if (volumes.empty()) {
        if (m_carved) { m_carved.reset(); m_carveVolumes.clear(); m_carvedPolys = 0; ++m_carveGen; }
        return base;
    }
    bool same = m_carved && m_carveVolumes.size() == volumes.size();
    for (std::size_t i = 0; same && i < volumes.size(); ++i) same = volumes[i].Delta(m_carveVolumes[i]) < 1e-5f;
    if (same) return m_carved.get();
    auto carved = std::make_unique<NavMesh>();
    m_carvedPolys = base->Carve(volumes, *carved);
    m_carved = std::move(carved);
    m_carveVolumes = volumes;
    ++m_carveGen;
    return m_carved.get();
}

void NavMeshSurface::GatherInput(Scene& scene, NavBakeInput& out) const {
    NavMeshBaker::Gather(scene, out, includeDynamic, layerMask);
    out.hasBounds = !autoBounds;
    if (!autoBounds) {
        out.boundsMin = boundsCenter - boundsSize * 0.5f;
        out.boundsMax = boundsCenter + boundsSize * 0.5f;
    }
}

bool NavMeshSurface::Bake(Scene& scene, NavBakeStats* stats, std::string* error) {
    if (m_job) { if (error) *error = "a bake is already running"; return false; }
    NavBakeInput input;
    GatherInput(scene, input);
    auto mesh = std::make_unique<NavMesh>();
    NavBakeStats st;
    if (!NavMeshBaker::Bake(input, settings, *mesh, nullptr, nullptr, &st)) {
        const Vec3 lo = input.boundsMin, hi = input.boundsMax;
        const bool badBounds = input.hasBounds && !(std::isfinite(lo.x) && std::isfinite(lo.y) && std::isfinite(lo.z) &&
                                                    std::isfinite(hi.x) && std::isfinite(hi.y) && std::isfinite(hi.z));
        if (badBounds) { if (error) *error = "the bake bounds contain a NaN or infinite number (fix the Bounds Center / Size)"; return false; }
        if (error) *error = "the bake area is too large for this cell size (raise Cell Size or shrink the bounds)";
        return false;
    }
    m_mesh = std::move(mesh);
    m_bakeKey = BakeKey(input, settings);
    DropCarve();
    m_stats = st;
    m_triedPath = navPath;
    if (stats) *stats = st;
    return true;
}

bool NavMeshSurface::BakeIfChanged(Scene& scene, bool* skipped, NavBakeStats* stats, std::string* error) {
    if (skipped) *skipped = false;
    if (m_job) { if (error) *error = "a bake is already running"; return false; }
    if (Mesh()) {   // loads the .okaynav first when the mesh is not in memory yet
        NavBakeInput input;
        GatherInput(scene, input);
        const std::vector<std::uint32_t> key = BakeKey(input, settings);
        bool same = !m_bakeKey.empty() && m_bakeKey == key;
        if (!same && m_bakeKey.empty() && !navPath.empty()) {   // a mesh read back from its file: was that file baked from this input?
            const std::string file = NormPath(ResolvedPath());
            std::uintmax_t size = 0; std::int64_t time = 0;
            same = FileStamp(file, size, time);
            if (same) {
                debugutil::SpinGuard lock(BakeRecordLock());
                auto it = BakeRecords().find(file);
                same = it != BakeRecords().end() && it->second.key == key && it->second.size == size && it->second.time == time;
            }
            if (same) m_bakeKey = key;
        }
        if (same) { if (skipped) *skipped = true; return true; }
    }
    return Bake(scene, stats, error);
}

bool NavMeshSurface::StartBake(Scene& scene) {
    if (m_job) return false;
    auto job = std::make_unique<Job>();
    auto input = std::make_shared<NavBakeInput>();
    GatherInput(scene, *input);
    job->result = std::make_unique<NavMesh>();
    Job* j = job.get();
    const NavMeshBuildSettings s = settings;
    j->key = BakeKey(*input, settings);
    // The job only touches `*j`, which lives until PollBake has seen `done` or CancelBake has
    // cancelled or waited for the job (the destructor cancels), so it never outlives the surface.
    JobSystem& sys = EngineJobs();
    j->sys = &sys;
    JobOptions opt;
    opt.priority = JobPriority::Low;   // frame work (physics, skinning) goes first
    opt.name = "NavMesh bake";
    m_job = std::move(job);
    try {
        j->handle = sys.Submit([j, input, s]() {
            bool ok = false;
            try { ok = NavMeshBaker::Bake(*input, s, *j->result, &j->progress, &j->cancel, &j->stats); } catch (...) { ok = false; }
            j->ok.store(ok);
            j->done.store(true);   // the last touch of *j
        }, opt);
    } catch (...) {
        m_job.reset();   // could not queue the bake
        return false;
    }
    return true;
}

float NavMeshSurface::BakeProgress() const { return m_job ? m_job->progress.load() : 0.0f; }

bool NavMeshSurface::PollBake(bool* ok) {
    if (!m_job || !m_job->done.load()) return false;   // (after `done` the job no longer touches m_job)
    const bool good = m_job->ok.load() && !m_job->cancel.load();
    if (good) { m_mesh = std::move(m_job->result); m_bakeKey = std::move(m_job->key); DropCarve(); m_stats = m_job->stats; m_triedPath = navPath; }
    m_job.reset();
    if (ok) *ok = good;
    return true;
}

void NavMeshSurface::CancelBake() {
    if (!m_job) return;
    m_job->cancel.store(true);   // a running bake stops at its next check
    if (m_job->sys && !m_job->done.load() && m_job->handle.Valid() && !m_job->sys->Cancel(m_job->handle)) {
        try { m_job->sys->Wait(m_job->handle); } catch (...) {}
    }
    m_job.reset();
}

bool NavMeshSurface::Save(const std::string& path) const {
    const std::string p = path.empty() ? navPath : path;
    if (p.empty() || !m_mesh) return false;
    std::error_code ec;
    const std::filesystem::path fp = std::filesystem::u8path(p);
    if (fp.has_parent_path()) std::filesystem::create_directories(fp.parent_path(), ec);
    if (!m_mesh->SaveToFile(p)) return false;
    if (!m_bakeKey.empty()) {   // remember which input this file was baked from (see BakeIfChanged)
        std::uintmax_t size = 0; std::int64_t time = 0;
        if (FileStamp(p, size, time)) {
            debugutil::SpinGuard lock(BakeRecordLock());
            BakeRecords()[NormPath(p)] = BakeRecord{m_bakeKey, size, time};
        }
    }
    return true;
}

bool NavMeshSurface::Load(const std::string& path, std::string* error) {
    const std::string p = path.empty() ? ResolvedPath() : path;
    if (p.empty()) { if (error) *error = "no nav mesh file set"; return false; }
    auto mesh = std::make_unique<NavMesh>();
    if (!mesh->LoadFromFile(p, error)) return false;
    m_mesh = std::move(mesh);
    m_bakeKey.clear();
    DropCarve();
    m_stats = NavBakeStats{};
    m_stats.polys = (int)m_mesh->polys.size(); m_stats.links = (int)m_mesh->links.size();
    m_stats.vertices = (int)m_mesh->vertices.size();
    if (path.empty()) m_triedPath = navPath;
    return true;
}

} // namespace okay
