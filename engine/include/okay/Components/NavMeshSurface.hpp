#pragma once
#include "okay/AI/NavMesh.hpp"
#include "okay/AI/NavMeshBake.hpp"
#include "okay/Scene/Component.hpp"
#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace okay {

class Scene;

/// The scene's navigation mesh: bake settings, the baked mesh and the `.okaynav`
/// file it lives in (next to the scene, referenced by path). One per scene is the
/// normal case. Bake() builds the mesh from the static colliders synchronously;
/// StartBake() does it on a worker thread with progress, PollBake() takes the
/// result. The mesh loads from `navPath` the first time it is needed.
class NavMeshSurface : public Component {
public:
    NavMeshBuildSettings settings;
    /// The .okaynav file (written by Bake). Absolute, or relative to the scene file / project.
    std::string navPath;
    /// Bake the union of the static geometry (padded), or the box below.
    bool autoBounds = true;
    Vec3 boundsCenter{0, 0, 0};
    Vec3 boundsSize{50, 20, 50};
    /// Also bake bodies with a dynamic Rigidbody3D (off: only static / kinematic ones).
    bool includeDynamic = false;
    /// Collider layers that take part (bit n = layer n).
    std::uint32_t layerMask = 0xFFFFFFFFu;
    /// Editor: bake again every time Play starts.
    bool bakeBeforePlay = false;
    /// Folder of the scene file this surface was loaded from (resolves a relative navPath).
    std::string sceneDir;

    NavMeshSurface() = default;
    ~NavMeshSurface() override;
    NavMeshSurface(const NavMeshSurface&) = delete;
    NavMeshSurface& operator=(const NavMeshSurface&) = delete;

    /// The baked mesh, loading navPath on first use; nullptr when there is none.
    const NavMesh* Mesh();
    bool HasMesh() const { return m_mesh && !m_mesh->Empty(); }
    /// Replace the mesh (takes ownership).
    void SetMesh(std::unique_ptr<NavMesh> mesh);
    /// Drop the baked mesh (the file is left alone).
    void Clear();

    /// Bake now, on this thread. False (with `error`) when it was refused.
    bool Bake(Scene& scene, NavBakeStats* stats = nullptr, std::string* error = nullptr);
    /// Bake on a worker thread. False when one is already running.
    bool StartBake(Scene& scene);
    bool IsBaking() const { return m_job != nullptr; }
    float BakeProgress() const;
    /// Take a finished background bake (call every frame; Update does). True once,
    /// when a bake just finished; `ok` says whether it produced a mesh.
    bool PollBake(bool* ok = nullptr);
    void CancelBake();
    /// Stats of the last bake that finished on this surface.
    const NavBakeStats& LastStats() const { return m_stats; }

    /// Write the mesh to `path` ("" = navPath).
    bool Save(const std::string& path = std::string()) const;
    /// Load a mesh file ("" = navPath). False (with `error`) when it can't be read.
    bool Load(const std::string& path = std::string(), std::string* error = nullptr);
    /// Where navPath resolves to on this machine (first existing candidate).
    std::string ResolvedPath() const;
    /// Project folder relative navPaths are tried against (the editor sets it).
    static void SetAssetBaseDir(const std::string& dir);

    /// Copy the scene's bakeable geometry with this surface's options (main thread).
    void GatherInput(Scene& scene, NavBakeInput& out) const;

    void Start() override { Mesh(); }
    void Update(float) override { PollBake(); }

private:
    struct Job {
        std::thread thread;
        std::atomic<float> progress{0.0f};
        std::atomic<bool> done{false}, cancel{false}, ok{false};
        std::unique_ptr<NavMesh> result;
        NavBakeStats stats;
    };
    std::unique_ptr<NavMesh> m_mesh;
    std::unique_ptr<Job> m_job;
    NavBakeStats m_stats;
    std::string m_triedPath;   // the navPath the lazy load last attempted
};

} // namespace okay
