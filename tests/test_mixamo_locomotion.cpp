#include "test_framework.hpp"
#include "rig_harness.hpp"
#include "okay/Components/FootIK.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Components/FirstPersonController.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace okay;

// A downloaded Mixamo character (one animation per file, usually no idle): its
// walk and run must step at the speed the body moves, blend with matching foot
// phase, and every slot the files don't fill falls back to the built-in animation.
namespace {

const float kDt = 1.0f / 60.0f;

// A leg-swing clip: the left thigh swings +-amp degrees over `len` seconds, looping.
// With `travel` > 0 the hips also travel that many local units per second.
ModelAnimator::Clip SwingClip(const std::string& name, float len, const std::string& hips,
                              const std::string& thigh, float amp, float travel) {
    ModelAnimator::Clip c;
    c.name = name;
    AnimationClip leg;
    for (int k = 0; k <= 48; ++k) {
        const float t = len * (float)k / 48.0f;
        leg.AddKey("rotation.x", t, amp * std::sin(6.2831853f * t / len));
    }
    c.nodes.push_back({thigh, leg});
    if (travel > 0.0f) {
        AnimationClip h;
        h.AddKey("position.z", 0.0f, 0.0f);
        h.AddKey("position.z", len, travel * len);
        c.nodes.push_back({hips, h});
    }
    return c;
}

struct Loco {
    righarness::Rig rig;
    ModelAnimator* ma = nullptr;
    HumanoidRetarget* hr = nullptr;
    Transform* thigh = nullptr;
    Vec3 pos{0, 0, 0};

    // A Mixamo T-pose skeleton on a player; the controller is removed so the test
    // moves the body itself at an exact speed.
    explicit Loco(bool keepController = false) {
        rig = righarness::PlayableFromFile("mixamo_t_noclips.glb");
        if (!rig) return;
        hr = rig.player->GetComponent<HumanoidRetarget>();
        thigh = rig.Bone(9);
        if (auto* f = rig.player->GetComponent<FootIK>()) f->weight = 0.0f;
        if (!keepController) {
            rig.player->RemoveComponent<FirstPersonController>();   // the test moves and turns the body itself
            rig.player->RemoveComponent<Rigidbody3D>();   // nothing holds the body's heading
            rig.scene->physicsEnabled = false;
        }
        ma = rig.model->GetComponent<ModelAnimator>();
        if (!ma)
            for (const auto& up : rig.scene->Objects())
                if (up->IsSelfOrDescendantOf(rig.model) && (ma = up->GetComponent<ModelAnimator>())) break;
        if (!ma) ma = rig.model->AddComponent<ModelAnimator>();
        pos = rig.player->transform->localPosition;
    }
    std::string HipsName() const { return rig.Bone(0)->gameObject->name; }
    std::string ThighName() const { return thigh->gameObject->name; }
    void AddClip(const std::string& name, float len, float amp, float travel = 0.0f) {
        ma->clips.push_back(SwingClip(name, len, HipsName(), ThighName(), amp, travel));
    }
    // Move forward (-Z) at `speed` m/s for `frames` frames; optional per-frame probe.
    template <class F> void Move(float speed, int frames, F probe) {
        for (int i = 0; i < frames; ++i) {
            pos.z -= speed * kDt;
            rig.player->transform->localPosition = pos;
            rig.scene->Update(kDt);
            probe();
        }
    }
    void Move(float speed, int frames) { Move(speed, frames, [] {}); }
    // The left thigh's swing (degrees) from the standing pose.
    float Swing() const { return thigh ? Quat::Angle(Quat::Identity, thigh->localRotation) : 0.0f; }
};

float DegBetween(const Quat& a, const Quat& b) { return Quat::Angle(a, b); }

} // namespace

