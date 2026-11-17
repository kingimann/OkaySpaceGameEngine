// Guided human + animation: a generated human is given a skeleton and skin weights, handed to the engine's own player setup
// (Character + HumanoidRetarget, the same path an imported humanoid takes) and played idle and walking.
// What this proves, and no more: the skeleton maps to the 15 humanoid bones, every vertex is skinned, the built-in idle and
// walk poses move the bones, the skin follows them, and nothing goes bad (no NaN, the body travels with the controller).
// It does NOT judge how good the deformation looks.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/HumanoidRetarget.hpp"
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
            CHECK(sm && sm->joints.size() == 15 && sm->MissingJoints() == 0);
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
        CHECK(hr && hr->Wired() && hr->MappedBones() == 15);
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
    TEST_MAIN_RESULT();
}
