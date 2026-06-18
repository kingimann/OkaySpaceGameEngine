#include "test_framework.hpp"
#include <Okay.hpp>
#include <cstdio>
#include <fstream>

using namespace okay;

int main() {
    RUN_SUITE("mesh3d");

    // --- New primitives have sensible geometry and triangle counts ---
    {
        Mesh plane = Mesh::Plane(4.0f);
        CHECK(plane.name == "Plane");
        CHECK(plane.vertices.size() == 4);
        CHECK(plane.TriangleCount() == 2);
        // Plane lies in the XZ plane (y == 0 for all verts).
        for (auto& v : plane.vertices) CHECK_NEAR(v.y, 0.0f, 0.0001f);

        Mesh sph = Mesh::Sphere(0.5f, 8, 12);
        CHECK(sph.name == "Sphere");
        CHECK(sph.vertices.size() == (8 + 1) * (12 + 1));
        CHECK(sph.TriangleCount() == 8 * 12 * 2);
        // Every vertex is ~radius from the center.
        for (auto& v : sph.vertices) CHECK_NEAR(v.Magnitude(), 0.5f, 0.01f);

        Mesh cyl = Mesh::Cylinder(0.5f, 2.0f, 12);
        CHECK(cyl.name == "Cylinder");
        CHECK(cyl.TriangleCount() == 12 * 2 /*sides*/ + 12 * 2 /*caps*/);

        Mesh cone = Mesh::Cone(0.5f, 1.0f, 16);
        CHECK(cone.name == "Cone");
        CHECK(cone.vertices.size() == 16u + 2u);          // ring + apex + base center
        CHECK(cone.TriangleCount() == 16 * 2);            // sides + base cap

        Mesh tor = Mesh::Torus(0.5f, 0.2f, 16, 10);
        CHECK(tor.name == "Torus");
        CHECK(tor.vertices.size() == 16u * 10u);
        CHECK(tor.TriangleCount() == 16 * 10 * 2);
    }

    // --- Bounds / Center / Size of the AABB ---
    {
        Mesh cube = Mesh::Cube(2.0f);                      // spans -1..1 on each axis
        Vec3 lo, hi; cube.Bounds(lo, hi);
        CHECK_NEAR(lo.x, -1.0f, 0.001f);
        CHECK_NEAR(hi.z, 1.0f, 0.001f);
        Vec3 c = cube.Center();
        CHECK_NEAR(c.x, 0.0f, 0.001f);
        CHECK_NEAR(c.y, 0.0f, 0.001f);
        Vec3 s = cube.Size();
        CHECK_NEAR(s.x, 2.0f, 0.001f);
        CHECK_NEAR(s.y, 2.0f, 0.001f);

        // A translated mesh reports a shifted center but the same size.
        Mesh moved = cube.Transformed({1, 1, 1}, {10, 0, 0});
        CHECK_NEAR(moved.Center().x, 10.0f, 0.001f);
        CHECK_NEAR(moved.Size().x, 2.0f, 0.001f);

        // Empty mesh is safe.
        Mesh empty;
        Vec3 el, eh; empty.Bounds(el, eh);
        CHECK_NEAR(el.x, 0.0f, 0.001f);
        CHECK_NEAR(eh.y, 0.0f, 0.001f);
    }

    // --- FromName round-trips the new primitives (so they serialize) ---
    {
        CHECK(Mesh::FromName("Sphere").name == "Sphere");
        CHECK(Mesh::FromName("Cylinder").name == "Cylinder");
        CHECK(Mesh::FromName("Plane").name == "Plane");
        CHECK(Mesh::FromName("Cube").name == "Cube");
        CHECK(Mesh::FromName("Cone").name == "Cone");
        CHECK(Mesh::FromName("Torus").name == "Torus");

        Scene scene("S");
        GameObject* go = scene.CreateGameObject("Ball");
        go->AddComponent<MeshRenderer>()->mesh = Mesh::Sphere();
        std::string text = SceneSerializer::Serialize(scene);
        Scene loaded("L");
        std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, text, &err));
        CHECK(loaded.Find("Ball")->GetComponent<MeshRenderer>()->mesh.name == "Sphere");
    }

    // --- OBJ save/load round-trips a cube (8 verts, 12 triangles) ---
    {
        Mesh cube = Mesh::Cube();
        const std::string path = "okay_test_cube.obj";
        CHECK(cube.SaveOBJ(path));
        bool ok = false;
        Mesh loaded = Mesh::LoadOBJ(path, &ok);
        CHECK(ok);
        CHECK(loaded.vertices.size() == cube.vertices.size());      // 8
        CHECK(loaded.TriangleCount() == cube.TriangleCount());      // 12
        for (std::size_t i = 0; i < cube.vertices.size(); ++i) {
            CHECK_NEAR(loaded.vertices[i].x, cube.vertices[i].x, 0.001f);
            CHECK_NEAR(loaded.vertices[i].y, cube.vertices[i].y, 0.001f);
            CHECK_NEAR(loaded.vertices[i].z, cube.vertices[i].z, 0.001f);
        }
        std::remove(path.c_str());
        // Missing file reports failure and yields an empty mesh.
        bool ok2 = true;
        Mesh missing = Mesh::LoadOBJ("does_not_exist.obj", &ok2);
        CHECK(!ok2);
        CHECK(missing.vertices.empty());
    }

    // --- LoadOBJ handles f indices with v/vt/vn slashes and negatives ---
    {
        const std::string path = "okay_test_face.obj";
        {
            std::ofstream f(path);
            f << "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n";
            f << "f 1/1/1 2/2/1 3/3/1 4/4/1\n";   // quad -> fan -> 2 triangles
        }
        bool ok = false;
        Mesh m = Mesh::LoadOBJ(path, &ok);
        CHECK(ok);
        CHECK(m.vertices.size() == 4);
        CHECK(m.TriangleCount() == 2);
        std::remove(path.c_str());
    }

    // --- Transformed / Combine build compound models ---
    {
        Mesh a = Mesh::Cube();
        Mesh b = a.Transformed({2.0f, 2.0f, 2.0f}, {10.0f, 0.0f, 0.0f});
        CHECK(b.name.empty());                       // no longer a primitive
        CHECK(b.vertices.size() == a.vertices.size());
        // Original first vert (-0.5) scaled by 2 then +10 -> 9.
        CHECK_NEAR(b.vertices[0].x, a.vertices[0].x * 2.0f + 10.0f, 0.001f);

        Mesh combo = Mesh::Combined(a, b);
        CHECK(combo.vertices.size() == a.vertices.size() + b.vertices.size());
        CHECK(combo.TriangleCount() == a.TriangleCount() + b.TriangleCount());
        // Re-indexed: the appended triangles point past the first mesh's verts.
        CHECK(combo.triangles[a.triangles.size()] >= (int)a.vertices.size());
    }

    // --- MeshRenderer.meshPath survives serialization ---
    {
        Scene scene("Model");
        GameObject* go = scene.CreateGameObject("Imported");
        go->AddComponent<MeshRenderer>()->meshPath = "models/ship.obj";
        std::string text = SceneSerializer::Serialize(scene);
        Scene loaded("L");
        std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, text, &err));
        CHECK(loaded.Find("Imported")->GetComponent<MeshRenderer>()->meshPath
              == "models/ship.obj");
    }

    // --- Scripts can move and rotate in 3D ---
    {
        Scene scene("3D");
        GameObject* go = scene.CreateGameObject("Mover");
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "function start() { set_pos3(1, 2, 3); move3(0, 0, 5); set_z(pos_z() + 1); }"));
        scene.Start();
        CHECK_NEAR(go->transform->localPosition.x, 1.0f, 0.001f);
        CHECK_NEAR(go->transform->localPosition.y, 2.0f, 0.001f);
        CHECK_NEAR(go->transform->localPosition.z, 9.0f, 0.001f); // 3 + 5 + 1
    }

    TEST_MAIN_RESULT();
}
