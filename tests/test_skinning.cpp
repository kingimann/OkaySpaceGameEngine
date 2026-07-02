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

    // ---- Skin data survives save/reload (joints re-resolved by name) ----
    {
        Scene a("save");
        GameObject* m = a.CreateGameObject("M");
        m->AddComponent<MeshRenderer>();
        auto* save = m->AddComponent<SkinnedMesh>();
        a.CreateGameObject("Bone");
        save->bind.vertices = { Vec3{1, 0, 0} };
        save->bind.triangles = { 0, 0, 0 };
        save->jointIdx = { {0, 0, 0, 0} };
        save->jointWt  = { {1.0f, 0.0f, 0.0f, 0.0f} };
        save->jointNames = { "Bone" };
        save->inverseBind = { Mat4::Identity() };

        std::string text = SceneSerializer::Serialize(a);
        Scene b("load");
        CHECK(SceneSerializer::Deserialize(b, text));

        GameObject* lm = b.Find("M");
        GameObject* lb = b.Find("Bone");
        auto* lsm = lm ? lm->GetComponent<SkinnedMesh>() : nullptr;
        CHECK(lsm != nullptr && lb != nullptr);
        if (lsm && lb) {
            CHECK(lsm->bind.vertices.size() == 1);
            CHECK(lsm->jointNames.size() == 1 && lsm->jointNames[0] == "Bone");
            CHECK(lsm->inverseBind.size() == 1);
            lsm->ResolveJoints();                         // happens in Start() at runtime
            CHECK(lsm->joints.size() == 1 && lsm->joints[0] == lb->transform);
            lb->transform->localRotation = Quat::Euler(0, 0, 90);
            lsm->Skin();
            auto* lmr = lm->GetComponent<MeshRenderer>();
            CHECK(std::fabs(lmr->mesh.vertices[0].x - 0.0f) < 1e-3f);
            CHECK(std::fabs(lmr->mesh.vertices[0].y - 1.0f) < 1e-3f);
        }
    }

    // ---- ModelAnimator: multiple clips, switch + drive a node, survive save/reload ----
    {
        Scene a("ma");
        GameObject* root = a.CreateGameObject("Model");
        GameObject* bone = a.CreateGameObject("B");
        auto* ma = root->AddComponent<ModelAnimator>();
        ma->loop = false; ma->autoPlay = false;
        // "slideX": B.position.x 0 -> 4 over 1s. "slideY": B.position.y 0 -> 6 over 1s.
        ModelAnimator::Clip cx; cx.name = "slideX";
        { ModelAnimator::NodeClip nc; nc.node = "B";
          nc.clip.AddKey("position.x", 0, 0); nc.clip.AddKey("position.x", 1, 4);
          cx.nodes.push_back(nc); }
        ModelAnimator::Clip cy; cy.name = "slideY";
        { ModelAnimator::NodeClip nc; nc.node = "B";
          nc.clip.AddKey("position.y", 0, 0); nc.clip.AddKey("position.y", 1, 6);
          cy.nodes.push_back(nc); }
        ma->clips = { cx, cy };

        CHECK(ma->ClipCount() == 2);
        CHECK(ma->ClipNames()[1] == "slideY");

        ma->Play("slideX");
        CHECK(ma->CurrentName() == "slideX");
        if (auto* an = bone->GetComponent<Animator>()) { an->SetTime(0.5f); }   // mid: x=2
        CHECK(bone->GetComponent<Animator>() != nullptr);
        CHECK(std::fabs(bone->transform->localPosition.x - 2.0f) < 0.05f);

        ma->Play("slideY");
        bone->GetComponent<Animator>()->SetTime(0.5f);                          // mid: y=3
        CHECK(std::fabs(bone->transform->localPosition.y - 3.0f) < 0.05f);

        // Save/reload keeps both clips.
        std::string text = SceneSerializer::Serialize(a);
        Scene b("ma2");
        CHECK(SceneSerializer::Deserialize(b, text));
        GameObject* lr = b.Find("Model");
        auto* lma = lr ? lr->GetComponent<ModelAnimator>() : nullptr;
        CHECK(lma && lma->ClipCount() == 2);
        if (lma) {
            CHECK(lma->Play("slideY"));
            GameObject* lb = b.Find("B");
            if (lb && lb->GetComponent<Animator>()) {
                lb->GetComponent<Animator>()->SetTime(0.5f);
                CHECK(std::fabs(lb->transform->localPosition.y - 3.0f) < 0.05f);
            }
        }
    }

    // ---- Locomotion: ModelAnimator auto-switches idle/walk/run from movement ----
    {
        Scene s("loco");
        GameObject* model = s.CreateGameObject("Model");
        auto* ma = model->AddComponent<ModelAnimator>();
        auto mk = [](const char* name) {
            ModelAnimator::Clip c; c.name = name;
            ModelAnimator::NodeClip nc; nc.node = "Model";
            nc.clip.AddKey("scale.x", 0, 1); nc.clip.AddKey("scale.x", 1, 1);
            c.nodes.push_back(nc); return c;
        };
        ma->clips = { mk("idle"), mk("walk"), mk("run") };
        ma->autoPlay = false;
        ma->driveByMovement = true;
        ma->idleClip = "idle"; ma->walkClip = "walk"; ma->runClip = "run";
        ma->walkThreshold = 0.3f; ma->runThreshold = 3.0f;
        s.Start();

        // Standing still -> idle.
        s.Update(0.1f); s.Update(0.1f);
        CHECK(ma->CurrentName() == "idle");

        // Move slowly (~1 unit/s) -> walk.
        for (int i = 0; i < 3; ++i) { model->transform->localPosition.x += 0.1f; s.Update(0.1f); }
        CHECK(ma->CurrentName() == "walk");

        // Move fast (~5 unit/s) -> run.
        for (int i = 0; i < 3; ++i) { model->transform->localPosition.x += 0.5f; s.Update(0.1f); }
        CHECK(ma->CurrentName() == "run");

        // Stop -> back to idle.
        for (int i = 0; i < 3; ++i) s.Update(0.1f);
        CHECK(ma->CurrentName() == "idle");
    }

    TEST_MAIN_RESULT();
}
