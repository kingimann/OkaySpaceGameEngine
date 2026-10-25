#include "okay/Modeling/Modifiers.hpp"
#include "okay/Modeling/PolyMesh.hpp"
#include "okay/Modeling/PolyOps.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>

namespace okay {

const char* MeshModifier::TypeName(Type t) {
    switch (t) {
        case Type::Mirror:      return "Mirror";
        case Type::Array:       return "Array";
        case Type::Subdivision: return "Subdivision Surface";
        case Type::Solidify:    return "Solidify";
        case Type::Bevel:       return "Bevel";
        case Type::Boolean:     return "Boolean";
        case Type::Decimate:    return "Decimate";
        case Type::Displace:    return "Displace";
        default:                return "?";
    }
}

bool SameModifiers(const std::vector<MeshModifier>& a, const std::vector<MeshModifier>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const MeshModifier& x = a[i];
        const MeshModifier& y = b[i];
        if (x.type != y.type || x.enabled != y.enabled || x.showInEditMode != y.showInEditMode ||
            x.mirrorX != y.mirrorX || x.mirrorY != y.mirrorY || x.mirrorZ != y.mirrorZ || x.mirrorMerge != y.mirrorMerge ||
            x.count != y.count || x.relative != y.relative ||
            x.offset.x != y.offset.x || x.offset.y != y.offset.y || x.offset.z != y.offset.z ||
            x.levels != y.levels || x.smoothShading != y.smoothShading || x.thickness != y.thickness ||
            x.width != y.width || x.segments != y.segments || x.angle != y.angle ||
            x.target != y.target || x.boolOp != y.boolOp || x.voxel != y.voxel || x.cellSize != y.cellSize ||
            x.strength != y.strength || x.frequency != y.frequency || x.seed != y.seed)
            return false;
    }
    return true;
}

MeshModifier MeshModifier::Make(Type t) {
    MeshModifier m;
    m.type = t;
    return m;
}

namespace {

// polyFace must stay one entry per triangle; drop it when an operation broke that,
// so PolyMesh::FromMesh falls back to recovering quads instead of misreading it.
void SyncPolyFace(Mesh& m) {
    if (!m.polyFace.empty() && (int)m.polyFace.size() != m.TriangleCount()) m.polyFace.clear();
}

Mesh ViaPoly(const Mesh& in, const std::function<bool(modeling::PolyMesh&, std::string*)>& op,
             std::string* error, bool& ok) {
    modeling::PolyMesh pm = modeling::PolyMesh::FromMesh(in);
    ok = op(pm, error);
    if (!ok) return in;
    Mesh out = pm.ToMesh();
    out.name = in.name;
    return out;
}

// Copies of `in` side by side; polyFace ids are offset per copy so polygons survive.
Mesh ArrayCopies(const Mesh& in, int count, const Vec3& step) {
    Mesh out = in;
    const bool pf = (int)in.polyFace.size() == in.TriangleCount() && !in.polyFace.empty();
    int maxId = 0;
    if (pf) for (int id : in.polyFace) maxId = std::max(maxId, id);
    const bool uv = in.uvs.size() == in.vertices.size();
    const bool nrm = in.normals.size() == in.vertices.size();
    const bool fc = in.triColors.size() * 3 == in.triangles.size() && !in.triColors.empty();
    for (int c = 1; c < count; ++c) {
        const int base = (int)out.vertices.size();
        for (const Vec3& v : in.vertices) out.vertices.push_back(v + step * (float)c);
        if (uv) out.uvs.insert(out.uvs.end(), in.uvs.begin(), in.uvs.end());
        if (nrm) out.normals.insert(out.normals.end(), in.normals.begin(), in.normals.end());
        for (int t : in.triangles) out.triangles.push_back(t + base);
        if (fc) out.triColors.insert(out.triColors.end(), in.triColors.begin(), in.triColors.end());
        if (pf) for (int id : in.polyFace) out.polyFace.push_back(id + c * (maxId + 1));
    }
    if (!uv) out.uvs.clear();
    if (!nrm) out.normals.clear();
    if (!fc) out.triColors.clear();
    SyncPolyFace(out);
    return out;
}

const MeshRenderer* FindTarget(const ModifierContext& ctx, const std::string& name) {
    if (!ctx.scene || name.empty()) return nullptr;
    for (const auto& go : ctx.scene->Objects()) {
        if (!go || go.get() == ctx.owner || go->name != name) continue;
        if (auto* mr = go->GetComponent<MeshRenderer>()) return mr;
    }
    return nullptr;
}

// Target placement in the owner's local space.
Mat4 TargetToOwner(const ModifierContext& ctx, const MeshRenderer* tmr) {
    Mat4 ownerW = (ctx.owner && ctx.owner->transform) ? ctx.owner->transform->LocalToWorldMatrix() : Mat4::Identity();
    Mat4 targetW = tmr->gameObject && tmr->gameObject->transform ? tmr->gameObject->transform->LocalToWorldMatrix()
                                                                 : Mat4::Identity();
    return ownerW.Inverse() * targetW;
}

// Runs every frame per stack over the whole base mesh, so it mixes 8 bytes a step.
void HashBytes(std::uint64_t& h, const void* data, std::size_t n) {
    const unsigned char* p = (const unsigned char*)data;
    std::size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        std::uint64_t k;
        std::memcpy(&k, p + i, 8);
        k *= 0x9E3779B97F4A7C15ull; k ^= k >> 29;
        h = (h ^ k) * 1099511628211ull;
    }
    for (; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; }
}
void HashMesh(std::uint64_t& h, const Mesh& m) {
    std::size_t nv = m.vertices.size(), nt = m.triangles.size();
    HashBytes(h, &nv, sizeof nv); HashBytes(h, &nt, sizeof nt);
    if (nv) HashBytes(h, m.vertices.data(), nv * sizeof(Vec3));
    if (nt) HashBytes(h, m.triangles.data(), nt * sizeof(int));
    if (!m.uvs.empty()) HashBytes(h, m.uvs.data(), m.uvs.size() * sizeof(m.uvs[0]));
    if (!m.polyFace.empty()) HashBytes(h, m.polyFace.data(), m.polyFace.size() * sizeof(int));
    if (!m.cornerUVs.empty()) HashBytes(h, m.cornerUVs.data(), m.cornerUVs.size() * sizeof(m.cornerUVs[0]));
    if (!m.triColors.empty()) HashBytes(h, m.triColors.data(), m.triColors.size() * sizeof(m.triColors[0]));
    std::size_t nn = m.normals.size(); HashBytes(h, &nn, sizeof nn);
}

} // namespace

