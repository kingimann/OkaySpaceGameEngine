#include "okay/AI/Navigation.hpp"

#include "okay/AI/NavGrid3D.hpp"
#include "okay/AI/NavMesh.hpp"
#include "okay/Components/NavMeshAgent.hpp"
#include "okay/Components/NavMeshLink.hpp"
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
    std::vector<NavMeshLink*> links;
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
        c->links = scene.FindObjectsOfType<NavMeshLink>();
    }
    return *c;
}

const Vec3 kSnap{2.0f, 4.0f, 2.0f};   // how far a start / goal may be from the mesh
const Vec3 kLinkSnap{1.5f, 3.0f, 1.5f};   // how far a link's end points may be from the mesh

} // namespace

NavMeshSurface* Navigation::Surface(Scene& scene) {
    NavMeshSurface* s = CacheFor(scene).surface;
    return (s && s->Mesh()) ? s : nullptr;
}

const NavMesh* Navigation::Mesh(Scene& scene) {
    SceneCache& c = CacheFor(scene);
    NavMeshSurface* s = c.surface;
    if (!s) return nullptr;
    // Obstacles that are carving right now cut their solid out of the baked mesh; every query below
    // (paths, raycasts, random points) then goes around them. Rebuilt only when the set changed.
    static thread_local std::vector<NavCarveVolume> volumes;
    volumes.clear();
    for (const NavMeshObstacle* o : c.obstacles)
        if (o && o->enabled && o->gameObject && o->gameObject->active && o->Carving()) volumes.push_back(o->CarvedVolume());
    return s->ApplyCarving(volumes);
}

const NavMesh* Navigation::BakedMesh(Scene& scene) {
    NavMeshSurface* s = CacheFor(scene).surface;
    return s ? s->Mesh() : nullptr;
}

std::uint32_t Navigation::CarveGeneration(Scene& scene) {
    if (!Mesh(scene)) return 0;
    NavMeshSurface* s = CacheFor(scene).surface;
    return s ? s->CarveGeneration() : 0;
}

bool Navigation::FindPath(Scene& scene, const Vec3& from, const Vec3& to, std::vector<Vec3>& out,
                          float agentRadius, GameObject* self) {
    out.clear();
    if (const NavMesh* mesh = Mesh(scene)) {
        Vec3 s, e;
        if (mesh->FindNearestPoly(from, kSnap, &s) < 0 || mesh->FindNearestPoly(to, kSnap, &e) < 0) return false;
        NavQueryFilter filter;   // default mask, the surface's costs, no links
        if (NavMeshSurface* sf = CacheFor(scene).surface) sf->FillFilter(filter);
        return mesh->FindPath(s, e, out, true, &filter);
    }
    out = NavGrid3D::FindPath(scene, from, to, self, 0.75f, agentRadius);
    if (out.empty()) { out.push_back(from); out.push_back(to); }
    return true;
}

bool Navigation::FindPathForAgent(Scene& scene, const Vec3& from, const Vec3& to, std::vector<Vec3>& out,
                                  std::vector<NavMeshLink*>& legLinks, std::uint32_t areaMask,
                                  float agentRadius, GameObject* self) {
    legLinks.clear();
    const NavMesh* mesh = Mesh(scene);
    if (!mesh) {
        if (!FindPath(scene, from, to, out, agentRadius, self)) return false;
        legLinks.assign(out.size(), nullptr);
        return true;
    }
    out.clear();
    Vec3 s, e;
    if (mesh->FindNearestPoly(from, kSnap, &s) < 0 || mesh->FindNearestPoly(to, kSnap, &e) < 0) return false;
    SceneCache& cache = CacheFor(scene);
    NavQueryFilter filter;
    filter.areaMask = areaMask;
    if (cache.surface) cache.surface->FillFilter(filter);
    // The scene's usable links as directed edges (a two-way link is two edges).
    static thread_local std::vector<NavOffMeshEdge> edges;
    static thread_local std::vector<int> legIds;
    edges.clear();
    for (std::size_t i = 0; i < cache.links.size(); ++i) {
        const NavMeshLink* l = cache.links[i];
        if (!l || !l->Usable() || !filter.Allows(l->area)) continue;
        NavOffMeshEdge e1;
        e1.fromPoly = mesh->FindNearestPoly(l->WorldStart(), kLinkSnap, &e1.from);
        e1.toPoly = mesh->FindNearestPoly(l->WorldEnd(), kLinkSnap, &e1.to);
        if (e1.fromPoly < 0 || e1.toPoly < 0) continue;
        e1.cost = l->cost; e1.area = l->area; e1.id = (int)i;
        edges.push_back(e1);
        if (l->bidirectional) {
            NavOffMeshEdge e2 = e1;
            std::swap(e2.from, e2.to); std::swap(e2.fromPoly, e2.toPoly);
            edges.push_back(e2);
        }
    }
    filter.edges = edges.empty() ? nullptr : &edges;
    if (!mesh->FindPath(s, e, out, true, &filter, &legIds)) { out.clear(); return false; }
    legLinks.assign(out.size(), nullptr);
    for (std::size_t i = 0; i < out.size() && i < legIds.size(); ++i)
        if (legIds[i] >= 0 && legIds[i] < (int)cache.links.size()) legLinks[i] = cache.links[(std::size_t)legIds[i]];
    return true;
}

const std::vector<NavMeshLink*>& Navigation::Links(Scene& scene) { return CacheFor(scene).links; }

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
