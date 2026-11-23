// AddStates and the state/transition builders it uses (dive, sit, hits, one-shots, the hit layer), moved verbatim out of
// HumanoidActions.cpp (docs/engine/modularization_plan.md, S9).
#include "okay/Animation/HumanoidActions.hpp"
#include "HumanoidActionsInternal.hpp"
#include "okay/Animation/HumanoidMotion.hpp"
#include "okay/Components/AnimControl.hpp"
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/Seat.hpp"
#include "okay/Components/VehicleSeat.hpp"
#include "okay/Components/DodgeAction.hpp"
#include "okay/Components/FirstPersonController.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Net/NetOwnership.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Components/ThirdPersonController.hpp"
#include "okay/Components/Traversal.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Input/GameInput.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace okay {
namespace HumanoidActions {

using namespace humanoid_actions;

void ApplyTimes(AnimStateMachine& sm) {
    const StanceTimes& t = sm.stanceTimes;
    for (SM::State& st : sm.states) {
        if (st.name != "Death" && st.name != "DeadHold" && st.name != "Dive" && st.name != "DiveLand" &&
            st.name != "Swim") continue;
        for (SM::Transition& tr : st.transitions) {
            if (tr.to == "DeadHold") tr.blend = t.crouchProne;          // ends on the ground
            else if (tr.to == "GetUp" || tr.to == "Idle") tr.blend = t.proneStand;
            else if (tr.to == "Prone" || tr.to == "Crawl" || tr.to == "Crouch" || tr.to == "DiveLand")
                tr.blend = t.crouchProne;
            else if (tr.to == "Sit" || tr.to == "StandUp" || tr.to == "SitDown") tr.blend = t.standCrouch;
        }
    }
    for (SM::Transition& tr : sm.anyState) {
        if (tr.to == "Death" || tr.to == "HitHeavy" || tr.to == "Hit_Crouch" || tr.to == "Hit_Prone")
            tr.blend = t.standCrouch;
        else if (tr.to == "Swim") tr.blend = t.crouchProne;
    }
}

SM::Condition Cond(C c, const char* param, float value = 0.0f) {
    SM::Condition x;
    x.cond = c;
    x.param = param ? param : "";
    x.value = value;
    return x;
}

void AddEdge(SM::State& st, const SM& sm, const char* to, C cond, const char* param,
             std::vector<SM::Condition> extra, bool exitTime) {
    if (!HasState(sm, to)) return;
    SM::Transition t;
    t.to = to;
    t.cond = cond;
    t.param = param ? param : "";
    t.extra = std::move(extra);
    t.hasExitTime = exitTime;
    t.exitTime = 1.0f;
    t.blend = 0.2f;
    st.transitions.push_back(std::move(t));
}

void WireDive(SM::State& dive, const SM& sm) {
    dive.transitions.clear();
    // Die wins over "wait for the clip". Anything else is locked out (not interruptible).
    AddEdge(dive, sm, "Death", C::Trigger, "die", {}, false);
    AddEdge(dive, sm, "DiveLand", C::OnClipEnd, "", {Cond(C::BoolFalse, "grounded")}, true);
    AddEdge(dive, sm, "Crawl", C::OnClipEnd, "", {Cond(C::BoolTrue, "grounded"), Cond(C::FloatGreater, "speed", 0.35f)}, true);
    AddEdge(dive, sm, "Prone", C::OnClipEnd, "", {Cond(C::BoolTrue, "grounded")}, true);
    if (!HasState(sm, "Prone"))
        AddEdge(dive, sm, "Crouch", C::OnClipEnd, "", {}, true);
}

void WireDiveLand(SM::State& land, const SM& sm) {
    land.transitions.clear();
    AddEdge(land, sm, "Death", C::Trigger, "die", {}, false);
    AddEdge(land, sm, "Crawl", C::BoolTrue, "grounded", {Cond(C::FloatGreater, "speed", 0.35f)}, false);
    AddEdge(land, sm, "Prone", C::BoolTrue, "grounded", {}, false);
    if (!HasState(sm, "Prone"))
        AddEdge(land, sm, "Crouch", C::BoolTrue, "grounded", {}, false);
}

SM::State MakeDiveState(const char* name, const char* clip, bool loop) {
    SM::State s;
    s.name = name;
    s.clip = clip;
    s.loop = loop;
    s.ikWeight = 0.0f;
    s.fullBodyIkWeight = 0.0f;
    s.interruptible = false;
    return s;
}

SM::Motion ClipAt(const char* clip, float threshold) {
    SM::Motion m;
    m.clip = clip;
    m.threshold = threshold;
    return m;
}

void WireSitDown(SM::State& st, const SM& sm) {
    st.transitions.clear();
    if (!HasState(sm, "Sit")) return;
    SM::Transition hold;
    hold.to = "Sit";
    hold.cond = C::OnClipEnd;
    hold.hasExitTime = true;
    hold.exitTime = 1.0f;
    hold.blend = 0.15f;
    st.transitions.push_back(hold);
}

void WireSit(SM::State& st, const SM& sm) {
    st.transitions.clear();
    if (HasState(sm, "StandUp")) {
        SM::Transition up;
        up.to = "StandUp";
        up.cond = C::BoolFalse;
        up.param = "sitting";
        up.blend = 0.12f;
        st.transitions.push_back(up);
    } else if (HasState(sm, "Idle")) {
        SM::Transition up;
        up.to = "Idle";
        up.cond = C::BoolFalse;
        up.param = "sitting";
        up.blend = 0.2f;
        st.transitions.push_back(up);
    }
}

void WireStand(SM::State& st, const SM& sm) {
    st.transitions.clear();
    const char* to = HasState(sm, "Idle") ? "Idle" : (HasState(sm, "Crouch") ? "Crouch" : nullptr);
    if (!to) return;
    SM::Transition done;
    done.to = to;
    done.cond = C::OnClipEnd;
    done.hasExitTime = true;
    done.exitTime = 1.0f;
    done.blend = 0.2f;
    st.transitions.push_back(done);
}

SM::State MakeSitTree(const char* name, bool loop) {
    SM::State s;
    s.name = name;
    s.blendTree = true;
    s.blendDims = 1;
    s.blendParam = "sitType";
    s.loop = loop;
    s.ikWeight = 0.0f;
    s.lookWeight = 0.0f;
    s.fullBodyIkWeight = 1.0f;   // hands stay free for holding
    s.interruptible = true;      // die and jump can cut a sit short
    return s;
}

SM::Motion HitClip(const char* clip, float threshold) {
    SM::Motion m;
    m.clip = clip;
    m.threshold = threshold;
    return m;
}

void WireClipEnd(SM::State& st, const char* to) {
    st.transitions.clear();
    if (!to) return;
    SM::Transition done;
    done.to = to;
    done.cond = C::OnClipEnd;
    done.hasExitTime = true;
    done.exitTime = 1.0f;
    done.blend = 0.1f;
    st.transitions.push_back(done);
}

SM::Condition CondOf(C cond, const char* param) {
    SM::Condition c;
    c.cond = cond;
    c.param = param;
    return c;
}

void HitEntry(SM& sm, const char* to, std::vector<SM::Condition> extra) {
    if (HasAny(sm, to, "hit") || !HasState(sm, to)) return;
    SM::Transition go;
    go.to = to;
    go.cond = C::Trigger;
    go.param = "hit";
    go.blend = 0.1f;
    go.canInterruptSelf = true;
    go.interruption = SM::Transition::Interrupt::None;
    go.extra = std::move(extra);
    sm.anyState.push_back(std::move(go));
}

AnimStateMachine* EnsureHitLayer(AnimStateMachine& base) {
    if (!base.gameObject) return nullptr;
    for (AnimStateMachine* sm : base.gameObject->GetComponents<AnimStateMachine>())
        if (sm->layer > 0 && sm->maskBone == "Torso") return sm;
    AnimStateMachine* layer = base.gameObject->AddComponent<AnimStateMachine>();
    layer->layer = 1;
    for (AnimStateMachine* sm : base.gameObject->GetComponents<AnimStateMachine>())
        if (sm != layer && sm->layer >= layer->layer) layer->layer = sm->layer + 1;
    layer->maskBone = "Torso";
    layer->layerWeight = 0.0f;
    return layer;
}

void BindHitLayer(AnimStateMachine& base) {
    if (!base.gameObject) return;
    for (AnimStateMachine* sm : base.gameObject->GetComponents<AnimStateMachine>()) {
        if (sm->layer <= 0 || sm->maskBone != "Torso") continue;
        if (!HasState(*sm, "Hit") && !HasState(*sm, "Off")) continue;
        sm->onStateEnter = [sm](const std::string& name) {
            sm->layerWeight = (name == "Off") ? 0.0f : 1.0f;
        };
        if (sm->Current().empty() || sm->Current() == "Off") sm->layerWeight = 0.0f;
    }
}

SM::Motion AtClip(const char* clip, float threshold) {
    SM::Motion m; m.clip = clip; m.threshold = threshold; return m;
}
void EndAt(SM::State& st, const char* to) {
    st.transitions.clear();
    if (!to) return;
    SM::Transition done;
    done.to = to; done.cond = C::OnClipEnd; done.hasExitTime = true; done.exitTime = 1.0f; done.blend = 0.12f;
    st.transitions.push_back(done);
}

void AddOneShot(SM& sm, const char* name, const char* clip, float col, float ny, const char* to) {
    if (HasState(sm, name)) return;
    SM::State s;
    s.name = name; s.clip = clip; s.loop = false; s.ikWeight = 0.0f;
    // Fall / Jump Any-State would cut a vault the moment the body leaves the
    // ground. Own edges still run, including die.
    s.interruptible = false;
    s.nx = col; s.ny = ny;
    if (HasState(sm, "Death")) {
        SM::Transition die;
        die.to = "Death"; die.cond = C::Trigger; die.param = "die"; die.blend = 0.12f;
        die.interruption = SM::Transition::Interrupt::None;
        s.transitions.push_back(die);
    }
    if (to) {
        SM::Transition done;
        done.to = to; done.cond = C::OnClipEnd; done.hasExitTime = true; done.exitTime = 1.0f; done.blend = 0.15f;
        s.transitions.push_back(done);
    }
    sm.states.push_back(std::move(s));
}

void AddStates(AnimStateMachine& sm, const Character* body, bool force) {
    const int had = sm.actionsRev;
    const bool deathDue = force || had < 1;
    const bool diveDue = force || had < 2;
    const bool sitDue = force || had < 3;
    const bool hitDue = force || had < 4;
    const bool rollDue = force || had < 5;
    const bool vaultDue = force || had < 6;
    const bool swimDue = force || had < 7;
    const bool emoteDue = force || had < 8;
    const bool freshDeath = deathDue && !HasState(sm, "Death");
    const bool freshHold = deathDue && !HasState(sm, "DeadHold");
    float col = 40.0f;
    if (const SM::State* existing = FindMutable(sm, "Death")) col = existing->nx;
    else if (const SM::State* existing = FindMutable(sm, "DeadHold")) col = existing->nx;
    else {
        for (const SM::State& o : sm.states) col = std::max(col, o.nx);
        col += 220.0f;
    }
    if (freshDeath) {
        SM::State death = MakeDeathState();
        death.nx = col;
        death.ny = 40.0f;
        sm.states.push_back(std::move(death));
    }
    if (freshHold) {
        SM::State hold = MakeHoldState();
        hold.nx = col;
        hold.ny = 130.0f;
        sm.states.push_back(std::move(hold));
    }
    // Only fill transitions on states this call created. A saved Death keeps
    // the user's edges.
    if (freshDeath) if (SM::State* s = FindMutable(sm, "Death")) WireDeath(*s, sm);
    if (freshHold) if (SM::State* s = FindMutable(sm, "DeadHold")) WireHold(*s, sm);
    const bool freshDive = diveDue && !HasState(sm, "Dive");
    const bool freshLand = diveDue && !HasState(sm, "DiveLand");
    if (freshDive || freshLand) {
        float col = 40.0f;
        for (const SM::State& o : sm.states) col = std::max(col, o.nx);
        col += 220.0f;
        if (freshDive) {
            SM::State dive = MakeDiveState("Dive", "Dive", false);
            if (body && !body->clipDive.empty()) dive.clip = body->clipDive;
            dive.nx = col;
            dive.ny = 40.0f;
            sm.states.push_back(std::move(dive));
        }
        if (freshLand) {
            SM::State land = MakeDiveState("DiveLand", "DiveLand", false);
            land.nx = col;
            land.ny = 130.0f;
            sm.states.push_back(std::move(land));
        }
        if (freshDive) if (SM::State* s = FindMutable(sm, "Dive")) WireDive(*s, sm);
        if (freshLand) if (SM::State* s = FindMutable(sm, "DiveLand")) WireDiveLand(*s, sm);
        EnsureParam(sm, "dive", 2);
        EnsureParam(sm, "grounded", 1);
        EnsureParam(sm, "sprint", 1);
        EnsureParam(sm, "run", 1);
        auto anyDive = [&](const char* flag) {
            if (!HasState(sm, "Dive")) return;
            SM::Transition go;
            go.to = "Dive";
            go.cond = C::Trigger;
            go.param = "dive";
            go.blend = 0.12f;
            go.extra = {Cond(C::BoolTrue, flag), Cond(C::BoolTrue, "grounded")};
            // None so the state being left cannot cancel the blend. Dive itself
            // is not interruptible, so only its own die edge can cut it short.
            go.interruption = SM::Transition::Interrupt::None;
            sm.anyState.push_back(std::move(go));
        };
        // sprint OR run, and grounded. Extras are ANDed, so this is two edges.
        if (!HasAny(sm, "Dive", "dive")) { anyDive("sprint"); anyDive("run"); }
    }
    if (freshDeath || freshHold) {
        EnsureParam(sm, "deathDir", 0);
        EnsureParam(sm, "dead", 1);
        EnsureParam(sm, "die", 2);
        if (!HasAny(sm, "Death", "die")) {
            SM::Transition die;
            die.to = "Death";
            die.cond = C::Trigger;
            die.param = "die";
            die.blend = 0.2f;
            // None, not CurrentState: Death is already current while this blend
            // runs, and CurrentState would let the state being left (Walk, Jump)
            // cancel the fall. interruptible = false stops Any State after that.
            die.interruption = SM::Transition::Interrupt::None;
            sm.anyState.push_back(std::move(die));
        }
    }
    const bool freshDown = sitDue && !HasState(sm, "SitDown");
    const bool freshSit = sitDue && !HasState(sm, "Sit");
    const bool freshUp = sitDue && !HasState(sm, "StandUp");
    if (freshDown || freshSit || freshUp) {
        float col = 40.0f;
        for (const SM::State& o : sm.states) col = std::max(col, o.nx);
        col += 220.0f;
        if (freshDown) {
            SM::State s = MakeSitTree("SitDown", false);
            s.motions = {ClipAt("SitDown_Chair", 0.0f), ClipAt("SitDown_Ground", 1.0f)};
            s.nx = col; s.ny = 40.0f;
            sm.states.push_back(std::move(s));
        }
        if (freshSit) {
            SM::State s = MakeSitTree("Sit", true);
            s.enterEvent = "sit";
            s.motions = {ClipAt("Sit_Chair", 0.0f), ClipAt("Sit_Ground", 1.0f), ClipAt("Sit_Ground_Knees", 2.0f)};
            s.nx = col; s.ny = 130.0f;
            sm.states.push_back(std::move(s));
        }
        if (freshUp) {
            SM::State s = MakeSitTree("StandUp", false);
            s.enterEvent = "stand";
            s.motions = {ClipAt("StandUp_Chair", 0.0f), ClipAt("StandUp_Ground", 1.0f)};
            s.nx = col; s.ny = 220.0f;
            sm.states.push_back(std::move(s));
        }
        if (freshDown) if (SM::State* s = FindMutable(sm, "SitDown")) WireSitDown(*s, sm);
        if (freshSit) if (SM::State* s = FindMutable(sm, "Sit")) WireSit(*s, sm);
        if (freshUp) if (SM::State* s = FindMutable(sm, "StandUp")) WireStand(*s, sm);
        EnsureParam(sm, "sitting", 1);
        EnsureParam(sm, "sitType", 0);
        EnsureParam(sm, "sit", 2);
        if (!HasAny(sm, "SitDown", "sit") && HasState(sm, "SitDown")) {
            SM::Transition go;
            go.to = "SitDown";
            go.cond = C::Trigger;
            go.param = "sit";
            go.blend = 0.2f;
            // None so Walk cannot cancel the blend. interruptible stays true, so die still can.
            go.interruption = SM::Transition::Interrupt::None;
            sm.anyState.push_back(std::move(go));
        }
    }
    if (hitDue) {
        const bool freshHeavy = !HasState(sm, "HitHeavy");
        const bool freshCrouch = !HasState(sm, "Hit_Crouch");
        const bool freshProne = !HasState(sm, "Hit_Prone");
        float col = 40.0f;
        for (const SM::State& o : sm.states) col = std::max(col, o.nx);
        col += 220.0f;
        if (freshHeavy) {
            SM::State s;
            s.name = "HitHeavy";
            s.blendTree = true;
            s.blendDims = 1;
            s.blendParam = "hitDir";
            s.loop = false;
            s.ikWeight = 0.0f;
            s.fullBodyIkWeight = 0.0f;
            s.interruptible = true;
            s.motions = {HitClip("Hit_Heavy_F", 0.0f), HitClip("Hit_Heavy_B", 1.0f)};
            s.nx = col; s.ny = 40.0f;
            sm.states.push_back(std::move(s));
        }
        if (freshCrouch) {
            SM::State s;
            s.name = "Hit_Crouch";
            s.clip = "Hit_Crouch";
            s.loop = false;
            s.ikWeight = 0.0f;
            s.interruptible = true;
            s.nx = col; s.ny = 130.0f;
            sm.states.push_back(std::move(s));
        }
        if (freshProne) {
            SM::State s;
            s.name = "Hit_Prone";
            s.clip = "Hit_Prone";
            s.loop = false;
            s.ikWeight = 0.0f;
            s.interruptible = true;
            s.nx = col; s.ny = 220.0f;
            sm.states.push_back(std::move(s));
        }
        if (freshHeavy) if (SM::State* s = FindMutable(sm, "HitHeavy"))
            WireClipEnd(*s, HasState(sm, "Idle") ? "Idle" : nullptr);
        if (freshCrouch) if (SM::State* s = FindMutable(sm, "Hit_Crouch"))
            WireClipEnd(*s, HasState(sm, "Crouch") ? "Crouch" : (HasState(sm, "Idle") ? "Idle" : nullptr));
        if (freshProne) if (SM::State* s = FindMutable(sm, "Hit_Prone"))
            WireClipEnd(*s, HasState(sm, "Prone") ? "Prone" : (HasState(sm, "Idle") ? "Idle" : nullptr));
        EnsureParam(sm, "hit", 2);
        EnsureParam(sm, "hitDir", 0);
        EnsureParam(sm, "hitHeavy", 1);
        bool haveScale = false;
        for (const SM::Param& p : sm.params) if (p.name == "staggerScale") haveScale = true;
        if (!haveScale) {
            sm.params.push_back({"staggerScale", 0});
            sm.SetFloat("staggerScale", 1.0f);
        }
        // Prone and crouch before the standing heavy edge, so the first match wins.
        HitEntry(sm, "Hit_Prone", {CondOf(C::BoolTrue, "prone")});
        HitEntry(sm, "Hit_Crouch", {CondOf(C::BoolTrue, "crouch"), CondOf(C::BoolFalse, "prone")});
        HitEntry(sm, "HitHeavy", {CondOf(C::BoolTrue, "hitHeavy"), CondOf(C::BoolFalse, "crouch"), CondOf(C::BoolFalse, "prone")});

        if (AnimStateMachine* layer = EnsureHitLayer(sm)) {
            const bool freshOff = !HasState(*layer, "Off");
            const bool freshHit = !HasState(*layer, "Hit");
            if (freshOff) {
                SM::State off;
                off.name = "Off";
                off.clip = "Hit_Off";
                off.loop = true;
                off.nx = 40.0f; off.ny = 40.0f;
                layer->states.push_back(std::move(off));
                if (layer->entry.empty()) layer->entry = "Off";
            }
            if (freshHit) {
                SM::State s;
                s.name = "Hit";
                s.blendTree = true;
                s.blendDims = 1;
                s.blendParam = "hitDir";
                s.loop = false;
                s.interruptible = true;
                s.motions = {HitClip("Hit_F", 0.0f), HitClip("Hit_B", 1.0f), HitClip("Hit_L", 2.0f), HitClip("Hit_R", 3.0f)};
                s.nx = 40.0f; s.ny = 140.0f;
                layer->states.push_back(std::move(s));
            }
            if (freshHit) if (SM::State* s = FindMutable(*layer, "Hit"))
                WireClipEnd(*s, HasState(*layer, "Off") ? "Off" : nullptr);
            HitEntry(*layer, "Hit", {CondOf(C::BoolFalse, "hitHeavy"), CondOf(C::BoolFalse, "crouch"), CondOf(C::BoolFalse, "prone")});
            BindHitLayer(sm);
        }
    }
    if (rollDue) {
        float col = 40.0f;
        for (const SM::State& o : sm.states) col = std::max(col, o.nx);
        col += 220.0f;
        const char* home = HasState(sm, "Idle") ? "Idle" : nullptr;
        if (!HasState(sm, "Roll")) {
            SM::State s;
            s.name = "Roll"; s.clip = "Roll_F"; s.loop = false; s.ikWeight = 0.0f; s.interruptible = true;
            s.nx = col; s.ny = 40.0f;
            sm.states.push_back(std::move(s));
            if (SM::State* st = FindMutable(sm, "Roll")) EndAt(*st, home);
        }
        if (!HasState(sm, "Dodge")) {
            SM::State s;
            s.name = "Dodge"; s.blendTree = true; s.blendDims = 1; s.blendParam = "dodgeDir";
            s.loop = false; s.ikWeight = 0.0f; s.interruptible = true;
            s.motions = {AtClip("Roll_F", 0.0f), AtClip("Dodge_B", 1.0f), AtClip("Dodge_L", 2.0f), AtClip("Dodge_R", 3.0f)};
            s.nx = col; s.ny = 130.0f;
            sm.states.push_back(std::move(s));
            if (SM::State* st = FindMutable(sm, "Dodge")) EndAt(*st, home);
        }
        if (!HasState(sm, "Slide")) {
            SM::State s;
            s.name = "Slide"; s.clip = "Slide"; s.loop = true; s.ikWeight = 0.0f; s.interruptible = true;
            s.nx = col; s.ny = 220.0f;
            sm.states.push_back(std::move(s));
        }
        if (!HasState(sm, "SlideEnd")) {
            SM::State s;
            s.name = "SlideEnd"; s.clip = "SlideEnd"; s.loop = false; s.ikWeight = 0.0f; s.interruptible = true;
            s.nx = col; s.ny = 310.0f;
            sm.states.push_back(std::move(s));
            if (SM::State* st = FindMutable(sm, "SlideEnd")) {
                st->transitions.clear();
                if (HasState(sm, "Run")) {
                    SM::Transition run;
                    run.to = "Run"; run.cond = C::OnClipEnd; run.hasExitTime = true; run.exitTime = 1.0f;
                    run.blend = 0.12f; run.extra.push_back({C::BoolTrue, "sprint", 0.0f});
                    run.extra.push_back({C::BoolFalse, "crouch", 0.0f});
                    st->transitions.push_back(run);
                }
                if (const char* to = HasState(sm, "Crouch") ? "Crouch" : home) {
                    SM::Transition done;
                    done.to = to; done.cond = C::OnClipEnd; done.hasExitTime = true; done.exitTime = 1.0f; done.blend = 0.12f;
                    st->transitions.push_back(done);
                }
            }
        }
        if (SM::State* st = FindMutable(sm, "Slide")) if (st->transitions.empty() && HasState(sm, "SlideEnd")) {
            SM::Transition up;
            up.to = "SlideEnd"; up.cond = C::BoolFalse; up.param = "slide"; up.blend = 0.1f;
            st->transitions.push_back(up);
        }
        EnsureParam(sm, "dodgeDir", 0);
        EnsureParam(sm, "roll", 2);
        EnsureParam(sm, "dodge", 2);
        EnsureParam(sm, "slide", 1);
        auto entry = [&](const char* to, const char* param, C cond) {
            if (HasAny(sm, to, param) || !HasState(sm, to)) return;
            SM::Transition go;
            go.to = to; go.cond = cond; go.param = param; go.blend = 0.1f;
            go.canInterruptSelf = (cond == C::Trigger);
            go.interruption = SM::Transition::Interrupt::None;
            sm.anyState.push_back(std::move(go));
        };
        entry("Roll", "roll", C::Trigger);
        entry("Dodge", "dodge", C::Trigger);
        entry("Slide", "slide", C::BoolTrue);
    }
    if (vaultDue) {
        float col = 40.0f;
        for (const SM::State& o : sm.states) col = std::max(col, o.nx);
        col += 220.0f;
        const char* home = HasState(sm, "Idle") ? "Idle" : nullptr;
        const char* drop = HasState(sm, "Fall") ? "Fall" : home;
        AddOneShot(sm, "Vault", "Vault", col, 40.0f, home);
        AddOneShot(sm, "MantleLow", "Mantle_Low", col, 130.0f, home);
        AddOneShot(sm, "MantleHigh", "Mantle_High", col, 220.0f, home);
        AddOneShot(sm, "LedgeGrab", "LedgeGrab", col, 310.0f, HasState(sm, "Hang") ? "Hang" : home);
        AddOneShot(sm, "ClimbUp", "ClimbUp", col, 400.0f, home);
        AddOneShot(sm, "LetGo", "LetGo", col, 490.0f, drop);
        if (!HasState(sm, "HangShimmy")) {
            SM::State s;
            s.name = "HangShimmy"; s.blendTree = true; s.blendDims = 1; s.blendParam = "shimmy";
            s.loop = true; s.ikWeight = 0.0f; s.interruptible = false;
            SM::Motion l; l.clip = "Hang_Shimmy_L"; l.threshold = -1.0f;
            SM::Motion r; r.clip = "Hang_Shimmy_R"; r.threshold = 1.0f;
            s.motions = {l, r};
            s.nx = col; s.ny = 580.0f;
            SM::Transition back;
            back.to = HasState(sm, "Hang") ? "Hang" : (home ? home : "");
            if (HasState(sm, "Death")) {
                SM::Transition die;
                die.to = "Death"; die.cond = C::Trigger; die.param = "die"; die.blend = 0.12f;
                die.interruption = SM::Transition::Interrupt::None;
                s.transitions.push_back(die);
            }
            if (!back.to.empty()) {
                back.cond = C::FloatLess; back.param = "shimmy"; back.value = 0.2f;
                SM::Condition neg; neg.cond = C::FloatGreater; neg.param = "shimmy"; neg.value = -0.2f;
                back.extra.push_back(neg);
                s.transitions.push_back(back);
            }
            sm.states.push_back(std::move(s));
        }
        if (SM::State* hang = FindMutable(sm, "Hang")) {
            bool has = false;
            for (const SM::Transition& t : hang->transitions) if (t.param == "hanging") has = true;
            if (!has && drop) {
                SM::Transition rel;
                rel.to = drop; rel.cond = C::BoolFalse; rel.param = "hanging"; rel.blend = 0.12f;
                hang->transitions.push_back(rel);
                // Fall's Any-State would yo-yo with `hanging` while the feet are off the floor.
                hang->interruptible = false;
                bool hasDie = false;
                for (const SM::Transition& t : hang->transitions) if (t.param == "die") hasDie = true;
                if (!hasDie && HasState(sm, "Death")) {
                    SM::Transition die;
                    die.to = "Death"; die.cond = C::Trigger; die.param = "die"; die.blend = 0.12f;
                    die.interruption = SM::Transition::Interrupt::None;
                    hang->transitions.insert(hang->transitions.begin(), die);
                }
            }
        }
        EnsureParam(sm, "vault", 2);
        EnsureParam(sm, "mantle", 2);
        EnsureParam(sm, "ledgeGrab", 2);
        EnsureParam(sm, "climbUp", 2);
        EnsureParam(sm, "letGo", 2);
        EnsureParam(sm, "hanging", 1);
        EnsureParam(sm, "shimmy", 0);
        auto trig = [&](const char* to, const char* param) {
            if (HasAny(sm, to, param) || !HasState(sm, to)) return;
            SM::Transition go;
            go.to = to; go.cond = C::Trigger; go.param = param; go.blend = 0.12f;
            go.interruption = SM::Transition::Interrupt::None;
            sm.anyState.push_back(std::move(go));
        };
        trig("Vault", "vault");
        trig("MantleLow", "mantle");
        trig("LedgeGrab", "ledgeGrab");
        trig("ClimbUp", "climbUp");
        trig("LetGo", "letGo");
        if (!HasAny(sm, "Hang", "hanging") && HasState(sm, "Hang")) {
            SM::Transition go;
            go.to = "Hang"; go.cond = C::BoolTrue; go.param = "hanging"; go.blend = 0.12f;
            go.interruption = SM::Transition::Interrupt::None;
            sm.anyState.push_back(std::move(go));
        }
    }
    const bool freshSwim = swimDue && !HasState(sm, "Swim");
    if (freshSwim) {
        float scol = 40.0f;
        for (const SM::State& o : sm.states) scol = std::max(scol, o.nx);
        scol += 220.0f;
        SM::State swim = MakeSwimState();
        swim.nx = scol;
        swim.ny = 40.0f;
        sm.states.push_back(std::move(swim));
        if (SM::State* s = FindMutable(sm, "Swim")) WireSwim(*s, sm);
        EnsureParam(sm, "swimSpeed", 0);
        EnsureParam(sm, "swimming", 1);
        if (!HasAny(sm, "Swim", "swimming")) {
            SM::Transition go;
            go.to = "Swim";
            go.cond = C::BoolTrue;
            go.param = "swimming";
            go.blend = 0.25f;
            // None so Walk cannot cancel the blend. Swim itself is not
            // interruptible, so only its die / leave edges run after that.
            go.interruption = SM::Transition::Interrupt::None;
            // Before jump and fall, so a body already in the air still enters.
            sm.anyState.insert(sm.anyState.begin(), std::move(go));
        }
    }
    bool freshEmote = false;
    float ecol = 40.0f;
    for (const SM::State& o : sm.states) ecol = std::max(ecol, o.nx);
    ecol += 220.0f;
    int row = 0;
    for (int i = 0; i < 3; ++i) {
        if (!emoteDue || HasState(sm, kEmoteNames[i])) continue;
        freshEmote = true;
        SM::State em = MakeEmoteState(kEmoteNames[i]);
        em.nx = ecol;
        em.ny = 40.0f + 90.0f * (float)row;
        sm.states.push_back(std::move(em));
        if (SM::State* s = FindMutable(sm, kEmoteNames[i])) WireEmote(*s, sm);
        AddEmoteAny(sm, kEmoteNames[i], (float)i);
        ++row;
    }
    if (freshEmote) {
        EnsureParam(sm, "emoteId", 3);
        EnsureParam(sm, "emote", 2);
    }
    if (sm.actionsRev < kActionsRev) sm.actionsRev = kActionsRev;
    ApplyTimes(sm);
    BindHitLayer(sm);
}
} // namespace HumanoidActions

} // namespace okay
