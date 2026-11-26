#pragma once
// When the Problems list (editor/ProblemsPanel.cpp, TickProjectDiagnostics) runs its checks. The scene's own checks are cheap and run
// on the editor's thread when the scene changed (at most every 0.25 s). The project-wide check reads every text asset of the
// project (okay::CollectProjectAssetDiagnostics): the owner's okay_editor_perf.log on 2026-11-25 shows it at 1.0-1.3 s, and it ran
// in the frame on every scene change (a rename, an added component). It does not depend on the scene, so it now runs on a worker:
// when the project opens, then every `refreshSeconds` and after InvalidateFiles, never twice at once; its last result is merged in.
// No ImGui here (tests/test_project_diag_runner.cpp). Threads through okay::thr (CLAUDE.md rule 21a).
#include "okay/Debug/Diagnostics.hpp"
#include "okay/Debug/ProjectDiagnostics.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace okay { class Scene; }
namespace okay::thr { struct Thread; }

namespace oked {

class ProjectDiagRunner {
public:
    double refreshSeconds = 20.0;   // the project-wide check again after this long
    double sceneThrottle = 0.25;    // the scene's checks at most this often

    ProjectDiagRunner() = default;
    ~ProjectDiagRunner();
    ProjectDiagRunner(const ProjectDiagRunner&) = delete;
    ProjectDiagRunner& operator=(const ProjectDiagRunner&) = delete;

    /// Every frame (edit mode). `sceneHash` changes when the scene does; `root` is the project folder ("" = none); `now` seconds
    /// on a steady clock. True when Items() changed (the caller hands them to EditorDiagnostics).
    bool Tick(const okay::Scene& scene, std::uint64_t sceneHash, const std::string& root, double now);
    const std::vector<okay::Diagnostic>& Items() const { return m_items; }
    /// Files changed on disk (a save, an import): the project-wide check runs again soon.
    void InvalidateFiles() { m_filesDirty = true; }
    int AssetPassesStarted() const { return m_passes; }
    bool AssetPassRunning() const { return m_job != nullptr; }

    struct Job;   // one project-wide pass on its worker (ProjectDiagRunner.cpp)

private:
    Job* m_job = nullptr;
    okay::thr::Thread* m_thread = nullptr;
    std::vector<okay::Diagnostic> m_sceneItems, m_assetItems, m_items;
    std::string m_root, m_assetRoot;          // the project now; the one m_assetItems is for
    std::uint64_t m_sceneHash = 0;
    double m_lastScene = -1e9, m_lastAsset = -1e9;
    bool m_haveScene = false, m_filesDirty = true;
    int m_passes = 0;

    void StartAssetPass(double now);
    bool CollectAssetPass();                 // a finished pass taken in: true when it changed the items
    void Drop();
};

}  // namespace oked
