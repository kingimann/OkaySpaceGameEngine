#include "okay/Components/SplineMesh.hpp"
#include "okay/Components/Character.hpp"     // AssetBaseDir: project-relative mesh / prefab files
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/Spline.hpp"
#include "okay/Core/Random.hpp"
#include "okay/Modeling/MeshAsset.hpp"
#include "okay/Physics/ColliderFit.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Scene/Transform.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>

namespace okay {

namespace {

// FNV-1a over the settings, to notice changes cheaply.
struct Hasher {
    std::uint64_t h = 1469598103934665603ull;
    void Bytes(const void* p, std::size_t n) {
        const unsigned char* b = static_cast<const unsigned char*>(p);
        for (std::size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    }
    template <class T> void Add(const T& v) { Bytes(&v, sizeof(v)); }
    void Add(const std::string& s) { Bytes(s.data(), s.size()); Add(s.size()); }
    void Add(const Mat4& m) { Bytes(m.m, sizeof(m.m)); }
};

// Spline local space -> `self`'s local space.
Mat4 Relative(const Component* self, const Spline& sp) {
    if (!self->transform || !sp.transform || sp.transform == self->transform) return Mat4::Identity();
    return self->transform->LocalToWorldMatrix().Inverse() * sp.transform->LocalToWorldMatrix();
}

// A mesh file named like a project asset: as written, else under the project folder.
std::string AssetPath(const std::string& p) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const std::string& base = Character::AssetBaseDir();
    if (p.empty() || fs::path(p).is_absolute() || base.empty() || fs::exists(fs::u8path(p), ec)) return p;
    return (fs::u8path(base) / fs::u8path(p)).u8string();
}

bool IsMeshFile(const std::string& n) {
    auto ends = [&](const char* e) {
        const std::size_t l = std::strlen(e);
        if (n.size() < l) return false;
        for (std::size_t i = 0; i < l; ++i)
            if (std::tolower((unsigned char)n[n.size() - l + i]) != e[i]) return false;
        return true;
    };
    return ends(".obj") || ends(".okaymesh");
}

void MarkTransient(GameObject* g) {
    if (!g) return;
    g->transient = true;
    if (g->transform)
        for (Transform* c : g->transform->Children())
            if (c && c->gameObject) MarkTransient(c->gameObject);
}

// The Road profile's top sits this far above the curve.
constexpr float kRoadLift = 0.02f;

bool HasInstancer(const GameObject* g) {
    if (!g) return false;
    if (g->GetComponent<SplineInstancer>()) return true;
    if (g->transform)
        for (Transform* c : g->transform->Children())
            if (c && c->gameObject && HasInstancer(c->gameObject)) return true;
    return false;
}

} // namespace

// ---- SplineMesh ------------------------------------------------------------

void SplineMesh::ProfilePoints(std::vector<Vec2>& pts, std::vector<float>& u, bool& loop) const {
    pts.clear(); u.clear(); loop = false;
    const float hw = std::max(0.001f, width) * 0.5f, h = std::max(0.0f, height);
    switch (profile) {
        case Profile::Road:     // a hair above the curve (no flicker with the ground it was snapped to),
                                // sides reaching down so the edge never shows a gap over uneven ground
            pts = {{-hw, -h}, {-hw, kRoadLift}, {hw, kRoadLift}, {hw, -h}};
            u = {0.0f, 0.0f, 1.0f, 1.0f};
            return;
        case Profile::Strip:
            pts = {{-hw, 0.0f}, {hw, 0.0f}};
            u = {0.0f, 1.0f};
            return;
        case Profile::Tube: {   // clockwise from the right, so the faces point out
            const int n = std::clamp(sides, 3, 64);
            for (int i = 0; i < n; ++i) {
                const float a = -6.2831853f * (float)i / (float)n;
                pts.push_back({hw * std::cos(a), hw * std::sin(a)});
            }
            loop = true;
            break;
        }
        case Profile::Rail:
            pts = {{-hw, 0.0f}, {-hw, std::max(0.001f, h)}, {hw, std::max(0.001f, h)}, {hw, 0.0f}};
            loop = true;
            break;
        case Profile::Custom:
            pts = customProfile;
            loop = closedProfile && pts.size() >= 3;
            break;
    }
    // u along the outline, 0..1 (a closed outline ends back at 1).
    float total = 0.0f;
    const std::size_t n = pts.size(), segs = loop ? n : (n ? n - 1 : 0);
    for (std::size_t i = 0; i < segs; ++i) total += (pts[(i + 1) % n] - pts[i]).Magnitude();
    float run = 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
        u.push_back(total > 1e-6f ? run / total : 0.0f);
        if (i + 1 < n) run += (pts[i + 1] - pts[i]).Magnitude();
    }
}

