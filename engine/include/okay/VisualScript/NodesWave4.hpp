#pragma once
// ---------------------------------------------------------------------------
// Visual-script nodes, fourth wave: custom events between graphs, object
// variables, save / load, text helpers, noise and a few game-flow actions.
// Event entry nodes (On Key, On Timer, On Custom Event, exits, mouse hover) are
// plain EventNodes; VisualScriptComponent fires them.
// ---------------------------------------------------------------------------
#include "okay/VisualScript/NodesExtra.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/ActionList.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/SpriteRenderer.hpp"
#include "okay/Core/SaveData.hpp"
#include "okay/Components/UIAnimation.hpp"
#include "okay/Core/Game.hpp"
#include "okay/Input/Cursor.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace okay::vs {

// Deliver a custom event to one object: its graphs' On Custom Event nodes and
// its scripts' OnEvent_<name>(value) functions.
inline void VsDeliverEvent(GameObject* o, const std::string& name, const VsValue& v, GameObject* sender) {
    if (!o) return;
    for (auto* vc : o->GetComponents<VisualScriptComponent>()) vc->SendEvent(name, v, sender);
    // Older Actions components listening with "On Message <name>".
    for (auto* al : o->GetComponents<ActionList>()) {
        if (v.IsNumber()) ActionList::Vars()["event_value"] = v.AsFloat();
        else if (v.IsString()) ActionList::StrVars()["event_text"] = v.AsString();
        al->ReceiveMessage(name);
    }
    for (auto* sc : o->GetComponents<ScriptComponent>())
        if (sc->VM()) sc->VM()->CallEventWith("OnEvent_" + name, v);
}

// On Custom Event: an entry node whose Value output is the value sent with it.
struct CustomEventNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph&, VsContext&) override { return 0; }
    VsValue Eval(int, NodeGraph&, VsContext& ctx) override {
        auto it = ctx.variables.find("event_value");
        return it != ctx.variables.end() ? it->second : VsValue{};
    }
};

// ---- Custom events -------------------------------------------------------
// Send Event: run another object's (or this object's) "On Custom Event <name>"
// nodes, handing them a value. in0 = target object name (empty = self), in1 = value.
struct SendEventNode : VsNode {
    std::string name;
    explicit SendEventNode(std::string n) : name(std::move(n)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        GameObject* o = HasInput(0) ? VsResolveObject(ctx, In(0, g, ctx)) : ctx.gameObject;
        VsValue v = In(1, g, ctx);
        VsDeliverEvent(o, name, v, ctx.gameObject);
        return 0;
    }
};
// Broadcast Event: every graph in the scene that listens for <name>. in0 = value.
struct BroadcastEventNode : VsNode {
    std::string name;
    explicit BroadcastEventNode(std::string n) : name(std::move(n)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (!ctx.gameObject || !ctx.gameObject->scene()) return 0;
        VsValue v = In(0, g, ctx);
        std::vector<GameObject*> all;   // collect first: a handler may spawn/destroy
        for (const auto& up : ctx.gameObject->scene()->Objects())
            if (up && up->active) all.push_back(up.get());
        for (GameObject* o : all) VsDeliverEvent(o, name, v, ctx.gameObject);
        return 0;
    }
};

// ---- Other objects' variables --------------------------------------------
// in0 = target object (empty = self), in1 = value.
struct SetObjectVarNode : VsNode {
    std::string var;
    explicit SetObjectVarNode(std::string v) : var(std::move(v)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        GameObject* o = HasInput(0) ? VsResolveObject(ctx, In(0, g, ctx)) : ctx.gameObject;
        VsValue v = In(1, g, ctx);
        if (o == ctx.gameObject) ctx.variables[var] = v;
        else if (o) if (auto* vc = o->GetComponent<VisualScriptComponent>()) vc->SetVariable(var, v);
        return 0;
    }
};
struct GetObjectVarNode : VsNode {   // in0 = target object
    std::string var;
    explicit GetObjectVarNode(std::string v) : var(std::move(v)) {}
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        GameObject* o = HasInput(0) ? VsResolveObject(ctx, In(0, g, ctx)) : ctx.gameObject;
        if (o == ctx.gameObject) { auto it = ctx.variables.find(var); return it != ctx.variables.end() ? it->second : VsValue{}; }
        if (o) if (auto* vc = o->GetComponent<VisualScriptComponent>()) return vc->GetVariable(var);
        return {};
    }
};

