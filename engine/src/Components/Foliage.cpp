#include "okay/Components/Foliage.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/Terrain.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Core/Random.hpp"
#include "okay/Modeling/MeshAsset.hpp"
#include "okay/Render/SurfaceRaycast.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Scene/Transform.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <unordered_set>

namespace okay {

namespace {

std::string AssetPath(const std::string& p) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const std::string& base = Character::AssetBaseDir();
    if (p.empty() || fs::path(p).is_absolute() || base.empty() || fs::exists(fs::u8path(p), ec)) return p;
    return (fs::u8path(base) / fs::u8path(p)).u8string();
}

Quat RotationOf(const FoliageInstance& in) {
    Quat yaw = Quat::AngleAxis(in.yaw, Vec3{0, 1, 0});
    if (in.leanX == 0.0f && in.leanZ == 0.0f) return yaw;
    const float yy = std::sqrt(std::max(0.0f, 1.0f - in.leanX * in.leanX - in.leanZ * in.leanZ));
    return Quat::FromToRotation(Vec3{0, 1, 0}, Vec3{in.leanX, yy, in.leanZ}.Normalized()) * yaw;
}

} // namespace

// ---- the shape -----------------------------------------------------------------

void Foliage::BuildSource() {
    shapes_.clear();
    shapeTexture_.clear();
    problem_.clear();
    sourceOk_ = true;
    if (!prefabPath.empty()) {
        Scene tmp("foliage-source");
        tmp.physicsEnabled = false;
        GameObject* root = SceneSerializer::InstantiateFromFile(tmp, AssetPath(prefabPath));
        if (!root) { problem_ = "the prefab '" + prefabPath + "' could not be read"; return; }
        Mesh flat;
        const Mat4 rootInv = root->transform->LocalToWorldMatrix().Inverse();
        std::vector<GameObject*> stack{root};
        bool any = false;
        while (!stack.empty()) {
            GameObject* g = stack.back(); stack.pop_back();
            for (Transform* c : g->transform->Children()) if (c && c->gameObject) stack.push_back(c->gameObject);
            auto* mr = g->GetComponent<MeshRenderer>();
            if (!mr || mr->mesh.vertices.empty()) continue;
            Mesh part = mr->DisplayMesh();
            const Mat4 M = rootInv * g->transform->LocalToWorldMatrix();
            for (auto& v : part.vertices) v = M.MultiplyPoint(v);
            if (part.HasNormals()) for (auto& n : part.normals) n = M.MultiplyVector(n).Normalized();
            const int tc = part.TriangleCount();
            if (!part.HasFaceColors()) part.triColors.assign((std::size_t)tc, mr->color);
            else for (auto& c : part.triColors) c = Color(c.r * mr->color.r, c.g * mr->color.g, c.b * mr->color.b, c.a * mr->color.a);
            if (!part.HasUVs()) part.uvs.assign(part.vertices.size(), Vec2{0, 0});
            part.cornerUVs.clear(); part.polyFace.clear(); part.seams.clear();
            if (shapeTexture_.empty()) shapeTexture_ = mr->texture;
            if (!any) { flat = std::move(part); any = true; }
            else {
                if (flat.HasNormals() != part.HasNormals()) { flat.normals.clear(); part.normals.clear(); }
                flat.Combine(part);
            }
        }
        if (!any) { problem_ = "the prefab '" + prefabPath + "' has no meshes"; return; }
        flat.name = "FoliageShape";
        shapes_.push_back(std::move(flat));
        return;
    }
    if (!meshPath.empty()) {
        Mesh m;
        std::string tex;
        if (!modeling::LoadMeshAsset(AssetPath(meshPath), m, &tex) || m.vertices.empty()) {
            problem_ = "the mesh file '" + meshPath + "' could not be read";
            return;
        }
        shapeTexture_ = tex;
        shapes_.push_back(std::move(m));
        return;
    }
    const int n = Mesh::NameHasVariants(mesh) ? std::min(8, std::max(1, variants)) : 1;
    for (int i = 0; i < n; ++i) {
        Mesh m = n > 1 ? Mesh::FromNameSeeded(mesh, i + 1) : Mesh::FromName(mesh);
        if (m.vertices.empty() || (m.name != mesh && mesh != "Cube")) { problem_ = "'" + mesh + "' is not a built-in prop"; shapes_.clear(); return; }
        shapes_.push_back(std::move(m));
    }
}

int Foliage::ShapeCount() const {
    if (!prefabPath.empty() || !meshPath.empty()) return 1;
    return Mesh::NameHasVariants(mesh) ? std::min(8, std::max(1, variants)) : 1;
}

