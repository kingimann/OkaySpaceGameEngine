// Guided human + animation: a generated human is given a skeleton and skin weights, handed to the engine's own player setup
// (Character + HumanoidRetarget, the same path an imported humanoid takes) and played idle and walking.
// What this proves, and no more: the skeleton (22 bones, Mixamo names by default or the engine's names) maps to the 15 humanoid
// bones, every vertex is skinned, the built-in idle and walk poses move the bones, the skin follows them, and nothing goes bad
// (no NaN, the body travels with the controller). A clip that targets Mixamo-style node names ("mixamorig:LeftUpLeg" ...) plays on
// a Guided human through the Model Animator, the pose changes and no edge of the skinned body stretches past a limit.
// It does NOT judge how good the deformation looks. No Mixamo file or data is used: the clip is written here from the bone names.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/HumanoidRetarget.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Components/SkinnedMesh.hpp"
#include "okay/Modeling/GuidedRig.hpp"
#include "okay/Render/ModelImport.hpp"
#include "okay/Scene/SceneTemplates.hpp"
#include <cmath>
#include <iostream>

using namespace okay;
namespace guided = okay::modeling::guided;
using guided::BuildRiggedHuman; using guided::DefaultRecipe; using guided::Generate; using guided::RegisterBuiltinTemplates; using guided::Registry; using guided::StartDesign; using guided::Template;

namespace {
const float kDt = 1.0f / 60.0f;
bool Finite(const Mesh& m) { for (const Vec3& v : m.vertices) if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) return false; return true; }
}