// ---- Save / load (persists between runs, like PlayerPrefs) ----------------
struct SaveValueNode : VsNode {   // in0 = value
    std::string key;
    explicit SaveValueNode(std::string k) : key(std::move(k)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        VsValue v = In(0, g, ctx);
        if (v.IsVec3())        Save::SetVec3(key, v.AsVec3());
        else if (v.IsString()) Save::SetString(key, v.AsString());
        else                   Save::SetFloat(key, v.AsFloat());
        return 0;
    }
};
struct LoadValueNode : VsNode {   // in0 = default (its type picks how the value is read)
    std::string key;
    explicit LoadValueNode(std::string k) : key(std::move(k)) {}
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        VsValue def = In(0, g, ctx);
        if (def.IsVec3())   return Save::GetVec3(key, def.AsVec3());
        if (def.IsString()) return Save::GetString(key, def.AsString());
        return Save::GetFloat(key, def.AsFloat());
    }
};

// ---- Text ------------------------------------------------------------------
// Format: "Score: {0} / {1}" with in0..in2 substituted.
struct FormatNode : VsNode {
    std::string tmpl;
    explicit FormatNode(std::string t) : tmpl(std::move(t)) {}
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        std::string out = tmpl.empty() ? std::string("{0}") : tmpl;
        for (int i = 0; i < 3; ++i) {
            const std::string tag = "{" + std::to_string(i) + "}";
            if (out.find(tag) == std::string::npos) continue;
            const std::string val = In(i, g, ctx).AsString();
            for (std::size_t p = out.find(tag); p != std::string::npos; p = out.find(tag, p + val.size()))
                out.replace(p, tag.size(), val);
        }
        return out;
    }
};
struct TextFnNode : VsNode {
    enum class Op { Substring, Replace, ToNumber, StartsWith, EndsWith, Trim, IndexOf } op;
    explicit TextFnNode(Op o) : op(o) {}
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        std::string s = In(0, g, ctx).AsString();
        switch (op) {
            case Op::Substring: {
                int st = std::max(0, (int)std::lround(In(1, g, ctx).AsFloat()));
                int len = HasInput(2) ? (int)std::lround(In(2, g, ctx).AsFloat()) : (int)s.size();
                if (st >= (int)s.size() || len <= 0) return std::string();
                return s.substr((std::size_t)st, (std::size_t)len);
            }
            case Op::Replace: {
                std::string a = In(1, g, ctx).AsString(), b = In(2, g, ctx).AsString();
                if (a.empty()) return s;
                for (std::size_t p = s.find(a); p != std::string::npos; p = s.find(a, p + b.size())) s.replace(p, a.size(), b);
                return s;
            }
            case Op::ToNumber: {
                try { return std::stof(s); } catch (...) { return 0.0f; }
            }
            case Op::StartsWith: { std::string a = In(1, g, ctx).AsString(); return s.rfind(a, 0) == 0; }
            case Op::EndsWith:   { std::string a = In(1, g, ctx).AsString();
                                   return s.size() >= a.size() && s.compare(s.size() - a.size(), a.size(), a) == 0; }
            case Op::Trim: {
                std::size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
                return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
            }
            case Op::IndexOf: { std::size_t p = s.find(In(1, g, ctx).AsString()); return p == std::string::npos ? -1.0f : (float)p; }
        }
        return {};
    }
};

