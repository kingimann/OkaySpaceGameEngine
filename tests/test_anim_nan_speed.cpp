// #888 finding 38: one frame with a NaN or Inf speed on an AnimStateMachine
// left the bones at NaN for good. The rate went into the backend's clip clock
// (t += dt * speed), and a NaN clock never comes back; exit-time and
// OnClipEnd tests compare against NaN, so the graph could not leave the
// state either. A matchSpeed blend tree did the same with a huge blend
// parameter (x * x overflowed to Inf).
//
// Each setup: 10 frames with the parameter at 1, ONE frame with the bad value,
// the parameter back at 1, then 120 frames (60 Hz). The bones and the
// normalized time must be finite, and the time must keep advancing.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

using namespace okay;
using SM = AnimStateMachine;

namespace {

const float kNaN = std::numeric_limits<float>::quiet_NaN();
const float kInf = std::numeric_limits<float>::infinity();
const float kDt  = 1.0f / 60.0f;

// A 1 s clip moving the bone's x from `x0` to `x0 + 1`.
ModelAnimator::Clip Ramp(const char* name, float x0) {
    ModelAnimator::Clip c; c.name = name;
    ModelAnimator::NodeClip nc; nc.node = "Bone";
    nc.clip.AddKey("position.x", 0.0f, x0);
    nc.clip.AddKey("position.x", 1.0f, x0 + 1.0f);
    c.nodes.push_back(nc);
    return c;
}

enum class Kind { Plain, Tree1D, Tree2D };

struct Setup {
    const char* label;
    Kind kind;
    bool matchSpeed = false;
    bool speedParam = false;     // the state reads "spd" as its speed
    std::string badParam;        // which parameter gets the bad value
    float bad = 0.0f;
};

struct Result {
    float x = 0.0f;              // bone x at the end
    float norm60 = 0.0f;         // normalized time 60 frames after the reset
    float norm120 = 0.0f;        // ... and 120 frames after
    std::string state;           // the state at the end
    std::string afterBad;        // the state right after the bad frame
    int exitFrame = -1;          // frame after the reset the state changed on
};

SM::State MakeState(const Setup& s) {
    SM::State st; st.name = "Loco";
    if (s.speedParam) st.speedParam = "spd";
    if (s.kind == Kind::Plain) { st.clip = "walk"; return st; }
    st.blendTree = true;
    st.matchSpeed = s.matchSpeed;
    st.blendParam = "bx";
    if (s.kind == Kind::Tree2D) {
        st.blendDims = 2;
        st.blendParamY = "by";
        st.motions = {{"idle", 0.0f, 0.0f}, {"walk", 2.0f, 0.0f}, {"run", 0.0f, 2.0f}};
    } else {
        st.motions = {{"idle", 0.0f}, {"walk", 2.0f}, {"run", 4.0f}};
    }
    return st;
}

// `exitTo` adds an exit-time transition (after 1.5 normalized) to a "Done" state.
Result Run(const Setup& s, bool exitTo = false, float paramDefault = 1.0f) {
    Scene scene; scene.physicsEnabled = false;
    GameObject* model = scene.CreateGameObject("Model");
    GameObject* bone = scene.CreateGameObject("Bone");
    bone->transform->SetParent(model->transform, false);
    auto* ma = model->AddComponent<ModelAnimator>();
    ma->autoPlay = false; ma->blendTime = 0.0f;
    ma->clips = {Ramp("idle", 0.0f), Ramp("walk", 10.0f), Ramp("run", 20.0f)};

    auto* sm = model->AddComponent<SM>();
    SM::State st = MakeState(s);
    if (exitTo) {
        SM::Transition tr; tr.to = "Done"; tr.cond = SM::Cond::Always;
        tr.hasExitTime = true; tr.exitTime = 1.5f; tr.blend = 0.0f;
        st.transitions.push_back(tr);
    }
    SM::State done; done.name = "Done"; done.clip = "idle";
    sm->states = {st, done};
    sm->params = {{"spd", 0, paramDefault}, {"bx", 0, 1.0f}, {"by", 0, 0.0f}};

    scene.Start();
    for (int i = 0; i < 10; ++i) scene.Update(kDt);
    if (!s.badParam.empty()) sm->SetFloat(s.badParam, s.bad);
    scene.Update(kDt);
    if (!s.badParam.empty()) sm->SetFloat(s.badParam, s.badParam == "by" ? 0.0f : 1.0f);
    Result r;
    r.afterBad = sm->Current();
    for (int i = 0; i < 120; ++i) {
        scene.Update(kDt);
        if (i == 59) r.norm60 = sm->NormalizedTime();
        if (r.exitFrame < 0 && sm->Current() != r.afterBad) r.exitFrame = i;
    }
    r.norm120 = sm->NormalizedTime();
    r.x = bone->transform->localPosition.x;
    r.state = sm->Current();
    return r;
}

void Print(const char* tag, const Setup& s, const Result& r) {
    std::printf("  %-8s %-40s x=%.9g (%a) norm60=%.9g norm120=%.9g (%a) state=%s (after bad frame %s, left at %d)\n",
                tag, s.label, r.x, r.x, r.norm60, r.norm120, r.norm120, r.state.c_str(),
                r.afterBad.c_str(), r.exitFrame);
}

bool Healthy(const Result& r) {
    return std::isfinite(r.x) && std::isfinite(r.norm60) && std::isfinite(r.norm120) && r.norm120 > r.norm60;
}

} // namespace

