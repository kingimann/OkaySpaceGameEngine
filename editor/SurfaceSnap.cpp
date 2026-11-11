#include "SurfaceSnap.hpp"
#include "EditorVisibility.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Render/Lightmap.hpp"   // RayTriangle
#include <algorithm>
#include <cmath>
#include <ostream>

namespace okay::editor::snap {

SnapSettings& Cfg() { static SnapSettings s; return s; }

// ---- Step maths ---------------------------------------------------------------------------------------------
float RoundStep(float v, float step) {
    if (!(step > 0.0f) || !std::isfinite(v)) return v;
    const float r = std::round(v / step) * step;
    return r == 0.0f ? 0.0f : r;   // never -0
}
float WrapDegrees(float deg) {
    if (!std::isfinite(deg)) return deg;
    float d = std::fmod(deg, 360.0f);
    if (d > 180.0f) d -= 360.0f;
    else if (d <= -180.0f) d += 360.0f;
    return d == 0.0f ? 0.0f : d;
}
float SnapDegrees(float deg, float step) { return WrapDegrees(RoundStep(deg, step)); }
float SnapScale(float v, float step, float minValue) { return std::max(minValue, RoundStep(v, step)); }
bool OnStep(float v, float step) {
    if (!(step > 0.0f)) return true;
    const float k = v / step;
    return std::fabs(k - std::round(k)) < 1e-3f;
}
int PresetIndex(const float* presets, int count, float v) {
    for (int i = 0; i < count; ++i) if (std::fabs(presets[i] - v) < 1e-4f) return i;
    return -1;
}

ActiveSnap Resolve(const SnapSettings& s, Mods m) {
    ActiveSnap a;
    if (m.alt) return a;                                   // Alt: nothing this drag
    a.steps = s.enabled || m.ctrl;
    a.surface = s.surface || (m.ctrl && m.shift);
    return a;
}

// ---- Persistence --------------------------------------------------------------------------------------------
static int Scaled(float v, float k) { return (int)std::lround(v * k); }
void AppendSettings(std::ostream& o, const SnapSettings& s) {
    o << "snapon " << (s.enabled ? 1 : 0) << "\n"
      << "snapmovex1000 " << Scaled(s.moveStep, 1000.0f) << "\n"
      << "snaprotx100 " << Scaled(s.rotStep, 100.0f) << "\n"
      << "snapscalex1000 " << Scaled(s.scaleStep, 1000.0f) << "\n"
      << "snapsurface " << (s.surface ? 1 : 0) << "\n"
      << "snapalign " << (s.alignNormal ? 1 : 0) << "\n"
      << "snapoffsetx1000 " << Scaled(s.surfaceOffset, 1000.0f) << "\n"
      << "snaprest " << (s.restOnSurface ? 1 : 0) << "\n"
      << "snapplace " << (s.placeOnSurface ? 1 : 0) << "\n";
}
bool ApplySetting(const std::string& k, int v, SnapSettings& s) {
    auto clampf = [](float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); };
    if (k == "snapon") s.enabled = v != 0;
    else if (k == "snapmovex1000") s.moveStep = clampf(v / 1000.0f, kMinStep, kMaxMoveStep);
    else if (k == "snaprotx100") s.rotStep = clampf(v / 100.0f, kMinRotStep, kMaxRotStep);
    else if (k == "snapscalex1000") s.scaleStep = clampf(v / 1000.0f, kMinStep, kMaxScaleStep);
    else if (k == "snapsurface") s.surface = v != 0;
    else if (k == "snapalign") s.alignNormal = v != 0;
    else if (k == "snapoffsetx1000") s.surfaceOffset = clampf(v / 1000.0f, -100.0f, 100.0f);
    else if (k == "snaprest") s.restOnSurface = v != 0;
    else if (k == "snapplace") s.placeOnSurface = v != 0;
    else return false;
    return true;
}

// ---- Surface snap -------------------------------------------------------------------------------------------
const char* HitSourceName(HitSource s) {
    switch (s) {
        case HitSource::Collider:  return "collider";
        case HitSource::Triangles: return "mesh triangles";
        case HitSource::Bounds:    return "render bounds";
        case HitSource::Terrain:   return "terrain";
        default: return "none";
    }
}

