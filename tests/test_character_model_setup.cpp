#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Render/ModelImport.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/FirstPersonController.hpp"
#include "okay/Components/ThirdPersonController.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include <cmath>
#include <string>

using namespace okay;

// A model swapped onto a player controller must face the way the controller
// walks (local -Z) whatever axis the file was exported facing, and the player
// must get a collider + rigidbody that wraps the model actually shown.

// A tiny humanoid facing +Z (Mixamo / glTF convention): its LEFT side is +X.
static GameObject* MakeModel(Scene& s, GameObject* parent, const char* name) {
    GameObject* root = s.CreateGameObject(name);
    root->transform->SetParent(parent->transform, false);
    auto bone = [&](const char* n, Vec3 p) {
        GameObject* g = s.CreateGameObject(n);
        g->transform->SetParent(root->transform, false);
        g->transform->localPosition = p;
        return g;
    };
    bone("mixamorig:Hips", {0, 0.9f, 0});
    bone("mixamorig:LeftUpLeg", {0.1f, 0.85f, 0});
    bone("mixamorig:RightUpLeg", {-0.1f, 0.85f, 0});
    bone("mixamorig:LeftArm", {0.2f, 1.4f, 0});
    bone("mixamorig:RightArm", {-0.2f, 1.4f, 0});
    GameObject* body = bone("Body", {0, 0, 0});
    auto* mr = body->AddComponent<MeshRenderer>();
    mr->mesh = Mesh::Cube(1.0f);
    body->transform->localScale = {1.6f, 1.8f, 0.3f};   // T-pose: wide arms, thin depth
    body->transform->localPosition = {0, 0.9f, 0};
    return root;
}

// The model's right-minus-left vector in player space. Facing -Z means the
// character's right side is +X.
static Vec3 RightSide(Scene& s, GameObject* player) {
    Mat4 inv = player->transform->LocalToWorldMatrix().Inverse();
    Vec3 l = inv.MultiplyPoint(s.Find("mixamorig:LeftUpLeg")->transform->Position());
    Vec3 r = inv.MultiplyPoint(s.Find("mixamorig:RightUpLeg")->transform->Position());
    return r - l;
}

int main() {
    RUN_SUITE("character_model_setup");

    for (float yaw : {0.0f, 180.0f, 90.0f, -90.0f, 30.0f}) {   // +Z, -Z, +X, -X exports, and a skewed one
        Scene s; s.physicsEnabled = false;
        GameObject* player = s.CreateGameObject("Player");
        player->AddComponent<ThirdPersonController>();
        GameObject* model = MakeModel(s, player, "Model");
        model->transform->localRotation = Quat::Euler(0, yaw, 0);
        CHECK(FindCharacterModelRoot(s, player) == model);
        int d = FaceCharacterModel(s, player, model);
        CHECK(d >= 0);
        Vec3 r = RightSide(s, player);
        CHECK(r.x > 0.15f);                 // right side on +X ...
        CHECK(std::fabs(r.z) < 0.02f);      // ... and square to the walk direction
        CHECK(FaceCharacterModel(s, player, model) == 0);   // idempotent
    }

    {   // Manual turn.
        Scene s; s.physicsEnabled = false;
        GameObject* player = s.CreateGameObject("Player");
        GameObject* model = MakeModel(s, player, "Model");
        FaceCharacterModel(s, player, model);
        CHECK(FaceCharacterModel(s, player, model, 180.0f) == 180);
        CHECK(RightSide(s, player).x < -0.15f);
    }

    {   // A controller added straight onto a model: its children turn, the root keeps its rotation.
        Scene s; s.physicsEnabled = false;
        GameObject* holder = s.CreateGameObject("Holder");
        GameObject* inner = MakeModel(s, holder, "Armature");
        (void)inner;
        GameObject* player = holder;
        player->AddComponent<FirstPersonController>();
        CHECK(FindCharacterModelRoot(s, player) == inner);
        GameObject* cam = s.CreateGameObject("Cam");
        cam->AddComponent<Camera>();
        cam->transform->SetParent(player->transform, false);
        cam->transform->localPosition = {0, 1.6f, 0};
        FaceCharacterModel(s, player, player);   // the player itself as the model root
        CHECK(RightSide(s, player).x > 0.15f);
        CHECK(std::fabs(player->transform->localRotation.y) < 1e-4f);
        CHECK(std::fabs(cam->transform->localPosition.y - 1.6f) < 1e-4f);   // camera untouched
    }

    {   // No left/right bones: undetectable, nothing turned.
        Scene s; s.physicsEnabled = false;
        GameObject* player = s.CreateGameObject("Player");
        GameObject* m = s.CreateGameObject("Rock");
        m->transform->SetParent(player->transform, false);
        m->AddComponent<MeshRenderer>()->mesh = Mesh::Cube(1.0f);
        CHECK(FindCharacterModelRoot(s, player) == m);
        CHECK(FaceCharacterModel(s, player, m) == -1);
        CHECK(std::fabs(m->transform->localRotation.w - 1.0f) < 1e-5f);
    }

    {   // Collider: added + fitted around the model, rigidbody added.
        Scene s; s.physicsEnabled = false;
        GameObject* player = s.CreateGameObject("Player");
        player->AddComponent<ThirdPersonController>();
        MakeModel(s, player, "Model");
        std::string msg = FitCharacterCollider(s, player);
        auto* cap = player->GetComponent<CapsuleCollider3D>();
        CHECK(cap != nullptr);
        CHECK(player->GetComponent<Rigidbody3D>() != nullptr);
        CHECK(msg.find("capsule collider added") != std::string::npos);
        if (cap) {
            CHECK(std::fabs(cap->height - 1.8f) < 0.01f);
            CHECK(std::fabs(cap->offset.y - 0.9f) < 0.01f);
            // Width from the body's depth (clamped to a person-shaped column), not the T-pose span.
            CHECK(cap->radius > 0.2f && cap->radius < 0.45f);
        }
        // A template's box is refitted, not duplicated.
        Scene s2; s2.physicsEnabled = false;
        GameObject* p2 = s2.CreateGameObject("Player");
        auto* box = p2->AddComponent<BoxCollider3D>();
        p2->AddComponent<Rigidbody3D>();
        GameObject* m2 = MakeModel(s2, p2, "Model");
        m2->transform->localScale = {2, 2, 2};   // a 3.6u giant
        FitCharacterCollider(s2, p2, m2);
        CHECK(!p2->GetComponent<CapsuleCollider3D>());
        CHECK(std::fabs(box->size.y - 3.6f) < 0.02f);
        CHECK(std::fabs(box->offset.y - 1.8f) < 0.02f);
    }

    {   // First person: a custom model's player keeps the blocky body hidden.
        Scene s; s.physicsEnabled = false;
        GameObject* player = s.CreateGameObject("Player");
        auto* ch = player->AddComponent<Character>();
        ch->Apply();
        auto* mr = player->GetComponent<MeshRenderer>();
        if (!mr) mr = player->AddComponent<MeshRenderer>();
        ch->enabled = false;      // what Set Character Model does on the clip path
        mr->enabled = false;
        auto* fp = player->AddComponent<FirstPersonController>();
        fp->lockCursor = false;
        fp->ApplyBodyVisibility();
        CHECK(!mr->enabled);
        ch->enabled = true; ch->driveExternal = true;   // retarget path
        fp->ApplyBodyVisibility();
        CHECK(!mr->enabled);
        ch->driveExternal = false;                      // plain blocky player: visible
        fp->ApplyBodyVisibility();
        CHECK(mr->enabled);
    }

    TEST_MAIN_RESULT();
}
