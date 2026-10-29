#pragma once
// ---------------------------------------------------------------------------
// The second wave of visual-script nodes: more math (remap, smooth step,
// ping-pong, move towards...), vector math, text, object queries, mouse
// buttons, and actions for objects, scenes, the Animator and IK. Included at
// the end of Nodes.hpp; MakeNode (NodeGraph.cpp) builds them by type name.
// ---------------------------------------------------------------------------
#include "okay/VisualScript/NodeGraph.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneManager.hpp"
#include "okay/Core/SaveData.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Components/SequencePlayer.hpp"
#include "okay/Components/FullBodyIK.hpp"
#include "okay/Components/NavMeshAgent.hpp"
#include "okay/Components/BehaviorTreeRunner.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/SpriteRenderer.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Components/AnimControl.hpp"
#include "okay/Render/Atmosphere.hpp"   // weather / fog / post-processing nodes
#include "okay/Animation/HumanoidActions.hpp"
#include "okay/Components/Seat.hpp"
#include "okay/Components/BlendShapes.hpp"
#include "okay/Input/Input.hpp"
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>

namespace okay::vs {

inline GameObject* VsFindObject(VsContext& ctx, const std::string& name) {
    if (!ctx.gameObject || !ctx.gameObject->scene()) return nullptr;
    if (name.empty() || name == "self") return ctx.gameObject;
    if (name == "other") return ctx.other;
    return ctx.gameObject->scene()->Find(name);
}

/// An object input: an object reference (Self, Other, Spawn's object, Find,
/// a raycast hit...) or a name typed into the pin ("self", "other" or an object's
/// name; empty = self). Null when that object doesn't exist (any more).
inline GameObject* VsResolveObject(VsContext& ctx, const VsValue& v) {
    if (v.IsObject()) return v.AsObjectId() ? GameObject::FromInstanceId(v.AsObjectId()) : nullptr;
    return VsFindObject(ctx, v.AsString());
}
/// A node that names its object in its text field may instead get it wired into
/// data input `pin` (which wins when connected or typed).
inline GameObject* VsPinOrName(const VsNode& n, int pin, NodeGraph& g, VsContext& ctx, const std::string& name) {
    return n.HasInput(pin) ? VsResolveObject(ctx, n.InputValue(pin, g, ctx)) : VsFindObject(ctx, name);
}

// ---- More math ---------------------------------------------------------
struct MathFn2Node : VsNode {   // in0, in1 (or one input) -> out
    enum class Op { Tan, Atan2, Exp, Log, PingPong, Repeat, DeltaAngle } op;
    explicit MathFn2Node(Op o) : op(o) {}
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        float a = In(0, g, ctx).AsFloat(), b = In(1, g, ctx).AsFloat();
        switch (op) {
            case Op::Tan:   return std::tan(a);
            case Op::Atan2: return std::atan2(a, b);
            case Op::Exp:   return std::exp(a);
            case Op::Log:   return a > 0.0f ? std::log(a) : 0.0f;
            case Op::PingPong: { if (b <= 0.0f) return 0.0f; float t = std::fmod(std::fabs(a), b * 2.0f); return t > b ? b * 2.0f - t : t; }
            case Op::Repeat:   { if (b <= 0.0f) return 0.0f; float t = std::fmod(a, b); return t < 0.0f ? t + b : t; }
            case Op::DeltaAngle: { float d = std::fmod(b - a + 540.0f, 360.0f); if (d < 0.0f) d += 360.0f; return d - 180.0f; }
        }
        return 0.0f;
    }
};
struct InverseLerpNode : VsNode {   // in0 a, in1 b, in2 value -> 0..1
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        float a = In(0, g, ctx).AsFloat(), b = In(1, g, ctx).AsFloat(), v = In(2, g, ctx).AsFloat();
        return std::fabs(b - a) < 1e-9f ? 0.0f : (v - a) / (b - a);
    }
};
struct RemapNode : VsNode {   // value, inMin, inMax, outMin, outMax
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        float v = In(0, g, ctx).AsFloat(), i0 = In(1, g, ctx).AsFloat(), i1 = In(2, g, ctx, VsValue{1.0f}).AsFloat();
        float o0 = In(3, g, ctx).AsFloat(), o1 = In(4, g, ctx, VsValue{1.0f}).AsFloat();
        float t = std::fabs(i1 - i0) < 1e-9f ? 0.0f : (v - i0) / (i1 - i0);
        return o0 + (o1 - o0) * t;
    }
};
struct SmoothStepNode : VsNode {   // edge0, edge1, x
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        float e0 = In(0, g, ctx).AsFloat(), e1 = In(1, g, ctx, VsValue{1.0f}).AsFloat(), x = In(2, g, ctx).AsFloat();
        float t = std::fabs(e1 - e0) < 1e-9f ? 0.0f : (x - e0) / (e1 - e0);
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        return t * t * (3.0f - 2.0f * t);
    }
};
struct MoveTowardsValueNode : VsNode {   // current, target, maxDelta
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        float c = In(0, g, ctx).AsFloat(), t = In(1, g, ctx).AsFloat(), m = In(2, g, ctx).AsFloat();
        if (std::fabs(t - c) <= m) return t;
        return c + (t > c ? m : -m);
    }
};

