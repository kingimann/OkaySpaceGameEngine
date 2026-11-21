// The navmesh bake reads a Mesh Collider's real triangles (NavMeshBaker::Gather / Bake), not its bounding box: an
// L-shaped floor leaves its missing corner unwalkable, a ramp's floor follows the slope, a wall made of a single quad
// blocks, a closed mesh pillar is walked around, the underside of a closed mesh is a ceiling, an open sheet is two-sided, a mirrored transform keeps its
// floor, and a Mesh Collider without a mesh still bakes as its box. Written for this engine from the general idea of
// rasterizing triangles into a column heightfield (clip each triangle to each column's square); no other project's
// code was used.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace okay;

namespace {
// Quads in the XZ plane at height y, wound like Mesh::Plane (front face up), or down when `down`.
void AddQuad(Mesh& m, float x0, float z0, float x1, float z1, float y, bool down = false) {
    const int b = (int)m.vertices.size();
    m.vertices.push_back({x0, y, z0}); m.vertices.push_back({x1, y, z0});
    m.vertices.push_back({x1, y, z1}); m.vertices.push_back({x0, y, z1});
    if (down) m.triangles.insert(m.triangles.end(), {b, b + 1, b + 2, b, b + 2, b + 3});
    else      m.triangles.insert(m.triangles.end(), {b, b + 2, b + 1, b, b + 3, b + 2});
}
// A single vertical quad in the plane x = x, from z0 to z1 and from y0 to y1 (no thickness).
Mesh WallQuad(float x, float z0, float z1, float y0, float y1) {
    Mesh m;
    m.vertices = {{x, y0, z0}, {x, y0, z1}, {x, y1, z1}, {x, y1, z0}};
    m.triangles = {0, 1, 2, 0, 2, 3};
    return m;
}
GameObject* MeshObject(Scene& s, const char* name, const Mesh& m, Vec3 pos = {0, 0, 0}) {
    GameObject* g = s.CreateGameObject(name);
    g->transform->localPosition = pos;
    g->AddComponent<MeshRenderer>()->mesh = m;
    g->AddComponent<MeshCollider3D>();
    return g;
}
GameObject* BoxFloor(Scene& s, float size = 20.0f) {
    GameObject* g = s.CreateGameObject("Ground");
    g->transform->localPosition = {0, -0.5f, 0};
    g->transform->localScale = {size, 1, size};
    g->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
    g->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
    return g;
}
NavMeshSurface* Surface(Scene& s) {
    auto* ns = s.CreateGameObject("Nav Mesh")->AddComponent<NavMeshSurface>();
    ns->settings.agentRadius = 0.4f; ns->settings.cellSize = 0.25f;
    return ns;
}
bool Crosses(const std::vector<Vec3>& p, float x0, float x1, float z0, float z1) {
    for (std::size_t i = 1; i < p.size(); ++i)
        for (int k = 0; k <= 40; ++k) {
            const float t = k / 40.0f;
            const float x = p[i - 1].x + (p[i].x - p[i - 1].x) * t, z = p[i - 1].z + (p[i].z - p[i - 1].z) * t;
            if (x > x0 && x < x1 && z > z0 && z < z1) return true;
        }
    return false;
}
bool Walkable(const NavMesh* m, Vec3 p, float within = 0.3f) { Vec3 o; return m && m->SamplePosition(p, within, o); }
} // namespace