namespace {

bool Ignored(const GameObject* go, const SurfaceQuery& q) {
    for (const GameObject* r : q.ignore) if (r && go->IsSelfOrDescendantOf(r)) return true;
    return false;
}
bool Candidate(const GameObject* go, const SurfaceQuery& q) {
    return go && go->transform && go->ActiveInHierarchy() && !Ignored(go, q) && vis::SceneViewVisible(go, q.isolate);
}
// Does this object have a solid collider of its own that is exact (not just a bounds-fitted MeshCollider)?
bool HasSolidPrimitive(const GameObject* go) {
    for (Collider3D* c : go->GetComponents<Collider3D>())
        if (c && !c->isTrigger && c->shape() != Collider3D::Shape::Mesh) return true;
    return false;
}

void Consider(SurfaceHit& best, float t, const Vec3& o, const Vec3& d, Vec3 n, GameObject* go, HitSource src) {
    if (t < 0.0f || (best.hit && t >= best.distance)) return;
    const float len = n.Magnitude();
    if (len < 1e-9f) return;
    n = n * (1.0f / len);
    if (Vec3::Dot(n, d) > 0.0f) n = n * -1.0f;   // face the viewer
    best.hit = true; best.distance = t; best.point = o + d * t; best.normal = n; best.object = go; best.source = src;
}

// Test one mesh object in its local space. The ray is carried into local space unnormalised, so t is the same
// distance along the world ray.
void TestMesh(SurfaceHit& best, GameObject* go, const Mesh& mesh, const Vec3& o, const Vec3& d, const SurfaceQuery& q) {
    if (mesh.vertices.empty()) return;
    const Mat4 l2w = go->transform->LocalToWorldMatrix();
    const Mat4 w2l = l2w.Inverse();
    const Vec3 lo_ = w2l.MultiplyPoint(o), ld = w2l.MultiplyVector(d);
    Vec3 mn, mx; mesh.Bounds(mn, mx);
    // Slab reject against the local bounds (a ray that misses the box misses the mesh).
    {
        float t0 = 0.0f, t1 = best.hit ? best.distance : q.maxDistance;
        const float oo[3] = {lo_.x, lo_.y, lo_.z}, dd[3] = {ld.x, ld.y, ld.z}, a[3] = {mn.x, mn.y, mn.z}, b[3] = {mx.x, mx.y, mx.z};
        for (int i = 0; i < 3; ++i) {
            if (std::fabs(dd[i]) < 1e-12f) { if (oo[i] < a[i] - 1e-4f || oo[i] > b[i] + 1e-4f) return; continue; }
            float ta = (a[i] - oo[i]) / dd[i], tb = (b[i] - oo[i]) / dd[i];
            if (ta > tb) std::swap(ta, tb);
            t0 = std::max(t0, ta); t1 = std::min(t1, tb);
            if (t0 > t1) return;
        }
    }
    const float maxT = best.hit ? best.distance : q.maxDistance;
    const int tris = mesh.TriangleCount();
    if (tris > 0 && tris <= q.maxTriangles) {
        float bestT = maxT; int bestTri = -1;
        for (int i = 0; i < tris; ++i) {
            const int a = mesh.triangles[3 * i], b = mesh.triangles[3 * i + 1], c = mesh.triangles[3 * i + 2];
            if (a < 0 || b < 0 || c < 0 || a >= (int)mesh.vertices.size() || b >= (int)mesh.vertices.size() || c >= (int)mesh.vertices.size()) continue;
            float t;
            if (RayTriangle(lo_, ld, mesh.vertices[a], mesh.vertices[b], mesh.vertices[c], bestT, t) && t < bestT) { bestT = t; bestTri = i; }
        }
        if (bestTri >= 0) {
            const Vec3 p0 = l2w.MultiplyPoint(mesh.vertices[mesh.triangles[3 * bestTri]]);
            const Vec3 p1 = l2w.MultiplyPoint(mesh.vertices[mesh.triangles[3 * bestTri + 1]]);
            const Vec3 p2 = l2w.MultiplyPoint(mesh.vertices[mesh.triangles[3 * bestTri + 2]]);
            Consider(best, bestT, o, d, Vec3::Cross(p1 - p0, p2 - p0), go, HitSource::Triangles);
        }
        return;
    }
    // No triangles, or too many to test: the render bounds, as 12 triangles of the box.
    Vec3 c[8];
    for (int i = 0; i < 8; ++i) c[i] = l2w.MultiplyPoint({(i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z});
    static const int F[12][3] = {{0,1,3},{0,3,2},{4,6,7},{4,7,5},{0,4,5},{0,5,1},{2,3,7},{2,7,6},{0,2,6},{0,6,4},{1,5,7},{1,7,3}};
    float bestT = maxT; int bi = -1;
    for (int i = 0; i < 12; ++i) {
        float t;
        if (RayTriangle(o, d, c[F[i][0]], c[F[i][1]], c[F[i][2]], bestT, t) && t < bestT) { bestT = t; bi = i; }
    }
    if (bi >= 0) Consider(best, bestT, o, d, Vec3::Cross(c[F[bi][1]] - c[F[bi][0]], c[F[bi][2]] - c[F[bi][0]]), go, HitSource::Bounds);
}

}  // namespace

SurfaceHit RaycastSurface(Scene& scene, const Vec3& origin, const Vec3& direction, const SurfaceQuery& q) {
    SurfaceHit best;
    const float dl = direction.Magnitude();
    if (dl < 1e-9f) return best;
    const Vec3 d = direction * (1.0f / dl);

    // Meshes with no solid primitive collider: triangles (or bounds).
    for (const auto& up : scene.Objects()) {
        GameObject* go = up.get();
        if (!Candidate(go, q)) continue;
        auto* mr = go->GetComponent<MeshRenderer>();
        if (!mr || HasSolidPrimitive(go)) continue;
        TestMesh(best, go, mr->DisplayMesh(), origin, d, q);
    }
    // Primitive colliders and terrain, nearest first; take the first one that may be hit.
    const std::vector<RaycastHit3D> hits = scene.physics3D().RaycastAll(scene, origin, d, q.maxDistance, nullptr, Physics3D::kAllLayers, false);
    for (const RaycastHit3D& h : hits) {
        if (!h.hit || !Candidate(h.gameObject, q)) continue;
        if (!h.collider) {   // terrain
            Consider(best, h.distance, origin, d, h.normal, h.gameObject, HitSource::Terrain);
            break;
        }
        // A MeshCollider on a mesh object is only a box: the triangle pass above already handled that object.
        if (h.collider->shape() == Collider3D::Shape::Mesh && h.gameObject->GetComponent<MeshRenderer>() && !HasSolidPrimitive(h.gameObject)) continue;
        Consider(best, h.distance, origin, d, h.normal, h.gameObject, HitSource::Collider);
        break;
    }
    return best;
}

// ---- Pose ---------------------------------------------------------------------------------------------------
static void CollectCorners(const GameObject* go, std::vector<Vec3>& out, int& budget) {
    if (!go || !go->transform || budget <= 0) return;
    --budget;
    if (auto* mr = go->GetComponent<MeshRenderer>()) {
        const Mesh& m = mr->DisplayMesh();
        if (!m.vertices.empty()) {
            Vec3 mn, mx; m.Bounds(mn, mx);
            const Mat4 l2w = go->transform->LocalToWorldMatrix();
            for (int i = 0; i < 8; ++i) out.push_back(l2w.MultiplyPoint({(i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z}));
        }
    }
    for (const Transform* c : go->transform->Children()) if (c) CollectCorners(c->gameObject, out, budget);
}

float ReachBelow(const GameObject* obj, const Vec3& normal, const Quat& delta) {
    if (!obj || !obj->transform) return 0.0f;
    std::vector<Vec3> corners; int budget = 2000;
    CollectCorners(obj, corners, budget);
    if (corners.empty()) return 0.0f;
    const Vec3 pivot = obj->transform->Position();
    float lowest = 1e30f;
    for (const Vec3& c : corners) lowest = std::min(lowest, Vec3::Dot(delta * (c - pivot), normal));
    return -lowest;   // positive when the object reaches below its pivot
}

SurfacePose ComputeSurfacePose(const GameObject* obj, const SurfaceHit& hit, const SnapSettings& s) {
    SurfacePose p;
    if (!obj || !obj->transform) return p;
    const Quat cur = obj->transform->Rotation();
    Quat delta{0, 0, 0, 1};
    p.rotation = cur;
    if (s.alignNormal && hit.hit) {
        delta = Quat::FromToRotation(cur * Vec3::Up, hit.normal);
        p.rotation = (delta * cur).Normalized();
        p.rotated = true;
    }
    float lift = s.surfaceOffset;
    if (s.restOnSurface && hit.hit) lift += ReachBelow(obj, hit.normal, delta);
    p.position = hit.point + hit.normal * lift;
    return p;
}

void ApplySurfacePose(GameObject* obj, const SurfacePose& p) {
    if (!obj || !obj->transform) return;
    if (p.rotated) obj->transform->SetRotation(p.rotation);
    obj->transform->SetPosition(p.position);
}

}  // namespace okay::editor::snap