// ---- Math ------------------------------------------------------------------
// Smooth 2D gradient noise in about -1..1 (classic Perlin), for wobble, terrain,
// flicker. in0 = x, in1 = y.
inline float VsPerlin2(float x, float y) {
    auto hash = [](int i, int j) {
        unsigned h = (unsigned)i * 374761393u + (unsigned)j * 668265263u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return h ^ (h >> 16);
    };
    auto grad = [&](int i, int j, float dx, float dy) {
        switch (hash(i, j) & 7u) {
            case 0: return  dx + dy; case 1: return -dx + dy; case 2: return  dx - dy; case 3: return -dx - dy;
            case 4: return  dx;      case 5: return -dx;      case 6: return  dy;      default: return -dy;
        }
    };
    int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
    float fx = x - (float)x0, fy = y - (float)y0;
    auto fade = [](float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); };
    float u = fade(fx), v = fade(fy);
    float n00 = grad(x0, y0, fx, fy), n10 = grad(x0 + 1, y0, fx - 1.0f, fy);
    float n01 = grad(x0, y0 + 1, fx, fy - 1.0f), n11 = grad(x0 + 1, y0 + 1, fx - 1.0f, fy - 1.0f);
    float a = n00 + (n10 - n00) * u, b = n01 + (n11 - n01) * u;
    return (a + (b - a) * v) * 0.7071f * 1.41421f;
}
struct MathFn4Node : VsNode {
    enum class Op { Noise, Snap, Wrap, Average, Distance2D } op;
    explicit MathFn4Node(Op o) : op(o) {}
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        float a = In(0, g, ctx).AsFloat(), b = In(1, g, ctx).AsFloat();
        switch (op) {
            case Op::Noise: return VsPerlin2(a, b);
            case Op::Snap:  { float st = std::fabs(b); return st > 1e-6f ? std::round(a / st) * st : a; }
            case Op::Wrap:  {   // a wrapped into [min, max)
                float lo = b, hi = In(2, g, ctx, VsValue{1.0f}).AsFloat(), r = hi - lo;
                if (std::fabs(r) < 1e-6f) return lo;
                float t = std::fmod(a - lo, r); if (t < 0) t += r; return lo + t;
            }
            case Op::Average: return (a + b) * 0.5f;
            case Op::Distance2D: {
                float c = In(2, g, ctx).AsFloat(), d = In(3, g, ctx).AsFloat();
                return std::sqrt((c - a) * (c - a) + (d - b) * (d - b));
            }
        }
        return 0.0f;
    }
};

// ---- Finding objects -------------------------------------------------------
struct TagQueryNode : VsNode {
    enum class Op { FindWithTag, CountWithTag, NearestWithTag } op;
    std::string tag;
    TagQueryNode(Op o, std::string t) : op(o), tag(std::move(t)) {}
    VsValue Eval(int, NodeGraph&, VsContext& ctx) override {
        if (!ctx.gameObject || !ctx.gameObject->scene()) return op == Op::CountWithTag ? VsValue{0.0f} : VsValue::Object(0);
        int count = 0; float best = 1e30f; GameObject* pick = nullptr;
        Vec3 me = ctx.transform ? ctx.transform->Position() : Vec3{0, 0, 0};
        for (const auto& up : ctx.gameObject->scene()->Objects()) {
            GameObject* o = up.get();
            if (!o || !o->active || o->tag != tag) continue;
            ++count;
            if (op == Op::FindWithTag && !pick) pick = o;
            if (op == Op::NearestWithTag && o != ctx.gameObject && o->transform) {
                float d = (o->transform->Position() - me).Magnitude();
                if (d < best) { best = d; pick = o; }
            }
        }
        if (op == Op::CountWithTag) return (float)count;
        return VsValue::Object(pick ? pick->InstanceId() : 0);   // an object (reads as its name as text)
    }
};

