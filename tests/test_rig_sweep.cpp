#include "test_framework.hpp"
#include "rig_harness.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include <cmath>
#include <cstdio>

using namespace okay;
using righarness::Rig;

// Every rig in tests/assets/rigs goes through the same motions as a playable
// character: idle, walk, run, jump, crouch and turning. Whatever the rig's
// naming or axes, its skeleton must stay finite, must not spike from one frame
// to the next, and (for humanoids) must stand on the ground and swing its legs.

namespace {

bool IsFin(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

bool AllFinite(const Rig& r) {
    for (Transform* t : r.joints) if (!IsFin(t->Position())) return false;
    return true;
}

/// Run `frames` frames with `keys`; return the largest one-frame move of any
/// joint measured in the player's own frame (so travelling doesn't count).
float WorstJump(Rig& r, float dt, const std::vector<char>& keys, int frames) {
    float worst = 0.0f;
    std::vector<Vec3> prev;
    for (int f = 0; f < frames; ++f) {
        r.Step(dt, keys);
        Mat4 inv = r.player->transform->LocalToWorldMatrix().Inverse();
        std::vector<Vec3> cur;
        cur.reserve(r.joints.size());
        for (Transform* t : r.joints) cur.push_back(inv.MultiplyPoint(t->Position()));
        if (prev.size() == cur.size())
            for (size_t i = 0; i < cur.size(); ++i) worst = std::fmax(worst, (cur[i] - prev[i]).Magnitude());
        prev = std::move(cur);
    }
    return worst;
}

} // namespace

int main() {
    RUN_SUITE("rig_sweep");
    const float dt = 1.0f / 60.0f;
    struct Case { const char* file; bool humanoid; };
    const Case rigs[] = {
        {"mixamo_t_noclips.glb", true},  {"mixamo_t_cm_clips.glb", true}, {"mixamo_t_stance_clips.glb", true},
        {"mixamo_a_blender_short.glb", true}, {"ue_a_blender.glb", true}, {"daz_t.glb", true},
        {"vroid_t.glb", true}, {"cc_t.glb", true}, {"fox.glb", false}, {"nonsense_t.glb", false},
    };
    for (const Case& c : rigs) {
        std::printf("-- %s\n", c.file);
        Rig r = righarness::PlayableFromFile(c.file);
        CHECK(r.model != nullptr);
        if (!r) continue;
        const bool human = c.humanoid && r.Bone(9) && r.Bone(10) && r.Bone(12) && r.Bone(13);
        CHECK(human == c.humanoid);
        CHECK(!r.joints.empty());

        // Idle: settled, finite, standing on the floor.
        r.Step(dt, {}, 90);
        CHECK(AllFinite(r));
        float standTop = r.TopY();
        std::printf("  idle: feet %.3f top %.3f\n", r.FeetY(), standTop);
        if (human) { CHECK(std::fabs(r.FeetY()) < 0.15f); CHECK(standTop > 0.5f); }

        // Walk: legs swing, nothing spikes.
        float swing = 0.0f, worst = 0.0f;
        for (int i = 0; i < 4; ++i) {
            worst = std::fmax(worst, WorstJump(r, dt, {'W'}, 30));
            swing = std::fmax(swing, r.ThighSwingDeg());
        }
        CHECK(AllFinite(r));
        std::printf("  walk: thigh swing %.1f deg, worst joint step %.3f m\n", swing, worst);
        CHECK(worst < 0.5f);
        if (human) CHECK(swing > 5.0f);

        // Run.
        worst = WorstJump(r, dt, {'W', Input::KeyShift}, 120);
        CHECK(AllFinite(r));
        std::printf("  run: worst joint step %.3f m\n", worst);
        CHECK(worst < 0.5f);

        // Turn about while running.
        worst = WorstJump(r, dt, {'D', Input::KeyShift}, 60);
        worst = std::fmax(worst, WorstJump(r, dt, {'S', Input::KeyShift}, 60));
        CHECK(AllFinite(r));
        std::printf("  turn: worst joint step %.3f m\n", worst);
        CHECK(worst < 0.5f);

        // Jump: leaves the ground and comes back to it.
        r.Step(dt, {}, 60);
        float peak = 0.0f;
        for (int i = 0; i < 90; ++i) { r.Step(dt, i == 0 ? std::vector<char>{' '} : std::vector<char>{}); peak = std::fmax(peak, r.FeetY()); }
        r.Step(dt, {}, 120);
        CHECK(AllFinite(r));
        std::printf("  jump: feet peak %.3f, back at %.3f\n", peak, r.FeetY());
        if (human) { CHECK(peak > 0.1f); CHECK(std::fabs(r.FeetY()) < 0.15f); }

        // Crouch (a toggle: one press down, one press up) lowers the head and
        // standing back up restores it.
        r.Step(dt, {}, 30);
        float before = r.TopY();
        r.Step(dt, {Input::KeyCtrl}, 1);
        r.Step(dt, {}, 90);
        float crouched = r.TopY();
        r.Step(dt, {Input::KeyCtrl}, 1);
        r.Step(dt, {}, 90);
        float after = r.TopY();

        CHECK(AllFinite(r));
        std::printf("  crouch: top %.3f -> %.3f -> %.3f\n", before, crouched, after);
        if (human) { CHECK(crouched < before - 0.1f); CHECK(std::fabs(after - before) < 0.1f); }
    }
    TEST_MAIN_RESULT();
}