const Mesh* SplineMesh::Source() const {
    if (m_sourceName != sourceMesh || (!m_sourceOk && m_source.vertices.empty())) {
        m_sourceName = sourceMesh;
        m_source = Mesh{};
        m_sourceOk = false;
        if (IsMeshFile(sourceMesh)) {
            m_sourceOk = modeling::LoadMeshAsset(AssetPath(sourceMesh), m_source);
        } else if (!sourceMesh.empty()) {
            m_source = Mesh::FromName(sourceMesh);
            m_sourceOk = true;
        }
        if (m_sourceOk && m_source.HasCornerUVs()) m_source = m_source.SplitCornerUVs();
    }
    return m_sourceOk && !m_source.vertices.empty() ? &m_source : nullptr;
}

Mat4 SplineMesh::SplineToLocal(const Spline& sp) const { return Relative(this, sp); }

bool SplineMesh::Build(const Spline& sp, Mesh& out) const {
    out = Mesh{};
    m_copies = 0;
    const float L = sp.LocalLength();
    if (sp.Count() < 2 || !(L > 1e-5f)) return false;
    const Mat4 rel = SplineToLocal(sp);
    const Mat4 relDir = rel;   // directions: MultiplyVector ignores the translation

    if (mode == Mode::Extrude) {
        std::vector<Vec2> prof; std::vector<float> pu; bool loop = false;
        ProfilePoints(prof, pu, loop);
        if (prof.size() < 2) return false;
        // Cross-sections every segmentLength, plus one exactly on every point; where
        // the curve turns sharply there (Linear / Free points) that one is mitred, so
        // corners stay crisp and the width stays true on both sides.
        struct Ring { float d; bool knot; };
        const float step = std::max(segmentLength, 0.01f);
        const int intervals = std::clamp((int)std::ceil(L / step), 1, 20000);
        std::vector<Ring> rings((std::size_t)intervals + 1);
        for (int i = 0; i <= intervals; ++i) rings[(std::size_t)i] = {L * (float)i / (float)intervals, false};
        if (sp.closed) rings.front().knot = rings.back().knot = true;
        const int segs = sp.SegmentCount();
        const float realStep = L / (float)intervals;
        for (int k = 1; k < segs; ++k) {
            const float dk = sp.LocalSample((float)k / (float)segs).distance;
            const int nearest = (int)std::lround(dk / realStep);
            if (nearest > 0 && nearest < intervals && std::fabs(rings[(std::size_t)nearest].d - dk) < 0.3f * realStep)
                rings[(std::size_t)nearest] = {dk, true};
            else
                rings.push_back({dk, true});
        }
        std::sort(rings.begin(), rings.end(), [](const Ring& a, const Ring& b) { return a.d < b.d; });
        std::vector<Ring> ds;
        for (const Ring& r : rings) {
            if (!ds.empty() && std::fabs(ds.back().d - r.d) < 1e-5f) { ds.back().knot = ds.back().knot || r.knot; continue; }
            ds.push_back(r);
        }
        const int P = (int)prof.size(), cols = P + (loop ? 1 : 0), S = loop ? P : P - 1;
        const float tile = uvTileLength > 1e-4f ? uvTileLength : 1.0f;
        const float eps = std::min(1e-3f * L, 0.01f);
        out.vertices.reserve(ds.size() * (std::size_t)cols);
        out.uvs.reserve(ds.size() * (std::size_t)cols);
        for (const Ring& ring : ds) {
            // The end of a closed spline samples its start (same place and frame); v runs on.
            const SplineSample f = sp.LocalSampleAtDistance(ring.d);
            Vec3 right = f.right;
            float widen = 1.0f;
            if (ring.knot) {
                const Vec3 rIn = sp.LocalSampleAtDistance(ring.d - eps).right, rOut = sp.LocalSampleAtDistance(ring.d + eps).right;
                const Vec3 mid = rIn + rOut;
                if (Vec3::Dot(rIn, rOut) < 0.9999f && mid.SqrMagnitude() > 1e-8f) {
                    right = mid.Normalized();
                    widen = 1.0f / std::max(Vec3::Dot(right, rOut), 0.25f);
                }
            }
            for (int c = 0; c < cols; ++c) {
                const Vec2& q = prof[(std::size_t)(c % P)];
                const Vec3 lp = f.position + right * (q.x * f.scale.x * widen) + f.up * (q.y * f.scale.y);
                out.vertices.push_back(rel.MultiplyPoint(lp));
                out.uvs.push_back({c < P ? pu[(std::size_t)c] : 1.0f, ring.d / tile});
            }
        }
        const int R = (int)ds.size();
        out.triangles.reserve((std::size_t)(R - 1) * (std::size_t)S * 6);
        for (int r = 0; r + 1 < R; ++r)
            for (int c = 0; c < S; ++c) {
                const int a = r * cols + c, b = r * cols + c + 1, cc = (r + 1) * cols + c + 1, dd = (r + 1) * cols + c;
                out.triangles.insert(out.triangles.end(), {a, dd, cc, a, cc, b});
            }
        if (profile == Profile::Tube) out.ComputeSmoothNormals();   // a round pipe, not facets
        return true;
    }

    // Repeat: copies of the source laid end to end and bent along the curve.
    const Mesh* src = Source();
    if (!src) return false;
    Vec3 lo, hi;
    src->Bounds(lo, hi);
    const int ax = meshAxis == Axis::X ? 0 : 2;
    const float sAlong = std::fabs(ax == 0 ? meshScale.x : meshScale.z);
    const float srcLen = (hi[ax] - lo[ax]) * sAlong;
    if (!(srcLen > 1e-5f)) return false;
    const float gap = std::max(0.0f, spacing);
    int n = 0;
    float copyLen = srcLen;
    if (sp.closed) {
        n = std::max(1, (int)std::lround(L / (srcLen + gap)));
        if (stretchToFit) copyLen = std::max(1e-4f, L / (float)n - gap);
        else n = std::max(1, (int)std::floor(L / (srcLen + gap) + 1e-4f));
    } else if (stretchToFit) {
        n = std::max(1, (int)std::lround((L + gap) / (srcLen + gap)));
        copyLen = std::max(1e-4f, (L - gap * (float)(n - 1)) / (float)n);
    } else {
        n = std::max(0, (int)std::floor((L + gap) / (srcLen + gap) + 1e-4f));
    }
    n = std::min(n, 4000);
    if (n == 0) return false;
    const bool nrm = src->HasNormals(), uv = src->HasUVs(), cols = src->HasFaceColors();
    const std::size_t nv = src->vertices.size();
    out.vertices.reserve(nv * (std::size_t)n);
    for (int c = 0; c < n; ++c) {
        const float start = (float)c * (copyLen + gap);
        for (std::size_t i = 0; i < nv; ++i) {
            const Vec3& v = src->vertices[i];
            const float a = (v[ax] - lo[ax]) / (hi[ax] - lo[ax]) * copyLen;
            const SplineSample s = sp.LocalSampleAtDistance(std::min(start + a, L));
            // Source axes -> curve frame: along -> tangent, y -> up, across -> right
            // (for X along, the source's z points left so the mapping stays a rotation).
            const float across = ax == 2 ? v.x * meshScale.x : -v.z * meshScale.z;
            const Vec3 lp = s.position + s.right * (across * s.scale.x) + s.up * (v.y * meshScale.y * s.scale.y);
            out.vertices.push_back(rel.MultiplyPoint(lp));
            if (uv) out.uvs.push_back(src->uvs[i]);
            if (nrm) {
                const Vec3& sn = src->normals[i];
                const float nAlong = sn[ax], nAcross = ax == 2 ? sn.x : -sn.z;
                const Vec3 ln = s.tangent * nAlong + s.right * nAcross + s.up * sn.y;
                out.normals.push_back(relDir.MultiplyVector(ln).Normalized());
            }
        }
        const int base = c * (int)nv;
        for (int t : src->triangles) out.triangles.push_back(base + t);
        if (cols) out.triColors.insert(out.triColors.end(), src->triColors.begin(), src->triColors.end());
    }
    m_copies = n;
    return true;
}

