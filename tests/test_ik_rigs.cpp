#include "test_framework.hpp"
#include "rig_harness.hpp"
#include "okay/Components/LookAtIK.hpp"
#include <cmath>

using namespace okay;

static float Deg(const Vec3& a, const Vec3& b) {
    float d = Vec3::Dot(a.Normalized(), b.Normalized()); d = d > 1 ? 1 : (d < -1 ? -1 : d);
    return std::acos(d) * 57.29578f;
}

// Look-At IK on imported rigs. The two test rigs disagree on which local axis
// of the head bone points at the face: one points its +Z at the front, the other
// out the back of the skull. RestForwardAxis reads the right one off the rest
// pose (what the editor's Auto-Detect Chain stores), and with it the head turns
// its face toward the target on both.
int main() {
    RUN_SUITE("ik_rigs");

    struct Case { const char* file; float wantZ; };
    for (const Case& c : {Case{"mixamo_t_noclips.glb", 1.0f}, Case{"ue_a_blender.glb", -1.0f}}) {
        righarness::Rig r = righarness::PlayableFromFile(c.file);
        CHECK(r.model != nullptr);
        if (!r) continue;
        r.Step(1.0f / 60.0f, {}, 5);
        Transform *spine = r.Bone(1), *head = r.Bone(2);
        CHECK(spine && head);
        if (!spine || !head) continue;
        const Vec3 facing = (r.player->transform->Rotation() * Vec3{0, 0, -1}).Normalized();
        const Vec3 axis = LookAtIK::RestForwardAxis(head, facing);
        CHECK(std::fabs(axis.z - c.wantZ) < 0.05f && std::fabs(axis.x) < 0.05f && std::fabs(axis.y) < 0.05f);
        // With that axis, the head's face ends up on the target: 50 degrees to
        // the right of the facing, at head height, 5 m out.
        const Vec3 right = Vec3::Cross(Vec3::Up, facing).Normalized();
        const Vec3 dir = (facing * std::cos(50.0f * 0.0174533f) + right * std::sin(50.0f * 0.0174533f)).Normalized();
        auto* ik = r.player->AddComponent<LookAtIK>();
        ik->chain = {spine, head};
        ik->forwardAxis = axis; ik->smoothing = 0.0f; ik->clampAngle = 120.0f; ik->maxAngle = 90.0f;
        ik->target = head->Position() + dir * 5.0f;
        r.Step(1.0f / 60.0f, {}, 10);
        const Vec3 face = (head->Rotation() * axis).Normalized();
        const Vec3 want = (ik->target - head->Position()).Normalized();
        CHECK(Deg(face, want) < 8.0f);
        // Relative to the body it turned about 50 degrees, not 130 the other way.
        CHECK(std::fabs(Deg(face, facing) - 50.0f) < 10.0f);
        CHECK(Vec3::Dot(face, right) > 0.5f);
    }

    TEST_MAIN_RESULT();
}
