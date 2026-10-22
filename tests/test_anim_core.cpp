#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Components/SkinnedMesh.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>

using namespace okay;

static bool Near(const Vec3& a, const Vec3& b, float eps = 1e-3f) {
    return std::fabs(a.x - b.x) < eps && std::fabs(a.y - b.y) < eps && std::fabs(a.z - b.z) < eps;
}
static void Tick(Scene& s, int frames, float dt = 1.0f / 60.0f) { for (int i = 0; i < frames; ++i) s.Update(dt); }

// One clip that moves `node`'s local position.x from 0 to `dist` over 1s.
static ModelAnimator::Clip Slide(const char* name, const char* node, const char* track, float dist) {
    ModelAnimator::Clip c; c.name = name;
    ModelAnimator::NodeClip nc; nc.node = node;
    nc.clip.smoothKeys = false;
    nc.clip.AddKey(track, 0.0f, 0.0f);
    nc.clip.AddKey(track, 1.0f, dist);
    c.nodes.push_back(nc);
    return c;
}

int main() {
    RUN_SUITE("anim_core");

    // ---- World-matrix cache follows direct field writes, parents and reparenting.
    {
        Scene s;
        GameObject* a = s.CreateGameObject("A");
        GameObject* b = s.CreateGameObject("B");
        GameObject* c = s.CreateGameObject("C");
        b->transform->SetParent(a->transform, false);
        c->transform->SetParent(b->transform, false);
        b->transform->localPosition = {1, 0, 0};
        c->transform->localPosition = {0, 1, 0};
        CHECK(Near(c->transform->Position(), {1, 1, 0}));
        a->transform->localPosition = {10, 0, 0};                    // grandparent moves
        CHECK(Near(c->transform->Position(), {11, 1, 0}));
        a->transform->localRotation = Quat::Euler(0, 0, 90);         // ... and turns
        CHECK(Near(c->transform->Position(), {9, 1, 0}));
        auto v0 = c->transform->WorldVersion();
        CHECK(c->transform->WorldVersion() == v0);                   // nothing changed: same version
        a->transform->localScale = {2, 2, 2};
        CHECK(c->transform->WorldVersion() != v0);
        CHECK(Near(c->transform->Position(), {8, 2, 0}));
        c->transform->SetParent(nullptr, true);                      // keeps its world spot
        CHECK(Near(c->transform->Position(), {8, 2, 0}));
        Quat r = b->transform->Rotation();
        CHECK(std::fabs(Quat::Dot(r, Quat::Euler(0, 0, 90))) > 0.999f);
    }

    // ---- Two copies of one model (same bone names) each animate their own bones.
    {
        Scene s; s.physicsEnabled = false;
        GameObject* rootA = s.CreateGameObject("Hero");
        GameObject* boneA = s.CreateGameObject("Arm"); boneA->transform->SetParent(rootA->transform, false);
        GameObject* rootB = s.CreateGameObject("Hero");
        GameObject* boneB = s.CreateGameObject("Arm"); boneB->transform->SetParent(rootB->transform, false);
        auto* ma = rootA->AddComponent<ModelAnimator>(); ma->autoPlay = false;
        auto* mb = rootB->AddComponent<ModelAnimator>(); mb->autoPlay = false;
        ma->clips = {Slide("a", "Arm", "position.x", 4.0f)};
        mb->clips = {Slide("b", "Arm", "position.y", 6.0f)};
        ma->Play("a"); ma->SetTime(0.5f);
        mb->Play("b"); mb->SetTime(0.5f);
        CHECK(std::fabs(boneA->transform->localPosition.x - 2.0f) < 1e-3f);
        CHECK(std::fabs(boneA->transform->localPosition.y) < 1e-3f);
        CHECK(std::fabs(boneB->transform->localPosition.y - 3.0f) < 1e-3f);
        CHECK(std::fabs(boneB->transform->localPosition.x) < 1e-3f);
        // Bindings survive objects being added/removed elsewhere in the scene.
        GameObject* junk = s.CreateGameObject("Junk");
        s.Destroy(junk); s.FlushDestroyed();
        ma->SetTime(0.25f);
        CHECK(std::fabs(boneA->transform->localPosition.x - 1.0f) < 1e-3f);
    }

    // ---- Root motion through a centimetre armature (scale 0.01): the object
    //      moves in metres, and the hips stay over the object.
    {
        Scene s; s.physicsEnabled = false;
        GameObject* model = s.CreateGameObject("Model");
        GameObject* arm = s.CreateGameObject("Armature"); arm->transform->SetParent(model->transform, false);
        arm->transform->localScale = {0.01f, 0.01f, 0.01f};
        GameObject* hips = s.CreateGameObject("Hips"); hips->transform->SetParent(arm->transform, false);
        auto* ma = model->AddComponent<ModelAnimator>();
        ma->clips = {Slide("walk", "Hips", "position.z", 100.0f)};   // 100 cm forward per second
        ma->rootMotion = true; ma->loop = true;
        s.Start();
        Tick(s, 30);                                                  // half a second
        CHECK(std::fabs(model->transform->Position().z - 0.5f) < 0.03f);
        CHECK(std::fabs(hips->transform->localPosition.z) < 1e-3f);   // travel stripped from the bone
        Tick(s, 60);                                                  // across a loop wrap
        CHECK(std::fabs(model->transform->Position().z - 1.5f) < 0.05f);
        CHECK(std::fabs(ma->LastRootDelta().z - 1.0f / 60.0f) < 0.005f);
    }

    // ---- Root motion through a Blender-style rotated armature (-90 deg about X):
    //      the bone's local Y is world forward, its local Z world up.
    {
        Scene s; s.physicsEnabled = false;
        GameObject* model = s.CreateGameObject("Model");
        GameObject* arm = s.CreateGameObject("Armature"); arm->transform->SetParent(model->transform, false);
        arm->transform->localRotation = Quat::Euler(-90, 0, 0);
        GameObject* hips = s.CreateGameObject("Hips"); hips->transform->SetParent(arm->transform, false);
        hips->transform->localPosition = {0, 0, 1};                   // 1 unit up in world
        auto* ma = model->AddComponent<ModelAnimator>();
        ModelAnimator::Clip c; c.name = "walk";
        ModelAnimator::NodeClip nc; nc.node = "Hips"; nc.clip.smoothKeys = false;
        for (float t : {0.0f, 1.0f}) {
            Vec3 local = Quat::Euler(-90, 0, 0).Conjugate() * Vec3{0, 1.0f + 0.2f * t, 2.0f * t};   // world: up 1(+bob), forward 2
            nc.clip.AddKey("position.x", t, local.x); nc.clip.AddKey("position.y", t, local.y); nc.clip.AddKey("position.z", t, local.z);
        }
        c.nodes.push_back(nc);
        ma->clips = {c};
        ma->rootMotion = true; ma->loop = false;
        s.Start();
        Tick(s, 30);
        Vec3 p = model->transform->Position();
        CHECK(std::fabs(p.z - 1.0f) < 0.06f);    // moved forward in the world
        CHECK(std::fabs(p.y) < 1e-3f);           // not up (rootMotionY off)
        CHECK(std::fabs(hips->transform->Position().y - 1.1f) < 0.02f);       // the bob stays on the bone
        CHECK(std::fabs(hips->transform->Position().z - p.z) < 0.02f);        // hips stay over the object
    }

    // ---- Root rotation: a turn-in-place clip turns the object, not the bone.
    {
        Scene s; s.physicsEnabled = false;
        GameObject* model = s.CreateGameObject("Model");
        GameObject* hips = s.CreateGameObject("Hips"); hips->transform->SetParent(model->transform, false);
        auto* ma = model->AddComponent<ModelAnimator>();
        ModelAnimator::Clip c; c.name = "turn";
        ModelAnimator::NodeClip nc; nc.node = "Hips"; nc.clip.smoothKeys = false;
        for (int i = 0; i <= 4; ++i) {
            float t = i / 4.0f;
            Quat q = Quat::Euler(0, 90.0f * t, 0);
            nc.clip.AddKey("rotation.qx", t, q.x); nc.clip.AddKey("rotation.qy", t, q.y);
            nc.clip.AddKey("rotation.qz", t, q.z); nc.clip.AddKey("rotation.qw", t, q.w);
            nc.clip.AddKey("position.x", t, 0.001f * i);   // a hint of travel so it's the root
        }
        c.nodes.push_back(nc);
        ma->clips = {c};
        ma->rootMotion = true; ma->rootMotionRotation = true; ma->loop = false;
        s.Start();
        Tick(s, 61);
        float yaw = ModelAnimator::YawOf(model->transform->Rotation());
        CHECK(std::fabs(yaw - 90.0f) < 4.0f);
        float boneYaw = ModelAnimator::YawOf(hips->transform->localRotation);
        CHECK(std::fabs(boneYaw) < 4.0f);                                // turning moved to the object
    }

    // ---- SkinnedMesh resolves joints inside its own model.
    {
        Scene s;
        GameObject* m1 = s.CreateGameObject("Char");
        GameObject* b1 = s.CreateGameObject("Spine"); b1->transform->SetParent(m1->transform, false);
        GameObject* m2 = s.CreateGameObject("Char");
        GameObject* b2 = s.CreateGameObject("Spine"); b2->transform->SetParent(m2->transform, false);
        GameObject* mesh2 = s.CreateGameObject("Body"); mesh2->transform->SetParent(m2->transform, false);
        auto* sm = mesh2->AddComponent<SkinnedMesh>();
        sm->jointNames = {"Spine"};
        sm->ResolveJoints();
        CHECK(sm->joints.size() == 1 && sm->joints[0] == b2->transform);
        (void)b1;
    }

    // ---- Dual quaternion skinning keeps volume where linear collapses.
    //      Two bones along X; a ring of vertices half-weighted to each; the
    //      child bone twists 180 degrees about X.
    {
        Scene s;
        GameObject* root = s.CreateGameObject("Rig");
        GameObject* j0 = s.CreateGameObject("J0"); j0->transform->SetParent(root->transform, false);
        GameObject* j1 = s.CreateGameObject("J1"); j1->transform->SetParent(j0->transform, false);
        j1->transform->localPosition = {1, 0, 0};
        GameObject* body = s.CreateGameObject("Body"); body->transform->SetParent(root->transform, false);
        Mesh ring;
        for (int i = 0; i < 8; ++i) {
            float a = i * 0.785398f;
            ring.vertices.push_back({1.0f, std::cos(a), std::sin(a)});
        }
        auto setup = [&](int method) {
            auto* mr = body->GetComponent<MeshRenderer>(); if (!mr) mr = body->AddComponent<MeshRenderer>();
            mr->mesh = ring;
            auto* sm = body->GetComponent<SkinnedMesh>(); if (!sm) sm = body->AddComponent<SkinnedMesh>();
            sm->bind = ring;
            sm->joints = {j0->transform, j1->transform};
            sm->inverseBind = {j0->transform->LocalToWorldMatrix().Inverse(), j1->transform->LocalToWorldMatrix().Inverse()};
            sm->jointIdx.assign(ring.vertices.size(), {0, 1, 0, 0});
            sm->jointWt.assign(ring.vertices.size(), {0.5f, 0.5f, 0, 0});
            sm->method = method;
            return sm;
        };
        auto radius = [&](SkinnedMesh* sm) {
            j1->transform->localRotation = Quat::Euler(179.0f, 0, 0);
            sm->Skin();
            const Mesh& m = body->GetComponent<MeshRenderer>()->mesh;
            float r = 0.0f;
            for (const Vec3& v : m.vertices) r += std::sqrt(v.y * v.y + v.z * v.z);
            j1->transform->localRotation = Quat::Identity;
            return r / m.vertices.size();
        };
        float lin = radius(setup(0));
        float dq  = radius(setup(1));
        CHECK(lin < 0.1f);                   // linear blend: the ring collapses ("candy wrapper")
        CHECK(std::fabs(dq - 1.0f) < 0.02f); // dual quaternion: the ring keeps its radius
        // Rest pose: both methods give the bind mesh back.
        SkinnedMesh* sm = setup(1);
        sm->Skin();
        CHECK(Near(body->GetComponent<MeshRenderer>()->mesh.vertices[2], ring.vertices[2]));
        // Weight tools.
        sm->jointWt[0] = {0.6f, 0.2f, 0.005f, 0.0f};
        CHECK(sm->NormalizeWeights() >= 1);
        CHECK(std::fabs(sm->jointWt[0][0] + sm->jointWt[0][1] - 1.0f) < 1e-5f && sm->jointWt[0][2] == 0.0f);
        CHECK(sm->MaxInfluences() == 2);
        CHECK(sm->UnweightedVertices() == 0);
        CHECK(sm->VerticesPerJoint()[1] == 8);
    }

    // ---- Speed: a 60-bone rig, 4-second clip baked at 30 fps, 2000 frames.
    {
        Scene s; s.physicsEnabled = false;
        GameObject* model = s.CreateGameObject("Model");
        Transform* parent = model->transform;
        ModelAnimator::Clip clip; clip.name = "mocap";
        for (int b = 0; b < 60; ++b) {
            std::string nm = "bone" + std::to_string(b);
            GameObject* g = s.CreateGameObject(nm);
            g->transform->SetParent(b % 6 == 0 ? model->transform : parent, false);
            parent = g->transform;
            ModelAnimator::NodeClip nc; nc.node = nm; nc.clip.smoothKeys = false;
            for (int k = 0; k <= 120; ++k) {
                float t = k / 30.0f;
                Quat q = Quat::Euler(std::sin(t + b) * 20.0f, std::cos(t * 2.0f) * 10.0f, 0.0f);
                nc.clip.AddKey("rotation.qx", t, q.x); nc.clip.AddKey("rotation.qy", t, q.y);
                nc.clip.AddKey("rotation.qz", t, q.z); nc.clip.AddKey("rotation.qw", t, q.w);
                nc.clip.AddKey("position.y", t, 0.1f);
            }
            clip.nodes.push_back(nc);
        }
        auto* ma = model->AddComponent<ModelAnimator>();
        ma->clips = {clip};
        s.Start();
        auto t0 = std::chrono::steady_clock::now();
        Tick(s, 2000);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::printf("  60-bone clip: %.3f ms per frame\n", ms / 2000.0);
        CHECK(ms / 2000.0 < 1.0);
    }

    TEST_MAIN_RESULT();
}