std::uint64_t SplineMesh::Signature(const Spline* sp) const {
    Hasher h;
    h.Add(sp); h.Add(sp ? sp->Revision() : 0ull);
    if (sp) h.Add(SplineToLocal(*sp));
    h.Add((int)mode); h.Add((int)profile); h.Add(width); h.Add(height); h.Add(sides);
    h.Add(customProfile.size());
    for (const Vec2& p : customProfile) h.Add(p);
    h.Add(closedProfile); h.Add(segmentLength); h.Add(uvTileLength);
    h.Add(sourceMesh); h.Add((int)meshAxis); h.Add(spacing); h.Add(meshScale); h.Add(stretchToFit);
    h.Add(generateCollider);
    return h.h;
}

bool SplineMesh::RebuildIfChanged() {
    const Spline* sp = Spline::Resolve(this, spline);
    if (m_built && Signature(sp) == m_signature) return false;
    Rebuild();
    return true;
}

bool SplineMesh::Rebuild() {
    if (!gameObject) return false;
    const Spline* sp = Spline::Resolve(this, spline);
    Mesh m;
    const bool ok = sp && Build(*sp, m);
    auto* mr = gameObject->GetComponent<MeshRenderer>();
    if (!mr) mr = gameObject->AddComponent<MeshRenderer>(Mesh{});
    m.name.clear();
    mr->mesh = std::move(m);
    if (generateCollider && !gameObject->GetComponent<MeshCollider3D>()) gameObject->AddComponent<MeshCollider3D>()->autoFit = true;
    FitColliders(gameObject, /*onlyAuto*/ true);
    m_signature = Signature(sp);
    m_built = true;
    return ok;
}