bool ApplyModifier(const Mesh& in, const MeshModifier& mod, const ModifierContext& ctx,
                   Mesh& out, std::string* error) {
    using T = MeshModifier::Type;
    out = in;
    if (in.vertices.empty() || in.triangles.empty()) return true;
    switch (mod.type) {
        case T::Mirror: {
            const bool ax[3] = {mod.mirrorX, mod.mirrorY, mod.mirrorZ};
            for (int a = 0; a < 3; ++a)
                if (ax[a]) { out.Mirror(a, mod.mirrorMerge); SyncPolyFace(out); }
            return true;
        }
        case T::Array: {
            int n = std::clamp(mod.count, 1, 1000);
            Vec3 step = mod.offset;
            if (mod.relative) {
                Vec3 lo, hi; in.Bounds(lo, hi);
                Vec3 size = hi - lo;
                step = Vec3{size.x * mod.offset.x, size.y * mod.offset.y, size.z * mod.offset.z};
            }
            out = ArrayCopies(in, n, step);
            return true;
        }
        case T::Subdivision: {
            const int levels = std::clamp(mod.levels, 0, 4);
            if (levels == 0) return true;
            bool ok = true;
            out = ViaPoly(in, [&](modeling::PolyMesh& pm, std::string*) {
                // Each level multiplies the face count by ~4; refuse a runaway result.
                long long faces = (long long)pm.faces.size();
                for (int i = 0; i < levels; ++i) faces *= 4;
                if (faces > 1500000) {
                    if (error) *error = "Too many faces for that many levels (" + std::to_string(faces) + ")";
                    return false;
                }
                pm = modeling::CatmullClark(pm, levels);
                return true;
            }, error, ok);
            if (ok && mod.smoothShading) out.ComputeSmoothNormals();
            return ok;
        }
        case T::Solidify: {
            out.Solidify(mod.thickness);
            SyncPolyFace(out);
            return true;
        }
        case T::Bevel: {
            bool ok = true;
            out = ViaPoly(in, [&](modeling::PolyMesh& pm, std::string* err) {
                // Every closed edge whose faces meet sharper than `angle`.
                modeling::EdgeList edges;
                const float cosLimit = std::cos(std::clamp(mod.angle, 0.0f, 180.0f) * 3.14159265f / 180.0f);
                for (const auto& e : pm.Edges()) {
                    if (e.faceCount != 2 || e.f0 < 0 || e.f1 < 0) continue;
                    Vec3 n0 = pm.FaceNormal(e.f0), n1 = pm.FaceNormal(e.f1);
                    if (Vec3::Dot(n0, n1) <= cosLimit + 1e-4f) edges.push_back({e.a, e.b});
                }
                if (edges.empty()) return true;   // nothing sharp enough: unchanged
                std::string why;
                int n = modeling::BevelEdges(pm, edges, std::max(1e-4f, mod.width), &why,
                                             std::clamp(mod.segments, 1, 8), 0.5f);
                if (n == 0) { if (err) *err = why.empty() ? "These edges can't be beveled" : why; return false; }
                return true;
            }, error, ok);
            return ok;
        }
        case T::Boolean: {
            const MeshRenderer* tmr = FindTarget(ctx, mod.target);
            if (!tmr) {
                if (error) *error = mod.target.empty() ? "Pick an object to cut with" : "No mesh object named \"" + mod.target + "\"";
                return false;
            }
            Mesh cutter = tmr->DisplayMesh();
            if (cutter.vertices.empty()) { if (error) *error = "The target has no mesh"; return false; }
            Mat4 m = TargetToOwner(ctx, tmr);
            for (Vec3& v : cutter.vertices) v = m.MultiplyPoint(v);
            const Mesh::BoolOp op = mod.boolOp == 0 ? Mesh::BoolOp::Union
                                  : mod.boolOp == 2 ? Mesh::BoolOp::Intersect : Mesh::BoolOp::Difference;
            Mesh r = Mesh::Boolean(in, cutter, op, std::max(0.005f, mod.voxel));
            if (r.vertices.empty() && op != Mesh::BoolOp::Intersect) {
                if (error) *error = "The boolean produced nothing";
                return false;
            }
            r.name = in.name;
            out = std::move(r);
            return true;
        }
        case T::Decimate: {
            out.Decimate(std::max(1e-4f, mod.cellSize));
            SyncPolyFace(out);
            return true;
        }
        case T::Displace: {
            out.Displace(mod.strength, mod.frequency, mod.seed);
            SyncPolyFace(out);
            return true;
        }
        default:
            if (error) *error = "Unknown modifier";
            return false;
    }
}

