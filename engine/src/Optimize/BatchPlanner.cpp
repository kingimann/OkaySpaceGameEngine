// Draw call batching planner and static mesh merge. See BatchPlanner.hpp.
#include "okay/Optimize/BatchPlanner.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

namespace okay::optimize {
namespace {

std::uint32_t Part1By2(std::uint32_t x) {
    x &= 0x000003ff;
    x = (x | (x << 16)) & 0xff0000ff;
    x = (x | (x << 8)) & 0x0300f00f;
    x = (x | (x << 4)) & 0x030c30c3;
    x = (x | (x << 2)) & 0x09249249;
    return x;
}

std::uint32_t Morton(const Vec3& p, const Vec3& lo, const Vec3& inv) {
    auto q = [](float v) { return static_cast<std::uint32_t>(std::min(1023.0f, std::max(0.0f, v * 1023.0f))); };
    return Part1By2(q((p.x - lo.x) * inv.x)) | (Part1By2(q((p.y - lo.y) * inv.y)) << 1) |
           (Part1By2(q((p.z - lo.z) * inv.z)) << 2);
}

Batch MakeBatch(BatchKind kind, const std::vector<int>& idx, const std::vector<DrawItem>& items) {
    Batch b;
    b.kind = kind;
    b.items = idx;
    const DrawItem& f = items[idx.front()];
    b.meshId = kind == BatchKind::StaticMerged ? -1 : f.meshId;
    b.materialId = f.materialId;
    b.stateKey = f.stateKey;
    b.transparent = f.transparent;
    Vec3 lo = f.bounds.Min(), hi = f.bounds.Max();
    for (int i : idx) {
        const DrawItem& it = items[i];
        b.triangleCount += it.triangleCount;
        if (kind == BatchKind::StaticMerged) b.vertexCount += it.vertexCount;
        const Vec3 mn = it.bounds.Min(), mx = it.bounds.Max();
        lo = {std::min(lo.x, mn.x), std::min(lo.y, mn.y), std::min(lo.z, mn.z)};
        hi = {std::max(hi.x, mx.x), std::max(hi.y, mx.y), std::max(hi.z, mx.z)};
    }
    if (kind != BatchKind::StaticMerged) b.vertexCount = f.vertexCount;
    b.bounds.SetMinMax(lo, hi);
    return b;
}

struct DrawState { std::uint64_t state; int material; int mesh; };

void CountChanges(const std::vector<DrawState>& seq, StateChanges& out) {
    out = StateChanges();
    for (std::size_t i = 0; i < seq.size(); ++i) {
        if (i == 0 || seq[i].state != seq[i - 1].state) ++out.shader;
        if (i == 0 || seq[i].material != seq[i - 1].material) ++out.material;
        if (i == 0 || seq[i].mesh != seq[i - 1].mesh) ++out.mesh;
    }
}

float Cost(const StateChanges& c, const BatchOptions& o) {
    return c.shader * o.shaderChangeCost + c.material * o.materialChangeCost + c.mesh * o.meshChangeCost;
}

}  // namespace

BatchPlan PlanBatches(const std::vector<DrawItem>& items, const BatchOptions& opt) {
    BatchPlan plan;
    const int n = static_cast<int>(items.size());
    plan.stats.items = n;
    plan.stats.drawCallsBefore = n;
    const int minInst = std::max(opt.minInstances, 2);
    const int maxInst = std::max(opt.maxInstancesPerBatch, 1);

    std::vector<Batch> opaque, blended;
    std::map<std::tuple<std::uint64_t, int, int>, std::vector<int>> groups;     // (state, material, mesh)
    std::vector<int> transparentItems;
    for (int i = 0; i < n; ++i) {
        const DrawItem& it = items[i];
        plan.stats.triangles += it.triangleCount;
        if (it.transparent) transparentItems.push_back(i);
        else if (it.skinned) opaque.push_back(MakeBatch(BatchKind::Single, {i}, items));
        else groups[std::make_tuple(it.stateKey, it.materialId, it.meshId)].push_back(i);
    }

    auto emitSingles = [&](const std::vector<int>& idx) {
        for (int i : idx) opaque.push_back(MakeBatch(BatchKind::Single, {i}, items));
    };
    auto emitInstanced = [&](const std::vector<int>& idx) {
        for (std::size_t s = 0; s < idx.size(); s += static_cast<std::size_t>(maxInst)) {
            const std::size_t e = std::min(idx.size(), s + static_cast<std::size_t>(maxInst));
            std::vector<int> chunk(idx.begin() + static_cast<std::ptrdiff_t>(s), idx.begin() + static_cast<std::ptrdiff_t>(e));
            if (chunk.size() >= 2) opaque.push_back(MakeBatch(BatchKind::Instanced, chunk, items));
            else emitSingles(chunk);
        }
    };

    std::map<std::pair<std::uint64_t, int>, std::vector<int>> mergeCandidates;   // (state, material) -> static items
    for (auto& kv : groups) {
        std::vector<int> dyn, stat;
        for (int i : kv.second) (items[i].isStatic ? stat : dyn).push_back(i);
        if (opt.enableInstancing && static_cast<int>(dyn.size()) >= minInst) emitInstanced(dyn); else emitSingles(dyn);
        if (stat.empty()) continue;
        const int count = static_cast<int>(stat.size());
        const bool instance = opt.enableInstancing && count >= minInst &&
                              (!opt.mergeStatic || count >= std::max(opt.staticInstancingThreshold, minInst));
        if (instance) emitInstanced(stat);
        else if (opt.mergeStatic) {
            auto& c = mergeCandidates[std::make_pair(std::get<0>(kv.first), std::get<1>(kv.first))];
            c.insert(c.end(), stat.begin(), stat.end());
        } else emitSingles(stat);
    }

    for (auto& kv : mergeCandidates) {
        std::vector<int>& cand = kv.second;
        Vec3 lo = items[cand[0]].bounds.center, hi = lo;
        for (int i : cand) {
            const Vec3& c = items[i].bounds.center;
            lo = {std::min(lo.x, c.x), std::min(lo.y, c.y), std::min(lo.z, c.z)};
            hi = {std::max(hi.x, c.x), std::max(hi.y, c.y), std::max(hi.z, c.z)};
        }
        const Vec3 ext = hi - lo;
        const Vec3 inv{ext.x > 1e-9f ? 1.0f / ext.x : 0.0f, ext.y > 1e-9f ? 1.0f / ext.y : 0.0f, ext.z > 1e-9f ? 1.0f / ext.z : 0.0f};
        std::vector<std::pair<std::uint32_t, int>> order;
        for (int i : cand) order.emplace_back(Morton(items[i].bounds.center, lo, inv), i);
        std::sort(order.begin(), order.end());
        std::vector<int> cur;
        int curVerts = 0;
        const int budget = std::max(opt.staticVertexBudget, 1);
        const int minMerge = std::max(opt.minMergeItems, 2);
        auto flush = [&]() {
            if (static_cast<int>(cur.size()) >= minMerge) opaque.push_back(MakeBatch(BatchKind::StaticMerged, cur, items));
            else emitSingles(cur);
            cur.clear();
            curVerts = 0;
        };
        for (const auto& o : order) {
            const int v = items[o.second].vertexCount;
            if (v > budget) { emitSingles({o.second}); continue; }
            if (!cur.empty() && curVerts + v > budget) flush();
            cur.push_back(o.second);
            curVerts += v;
        }
        flush();
    }

    auto dist2 = [&](const Batch& b) { const Vec3 d = b.bounds.center - opt.cameraPosition; return d.SqrMagnitude(); };
    std::sort(opaque.begin(), opaque.end(), [&](const Batch& a, const Batch& b) {
        if (a.stateKey != b.stateKey) return a.stateKey < b.stateKey;
        if (a.materialId != b.materialId) return a.materialId < b.materialId;
        if (a.meshId != b.meshId) return a.meshId < b.meshId;
        const float da = dist2(a), db = dist2(b);
        if (da != db) return da < db;
        return a.items.front() < b.items.front();
    });
    for (int i : transparentItems) blended.push_back(MakeBatch(BatchKind::Single, {i}, items));
    std::sort(blended.begin(), blended.end(), [&](const Batch& a, const Batch& b) {
        const float da = dist2(a), db = dist2(b);
        if (da != db) return da > db;                       // back to front
        return a.items.front() < b.items.front();
    });
    plan.batches = std::move(opaque);
    for (Batch& b : blended) plan.batches.push_back(std::move(b));

    // Statistics.
    BatchStats& st = plan.stats;
    st.drawCallsAfter = static_cast<int>(plan.batches.size());
    std::vector<DrawState> before, after;
    for (const DrawItem& it : items) before.push_back({it.stateKey, it.materialId, it.meshId});
    int mergedSeq = 0;
    for (const Batch& b : plan.batches) {
        switch (b.kind) {
            case BatchKind::Single: ++st.singleBatches; break;
            case BatchKind::Instanced: ++st.instancedBatches; st.instancedItems += b.InstanceCount(); break;
            case BatchKind::StaticMerged: ++st.mergedBatches; st.mergedItems += b.InstanceCount(); break;
        }
        after.push_back({b.stateKey, b.materialId, b.kind == BatchKind::StaticMerged ? -2 - mergedSeq++ : b.meshId});
    }
    CountChanges(before, st.changesBefore);
    CountChanges(after, st.changesAfter);
    st.costBefore = Cost(st.changesBefore, opt);
    st.costAfter = Cost(st.changesAfter, opt);
    return plan;
}

MergeResult MergeStaticMeshes(const std::vector<MergeInput>& inputs) {
    MergeResult r;
    r.firstVertex.assign(inputs.size(), -1);
    r.firstTriangle.assign(inputs.size(), -1);
    r.mesh.name = "Merged";
    bool anyUV = false, anyNormals = false, anyColors = false;
    for (const MergeInput& in : inputs) {
        if (!in.mesh || in.mesh->vertices.empty() || in.mesh->triangles.size() < 3) continue;
        anyUV = anyUV || in.mesh->HasUVs() || in.mesh->HasCornerUVs();
        anyNormals = anyNormals || in.mesh->HasNormals();
        anyColors = anyColors || in.mesh->HasFaceColors();
    }
    for (std::size_t k = 0; k < inputs.size(); ++k) {
        const MergeInput& in = inputs[k];
        if (!in.mesh || in.mesh->vertices.empty() || in.mesh->triangles.size() < 3) { ++r.skipped; continue; }
        const Mesh* src = in.mesh;
        Mesh holder;
        if (src->HasCornerUVs()) { holder = src->SplitCornerUVs(); src = &holder; }
        if (anyNormals && !src->HasNormals()) {
            if (src != &holder) { holder = *src; src = &holder; }
            holder.ComputeSmoothNormals();
        }
        const Mat4& M = in.transform;
        const Vec3 a{M.m[0], M.m[1], M.m[2]}, b{M.m[4], M.m[5], M.m[6]}, c{M.m[8], M.m[9], M.m[10]};
        const bool mirrored = Vec3::Dot(a, Vec3::Cross(b, c)) < 0.0f;
        const Mat4 NM = M.Inverse().Transpose();
        const int base = static_cast<int>(r.mesh.vertices.size());
        const int nv = static_cast<int>(src->vertices.size());
        r.firstVertex[k] = base;
        r.firstTriangle[k] = r.mesh.TriangleCount();
        for (int i = 0; i < nv; ++i) {
            r.mesh.vertices.push_back(M.MultiplyPoint(src->vertices[i]));
            if (anyUV) r.mesh.uvs.push_back(src->HasUVs() ? src->uvs[i] : Vec2{0, 0});
            if (anyNormals) {
                const Vec3 n = NM.MultiplyVector(src->normals[i]);
                const float m = n.Magnitude();
                r.mesh.normals.push_back(m > 1e-8f ? n * (1.0f / m) : Vec3{0, 1, 0});
            }
        }
        const int nt = src->TriangleCount();
        for (int t = 0; t < nt; ++t) {
            int i0 = src->triangles[t * 3], i1 = src->triangles[t * 3 + 1], i2 = src->triangles[t * 3 + 2];
            if (i0 < 0 || i1 < 0 || i2 < 0 || i0 >= nv || i1 >= nv || i2 >= nv) {
                // Keep the triangle count aligned with per-triangle colours: a degenerate stand-in.
                i0 = i1 = i2 = 0;
            }
            if (mirrored) std::swap(i1, i2);
            r.mesh.triangles.push_back(base + i0);
            r.mesh.triangles.push_back(base + i1);
            r.mesh.triangles.push_back(base + i2);
            if (anyColors) r.mesh.triColors.push_back(src->HasFaceColors() ? src->triColors[t] : Color{1, 1, 1, 1});
        }
    }
    return r;
}

std::vector<MergeResult> BuildMergedMeshes(const BatchPlan& plan, const std::vector<DrawItem>& items,
                                           const MeshLookup& lookup) {
    std::vector<MergeResult> out;
    for (std::size_t b = 0; b < plan.batches.size(); ++b) {
        const Batch& batch = plan.batches[b];
        if (batch.kind != BatchKind::StaticMerged) continue;
        std::vector<MergeInput> in;
        for (int i : batch.items) {
            MergeInput m;
            m.mesh = lookup ? lookup(items[i].meshId) : nullptr;
            m.transform = items[i].transform;
            in.push_back(m);
        }
        MergeResult r = MergeStaticMeshes(in);
        r.mesh.name = "Merged_" + std::to_string(b);
        r.batchIndex = static_cast<int>(b);
        out.push_back(std::move(r));
    }
    return out;
}

} // namespace okay::optimize
