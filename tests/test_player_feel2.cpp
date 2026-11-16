// Player feel, round 2 (claude/player-feel-2): the body's animation follows the controller: landing depth follows the
// impact (driven by the hard-landing event path), stopping, turning in place and stance changes do not skate or pop,
// and the third-person camera is the same at every frame rate through a walk that changes speed. Each block prints the
// numbers it measured. Run with OKAY_OLD_PLAYER=1 to see the landing check fail (the old fixed dip).
#include "test_framework.hpp"
#include <Okay.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using namespace okay;
using Mode = PlayerController::ViewMode;

namespace {

struct Rig {
    std::unique_ptr<Scene> sc;
    GameObject* p = nullptr;
    Rigidbody3D* rb = nullptr;
    Character* ch = nullptr;
    MeshRenderer* mr = nullptr;
    float dt = 1.0f / 60.0f;
    Rig() {
        sc = std::make_unique<Scene>("feel2");
        sc->physicsEnabled = true;
        sc->UseFixedStepPhysics(true);
        GameObject* f = sc->CreateGameObject("Floor");
        f->transform->localPosition = {0, -0.5f, 0};
        f->AddComponent<BoxCollider3D>()->size = {400, 1, 400};
    }
    void Spawn(Mode m, Vec3 at = {0, 0.02f, 0}) {
        p = Templates::BuildPlayerBody(*sc, at, "Player");
        rb = p->GetComponent<Rigidbody3D>();
        PlayerController::Add(p, m, m == Mode::Third ? PlayerView::View::Third : PlayerView::View::First);
        Templates::MountEyeCamera(*sc, p, "Main Camera");
        sc->Start();
        ch = p->GetComponent<Character>(); mr = p->GetComponent<MeshRenderer>();
    }
    void Frame(const std::vector<char>& k, float d = 0.0f) { Input::FeedKeys(k); sc->Update(d > 0 ? d : dt); }
    void Frames(double s, const std::vector<char>& k) { for (int i = 0, n = (int)std::lround(s / dt); i < n; ++i) Frame(k); }
    Vec3 Pos() const { return p->transform->Position(); }
    float Top() const {   // highest point of the animated body, above the feet
        float hi = -1e9f;
        for (const Vec3& v : mr->mesh.vertices) hi = std::max(hi, p->transform->TransformPoint(v).y);
        return hi - Pos().y;
    }
    ~Rig() { Input::FeedKeys({}); }
};

// Lowest point of the body in the first/second foot bone (the planted foot), world position.
struct Feet {
    std::vector<int> bone; int idx[2]; std::size_t vi[2] = {0, 0}; Vec3 pos[2];
    explicit Feet(Rig& r) { r.ch->BuildRig(bone); idx[0] = Character::BoneIndex("l_foot"); idx[1] = Character::BoneIndex("r_foot"); }
    Vec3 World(Rig& r, std::size_t i) { return r.p->transform->TransformPoint(r.mr->mesh.vertices[i]); }
    // Planted-foot speeds this frame (m/s over the ground) for feet that are on the ground.
    void Step(Rig& r, float dt, std::vector<float>& out) {
        if (r.mr->mesh.vertices.size() != bone.size()) return;
        for (int f = 0; f < 2; ++f) {
            if (pos[f].y < 0.01f && have) { const Vec3 w = World(r, vi[f]); out.push_back(std::hypot(w.x - pos[f].x, w.z - pos[f].z) / dt); }
            float lo = 1e9f; std::size_t best = 0;
            for (std::size_t i = 0; i < bone.size(); ++i) if (bone[i] == idx[f]) { const float y = World(r, i).y; if (y < lo) { lo = y; best = i; } }
            vi[f] = best; pos[f] = World(r, best);
        }
        have = true;
    }
    bool have = false;
};

float Median(std::vector<float> v) { if (v.empty()) return 99.0f; std::sort(v.begin(), v.end()); return v[v.size() / 2]; }
float Percentile(std::vector<float> v, float q) { if (v.empty()) return 99.0f; std::sort(v.begin(), v.end()); return v[(std::size_t)((v.size() - 1) * q)]; }

}  // namespace