// ---- SplineInstancer -----------------------------------------------------------

std::vector<SplineInstancer::Placement> SplineInstancer::Placements(const Spline& sp) const {
    std::vector<Placement> out;
    const float L = sp.LocalLength();
    if (sp.Count() < 2 || !(L > 1e-5f) || !(spacing > 1e-3f)) return out;
    const Mat4 rel = Relative(this, sp);
    Random rng((std::uint64_t)seed * 0x9E3779B97F4A7C15ull + 17u);
    const int cap = std::max(0, maxCount);
    const float lo = std::min(scaleMin, scaleMax), hi = std::max(scaleMin, scaleMax);
    for (int k = 0;; ++k) {
        const float d = std::max(0.0f, startOffset) + (float)k * spacing;
        if (sp.closed ? d >= L - 1e-4f : d > L + 1e-4f) break;
        if ((int)out.size() >= cap) break;
        // Four draws per station whatever the settings, so one setting never reshuffles the rest.
        const float ja = rng.Range(-1.0f, 1.0f), jc = rng.Range(-1.0f, 1.0f);
        const float yaw = rng.Range(-1.0f, 1.0f), sc = rng.Range(0.0f, 1.0f);
        const SplineSample s = sp.LocalSampleAtDistance(std::min(d + ja * jitter, L));
        Vec3 fwd = rel.MultiplyVector(s.tangent).Normalized();
        Vec3 up = rel.MultiplyVector(s.up).Normalized();
        if (align == Align::Upright) {
            const Vec3 flat{fwd.x, 0.0f, fwd.z};
            if (flat.SqrMagnitude() > 1e-6f) { fwd = flat.Normalized(); up = Vec3{0, 1, 0}; }
        }
        const Vec3 right = Vec3::Cross(up, fwd);
        const Vec3 at = rel.MultiplyPoint(s.position);
        const Quat yawQ = Quat::AngleAxis(yaw * rotationJitter, Vec3{0, 1, 0});
        const Quat extra = Quat::Euler(rotationOffset);
        const Quat face = Quat::LookRotation(fwd, up);
        const float scale = lo + (hi - lo) * sc;
        for (int side = 0; side < (bothSides ? 2 : 1); ++side) {
            if ((int)out.size() >= cap) break;
            const float sign = side == 0 ? 1.0f : -1.0f;
            Placement p;
            p.position = at + right * (sign * sideOffset + jc * jitter);
            // The mirrored item turns round, so both face the curve the same way.
            p.rotation = (side == 0 ? face : face * Quat::AngleAxis(180.0f, Vec3{0, 1, 0})) * yawQ * extra;
            p.scale = scale;
            out.push_back(p);
        }
    }
    return out;
}