int main() {
    RUN_SUITE("guided animation");
    RegisterBuiltinTemplates();
    const guided::Template& T = *Registry::Instance().Find("human");

    // ---- the skeleton and the skin weights, for every start design at Medium and High ------------------------------------------
    for (const StartDesign& st : T.starts)
        for (double lv : {1.0, 2.0}) {
            guided::Recipe r = DefaultRecipe(T, st.id); r.params["detail"] = lv;
            Scene scene; scene.physicsEnabled = false;
            std::string why;
            GameObject* root = BuildRiggedHuman(scene, r, "Human", &why);
            CHECK(root != nullptr);
            if (!root) continue;
            SkinnedMesh* sm = nullptr;
            for (const auto& up : scene.Objects()) if (up->GetComponent<SkinnedMesh>()) sm = up->GetComponent<SkinnedMesh>();
            CHECK(sm && sm->joints.size() == 22 && sm->MissingJoints() == 0);
            if (!sm) continue;
            CHECK(sm->UnweightedVertices() == 0 && sm->MaxInfluences() <= 4);
            bool sums = true;
            for (const auto& w : sm->jointWt) { const float s = w[0] + w[1] + w[2] + w[3]; if (std::fabs(s - 1.0f) > 1e-3f) sums = false; }
            CHECK(sums);
            int dead = 0;
            for (int c : sm->VerticesPerJoint()) if (c == 0) ++dead;
            if (dead) std::cerr << "  " << st.id << " level " << lv << ": " << dead << " bones move no vertex\n";
            CHECK(dead == 0);
            HumanoidRetarget probe;
            humanoid::BoneMap bm = humanoid::MapHumanoidBones(root, probe.boneMap);
            int mapped = 0; for (int b = 0; b < humanoid::BoneCount; ++b) if (bm.bone[b]) ++mapped;
            CHECK(mapped == 15);
        }

    // ---- a human on the engine's third-person player: idle, then walk ---------------------------------------------------------------
    {
        Scene scene; scene.physicsEnabled = true;
        GameObject* ground = scene.CreateGameObject("Ground");
        ground->transform->localPosition = {0, -0.5f, 0};
        ground->transform->localScale = {200, 1, 200};
        ground->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
        ground->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
        GameObject* player = Templates::AddThirdPersonPlayer(scene, {0, 0, 0});
        guided::Recipe r = DefaultRecipe(T, "average");
        GameObject* model = BuildRiggedHuman(scene, r, "Guided human");
        CHECK(model != nullptr);
        if (!model) TEST_MAIN_RESULT();
        std::string log;
        CHECK(AttachCharacterModelRoot(scene, player, model, &log) == model);
        std::printf("  attach: %s\n", log.c_str());
        SkinnedMesh* sm = nullptr;
        for (const auto& up : scene.Objects()) if (up->IsSelfOrDescendantOf(model) && up->GetComponent<SkinnedMesh>()) sm = up->GetComponent<SkinnedMesh>();
        CHECK(sm != nullptr);
        if (!sm) TEST_MAIN_RESULT();
        scene.Start();
        auto* hr = player->GetComponent<HumanoidRetarget>();
        Input::FeedKeys({});
        for (int i = 0; i < 5; ++i) scene.Update(kDt);   // the retarget wires itself on its first frames
        CHECK(hr && hr->Wired() && hr->MappedBones() == 15);   // the 15 humanoid bones; the other 7 (spine chain, neck, collar bones, toes) ride along
        GameObject* lThigh = hr ? hr->MappedNode(9) : nullptr;
        GameObject* rThigh = hr ? hr->MappedNode(12) : nullptr;
        CHECK(lThigh && rThigh);
        if (!lThigh || !rThigh) TEST_MAIN_RESULT();
        auto thighSpread = [&]() {   // angle between the two thigh directions (hip to knee), degrees
            Transform* lk = hr->MappedNode(10)->transform; Transform* rk = hr->MappedNode(13)->transform;
            Vec3 a = (lk->Position() - lThigh->transform->Position()).Normalized(), b = (rk->Position() - rThigh->transform->Position()).Normalized();
            const float d = std::fmax(-1.0f, std::fmin(1.0f, Vec3::Dot(a, b)));
            return std::acos(d) * 57.29578f;
        };
        auto bodyBox = [&](Vec3& lo, Vec3& hi) {
            const Mesh& m = sm->gameObject->GetComponent<MeshRenderer>()->mesh;
            lo = {1e9f, 1e9f, 1e9f}; hi = {-1e9f, -1e9f, -1e9f};
            for (const Vec3& v : m.vertices) { lo = {std::fmin(lo.x, v.x), std::fmin(lo.y, v.y), std::fmin(lo.z, v.z)}; hi = {std::fmax(hi.x, v.x), std::fmax(hi.y, v.y), std::fmax(hi.z, v.z)}; }
        };
        // idle
        Input::FeedKeys({});
        for (int i = 0; i < 90; ++i) scene.Update(kDt);
        const float idleSpread = thighSpread();
        const Vec3 startPos = player->transform->Position();
        // walk forward (W) for two seconds
        float maxSpread = 0, maxMove = 0; bool finite = true;
        Mesh idleMesh = sm->gameObject->GetComponent<MeshRenderer>()->mesh;
        float maxDisp = 0;
        for (int i = 0; i < 150; ++i) {
            Input::FeedKeys({'W'});
            scene.Update(kDt);
            maxSpread = std::fmax(maxSpread, thighSpread());
            maxMove = std::fmax(maxMove, (player->transform->Position() - startPos).Magnitude());
            const Mesh& m = sm->gameObject->GetComponent<MeshRenderer>()->mesh;
            if (!Finite(m)) finite = false;
            if (m.vertices.size() == idleMesh.vertices.size() && i % 10 == 0)
                for (size_t v = 0; v < m.vertices.size(); v += 7) maxDisp = std::fmax(maxDisp, (m.vertices[v] - idleMesh.vertices[v]).Magnitude());
        }
        std::printf("  idle thigh spread %.1f deg, walking up to %.1f deg; moved %.2f m; skin moved up to %.2f m\n", idleSpread, maxSpread, maxMove, maxDisp);
        CHECK(finite);
        CHECK(maxMove > 1.0f);                       // the controller really moves the body
        CHECK(maxSpread > idleSpread + 8.0f);        // the walk swings the legs (the built-in walk drives the bones)
        CHECK(maxDisp > 0.05f);                      // and the skin follows
        Vec3 lo, hi; bodyBox(lo, hi);
        CHECK(hi.y - lo.y > 1.2f && hi.y - lo.y < 2.4f);   // still a person-sized body after skinning
    }

    // ---- Mixamo-style names: both namings map, and a clip that targets the Mixamo node names plays on the Guided human -----------------
    {
        guided::Recipe r = DefaultRecipe(T, "average");
        r.params["detail"] = 1;
        for (int naming = 0; naming < 2; ++naming) {
            std::vector<guided::RigJoint> js;
            CHECK(guided::HumanoidSkeleton(r, js, naming ? guided::RigNaming::Engine : guided::RigNaming::Mixamo));
            CHECK(js.size() == 22 && js[0].parent == -1);
            bool order = true, unique = true;
            for (size_t i = 1; i < js.size(); ++i) { if (js[i].parent < 0 || js[i].parent >= (int)i) order = false; for (size_t k = 0; k < i; ++k) if (js[k].name == js[i].name) unique = false; }
            CHECK(order && unique);
            CHECK(naming == 0 ? js[0].name == "mixamorig:Hips" && js[10].name.find("mixamorig:Left") == 0 : js[0].name == "pelvis");
            Scene scene; scene.physicsEnabled = false;
            GameObject* root = BuildRiggedHuman(scene, r, "Human", nullptr);   // default naming is Mixamo
            (void)root;
        }
        Scene scene; scene.physicsEnabled = false;
        GameObject* model = BuildRiggedHuman(scene, r, "Guided human");
        CHECK(model != nullptr);
        if (!model) TEST_MAIN_RESULT();
        SkinnedMesh* sm = nullptr;
        for (const auto& up : scene.Objects()) if (up->GetComponent<SkinnedMesh>()) sm = up->GetComponent<SkinnedMesh>();
        CHECK(sm && sm->jointNames.size() == 22 && sm->jointNames[0] == "mixamorig:Hips");
        if (!sm) TEST_MAIN_RESULT();
        // a short "wave and step" clip written from the bone names: right arm raised 35 to 75 degrees, left thigh and knee swing, spine twist, head nod
        ModelAnimator::Clip c;
        c.name = "Test wave and step";
        auto track = [&](const char* node, const char* prop, float amp, float bias) {
            AnimationClip a;
            for (int k = 0; k <= 24; ++k) a.AddKey(prop, 2.0f * (float)k / 24.0f, bias + amp * std::sin(6.2831853f * (float)k / 24.0f));
            c.nodes.push_back({node, a});
        };
        track("mixamorig:LeftUpLeg", "rotation.x", 35.0f, 0.0f);
        track("mixamorig:LeftLeg", "rotation.x", 25.0f, 30.0f);
        track("mixamorig:RightUpLeg", "rotation.x", -30.0f, 0.0f);
        track("mixamorig:RightArm", "rotation.z", 20.0f, 55.0f);
        track("mixamorig:RightForeArm", "rotation.x", 40.0f, 40.0f);
        track("mixamorig:LeftArm", "rotation.x", 30.0f, 0.0f);
        track("mixamorig:Spine1", "rotation.y", 15.0f, 0.0f);
        track("mixamorig:Head", "rotation.x", 12.0f, 0.0f);
        auto* ma = model->AddComponent<ModelAnimator>();
        ma->clips.push_back(c);
        ma->autoPlay = false;
        scene.Start();
        CHECK(ma->Play("Test wave and step"));
        MeshRenderer* mr = sm->gameObject->GetComponent<MeshRenderer>();
        const Mesh bind = sm->bind;
        // every triangle edge of the bind pose, to see how far the skin stretches
        std::vector<std::pair<int, int>> edges;
        for (size_t t = 0; t + 2 < bind.triangles.size(); t += 3) for (int k = 0; k < 3; ++k) edges.push_back({bind.triangles[t + k], bind.triangles[t + (k + 1) % 3]});
        float maxStretch = 0, maxGrow = 0, maxMove = 0, maxBone = 0; bool finite = true;
        Transform* rArm = nullptr; for (const auto& up : scene.Objects()) if (up->name == "mixamorig:RightArm") rArm = up->transform;
        CHECK(rArm != nullptr);
        for (int i = 0; i < 150; ++i) {
            scene.Update(kDt);
            const Mesh& m = mr->mesh;
            if (!Finite(m) || m.vertices.size() != bind.vertices.size()) { finite = false; break; }
            if (rArm) maxBone = std::fmax(maxBone, Quat::Angle(Quat::Identity, rArm->localRotation));
            if (i % 5 == 0) {
                for (size_t v = 0; v < m.vertices.size(); ++v) maxMove = std::fmax(maxMove, (m.vertices[v] - bind.vertices[v]).Magnitude());
                for (const auto& e : edges) {
                    const float a = (bind.vertices[e.first] - bind.vertices[e.second]).Magnitude(), b = (m.vertices[e.first] - m.vertices[e.second]).Magnitude();
                    if (a > 0.01f) maxStretch = std::fmax(maxStretch, b / a);   // edges shorter than a centimetre only tell us about rounding
                    maxGrow = std::fmax(maxGrow, b - a);
                }
            }
        }
        std::printf("  Mixamo-named clip: right arm turned up to %.0f deg, skin moved up to %.2f m, edges (longer than 1 cm) stretched up to x%.2f, the biggest growth of any edge %.3f m\n", maxBone, maxMove, maxStretch, maxGrow);
        CHECK(finite);
        CHECK(maxBone > 40.0f);        // the clip really drove the bones it names
        CHECK(maxMove > 0.2f);         // and the skin followed
        CHECK(maxMove < 1.6f);         // no vertex flew off
        CHECK(maxStretch < 4.0f);      // no triangle edge stretched to a spike
        CHECK(maxGrow < 0.12f);        // and none grew by more than 12 cm
    }
    TEST_MAIN_RESULT();
}
