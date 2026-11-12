// Level-design rules for SceneValidator (see the code list in Testing/SceneValidator.hpp).
//
// Cost: every rule is one pass over the objects, a hash lookup per object or a sweep along one axis, so a
// 10,000-object scene validates in well under a second (tests/test_scene_validator.cpp checks the budget).
// Output is deterministic: objects are visited in scene order and pair rules sort their pairs by scene index.
//
// What each rule does and does not check:
//
//  geometry.zero_collider  Box size on any axis <= 0, sphere / capsule radius <= 0, capsule height <= 0 (3D), the
//                          2D box, circle and capsule likewise. A collider with autoFit on is skipped (it resizes
//                          itself). A zero Transform scale is the older transform.zero_scale rule.
//  geometry.empty_mesh     A MeshRenderer whose displayed mesh has no triangles. Skipped when a component that
//                          builds the mesh at run time (Terrain, VoxelTerrain, SplineMesh, SkinnedMesh, Water,
//                          BlendShapes, BlockBuilder) sits on the object. A mesh file that is missing is the
//                          asset.missing rule; this one is "the file loaded and is empty" or "no mesh was ever set".
//  object.duplicate_overlap  Same mesh and near-identical transform. "Same mesh" = same meshPath, or no path and the
//                          same vertex / triangle count and bounds to a millimetre; sprites compare their texture.
//                          Transform: position within duplicatePositionEpsilon, rotation within a thousandth,
//                          scale within a thousandth (relative). A spatial hash on (mesh, position cell) is probed
//                          in the 27 neighbouring cells, so the cost is linear.
//  lighting.light_leak     See LightLeaks below.
//  nav.off_mesh / nav.unreachable  See Navigation below.
//  physics.moving_collider_no_rigidbody  See MovingColliders below.
//  scene.multiple_main_cameras / scene.no_light  See SceneSanity below.
#include "okay/Testing/SceneValidator.hpp"

#include "okay/AI/NavMesh.hpp"
#include "okay/Components/BlendShapes.hpp"
#include "okay/Components/BlockBuilder.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/ItemPickup.hpp"
#include "okay/Components/Light.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/Mover.hpp"
#include "okay/Components/NavMeshAgent.hpp"
#include "okay/Components/NavMeshSurface.hpp"
#include "okay/Components/SkinnedMesh.hpp"
#include "okay/Components/SplineMesh.hpp"
#include "okay/Components/SpriteRenderer.hpp"
#include "okay/Components/Terrain.hpp"
#include "okay/Components/VoxelTerrain.hpp"
#include "okay/Components/Water.hpp"
#include "okay/Physics/Collider2D.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Rigidbody2D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Testing/ObjectPath.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <numeric>
#include <unordered_map>