int Foliage::TrianglesPerInstance() const {
    if (!sourceOk_) const_cast<Foliage*>(this)->BuildSource();
    if (shapes_.empty()) return 1;
    int t = 0;
    for (const auto& m : shapes_) t += m.TriangleCount();
    return std::max(1, t / (int)shapes_.size());
}

int Foliage::Capacity() const { return std::max(0, maxTriangles / TrianglesPerInstance()); }

// ---- chunks ----------------------------------------------------------------------

Mat4 Foliage::WorldToLocal() const {
    return transform ? transform->LocalToWorldMatrix().Inverse() : Mat4::Identity();
}

long long Foliage::KeyOf(const Vec3& l) const {
    const float cs = std::max(1.0f, chunkSize);
    return ChunkKey((int)std::floor(l.x / cs), (int)std::floor(l.z / cs));
}

void Foliage::ReIndex() {
    index_.clear();
    for (int i = 0; i < (int)instances.size(); ++i) index_[KeyOf(instances[i].position)].push_back(i);
    indexDirty_ = false;
}

void Foliage::Touch(long long key) {
    if (std::find(dirty_.begin(), dirty_.end(), key) == dirty_.end()) dirty_.push_back(key);
}

void Foliage::DestroyChunks() {
    Scene* sc = gameObject ? gameObject->scene() : nullptr;
    for (auto& kv : chunkObjects_) {
        GameObject* g = GameObject::FromInstanceId(kv.second.objectId);
        if (g && sc) sc->Destroy(g);
    }
    chunkObjects_.clear();
}

void Foliage::OnDestroy() { DestroyChunks(); }

void Foliage::BuildChunk(long long key) {
    Scene* sc = gameObject ? gameObject->scene() : nullptr;
    if (!sc || shapes_.empty()) return;
    auto it = index_.find(key);
    GameObject* g = nullptr;
    auto co = chunkObjects_.find(key);
    if (co != chunkObjects_.end()) g = GameObject::FromInstanceId(co->second.objectId);
    if (it == index_.end() || it->second.empty()) {
        if (g) sc->Destroy(g);
        if (co != chunkObjects_.end()) chunkObjects_.erase(co);
        return;
    }
    Mesh out;
    out.name = "FoliageChunk";
    std::size_t nv = 0, nt = 0;
    for (int i : it->second) {
        const Mesh& s = shapes_[(std::size_t)instances[i].variant % shapes_.size()];
        nv += s.vertices.size(); nt += s.triangles.size();
    }
    out.vertices.reserve(nv); out.triangles.reserve(nt);
    const bool uv = shapes_[0].HasUVs(), nrm = shapes_[0].HasNormals(), fc = shapes_[0].HasFaceColors();
    if (uv) out.uvs.reserve(nv);
    if (nrm) out.normals.reserve(nv);
    for (int i : it->second) {
        const FoliageInstance& in = instances[i];
        const Mesh& s = shapes_[(std::size_t)in.variant % shapes_.size()];
        const Quat q = RotationOf(in);
        const int base = (int)out.vertices.size();
        for (const Vec3& v : s.vertices) out.vertices.push_back(in.position + q * (v * in.scale));
        for (int t : s.triangles) out.triangles.push_back(base + t);
        if (uv) { if (s.HasUVs()) out.uvs.insert(out.uvs.end(), s.uvs.begin(), s.uvs.end()); else out.uvs.insert(out.uvs.end(), s.vertices.size(), Vec2{0, 0}); }
        if (nrm) { if (s.HasNormals()) for (const Vec3& n : s.normals) out.normals.push_back(q * n); else out.normals.insert(out.normals.end(), s.vertices.size(), Vec3{0, 1, 0}); }
        if (fc) { if (s.HasFaceColors()) out.triColors.insert(out.triColors.end(), s.triColors.begin(), s.triColors.end()); else out.triColors.insert(out.triColors.end(), (std::size_t)s.TriangleCount(), Color(1, 1, 1, 1)); }
    }
    if (!g) {
        g = sc->CreateGameObject("Foliage Chunk");
        g->transient = true;
        g->transform->SetParent(transform, /*worldPositionStays=*/false);
        g->AddComponent<MeshRenderer>();
        chunkObjects_[key].objectId = g->InstanceId();
    }
    auto* mr = g->GetComponent<MeshRenderer>();
    mr->mesh = std::move(out);
    mr->color = tint;
    mr->texture = shapeTexture_;
    mr->castShadows = castShadows;
    g->name = "Foliage Chunk " + std::to_string((int)(key >> 32)) + "," + std::to_string((int)(key & 0xffffffff));
}

void Foliage::RebuildAll() {
    DestroyChunks();
    allDirty_ = true; sourceOk_ = false; indexDirty_ = true;   // chunk size, shape or tint may have changed
    EnsureBuilt();
}