// ---- Vector math -------------------------------------------------------
struct Vec3OpNode : VsNode {
    enum class Op { Add, Sub, Scale, Length, Normalize, Dot, Distance } op;
    explicit Vec3OpNode(Op o) : op(o) {}
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        Vec3 a = In(0, g, ctx).AsVec3();
        switch (op) {
            case Op::Add:   return a + In(1, g, ctx).AsVec3();
            case Op::Sub:   return a - In(1, g, ctx).AsVec3();
            case Op::Scale: return a * In(1, g, ctx, VsValue{1.0f}).AsFloat();
            case Op::Length: return a.Magnitude();
            case Op::Normalize: return a.Magnitude() > 1e-9f ? a.Normalized() : Vec3{0, 0, 0};
            case Op::Dot:   return Vec3::Dot(a, In(1, g, ctx).AsVec3());
            case Op::Distance: return (a - In(1, g, ctx).AsVec3()).Magnitude();
        }
        return 0.0f;
    }
};

// ---- Text ----------------------------------------------------------------
struct TextOpNode : VsNode {
    enum class Op { ToString, Length, Contains, Upper, Lower } op;
    explicit TextOpNode(Op o) : op(o) {}
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        std::string s = In(0, g, ctx).AsString();
        switch (op) {
            case Op::ToString: return s;
            case Op::Length:   return (float)s.size();
            case Op::Contains: return s.find(In(1, g, ctx).AsString()) != std::string::npos;
            case Op::Upper:    for (char& c : s) c = (char)std::toupper((unsigned char)c); return s;
            case Op::Lower:    for (char& c : s) c = (char)std::tolower((unsigned char)c); return s;
        }
        return s;
    }
};
struct ToFixedNode : VsNode {   // value -> text with N decimals (arg)
    int decimals;
    explicit ToFixedNode(int d) : decimals(d < 0 ? 0 : (d > 6 ? 6 : d)) {}
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        char b[64]; std::snprintf(b, sizeof(b), "%.*f", decimals, In(0, g, ctx).AsFloat());
        return std::string(b);
    }
};

// ---- Objects ---------------------------------------------------------------
struct ObjectQueryNode : VsNode {   // arg = object name ("self", "other" or a name); in0 = object (wins)
    enum class Op { Exists, Position, DistanceTo, Name } op;
    std::string name;
    ObjectQueryNode(Op o, std::string n) : op(o), name(std::move(n)) {}
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        GameObject* o = VsPinOrName(*this, 0, g, ctx, name);
        switch (op) {
            case Op::Exists:   return o != nullptr;
            case Op::Position: return (o && o->transform) ? o->transform->Position() : Vec3{0, 0, 0};
            case Op::DistanceTo:
                if (!o || !o->transform || !ctx.transform) return 1e9f;
                return (o->transform->Position() - ctx.transform->Position()).Magnitude();
            case Op::Name: return o ? o->name : std::string();
        }
        return {};
    }
};
struct OtherNameNode : VsNode {   // the object a collision / trigger / click event is about
    VsValue Eval(int, NodeGraph&, VsContext& ctx) override { return ctx.other ? ctx.other->name : std::string(); }
};
// ---- Object references -------------------------------------------------------
struct SelfNode : VsNode {    // this graph's own object
    VsValue Eval(int, NodeGraph&, VsContext& ctx) override { return VsValue::Object(ctx.gameObject ? ctx.gameObject->InstanceId() : 0); }
};
struct OtherNode : VsNode {   // the other object of a collision / trigger / custom event
    VsValue Eval(int, NodeGraph&, VsContext& ctx) override { return VsValue::Object(ctx.other ? ctx.other->InstanceId() : 0); }
};
struct FindObjectNode : VsNode {   // arg = object name ("self" / "other" work too); no match = no object
    std::string name;
    explicit FindObjectNode(std::string n) : name(std::move(n)) {}
    VsValue Eval(int, NodeGraph&, VsContext& ctx) override {
        GameObject* o = VsFindObject(ctx, name);
        return VsValue::Object(o ? o->InstanceId() : 0);
    }
};
struct ObjectInfoNode : VsNode {   // the Target's (default: this object's) name or tag
    bool tag;
    explicit ObjectInfoNode(bool t) : tag(t) {}
    VsValue Eval(int, NodeGraph&, VsContext& ctx) override {
        if (!ctx.gameObject) return std::string();
        return tag ? ctx.gameObject->tag : ctx.gameObject->name;
    }
};
struct AnimEventNameNode : VsNode {   // the clip event an On Anim Event chain is running for
    VsValue Eval(int, NodeGraph&, VsContext& ctx) override {
        auto it = ctx.variables.find("anim_event");
        return it != ctx.variables.end() ? it->second : VsValue(std::string());
    }
};
struct OtherTagNode : VsNode {
    VsValue Eval(int, NodeGraph&, VsContext& ctx) override { return ctx.other ? ctx.other->tag : std::string(); }
};