Mesh EvaluateModifiers(const Mesh& base, const std::vector<MeshModifier>& stack, const ModifierContext& ctx,
                       std::vector<std::string>* errors) {
    if (errors) errors->assign(stack.size(), std::string());
    Mesh cur = base;
    for (std::size_t i = 0; i < stack.size(); ++i) {
        const MeshModifier& m = stack[i];
        if (!m.enabled || (ctx.editMode && !m.showInEditMode)) continue;
        Mesh next;
        std::string err;
        if (ApplyModifier(cur, m, ctx, next, &err)) cur = std::move(next);
        else if (errors) (*errors)[i] = err.empty() ? "Failed" : err;
    }
    // Keep the base's shading intent: a smooth-shaded base stays smooth.
    if (base.HasNormals() && !cur.HasNormals()) cur.ComputeSmoothNormals();
    return cur;
}

std::uint64_t ModifierInputHash(const Mesh& base, const std::vector<MeshModifier>& stack, const ModifierContext& ctx) {
    std::uint64_t h = 1469598103934665603ull;
    HashMesh(h, base);
    HashBytes(h, &ctx.editMode, sizeof ctx.editMode);
    for (const MeshModifier& m : stack) {
        int t = (int)m.type;
        HashBytes(h, &t, sizeof t);
        const bool flags[7] = {m.enabled, m.showInEditMode, m.mirrorX, m.mirrorY, m.mirrorZ, m.mirrorMerge, m.relative};
        HashBytes(h, flags, sizeof flags);
        const bool smooth = m.smoothShading; HashBytes(h, &smooth, sizeof smooth);
        const int ints[5] = {m.count, m.levels, m.segments, m.boolOp, m.seed};
        HashBytes(h, ints, sizeof ints);
        const float fl[10] = {m.offset.x, m.offset.y, m.offset.z, m.thickness, m.width, m.angle,
                              m.voxel, m.cellSize, m.strength, m.frequency};
        HashBytes(h, fl, sizeof fl);
        HashBytes(h, m.target.data(), m.target.size());
        if (m.type == MeshModifier::Type::Boolean && m.enabled) {
            if (const MeshRenderer* tmr = FindTarget(ctx, m.target)) {
                HashMesh(h, tmr->DisplayMesh());
                Mat4 rel = TargetToOwner(ctx, tmr);
                HashBytes(h, rel.m, sizeof rel.m);
            }
        }
    }
    return h;
}

} // namespace okay
