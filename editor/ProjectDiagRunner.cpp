// editor/ProjectDiagRunner.hpp. Written from the plain idea of a background job whose last result is kept; nothing copied.
#include "ProjectDiagRunner.hpp"

#include "../shared/OkayThreads.hpp"
#include "okay/Scene/Scene.hpp"

#include <atomic>

namespace oked {

struct ProjectDiagRunner::Job {
    okay::ProjectDiagOptions opt;
    std::vector<okay::Diagnostic> items;
    std::atomic<int> state{0};   // 0 running, 1 finished, 2 the runner is gone (the worker frees the job)
};

namespace {
int AssetWorker(void* p) {
    auto* j = static_cast<ProjectDiagRunner::Job*>(p);
    j->items = okay::CollectProjectAssetDiagnostics(j->opt);
    if (j->state.exchange(1) == 2) delete j;
    return 0;
}
}  // namespace

ProjectDiagRunner::~ProjectDiagRunner() { Drop(); }

void ProjectDiagRunner::Drop() {
    if (!m_job) return;
    if (m_job->state.exchange(2) == 1) delete m_job;   // finished already: ours to free; otherwise the worker frees it
    okay::thr::DetachThread(m_thread);
    m_job = nullptr;
    m_thread = nullptr;
}

void ProjectDiagRunner::StartAssetPass(double now) {
    m_lastAsset = now;
    m_filesDirty = false;
    ++m_passes;
    Job* j = new Job();
    j->opt.assetRoot = m_root;
    if (m_root.empty()) { m_assetItems.clear(); m_assetRoot.clear(); delete j; return; }
    okay::thr::Thread* t = okay::thr::CreateThread(&AssetWorker, "okay-project-diag", j);
    if (!t) {   // no thread: run it here, as before
        m_assetItems = okay::CollectProjectAssetDiagnostics(j->opt);
        m_assetRoot = m_root;
        delete j;
        return;
    }
    m_job = j;
    m_thread = t;
}

bool ProjectDiagRunner::CollectAssetPass() {
    if (!m_job || m_job->state.load() != 1) return false;
    okay::thr::WaitThread(m_thread, nullptr);   // it has finished: this only collects it
    m_thread = nullptr;
    Job* j = m_job;
    m_job = nullptr;
    const bool forNow = j->opt.assetRoot == m_root;   // a pass for a project that is no longer open is dropped
    if (forNow) { m_assetItems = std::move(j->items); m_assetRoot = m_root; }
    delete j;
    return forNow;
}

bool ProjectDiagRunner::Tick(const okay::Scene& scene, std::uint64_t sceneHash, const std::string& root, double now) {
    bool changed = false;
    if (root != m_root) {   // another project: its own project-wide check, the old one's results go
        Drop();
        m_root = root;
        m_assetItems.clear();
        m_assetRoot.clear();
        m_filesDirty = true;
        changed = true;
    }
    if (CollectAssetPass()) changed = true;
    if (!m_job && (m_filesDirty || now - m_lastAsset >= refreshSeconds)) StartAssetPass(now);
    if (!m_haveScene || (sceneHash != m_sceneHash && now - m_lastScene >= sceneThrottle)) {
        okay::ProjectDiagOptions opt;
        opt.assetRoot = m_root;
        opt.checkAssetIndex = false;   // the project-wide part runs on the worker above
        m_sceneItems = okay::CollectProjectDiagnostics(scene, opt).items;
        m_sceneHash = sceneHash;
        m_lastScene = now;
        m_haveScene = true;
        changed = true;
    }
    if (changed) {
        m_items = m_sceneItems;
        okay::MergeProjectAssetDiagnostics(m_items, m_assetItems);
    }
    return changed;
}

}  // namespace oked
