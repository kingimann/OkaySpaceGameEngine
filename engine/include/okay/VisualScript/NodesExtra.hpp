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
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Components/FullBodyIK.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Input/Input.hpp"
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
struct ObjectQueryNode : VsNode {   // arg = object name ("self", "other" or a name)
    enum class Op { Exists, Position, DistanceTo, Name } op;
    std::string name;
    ObjectQueryNode(Op o, std::string n) : op(o), name(std::move(n)) {}
    VsValue Eval(int, NodeGraph&, VsContext& ctx) override {
        GameObject* o = VsFindObject(ctx, name);
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
struct LookAtNode : VsNode {   // turn to face the named object (3D: yaw; 2D: z rotation)
    std::string name;
    explicit LookAtNode(std::string n) : name(std::move(n)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph&, VsContext& ctx) override {
        GameObject* o = VsFindObject(ctx, name);
        if (o && o->transform && ctx.transform && o != ctx.gameObject) {
            Vec3 d = o->transform->Position() - ctx.transform->Position();
            if (std::fabs(d.z) > 1e-4f || std::fabs(d.x) > 1e-4f) {
                if (std::fabs(d.z) > 1e-4f)   // 3D: face it horizontally (forward = -Z)
                    ctx.transform->localRotation = Quat::LookRotation(Vec3{-d.x, 0.0f, -d.z}.Normalized());
                else                          // 2D: rotate about Z toward it
                    ctx.transform->localRotation = Quat::Euler(0, 0, std::atan2(d.y, d.x) * 57.29578f);
            }
        }
        return 0;
    }
};
struct MoveTowardsObjectNode : VsNode {   // in0 = speed (units/s)
    std::string name;
    explicit MoveTowardsObjectNode(std::string n) : name(std::move(n)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        GameObject* o = VsFindObject(ctx, name);
        if (o && o->transform && ctx.transform && o != ctx.gameObject) {
            Vec3 d = o->transform->Position() - ctx.transform->Position();
            float dist = d.Magnitude(), step = In(0, g, ctx, VsValue{2.0f}).AsFloat() * ctx.deltaTime;
            if (dist > 1e-5f)
                ctx.transform->SetPosition(ctx.transform->Position() + d * ((step >= dist ? dist : step) / dist));
        }
        return 0;
    }
};
struct SetActiveNode : VsNode {   // in0 = on/off
    std::string name;
    explicit SetActiveNode(std::string n) : name(std::move(n)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        // A deactivated object isn't found by name, so look through the scene list.
        GameObject* o = nullptr;
        if (name.empty() || name == "self") o = ctx.gameObject;
        else if (name == "other") o = ctx.other;
        else if (ctx.gameObject && ctx.gameObject->scene())
            for (const auto& up : ctx.gameObject->scene()->Objects()) if (up && up->name == name) { o = up.get(); break; }
        if (o) o->active = In(0, g, ctx, VsValue{true}).AsBool();
        return 0;
    }
};
struct SetTextNode : VsNode {   // in0 = text; the named object's TextRenderer
    std::string name;
    explicit SetTextNode(std::string n) : name(std::move(n)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (GameObject* o = VsFindObject(ctx, name))
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
struct AnimParamNode : VsNode {   // Animator state machine on this object or a child
    enum class Op { Trigger, SetFloat, SetBool } op;
    std::string param;
    AnimParamNode(Op o, std::string p) : op(o), param(std::move(p)) {}
    int ExecOutCount() const override { return 1; }
    static AnimStateMachine* FindSM(GameObject* go, int depth = 0) {
        if (!go || depth > 6) return nullptr;
        if (auto* m = go->GetComponent<AnimStateMachine>()) return m;
        if (go->transform) for (Transform* c : go->transform->Children()) if (c && c->gameObject) if (auto* m = FindSM(c->gameObject, depth + 1)) return m;
        return nullptr;
    }
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

} // namespace okay::vs