// ---- Mouse buttons ----------------------------------------------------------
struct MouseButtonNode : VsNode {   // arg 0 left, 1 right, 2 middle
    int button; bool pressedOnly;
    MouseButtonNode(int b, bool p) : button(b), pressedOnly(p) {}
    VsValue Eval(int, NodeGraph&, VsContext&) override {
        return pressedOnly ? Input::GetMouseButtonDown(button) : Input::GetMouseButton(button);
    }
};

// ---- Actions ----------------------------------------------------------------
struct LookAtNode : VsNode {   // turn to face the named / wired (in0) object (3D: yaw; 2D: z rotation)
    std::string name;
    explicit LookAtNode(std::string n) : name(std::move(n)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        GameObject* o = VsPinOrName(*this, 0, g, ctx, name);
        if (o && o->transform && ctx.transform && o != ctx.gameObject) {
            Vec3 d = o->transform->Position() - ctx.transform->Position();
            // 2D objects (sprites / 2D bodies) turn about Z; everything else is 3D
            // and yaws to face it — even a target lying exactly along X.
            const bool is2D = ctx.gameObject->GetComponent<SpriteRenderer>() || ctx.gameObject->GetComponent<Rigidbody2D>();
            if (std::fabs(d.z) > 1e-4f || std::fabs(d.x) > 1e-4f || (is2D && std::fabs(d.y) > 1e-4f)) {
                if (!is2D)                    // 3D: face it horizontally (forward = -Z)
                    ctx.transform->localRotation = Quat::LookRotation(Vec3{-d.x, 0.0f, -d.z}.Normalized());
                else                          // 2D: rotate about Z toward it
                    ctx.transform->localRotation = Quat::Euler(0, 0, std::atan2(d.y, d.x) * 57.29578f);
            }
        }
        return 0;
    }
};
struct MoveTowardsObjectNode : VsNode {   // in0 = speed (units/s), in1 = object to move to (wins over the name)
    std::string name;
    explicit MoveTowardsObjectNode(std::string n) : name(std::move(n)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        GameObject* o = VsPinOrName(*this, 1, g, ctx, name);
        if (o && o->transform && ctx.transform && o != ctx.gameObject) {
            Vec3 d = o->transform->Position() - ctx.transform->Position();
            float dist = d.Magnitude(), step = In(0, g, ctx, VsValue{2.0f}).AsFloat() * ctx.deltaTime;
            if (dist > 1e-5f)
                ctx.transform->SetPosition(ctx.transform->Position() + d * ((step >= dist ? dist : step) / dist));
        }
        return 0;
    }
};
struct SetActiveNode : VsNode {   // in0 = on/off, in1 = object (wins over the name)
    std::string name;
    explicit SetActiveNode(std::string n) : name(std::move(n)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        // A deactivated object isn't found by name, so look through the scene list.
        GameObject* o = nullptr;
        if (HasInput(1)) {
            const VsValue t = In(1, g, ctx);
            if (t.IsObject()) o = t.AsObjectId() ? GameObject::FromInstanceId(t.AsObjectId()) : nullptr;
            else {
                const std::string nm = t.AsString();
                if (nm.empty() || nm == "self") o = ctx.gameObject;
                else if (nm == "other") o = ctx.other;
                else if (ctx.gameObject && ctx.gameObject->scene())
                    for (const auto& up : ctx.gameObject->scene()->Objects()) if (up && up->name == nm) { o = up.get(); break; }
            }
        } else if (name.empty() || name == "self") o = ctx.gameObject;
        else if (name == "other") o = ctx.other;
        else if (ctx.gameObject && ctx.gameObject->scene())
            for (const auto& up : ctx.gameObject->scene()->Objects()) if (up && up->name == name) { o = up.get(); break; }
        if (o) o->active = In(0, g, ctx, VsValue{true}).AsBool();
        return 0;
    }
};
struct SetTextNode : VsNode {   // in0 = text; the named (or in1-wired) object's TextRenderer
    std::string name;
    explicit SetTextNode(std::string n) : name(std::move(n)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (GameObject* o = VsPinOrName(*this, 1, g, ctx, name))
            if (auto* t = o->GetComponent<TextRenderer>()) t->text = In(0, g, ctx).AsString();
        return 0;
    }
};
struct LoadSceneNode : VsNode {   // arg = scene name (build list) or .okayscene path
    std::string scene;
    explicit LoadSceneNode(std::string s) : scene(std::move(s)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph&, VsContext& ctx) override {
        Scene* s = ctx.gameObject ? ctx.gameObject->scene() : nullptr;
        if (!s || scene.empty()) return 0;
        if (!Save::IsSafeRelative(scene)) {   // graphs are game content: stay in the game's folders
            OKAY_ERROR("[visual script] Load Scene: refusing '", scene, "' (absolute paths and '..' aren't allowed)");
            return 0;
        }
        if (scene.find(".okayscene") != std::string::npos) s->RequestLoad(scene);
        else if (!SceneManager::LoadSceneByName(*s, scene)) s->RequestLoad(scene + ".okayscene");
        return -1;   // the scene is being replaced: stop this chain
    }
};
struct CooldownNode : VsNode {   // passes at most once every `seconds`
    float seconds; float last = -1e9f;
    explicit CooldownNode(float s) : seconds(s) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph&, VsContext&) override {
        float now = Time::ElapsedTime();
        if (now - last < seconds) return -1;
        last = now;
        return 0;
    }
};
struct AnimParamNode : VsNode {   // Animator state machine on this object, a child or a parent (AnimControl.hpp)
    enum class Op { Trigger, SetFloat, SetBool } op;
    std::string param;
    AnimParamNode(Op o, std::string p) : op(o), param(std::move(p)) {}
    int ExecOutCount() const override { return 1; }
    static AnimStateMachine* FindSM(GameObject* go) { return FindAnimComponent<AnimStateMachine>(go); }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (AnimStateMachine* sm = FindSM(ctx.gameObject)) {
            if (op == Op::Trigger) sm->SetTrigger(param);
            else if (op == Op::SetFloat) sm->SetFloat(param, In(0, g, ctx).AsFloat());
            else sm->SetBool(param, In(0, g, ctx).AsBool());
        }
        return 0;
    }
};
struct AnimStateNode : VsNode {   // current Animator state name
    VsValue Eval(int, NodeGraph&, VsContext& ctx) override {
        AnimStateMachine* sm = AnimParamNode::FindSM(ctx.gameObject);
        return sm ? sm->Current() : std::string();
    }
};
struct IkLookNode : VsNode {   // in0 = Vec3 point (Full Body IK look-at), in1 = weight
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (ctx.gameObject)
            if (auto* f = ctx.gameObject->GetComponent<FullBodyIK>()) {
                f->lookTarget = nullptr; f->lookPosition = In(0, g, ctx).AsVec3();
                f->lookWeight = In(1, g, ctx, VsValue{1.0f}).AsFloat(); f->lookEnabled = f->lookWeight > 0.0f;
            }
        return 0;
    }
};
struct PlaySequenceNode : VsNode {   // arg = the sequence (a Sequence Player's file, or Assets/<name>.okayseq)
    std::string name;
    explicit PlaySequenceNode(std::string n) : name(std::move(n)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph&, VsContext& ctx) override {
        if (ctx.gameObject && ctx.gameObject->scene()) SequencePlayer::PlayByName(*ctx.gameObject->scene(), ctx.gameObject, name);
        return 0;
    }
};
struct StopSequenceNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph&, VsContext& ctx) override {
        if (ctx.gameObject && ctx.gameObject->scene()) SequencePlayer::StopAll(*ctx.gameObject->scene());
        return 0;
    }
};
// ---- The scene's look: weather, fog and post-processing (Atmosphere.hpp) ----
struct SetWeatherNode : VsNode {   // arg = none / rain / snow / storm; in0 = intensity (unwired: keep)
    std::string preset;
    explicit SetWeatherNode(std::string p) : preset(std::move(p)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (Scene* s = ctx.gameObject ? ctx.gameObject->scene() : nullptr) {
            auto& w = s->renderSettings.weather;
            w.preset = WeatherPresetFromName(preset);
            const float in = In(0, g, ctx, VsValue{-1.0f}).AsFloat();
            if (in >= 0.0f) w.intensity = Mathf::Clamp01(in);
        }
        return 0;
    }
};
struct SetFogNode : VsNode {   // in0 = on, in1 = start, in2 = end (unwired: keep the scene's)
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (Scene* s = ctx.gameObject ? ctx.gameObject->scene() : nullptr) {
            auto& rs = s->renderSettings;
            rs.fog = In(0, g, ctx, VsValue{true}).AsBool();
            const float start = In(1, g, ctx, VsValue{-1.0f}).AsFloat(), end = In(2, g, ctx, VsValue{-1.0f}).AsFloat();
            if (start >= 0.0f) rs.fogStart = start;
            if (end >= 0.0f) rs.fogEnd = end;
        }
        return 0;
    }
};
struct SetPostEffectNode : VsNode {   // arg = the setting's name (PostSet); in0 = value
    std::string name;
    explicit SetPostEffectNode(std::string n) : name(std::move(n)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (Scene* s = ctx.gameObject ? ctx.gameObject->scene() : nullptr)
            PostSet(s->renderSettings, name, In(0, g, ctx, VsValue{1.0f}).AsFloat());
        return 0;
    }
};
// Navigation: the NavMeshAgent on this object (or the Target) walks the navmesh.
struct NavMoveToNode : VsNode {   // in0 = Vec3 destination
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (ctx.gameObject)
            if (auto* a = ctx.gameObject->GetComponent<NavMeshAgent>()) a->SetDestination(In(0, g, ctx).AsVec3());
        return 0;
    }
};
struct NavStopNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph&, VsContext& ctx) override {
        if (ctx.gameObject)
            if (auto* a = ctx.gameObject->GetComponent<NavMeshAgent>()) a->Stop();
        return 0;
    }
};
struct NavArrivedNode : VsNode {   // true once the last Nav Move To point was reached
    VsValue Eval(int, NodeGraph&, VsContext& ctx) override {
        auto* a = ctx.gameObject ? ctx.gameObject->GetComponent<NavMeshAgent>() : nullptr;
        return a != nullptr && a->Arrived();
    }
};
// Behavior trees: the Behavior Tree Runner on this object (or the Target).
struct BtSetNode : VsNode {   // arg = blackboard key; in0 = the value
    std::string key;
    explicit BtSetNode(std::string k) : key(std::move(k)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (ctx.gameObject)
            if (auto* r = ctx.gameObject->GetComponent<BehaviorTreeRunner>()) r->blackboard.Set(key, BtValue::FromVs(In(0, g, ctx)));
        return 0;
    }
};
struct BtGetNode : VsNode {   // arg = blackboard key; out0 = the value
    std::string key;
    explicit BtGetNode(std::string k) : key(std::move(k)) {}
    VsValue Eval(int, NodeGraph&, VsContext& ctx) override {
        auto* r = ctx.gameObject ? ctx.gameObject->GetComponent<BehaviorTreeRunner>() : nullptr;
        const BtValue* v = r ? r->blackboard.Find(key) : nullptr;
        return v ? v->ToVs() : VsValue{};
    }
};
struct BtRunNode : VsNode {   // arg = the .okaybt file ("" = start the current tree over)
    std::string tree;
    explicit BtRunNode(std::string t) : tree(std::move(t)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph&, VsContext& ctx) override {
        if (!ctx.gameObject) return 0;
        auto* r = ctx.gameObject->GetComponent<BehaviorTreeRunner>();
        if (!r) r = ctx.gameObject->AddComponent<BehaviorTreeRunner>();
        if (!tree.empty()) {
            std::string err;
            if (!r->LoadTree(tree, &err)) { Log::Warning("Run Tree: ", err); return 0; }
        }
        r->Run();
        return 0;
    }
};
struct IkHandNode : VsNode {   // arg "left"/"right"; in0 = Vec3 point, in1 = weight (0 releases)
    bool left;
    explicit IkHandNode(bool l) : left(l) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (ctx.gameObject)
            if (auto* f = ctx.gameObject->GetComponent<FullBodyIK>()) {
                auto& e = f->effectors[left ? FullBodyIK::LeftHand : FullBodyIK::RightHand];
                e.target = nullptr; e.position = In(0, g, ctx).AsVec3();
                e.positionWeight = In(1, g, ctx, VsValue{1.0f}).AsFloat(); e.enabled = e.positionWeight > 0.0f;
            }
        return 0;
    }
};