namespace okay::testing::detail {

namespace {

std::string Num(float v) {
    char b[32];
    std::snprintf(b, sizeof(b), "%g", (double)v);
    return b;
}

bool Finite3(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

struct Ctx {
    const Scene& scene;
    const ValidatorOptions& opt;
    ValidationReport& rep;
    std::vector<const GameObject*> objects;   // live, non-transient objects with a transform, scene order

    void Add(Severity s, const char* code, const GameObject* go, const char* component, std::string message,
             const GameObject* other = nullptr) {
        ValidationIssue i;
        i.severity = s;
        i.code = code;
        i.object = go ? ObjectPath(go) : std::string();
        i.guid = go ? go->Guid() : std::string();
        i.component = component;
        i.message = std::move(message);
        if (other) {
            i.related.push_back(ObjectPath(other));
            i.relatedGuids.push_back(other->Guid());
        }
        rep.issues.push_back(std::move(i));
    }
};

// ---- geometry.zero_collider / geometry.empty_mesh -----------------------------------------------------------

void Degenerate(Ctx& c) {
    for (const GameObject* go : c.objects) {
        if (!go->active) continue;
        for (BoxCollider3D* b : go->GetComponents<BoxCollider3D>()) {
            if (b->autoFit) continue;
            if (b->size.x <= 0 || b->size.y <= 0 || b->size.z <= 0)
                c.Add(Severity::Warning, "geometry.zero_collider", go, "BoxCollider3D",
                      "Box collider size (" + Num(b->size.x) + ", " + Num(b->size.y) + ", " + Num(b->size.z) +
                      ") has a zero or negative side: it has no volume and nothing can touch it");
        }
        for (SphereCollider3D* s : go->GetComponents<SphereCollider3D>())
            if (!s->autoFit && s->radius <= 0)
                c.Add(Severity::Warning, "geometry.zero_collider", go, "SphereCollider3D",
                      "Sphere collider radius " + Num(s->radius) + " is zero or negative: nothing can touch it");
        for (CapsuleCollider3D* s : go->GetComponents<CapsuleCollider3D>())
            if (!s->autoFit && (s->radius <= 0 || s->height <= 0))
                c.Add(Severity::Warning, "geometry.zero_collider", go, "CapsuleCollider3D",
                      "Capsule collider radius " + Num(s->radius) + " / height " + Num(s->height) +
                      " is zero or negative: nothing can touch it");
        for (BoxCollider2D* b : go->GetComponents<BoxCollider2D>())
            if (!b->autoFit && (b->size.x <= 0 || b->size.y <= 0))
                c.Add(Severity::Warning, "geometry.zero_collider", go, "BoxCollider2D",
                      "2D box collider size (" + Num(b->size.x) + ", " + Num(b->size.y) +
                      ") has a zero or negative side: it has no area and nothing can touch it");
        for (CircleCollider2D* s : go->GetComponents<CircleCollider2D>())
            if (!s->autoFit && s->radius <= 0)
                c.Add(Severity::Warning, "geometry.zero_collider", go, "CircleCollider2D",
                      "2D circle collider radius " + Num(s->radius) + " is zero or negative: nothing can touch it");
        if (go->GetComponent<Terrain>() || go->GetComponent<VoxelTerrain>() || go->GetComponent<SplineMesh>() ||
            go->GetComponent<SkinnedMesh>() || go->GetComponent<Water>() || go->GetComponent<BlendShapes>() ||
            go->GetComponent<BlockBuilder>())
            continue;   // these fill the mesh in themselves
        for (MeshRenderer* m : go->GetComponents<MeshRenderer>())
            if (m->DisplayMesh().TriangleCount() == 0)
                c.Add(Severity::Warning, "geometry.empty_mesh", go, "MeshRenderer",
                      m->meshPath.empty() ? "MeshRenderer has no triangles, so nothing is drawn"
                                          : "MeshRenderer '" + m->meshPath + "' has no triangles, so nothing is drawn");
    }
}

// ---- object.duplicate_overlap -------------------------------------------------------------------------------

std::uint64_t Mix(std::uint64_t h, std::uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    return h * 0xFF51AFD7ED558CCDull;
}
std::uint64_t HashStr(const std::string& s) {
    std::uint64_t h = 1469598103934665603ull;
    for (unsigned char ch : s) h = (h ^ ch) * 1099511628211ull;
    return h;
}
std::int64_t Q(float v, float step) { return (std::int64_t)std::floor(v / step + 0.5f); }

// 0 = this object has no renderer worth comparing.
std::uint64_t MeshSignature(const GameObject& go) {
    if (MeshRenderer* m = go.GetComponent<MeshRenderer>()) {
        const Mesh& mesh = m->DisplayMesh();
        if (!m->meshPath.empty()) return Mix(1, HashStr(m->meshPath)) | 1;
        if (mesh.TriangleCount() == 0) return 0;
        Vec3 lo, hi;
        mesh.Bounds(lo, hi);
        std::uint64_t h = Mix(2, (std::uint64_t)mesh.vertices.size());
        h = Mix(h, (std::uint64_t)mesh.triangles.size());
        for (float f : {lo.x, lo.y, lo.z, hi.x, hi.y, hi.z}) h = Mix(h, (std::uint64_t)Q(f, 0.001f));
        return h | 1;
    }
    if (SpriteRenderer* s = go.GetComponent<SpriteRenderer>()) return Mix(3, HashStr(s->texture)) | 1;
    return 0;
}

struct DupInfo {
    const GameObject* go = nullptr;
    int order = 0;
    std::uint64_t sig = 0;
    Vec3 pos, scale;
    Quat rot;
};

bool SameTransform(const DupInfo& a, const DupInfo& b, float eps) {
    if (std::fabs(a.pos.x - b.pos.x) > eps || std::fabs(a.pos.y - b.pos.y) > eps || std::fabs(a.pos.z - b.pos.z) > eps)
        return false;
    const double dot = (double)a.rot.x * b.rot.x + (double)a.rot.y * b.rot.y + (double)a.rot.z * b.rot.z + (double)a.rot.w * b.rot.w;
    if (std::fabs(dot) < 0.999999) return false;
    auto close = [](float x, float y) { return std::fabs(x - y) <= 0.001f * std::max(1.0f, std::max(std::fabs(x), std::fabs(y))); };
    return close(a.scale.x, b.scale.x) && close(a.scale.y, b.scale.y) && close(a.scale.z, b.scale.z);
}

void Duplicates(Ctx& c) {
    const float eps = std::max(1e-5f, c.opt.duplicatePositionEpsilon);
    const float cell = eps * 2.0f;
    std::unordered_map<std::uint64_t, std::vector<DupInfo>> grid;
    grid.reserve(c.objects.size() * 2);
    auto key = [](std::uint64_t sig, std::int64_t x, std::int64_t y, std::int64_t z) {
        return Mix(Mix(Mix(sig, (std::uint64_t)x), (std::uint64_t)y), (std::uint64_t)z);
    };
    int order = -1;
    for (const GameObject* go : c.objects) {
        ++order;
        const std::uint64_t sig = MeshSignature(*go);
        if (!sig) continue;
        DupInfo d;
        d.go = go;
        d.order = order;
        d.sig = sig;
        d.pos = go->transform->Position();
        d.scale = go->transform->LossyScale();
        d.rot = go->transform->Rotation();
        if (!Finite3(d.pos) || !Finite3(d.scale) || !std::isfinite(d.rot.w)) continue;   // transform.non_finite covers it
        const std::int64_t cx = Q(d.pos.x, cell), cy = Q(d.pos.y, cell), cz = Q(d.pos.z, cell);
        const DupInfo* first = nullptr;   // the earliest (lowest scene index) match: the one to keep
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
                for (int dz = -1; dz <= 1; ++dz) {
                    auto it = grid.find(key(sig, cx + dx, cy + dy, cz + dz));
                    if (it == grid.end()) continue;
                    for (const DupInfo& o : it->second)
                        if (SameTransform(d, o, eps) && (!first || o.order < first->order)) first = &o;
                }
        if (first) {
            c.Add(Severity::Warning, "object.duplicate_overlap", go, "",
                  "Sits on top of '" + ObjectPath(first->go) + "': same " +
                  (go->GetComponent<MeshRenderer>() ? "mesh" : "sprite") +
                  " at the same place, size and rotation. Probably an accidental duplicate", first->go);
        }
        grid[key(sig, cx, cy, cz)].push_back(d);
    }
}

// ---- lighting.light_leak ------------------------------------------------------------------------------------
//
// What is checked: pairs of STATIC axis-aligned box colliders (isStatic, or a Static Rigidbody3D; not triggers;
// rotation a multiple of 90 degrees so the box equals its bounds) whose facing sides are a sliver apart:
//   * the gap along exactly one axis is more than leakMinGap and at most leakMaxGap (default 0.5 mm to 5 cm);
//   * on the other two axes the boxes overlap by at least leakMinFace (10 cm), so there is a real seam, not a corner;
//   * no third static box fills the gap.
// A gap on a horizontal axis (a wall seam) is a Warning; a gap between a box and the one above it is Info.
// Why it matters: the face-lighting bake (BakeFaceLighting) shadows with rays against colliders, and a ray through
// a sliver that the collider shapes leave open reaches the light, so the bake draws a bright line along the seam.
// This test is purely geometric: it does NOT run the bake, so it can not see a leak through a hole in a mesh,
// rotated or non-box walls, mesh colliders, or boxes that are not marked static.
// False positives: an intended slit or vent, a prop that rests a centimetre above the floor on purpose, two
// walls that a renderer mesh bridges even though the colliders do not. Overlapping boxes are not leaks.

struct Box {
    Vec3 mn, mx;
    const GameObject* go;
    int order;   // scene order
};

bool IsStaticBody(const GameObject& go) {
    if (go.isStatic) return true;
    Rigidbody3D* rb = go.GetComponent<Rigidbody3D>();
    return rb && rb->bodyType == Rigidbody3D::BodyType::Static;
}

float Axis(const Vec3& v, int k) { return k == 0 ? v.x : k == 1 ? v.y : v.z; }

void LightLeaks(Ctx& c) {
    std::vector<Box> boxes;
    for (const GameObject* go : c.objects) {
        if (!IsStaticBody(*go)) continue;
        for (BoxCollider3D* b : go->GetComponents<BoxCollider3D>()) {
            if (b->isTrigger || !b->enabled) continue;
            Box bx;
            b->WorldAABB(bx.mn, bx.mx);
            if (!Finite3(bx.mn) || !Finite3(bx.mx)) continue;
            const Vec3 h = b->HalfExtents();
            const double vol = 8.0 * h.x * h.y * h.z;
            const double aabb = (double)(bx.mx.x - bx.mn.x) * (bx.mx.y - bx.mn.y) * (bx.mx.z - bx.mn.z);
            if (vol <= 1e-9 || std::fabs(aabb - vol) > 1e-3 * std::max(1.0, vol)) continue;   // rotated: the AABB is not the box
            bx.go = go;
            bx.order = (int)boxes.size();
            boxes.push_back(bx);
        }
    }
    if (boxes.size() < 2) return;
    // Sweep along X: sorted by min x, a box only meets the ones that start before its max x + the gap limit.
    std::vector<int> idx(boxes.size());
    std::iota(idx.begin(), idx.end(), 0);
    std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) { return boxes[a].mn.x < boxes[b].mn.x; });
    const float maxGap = c.opt.leakMaxGap, minGap = c.opt.leakMinGap, minFace = c.opt.leakMinFace;
    struct Pair { int a, b, axis; float lo, hi; };
    std::vector<Pair> found;
    int examined = 0;
    for (std::size_t ii = 0; ii < idx.size(); ++ii) {
        const Box& A = boxes[idx[ii]];
        for (std::size_t jj = ii + 1; jj < idx.size(); ++jj) {
            const Box& B = boxes[idx[jj]];
            if (B.mn.x > A.mx.x + maxGap) break;
            if (A.go == B.go) continue;
            int gapAxis = -1;
            float gapSize = 0, lo = 0, hi = 0;
            bool ok = true;
            for (int k = 0; k < 3 && ok; ++k) {
                const float g1 = Axis(B.mn, k) - Axis(A.mx, k), g2 = Axis(A.mn, k) - Axis(B.mx, k);
                const float gap = std::max(g1, g2);
                if (gap > maxGap) { ok = false; break; }
                if (gap > minGap) {
                    if (gapAxis >= 0) { ok = false; break; }   // apart on two axes: a diagonal neighbour
                    gapAxis = k;
                    gapSize = gap;
                    lo = g1 >= g2 ? Axis(A.mx, k) : Axis(B.mx, k);
                    hi = lo + gap;
                } else if (-gap < minFace) {
                    ok = false;   // touching or barely overlapping on this axis: not a facing seam across it
                }
            }
            if (!ok || gapAxis < 0) continue;
            // The two other axes must overlap by at least minFace (checked above as -gap >= minFace).
            if (++examined > 20000) break;
            found.push_back({std::min(A.order, B.order), std::max(A.order, B.order), gapAxis, lo, hi});
            (void)gapSize;
        }
        if (examined > 20000) break;
    }
    std::sort(found.begin(), found.end(), [](const Pair& x, const Pair& y) {
        return x.a != y.a ? x.a < y.a : x.b != y.b ? x.b < y.b : x.axis < y.axis;
    });
    int reported = 0, dropped = 0;
    for (const Pair& p : found) {
        const Box& A = boxes[p.a];
        const Box& B = boxes[p.b];
        // The gap slab: between the faces on the gap axis, the overlap of the two boxes on the others.
        Vec3 smn{std::max(A.mn.x, B.mn.x), std::max(A.mn.y, B.mn.y), std::max(A.mn.z, B.mn.z)};
        Vec3 smx{std::min(A.mx.x, B.mx.x), std::min(A.mx.y, B.mx.y), std::min(A.mx.z, B.mx.z)};
        (p.axis == 0 ? smn.x : p.axis == 1 ? smn.y : smn.z) = p.lo;
        (p.axis == 0 ? smx.x : p.axis == 1 ? smx.y : smx.z) = p.hi;
        bool filled = false;
        for (const Box& O : boxes) {
            if (O.order == A.order || O.order == B.order) continue;
            if (O.mn.x < smx.x - 1e-5f && O.mx.x > smn.x + 1e-5f && O.mn.y < smx.y - 1e-5f && O.mx.y > smn.y + 1e-5f &&
                O.mn.z < smx.z - 1e-5f && O.mx.z > smn.z + 1e-5f) { filled = true; break; }
        }
        if (filled) continue;
        if (reported >= c.opt.leakMaxReports) { ++dropped; continue; }
        ++reported;
        const char* axisName = p.axis == 0 ? "X" : p.axis == 1 ? "Y" : "Z";
        c.Add(p.axis == 1 ? Severity::Info : Severity::Warning, "lighting.light_leak", A.go, "BoxCollider3D",
              "A " + Num((p.hi - p.lo) * 100.0f) + " cm gap along " + axisName + " separates it from '" + ObjectPath(B.go) +
              "': baked light can leak through the seam. Close the gap or overlap the two boxes", B.go);
    }
    if (dropped > 0)
        c.Add(Severity::Info, "lighting.light_leak", nullptr, "",
              std::to_string(dropped) + " more light-leak seams were found and not listed (limit " +
              std::to_string(c.opt.leakMaxReports) + ")");
}

// ---- nav.off_mesh / nav.unreachable -------------------------------------------------------------------------
//
// Uses the scene's baked NavMesh (a NavMeshSurface whose mesh is loaded or baked; when nothing is baked the rules
// stay silent and the older navmesh.no_bake row speaks). Points tested: every NavMeshAgent (its feet: position
// minus baseOffset), and every object tagged Spawn, SpawnPoint, PlayerStart, Respawn, Goal, Pickup, Checkpoint
// or Exit (any case) or carrying an ItemPickup. A point is "on the mesh" when NavMesh::FindNearestPoly finds a
// polygon within 1 unit horizontally and 2 vertically (the same snap FindPath uses). Regions are the connected
// components of the polygon graph (union-find over the portal links). A target on a region without any agent
// is unreachable. With no agents there is nothing to be unreachable from, so only nav.off_mesh is reported.
// Limits: dynamic obstacles, jump links and doors are not modelled; off-mesh means "no polygon nearby", which a
// pickup floating more than 2 units above the floor will trigger.

bool IsNavTarget(const GameObject& go) {
    if (go.GetComponent<ItemPickup>()) return true;
    std::string t = go.tag;
    if (t.empty()) return false;
    for (char& ch : t) ch = (char)std::tolower((unsigned char)ch);
    return t == "spawn" || t == "spawnpoint" || t == "playerstart" || t == "respawn" || t == "goal" || t == "pickup" ||
           t == "checkpoint" || t == "exit";
}

int Find(std::vector<int>& p, int x) {
    while (p[x] != x) { p[x] = p[p[x]]; x = p[x]; }
    return x;
}

void Navigation(Ctx& c) {
    const NavMesh* mesh = nullptr;
    for (NavMeshSurface* s : c.scene.FindObjectsOfType<NavMeshSurface>()) {
        if (!s) continue;
        const NavMesh* m = s->Mesh();
        if (m && !m->Empty()) { mesh = m; break; }
    }
    if (!mesh) return;
    std::vector<int> parent(mesh->polys.size());
    std::iota(parent.begin(), parent.end(), 0);
    for (const NavMesh::Link& l : mesh->links) {
        if (l.from < 0 || l.to < 0 || l.from >= (int)parent.size() || l.to >= (int)parent.size()) continue;
        parent[Find(parent, l.from)] = Find(parent, l.to);
    }
    struct Pt { const GameObject* go; bool agent; int region; };
    std::vector<Pt> pts;
    const Vec3 ext{1.0f, 2.0f, 1.0f};
    for (const GameObject* go : c.objects) {
        NavMeshAgent* ag = go->GetComponent<NavMeshAgent>();
        const bool target = IsNavTarget(*go);
        if (!ag && !target) continue;
        Vec3 p = go->transform->Position();
        if (!Finite3(p)) continue;
        if (ag) p.y -= ag->baseOffset;
        const int poly = mesh->FindNearestPoly(p, ext);
        if (poly < 0) {
            c.Add(Severity::Warning, "nav.off_mesh", go, ag ? "NavMeshAgent" : "",
                  std::string(ag ? "This agent starts" : "This spawn point, goal or pickup sits") +
                  " off the baked navigation mesh (nothing walkable within 1 unit sideways and 2 up of (" +
                  Num(p.x) + ", " + Num(p.y) + ", " + Num(p.z) + ")), so it cannot path anywhere");
            continue;
        }
        pts.push_back({go, ag != nullptr, Find(parent, poly)});
    }
    const Pt* firstAgent = nullptr;
    std::unordered_map<int, const Pt*> agentRegion;
    for (const Pt& p : pts)
        if (p.agent && !agentRegion.count(p.region)) {
            agentRegion[p.region] = &p;
            if (!firstAgent) firstAgent = &p;
        }
    if (!firstAgent) return;
    for (const Pt& p : pts) {
        if (p.agent || agentRegion.count(p.region)) continue;
        c.Add(Severity::Warning, "nav.unreachable", p.go, "",
              "This spawn point, goal or pickup is on a part of the navigation mesh that no agent can walk to (a "
              "separate region from every agent). Join the regions or move it", firstAgent->go);
    }
}

// ---- physics.moving_collider_no_rigidbody -------------------------------------------------------------------
// A Mover with a non-zero velocity on an object that has a non-trigger collider, and no Rigidbody on it or any
// ancestor: the physics world never hears about the movement, so dynamic bodies are pushed through it or tunnel.
// Not covered: movement by scripts, tweens, animation and the navigation agent (those would need a run to see).

bool BodyAbove3D(const GameObject& go) {
    for (const Transform* t = go.transform; t; t = t->Parent())
        if (t->gameObject && t->gameObject->GetComponent<Rigidbody3D>()) return true;
    return false;
}
bool BodyAbove2D(const GameObject& go) {
    for (const Transform* t = go.transform; t; t = t->Parent())
        if (t->gameObject && t->gameObject->GetComponent<Rigidbody2D>()) return true;
    return false;
}

void MovingColliders(Ctx& c) {
    for (const GameObject* go : c.objects) {
        Mover* m = go->GetComponent<Mover>();
        if (!m || !m->enabled) continue;
        if (m->velocity.x == 0 && m->velocity.y == 0 && m->velocity.z == 0) continue;
        bool solid3 = false, solid2 = false;
        for (Collider3D* col : go->GetComponents<Collider3D>()) if (!col->isTrigger) solid3 = true;
        for (Collider2D* col : go->GetComponents<Collider2D>()) if (!col->isTrigger) solid2 = true;
        if (solid3 && !BodyAbove3D(*go))
            c.Add(Severity::Warning, "physics.moving_collider_no_rigidbody", go, "Mover",
                  "A Mover moves this object but its collider has no Rigidbody, so physics does not follow it: bodies "
                  "it pushes into are overlapped or tunnelled. Add a Kinematic Rigidbody3D");
        else if (solid2 && !BodyAbove2D(*go))
            c.Add(Severity::Warning, "physics.moving_collider_no_rigidbody", go, "Mover",
                  "A Mover moves this object but its collider has no Rigidbody, so physics does not follow it: bodies "
                  "it pushes into are overlapped or tunnelled. Add a Kinematic Rigidbody2D");
    }
}

// ---- scene.multiple_main_cameras / scene.no_light -----------------------------------------------------------
// Several usable (enabled, active) cameras flagged Main: only one is the main camera, so Camera.main and
// the game view are ambiguous (a render-texture or split-screen camera should have Main off).
// No light: the scene draws lit 3D meshes (any MeshRenderer that is not Unlit) but has no enabled, active Light.

void SceneSanity(Ctx& c) {
    const Camera* firstMain = nullptr;
    const GameObject* firstMainGo = nullptr;
    for (Camera* cam : c.scene.FindObjectsOfType<Camera>()) {
        if (!cam || !cam->enabled || !cam->gameObject || !cam->gameObject->ActiveInHierarchy() || !cam->main) continue;
        if (!firstMain) { firstMain = cam; firstMainGo = cam->gameObject; continue; }
        c.Add(Severity::Warning, "scene.multiple_main_cameras", cam->gameObject, "Camera",
              "More than one camera is flagged Main (first: '" + ObjectPath(firstMainGo) +
              "'), so the game does not know which one is the main camera. Turn Main off on this one", firstMainGo);
    }
    bool lit3D = false;
    for (const GameObject* go : c.objects) {
        if (!go->ActiveInHierarchy()) continue;
        for (MeshRenderer* m : go->GetComponents<MeshRenderer>())
            if (m->enabled && m->shader != MeshRenderer::Shader::Unlit && m->DisplayMesh().TriangleCount() > 0) lit3D = true;
        if (lit3D) break;
    }
    if (!lit3D) return;
    int lights = 0, usable = 0;
    for (Light* l : c.scene.FindObjectsOfType<Light>()) {
        if (!l) continue;
        ++lights;
        if (l->enabled && l->gameObject && l->gameObject->ActiveInHierarchy()) ++usable;
    }
    if (usable == 0)
        c.Add(Severity::Warning, "scene.no_light", nullptr, "Light",
              lights == 0 ? "The scene draws lit 3D meshes but has no Light, so they render at the flat ambient level"
                          : "The scene has " + std::to_string(lights) + " Light" + (lights == 1 ? "" : "s") +
                            " but none is enabled and active, so lit 3D meshes render at the flat ambient level");
}

} // namespace

void RunLevelChecks(const Scene& scene, const ValidatorOptions& options, ValidationReport& report) {
    Ctx c{scene, options, report, {}};
    c.objects.reserve(scene.Objects().size());
    for (const auto& up : scene.Objects()) {
        const GameObject& go = *up;
        if (go.IsDestroyed() || go.transient || !go.transform) continue;
        c.objects.push_back(&go);
    }
    Degenerate(c);
    Duplicates(c);
    LightLeaks(c);
    Navigation(c);
    MovingColliders(c);
    SceneSanity(c);
}

} // namespace okay::testing::detail
