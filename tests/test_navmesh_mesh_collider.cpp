// The navmesh bake reads a Mesh Collider's real triangles (NavMeshBaker::Gather / Bake), not its bounding box: an
// L-shaped floor leaves its missing corner unwalkable, a ramp's floor follows the slope, a wall made of a single quad
// blocks, a closed mesh pillar is walked around, a surface facing down is a ceiling, a mirrored transform keeps its
// floor, and a Mesh Collider without a mesh still bakes as its box. Written for this engine from the general idea of
// rasterizing triangles into a column heightfield (clip each triangle to each column's square); no other project's
// code was used.
#include "test_framework.hpp"
#include <Okay.hpp>
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

    {   // A surface whose front faces point down is a ceiling: it is never a floor.
        Scene s("ceiling"); s.physicsEnabled = false;
        Mesh down; AddQuad(down, -4, -4, 4, 4, 0.0f, true);
        MeshObject(s, "Down", down);
        NavMeshSurface* ns = Surface(s);
        NavBakeStats st;
        CHECK(ns->Bake(s, &st));
        CHECK(!Walkable(ns->Mesh(), {0, 0, 0}));
        CHECK(st.walkable == 0);
        // Low over a box floor it takes the head room: the floor under it is not walkable, beside it is.
        Scene s2("low ceiling"); s2.physicsEnabled = false;
        BoxFloor(s2);
        Mesh low; AddQuad(low, -2, -2, 2, 2, 1.0f, true);
        MeshObject(s2, "Low", low);
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

    TEST_MAIN_RESULT();
}