int main() {
    RUN_SUITE("player_feel2");

    // ---- Landing: the knee bend follows the impact. Drop from 0.6 m, 2.5 m and 9 m while holding W. ----
    {
        float dip[3]; const float heights[3] = {0.6f, 2.5f, 9.0f};
        for (int i = 0; i < 3; ++i) {
            Rig r; r.Spawn(Mode::Third, {0, heights[i], 0});
            float stand = 0;
            for (int k = 0; k < 600 && !r.rb->velocity.y; ++k) r.Frame({});     // let it start falling
            float lowest = 1e9f; bool landed = false; int after = 0;
            for (int k = 0; k < 600 && after < 40; ++k) {
                r.Frame({'w'});
                const bool g = r.p->GetComponent<ThirdPersonController>()->IsGrounded();
                if (g) { if (!landed) { landed = true; } ++after; lowest = std::min(lowest, r.Top()); stand = std::max(stand, r.Top()); }
            }
            dip[i] = stand - lowest;
            std::printf("[player_feel2] landing from %.1f m: body top drops %.3f m during recovery (impact %.1f m/s)\n", heights[i], dip[i], r.p->GetComponent<ThirdPersonController>()->debugState().lastLandSpeed);
            CHECK(landed);
        }
        CHECK(dip[2] > dip[0] + 0.05f);      // player_feel2_landing_scales: a hard landing bends the knees clearly more than a step down
        CHECK(dip[2] > dip[1] * 1.15f);
        CHECK(dip[0] > 0.005f);              // a light landing still shows
    }

    // ---- Stopping: while slowing from a run, the planted foot does not skate. ----
    {
        Rig r; r.Spawn(Mode::Third);
        Feet ft(r);
        const float d = 1.0f / 120.0f;
        for (int i = 0; i < 240; ++i) r.Frame({'w', Input::KeyShift}, d);
        std::vector<float> slip; std::vector<float> ratio; Vec3 prev = r.Pos();
        for (int i = 0; i < 90; ++i) {
            r.Frame({}, d);
            const float g = std::hypot(r.Pos().x - prev.x, r.Pos().z - prev.z) / d; prev = r.Pos();
            std::vector<float> s; ft.Step(r, d, s);
            if (g > 0.4f) for (float x : s) ratio.push_back(x / g);
        }
        const float med = Median(ratio), p90 = Percentile(ratio, 0.9f);
        std::printf("[player_feel2] stopping: planted-foot speed / ground speed while slowing: median %.2f, p90 %.2f (%zu samples)\n", med, p90, ratio.size());
        CHECK(!ratio.empty());
        CHECK(med < 0.35f);          // player_feel2_stop_no_skate
    }

    // ---- Turning in place: a 120 degree turn of a standing player does not skate the feet. ----
    {
        Rig r; r.Spawn(Mode::Third);
        Feet ft(r);
        const float d = 1.0f / 120.0f;
        for (int i = 0; i < 120; ++i) r.Frame({}, d);
        auto* tp = r.p->GetComponent<ThirdPersonController>();
        std::vector<float> slip; float yaw0 = r.p->transform->Rotation().ToEuler().y;
        for (int i = 0; i < 240; ++i) {
            if (i == 20) tp->yaw += 120.0f;
            r.Frame(i > 20 && i < 24 ? std::vector<char>{'w'} : std::vector<char>{}, d);
            ft.Step(r, d, slip);
        }
        const float p90 = Percentile(slip, 0.9f);
        std::printf("[player_feel2] turn in place: planted-foot speed median %.3f, p90 %.3f m/s (%zu samples), body yaw %.0f -> %.0f\n", Median(slip), p90, slip.size(), yaw0, r.p->transform->Rotation().ToEuler().y);
        CHECK(!slip.empty());
        CHECK(Median(slip) < 0.25f);   // player_feel2_turn_no_skate
    }

    // ---- Crouch and prone: the body height changes smoothly, no frame jumps more than 12 cm. ----
    {
        Rig r; r.Spawn(Mode::Third);
        r.Frames(1.0, {});
        float prev = r.Top(), worst = 0, low = prev;
        const std::vector<std::vector<char>> script = {{Input::KeyCtrl}, {}, {}, {Input::KeyCtrl}, {}, {}, {}};
        for (int phase = 0; phase < 7; ++phase) {
            for (int i = 0; i < (phase == 0 || phase == 3 ? 1 : 30); ++i) {
                r.Frame(script[phase]);
                const float t = r.Top(); worst = std::max(worst, std::fabs(t - prev)); prev = t; low = std::min(low, t);
            }
        }
        std::printf("[player_feel2] crouch and stand: body top %.2f -> low %.2f m, biggest change in one frame %.3f m\n", 0.0f + prev, low, worst);
        CHECK(low < 1.4f);       // it did crouch
        CHECK(worst < 0.12f);    // player_feel2_stance_smooth
    }

    // ---- Third-person camera through a walk that changes speed: same path at 30 / 60 / 144 / 240 fps. ----
    {
        auto keysAt = [](double t) -> std::vector<char> {
            if (t < 1.0) return {'w'};                       // walk
            if (t < 2.0) return {'w', Input::KeyShift};      // run
            if (t < 2.6) return {};                          // stop
            if (t < 3.4) return {'w'};                       // walk again
            if (t < 4.0) return {'w', Input::KeyShift};      // run
            return {};
        };
        auto run = [&](float fps) {
            Rig r; r.Spawn(Mode::Third);
            const float d = 1.0f / fps;
            for (int i = 0; i < (int)(0.6f * fps); ++i) r.Frame({}, d);
            Camera* cam = r.sc->mainCamera;
            std::vector<std::pair<double, Vec3>> trace;
            double t = 0;
            for (int i = 0; i < (int)(4.6f * fps); ++i, t += d) { r.Frame(keysAt(t), d); trace.push_back({t + d, cam->transform->Position() - Vec3{r.Pos().x, 0, r.Pos().z} * 0.0f}); }
            return trace;
        };
        auto sample = [](const std::vector<std::pair<double, Vec3>>& tr, double t) {
            for (std::size_t i = 1; i < tr.size(); ++i) if (tr[i].first >= t) { const double u = (t - tr[i - 1].first) / (tr[i].first - tr[i - 1].first); return tr[i - 1].second * (float)(1 - u) + tr[i].second * (float)u; }
            return tr.back().second;
        };
        const auto ref = run(240.0f);
        for (float fps : {30.0f, 60.0f, 144.0f}) {
            const auto tr = run(fps);
            float worst = 0, sum = 0; int n = 0;
            for (double t = 0.2; t < 4.5; t += 0.05) { const float e = (sample(tr, t) - sample(ref, t)).Magnitude(); worst = std::max(worst, e); sum += e; ++n; }
            std::printf("[player_feel2] camera path at %3.0f fps against 240 fps: mean difference %.3f m, worst %.3f m\n", fps, sum / n, worst);
            CHECK(worst < 0.25f);   // player_feel2_camera_fps: the same path, not a different feel
        }
        // Backwards motion: through the stop and restart the camera never moves against the player's heading, at any rate.
        for (float fps : {30.0f, 144.0f, 240.0f}) {
            Rig r; r.Spawn(Mode::Third);
            const float d = 1.0f / fps;
            for (int i = 0; i < (int)(0.6f * fps); ++i) r.Frame({}, d);
            Camera* cam = r.sc->mainCamera;
            Vec3 prev = cam->transform->Position(); double t = 0; float back = 0;
            for (int i = 0; i < (int)(4.6f * fps); ++i, t += d) {
                r.Frame(keysAt(t), d);
                const Vec3 c = cam->transform->Position();
                back = std::min(back, -(c.z - prev.z) / d);   // heading is -Z; the camera should not advance +Z
                prev = c;
            }
            std::printf("[player_feel2] camera at %3.0f fps: fastest move against the heading %.3f m/s\n", fps, -back);
            CHECK(-back < 0.5f);
        }
    }

    TEST_MAIN_RESULT();
}
