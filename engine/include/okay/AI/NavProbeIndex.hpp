#pragma once
#include "okay/Math/Vec3.hpp"
#include "okay/Physics/Physics3D.hpp"
#include <cstdint>
#include <vector>

namespace okay {

class Scene;
class GameObject;
class Collider3D;

/// A grid over the ground plane of every 3D collider's world box, for the no-NavMesh path search
/// (NavGrid3D, Finding 107).
///
/// The grid search tests up to 4,096 cells per path, and each cell costs a down ray and a sphere
/// overlap. Those used to test every collider in the scene, so one search in a level with 1,000
/// colliders did millions of shape tests. With this index a probe only tests the colliders whose box
/// is near it, through Physics3D::RaycastAmong / OverlapSphereAmong, which run the same per-collider
/// steps as Raycast / OverlapSphere. A collider whose box is nowhere near a probe could not have been
/// hit by it, so the answers, and therefore the paths, are the same as before.
///
/// Keeping it current: the collider list is re-fetched when the scene's objects or components change;
/// the boxes are measured again at the start of every path search (one box per collider per search,
/// not per probe), and the grid is rebuilt only when a box moved. So a moving door or a pushed crate
/// is seen by the very next search.
///
/// Turn it off (for comparisons) with SetEnabled(false) or the environment variable
/// OKAY_NAV_PROBE_INDEX=0; the search then probes the whole scene as it used to.
class NavProbeIndex {
public:
    /// The index for `scene`, brought up to date (see above). Call once per path search.
    static NavProbeIndex& For(Scene& scene);

    static bool Enabled();
    static void SetEnabled(bool on);

    /// The down ray of a grid probe: exactly Physics3D::Raycast(scene, origin, {0,-1,0}, maxDistance, ignore).
    RaycastHit3D CastDown(Scene& scene, const Vec3& origin, float maxDistance, GameObject* ignore);
    /// The sphere probe: exactly the colliders Physics3D::OverlapSphere(scene, center, radius) returns
    /// (all layers, triggers included, nothing ignored), in an unspecified order.
    const std::vector<Collider3D*>& OverlapSphere(Scene& scene, const Vec3& center, float radius);

    // ---- Diagnostics (tests, docs/perf_log) ----
    std::size_t ColliderCount() const { return m_colliders.size(); }
    std::size_t AlwaysCount() const { return m_always.size(); }   ///< colliders tested by every probe (huge, unbounded)
    int CellsX() const { return m_nx; }
    int CellsZ() const { return m_nz; }
    float CellSize() const { return m_cell; }
    static std::uint64_t GridBuilds();     ///< times any index rebuilt its grid (all scenes)
    static std::uint64_t Probes();         ///< probes answered through an index
    static std::uint64_t ShapeTests();     ///< colliders handed to the exact tests through an index

private:
    void Refresh(Scene& scene);
    void BuildGrid();
    void Gather(float x0, float y0, float z0, float x1, float y1, float z1);   // candidates whose box meets this box

    std::uint64_t m_uid = 0, m_structure = 0, m_components = 0;
    bool m_listed = false, m_built = false;
    std::vector<Collider3D*> m_colliders;        // Physics3D::QueryColliders order
    std::vector<Vec3> m_mn, m_mx;                // each collider's world box, padded
    std::vector<int> m_always;                   // indices tested by every probe (ascending)
    float m_cell = 2.0f, m_x0 = 0.0f, m_z0 = 0.0f;
    int m_nx = 0, m_nz = 0;
    std::vector<int> m_cellStart, m_cellItems;   // per cell, the indices of the boxes over it (ascending)
    std::vector<int> m_scratchIdx;
    std::vector<std::uint32_t> m_seen;           // per collider: the gather that last took it
    std::uint32_t m_gather = 0;
    std::vector<Collider3D*> m_candidates, m_hits;
};

} // namespace okay
