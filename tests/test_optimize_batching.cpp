// Optimization tools 27.3: the batch planner and the static mesh merge.
#include "test_framework.hpp"
#include "okay/Optimize/BatchPlanner.hpp"
#include <algorithm>
#include <cmath>
#include <set>

using namespace okay;
using namespace okay::optimize;

namespace {

DrawItem Item(int id, int mesh, int mat, std::uint64_t state, bool isStatic, float x = 0.0f, int verts = 100) {
    DrawItem d;
    d.id = id;
    d.meshId = mesh;
    d.materialId = mat;
    d.stateKey = state;
    d.isStatic = isStatic;
    d.transform = Mat4::Translate(Vec3{x, 0, 0});
    d.bounds = Bounds(Vec3{x, 0, 0}, Vec3{1, 1, 1});
    d.vertexCount = verts;
    d.triangleCount = verts / 2;
    return d;
}

/// Every input index appears exactly once across the batches.
bool Partitions(const BatchPlan& plan, int n) {
    std::vector<int> seen(n, 0);
    for (const Batch& b : plan.batches) for (int i : b.items) { if (i < 0 || i >= n) return false; ++seen[i]; }
    for (int s : seen) if (s != 1) return false;
    return true;
}

float Volume(const Mesh& m) {
    double v = 0;
    for (int t = 0; t < m.TriangleCount(); ++t) {
        const Vec3& p = m.vertices[m.triangles[t * 3]];
        const Vec3& q = m.vertices[m.triangles[t * 3 + 1]];
        const Vec3& r = m.vertices[m.triangles[t * 3 + 2]];
        v += Vec3::Dot(p, Vec3::Cross(q, r)) / 6.0;
    }
    return static_cast<float>(v);
}

}  // namespace

