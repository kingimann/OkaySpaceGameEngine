#pragma once
// Shared helper for the imported-humanoid tests: load a rig from
// tests/assets/rigs/ onto a third-person player standing on a ground box, step
// the scene with keys held, and measure the skeleton in world space.
#include <Okay.hpp>
#include "okay/Render/ModelImport.hpp"
#include "okay/Components/HumanoidRetarget.hpp"
#include "okay/Components/SkinnedMesh.hpp"
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace righarness {

using namespace okay;

inline std::string RigPath(const std::string& file) {
    return std::string(OKAY_SOURCE_DIR) + "/tests/assets/rigs/" + file;
}

struct Rig {
    std::unique_ptr<Scene> scene;
    GameObject* player = nullptr;
    GameObject* model = nullptr;
    std::vector<Transform*> joints;   ///< every skinned joint of the model

    explicit operator bool() const { return model != nullptr; }

    /// Run `frames` frames of `dt` with `keys` held.
    void Step(float dt, const std::vector<char>& keys, int frames = 1) {
        for (int i = 0; i < frames; ++i) { Input::FeedKeys(keys); scene->Update(dt); }
    }
    /// Lowest skeleton joint (toes / soles), metres above the ground (y = 0).
    float FeetY() const {
        float y = 1e9f;
        for (Transform* t : joints) y = std::fmin(y, t->Position().y);
        return y;
    }
    /// Highest skeleton joint (top of the head).
    float TopY() const {
        float y = -1e9f;
        for (Transform* t : joints) y = std::fmax(y, t->Position().y);
        return y;
    }
    /// The joint whose name stands for humanoid bone `b` (HumanoidRetarget order).
    Transform* Bone(int b) const {
        for (Transform* t : joints) {
            int sc = 0;
            if (t->gameObject && HumanoidRetarget::ClassifyBone(t->gameObject->name, sc) == b && sc == 2) return t;
        }
        return nullptr;
    }
    /// Angle between the two thighs (hip -> knee), degrees.
    float ThighSwingDeg() const {
        Transform *lh = Bone(9), *lk = Bone(10), *rh = Bone(12), *rk = Bone(13);
        if (!lh || !lk || !rh || !rk) return 0.0f;
        Vec3 a = (lk->Position() - lh->Position()).Normalized();
        Vec3 b = (rk->Position() - rh->Position()).Normalized();
        float d = Vec3::Dot(a, b);
        return std::acos(d > 1.0f ? 1.0f : (d < -1.0f ? -1.0f : d)) * 57.29578f;
    }
};

/// New scene, a ground box with its top at y = 0, and the rig at `file` made the
/// playable character of a third-person player standing on it.
inline Rig PlayableFromFile(const std::string& file) {
    Rig r;
    r.scene = std::make_unique<Scene>("Rig");
    Scene& s = *r.scene;
    GameObject* ground = s.CreateGameObject("Ground");
    ground->transform->localPosition = {0, -0.5f, 0};
    ground->transform->localScale = {200, 1, 200};
    ground->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
    ground->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
    r.player = Templates::AddThirdPersonPlayer(s, {0, 0, 0});
    r.model = AttachCharacterModel(s, r.player, RigPath(file));
    if (!r.model) return r;
    for (const auto& up : s.Objects())
        if (up->IsSelfOrDescendantOf(r.model))
            if (auto* sm = up->GetComponent<SkinnedMesh>()) {
                sm->ResolveJoints();
                for (Transform* t : sm->joints) if (t) r.joints.push_back(t);
            }
    s.Start();
    return r;
}

} // namespace righarness
