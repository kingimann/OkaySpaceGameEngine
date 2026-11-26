// editor/ProjectDiagRunner.hpp. Written from the plain idea of a background job whose last result is kept; nothing copied.
#include "ProjectDiagRunner.hpp"

#include "okay/Scene/Scene.hpp"

#include <utility>

namespace oked {

void ProjectDiagRunner::StartAssetPass(double now) {
    m_lastAsset = now;
    m_filesDirty = false;
    ++m_passes;
    if (m_root.empty()) { m_assetItems.clear(); m_assetRoot.clear(); return; }
    auto j = std::make_shared<Job>();
    j->opt.assetRoot = m_root;
    m_job = j;
    m_call.Start([j] { j->items = okay::CollectProjectAssetDiagnostics(j->opt); }, "okay-project-diag");   // no thread: runs here
}

bool ProjectDiagRunner::CollectAssetPass() {
    if (!m_call.Collect()) return false;
    std::shared_ptr<Job> j = std::move(m_job);
    const bool forNow = j && j->opt.assetRoot == m_root;   // a pass for a project that is no longer open is dropped
    if (forNow) { m_assetItems = std::move(j->items); m_assetRoot = m_root; }
    return forNow;
}

bool ProjectDiagRunner::Tick(const okay::Scene& scene, std::uint64_t sceneHash, const std::string& root, double now) {
    bool changed = false;
    if (root != m_root) {   // another project: its own project-wide check, the old one's results go
        m_call.Drop();   // the old project's pass finishes on its own; its result is not taken
        m_job.reset();
        m_root = root;
        m_assetItems.clear();
        m_assetRoot.clear();
        m_filesDirty = true;
        changed = true;
    }
    if (CollectAssetPass()) changed = true;
    if (!m_call.Busy() && (m_filesDirty || now - m_lastAsset >= refreshSeconds)) StartAssetPass(now);
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
