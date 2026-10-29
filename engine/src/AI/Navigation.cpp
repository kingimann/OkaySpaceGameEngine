#include "okay/AI/Navigation.hpp"

#include "okay/AI/NavGrid3D.hpp"
#include "okay/AI/NavMesh.hpp"
#include "okay/Components/NavMeshAgent.hpp"
#include "okay/Components/NavMeshObstacle.hpp"
#include "okay/Components/NavMeshSurface.hpp"
#include "okay/Scene/Scene.hpp"

#include <algorithm>

namespace okay {

namespace {

// What a scene has for navigation, re-found only when its objects or components change.
struct SceneCache {
    std::uint64_t uid = 0, structure = 0, components = 0;
    NavMeshSurface* surface = nullptr;
    std::vector<NavMeshAgent*> agents;
    std::vector<NavMeshObstacle*> obstacles;
};

SceneCache& CacheFor(Scene& scene) {
    static std::vector<SceneCache> caches;
    SceneCache* c = nullptr;
    for (SceneCache& k : caches) if (k.uid == scene.Uid()) { c = &k; break; }
    if (!c) {
        if (caches.size() >= 16) caches.erase(caches.begin());
        caches.push_back(SceneCache{}); c = &caches.back(); c->uid = scene.Uid();
        c->structure = scene.StructureVersion() - 1;   // force a refresh
    }
    if (c->structure != scene.StructureVersion() || c->components != scene.ComponentVersion()) {
        c->structure = scene.StructureVersion(); c->components = scene.ComponentVersion();
        c->surface = nullptr;
        for (NavMeshSurface* s : scene.FindObjectsOfType<NavMeshSurface>())
            if (s && s->enabled && s->gameObject && s->gameObject->active) { c->surface = s; break; }
        c->agents = scene.FindObjectsOfType<NavMeshAgent>();
        c->obstacles = scene.FindObjectsOfType<NavMeshObstacle>();
    }
    return *c;
}

const Vec3 kSnap{2.0f, 4.0f, 2.0f};   // how far a start / goal may be from the mesh

} // namespace

NavMeshSurface* Navigation::Surface(Scene& scene) {
    NavMeshSurface* s = CacheFor(scene).surface;
    return (s && s->Mesh()) ? s : nullptr;
}

const NavMesh* Navigation::Mesh(Scene& scene) {
    NavMeshSurface* s = CacheFor(scene).surface;
    return s ? s->Mesh() : nullptr;
}

bool Navigation::FindPath(Scene& scene, const Vec3& from, const Vec3& to, std::vector<Vec3>& out,
                          float agentRadius, GameObject* self) {
    out.clear();
    if (const NavMesh* mesh = Mesh(scene)) {
        Vec3 s, e;
        if (mesh->FindNearestPoly(from, kSnap, &s) < 0 || mesh->FindNearestPoly(to, kSnap, &e) < 0) return false;
        return mesh->FindPath(s, e, out, true);
    }
    out = NavGrid3D::FindPath(scene, from, to, self, 0.75f, agentRadius);
    if (out.empty()) { out.push_back(from); out.push_back(to); }
    return true;
}

std::vector<Vec3> Navigation::FindPath(Scene& scene, const Vec3& from, const Vec3& to, float agentRadius, GameObject* self) {
    std::vector<Vec3> out;
    if (!FindPath(scene, from, to, out, agentRadius, self)) out.clear();
    return out;
}

bool Navigation::SamplePosition(Scene& scene, const Vec3& p, float maxDistance, Vec3& out) {
    const NavMesh* mesh = Mesh(scene);
    return mesh && mesh->SamplePosition(p, maxDistance, out);
}

bool Navigation::Raycast(Scene& scene, const Vec3& from, const Vec3& to, Vec3& hit) {
    const NavMesh* mesh = Mesh(scene);
    if (!mesh) { hit = to; return false; }
    return mesh->Raycast(from, to, hit);
}

bool Navigation::RandomPoint(Scene& scene, const Vec3& center, float radius, std::uint32_t& seed, Vec3& out) {
    const NavMesh* mesh = Mesh(scene);
    return mesh && mesh->RandomPointNear(center, radius, seed, out);
}

float Navigation::SampleHeight(Scene& scene, const Vec3& p, float fallback) {
    const NavMesh* mesh = Mesh(scene);
    return mesh ? mesh->SampleHeight(p, fallback) : fallback;
}

const std::vector<NavMeshAgent*>& Navigation::Agents(Scene& scene) { return CacheFor(scene).agents; }
const std::vector<NavMeshObstacle*>& Navigation::Obstacles(Scene& scene) { return CacheFor(scene).obstacles; }

} // namespace okay
