#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>

using namespace okay;

// SkinnedMesh deforms a bind-pose mesh by its joint Transforms: a vertex fully bound
// to a joint follows that joint's translation and rotation.
int main() {
    RUN_SUITE("skinning");

    Scene s("skin");
    // Mesh node at the origin (identity), so the skinned result is world-space directly.
    GameObject* meshGo = s.CreateGameObject("Mesh");
    meshGo->AddComponent<MeshRenderer>();
    auto* sm = meshGo->AddComponent<SkinnedMesh>();

    // One joint, starting at the origin (its inverse bind = identity).
    GameObject* joint = s.CreateGameObject("Joint");

    // A single vertex at (1,0,0), fully weighted to joint 0.
    sm->bind.vertices = { Vec3{1, 0, 0} };
    sm->bind.triangles = { 0, 0, 0 };
    sm->jointIdx = { {0, 0, 0, 0} };
    sm->jointWt  = { {1.0f, 0.0f, 0.0f, 0.0f} };
    sm->joints   = { joint->transform };
    sm->inverseBind = { Mat4::Identity() };

    auto* mr = meshGo->GetComponent<MeshRenderer>();

    // Rest pose: joint at identity -> vertex unchanged.
    sm->Skin();
    CHECK(std::fabs(mr->mesh.vertices[0].x - 1.0f) < 1e-4f);
    CHECK(std::fabs(mr->mesh.vertices[0].y - 0.0f) < 1e-4f);

    // Translate the joint +5 in Y -> vertex follows.
    joint->transform->localPosition = {0, 5, 0};
    sm->Skin();
    CHECK(std::fabs(mr->mesh.vertices[0].x - 1.0f) < 1e-3f);
    CHECK(std::fabs(mr->mesh.vertices[0].y - 5.0f) < 1e-3f);

    // Rotate the joint 90 deg about Z (back at origin): (1,0,0) -> (0,1,0).
    joint->transform->localPosition = {0, 0, 0};
    joint->transform->localRotation = Quat::Euler(0, 0, 90);
    sm->Skin();
    CHECK(std::fabs(mr->mesh.vertices[0].x - 0.0f) < 1e-3f);
    CHECK(std::fabs(mr->mesh.vertices[0].y - 1.0f) < 1e-3f);

    // Blend 50/50 between two joints: vertex sits halfway between their results.
    {
        GameObject* j2 = s.CreateGameObject("Joint2");
        sm->joints = { joint->transform, j2->transform };
        sm->inverseBind = { Mat4::Identity(), Mat4::Identity() };
        sm->jointIdx = { {0, 1, 0, 0} };
        sm->jointWt  = { {0.5f, 0.5f, 0.0f, 0.0f} };
        joint->transform->localRotation = Quat::Identity;       // joint 0 identity -> (1,0,0)
        joint->transform->localPosition = {0, 0, 0};
        j2->transform->localPosition = {0, 10, 0};              // joint 1 -> (1,10,0)
        sm->Skin();
        // Halfway: (1, 5, 0)
        CHECK(std::fabs(mr->mesh.vertices[0].x - 1.0f) < 1e-3f);
        CHECK(std::fabs(mr->mesh.vertices[0].y - 5.0f) < 1e-3f);
    }

    TEST_MAIN_RESULT();
}
