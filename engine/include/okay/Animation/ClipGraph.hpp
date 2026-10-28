#pragma once
// ---------------------------------------------------------------------------
// ClipGraph — an Animator graph for an imported model that brings its own clips.
// The built-in humanoid gets HumanoidLibrary::DefaultGraph; a model played by
// its ModelAnimator gets this one, built from the clips the importer mapped
// (idle / walk / run, jump / fall / land, crouch, crouch walk, prone, crawl and
// get up). It takes the parameters the player controllers send (AnimLink):
// speed, grounded, verticalSpeed, crouch, prone and the jump trigger, so a
// player set up with this graph moves exactly as with "drive by movement",
// except that every state and transition is now in the Animator to edit.
// ---------------------------------------------------------------------------
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Scene/GameObject.hpp"
#include <string>

namespace okay {
namespace ClipGraph {

/// Replace `sm`'s states with the locomotion graph for `ma`'s clips. Returns how
/// many states it made (0: the model has no clips, and `sm` is left alone).
inline int Build(AnimStateMachine& sm, const ModelAnimator& ma) {
    using SM = AnimStateMachine;
    using C = SM::Cond;
    if (ma.clips.empty()) return 0;
    auto clip = [&](const std::string& n) { return !n.empty() && ma.FindClip(n) >= 0 ? n : std::string(); };
    const std::string idle = !clip(ma.idleClip).empty() ? ma.idleClip : ma.clips.front().name;
    std::string walk = clip(ma.walkClip), run = clip(ma.runClip);
    if (walk.empty()) walk = run;
    if (run == walk) run.clear();
    const std::string jump = clip(ma.jumpClip), fall = clip(ma.fallClip), land = clip(ma.landClip);
    const std::string crouch = clip(ma.crouchClip), crouchWalk = clip(ma.crouchWalkClip);
    const std::string prone = clip(ma.proneClip), crawl = clip(ma.crawlClip), getUp = clip(ma.getUpClip);
    // The blend is fully "walk" at the walk threshold and fully "run" at the run
    // threshold: the speeds the importer measured on the player's controller.
    const float walkAt = ma.walkThreshold > 0.0f ? ma.walkThreshold : 1.5f;
    const float runAt = ma.runThreshold > walkAt ? ma.runThreshold : walkAt * 3.0f;

    sm.states.clear(); sm.anyState.clear();
    auto param = [&](const char* nm, int type) {
        for (const auto& p : sm.params) if (p.name == nm) return;
        sm.params.push_back({nm, type});
    };
    param("speed", 0); param("verticalSpeed", 0); param("grounded", 1);
    param("crouch", 1); param("prone", 1); param("jump", 2);

    auto tr = [](const std::string& to, C c, const char* p, float v, float blend) {
        SM::Transition t; t.to = to; t.cond = c; t.param = p; t.value = v; t.blend = blend; return t;
    };
    auto also = [](SM::Transition t, C c, const char* p, float v = 0.0f) { t.extra.push_back({c, p, v}); return t; };
    // A still clip and a moving one blended by speed (or just the clip that exists).
    auto moves = [&](const char* name, const std::string& still, const std::string& go, const std::string& fast,
                     float goAt, float nx, float ny) {
        SM::State st; st.name = name; st.nx = nx; st.ny = ny;
        const std::string& base = !still.empty() ? still : go;
        if (go.empty() || go == base) { st.clip = base; return st; }
        st.blendTree = true; st.blendParam = "speed";
        st.motions.push_back({base, 0.0f});
        st.motions.push_back({go, goAt});
        if (!fast.empty()) st.motions.push_back({fast, runAt});
        return st;
    };
    const bool hasCrouch = !crouch.empty() || !crouchWalk.empty();
    const bool hasProne = !prone.empty() || !crawl.empty();

    // Crouched and lying down the body moves slower: their moving clip is fully
    // on at half the walk speed.
    SM::State loco = moves("Locomotion", idle, walk, run, walkAt, 20, 140);
    if (hasProne) loco.transitions.push_back(tr("Prone", C::BoolTrue, "prone", 0, 0.3f));
    if (hasCrouch) loco.transitions.push_back(tr("Crouch", C::BoolTrue, "crouch", 0, 0.25f));
    sm.states.push_back(loco);

    if (hasCrouch) {
        SM::State st = moves("Crouch", crouch, crouchWalk, std::string(), walkAt * 0.5f, 20, 280);
        if (hasProne) st.transitions.push_back(tr("Prone", C::BoolTrue, "prone", 0, 0.3f));
        st.transitions.push_back(tr("Locomotion", C::BoolFalse, "crouch", 0, 0.25f));
        sm.states.push_back(st);
    }
    if (hasProne) {
        SM::State st = moves("Prone", prone, crawl, std::string(), walkAt * 0.5f, 260, 280);
        st.ikWeight = 0.0f;           // lying down: no foot planting
        st.fullBodyIkWeight = 1.0f;   // the hands still hold and reach
        if (hasCrouch) st.transitions.push_back(also(tr("Crouch", C::BoolFalse, "prone", 0, 0.3f), C::BoolTrue, "crouch"));
        st.transitions.push_back(tr(getUp.empty() ? "Locomotion" : "GetUp", C::BoolFalse, "prone", 0, 0.2f));
        sm.states.push_back(st);
        if (!getUp.empty()) {
            SM::State g; g.name = "GetUp"; g.clip = getUp; g.loop = false; g.nx = 500; g.ny = 280;
            g.transitions.push_back(tr("Prone", C::BoolTrue, "prone", 0, 0.2f));
            g.transitions.push_back(tr("Locomotion", C::OnClipEnd, "", 0, 0.2f));
            sm.states.push_back(g);
        }
    }

    // In the air: a jump from anywhere, a fall off a ledge, and the landing.
    const std::string afterAir = land.empty() ? "Locomotion" : "Land";
    if (!jump.empty()) {
        SM::State j; j.name = "Jump"; j.clip = jump; j.loop = false; j.nx = 260; j.ny = 20;
        j.ikWeight = 0.0f; j.enterEvent = "jump";
        if (!fall.empty()) j.transitions.push_back(tr("Fall", C::OnClipEnd, "", 0, 0.15f));
        SM::Transition down = tr(afterAir, C::BoolTrue, "grounded", 0, 0.1f);
        down.hasExitTime = true; down.exitTime = 0.4f;
        j.transitions.push_back(down);
        sm.states.push_back(j);
        sm.anyState.push_back(tr("Jump", C::Trigger, "jump", 0, 0.1f));
    }
    if (!fall.empty()) {
        SM::State f; f.name = "Fall"; f.clip = fall; f.nx = 500; f.ny = 20; f.ikWeight = 0.0f;
        f.transitions.push_back(tr(afterAir, C::BoolTrue, "grounded", 0, 0.1f));
        sm.states.push_back(f);
        const SM::Transition off = also(tr("Fall", C::BoolFalse, "grounded", 0, 0.2f), C::FloatLess, "verticalSpeed", -2.0f);
        sm.states[0].transitions.push_back(off);
        if (hasCrouch) sm.states[1].transitions.push_back(off);
    }
    if (!land.empty()) {
        SM::State l; l.name = "Land"; l.clip = land; l.loop = false; l.nx = 500; l.ny = 140;
        l.enterEvent = "land";
        l.transitions.push_back(tr("Locomotion", C::OnClipEnd, "", 0, 0.2f));
        SM::Transition early = tr("Locomotion", C::FloatGreater, "speed", 1.0f, 0.2f);
        early.hasExitTime = true; early.exitTime = 0.3f;
        l.transitions.push_back(early);
        sm.states.push_back(l);
    }
    sm.entry = "Locomotion";
    return (int)sm.states.size();
}

/// The model's base-layer Animator: the one already on `go`, else a new one built
/// from its ModelAnimator's clips (which then stops picking clips by itself).
/// Null when `go` has no ModelAnimator with clips.
inline AnimStateMachine* Ensure(GameObject* go) {
    ModelAnimator* ma = go ? go->GetComponent<ModelAnimator>() : nullptr;
    if (!ma || ma->clips.empty()) return nullptr;
    for (AnimStateMachine* sm : go->GetComponents<AnimStateMachine>())
        if (sm->layer == 0) return sm;
    AnimStateMachine* sm = go->AddComponent<AnimStateMachine>();
    Build(*sm, *ma);
    ma->driveByMovement = false;   // the graph picks the clips now
    return sm;
}

} // namespace ClipGraph
} // namespace okay