int main() {
    RUN_SUITE("optimize_batching");

    // ---- instancing groups ------------------------------------------------------------------------------
    {
        std::vector<DrawItem> items;
        for (int i = 0; i < 50; ++i) items.push_back(Item(i, 1, 1, 7, false, static_cast<float>(i)));          // 50 x mesh 1
        for (int i = 0; i < 30; ++i) items.push_back(Item(100 + i, 2, 1, 7, false, static_cast<float>(i)));    // 30 x mesh 2
        for (int i = 0; i < 20; ++i) items.push_back(Item(200 + i, 3, 2, 7, false, static_cast<float>(i)));    // 20 x mesh 3, other material
        items.push_back(Item(300, 4, 3, 7, false));                                                              // a one-off
        BatchPlan plan = PlanBatches(items);
        CHECK(Partitions(plan, static_cast<int>(items.size())));
        CHECK(plan.stats.items == 101);
        CHECK(plan.stats.drawCallsBefore == 101);
        CHECK(plan.stats.drawCallsAfter == 4);                                       // three instanced groups + the single
        CHECK(plan.stats.instancedBatches == 3 && plan.stats.singleBatches == 1 && plan.stats.mergedBatches == 0);
        CHECK(plan.stats.instancedItems == 100);
        for (const Batch& b : plan.batches) {
            if (b.kind == BatchKind::Instanced) {
                // Identical mesh + material + state inside every batch.
                for (int i : b.items) CHECK(items[i].meshId == b.meshId && items[i].materialId == b.materialId && items[i].stateKey == b.stateKey);
                CHECK(b.InstanceCount() >= 2);
                CHECK(b.vertexCount == 100);                                          // one copy of the mesh is uploaded
                CHECK(b.triangleCount == b.InstanceCount() * 50);                     // but every instance is drawn
            }
        }
        // Instancing off: every item is its own draw call.
        BatchOptions off;
        off.enableInstancing = false;
        off.mergeStatic = false;
        BatchPlan single = PlanBatches(items, off);
        CHECK(single.stats.drawCallsAfter == 101 && single.stats.singleBatches == 101);
        CHECK(Partitions(single, 101));
        // maxInstancesPerBatch splits a big group.
        std::vector<DrawItem> many;
        for (int i = 0; i < 25; ++i) many.push_back(Item(i, 1, 1, 1, false, static_cast<float>(i)));
        BatchOptions cap;
        cap.maxInstancesPerBatch = 10;
        BatchPlan chunks = PlanBatches(many, cap);
        CHECK(chunks.batches.size() == 3);
        CHECK(chunks.batches[0].InstanceCount() + chunks.batches[1].InstanceCount() + chunks.batches[2].InstanceCount() == 25);
        CHECK(std::max({chunks.batches[0].InstanceCount(), chunks.batches[1].InstanceCount(), chunks.batches[2].InstanceCount()}) == 10);
        // A remainder of one is a plain draw, not a one-instance "instanced" batch.
        std::vector<DrawItem> eleven(many.begin(), many.begin() + 11);
        BatchPlan rem = PlanBatches(eleven, cap);
        CHECK(rem.batches.size() == 2 && rem.stats.singleBatches == 1 && rem.stats.instancedBatches == 1);
        // Different shader state never shares a batch even with the same mesh and material.
        std::vector<DrawItem> split;
        for (int i = 0; i < 6; ++i) split.push_back(Item(i, 1, 1, i % 2 ? 100 : 200, false));
        BatchPlan sp = PlanBatches(split);
        CHECK(sp.batches.size() == 2);
        for (const Batch& b : sp.batches) for (int i : b.items) CHECK(split[i].stateKey == b.stateKey);
    }

    // ---- state change sorting -------------------------------------------------------------------------------
    {
        // Interleaved materials and shaders in the order a scene walk would produce them.
        std::vector<DrawItem> items;
        for (int i = 0; i < 60; ++i) items.push_back(Item(i, i % 5, i % 3, (i % 2) ? 10 : 20, false, static_cast<float>(i), 100));
        BatchOptions o;
        o.enableInstancing = false;
        o.mergeStatic = false;
        BatchPlan plan = PlanBatches(items, o);
        CHECK(Partitions(plan, 60));
        // Before: nearly every draw changes everything. After: shader changes only between the two shader groups.
        CHECK(plan.stats.changesBefore.shader >= 59);
        CHECK(plan.stats.changesAfter.shader == 2);
        CHECK(plan.stats.changesAfter.material <= 2 * 3);                            // 3 materials in each shader group
        CHECK(plan.stats.changesAfter.Total() < plan.stats.changesBefore.Total() / 3);
        CHECK(plan.stats.costAfter < plan.stats.costBefore);
        // The order really is sorted by (state, material, mesh).
        for (std::size_t i = 1; i < plan.batches.size(); ++i) {
            const Batch& a = plan.batches[i - 1];
            const Batch& b = plan.batches[i];
            CHECK(std::make_tuple(a.stateKey, a.materialId, a.meshId) <= std::make_tuple(b.stateKey, b.materialId, b.meshId));
        }
        // The same items in any input order give the same counts.
        std::vector<DrawItem> shuffled = items;
        for (std::size_t i = 0; i < shuffled.size(); ++i) std::swap(shuffled[i], shuffled[(i * 7 + 3) % shuffled.size()]);
        BatchPlan p2 = PlanBatches(shuffled, o);
        CHECK(p2.stats.drawCallsAfter == plan.stats.drawCallsAfter);
        CHECK(p2.stats.changesAfter.Total() == plan.stats.changesAfter.Total());
        // Deterministic.
        BatchPlan p3 = PlanBatches(items, o);
        CHECK(p3.batches.size() == plan.batches.size());
        for (std::size_t i = 0; i < plan.batches.size(); ++i) CHECK(p3.batches[i].items == plan.batches[i].items);
        // Front to back inside a state: nearer batches come first.
        std::vector<DrawItem> row;
        for (int i = 0; i < 5; ++i) row.push_back(Item(i, 1, 1, 1, false, static_cast<float>(10 - i * 2)));
        BatchOptions camera;
        camera.enableInstancing = false;
        camera.cameraPosition = Vec3{0, 0, 0};
        BatchPlan fb = PlanBatches(row, camera);
        for (std::size_t i = 1; i < fb.batches.size(); ++i)
            CHECK((fb.batches[i - 1].bounds.center - camera.cameraPosition).SqrMagnitude() <= (fb.batches[i].bounds.center - camera.cameraPosition).SqrMagnitude());
    }

    // ---- static merge groups under a vertex budget -------------------------------------------------------------
    {
        std::vector<DrawItem> items;
        for (int i = 0; i < 200; ++i) items.push_back(Item(i, 1000 + i, 1, 5, true, static_cast<float>(i) * 3.0f, 500));   // 200 different static meshes
        BatchPlan plan = PlanBatches(items);
        CHECK(Partitions(plan, 200));
        CHECK(plan.stats.mergedBatches == 2);                                        // 100000 vertices / 65535 per group
        CHECK(plan.stats.mergedItems == 200);
        CHECK(plan.stats.drawCallsAfter == 2);
        for (const Batch& b : plan.batches) {
            CHECK(b.kind == BatchKind::StaticMerged && b.meshId == -1);
            CHECK(b.vertexCount <= 65535);
            CHECK(b.vertexCount == b.InstanceCount() * 500);
        }
        // Spatially coherent: the two merged groups do not overlap along the row they were laid out on.
        const Batch& g0 = plan.batches[0];
        const Batch& g1 = plan.batches[1];
        const bool apart = g0.bounds.Max().x < g1.bounds.Min().x || g1.bounds.Max().x < g0.bounds.Min().x;
        CHECK(apart);
        // A smaller budget makes more groups, and none exceeds it.
        BatchOptions small;
        small.staticVertexBudget = 10000;
        BatchPlan sp = PlanBatches(items, small);
        CHECK(sp.stats.mergedBatches == 10);
        for (const Batch& b : sp.batches) CHECK(b.vertexCount <= 10000);
        CHECK(Partitions(sp, 200));
        // A mesh bigger than the budget stays a draw of its own and the rest still merge.
        std::vector<DrawItem> mixed = items;
        mixed.push_back(Item(999, 5000, 1, 5, true, 1.0f, 70000));
        BatchPlan mp = PlanBatches(mixed);
        CHECK(Partitions(mp, 201));
        CHECK(mp.stats.singleBatches == 1 && mp.stats.mergedBatches == 2);
        // Merging off.
        BatchOptions nomerge;
        nomerge.mergeStatic = false;
        BatchPlan nm = PlanBatches(items, nomerge);
        CHECK(nm.stats.mergedBatches == 0 && nm.stats.drawCallsAfter == 200);
        // Static items with different materials or states are merged separately.
        std::vector<DrawItem> two;
        for (int i = 0; i < 10; ++i) two.push_back(Item(i, 10 + i, i % 2, 1, true, static_cast<float>(i)));
        BatchPlan tp = PlanBatches(two);
        CHECK(tp.stats.mergedBatches == 2);
        for (const Batch& b : tp.batches) for (int i : b.items) CHECK(two[i].materialId == b.materialId);
        // Too few items to bother merging stay single.
        std::vector<DrawItem> lone = {Item(0, 1, 1, 1, true), Item(1, 2, 2, 1, true)};
        CHECK(PlanBatches(lone).stats.mergedBatches == 0);
    }

    // ---- static repeats: instance them instead of merging copies ---------------------------------------------------
    {
        std::vector<DrawItem> trees;
        for (int i = 0; i < 20; ++i) trees.push_back(Item(i, 7, 1, 1, true, static_cast<float>(i) * 4.0f, 800));
        BatchPlan inst = PlanBatches(trees);                                         // 20 >= threshold 8
        CHECK(inst.stats.instancedBatches == 1 && inst.stats.mergedBatches == 0);
        BatchOptions high;
        high.staticInstancingThreshold = 100;
        BatchPlan merged = PlanBatches(trees, high);
        CHECK(merged.stats.mergedBatches == 1 && merged.stats.instancedBatches == 0);
        BatchOptions noInst;
        noInst.enableInstancing = false;
        CHECK(PlanBatches(trees, noInst).stats.mergedBatches == 1);
        BatchOptions noMerge;
        noMerge.mergeStatic = false;
        CHECK(PlanBatches(trees, noMerge).stats.instancedBatches == 1);
        // 5 repeats are below the threshold: they merge.
        std::vector<DrawItem> few(trees.begin(), trees.begin() + 5);
        CHECK(PlanBatches(few).stats.mergedBatches == 1);
    }

    // ---- skinned and transparent items --------------------------------------------------------------------------------
    {
        std::vector<DrawItem> items;
        for (int i = 0; i < 6; ++i) { DrawItem d = Item(i, 1, 1, 1, false, static_cast<float>(i)); d.skinned = true; items.push_back(d); }
        for (int i = 0; i < 6; ++i) { DrawItem d = Item(10 + i, 2, 1, 1, true, static_cast<float>(i)); d.skinned = true; items.push_back(d); }
        // Transparent items at different distances, listed in a scrambled order.
        const float xs[5] = {3.0f, 9.0f, 1.0f, 7.0f, 5.0f};
        for (int i = 0; i < 5; ++i) { DrawItem d = Item(20 + i, 3, 1, 1, true, xs[i]); d.transparent = true; items.push_back(d); }
        for (int i = 0; i < 4; ++i) items.push_back(Item(30 + i, 4, 2, 1, false, static_cast<float>(i)));
        BatchPlan plan = PlanBatches(items);
        CHECK(Partitions(plan, static_cast<int>(items.size())));
        for (const Batch& b : plan.batches)
            if (b.kind != BatchKind::Single) for (int i : b.items) CHECK(!items[i].skinned && !items[i].transparent);
        // The four opaque identical items are instanced; skinned + transparent ones are single draws.
        CHECK(plan.stats.instancedBatches == 1);
        CHECK(plan.stats.singleBatches == 12 + 5);
        // Transparent draws come last, farthest first.
        std::size_t firstTransparent = plan.batches.size();
        for (std::size_t i = 0; i < plan.batches.size(); ++i) if (plan.batches[i].transparent) { firstTransparent = i; break; }
        CHECK(firstTransparent == plan.batches.size() - 5);
        float prev = 1e9f;
        for (std::size_t i = firstTransparent; i < plan.batches.size(); ++i) {
            CHECK(plan.batches[i].transparent);
            const float d = plan.batches[i].bounds.center.x;                          // camera at the origin: distance = x
            CHECK(d <= prev);
            prev = d;
        }
        for (std::size_t i = 0; i < firstTransparent; ++i) CHECK(!plan.batches[i].transparent);
    }

    // ---- empty / tiny inputs -------------------------------------------------------------------------------------------------
    {
        BatchPlan empty = PlanBatches({});
        CHECK(empty.batches.empty() && empty.stats.drawCallsAfter == 0 && empty.stats.changesAfter.Total() == 0);
        BatchPlan one = PlanBatches({Item(0, 1, 1, 1, true)});
        CHECK(one.batches.size() == 1 && one.batches[0].kind == BatchKind::Single);
        CHECK(one.stats.changesBefore.Total() == 3 && one.stats.changesAfter.Total() == 3);   // the first binds count
    }

    // ---- MergeStaticMeshes ---------------------------------------------------------------------------------------------------
    {
        const Mesh cube = Mesh::Cube(1.0f);
        std::vector<MergeInput> in(3);
        in[0].mesh = &cube; in[0].transform = Mat4::Translate(Vec3{5, 0, 0});
        in[1].mesh = &cube; in[1].transform = Mat4::TRS(Vec3{0, 3, 0}, Quat::Identity, Vec3{2, 2, 2});
        in[2].mesh = &cube; in[2].transform = Mat4::TRS(Vec3{-4, 0, 0}, Quat::Identity, Vec3{-1, 1, 1});   // mirrored
        MergeResult r = MergeStaticMeshes(in);
        CHECK(r.mesh.vertices.size() == 24 && r.mesh.TriangleCount() == 36);
        CHECK(r.firstVertex[0] == 0 && r.firstVertex[1] == 8 && r.firstVertex[2] == 16);
        CHECK(r.firstTriangle[0] == 0 && r.firstTriangle[1] == 12 && r.firstTriangle[2] == 24);
        CHECK(r.skipped == 0);
        // Transforms are baked into the positions.
        for (int i = 0; i < 8; ++i) {
            const Vec3 p = cube.vertices[i];
            CHECK_NEAR((r.mesh.vertices[i] - (p + Vec3{5, 0, 0})).Magnitude(), 0.0, 1e-5);
            CHECK_NEAR((r.mesh.vertices[8 + i] - (p * 2.0f + Vec3{0, 3, 0})).Magnitude(), 0.0, 1e-5);
            CHECK_NEAR((r.mesh.vertices[16 + i] - Vec3{-p.x - 4, p.y, p.z}).Magnitude(), 0.0, 1e-5);
        }
        // Winding stays outward everywhere: each part's volume is positive and the scaled cube is 8 x bigger.
        Mesh part0, part1, part2;
        for (int t = 0; t < 12; ++t)
            for (int c = 0; c < 3; ++c) {
                part0.triangles.push_back(r.mesh.triangles[t * 3 + c]);
                part1.triangles.push_back(r.mesh.triangles[(12 + t) * 3 + c]);
                part2.triangles.push_back(r.mesh.triangles[(24 + t) * 3 + c]);
            }
        part0.vertices = part1.vertices = part2.vertices = r.mesh.vertices;
        CHECK_NEAR(Volume(part0), 1.0, 1e-4);
        CHECK_NEAR(Volume(part1), 8.0, 1e-3);
        CHECK_NEAR(Volume(part2), 1.0, 1e-4);                                         // the mirror's flipped winding keeps it positive
        for (int i : r.mesh.triangles) CHECK(i >= 0 && i < 24);
        CHECK(!r.mesh.HasUVs() && !r.mesh.HasNormals() && !r.mesh.HasFaceColors());
        CHECK(r.mesh.name == "Merged");
    }
    {
        // Attributes: UVs, normals and colours survive; inputs without them get neutral values.
        Mesh a = Mesh::Sphere(0.5f, 8, 12);                                          // UVs + normals
        Mesh b = Mesh::Cube(1.0f);                                                   // neither
        for (int t = 0; t < a.TriangleCount(); ++t) a.triColors.push_back(Color{0.25f, 0.5f, 0.75f, 1.0f});
        std::vector<MergeInput> in(2);
        in[0].mesh = &a; in[0].transform = Mat4::TRS(Vec3{0, 0, 0}, Quat::Euler(0, 0, 90), Vec3{1, 1, 1});
        in[1].mesh = &b; in[1].transform = Mat4::Translate(Vec3{3, 0, 0});
        MergeResult r = MergeStaticMeshes(in);
        const Mesh& m = r.mesh;
        CHECK(m.HasUVs() && m.HasNormals() && m.HasFaceColors());
        CHECK(m.uvs.size() == m.vertices.size() && m.normals.size() == m.vertices.size());
        CHECK(static_cast<int>(m.triColors.size()) == m.TriangleCount());
        for (std::size_t i = 0; i < a.vertices.size(); ++i) {
            CHECK(m.uvs[i].x == a.uvs[i].x && m.uvs[i].y == a.uvs[i].y);
            CHECK_NEAR(m.normals[i].Magnitude(), 1.0, 1e-4);
            // A quarter turn about z sends the normal (x, y, z) to (-y, x, z).
            CHECK_NEAR((m.normals[i] - Vec3{-a.normals[i].y, a.normals[i].x, a.normals[i].z}).Magnitude(), 0.0, 1e-3);
        }
        for (std::size_t i = a.vertices.size(); i < m.vertices.size(); ++i) {
            CHECK(m.uvs[i].x == 0.0f && m.uvs[i].y == 0.0f);                          // the cube had none
            CHECK_NEAR(m.normals[i].Magnitude(), 1.0, 1e-4);                          // smooth normals were generated for it
        }
        CHECK(m.triColors[0].r == 0.25f && m.triColors.back().r == 1.0f);             // the cube's triangles are white
        // Non-uniform scale: normals follow the inverse transpose, not the matrix.
        Mesh s = Mesh::Sphere(0.5f, 16, 24);
        MergeInput sc;
        sc.mesh = &s;
        sc.transform = Mat4::Scale(Vec3{2, 1, 1});
        MergeResult sr = MergeStaticMeshes({sc});
        for (std::size_t i = 0; i < s.vertices.size(); ++i) {
            const Vec3 want = Vec3{s.normals[i].x * 0.5f, s.normals[i].y, s.normals[i].z}.Normalized();
            CHECK_NEAR((sr.mesh.normals[i] - want).Magnitude(), 0.0, 1e-3);
        }
        // Corner UVs are resolved into per-vertex UVs.
        Mesh c = Mesh::Cube(1.0f);
        c.uvs.assign(c.vertices.size(), Vec2{0.5f, 0.5f});
        c.CaptureCornerUVs();
        c.uvs.clear();
        CHECK(c.HasCornerUVs());
        MergeInput ci;
        ci.mesh = &c;
        CHECK(MergeStaticMeshes({ci}).mesh.HasUVs());
        // Null / empty inputs are skipped and counted.
        Mesh empty;
        MergeInput e1, e2;
        e2.mesh = &empty;
        MergeResult skip = MergeStaticMeshes({e1, e2, in[1]});
        CHECK(skip.skipped == 2 && skip.mesh.TriangleCount() == 12);
        CHECK(skip.firstVertex[0] == -1 && skip.firstVertex[1] == -1 && skip.firstVertex[2] == 0);
        CHECK(MergeStaticMeshes({}).mesh.vertices.empty());
    }

    // ---- plan -> merged meshes -----------------------------------------------------------------------------------------------
    {
        const Mesh cube = Mesh::Cube(1.0f);
        const Mesh sphere = Mesh::Sphere(0.5f, 8, 12);
        std::vector<DrawItem> items;
        for (int i = 0; i < 12; ++i) {
            const Mesh& m = (i % 2) ? sphere : cube;
            DrawItem d = Item(i, i % 2, 1, 1, true, static_cast<float>(i) * 2.0f, static_cast<int>(m.vertices.size()));
            d.triangleCount = m.TriangleCount();
            items.push_back(d);
        }
        BatchOptions o;
        o.staticInstancingThreshold = 100;                                           // merge rather than instance
        BatchPlan plan = PlanBatches(items, o);
        CHECK(plan.stats.mergedBatches == 1);
        auto lookup = [&](int id) { return id == 0 ? &cube : &sphere; };
        std::vector<MergeResult> merged = BuildMergedMeshes(plan, items, lookup);
        CHECK(merged.size() == 1);
        CHECK(merged[0].batchIndex == 0);
        const Batch& batch = plan.batches[0];
        CHECK(merged[0].mesh.TriangleCount() == batch.triangleCount);                 // what the plan promised is what was built
        CHECK(static_cast<int>(merged[0].mesh.vertices.size()) == batch.vertexCount);
        CHECK(merged[0].mesh.name == "Merged_0");
        // Every vertex sits where its item's transform put it.
        const DrawItem& first = items[batch.items.front()];
        const Mesh& firstMesh = lookup(first.meshId) == &cube ? cube : sphere;
        CHECK_NEAR((merged[0].mesh.vertices[0] - first.transform.MultiplyPoint(firstMesh.vertices[0])).Magnitude(), 0.0, 1e-5);
        // A plan without merged batches builds nothing.
        BatchOptions none;
        none.mergeStatic = false;
        CHECK(BuildMergedMeshes(PlanBatches(items, none), items, lookup).empty());
        // A missing mesh is skipped, not fatal.
        std::vector<MergeResult> missing = BuildMergedMeshes(plan, items, [](int) -> const Mesh* { return nullptr; });
        CHECK(missing.size() == 1 && missing[0].mesh.vertices.empty() && missing[0].skipped == 12);
    }

    TEST_MAIN_RESULT();
}
