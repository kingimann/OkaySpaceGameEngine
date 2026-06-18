#include "test_framework.hpp"
#include <Okay.hpp>

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
    }

    // --- FromName round-trips the new primitives (so they serialize) ---
    {
        CHECK(Mesh::FromName("Sphere").name == "Sphere");
        CHECK(Mesh::FromName("Cylinder").name == "Cylinder");
        CHECK(Mesh::FromName("Plane").name == "Plane");
        CHECK(Mesh::FromName("Cube").name == "Cube");

        Scene scene("S");
        GameObject* go = scene.CreateGameObject("Ball");
        go->AddComponent<MeshRenderer>()->mesh = Mesh::Sphere();
        std::string text = SceneSerializer::Serialize(scene);
        Scene loaded("L");
        std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, text, &err));
        CHECK(loaded.Find("Ball")->GetComponent<MeshRenderer>()->mesh.name == "Sphere");
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