// ---- Third wave: loops, switch, gate, 3D transform, colour, clips, shapes ----
// Run "Body" once per index from First to Last (inclusive; counts down when
// Last < First), then "Done". The Index output reads the current index.
struct ForLoopNode : VsNode {   // in0 = first, in1 = last; out0 = index
    int index = 0;
    int ExecOutCount() const override { return 2; }
    VsValue Eval(int, NodeGraph&, VsContext&) override { return (float)index; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        const int first = (int)std::lround(In(0, g, ctx, VsValue{0.0f}).AsFloat());
        const int last  = (int)std::lround(In(1, g, ctx, VsValue{9.0f}).AsFloat());
        const int step = last >= first ? 1 : -1;
        const int body = Next(0);
        // No iteration cap of its own: every body run spends the event's step
        // budget, so a huge loop is stopped (and reported) by the graph.
        g.EnterLoop();
        for (index = first; body >= 0; index += step) {
            g.Run(body, ctx);
            if (g.TakeLoopSignal() == NodeGraph::kLoopBreak || g.Aborted() || index == last) break;
        }
        g.LeaveLoop();
        return 1;
    }
};
// Only lets the flow through while Open is true.
struct GateNode : VsNode {   // in0 = open
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override { return In(0, g, ctx, VsValue{true}).AsBool() ? 0 : -1; }
};
// Passes the first N times only; Count reads how many times it has passed.
struct DoNNode : VsNode {
    int n, count = 0;
    explicit DoNNode(int c) : n(c < 0 ? 0 : c) {}
    int ExecOutCount() const override { return 1; }
    VsValue Eval(int, NodeGraph&, VsContext&) override { return (float)count; }
    int Exec(NodeGraph&, VsContext&) override { if (count >= n) return -1; ++count; return 0; }
};
// Pick an exec output by a whole number: 0, 1, 2, or Default for anything else.
struct SwitchIntNode : VsNode {   // in0 = value
    int ExecOutCount() const override { return 4; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        const int v = (int)std::lround(In(0, g, ctx).AsFloat());
        return (v >= 0 && v <= 2) ? v : 3;
    }
};
// Integer in [min, max], both ends included.
struct RandomIntNode : VsNode {   // in0 = min, in1 = max
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        int lo = (int)std::lround(In(0, g, ctx, VsValue{0.0f}).AsFloat());
        int hi = (int)std::lround(In(1, g, ctx, VsValue{10.0f}).AsFloat());
        if (hi < lo) std::swap(lo, hi);
        int v = lo + (int)std::floor(VsRandom01() * (float)(hi - lo + 1));
        return (float)(v > hi ? hi : v);
    }
};
struct MathFn3Node : VsNode {   // one-input math: in0 -> out
    enum class Op { Ceil, Asin, Acos, Deg2Rad, Rad2Deg, Frac, OneMinus, Saturate } op;
    explicit MathFn3Node(Op o) : op(o) {}
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        const float x = In(0, g, ctx).AsFloat();
        auto c1 = [](float v) { return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v); };
        switch (op) {
            case Op::Ceil:     return std::ceil(x);
            case Op::Asin:     return std::asin(c1(x));
            case Op::Acos:     return std::acos(c1(x));
            case Op::Deg2Rad:  return x * 0.017453292519943295f;
            case Op::Rad2Deg:  return x * 57.29577951308232f;
            case Op::Frac:     return x - std::floor(x);
            case Op::OneMinus: return 1.0f - x;
            case Op::Saturate: return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
        }
        return x;
    }
};
struct Vec3MoreNode : VsNode {
    enum class Op { Cross, Lerp, Negate, Up, Forward, Right } op;
    explicit Vec3MoreNode(Op o) : op(o) {}
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        switch (op) {
            case Op::Cross:  return Vec3::Cross(In(0, g, ctx).AsVec3(), In(1, g, ctx).AsVec3());
            case Op::Lerp: {
                Vec3 a = In(0, g, ctx).AsVec3(), b = In(1, g, ctx).AsVec3();
                return a + (b - a) * In(2, g, ctx, VsValue{0.5f}).AsFloat();
            }
            case Op::Negate: return In(0, g, ctx).AsVec3() * -1.0f;
            case Op::Up:      return ctx.transform ? ctx.transform->Up()      : Vec3::Up;
            case Op::Forward: return ctx.transform ? ctx.transform->Forward() : Vec3::Forward;
            case Op::Right:   return ctx.transform ? ctx.transform->Right()   : Vec3::Right;
        }
        return Vec3::Zero;
    }
};
struct TransformReadNode : VsNode {   // this object's scale / rotation (Euler degrees)
    bool rotation;
    explicit TransformReadNode(bool r) : rotation(r) {}
    VsValue Eval(int, NodeGraph&, VsContext& ctx) override {
        if (!ctx.transform) return rotation ? Vec3::Zero : Vec3{1, 1, 1};
        return rotation ? ctx.transform->localRotation.ToEuler() : ctx.transform->localScale;
    }
};
// 3D transform actions with one Vec3 input (or X/Y/Z).
struct Transform3Node : VsNode {
    enum class Op { SetPosition, MoveBy, Rotate, SetRotation } op;
    explicit Transform3Node(Op o) : op(o) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (!ctx.transform) return 0;
        Vec3 v;
        if (op == Op::SetPosition || op == Op::MoveBy) v = In(0, g, ctx).AsVec3();
        else v = {In(0, g, ctx).AsFloat(), In(1, g, ctx).AsFloat(), In(2, g, ctx).AsFloat()};
        switch (op) {
            case Op::SetPosition: ctx.transform->localPosition = v; break;
            case Op::MoveBy:      ctx.transform->localPosition = ctx.transform->localPosition + v; break;
            case Op::Rotate:      ctx.transform->Rotate(v); break;
            case Op::SetRotation: ctx.transform->localRotation = Quat::Euler(v); break;
        }
        return 0;
    }
};
// Tint this object's sprite / text / mesh (R, G, B, A in 0..1; blank = 1).
struct SetColorNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (!ctx.gameObject) return 0;
        const Color c{In(0, g, ctx, VsValue{1.0f}).AsFloat(), In(1, g, ctx, VsValue{1.0f}).AsFloat(),
                      In(2, g, ctx, VsValue{1.0f}).AsFloat(), In(3, g, ctx, VsValue{1.0f}).AsFloat()};
        if (auto* sr = ctx.gameObject->GetComponent<SpriteRenderer>()) sr->color = c;
        if (auto* tr = ctx.gameObject->GetComponent<TextRenderer>()) tr->color = c;
        if (auto* mr = ctx.gameObject->GetComponent<MeshRenderer>()) mr->color = c;
        return 0;
    }
};
// Play a clip by name the same way a script's play_clip does (AnimControl.hpp):
// a Character's clip, the imported model's clip or a built-in animation, held
// until a script's stop_clip.
struct PlayActionNode : VsNode {
    std::string action;
    explicit PlayActionNode(std::string a) : action(std::move(a)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        const float blend = In(0, g, ctx, VsValue{0.2f}).AsFloat();
        HumanoidActions::Play(ctx.gameObject, action, blend);
        return 0;
    }
};
struct IsInActionNode : VsNode {
    VsValue Eval(int, NodeGraph&, VsContext& ctx) override {
        return HumanoidActions::IsInAction(ctx.gameObject) ? 1.0f : 0.0f;
    }
};
struct DieNode : VsNode {
    std::string dir;
    explicit DieNode(std::string d) : dir(std::move(d)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph&, VsContext& ctx) override {
        const int d = HumanoidActions::DirFromName(dir.empty() ? "forward" : dir);
        HumanoidActions::RequestDeath(ctx.gameObject, d, nullptr);
        return 0;
    }
};
struct DiveNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph&, VsContext& ctx) override {
        HumanoidActions::RequestDive(ctx.gameObject);
        return 0;
    }
};
struct HitReactNode : VsNode {
    std::string dir;
    explicit HitReactNode(std::string d) : dir(std::move(d)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        const int d = HumanoidActions::DirFromName(dir.empty() ? "forward" : dir);
        const bool heavy = In(0, g, ctx, VsValue{0.0f}).AsFloat() != 0.0f;
        HumanoidActions::HitReact(ctx.gameObject, d, heavy);
        return 0;
    }
};
struct RollNode : VsNode {
    std::string dir;
    explicit RollNode(std::string d) : dir(std::move(d)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph&, VsContext& ctx) override {
        HumanoidActions::RequestRoll(ctx.gameObject, HumanoidActions::DirFromName(dir.empty() ? "forward" : dir));
        return 0;
    }
};
struct DodgeNode : VsNode {
    std::string dir;
    explicit DodgeNode(std::string d) : dir(std::move(d)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph&, VsContext& ctx) override {
        HumanoidActions::RequestDodge(ctx.gameObject, HumanoidActions::DirFromName(dir.empty() ? "forward" : dir));
        return 0;
    }
};
struct ReviveNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph&, VsContext& ctx) override {
        HumanoidActions::RequestRevive(ctx.gameObject);
        return 0;
    }
};
// arg: a Seat's name, or ground / knees / chair. Empty sits on a flat floor.
struct SitNode : VsNode {
    std::string which;
    explicit SitNode(std::string w) : which(std::move(w)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph&, VsContext& ctx) override {
        if (!ctx.gameObject) return 0;
        GameObject* seat = nullptr;
        int type = 1;
        if (!which.empty()) {
            seat = VsFindObject(ctx, which);
            if (!seat || !seat->GetComponent<Seat>()) {
                seat = nullptr;
                std::string k = which;
                for (char& c : k) c = (char)std::tolower((unsigned char)c);
                if (k == "chair" || k == "bench") type = 0;
                else if (k == "knees") type = 2;
            }
        }
        HumanoidActions::RequestSit(ctx.gameObject, seat, type);
        return 0;
    }
};
struct StandUpNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph&, VsContext& ctx) override {
        HumanoidActions::RequestStandUp(ctx.gameObject);
        return 0;
    }
};
struct IsSittingNode : VsNode {
    VsValue Eval(int, NodeGraph&, VsContext& ctx) override {
        return HumanoidActions::IsSitting(ctx.gameObject) ? 1.0f : 0.0f;
    }
};
struct InteractNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph&, VsContext& ctx) override {
        HumanoidActions::Interact(ctx.gameObject);
        return 0;
    }
};
struct SeatPromptNode : VsNode {
    VsValue Eval(int, NodeGraph&, VsContext& ctx) override {
        return HumanoidActions::SeatPrompt(ctx.gameObject);
    }
};
struct VaultNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph&, VsContext& ctx) override { HumanoidActions::RequestVault(ctx.gameObject); return 0; }
};
struct MantleNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph&, VsContext& ctx) override { HumanoidActions::RequestMantle(ctx.gameObject); return 0; }
};
struct LedgeGrabNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph&, VsContext& ctx) override { HumanoidActions::RequestLedgeGrab(ctx.gameObject); return 0; }
};
struct ClimbUpNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph&, VsContext& ctx) override { HumanoidActions::RequestClimbUp(ctx.gameObject); return 0; }
};
struct LetGoNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph&, VsContext& ctx) override { HumanoidActions::RequestLetGo(ctx.gameObject); return 0; }
};
struct IsHangingNode : VsNode {
    VsValue Eval(int, NodeGraph&, VsContext& ctx) override {
        return HumanoidActions::IsHanging(ctx.gameObject) ? 1.0f : 0.0f;
    }
};
struct ShimmyNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        HumanoidActions::SetShimmy(ctx.gameObject, In(0, g, ctx, VsValue{0.0f}).AsFloat());
        return 0;
    }
};
struct PlayClipNode : VsNode {
    std::string clip;
    bool warned = false;   // "no animator" is logged once per node
    explicit PlayClipNode(std::string c) : clip(std::move(c)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph&, VsContext& ctx) override {
        PlayClipFrom(ctx.gameObject, clip, warned);
        return 0;
    }
};
struct SetTimeScaleNode : VsNode {   // in0 = scale (1 = normal, 0 = paused)
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        float s = In(0, g, ctx, VsValue{1.0f}).AsFloat();
        Time::SetTimeScale(s < 0.0f ? 0.0f : s);
        return 0;
    }
};
// Blend shape (morph target) weight on every mesh under this object.
struct BlendShapeNode : VsNode {   // arg = shape name; in0 = weight (exec) / out = weight (data)
    std::string shape; bool set;
    BlendShapeNode(std::string s, bool st) : shape(std::move(s)), set(st) {}
    int ExecOutCount() const override { return set ? 1 : 0; }
    template <class F> void Each(VsContext& ctx, F fn) {
        if (!ctx.gameObject || !ctx.gameObject->scene()) return;
        for (const auto& up : ctx.gameObject->scene()->Objects())
            if (up && up->IsSelfOrDescendantOf(ctx.gameObject))
                if (auto* bs = up->GetComponent<BlendShapes>()) fn(bs);
    }
    VsValue Eval(int, NodeGraph&, VsContext& ctx) override {
        float w = 0.0f;
        Each(ctx, [&](BlendShapes* bs) { if (bs->Find(shape) >= 0) w = bs->Weight(shape); });
        return w;
    }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        const float w = In(0, g, ctx, VsValue{1.0f}).AsFloat();
        Each(ctx, [&](BlendShapes* bs) { bs->SetWeight(shape, w); });
        return 0;
    }
};
struct EngineReadNode : VsNode {   // frame-level values
    enum class Op { MouseWheel, TimeScale, FrameCount } op;
    explicit EngineReadNode(Op o) : op(o) {}
    VsValue Eval(int, NodeGraph&, VsContext&) override {
        switch (op) {
            case Op::MouseWheel: return Input::MouseWheel();
            case Op::TimeScale:  return Time::TimeScale();
            case Op::FrameCount: return (float)Time::FrameCount();
        }
        return 0.0f;
    }
};

} // namespace okay::vs
