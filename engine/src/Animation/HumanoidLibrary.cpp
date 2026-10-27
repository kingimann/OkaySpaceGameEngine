#include "okay/Animation/HumanoidLibrary.hpp"
#include "okay/Animation/HumanoidActions.hpp"
#include "okay/Animation/HumanoidMotion.hpp"
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Scene/GameObject.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>

namespace okay {
namespace HumanoidLibrary {

namespace {
constexpr float kPi = 3.14159265f;

// `keys` evenly spaced keys over `length` seconds of `state`, looping back to
// the first; `at(u)` sets the motion inputs at cycle fraction u.
template <class F>
AnimClip Bake(const std::string& name, int state, float length, int keys, F at) {
    AnimClip c;
    c.name = name;
    c.loop = true;
    c.length = length;
    c.interp = AnimInterp::Cubic;   // a curve through the sampled poses: no kink at each key
    for (int k = 0; k < keys; ++k) {
        const float u = (float)k / (float)keys;
        HumanoidMotion::Inputs in = at(u);
        AnimKey key;
        key.time = u * length;
        HumanoidMotion::Pose(state, key.time, in, key.pose);
        key.root = Vec3{0.0f, HumanoidMotion::HipY(state, in), 0.0f};
        if (key.root.y != 0.0f) c.hasRoot = true;
        c.keys.push_back(std::move(key));
    }
    return c;
}

// A pose that holds (or repeats with period `period`, sampled `keys` times).
AnimClip Hold(const std::string& name, int state, float period = 0.0f, int keys = 1) {
    return Bake(name, state, period, period > 0.0f ? keys : 1, [](float) { return HumanoidMotion::Inputs{}; });
}

// A one-shot through poses at times (seconds): 1 = standing, 7 = lying prone,
// 10 = on hands and knees, 11 = kneeling.
AnimClip Through(const std::string& name, const std::vector<std::pair<float, int>>& keys) {
    AnimClip c;
    c.name = name;
    c.loop = false;
    c.length = keys.back().first;
    c.hasRoot = true;
    for (const auto& kv : keys) {
        AnimKey k;
        k.time = kv.first;
        float y;
        if (kv.second == 10 || kv.second == 11) y = HumanoidMotion::KneelPose(kv.second - 10, k.pose);
        else {
            HumanoidMotion::Pose(kv.second, 0.0f, HumanoidMotion::Inputs{}, k.pose);
            y = HumanoidMotion::HipY(kv.second, HumanoidMotion::Inputs{});
        }
        k.root = Vec3{0.0f, y, 0.0f};
        c.keys.push_back(std::move(k));
    }
    return c;
}

std::string Num(float v) {
    char b[16];
    std::snprintf(b, sizeof(b), "%g", v);
    return b;
}

// One stride cycle of a gait at `speed` toward `dirDeg` (0 = ahead, + = right).
// Past the side the body backpedals: the stride runs backwards with the hips
// turned the other way, as the live gait does.
Entry Gait(const char* base, int state, float speed, int dir8) {
    static const char* const kDir[8] = {"F", "FR", "R", "BR", "B", "BL", "L", "FL"};
    const float a = dir8 <= 4 ? dir8 * 45.0f : (dir8 - 8) * 45.0f;   // -135 .. 180
    const bool back = std::fabs(a) > 90.0f;
    const float yaw = back ? (a > 0.0f ? a - 180.0f : a + 180.0f) : a;
    // A fast walk is baked in the run's style (a jog), as the live body plays it.
    const float runW = state == 3 ? 1.0f : (state == 2 ? HumanoidMotion::JogWeight(speed) : 0.0f);
    const float len = 1.0f / HumanoidMotion::CycleFreq(state, speed, runW);
    Entry e;
    e.state = state; e.speed = speed;
    e.x = speed * std::sin(a * kPi / 180.0f);
    e.z = speed * std::cos(a * kPi / 180.0f);
    e.clip = Bake(std::string(base) + "_" + Num(speed) + "_" + kDir[dir8], state, len, 24, [&](float u) {
        HumanoidMotion::Inputs in;
        in.speed = speed;
        in.phase = back ? 1.0f - u : u;
        in.runW = runW;
        in.travelYaw = yaw;
        in.lowerYaw = yaw < -75.0f ? -75.0f : (yaw > 75.0f ? 75.0f : yaw);
        return in;
    });
    return e;
}

std::vector<Entry> BakeAll() {
    std::vector<Entry> out;
    auto add = [&](AnimClip c, int state) { Entry e; e.clip = std::move(c); e.state = state; out.push_back(std::move(e)); };
    // Idle: 16 breaths, 2 weight shifts and 3 glances, so the loop closes.
    add(Hold("Idle", 1, 16.0f * kPi / 1.8f, 224), 1);
    for (float v : {0.3f, 1.0f, 2.0f, 3.0f, 4.5f}) for (int d = 0; d < 8; ++d) out.push_back(Gait("Walk", 2, v, d));
    for (float v : {4.0f, 5.5f, 7.0f, 8.5f})       for (int d = 0; d < 8; ++d) out.push_back(Gait("Run", 3, v, d));
    for (float v : {0.3f, 1.0f, 2.0f})             for (int d = 0; d < 8; ++d) out.push_back(Gait("CrouchWalk", 17, v, d));
    add(Hold("Crouch", 6, 2.0f * kPi / 2.2f, 24), 6);
    add(Hold("Prone", 7), 7);
    {   // the crawl at its natural pace, four ways (the Animator plays it as fast as the body moves)
        static const char* const kDir[4] = {"", "_R", "_B", "_L"};   // forward keeps the name scenes saved since 1b use
        const float v = HumanoidMotion::NaturalSpeed(7);
        for (int d = 0; d < 4; ++d) {
            const bool back = d == 2;
            const float side = d == 1 ? 90.0f : (d == 3 ? -90.0f : 0.0f);
            Entry e;
            e.state = 7; e.speed = v;
            e.x = d == 1 ? v : (d == 3 ? -v : 0.0f);
            e.z = d == 0 ? v : (back ? -v : 0.0f);
            e.clip = Bake(std::string("Crawl") + kDir[d], 7, 1.0f / HumanoidMotion::CycleFreq(7, v, 0.0f), 24, [&](float u) {
                HumanoidMotion::Inputs in; in.speed = v; in.phase = back ? 1.0f - u : u; in.travelYaw = side; return in;
            });
            out.push_back(std::move(e));
        }
    }
    // Getting up from prone through hands-and-knees and a kneel, and dropping
    // down the same way (one second each; the Animator times them).
    add(Through("GetUp", {{0.0f, 7}, {0.3f, 10}, {0.62f, 11}, {1.0f, 1}}), 0);
    add(Through("Drop",  {{0.0f, 1}, {0.35f, 11}, {0.65f, 10}, {1.0f, 7}}), 0);
    add(Hold("Jump", 5), 5);
    add(Hold("Fall", 18), 18);
    {   // land: the knees give and spring back over 0.28 s
        AnimClip c = Bake("Land", 1, 0.28f, 8, [](float u) {
            HumanoidMotion::Inputs in; in.landK = std::sin(kPi * u); return in;
        });
        c.loop = false;
        AnimKey last; last.time = 0.28f;
        HumanoidMotion::Pose(1, last.time, HumanoidMotion::Inputs{}, last.pose);
        c.keys.push_back(std::move(last));
        add(std::move(c), 1);
    }
    add(Hold("Hang", 19, 2.0f * kPi / 1.6f, 24), 19);
    add(Hold("Wave", 4, 2.0f * kPi / 8.0f, 16), 4);
    add(Hold("Point", 8), 8);
    add(Hold("Clap", 9, 2.0f * kPi / 7.0f, 16), 9);
    add(Hold("ThumbsUp", 10), 10);
    add(Hold("Salute", 11), 11);
    add(Hold("WaveBoth", 12, 2.0f * kPi / 6.0f, 16), 12);
    add(Hold("Cheer", 13, 2.0f * kPi / 6.0f, 16), 13);
    add(Hold("Sad", 14, 2.0f * kPi / 1.4f, 24), 14);
    add(Hold("Angry", 15, 2.0f * kPi / 5.0f, 16), 15);
    add(Hold("Think", 16), 16);
    HumanoidActions::Bake(out);
    return out;
}
} // namespace

const std::vector<Entry>& Entries() {
    static const std::vector<Entry> lib = BakeAll();
    return lib;
}

const std::vector<std::string>& Names() {
    static const std::vector<std::string> names = [] {
        std::vector<std::string> n;
        for (const Entry& e : Entries()) n.push_back(e.clip.name);
        std::sort(n.begin(), n.end());
        return n;
    }();
    return names;
}

const AnimClip* Find(const std::string& name) {
    static const std::unordered_map<std::string, const AnimClip*> byName = [] {
        std::unordered_map<std::string, const AnimClip*> m;
        for (const Entry& e : Entries()) m[e.clip.name] = &e.clip;
        return m;
    }();
    auto it = byName.find(name);
    return it == byName.end() ? nullptr : it->second;
}

namespace {
using SM = AnimStateMachine;
using C = SM::Cond;

SM::Condition If(C c, const char* param, float value = 0.0f) { SM::Condition k; k.cond = c; k.param = param; k.value = value; return k; }
SM::Condition Speed(float above)  { return If(C::FloatGreater, "speed", above); }
SM::Condition Still(float below)  { return If(C::FloatLess, "speed", below); }
SM::Condition Is(const char* b)   { return If(C::BoolTrue, b); }
SM::Condition Not(const char* b)  { return If(C::BoolFalse, b); }
SM::Condition OnGround()          { return If(C::FloatLess, "airTime", 0.12f); }

// A transition to `to` when every condition holds.
SM::Transition To(const char* to, float blend, std::vector<SM::Condition> all, float offset = 0.0f) {
    SM::Transition t;
    t.to = to; t.blend = blend; t.offset = offset;
    t.cond = all.front().cond; t.param = all.front().param; t.value = all.front().value;
    t.extra.assign(all.begin() + 1, all.end());
    return t;
}

// From standing (or prone, or a landing): crouch, walk or run off, each stride
// starting with the left foot planted. `pre` must hold for any of them.
void StandOff(std::vector<SM::Transition>& out, std::vector<SM::Condition> pre, float blend, bool startPhase) {
    auto with = [&](std::vector<SM::Condition> c) { c.insert(c.begin(), pre.begin(), pre.end()); return c; };
    auto start = [&](int state) {   // the left foot planted under the hips, the right swinging through
        return startPhase ? HumanoidMotion::StartPhase(state, HumanoidMotion::NaturalSpeed(state)) : 0.0f;
    };
    out.push_back(To("CrouchWalk", blend, with({Is("crouch"), Speed(0.35f)}), start(17)));
    out.push_back(To("Crouch",     blend, with({Is("crouch")})));
    out.push_back(To("Run",        blend, with({Speed(0.35f), Is("run")}), start(3)));
    out.push_back(To("Walk",       blend, with({Speed(0.35f)}), start(2)));
}

SM::State Clip(const char* name, const char* clip, bool loop = true) {
    SM::State s; s.name = name; s.clip = clip; s.loop = loop; return s;
}
// A gait tree over the library's clips of `state`, laid out by direction and speed.
SM::State GaitTree(const char* name, int state) {
    SM::State s;
    s.name = name;
    s.blendTree = true; s.blendDims = 2; s.matchSpeed = true;
    s.blendParam = "gaitX"; s.blendParamY = "gaitZ"; s.speedParam = "gaitSpeed";
    for (const Entry& e : Entries())
        if (e.state == state && e.speed > 0.0f) {
            SM::Motion m; m.clip = e.clip.name; m.threshold = e.x; m.posY = e.z;
            s.motions.push_back(m);
        }
    return s;
}
} // namespace

void DefaultGraph(AnimStateMachine& sm, const Character* body) {
    sm.states.clear(); sm.anyState.clear(); sm.params.clear();
    sm.entry = "Idle";
    for (const char* f : {"speed", "airTime", "verticalSpeed", "gaitX", "gaitZ", "gaitSpeed"}) sm.params.push_back({f, 0});
    for (const char* b : {"run", "crouch", "prone"}) sm.params.push_back({b, 1});

    // Going prone: on the move the locomotion blends straight into the crawl;
    // standing still drops down through a kneel; crouched goes straight down.
    auto dropFirst = [](SM::State& st) {
        st.transitions.insert(st.transitions.begin(), {To("Crawl", 0.2f, {Is("prone"), Speed(0.35f)}), To("Drop", 0.1f, {Is("prone")})});
    };
    auto proneFirst = [](SM::State& st) {
        st.transitions.insert(st.transitions.begin(), {To("Crawl", 0.2f, {Is("prone"), Speed(0.35f)}), To("Prone", 0.2f, {Is("prone")})});
    };

    SM::State idle = Clip("Idle", "Idle");
    StandOff(idle.transitions, {}, 0.2f, true);

    SM::State walk = GaitTree("Walk", 2);
    walk.transitions = {To("CrouchWalk", 0.2f, {Is("crouch"), Speed(0.2f)}), To("Crouch", 0.2f, {Is("crouch")}),
                        To("Idle", 0.2f, {Still(0.2f)}), To("Run", 0.2f, {Is("run")})};
    SM::State run = GaitTree("Run", 3);
    run.transitions = {To("CrouchWalk", 0.2f, {Is("crouch"), Speed(0.2f)}), To("Crouch", 0.2f, {Is("crouch")}),
                       To("Idle", 0.2f, {Still(0.2f)}), To("Walk", 0.2f, {Not("run")})};

    SM::State crouch = Clip("Crouch", "Crouch");
    auto start = [](int state) { return HumanoidMotion::StartPhase(state, HumanoidMotion::NaturalSpeed(state)); };
    crouch.transitions = {To("CrouchWalk", 0.2f, {Speed(0.35f)}, start(17)),
                          To("Run", 0.2f, {Not("crouch"), Speed(0.35f), Is("run")}, start(3)),
                          To("Walk", 0.2f, {Not("crouch"), Speed(0.35f)}, start(2)),
                          To("Idle", 0.2f, {Not("crouch")})};

    SM::State crouchWalk = GaitTree("CrouchWalk", 17);
    crouchWalk.transitions = {To("Run", 0.2f, {Not("crouch"), Speed(0.2f), Is("run")}),
                              To("Walk", 0.2f, {Not("crouch"), Speed(0.2f)}),
                              To("Idle", 0.2f, {Not("crouch")}), To("Crouch", 0.2f, {Still(0.2f)})};

    // Prone, and the crawl in the direction the body goes, as fast as it moves.
    // Out of prone into a crouch directly; up to standing through GetUp.
    SM::State prone = Clip("Prone", "Prone"), crawl = GaitTree("Crawl", 7);
    prone.transitions = {To("Crawl", 0.2f, {Speed(0.35f)})};
    crawl.transitions = {To("Prone", 0.2f, {Still(0.2f)})};
    for (SM::State* st : {&prone, &crawl}) {
        st->transitions.push_back(To("CrouchWalk", 0.2f, {Not("prone"), Is("crouch"), Speed(0.35f)}));
        st->transitions.push_back(To("Crouch", 0.2f, {Not("prone"), Is("crouch")}));
        if (st == &crawl) {   // standing up on the move: straight into the stride
            st->transitions.push_back(To("Run", 0.2f, {Not("prone"), Speed(0.35f), Is("run")}));
            st->transitions.push_back(To("Walk", 0.2f, {Not("prone"), Speed(0.35f)}));
        }
        st->transitions.push_back(To("GetUp", 0.1f, {Not("prone")}));
        st->ikWeight = 0.0f;           // lying on the ground: no foot planting
        st->fullBodyIkWeight = 1.0f;   // the hands keep working (holding, look-at)
    }
    // Getting up and dropping down play through; a change of mind blends out
    // of them from wherever the body is.
    SM::State getUp = Clip("GetUp", "GetUp", false), drop = Clip("Drop", "Drop", false);
    getUp.transitions = {To("Prone", 0.2f, {Is("prone")}), To("Crouch", 0.2f, {Is("crouch")})};
    StandOff(getUp.transitions, {If(C::OnClipEnd, "")}, 0.15f, true);
    getUp.transitions.push_back(To("Idle", 0.15f, {If(C::OnClipEnd, "")}));
    drop.transitions = {To("Crouch", 0.2f, {Not("prone"), Is("crouch")}), To("Idle", 0.2f, {Not("prone")}),
                        To("Crawl", 0.15f, {If(C::OnClipEnd, ""), Speed(0.35f)}), To("Prone", 0.15f, {If(C::OnClipEnd, "")})};
    getUp.ikWeight = drop.ikWeight = 0.0f;
    getUp.fullBodyIkWeight = drop.fullBodyIkWeight = 1.0f;   // held objects stay in hand through the move

    // Air: back on the ground, land into whatever the body does (a standing
    // landing plays Land; a moving one gives at the knees mid-stride).
    // Jump holds its last frame (a jump outlasts the clip). Fall loops.
    SM::State jump = Clip("Jump", "Jump", false), fall = Clip("Fall", "Fall"), hang = Clip("Hang", "Hang");
    for (SM::State* s : {&jump, &fall}) {
        StandOff(s->transitions, {OnGround()}, 0.08f, false);
        s->transitions.push_back(To("Land", 0.08f, {OnGround()}));
    }
    StandOff(hang.transitions, {OnGround()}, 0.2f, true);
    hang.transitions.push_back(To("Idle", 0.2f, {OnGround()}));
    SM::State land = Clip("Land", "Land", false);
    StandOff(land.transitions, {}, 0.2f, true);
    land.transitions.push_back(To("Idle", 0.2f, {If(C::OnClipEnd, "")}));

    for (SM::State* st : {&idle, &walk, &run, &land, &hang}) dropFirst(*st);
    for (SM::State* st : {&crouch, &crouchWalk, &jump, &fall}) proneFirst(*st);
    sm.states = {idle, walk, run, crouch, crouchWalk, prone, crawl, getUp, drop, jump, fall, land, hang};
    // Gestures and emotions (a script's play_clip, `anim`): they last until the body moves off.
    for (const char* g : {"Wave", "Point", "Clap", "ThumbsUp", "Salute", "WaveBoth", "Cheer", "Sad", "Angry", "Think"}) {
        SM::State s = Clip(g, g);
        StandOff(s.transitions, {}, 0.2f, true);
        dropFirst(s);
        sm.states.push_back(s);
    }

    sm.anyState = {To("Jump", 0.1f, {If(C::FloatGreater, "airTime", 0.12f), If(C::FloatGreater, "verticalSpeed", 0.4f), Not("prone")}),
                   To("Fall", 0.1f, {If(C::FloatGreater, "airTime", 0.12f), If(C::FloatLess, "verticalSpeed", 0.4f), Not("prone")})};

    ApplyCrossfades(sm);
    ApplyStanceTimes(sm);

    // Graph layout: standing, crouched, prone and getting up/down, air, gestures.
    static const int kColStart[] = {0, 3, 5, 9, 13};
    for (std::size_t i = 0; i < sm.states.size(); ++i) {
        int col = 4;
        while (col > 0 && (int)i < kColStart[col]) --col;
        sm.states[i].nx = 40.0f + 220.0f * (float)col;
        sm.states[i].ny = 40.0f + 90.0f * (float)((int)i - kColStart[col]);
    }
    sm.actionsRev = 0;   // a rebuilt default graph takes every action, including ones already merged
    sm.actionsRev = 0;   // a rebuilt default graph takes every action
    sm.actionsRev = 0;   // a rebuilt default graph takes every action
    HumanoidActions::AddStates(sm, body);

    // A clip bound to a movement state on the body plays in its place.
    if (!body) return;
    const std::pair<const char*, const std::string*> bound[] = {
        {"Idle", &body->clipIdle}, {"Walk", &body->clipWalk}, {"Run", &body->clipRun},
        {"Jump", &body->clipJump}, {"Fall", &body->clipFall}, {"Crouch", &body->clipCrouch},
        {"Dive", &body->clipDive}};
    for (const auto& b : bound) {
        if (b.second->empty()) continue;
        for (SM::State& s : sm.states)
            if (s.name == b.first) { s.blendTree = false; s.matchSpeed = false; s.motions.clear(); s.clip = *b.second; }
    }
}

void ApplyStanceTimes(AnimStateMachine& sm) {
    // Which stance a state stands for: 0 standing, 1 crouched, 2 prone (-1 = none).
    auto stanceOf = [](const std::string& n) {
        if (n == "Crouch" || n == "CrouchWalk") return 1;
        if (n == "Prone" || n == "Crawl") return 2;
        if (n == "Jump" || n == "Fall") return -1;
        return 0;
    };
    const StanceTimes& t = sm.stanceTimes;
    for (SM::State& st : sm.states) {
        if (st.name == "Drop")  st.speed = 1.0f / std::max(0.05f, t.standCrouch + t.crouchProne);
        if (st.name == "GetUp") st.speed = 1.0f / std::max(0.05f, t.proneStand);
        const int from = stanceOf(st.name);
        for (SM::Transition& tr : st.transitions) {
            const int to = stanceOf(tr.to);
            if (from < 0 || to < 0 || from == to || tr.to == "Drop" || tr.to == "GetUp") continue;
            if (from + to == 1)   tr.blend = t.standCrouch;                    // stand <-> crouch
            else if (to == 0)     tr.blend = t.proneStand;                     // prone -> stand (on the move)
            else if (from == 0)   tr.blend = t.standCrouch + t.crouchProne;    // stand -> prone (on the move)
            else                  tr.blend = t.crouchProne;                    // crouch <-> prone
            // Either state can cut the blend short, so a change of mind mid-way
            // eases back instead of waiting the transition out.
            tr.interruption = SM::Transition::Interrupt::NextThenCurrent;
        }
    }
    HumanoidActions::ApplyTimes(sm);
}

void ApplyCrossfades(AnimStateMachine& sm) {
    const GaitCrossfades& x = sm.crossfades;
    auto loc = [](const std::string& n) { return n == "Idle" || n == "Walk" || n == "Run"; };
    auto air = [](const std::string& n) { return n == "Jump" || n == "Fall"; };
    using I = SM::Transition::Interrupt;
    auto arm = [&](SM::Transition& tr, float blend) {
        tr.blend = blend;
        tr.interruption = I::NextThenCurrent;
    };
    for (SM::State& st : sm.states) {
        for (SM::Transition& tr : st.transitions) {
            if (loc(st.name) && loc(tr.to)) {
                const bool run = st.name == "Run" || tr.to == "Run";
                const bool walk = st.name == "Walk" || tr.to == "Walk";
                if (run && walk) arm(tr, x.walkRun);          // walk <-> run
                else if (run)    arm(tr, x.walkRun);          // idle <-> run
                else             arm(tr, x.idleWalk);         // idle <-> walk
            } else if (air(st.name) && (loc(tr.to) || tr.to == "Land")) {
                arm(tr, x.airGround);
            } else if (st.name == "Land" && loc(tr.to)) {
                arm(tr, tr.to == "Run" ? x.walkRun : x.idleWalk);
            }
        }
    }
    // Into the air. The target's own transitions already bring it back down, so
    // the source's ground transitions must not yank the jump short.
    for (SM::Transition& tr : sm.anyState)
        if (tr.to == "Jump" || tr.to == "Fall") tr.blend = x.groundAir;
}

AnimStateMachine* EnsureAnimator(GameObject* go) {
    Character* body = go ? go->GetComponent<Character>() : nullptr;
    if (!body) return nullptr;
    for (AnimStateMachine* sm : go->GetComponents<AnimStateMachine>())
        if (sm->layer == 0) { HumanoidActions::MergeMissing(*sm, body, false); return sm; }
    if (go->GetComponent<ModelAnimator>()) return nullptr;
    AnimStateMachine* sm = go->AddComponent<AnimStateMachine>();
    DefaultGraph(*sm, body);
    return sm;
}

} // namespace HumanoidLibrary
} // namespace okay