int main() {
    RUN_SUITE("mixamo_locomotion");

    // ---- A walk-only download (no idle): the idle, run, jump... slots fall back to
    // the built-in animation; the walk stays the file's clip.
    {
        Loco L;
        CHECK(L.rig && L.ma && L.hr);
        if (!L.rig || !L.ma || !L.hr) TEST_MAIN_RESULT();
        CharacterModelInfo none;
        CHECK(DescribeCharacterModel(*L.rig.scene, L.rig.player, none));
        CHECK(none.source == 2 && none.importedSlots == 0 && none.builtinSlots == ModelAnimator::SlotCount);
        CHECK(none.mappedBones >= 12);

        L.AddClip("Walking", 1.0f, 30.0f);
        const std::string log = FixCharacterAnimations(*L.rig.scene, L.rig.player);
        std::printf("  fix: %s\n", log.c_str());
        CharacterModelInfo info;
        CHECK(DescribeCharacterModel(*L.rig.scene, L.rig.player, info));
        CHECK(info.source == 3);
        CHECK(info.importedSlots == 1 && info.builtinSlots == ModelAnimator::SlotCount - 1);
        CHECK(info.slots[ModelAnimator::SlotWalk].source == 1 && info.slots[ModelAnimator::SlotWalk].clip == "Walking");
        for (int s : {ModelAnimator::SlotIdle, ModelAnimator::SlotRun, ModelAnimator::SlotJump, ModelAnimator::SlotFall,
                      ModelAnimator::SlotLand, ModelAnimator::SlotCrouch, ModelAnimator::SlotCrouchWalk,
                      ModelAnimator::SlotProne, ModelAnimator::SlotCrawl, ModelAnimator::SlotGetUp})
            CHECK(info.slots[s].source == 2);
        CHECK(L.ma->driveByMovement && L.ma->smoothLocomotion && L.ma->inPlace);

        // Standing still: the built-in idle poses the skeleton (the retarget is not yielding).
        L.rig.scene->Update(kDt);
        for (int i = 0; i < 20; ++i) L.rig.scene->Update(kDt);
        CHECK(L.ma->BuiltinOwnsPose() && L.ma->ActiveSlot() == ModelAnimator::SlotIdle);
        CHECK(!L.ma->ClipOverridesPose());
        CHECK(L.hr->MappedBones() >= 12);

        // Walking: the file's clip, stepping at the pace of the body.
        L.Move(1.4f, 60);
        CHECK(!L.ma->BuiltinOwnsPose() && L.ma->ActiveSlot() == ModelAnimator::SlotWalk);
        CHECK(L.ma->ClipOverridesPose());
        CHECK_NEAR(L.ma->CurrentStrideRate(), 1.0f, 0.08f);          // made for 1.4 m/s
        L.Move(2.0f, 60);
        CHECK(L.ma->ActiveSlot() == ModelAnimator::SlotWalk);
        CHECK_NEAR(L.ma->CurrentStrideRate(), 2.0f / 1.4f, 0.1f);    // faster body, faster steps
        L.Move(2.3f, 60);
        CHECK_NEAR(L.ma->CurrentStrideRate(), ModelAnimator::kStrideMaxRate, 1e-3f);   // clamped
        L.Move(0.7f, 60);
        CHECK_NEAR(L.ma->CurrentStrideRate(), ModelAnimator::kStrideMinRate, 1e-3f);
        // Fast enough that the walk would turn into a jog: the (built-in) run takes it.
        L.Move(4.0f, 60);
        CHECK(L.ma->BuiltinOwnsPose() && L.ma->ActiveSlot() == ModelAnimator::SlotRun);
        // And stopping goes back to the built-in idle.
        L.Move(0.0f, 60);
        CHECK(L.ma->BuiltinOwnsPose() && L.ma->ActiveSlot() == ModelAnimator::SlotIdle);
    }

    // ---- The playback rate follows the clip's own travel when it has one.
    {
        Loco L;
        CHECK(L.rig && L.ma);
        if (L.rig && L.ma) {
            L.AddClip("Walking", 1.0f, 30.0f, 1.0f);   // 1 local unit per second of root travel
            L.AddClip("Running", 0.7f, 45.0f, 1.0f);
            FixCharacterAnimations(*L.rig.scene, L.rig.player);
            const int walk = L.ma->FindClip("Walking");
            const float measured = L.ma->ClipRootSpeed(walk);
            std::printf("  measured walk travel %.3f m/s (nominal %.1f)\n", measured, L.ma->walkClipSpeed);
            CHECK(measured > 0.0f);
            CHECK_NEAR(L.ma->SlotClipSpeed(ModelAnimator::SlotWalk), measured, 1e-4f);
            // An in-place clip uses the slot's nominal speed instead.
            L.AddClip("Crawling", 1.0f, 20.0f);
            L.ma->crawlClip = "Crawling";
            CHECK_NEAR(L.ma->SlotClipSpeed(ModelAnimator::SlotCrawl), L.ma->crawlClipSpeed, 1e-6f);
            // ...and the nominal speed can be overridden per slot.
            L.ma->crawlClipSpeed = 1.0f;
            CHECK_NEAR(L.ma->StrideRate(ModelAnimator::SlotCrawl, 0.8f), 0.8f, 1e-4f);
            // Stride matching off: clips play at their authored rate.
            L.ma->strideMatch = false;
            L.Move(2.0f, 30);
            L.Move(2.0f, 30);
            CHECK(L.ma->ActiveSlot() != ModelAnimator::SlotIdle);
        }
    }

    // ---- Walk -> run (both imported): one normalized phase through the blend, no foot pop.
    {
        Loco L;
        CHECK(L.rig && L.ma);
        if (L.rig && L.ma) {
            L.AddClip("Idle", 4.0f, 2.0f);
            L.AddClip("Walking", 1.0f, 30.0f);
            L.AddClip("Running", 0.7f, 45.0f);
            FixCharacterAnimations(*L.rig.scene, L.rig.player);
            CharacterModelInfo info;
            DescribeCharacterModel(*L.rig.scene, L.rig.player, info);
            CHECK(info.slots[ModelAnimator::SlotIdle].source == 1 && info.slots[ModelAnimator::SlotRun].source == 1);
            CHECK(info.slots[ModelAnimator::SlotJump].source == 2);   // the file has no jump: built-in
            L.Move(0.0f, 30);
            CHECK(!L.ma->BuiltinOwnsPose() && L.ma->ActiveSlot() == ModelAnimator::SlotIdle);

            // Inside the blend zone: the sampled swing is the weighted mix of the two
            // clips at the SAME normalized phase.
            L.Move(2.5f, 90);
            const float w = (2.5f - L.ma->WalkEndSpeed()) / (L.ma->RunStartSpeed() - L.ma->WalkEndSpeed());
            CHECK(w > 0.2f && w < 0.8f);
            float worstPhase = 0.0f;
            for (int i = 0; i < 40; ++i) {
                L.Move(2.5f, 1);
                const float p = L.ma->BlendPhase();
                const float want = std::fabs((1.0f - w) * 30.0f * std::sin(6.2831853f * p) + w * 45.0f * std::sin(6.2831853f * p));
                worstPhase = std::fmax(worstPhase, std::fabs(L.Swing() - want));
            }
            std::printf("  blend zone: sampled swing is within %.2f deg of the phase-matched mix\n", worstPhase);
            CHECK(worstPhase < 3.0f);

            // Ramp 0 -> 5 m/s over 5 s: the phase and the leg never jump.
            float worstSwing = 0.0f, worstPhaseStep = 0.0f;
            float prevSwing = L.Swing(), prevPhase = L.ma->BlendPhase();
            for (int i = 0; i < 300; ++i) {
                L.Move(5.0f * (float)i / 300.0f, 1);
                worstSwing = std::fmax(worstSwing, std::fabs(L.Swing() - prevSwing));
                float dp = L.ma->BlendPhase() - prevPhase;
                if (dp < -0.5f) dp += 1.0f;   // wrapped
                worstPhaseStep = std::fmax(worstPhaseStep, std::fabs(dp));
                prevSwing = L.Swing(); prevPhase = L.ma->BlendPhase();
            }
            std::printf("  ramp 0..5 m/s: leg moves at most %.2f deg a frame, phase %.3f a frame\n", worstSwing, worstPhaseStep);
            CHECK(worstSwing < 12.0f);
            CHECK(worstPhaseStep < 0.06f);
            CHECK(L.ma->ActiveSlot() == ModelAnimator::SlotRun);
            CHECK_NEAR(L.ma->CurrentStrideRate(), 5.0f / 4.0f, 0.1f);

            // Switching walk <-> run in discrete mode keeps the feet in step too.
            L.ma->smoothLocomotion = false;
            L.Move(1.4f, 120);
            CHECK(L.ma->ActiveSlot() == ModelAnimator::SlotWalk);
            const int walkIdx = L.ma->FindClip("Walking"), runIdx = L.ma->FindClip("Running");
            float phaseBefore = 0.0f, phaseAfter = -1.0f;
            for (int i = 0; i < 400 && phaseAfter < 0.0f; ++i) {
                phaseBefore = L.ma->Time() / L.ma->ClipLength(walkIdx);
                L.Move(i < 5 ? 1.4f : 4.0f, 1);
                if (L.ma->ActiveSlot() == ModelAnimator::SlotRun) phaseAfter = L.ma->Time() / L.ma->ClipLength(runIdx);
            }
            std::printf("  discrete walk->run: phase %.3f -> %.3f\n", phaseBefore, phaseAfter);
            CHECK(phaseAfter >= 0.0f);
            float d = std::fabs(phaseAfter - phaseBefore);
            d = std::fmin(d, 1.0f - d);
            CHECK(d < 0.1f);
        }
    }

    // ---- Built-in <-> clip hand-over: no pop in the legs.
    {
        Loco L;
        CHECK(L.rig && L.ma);
        if (L.rig && L.ma) {
            L.AddClip("Walking", 1.0f, 30.0f);
            FixCharacterAnimations(*L.rig.scene, L.rig.player);
            L.Move(0.0f, 60);
            Transform* knee = L.rig.Bone(10);
            Mat4 toBody = L.rig.player->transform->LocalToWorldMatrix().Inverse();
            Vec3 prev = toBody.MultiplyPoint(knee->Position());
            float worst = 0.0f;
            for (float speed : {1.4f, 0.0f, 1.4f, 4.0f, 1.4f, 0.0f})   // idle, walk, idle, run, walk, idle
                for (int i = 0; i < 60; ++i) {
                    L.Move(speed, 1);
                    toBody = L.rig.player->transform->LocalToWorldMatrix().Inverse();
                    Vec3 k = toBody.MultiplyPoint(knee->Position());
                    float dd = (k - prev).Magnitude();
                    worst = std::fmax(worst, dd);
                    prev = k;
                }
            std::printf("  hand-overs: knee moves at most %.3f m a frame\n", worst);
            CHECK(worst < 0.1f);
        }
    }

    // ---- Built-in start / stop: the body leans ahead into a start and back into a stop,
    // eased, and the lean adds no pop to the pose (compared with the same run without it).
    {
        struct Run { float startLean = 0, stopLean = 0, maxPitch = 0, maxRoll = 0, spine = 0, legs = 0, rest = 0; };
        auto run = [&](float lean) {
            Run r;
            Loco L(/*keepController=*/true);
            if (!L.rig || !L.hr) { CHECK(false); return r; }
            L.hr->leanScale = lean;
            L.rig.Step(kDt, {}, 60);
            auto sample = [&](std::initializer_list<int> bones) {
                std::vector<Quat> q;
                for (int b : bones) if (Transform* t = L.rig.Bone(b)) q.push_back(t->localRotation);
                return q;
            };
            auto delta = [](const std::vector<Quat>& a, const std::vector<Quat>& b) {
                float w = 0.0f;
                for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) w = std::fmax(w, DegBetween(a[i], b[i]));
                return w;
            };
            std::vector<Quat> pSpine = sample({0, 1}), pLegs = sample({9, 10, 12, 13});
            auto track = [&] {
                std::vector<Quat> cs = sample({0, 1}), cl = sample({9, 10, 12, 13});
                r.spine = std::fmax(r.spine, delta(cs, pSpine));
                r.legs = std::fmax(r.legs, delta(cl, pLegs));
                pSpine = cs; pLegs = cl;
                r.maxPitch = std::fmax(r.maxPitch, std::fabs(L.hr->LeanPitch()));
                r.maxRoll = std::fmax(r.maxRoll, std::fabs(L.hr->LeanRoll()));
            };
            for (int i = 0; i < 90; ++i) {
                L.rig.Step(kDt, {'W'});
                track();
                if (i < 20) r.startLean = std::fmax(r.startLean, L.hr->LeanPitch());
            }
            for (int i = 0; i < 90; ++i) {
                L.rig.Step(kDt, {});
                track();
                r.stopLean = std::fmin(r.stopLean, L.hr->LeanPitch());
            }
            L.rig.Step(kDt, {}, 60);
            r.rest = std::fabs(L.hr->LeanPitch());
            return r;
        };
        const Run on = run(1.0f), off = run(0.0f);
        std::printf("  start/stop: lean pitch +%.2f / %.2f deg, roll up to %.2f; hips+spine move at most %.2f deg a frame "
                    "(%.2f without lean), legs %.2f (%.2f without)\n",
                    on.startLean, on.stopLean, on.maxRoll, on.spine, off.spine, on.legs, off.legs);
        CHECK(on.startLean > 0.5f);                          // tips ahead as it sets off
        CHECK(on.stopLean < -0.5f);                          // tips back as it stops
        CHECK(on.maxPitch <= 5.01f && on.maxRoll <= 6.01f);  // a few degrees, never a stagger
        CHECK(on.spine <= off.spine + 1.5f);                 // the lean itself adds no pop
        CHECK(on.legs <= off.legs + 1.5f);
        CHECK(on.rest < 0.3f);                               // upright again at rest
        CHECK(off.startLean == 0.0f && off.maxPitch == 0.0f);   // off means off
    }

    // ---- Lean direction: a forward tip puts the head ahead of the hips.
    {
        float ahead[2] = {0, 0};
        for (int on = 0; on < 2; ++on) {
            Loco L(true);
            if (!L.rig || !L.hr) { CHECK(false); continue; }
            L.hr->leanScale = on ? 1.0f : 0.0f;
            L.rig.Step(kDt, {}, 30);
            Transform* head = L.rig.Bone(2);
            Transform* hips = L.rig.Bone(0);
            float best = 0.0f;
            for (int i = 0; i < 25; ++i) {
                L.rig.Step(kDt, {'W'});
                if (L.hr->LeanPitch() > best) {
                    best = L.hr->LeanPitch();
                    Vec3 f = L.rig.player->transform->Rotation() * Vec3{0, 0, -1};
                    ahead[on] = Vec3::Dot(head->Position() - hips->Position(), f);
                }
            }
        }
        std::printf("  head ahead of hips: %.4f without lean, %.4f with\n", ahead[0], ahead[1]);
        CHECK(ahead[1] > ahead[0]);
    }

    // ---- Lean into a turn: walking a curve rolls the body toward the centre of it.
    {
        float lateral[2] = {0, 0}, roll[2] = {0, 0};
        for (int on = 0; on < 2; ++on) {
            Loco L;
            if (!L.rig || !L.hr) { CHECK(false); continue; }
            L.hr->leanScale = on ? 1.0f : 0.0f;
            Transform* head = L.rig.Bone(2);
            Transform* hips = L.rig.Bone(0);
            float yaw = 0.0f, side = 0.0f;
            for (int i = 0; i < 150; ++i) {
                const Quat q = Quat::AngleAxis(yaw, Vec3::Up);
                const Vec3 f = q * Vec3{0, 0, -1};
                L.pos = L.pos + f * (3.0f * kDt);
                L.rig.player->transform->localPosition = L.pos;
                yaw += 60.0f * kDt;   // 60 degrees a second
                L.rig.player->transform->SetRotation(Quat::AngleAxis(yaw, Vec3::Up));
                L.rig.scene->Update(kDt);
                if (i == 100) {   // the side the curve bends toward, relative to the body's right
                    const Vec3 f2 = Quat::AngleAxis(yaw + 1.0f, Vec3::Up) * Vec3{0, 0, -1};
                    side = Vec3::Dot(f2 - f, L.rig.player->transform->Rotation() * Vec3{1, 0, 0});
                    roll[on] = L.hr->LeanRoll();
                    lateral[on] = Vec3::Dot(head->Position() - hips->Position(), L.rig.player->transform->Rotation() * Vec3{1, 0, 0});
                }
            }
            if (on) CHECK(roll[on] * side > 0.0f && std::fabs(roll[on]) > 1.0f);   // toward the inside of the curve
            if (on) lateral[on] *= side > 0.0f ? 1.0f : -1.0f;
            else    lateral[on] *= side > 0.0f ? 1.0f : -1.0f;
        }
        std::printf("  turning: roll %.2f deg; head sideways of hips %.4f without lean, %.4f with (positive = inside of the curve)\n",
                    roll[1], lateral[0], lateral[1]);
        CHECK(lateral[1] > lateral[0]);
    }

    // ---- Saving: new fields round-trip; a scene saved before them loads with the defaults.
    {
        Loco L;
        CHECK(L.rig && L.ma);
        if (L.rig && L.ma) {
            L.AddClip("Walking", 1.0f, 30.0f);
            FixCharacterAnimations(*L.rig.scene, L.rig.player);
            const std::string base = SceneSerializer::Serialize(*L.rig.scene);
            CHECK(base.find("modelanimstride") == std::string::npos);   // defaults are not written

            auto reload = [&](const std::string& text, Scene& dst) {
                CHECK(SceneSerializer::Deserialize(dst, text));
                dst.Start();
                dst.Update(kDt);
            };
            auto animatorOf = [](Scene& sc) -> ModelAnimator* {
                for (const auto& up : sc.Objects()) if (auto* m = up->GetComponent<ModelAnimator>()) return m;
                return nullptr;
            };

            Scene old("old");
            reload(base, old);
            ModelAnimator* oa = animatorOf(old);
            CHECK(oa != nullptr);
            if (oa) {
                CHECK(oa->strideMatch && oa->builtinFallback);
                CHECK_NEAR(oa->walkClipSpeed, 1.4f, 1e-6f);
                CHECK_NEAR(oa->runClipSpeed, 4.0f, 1e-6f);
                CHECK(oa->walkClip == "Walking" && oa->driveByMovement);
            }
            HumanoidRetarget* ohr = nullptr;
            for (const auto& up : old.Objects()) if (auto* h = up->GetComponent<HumanoidRetarget>()) ohr = h;
            CHECK(ohr && ohr->leanScale == 1.0f);
            if (oa) {   // and it still sorts its slots out after the load
                CharacterModelInfo info;
                GameObject* player = ohr->gameObject;
                CHECK(DescribeCharacterModel(old, player, info));
                CHECK(info.source == 3 && info.slots[ModelAnimator::SlotWalk].source == 1);
            }

            L.ma->strideMatch = false;
            L.ma->walkClipSpeed = 2.0f;
            L.ma->crawlClipSpeed = 0.7f;
            L.ma->builtinFallback = false;
            L.hr->leanScale = 0.5f;
            Scene back("back");
            reload(SceneSerializer::Serialize(*L.rig.scene), back);
            ModelAnimator* ba = animatorOf(back);
            CHECK(ba && !ba->strideMatch && !ba->builtinFallback);
            if (ba) {
                CHECK_NEAR(ba->walkClipSpeed, 2.0f, 1e-6f);
                CHECK_NEAR(ba->crawlClipSpeed, 0.7f, 1e-6f);
                CHECK_NEAR(ba->runClipSpeed, 4.0f, 1e-6f);
            }
            HumanoidRetarget* bhr = nullptr;
            for (const auto& up : back.Objects()) if (auto* h = up->GetComponent<HumanoidRetarget>()) bhr = h;
            CHECK(bhr && std::fabs(bhr->leanScale - 0.5f) < 1e-6f);
        }
    }

    // ---- Fix animations clears a slot that names a missing clip, and a file with
    // every slot filled needs no built-in animation.
    {
        Loco L;
        CHECK(L.rig && L.ma);
        if (L.rig && L.ma) {
            L.AddClip("Walking", 1.0f, 30.0f);
            L.ma->runClip = "Sprint That Was Deleted";
            const std::string log = FixCharacterAnimations(*L.rig.scene, L.rig.player);
            CHECK(L.ma->runClip.empty());
            CHECK(log.find("cleared") != std::string::npos);
            for (int s = 0; s < ModelAnimator::SlotCount; ++s) {
                if (s == ModelAnimator::SlotWalk) continue;
                L.AddClip(std::string("Clip") + ModelAnimator::SlotName(s), 1.0f, 10.0f);
                L.ma->SlotClip(s) = std::string("Clip") + ModelAnimator::SlotName(s);
            }
            FixCharacterAnimations(*L.rig.scene, L.rig.player);
            CharacterModelInfo info;
            CHECK(DescribeCharacterModel(*L.rig.scene, L.rig.player, info));
            CHECK(info.source == 1 && info.builtinSlots == 0 && info.importedSlots == ModelAnimator::SlotCount);
            CHECK(std::string(CharacterAnimationSourceName(info.source)) == "Imported clips");
        }
    }

    TEST_MAIN_RESULT();
}