// ---- Actions ---------------------------------------------------------------
// Move a named object (empty = self): in0..in2 = x, y, z (unset axes keep their value).
struct TeleportNode : VsNode {
    std::string name;
    explicit TeleportNode(std::string n) : name(std::move(n)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        GameObject* o = VsPinOrName(*this, 3, g, ctx, name);
        if (o && o->transform) {
            Vec3 p = o->transform->Position();
            if (HasInput(0)) p.x = In(0, g, ctx).AsFloat();
            if (HasInput(1)) p.y = In(1, g, ctx).AsFloat();
            if (HasInput(2)) p.z = In(2, g, ctx).AsFloat();
            o->transform->SetPosition(p);
        }
        return 0;
    }
};
// Show / hide this object's mesh or sprite without deactivating it. in0 = visible.
struct SetVisibleNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        bool vis = In(0, g, ctx, VsValue{true}).AsBool();
        if (ctx.gameObject) {
            if (auto* mr = ctx.gameObject->GetComponent<MeshRenderer>()) mr->enabled = vis;
            if (auto* sr = ctx.gameObject->GetComponent<SpriteRenderer>()) sr->enabled = vis;
        }
        return 0;
    }
};
struct GameControlNode : VsNode {
    enum class Op { Quit, Pause, ShowCursor } op;
    explicit GameControlNode(Op o) : op(o) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        switch (op) {
            case Op::Quit:       Game::RequestQuit(); break;
            case Op::Pause:      Game::SetPaused(In(0, g, ctx, VsValue{true}).AsBool()); break;
            case Op::ShowCursor: Cursor::Capture(!In(0, g, ctx, VsValue{true}).AsBool()); break;
        }
        return 0;
    }
};

// Show UI: activate a UI object (blank = self) and play its UI Animation intro.
// Hide UI: play its outro, then hide it (instantly with no UI Animation).
struct ShowHideUINode : VsNode {
    bool show; std::string name;
    ShowHideUINode(bool s, std::string n) : show(s), name(std::move(n)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph&, VsContext& ctx) override {
        GameObject* o = VsFindObject(ctx, name);
        if (!o) return 0;
        auto* ua = o->GetComponent<UIAnimation>();
        if (show) { o->active = true; if (ua) ua->Play(); }
        else if (ua && o->active) ua->PlayOut();
        else o->active = false;
        return 0;
    }
};

// ---- Flow ------------------------------------------------------------------
// Branch on a probability: True with chance in0 (0..1), else False.
struct ChanceBranchNode : VsNode {
    int ExecOutCount() const override { return 2; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        float p = In(0, g, ctx, VsValue{0.5f}).AsFloat();
        float r = (float)(std::rand() % 10000) / 10000.0f;
        return r < p ? 0 : 1;
    }
};
// Counts how many times it has run; Count reads it. in0 = reset (true resets first).
struct CounterNode : VsNode {
    int count = 0;
    int ExecOutCount() const override { return 1; }
    VsValue Eval(int, NodeGraph&, VsContext&) override { return (float)count; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (HasInput(0) && In(0, g, ctx).AsBool()) count = 0;
        ++count;
        return 0;
    }
};
// Lets the flow through every Nth time (in0 = N).
struct EveryNNode : VsNode {
    int calls = 0;
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        int n = std::max(1, (int)std::lround(In(0, g, ctx, VsValue{2.0f}).AsFloat()));
        return (++calls % n) == 0 ? 0 : -1;
    }
};

// The value handed to an On Custom Event (from Send / Broadcast Event).
struct EventValueNode : VsNode {
    bool wantName;
    explicit EventValueNode(bool n) : wantName(n) {}
    VsValue Eval(int, NodeGraph&, VsContext& ctx) override {
        auto it = ctx.variables.find(wantName ? "event_name" : "event_value");
        return it != ctx.variables.end() ? it->second : VsValue{};
    }
};

// Reject Rpc: inside On Validate Rpc, refuse the RPC being checked (it won't
// run on the server or reach anyone, and counts against the sender).
struct RejectRpcNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph&, VsContext& ctx) override {
        ctx.variables["rpc_reject"] = VsValue(true);
        return 0;
    }
};

} // namespace okay::vs