int main() {
    RUN_SUITE("navmesh_mesh_collider");

    {   // An L-shaped floor: its bounding box is a full 10 x 10 square, but the corner x, z in (-1, 5) is empty.
        Scene s("L"); s.physicsEnabled = false;
        Mesh L;
        AddQuad(L, -5, -5, 5, -1, 0.0f);   // the bottom arm
        AddQuad(L, -5, -1, -1, 5, 0.0f);   // the left arm
        MeshObject(s, "L floor", L);
        NavMeshSurface* ns = Surface(s);
        NavBakeStats st;
        CHECK(ns->Bake(s, &st));
        const NavMesh* m = ns->Mesh();
        std::printf("  L floor: %d polys, %d walkable cells\n", st.polys, st.walkable);
        CHECK(Walkable(m, {3, 0, -3}));
        CHECK(Walkable(m, {-3, 0, 3}));
        CHECK(!Walkable(m, {3, 0, 3}));             // the missing corner (the bounding box would make it floor)
        CHECK(!Walkable(m, {0.5f, 0, 0.5f}));
        std::vector<Vec3> path;
        CHECK(m && m->FindPath({3, 0, -3}, {-3, 0, 3}, path));
        CHECK(!Crosses(path, -0.95f, 5.0f, -0.95f, 5.0f));   // goes round the inside corner
        if (!path.empty()) CHECK_NEAR(path.front().y, 0.0f, 0.05f);
    }

    {   // A ramp: a plane rotated 20 degrees about z rises toward +x; the floor follows it.
        Scene s("ramp"); s.physicsEnabled = false;
        GameObject* r = MeshObject(s, "Ramp", Mesh::Plane(10.0f));
        r->transform->localRotation = Quat::AngleAxis(20.0f, {0, 0, 1});
        NavMeshSurface* ns = Surface(s);
        CHECK(ns->Bake(s));
        const NavMesh* m = ns->Mesh();
        const float want = 2.0f * std::tan(20.0f * 3.14159265f / 180.0f);
        const float at2 = m ? m->SampleHeight({2, 1, 0}, -99.0f) : -99.0f;
        const float atM2 = m ? m->SampleHeight({-2, 1, 0}, -99.0f) : -99.0f;
        std::printf("  ramp: height at x=2 %.3f (want %.3f), at x=-2 %.3f\n", at2, want, atM2);
        CHECK_NEAR(at2, want, 0.15f);              // the bounding box would give a flat top near 1.71
        CHECK_NEAR(atM2, -want, 0.15f);
        std::vector<Vec3> path;
        CHECK(m && m->FindPath({-3, -1, 0}, {3, 1, 0}, path));
    }

    {   // A wall that is a single quad (no thickness) on a box floor, on a column border and off it.
        for (float x : {0.0f, 0.13f, 0.25f}) {
            Scene s("wall quad"); s.physicsEnabled = false;
            BoxFloor(s);
            MeshObject(s, "Wall", WallQuad(x, -3, 3, 0, 2));
            NavMeshSurface* ns = Surface(s);
            CHECK(ns->Bake(s));
            const NavMesh* m = ns->Mesh();
            std::vector<Vec3> path;
            CHECK(m && m->FindPath({-5, 0, 0}, {5, 0, 0}, path));
            const bool through = Crosses(path, x - 0.2f, x + 0.2f, -2.9f, 2.9f);
            std::printf("  wall quad at x=%.2f: path of %d points, through the wall: %s\n", x, (int)path.size(), through ? "yes" : "no");
            CHECK(!through);
        }
    }

    {   // A closed mesh pillar (a scaled cube) on a box floor: walked around; its inside is not floor.
        Scene s("pillar"); s.physicsEnabled = false;
        BoxFloor(s);
        GameObject* p = MeshObject(s, "Pillar", Mesh::Cube(1.0f), {0, 1.0f, 0});
        p->transform->localScale = {1, 2, 6};
        NavMeshSurface* ns = Surface(s);
        CHECK(ns->Bake(s));
        const NavMesh* m = ns->Mesh();
        std::vector<Vec3> path;
        CHECK(m && m->FindPath({-5, 0, 0}, {5, 0, 0}, path));
        CHECK(!Crosses(path, -0.5f - 0.35f, 0.5f + 0.35f, -3.0f - 0.35f, 3.0f + 0.35f));
        CHECK(!Walkable(m, {0, 0, 0}, 0.2f));        // under the pillar
    }

    {   // A flipped, open floor (a single sheet wound downward) is still a floor: physics treats a mesh collider as
        // two-sided, so an agent can stand on it. (Open meshes are two-sided for the bake; see docs/gameplay/navigation.md.)
        Scene s("flipped floor"); s.physicsEnabled = false;
        Mesh down; AddQuad(down, -4, -4, 4, 4, 0.0f, true);
        MeshObject(s, "Down", down);
        NavMeshSurface* ns = Surface(s);
        CHECK(ns->Bake(s));
        CHECK(Walkable(ns->Mesh(), {0, 0, 0}));
    }

    {   // A CLOSED mesh trusts its winding: the underside of a thick slab is a ceiling, never a floor.
        Scene s("slab"); s.physicsEnabled = false;
        GameObject* g = MeshObject(s, "Slab", Mesh::Cube(1.0f), {0, 4.5f, 0});
        g->transform->localScale = {8, 3, 8};     // y from 3 to 6
        NavMeshSurface* ns = Surface(s);
        CHECK(ns->Bake(s));
        CHECK(Walkable(ns->Mesh(), {0, 6, 0}, 0.3f));
        CHECK(!Walkable(ns->Mesh(), {0, 3, 0}, 0.3f));
        // Low over a box floor it takes the head room: the floor under it is not walkable, beside it is.
        Scene s2("low ceiling"); s2.physicsEnabled = false;
        BoxFloor(s2);
        GameObject* lo = MeshObject(s2, "Low", Mesh::Cube(1.0f), {0, 1.1f, 0});
        lo->transform->localScale = {4, 0.2f, 4};
        NavMeshSurface* ns2 = Surface(s2);
        CHECK(ns2->Bake(s2));
        CHECK(!Walkable(ns2->Mesh(), {0, 0, 0}, 0.2f));
        CHECK(Walkable(ns2->Mesh(), {5, 0, 5}));
    }

    {   // A mirrored transform (negative scale) keeps an upward floor upward.
        Scene s("mirror"); s.physicsEnabled = false;
        GameObject* g = MeshObject(s, "Mirrored", Mesh::Plane(8.0f));
        g->transform->localScale = {-1, 1, 1};
        NavMeshSurface* ns = Surface(s);
        CHECK(ns->Bake(s));
        CHECK(Walkable(ns->Mesh(), {0, 0, 0}));
    }

    {   // A Mesh Collider with no mesh to read still bakes as the box its size describes.
        Scene s("no mesh"); s.physicsEnabled = false;
        GameObject* g = s.CreateGameObject("Box only");
        g->transform->localPosition = {0, -0.5f, 0};
        g->AddComponent<MeshCollider3D>()->size = {8, 1, 8};
        NavMeshSurface* ns = Surface(s);
        CHECK(ns->Bake(s));
        CHECK(Walkable(ns->Mesh(), {0, 0, 0}));
        CHECK(!Walkable(ns->Mesh(), {6, 0, 0}, 0.3f));
    }

    {   // The bake input keeps the triangles (and the box list stays empty for a meshed collider).
        Scene s("gather"); s.physicsEnabled = false;
        MeshObject(s, "Plane", Mesh::Plane(4.0f));
        NavBakeInput in;
        NavMeshBaker::Gather(s, in);
        CHECK(in.meshes.size() == 1);
        CHECK(in.boxes.empty());
        if (!in.meshes.empty()) CHECK(in.meshes[0].corners.size() == 6);
    }

    // ---- Raw bake input: budgets, bad numbers, borders ----------------------------------------------------------------
    {   // A thin diagonal sliver crosses ~800 columns of a 640k-column grid, so it must cost about that, not the grid.
        NavBakeInput in; in.hasBounds = true; in.boundsMin = {-100, -1, -100}; in.boundsMax = {100, 3, 100};
        in.maxMeshColumnVisits = 20000;
        NavBakeInput::Triangles t;
        t.corners = {{-99, 0, -99}, {99, 0, 99.2f}, {99, 0, 99}};
        in.meshes.push_back(t);
        NavMeshBuildSettings S; S.cellSize = 0.25f;
        NavMesh m; NavBakeStats st;
        CHECK(NavMeshBaker::Bake(in, S, m, nullptr, nullptr, &st));
        CHECK(!st.meshBudgetExceeded);
        CHECK(!st.incomplete);
    }
    {   // Many such slivers run out of the work budget: the bake still finishes (quickly), says so, and does not crash.
        NavBakeInput in; in.hasBounds = true; in.boundsMin = {-100, -1, -100}; in.boundsMax = {100, 3, 100};
        in.maxMeshColumnVisits = 20000;
        NavBakeInput::Triangles t;
        for (int i = 0; i < 400; ++i) {
            const float o = (float)i * 0.4f - 80.0f;
            t.corners.push_back({-99, 0, -99 + o}); t.corners.push_back({99, 0, 99.2f}); t.corners.push_back({99, 0, 99});
        }
        in.meshes.push_back(t);
        NavMeshBuildSettings S; S.cellSize = 0.25f;
        NavMesh m; NavBakeStats st;
        const auto t0 = std::chrono::steady_clock::now();
        CHECK(NavMeshBaker::Bake(in, S, m, nullptr, nullptr, &st));
        CHECK(std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 20.0);
        CHECK(st.meshBudgetExceeded);
        CHECK(st.incomplete);
        CHECK(!st.warning.empty());
    }
    {   // The triangle cap in Gather: the rest is counted, not copied, and the bake reports it.
        Scene s("cap"); s.physicsEnabled = false;
        MeshObject(s, "Cube", Mesh::Cube(2.0f));
        NavBakeInput in; in.maxMeshTriangles = 5;
        NavMeshBaker::Gather(s, in);
        CHECK(in.meshes.size() == 1);
        if (!in.meshes.empty()) CHECK(in.meshes[0].corners.size() == 15);
        CHECK(in.meshTrianglesDropped == 7);
        CHECK(in.closedTestTriangles == 0);   // the cut-off mesh gets no edge table (counts as open)
        if (!in.meshes.empty()) CHECK(in.meshes[0].twoSided);
        NavBakeInput whole; NavMeshBaker::Gather(s, whole);
        CHECK(whole.closedTestTriangles == 12);
        if (!whole.meshes.empty()) CHECK(!whole.meshes[0].twoSided);   // a whole cube is closed
        NavMeshBuildSettings S; NavMesh m; NavBakeStats st;
        CHECK(NavMeshBaker::Bake(in, S, m, nullptr, nullptr, &st));
        CHECK(st.meshTrianglesDropped == 7);
        CHECK(st.incomplete);
    }
    {   // NaN, infinite and absurd corners are skipped (and counted); the good floor still bakes.
        NavBakeInput in;
        NavBakeInput::Triangles t;
        const float nan = std::nanf(""), inf = INFINITY;
        t.corners = {{-4, 0, -4}, {4, 0, 4}, {4, 0, -4}, {-4, 0, -4}, {-4, 0, 4}, {4, 0, 4}};
        t.corners.insert(t.corners.end(), {{nan, 0, 0}, {1, 0, 0}, {0, 0, 1}});
        t.corners.insert(t.corners.end(), {{0, 0, 0}, {inf, 0, 1}, {0, 0, 1}});
        t.corners.insert(t.corners.end(), {{0, 0, 0}, {1e30f, 0, 1}, {0, 0, 1}});
        in.meshes.push_back(t);
        NavMeshBuildSettings S; NavMesh m; NavBakeStats st;
        CHECK(NavMeshBaker::Bake(in, S, m, nullptr, nullptr, &st));
        CHECK(st.meshTrianglesSkipped == 3);
        CHECK(st.incomplete);
        CHECK(Walkable(&m, {0, 0, 0}));
    }
    {   // A dynamic body: its convex hull bakes only when asked for; with no hull it bakes as its box.
        Scene s("dynamic"); s.physicsEnabled = false;
        GameObject* g = MeshObject(s, "Dyn", Mesh::Cube(2.0f), {0, 1, 0});
        g->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Dynamic;
        NavBakeInput none; NavMeshBaker::Gather(s, none, false);
        CHECK(none.meshes.empty() && none.boxes.empty());
        NavBakeInput with; NavMeshBaker::Gather(s, with, true);
        CHECK(with.meshes.size() + with.boxes.size() == 1);
        Scene s2("dynamic no mesh"); s2.physicsEnabled = false;
        GameObject* g2 = s2.CreateGameObject("Dyn box");
        g2->AddComponent<MeshCollider3D>()->size = {2, 2, 2};
        g2->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Dynamic;
        NavBakeInput nb; NavMeshBaker::Gather(s2, nb, true);
        CHECK(nb.meshes.empty());
        CHECK(nb.boxes.size() == 1);
    }
    {   // A wall standing exactly on a column border, with a grid origin that is not a multiple of the cell size and
        // sits far from the world origin: it is one wall, and a path cannot cross it.
        const float cs = 0.25f, ox = 5000.3f, oz = -7000.1f;
        NavBakeInput in; in.hasBounds = true; in.boundsMin = {ox, -1, oz}; in.boundsMax = {ox + 16, 3, oz + 16};
        NavBakeInput::Triangles t;
        auto quad = [&](float x0, float z0, float x1, float z1) {
            t.corners.insert(t.corners.end(), {{x0, 0, z0}, {x1, 0, z1}, {x1, 0, z0}, {x0, 0, z0}, {x0, 0, z1}, {x1, 0, z1}});
        };
        quad(ox + 1, oz + 1, ox + 15, oz + 15);
        const float wx = ox + 32.0f * cs;   // exactly the border between columns 31 and 32
        const float wz0 = oz + 4, wz1 = oz + 12;
        t.corners.insert(t.corners.end(), {{wx, 0, wz0}, {wx, 0, wz1}, {wx, 2, wz1}, {wx, 0, wz0}, {wx, 2, wz1}, {wx, 2, wz0}});
        in.meshes.push_back(t);
        NavMeshBuildSettings S; S.cellSize = cs; S.agentRadius = 0.4f;
        NavMesh m; NavBakeStats st;
        CHECK(NavMeshBaker::Bake(in, S, m, nullptr, nullptr, &st));
        CHECK(!st.incomplete);
        std::vector<Vec3> path;
        CHECK(m.FindPath({wx - 3, 0, oz + 8}, {wx + 3, 0, oz + 8}, path));
        CHECK(!Crosses(path, wx - 0.2f, wx + 0.2f, wz0, wz1));
        CHECK(!Walkable(&m, {wx, 0, oz + 8}, 0.1f));
    }

    TEST_MAIN_RESULT();
}