int main() {
    RUN_SUITE("anim_nan_speed");

    // Grok's five failing setups.
    const std::vector<Setup> bad = {
        {"plain state, speedParam = NaN",            Kind::Plain,  false, true,  "spd", kNaN},
        {"plain state, speedParam = Inf",            Kind::Plain,  false, true,  "spd", kInf},
        {"1D matchSpeed, blendParam = Inf",          Kind::Tree1D, true,  false, "bx",  kInf},
        {"1D matchSpeed, blendParam = 1e20",         Kind::Tree1D, true,  false, "bx",  1e20f},
        {"1D matchSpeed + speedParam, spd = NaN",    Kind::Tree1D, true,  true,  "spd", kNaN},
    };
    // Grok's controls: these already recovered before the fix.
    const std::vector<Setup> controls = {
        {"1D tree, blendParam = NaN",                Kind::Tree1D, false, false, "bx",  kNaN},
        {"1D tree, blendParam = Inf",                Kind::Tree1D, false, false, "bx",  kInf},
        {"2D tree, blendParam = NaN",                Kind::Tree2D, false, false, "bx",  kNaN},
        {"1D matchSpeed, blendParam = NaN",          Kind::Tree1D, true,  false, "bx",  kNaN},
    };

    std::printf("bad setups:\n");
    for (const Setup& s : bad) {
        const Result r = Run(s);
        Print("bad", s, r);
        CHECK(Healthy(r));
    }
    std::printf("controls:\n");
    for (const Setup& s : controls) {
        const Result r = Run(s);
        Print("control", s, r);
        CHECK(Healthy(r));
    }

    // Finite inputs: the fix must not change them. These lines are compared
    // before and after the fix (they print the exact bits with %a).
    std::printf("finite reference runs:\n");
    for (Setup s : bad) {
        s.bad = s.badParam == "spd" ? 1.75f : 3.0f;
        const Result r = Run(s);
        Print("finite", s, r);
        CHECK(Healthy(r));
    }
    for (Setup s : controls) {
        s.bad = 0.5f;
        const Result r = Run(s);
        Print("finite", s, r);
        CHECK(Healthy(r));
    }
    {   // A huge finite speed is a speed: it plays one wild frame. The bones
        // stay finite. (The normalized time jumps by about 1.7e28 and then can
        // no longer grow by a frame's worth; that is float precision, not NaN.)
        Setup s{"plain state, speedParam = 1e30", Kind::Plain, false, true, "spd", 1e30f};
        const Result r = Run(s);
        Print("huge", s, r);
        CHECK(std::isfinite(r.x));
        CHECK(std::isfinite(r.norm120));
    }
    {   // a state speed times a huge parameter that overflows to Inf
        Setup s{"plain state, speedParam = 3e38 (x speed 10)", Kind::Plain, false, true, "spd", 3e38f};
        const Result r = [&] {
            Scene sc; sc.physicsEnabled = false;
            GameObject* model = sc.CreateGameObject("Model");
            GameObject* bone = sc.CreateGameObject("Bone");
            bone->transform->SetParent(model->transform, false);
            auto* ma = model->AddComponent<ModelAnimator>();
            ma->autoPlay = false; ma->blendTime = 0.0f;
            ma->clips = {Ramp("idle", 0.0f), Ramp("walk", 10.0f), Ramp("run", 20.0f)};
            auto* sm = model->AddComponent<SM>();
            SM::State st = MakeState(s); st.speed = 10.0f;
            sm->states = {st};
            sc.Start();
            sm->SetFloat("spd", 0.1f);
            for (int i = 0; i < 10; ++i) sc.Update(kDt);
            sm->SetFloat("spd", 3e38f);
            sc.Update(kDt);
            sm->SetFloat("spd", 0.1f);
            Result out;
            for (int i = 0; i < 120; ++i) { sc.Update(kDt); if (i == 59) out.norm60 = sm->NormalizedTime(); }
            out.norm120 = sm->NormalizedTime();
            out.x = bone->transform->localPosition.x;
            out.state = sm->Current();
            return out;
        }();
        Print("overflow", s, r);
        CHECK(Healthy(r));
    }

    // A declared parameter whose saved default is NaN (a hand-edited scene)
    // never goes through SetFloat; the state speed still has to stay usable.
    {
        Setup s{"plain state, declared default spd = NaN", Kind::Plain, false, true, "", 0.0f};
        const Result r = Run(s, false, kNaN);
        Print("default", s, r);
        CHECK(Healthy(r));
    }

    // Exit time (1.5 cycles) after the reset: the graph must still leave the
    // state, and at the right time. A NaN clock made `t < exitTime` false, so
    // the transition fired on the bad frame itself (0.18 cycles in).
    std::printf("exit-time transition after the bad frame:\n");
    for (const Setup& s : bad) {
        const Result r = Run(s, true);
        Print("exit", s, r);
        CHECK(r.afterBad == "Loco");
        CHECK(r.state == "Done");
        if (std::isfinite(s.bad)) {
            // 1e20 is a real (huge) speed: that one frame jumps the cycle ahead,
            // so the exit comes earlier. It must still come, and not at once.
            CHECK(r.exitFrame >= 1 && r.exitFrame <= 85);
        } else {
            CHECK(r.exitFrame >= 70 && r.exitFrame <= 85);   // 1.5 - 11/60 cycles = ~79 frames
        }
        CHECK(std::isfinite(r.x));
        CHECK(std::isfinite(r.norm120));
    }

    TEST_MAIN_RESULT();
}
