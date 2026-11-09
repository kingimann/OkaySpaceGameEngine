#include "test_framework.hpp"
#include "rig_harness.hpp"
#include "okay/Animation/HumanoidBones.hpp"
#include "okay/Animation/HumanoidLibrary.hpp"
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/ControllerFeel.hpp"
#include "okay/Components/LookAtIK.hpp"
#include "okay/Components/PlayerController.hpp"
#include "okay/Components/ThirdPersonController.hpp"
#include "okay/Math/Quat.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace okay;

// Headless playback of a rigged character. Held keys and a steady mouse stand
// in for a Play session. Per-frame bone and camera steps are scored for spikes
// and for the head turning the wrong way.

namespace {

constexpr float kRad = 57.2957795f;

float Heading(const Vec3& f) { return std::atan2(f.x, -f.z) * kRad; }

float Wrap180(float d) {
    while (d > 180.0f) d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return d;
}

float Median(std::vector<float> v) {
    if (v.empty()) return 0.0f;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

Vec3 FacingAxis(const Quat& rot, const Vec3& worldDir) {
    const Vec3 want = rot.Inverse() * worldDir;
    const Vec3 axes[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    int best = 0;
    float bd = -2.0f;
    for (int i = 0; i < 6; ++i) {
        const float d = Vec3::Dot(axes[i], want);
        if (d > bd) { bd = d; best = i; }
    }
    return axes[best];
}

struct Series {
    const char* name = "";
    std::vector<float> deg;     // player-space rotation step, degrees
    std::vector<float> dps;     // that step / dt
    std::vector<float> yawD;    // signed heading change of the facing axis
    float maxPos = 0.0f;        // player-space position step, metres
    int spikes = 0;
    int flips = 0;

    void Add(float stepDeg, float dt, float yawDelta, float posStep) {
        deg.push_back(stepDeg);
        dps.push_back(dt > 1e-5f ? stepDeg / dt : 0.0f);
        yawD.push_back(yawDelta);
        maxPos = std::fmax(maxPos, posStep);
    }
    void Finish() {
        const float med = Median(dps);
        const float gate = std::fmax(140.0f, med * 6.0f);
        for (float s : dps) if (s > gate) ++spikes;
        for (std::size_t i = 1; i < yawD.size(); ++i) {
            if (std::fabs(yawD[i]) > 0.2f && std::fabs(yawD[i - 1]) > 0.2f && yawD[i] * yawD[i - 1] < 0.0f)
                ++flips;
        }
    }
    void Print(float seconds) const {
        const float med = Median(dps);
        float mx = 0.0f;
        for (float s : dps) mx = std::fmax(mx, s);
        int moving = 0;
        for (float d : deg) if (d > 0.15f) ++moving;
        std::printf("  HARNESS %-14s max %6.1f deg/s  median %5.1f  spikes %2d  yaw flips %2d (%.1f/s)  moving %3d/%zu  pos %.4f m\n",
                    name, mx, med, spikes, flips, seconds > 1e-3f ? flips / seconds : 0.0f,
                    moving, deg.size(), maxPos);
    }
};

struct Sampler {
    Transform* bone = nullptr;
    Vec3 axis{0, 0, 1};
    bool have = false;
    Quat prev = Quat::Identity;
    float prevHeading = 0.0f;
    Vec3 prevPos{0, 0, 0};
    Series series;

    void Begin(Transform* t, const Vec3& ax, const char* name) {
        bone = t; axis = ax; have = false; series = Series{}; series.name = name;
    }
    void Sample(const Quat& playerRot, const Mat4& playerInv, float dt) {
        if (!bone) return;
        const Quat local = (playerRot.Inverse() * bone->Rotation()).Normalized();
        const Vec3 pos = playerInv.MultiplyPoint(bone->Position());
        const float h = Heading(bone->Rotation() * axis);
        if (have) {
            series.Add(Quat::Angle(prev, local), dt, Wrap180(h - prevHeading), (pos - prevPos).Magnitude());
        }
        prev = local; prevPos = pos; prevHeading = h; have = true;
    }
};

Transform* Named(const righarness::Rig& r, const char* frag) {
    for (Transform* t : r.joints) {
        if (!t || !t->gameObject) continue;
        std::string n = t->gameObject->name;
        for (char& c : n) c = (char)std::tolower((unsigned char)c);
        if (n.find(frag) != std::string::npos) return t;
    }
    return nullptr;
}

struct Run {
    Series head, neck, spine, hips, cam;
    float headVsCam = 0.0f;     // |heading(head) - heading(camera)| at the end
    float headVsBody = 0.0f;
    float camHeadingD = 0.0f;   // camera heading change over the run
    float headHeadingD = 0.0f;
    float bodyHeadingD = 0.0f;
    float lookYaw = 0.0f;
    int states = 0;
};

Run Play(const char* label, float dt, const std::vector<char>& keys, float mousePerSec,
         float seconds, bool strafeScore) {
    Run out;
    righarness::Rig R = righarness::PlayableFromFile("mixamo_t_cm_clips.glb");
    if (!R || !R.player || !R.scene) {
        std::printf("  HARNESS %s: rig failed to load\n", label);
        return out;
    }
    Transform* head = R.Bone(humanoid::Head);
    Transform* spine = R.Bone(humanoid::Torso);
    Transform* hips = R.Bone(humanoid::Hips);
    Transform* neck = Named(R, "neck");
    Camera* cam = R.scene->mainCamera;
    Character* ch = R.player->GetComponent<Character>();
    if (!head || !spine || !hips || !cam || !cam->transform || !ch) {
        std::printf("  HARNESS %s: missing bones or camera\n", label);
        return out;
    }
    const Vec3 body0 = R.player->transform->Rotation() * Vec3{0, 0, -1};
    const Vec3 headAxis = FacingAxis(head->Rotation(), body0);
    const Vec3 neckAxis = neck ? FacingAxis(neck->Rotation(), body0) : Vec3{0, 0, 1};
    const Vec3 spineAxis = FacingAxis(spine->Rotation(), body0);
    const Vec3 hipAxis = FacingAxis(hips->Rotation(), body0);

    Sampler sh, sn, ss, shp, sc;
    sh.Begin(head, headAxis, "head");
    sn.Begin(neck, neckAxis, "neck");
    ss.Begin(spine, spineAxis, "spine");
    shp.Begin(hips, hipAxis, "hips");
    sc.Begin(cam->transform, Vec3{0, 0, -1}, "camera");

    Vec2 mouse{400.0f, 300.0f};
    auto step = [&](float frameDt, bool moveMouse) {
        if (moveMouse) mouse.x += mousePerSec * frameDt;
        Input::FeedMouse(mouse, 0);
        Input::FeedKeys(keys);
        R.scene->Update(frameDt);
    };
    // Baseline: the first mouse sample is not a look.
    step(dt, false);
    const float camH0 = Heading(cam->transform->Rotation() * Vec3{0, 0, -1});
    const float headH0 = Heading(head->Rotation() * headAxis);
    const float bodyH0 = Heading(R.player->transform->Rotation() * Vec3{0, 0, -1});
    const Vec3 origin = R.player->transform->Position();
    std::string state0;
    if (auto* sm = R.player->GetComponent<AnimStateMachine>()) state0 = sm->Current();
    int stateChanges = 0;

    const int frames = std::max(1, (int)std::lround(seconds / dt));
    for (int i = 0; i < frames; ++i) {
        step(dt, true);
        const Quat pr = R.player->transform->Rotation();
        const Mat4 inv = R.player->transform->LocalToWorldMatrix().Inverse();
        sh.Sample(pr, inv, dt);
        sn.Sample(pr, inv, dt);
        ss.Sample(pr, inv, dt);
        shp.Sample(pr, inv, dt);
        sc.Sample(pr, inv, dt);
        if (auto* sm = R.player->GetComponent<AnimStateMachine>())
            if (sm->Current() != state0) { ++stateChanges; state0 = sm->Current(); }
    }
    sh.series.Finish(); sn.series.Finish(); ss.series.Finish(); shp.series.Finish(); sc.series.Finish();
    out.head = sh.series; out.neck = sn.series; out.spine = ss.series; out.hips = shp.series; out.cam = sc.series;
    out.states = stateChanges;
    const Vec3 camF = cam->transform->Rotation() * Vec3{0, 0, -1};
    const Vec3 headF = head->Rotation() * headAxis;
    const Vec3 bodyF = R.player->transform->Rotation() * Vec3{0, 0, -1};
    out.headVsCam = std::fabs(Wrap180(Heading(headF) - Heading(camF)));
    out.headVsBody = std::fabs(Wrap180(Heading(headF) - Heading(bodyF)));
    out.camHeadingD = Wrap180(Heading(camF) - camH0);
    out.headHeadingD = Wrap180(Heading(headF) - headH0);
    out.bodyHeadingD = Wrap180(Heading(bodyF) - bodyH0);
    out.lookYaw = ch->HeadYaw();
    std::printf("HARNESS %s dt %.4f  %s\n", label, dt, strafeScore ? "strafe" : "move");
    out.head.Print(seconds); out.neck.Print(seconds); out.spine.Print(seconds);
    out.hips.Print(seconds); out.cam.Print(seconds);
    const float travelled = (R.player->transform->Position() - origin).Magnitude();
    std::printf("  HARNESS end head-cam %.1f deg  head-body %.1f  cam d %.1f  head d %.1f  body d %.1f  eased look %.1f  state changes %d  travel %.2f m\n",
                out.headVsCam, out.headVsBody, out.camHeadingD, out.headHeadingD, out.bodyHeadingD, out.lookYaw, out.states, travelled);
    return out;
}

} // namespace

int main() {
    RUN_SUITE("anim_jitter");

    // Look-at: a target that walks behind the head must not flip the clamp.
    {
        Scene s("look"); s.physicsEnabled = false;
        GameObject* body = s.CreateGameObject("Body");
        GameObject* spine = s.CreateGameObject("Spine");
        GameObject* head = s.CreateGameObject("Head");
        spine->transform->SetParent(body->transform, false);
        spine->transform->localPosition = {0, 1.2f, 0};
        head->transform->SetParent(spine->transform, false);
        head->transform->localPosition = {0, 0.25f, 0};
        auto* ik = body->AddComponent<LookAtIK>();
        ik->chain = {spine->transform, head->transform};
        ik->forwardAxis = Vec3{0, 0, 1};
        ik->smoothing = 12.0f;
        ik->clampAngle = 70.0f;
        ik->maxAngle = 55.0f;
        ik->aimAtPoint = true;
        s.Start();
        int flips = 0;
        float prevD = 0.0f;
        float prevH = 0.0f;
        bool have = false;
        const float dt = 1.0f / 60.0f;
        float worst = 0.0f;
        for (int i = 0; i < 240; ++i) {
            const float a = (float)i / 240.0f * 6.2831853f;   // full circle, 4 s
            ik->target = {std::sin(a) * 3.0f, 1.45f, std::cos(a) * 3.0f};
            s.Update(dt);
            const float h = Heading(head->transform->Rotation() * Vec3{0, 0, 1});
            if (have) {
                const float d = Wrap180(h - prevH);
                worst = std::fmax(worst, std::fabs(d));
                if (std::fabs(d) > 0.4f && std::fabs(prevD) > 0.4f && d * prevD < 0.0f) ++flips;
                prevD = d;
            }
            prevH = h; have = true;
        }
        std::printf("  HARNESS look-at circle: worst step %.2f deg, yaw reversals %d\n", worst, flips);
        // One frame where the target crosses the rear of the clamp is a few
        // degrees of ease, not a cheek-to-cheek flip (that is a reversal).
        CHECK(worst < 16.0f);
        CHECK(flips < 4);
    }

    // A far aim point moves metres when the view turns a few degrees. That eases.
    // A jump to the opposite side is a teleport and snaps.
    {
        Scene s("far"); s.physicsEnabled = false;
        GameObject* body = s.CreateGameObject("Body");
        GameObject* head = s.CreateGameObject("Head");
        head->transform->SetParent(body->transform, false);
        head->transform->localPosition = {0, 1.6f, 0};
        auto* ik = body->AddComponent<LookAtIK>();
        ik->chain = {head->transform};
        ik->forwardAxis = Vec3{0, 0, 1};
        ik->smoothing = 8.0f;
        ik->clampAngle = 180.0f;
        ik->maxAngle = 180.0f;
        ik->target = {0, 1.6f, 12.0f};
        s.Start();
        const float dt = 1.0f / 60.0f;
        for (int i = 0; i < 40; ++i) s.Update(dt);
        const float a = 12.0f * 0.017453292f;
        ik->target = {std::sin(a) * 12.0f, 1.6f, std::cos(a) * 12.0f};   // ~2.5 m, 12 deg
        s.Update(dt);
        const Vec3 f = (head->transform->Rotation() * Vec3{0, 0, 1}).Normalized();
        const Vec3 to = (ik->target - head->transform->Position()).Normalized();
        const float left = std::acos(std::fmax(-1.0f, std::fmin(1.0f, Vec3::Dot(f, to)))) * kRad;
        std::printf("  HARNESS far aim 12 deg: %.1f deg still to close\n", left);
        CHECK(left > 6.0f);
        CHECK(left < 12.0f);
        ik->target = {0, 1.6f, -12.0f};
        s.Update(dt);
        const Vec3 f2 = (head->transform->Rotation() * Vec3{0, 0, 1}).Normalized();
        const Vec3 to2 = (ik->target - head->transform->Position()).Normalized();
        const float gap = std::acos(std::fmax(-1.0f, std::fmin(1.0f, Vec3::Dot(f2, to2)))) * kRad;
        std::printf("  HARNESS opposite-side teleport: %.1f deg left (snap)\n", gap);
        CHECK(gap < 2.0f);
    }

    // Gait parameters while the travel heading sweeps past the side and behind.
    {
        Scene s("gait"); s.physicsEnabled = false;
        GameObject* body = s.CreateGameObject("Body");
        auto* ch = body->AddComponent<Character>();
        ch->Apply();
        AnimStateMachine* sm = HumanoidLibrary::EnsureAnimator(body);
        s.Start();
        const float dt = 1.0f / 60.0f;
        for (int i = 0; i < 30; ++i) s.Update(dt);
        float prevX = 0.0f, prevZ = 0.0f;
        bool have = false;
        float worst = 0.0f;
        int jumps = 0;
        for (int deg = -170; deg <= 170; deg += 4) {
            const float a = (float)deg * 0.017453292f;
            Character::Locomotion lo;
            lo.velocity = {std::sin(a) * 1.4f, 0.0f, -std::cos(a) * 1.4f};
            lo.facing = {0, 0, -1};
            lo.onGround = true;
            lo.dt = dt;
            lo.walkSpeed = 1.6f;
            lo.runSpeed = 5.5f;
            ch->DriveLocomotion(lo);
            s.Update(dt);
            if (!sm) continue;
            const float x = sm->GetFloat("gaitX"), z = sm->GetFloat("gaitZ");
            if (have) {
                const float d = std::sqrt((x - prevX) * (x - prevX) + (z - prevZ) * (z - prevZ));
                worst = std::fmax(worst, d);
                if (d > 1.0f) ++jumps;
            }
            prevX = x; prevZ = z; have = true;
        }
        std::printf("  HARNESS gait sweep: worst step %.3f, jumps %d  (end gait %.2f, %.2f)\n",
                    worst, jumps, prevX, prevZ);
        CHECK(jumps == 0);
        CHECK(worst < 0.8f);
    }

    const std::vector<float> rates = {1.0f / 11.0f, 1.0f / 30.0f, 1.0f / 60.0f, 1.0f / 144.0f};
    Run walk60;
    for (float dt : rates) {
        const char* tag = dt > 0.05f ? "walk-11" : dt > 0.02f ? "walk-30" : dt > 0.01f ? "walk-60" : "walk-144";
        Run r = Play(tag, dt, {'W'}, 0.0f, 2.0f, false);
        if (tag[5] == '6') walk60 = r;
        CHECK(r.head.spikes < 8);
        CHECK(r.hips.spikes < 8);
        CHECK(r.cam.spikes < 6);
        CHECK(r.cam.maxPos < 0.08f);
    }

    // Uneven frame times, the way a busy editor delivers them.
    {
        righarness::Rig R = righarness::PlayableFromFile("mixamo_t_cm_clips.glb");
        CHECK(R.model != nullptr);
        if (R && R.player) {
            const float seq[] = {1.0f / 20.0f, 1.0f / 144.0f, 1.0f / 40.0f, 1.0f / 90.0f, 1.0f / 30.0f};
            Transform* head = R.Bone(humanoid::Head);
            Transform* hips = R.Bone(humanoid::Hips);
            Camera* cam = R.scene->mainCamera;
            Quat prevHead = Quat::Identity, prevHips = Quat::Identity;
            Vec3 prevCam{0, 0, 0};
            bool have = false;
            float worstH = 0.0f, worstP = 0.0f, worstC = 0.0f;
            Input::FeedMouse({400, 300}, 0);
            for (int i = 0; i < 180; ++i) {
                const float dt = seq[i % 5];
                Input::FeedKeys({'W'});
                Input::FeedMouse({400, 300}, 0);
                R.scene->Update(dt);
                const Quat pr = R.player->transform->Rotation();
                const Mat4 inv = R.player->transform->LocalToWorldMatrix().Inverse();
                if (head) {
                    const Quat local = (pr.Inverse() * head->Rotation()).Normalized();
                    if (have) worstH = std::fmax(worstH, Quat::Angle(prevHead, local) / dt);
                    prevHead = local;
                }
                if (hips) {
                    const Quat local = (pr.Inverse() * hips->Rotation()).Normalized();
                    if (have) worstP = std::fmax(worstP, Quat::Angle(prevHips, local) / dt);
                    prevHips = local;
                }
                if (cam && cam->transform) {
                    const Vec3 c = inv.MultiplyPoint(cam->transform->Position());
                    if (have) worstC = std::fmax(worstC, (c - prevCam).Magnitude());
                    prevCam = c;
                }
                have = true;
            }
            std::printf("  HARNESS jittery dt: head %.1f deg/s  hips %.1f deg/s  cam step %.4f m\n",
                        worstH, worstP, worstC);
            CHECK(worstH < 800.0f);
            CHECK(worstP < 900.0f);
        }
    }

    // A cursor-space change (Game view grabs or releases the pointer) is not a look.
    // 150 px is under the 700 px warp drop, so without the generation it snaps ~30°.
    {
        Scene s("warp"); s.physicsEnabled = false;
        GameObject* p = s.CreateGameObject("P");
        auto* tp = p->AddComponent<ThirdPersonController>();
        tp->lockCursor = false;
        s.Start();
        Input::FeedMouse({200.0f, 200.0f}, 0);
        s.Update(1.0f / 60.0f);
        Input::FeedMouse({40.0f, 200.0f}, 0);   // 160 px left: a real look
        s.Update(1.0f / 60.0f);
        const float yaw = tp->yaw;
        const float pitched = tp->pitch;
        NoteMouseSpaceChanged();
        Input::FeedMouse({200.0f, 80.0f}, 0);   // back to another corner of the view
        s.Update(1.0f / 60.0f);
        std::printf("  HARNESS mouse space: yaw %.2f -> %.2f  pitch %.2f -> %.2f\n",
                    yaw, tp->yaw, pitched, tp->pitch);
        CHECK_NEAR(tp->yaw, yaw, 0.05f);
        CHECK_NEAR(tp->pitch, pitched, 0.05f);
        // The following real pixel still looks.
        Input::FeedMouse({200.0f, 80.0f}, 0);
        s.Update(1.0f / 60.0f);
        Input::FeedMouse({230.0f, 80.0f}, 0);
        s.Update(1.0f / 60.0f);
        CHECK(std::fabs(tp->yaw - yaw) > 2.0f);
    }

    // Standing still, mouse looks right. The head must follow the camera, not the opposite way.
    {
        // 200 px/s * 0.2 deg/px = 40 deg/s, for 1 s.
        Run r = Play("look-right", 1.0f / 60.0f, {}, 200.0f, 1.0f, false);
        std::printf("  HARNESS look sign: cam %.1f head %.1f (same sign means the head followed)\n",
                    r.camHeadingD, r.headHeadingD);
        CHECK(r.camHeadingD > 20.0f);
        CHECK(r.headHeadingD > 12.0f);
        CHECK(r.head.spikes < 4);
        float worst = 0.0f;
        for (float d : r.head.deg) worst = std::fmax(worst, d);
        std::printf("  HARNESS look-right max head step %.2f deg\n", worst);
        CHECK(worst < 18.0f);
    }

    // Mouse up raises the gaze. The head bone's forward must rise with the camera.
    {
        righarness::Rig R = righarness::PlayableFromFile("mixamo_t_cm_clips.glb");
        CHECK(R.model != nullptr);
        if (R && R.player) {
            Transform* head = R.Bone(humanoid::Head);
            Camera* cam = R.scene->mainCamera;
            const Vec3 bodyF = R.player->transform->Rotation() * Vec3{0, 0, -1};
            const Vec3 axis = head ? FacingAxis(head->Rotation(), bodyF) : Vec3{0, 0, 1};
            Vec2 mouse{400.0f, 300.0f};
            Input::FeedMouse(mouse, 0);
            Input::FeedKeys({});
            R.scene->Update(1.0f / 60.0f);
            const float y0 = head ? (head->Rotation() * axis).y : 0.0f;
            const float camY0 = cam && cam->transform ? (cam->transform->Rotation() * Vec3{0, 0, -1}).y : 0.0f;
            for (int i = 0; i < 60; ++i) {
                mouse.y -= 3.0f;   // mouse up
                Input::FeedMouse(mouse, 0);
                Input::FeedKeys({});
                R.scene->Update(1.0f / 60.0f);
            }
            const float y1 = head ? (head->Rotation() * axis).y : 0.0f;
            const float camY1 = cam && cam->transform ? (cam->transform->Rotation() * Vec3{0, 0, -1}).y : 0.0f;
            std::printf("  HARNESS look-up: camera forward y %.3f -> %.3f  head forward y %.3f -> %.3f\n",
                        camY0, camY1, y0, y1);
            CHECK(camY1 > camY0 + 0.15f);
            CHECK(y1 > y0 + 0.05f);
        }
    }

    // Strafe: body faces travel, head should keep the camera in view.
    {
        Run r = Play("strafe", 1.0f / 60.0f, {'D'}, 0.0f, 1.6f, true);
        std::printf("  HARNESS strafe head-cam %.1f (want the camera, not the body's side)\n", r.headVsCam);
        CHECK(r.headVsCam < 22.0f);
        float worst = 0.0f;
        for (float d : r.head.deg) worst = std::fmax(worst, d);
        std::printf("  HARNESS strafe max head step %.2f deg\n", worst);
        CHECK(worst < 14.0f);
    }

    // Run and a walk with the mouse turning, at 60 Hz.
    {
        Run run = Play("run", 1.0f / 60.0f, {'W', Input::KeyShift}, 0.0f, 1.5f, false);
        CHECK(run.hips.spikes < 8);
        CHECK(run.cam.maxPos < 0.1f);
        Run turn = Play("walk-turn", 1.0f / 60.0f, {'W'}, 80.0f, 1.5f, false);
        CHECK(turn.head.spikes < 6);
        CHECK(turn.headHeadingD * turn.camHeadingD > 0.0f);
    }

    (void)walk60;
    TEST_MAIN_RESULT();
}