void Foliage::EnsureBuilt() {
    if (!gameObject || !gameObject->scene()) return;
    if (!sourceOk_) { BuildSource(); allDirty_ = true; }
    if (indexDirty_) ReIndex();
    if (allDirty_) {
        // Drop chunks that no longer exist, make all that do.
        std::vector<long long> keys;
        for (auto& kv : index_) keys.push_back(kv.first);
        for (auto& kv : chunkObjects_) if (!index_.count(kv.first)) keys.push_back(kv.first);
        for (long long k : keys) BuildChunk(k);
        allDirty_ = false;
        dirty_.clear();
    } else if (!dirty_.empty()) {
        for (long long k : dirty_) BuildChunk(k);
        dirty_.clear();
    }
}

// ---- surfaces ---------------------------------------------------------------------

Foliage::Surface Foliage::TerrainSurface(const Terrain& t) {
    const Terrain* tp = &t;
    return [tp](float wx, float wz, Vec3& pos, Vec3& normal) {
        if (!tp->transform) return false;
        const Mat4 M = tp->transform->LocalToWorldMatrix();
        const Mat4 Mi = M.Inverse();
        // Find the local point whose world XZ is (wx, wz): the height is unknown, so use the terrain's
        // own origin height (exact when the terrain is not tilted).
        const Vec3 base = Mi.MultiplyPoint({wx, tp->transform->Position().y, wz});
        const float half = tp->size * 0.5f;
        if (base.x < -half || base.x > half || base.z < -half || base.z > half) return false;
        const float h = tp->SampleHeight(base.x, base.z);
        pos = M.MultiplyPoint({base.x, h, base.z});
        normal = M.MultiplyVector(tp->NormalAt(base.x, base.z)).Normalized();
        return true;
    };
}

Foliage::Surface Foliage::SceneSurface(Scene& scene, float castFromY) const {
    Scene* sp = &scene;
    const GameObject* self = gameObject;
    return [sp, self, castFromY](float wx, float wz, Vec3& pos, Vec3& normal) {
        SurfaceHit hit;
        auto skip = [self](const GameObject& g) {
            return self && (g.IsSelfOrDescendantOf(self) || g.GetComponent<Foliage>() != nullptr);
        };
        if (!RaycastSurfaces(*sp, {wx, castFromY, wz}, {0, -1, 0}, 100000.0f, hit, skip)) return false;
        pos = hit.position; normal = hit.normal;
        return true;
    };
}

// ---- brushes ------------------------------------------------------------------------

int Foliage::Paint(float wx, float wz, const Surface& ground) {
    if (!transform || !ground) return 0;
    if (!sourceOk_) BuildSource();
    if (shapes_.empty()) return 0;
    if (indexDirty_) ReIndex();
    const Mat4 inv = WorldToLocal();
    Random rng((std::uint64_t)seed * 2654435761ull + 12345u);
    seed += 1;
    const float r = std::max(0.05f, radius);
    const float dens = std::max(0.0001f, density);
    const int want = std::min(400, std::max(1, (int)std::lround(dens * 3.14159265f * r * r)));
    const float gap = std::max(0.0f, spacing) / std::sqrt(dens);
    const float gap2 = gap * gap;
    const float cosMax = std::cos(std::min(89.0f, std::max(0.0f, maxSlope)) * 0.0174532925f);
    const int room = Capacity() - Count();
    int added = 0;
    const float cs = std::max(1.0f, chunkSize);
    for (int attempt = 0; attempt < want * 4 && added < want && added < room; ++attempt) {
        const float ang = rng.Range(0.0f, 6.2831853f), d = r * std::sqrt(rng.Range(0.0f, 1.0f));
        Vec3 p, n;
        if (!ground(wx + std::cos(ang) * d, wz + std::sin(ang) * d, p, n)) continue;
        if (p.y < minHeight || p.y > maxHeight) continue;
        if (n.y < cosMax) continue;                                  // steeper than the limit
        const Vec3 lp = inv.MultiplyPoint(p);
        // Too close to a copy already there?
        bool crowded = false;
        if (gap2 > 0.0f) {
            const int cx0 = (int)std::floor((lp.x - gap) / cs), cx1 = (int)std::floor((lp.x + gap) / cs);
            const int cz0 = (int)std::floor((lp.z - gap) / cs), cz1 = (int)std::floor((lp.z + gap) / cs);
            for (int cx = cx0; cx <= cx1 && !crowded; ++cx)
                for (int cz = cz0; cz <= cz1 && !crowded; ++cz) {
                    auto it = index_.find(ChunkKey(cx, cz));
                    if (it == index_.end()) continue;
                    for (int j : it->second) {
                        const Vec3 dd = instances[j].position - lp;
                        if (dd.x * dd.x + dd.z * dd.z < gap2) { crowded = true; break; }
                    }
                }
        }
        if (crowded) continue;
        FoliageInstance in;
        in.position = lp;
        in.yaw = randomYaw ? rng.Range(0.0f, 360.0f) : 0.0f;
        in.scale = (maxScale > minScale) ? rng.Range(minScale, maxScale) : minScale;
        in.variant = rng.Range(0, (int)shapes_.size());
        if (alignToSurface > 0.0f) {
            const Vec3 ln = inv.MultiplyVector(n).Normalized();
            Vec3 u = Vec3{0, 1, 0} * (1.0f - alignToSurface) + ln * alignToSurface;
            u = u.Normalized();
            in.leanX = u.x; in.leanZ = u.z;
        }
        instances.push_back(in);
        const long long key = KeyOf(lp);
        index_[key].push_back((int)instances.size() - 1);
        Touch(key);
        ++added;
    }
    return added;
}