std::uint64_t SplineInstancer::SourceSignature() const {
    Hasher h;
    h.Add(templateName); h.Add(prefabPath); h.Add(meshName);
    return h.h;
}

std::uint64_t SplineInstancer::Signature(const Spline* sp) const {
    Hasher h;
    h.Add(sp); h.Add(sp ? sp->Revision() : 0ull);
    if (sp) h.Add(Relative(this, *sp));
    h.Add(SourceSignature());
    h.Add(spacing); h.Add(startOffset); h.Add(sideOffset); h.Add(bothSides); h.Add(jitter); h.Add(rotationJitter);
    h.Add(scaleMin); h.Add(scaleMax); h.Add(seed); h.Add((int)align); h.Add(rotationOffset); h.Add(maxCount);
    return h.h;
}

GameObject* SplineInstancer::MakeItem(int index) {
    Scene* s = gameObject ? gameObject->scene() : nullptr;
    if (!s) return nullptr;
    // A prefab holding an instancer of its own could nest forever: stop a few levels down.
    static int s_depth = 0;
    if (s_depth > 2) return nullptr;
    ++s_depth;
    GameObject* g = nullptr;
    if (!templateName.empty()) {
        GameObject* t = s->Find(templateName);
        const bool related = t && (t->IsSelfOrDescendantOf(gameObject) || gameObject->IsSelfOrDescendantOf(t));
        if (t && !related && !HasInstancer(t)) {
            g = SceneSerializer::Instantiate(*s, *t);
            if (g) g->active = true;
        }
    } else if (!prefabPath.empty()) {
        g = SceneSerializer::InstantiateFromFile(*s, AssetPath(prefabPath));
    } else if (!meshName.empty()) {
        g = s->CreateGameObject(meshName);
        g->AddComponent<MeshRenderer>(Mesh::FromName(meshName));
    }
    --s_depth;
    if (!g) return nullptr;
    g->name += " " + std::to_string(index + 1);
    MarkTransient(g);   // placed again on load: never saved with the scene
    g->transform->SetParent(transform, /*worldPositionStays=*/false);
    return g;
}

int SplineInstancer::Rebuild() {
    m_built = true;
    const Spline* sp = Spline::Resolve(this, spline);
    m_signature = Signature(sp);
    const std::vector<Placement> ps = sp ? Placements(*sp) : std::vector<Placement>{};
    const std::uint64_t src = SourceSignature();
    if (src != m_sourceSig || ps.size() != m_items.size()) {
        Clear();
        m_sourceSig = src;
        for (std::size_t i = 0; i < ps.size(); ++i) {
            GameObject* g = MakeItem((int)i);
            if (!g) break;
            m_items.push_back(g);
        }
    }
    // Same items, new places: move them instead of making them again (live editing).
    const std::size_t n = std::min(ps.size(), m_items.size());
    for (std::size_t i = 0; i < n; ++i) {
        Transform* t = m_items[i]->transform;
        t->localPosition = ps[i].position;
        t->localRotation = ps[i].rotation;
        t->localScale = Vec3{ps[i].scale, ps[i].scale, ps[i].scale};
    }
    return (int)m_items.size();
}

bool SplineInstancer::RebuildIfChanged() {
    const Spline* sp = Spline::Resolve(this, spline);
    if (m_built && Signature(sp) == m_signature) return false;
    Rebuild();
    return true;
}

void SplineInstancer::Clear() {
    Scene* s = gameObject ? gameObject->scene() : nullptr;
    for (GameObject* g : m_items) {
        if (!g || !s) continue;
        g->active = false;   // gone from view this frame; freed at the end of it
        s->Destroy(g);
    }
    m_items.clear();
}

void SplineInstancer::OnObjectsDestroyed(const std::unordered_set<const GameObject*>& dying) {
    m_items.erase(std::remove_if(m_items.begin(), m_items.end(), [&](GameObject* g) { return dying.count(g) != 0; }),
                  m_items.end());
}

} // namespace okay
