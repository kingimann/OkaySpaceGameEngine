#include "okay/Components/NavMeshSurface.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Scene/Scene.hpp"

#include <filesystem>

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

const NavMesh* NavMeshSurface::Mesh() {
    if (!m_mesh && !navPath.empty() && m_triedPath != navPath) {
        m_triedPath = navPath;
        std::string err;
        if (!Load(std::string(), &err)) Log::Warning("Nav Mesh Surface: ", err);
    }
    return (m_mesh && !m_mesh->Empty()) ? m_mesh.get() : nullptr;
}

void NavMeshSurface::SetMesh(std::unique_ptr<NavMesh> mesh) { m_mesh = std::move(mesh); }

void NavMeshSurface::Clear() { m_mesh.reset(); m_triedPath.clear(); m_stats = NavBakeStats{}; }

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
        if (error) *error = "the bake area is too large for this cell size (raise Cell Size or shrink the bounds)";
        return false;
    }
    m_mesh = std::move(mesh);
    m_stats = st;
    m_triedPath = navPath;
    if (stats) *stats = st;
    return true;
}

bool NavMeshSurface::StartBake(Scene& scene) {
    if (m_job) return false;
    auto job = std::make_unique<Job>();
    auto input = std::make_shared<NavBakeInput>();
    GatherInput(scene, *input);
    job->result = std::make_unique<NavMesh>();
    Job* j = job.get();
    const NavMeshBuildSettings s = settings;
    j->thread = std::thread([j, input, s]() {
        j->ok.store(NavMeshBaker::Bake(*input, s, *j->result, &j->progress, &j->cancel, &j->stats));
        j->done.store(true);
    });
    m_job = std::move(job);
    return true;
}

float NavMeshSurface::BakeProgress() const { return m_job ? m_job->progress.load() : 0.0f; }

bool NavMeshSurface::PollBake(bool* ok) {
    if (!m_job || !m_job->done.load()) return false;
    if (m_job->thread.joinable()) m_job->thread.join();
    const bool good = m_job->ok.load() && !m_job->cancel.load();
    if (good) { m_mesh = std::move(m_job->result); m_stats = m_job->stats; m_triedPath = navPath; }
    m_job.reset();
    if (ok) *ok = good;
    return true;
}

void NavMeshSurface::CancelBake() {
    if (!m_job) return;
    m_job->cancel.store(true);
    if (m_job->thread.joinable()) m_job->thread.join();
    m_job.reset();
}

bool NavMeshSurface::Save(const std::string& path) const {
    const std::string p = path.empty() ? navPath : path;
    if (p.empty() || !m_mesh) return false;
    std::error_code ec;
    const std::filesystem::path fp = std::filesystem::u8path(p);
    if (fp.has_parent_path()) std::filesystem::create_directories(fp.parent_path(), ec);
    return m_mesh->SaveToFile(p);
}

bool NavMeshSurface::Load(const std::string& path, std::string* error) {
    const std::string p = path.empty() ? ResolvedPath() : path;
    if (p.empty()) { if (error) *error = "no nav mesh file set"; return false; }
    auto mesh = std::make_unique<NavMesh>();
    if (!mesh->LoadFromFile(p, error)) return false;
    m_mesh = std::move(mesh);
    m_stats = NavBakeStats{};
    m_stats.polys = (int)m_mesh->polys.size(); m_stats.links = (int)m_mesh->links.size();
    m_stats.vertices = (int)m_mesh->vertices.size();
    if (path.empty()) m_triedPath = navPath;
    return true;
}

} // namespace okay