int Foliage::Erase(float wx, float wz, float eraseRadius, float strength) {
    if (!transform || instances.empty()) return 0;
    const Mat4 inv = WorldToLocal();
    const Vec3 c = inv.MultiplyPoint({wx, transform->Position().y, wz});
    const float r2 = eraseRadius * eraseRadius;
    Random rng((std::uint64_t)seed * 40503ull + 7u);
    seed += 1;
    std::vector<FoliageInstance> keep;
    keep.reserve(instances.size());
    int removed = 0;
    std::unordered_set<long long> touched;
    for (const auto& in : instances) {
        const float dx = in.position.x - c.x, dz = in.position.z - c.z;
        if (dx * dx + dz * dz <= r2 && (strength >= 1.0f || rng.Range(0.0f, 1.0f) < strength)) {
            ++removed; touched.insert(KeyOf(in.position));
        } else keep.push_back(in);
    }
    if (!removed) return 0;
    instances = std::move(keep);
    indexDirty_ = true;
    for (long long k : touched) Touch(k);
    return removed;
}

int Foliage::Rescale(float wx, float wz, float rescaleRadius) {
    if (!transform || instances.empty()) return 0;
    const Mat4 inv = WorldToLocal();
    const Vec3 c = inv.MultiplyPoint({wx, transform->Position().y, wz});
    const float r2 = rescaleRadius * rescaleRadius;
    Random rng((std::uint64_t)seed * 69069ull + 3u);
    seed += 1;
    int n = 0;
    for (auto& in : instances) {
        const float dx = in.position.x - c.x, dz = in.position.z - c.z;
        if (dx * dx + dz * dz > r2) continue;
        in.scale = (maxScale > minScale) ? rng.Range(minScale, maxScale) : minScale;
        Touch(KeyOf(in.position));
        ++n;
    }
    return n;
}

int Foliage::ScatterOnTerrain(const Terrain& t, int count, std::uint32_t scatterSeed) {
    if (!sourceOk_) BuildSource();
    if (shapes_.empty() || !t.transform || !transform) return 0;
    if (indexDirty_) ReIndex();
    Random rng(scatterSeed * 2246822519ull + 1u);
    const Surface ground = TerrainSurface(t);
    const Mat4 inv = WorldToLocal();
    const float half = t.size * 0.5f;
    const Mat4 TM = t.transform->LocalToWorldMatrix();
    const float cosMax = std::cos(std::min(89.0f, std::max(0.0f, maxSlope)) * 0.0174532925f);
    const int room = Capacity() - Count();
    int added = 0;
    for (int i = 0; i < count * 3 && added < count && added < room; ++i) {
        const Vec3 w = TM.MultiplyPoint({rng.Range(-half, half), 0.0f, rng.Range(-half, half)});
        Vec3 p, n;
        if (!ground(w.x, w.z, p, n) || p.y < minHeight || p.y > maxHeight || n.y < cosMax) continue;
        FoliageInstance in;
        in.position = inv.MultiplyPoint(p);
        in.yaw = randomYaw ? rng.Range(0.0f, 360.0f) : 0.0f;
        in.scale = (maxScale > minScale) ? rng.Range(minScale, maxScale) : minScale;
        in.variant = rng.Range(0, (int)shapes_.size());
        instances.push_back(in);
        index_[KeyOf(in.position)].push_back((int)instances.size() - 1);
        Touch(KeyOf(in.position));
        ++added;
    }
    return added;
}

void Foliage::Clear() {
    for (const auto& kv : index_) Touch(kv.first);
    if (indexDirty_) for (const auto& in : instances) Touch(KeyOf(in.position));
    instances.clear();
    index_.clear();
    indexDirty_ = false;
}

} // namespace okay
