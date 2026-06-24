#pragma once
#include "okay/VisualScript/NodeGraph.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Net/NetworkManager.hpp"
#include "okay/Platform/Steam/Steam.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Input/Input.hpp"
#include <cmath>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace okay::vs {

/// A shared, lazily-seeded RNG for the Random nodes. Deterministic seed so graph
/// behavior is reproducible across runs (tests, replays); games that want true
/// randomness can drive a seed in via a variable.
inline float VsRandom01() {
    static std::mt19937 rng(0xC0FFEEu);
    static std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    return dist(rng);
}


// ---- Event entry nodes (one exec output) ------------------------------
struct EventNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph&, VsContext&) override { return 0; }
};

// ---- Data nodes -------------------------------------------------------
struct ConstNode : VsNode {
    VsValue value;
    explicit ConstNode(VsValue v) : value(std::move(v)) {}
    VsValue Eval(int, NodeGraph&, VsContext&) override { return value; }
};

struct GetVarNode : VsNode {
    std::string name;
    explicit GetVarNode(std::string n) : name(std::move(n)) {}
    VsValue Eval(int, NodeGraph&, VsContext& ctx) override {
        auto it = ctx.variables.find(name);
        return it != ctx.variables.end() ? it->second : VsValue{};
    }
};

struct TimeNode : VsNode {
    VsValue Eval(int, NodeGraph&, VsContext&) override { return Time::ElapsedTime(); }
};

struct DeltaTimeNode : VsNode {
    VsValue Eval(int, NodeGraph&, VsContext& ctx) override { return ctx.deltaTime; }
};

struct AxisXNode : VsNode {
    VsValue Eval(int, NodeGraph&, VsContext&) override { return Input::AxisWASD().x; }
};
struct AxisYNode : VsNode {
    VsValue Eval(int, NodeGraph&, VsContext&) override { return Input::AxisWASD().y; }
};

struct BinaryOpNode : VsNode {
    enum class Op { Add, Sub, Mul, Div } op;
    explicit BinaryOpNode(Op o) : op(o) {}
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        float a = In(0, g, ctx).AsFloat();
        float b = In(1, g, ctx).AsFloat();
        switch (op) {
            case Op::Add: return a + b;
            case Op::Sub: return a - b;
            case Op::Mul: return a * b;
            case Op::Div: return b != 0.0f ? a / b : 0.0f;
        }
        return 0.0f;
    }
};

struct CompareNode : VsNode {
    enum class Op { Gt, Lt, Ge, Le, Eq, Ne } op;
    explicit CompareNode(Op o) : op(o) {}
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        float a = In(0, g, ctx).AsFloat();
        float b = In(1, g, ctx).AsFloat();
        switch (op) {
            case Op::Gt: return a > b;
            case Op::Lt: return a < b;
            case Op::Ge: return a >= b;
            case Op::Le: return a <= b;
            case Op::Eq: return a == b;
            case Op::Ne: return a != b;
        }
        return false;
    }
};

// One-input math: Abs, Neg, Sqrt, Sin, Cos, Floor, Round, Sign (trig in radians).
struct UnaryMathNode : VsNode {
    enum class Op { Abs, Neg, Sqrt, Sin, Cos, Floor, Round, Sign } op;
    explicit UnaryMathNode(Op o) : op(o) {}
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        float a = In(0, g, ctx).AsFloat();
        switch (op) {
            case Op::Abs:   return std::fabs(a);
            case Op::Neg:   return -a;
            case Op::Sqrt:  return a > 0.0f ? std::sqrt(a) : 0.0f;
            case Op::Sin:   return std::sin(a);
            case Op::Cos:   return std::cos(a);
            case Op::Floor: return std::floor(a);
            case Op::Round: return std::round(a);
            case Op::Sign:  return a > 0.0f ? 1.0f : (a < 0.0f ? -1.0f : 0.0f);
        }
        return 0.0f;
    }
};

// Two-input math beyond the basic arithmetic ops: Mod, Min, Max, Pow.
struct MathFnNode : VsNode {
    enum class Op { Mod, Min, Max, Pow } op;
    explicit MathFnNode(Op o) : op(o) {}
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        float a = In(0, g, ctx).AsFloat();
        float b = In(1, g, ctx).AsFloat();
        switch (op) {
            case Op::Mod: return b != 0.0f ? std::fmod(a, b) : 0.0f;
            case Op::Min: return a < b ? a : b;
            case Op::Max: return a > b ? a : b;
            case Op::Pow: return std::pow(a, b);
        }
        return 0.0f;
    }
};

struct ClampNode : VsNode {   // in0 = value, in1 = lo, in2 = hi
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        float v = In(0, g, ctx).AsFloat();
        float lo = In(1, g, ctx).AsFloat();
        float hi = In(2, g, ctx).AsFloat();
        if (lo > hi) std::swap(lo, hi);
        return v < lo ? lo : (v > hi ? hi : v);
    }
};

struct LerpNode : VsNode {    // in0 = a, in1 = b, in2 = t (clamped 0..1)
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        float a = In(0, g, ctx).AsFloat();
        float b = In(1, g, ctx).AsFloat();
        float t = In(2, g, ctx).AsFloat();
        if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
        return a + (b - a) * t;
    }
};

struct RandomNode : VsNode {  // uniform 0..1
    VsValue Eval(int, NodeGraph&, VsContext&) override { return VsRandom01(); }
};
struct RandomRangeNode : VsNode {  // in0 = lo, in1 = hi
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        float lo = In(0, g, ctx).AsFloat();
        float hi = In(1, g, ctx).AsFloat();
        return lo + (hi - lo) * VsRandom01();
    }
};

// Boolean logic: And, Or, Xor (two inputs) and Not (one input).
struct LogicNode : VsNode {
    enum class Op { And, Or, Xor } op;
    explicit LogicNode(Op o) : op(o) {}
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        bool a = In(0, g, ctx).AsBool();
        bool b = In(1, g, ctx).AsBool();
        switch (op) {
            case Op::And: return a && b;
            case Op::Or:  return a || b;
            case Op::Xor: return a != b;
        }
        return false;
    }
};
struct NotNode : VsNode {
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override { return !In(0, g, ctx).AsBool(); }
};

// ---- More conditions (data nodes returning a bool) --------------------
// Type-aware equality: compares as text if either side is a string, else
// numerically. Pairs with Branch for "if this == that".
struct EqualsNode : VsNode {
    bool invert;   // NotEquals reuses this with invert = true
    explicit EqualsNode(bool inv) : invert(inv) {}
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        VsValue a = In(0, g, ctx), b = In(1, g, ctx);
        bool eq = (a.IsString() || b.IsString()) ? (a.AsString() == b.AsString())
                                                 : (a.AsFloat() == b.AsFloat());
        return invert ? !eq : eq;
    }
};

// |a - b| <= epsilon (in2, default 0.001) — float-safe equality.
struct ApproxNode : VsNode {
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        float a = In(0, g, ctx).AsFloat();
        float b = In(1, g, ctx).AsFloat();
        float eps = In(2, g, ctx, VsValue{0.001f}).AsFloat();
        return std::fabs(a - b) <= eps;
    }
};

// in1 <= in0 <= in2 (range test).
struct BetweenNode : VsNode {
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        float v = In(0, g, ctx).AsFloat();
        float lo = In(1, g, ctx).AsFloat();
        float hi = In(2, g, ctx).AsFloat();
        if (lo > hi) std::swap(lo, hi);
        return v >= lo && v <= hi;
    }
};

// True with probability p (0..1) — a coin-flip condition for variety.
struct ChanceNode : VsNode {
    float p;
    explicit ChanceNode(float prob) : p(prob) {}
    VsValue Eval(int, NodeGraph&, VsContext&) override { return VsRandom01() < p; }
};

// True if the named blackboard variable has been set.
struct HasVarNode : VsNode {
    std::string name;
    explicit HasVarNode(std::string n) : name(std::move(n)) {}
    VsValue Eval(int, NodeGraph&, VsContext& ctx) override {
        return ctx.variables.find(name) != ctx.variables.end();
    }
};

struct KeyUpNode : VsNode {   // key released this frame
    char key;
    explicit KeyUpNode(char k) : key(k) {}
    VsValue Eval(int, NodeGraph&, VsContext&) override { return Input::GetKeyUp(key); }
};

// Ternary: in0 = condition, in1 = if-true, in2 = if-false. Passes the *value*
// through unchanged (any type), so it works for numbers, strings or vectors.
struct SelectNode : VsNode {
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        return In(0, g, ctx).AsBool() ? In(1, g, ctx) : In(2, g, ctx);
    }
};

struct ConcatNode : VsNode {  // in0 .. in1 as strings (string building)
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        return In(0, g, ctx).AsString() + In(1, g, ctx).AsString();
    }
};

struct MakeVec3Node : VsNode {  // in0=x, in1=y, in2=z -> Vec3
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        return Vec3{In(0, g, ctx).AsFloat(), In(1, g, ctx).AsFloat(), In(2, g, ctx).AsFloat()};
    }
};
struct BreakVec3Node : VsNode {  // in0 = vec -> one component
    int axis;  // 0=x, 1=y, 2=z
    explicit BreakVec3Node(int a) : axis(a) {}
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        Vec3 v = In(0, g, ctx).AsVec3();
        return axis == 0 ? v.x : (axis == 1 ? v.y : v.z);
    }
};

// Input query data nodes. Keys are single characters, matching OkayScript
// (e.g. "a", "w", " " for space).
struct KeyNode : VsNode {
    char key;
    explicit KeyNode(char k) : key(k) {}
    VsValue Eval(int, NodeGraph&, VsContext&) override { return Input::GetKey(key); }
};
struct KeyDownNode : VsNode {
    char key;
    explicit KeyDownNode(char k) : key(k) {}
    VsValue Eval(int, NodeGraph&, VsContext&) override { return Input::GetKeyDown(key); }
};
struct MouseXNode : VsNode {
    VsValue Eval(int, NodeGraph&, VsContext&) override { return Input::MousePosition().x; }
};
struct MouseYNode : VsNode {
    VsValue Eval(int, NodeGraph&, VsContext&) override { return Input::MousePosition().y; }
};

// Transform getters (read this object's local position).
struct GetPositionNode : VsNode {
    VsValue Eval(int, NodeGraph&, VsContext& ctx) override {
        return ctx.transform ? VsValue{ctx.transform->localPosition} : VsValue{Vec3::Zero};
    }
};
struct GetAxisNode : VsNode {   // 0=x, 1=y, 2=z of local position
    int axis;
    explicit GetAxisNode(int a) : axis(a) {}
    VsValue Eval(int, NodeGraph&, VsContext& ctx) override {
        if (!ctx.transform) return 0.0f;
        const Vec3& p = ctx.transform->localPosition;
        return axis == 0 ? p.x : (axis == 1 ? p.y : p.z);
    }
};

// ---- Exec nodes (perform side effects) --------------------------------
struct SetVarNode : VsNode {
    std::string name;
    explicit SetVarNode(std::string n) : name(std::move(n)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        ctx.variables[name] = In(0, g, ctx);
        return 0;
    }
};

struct PrintNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        Log::Info("[vs] ", In(0, g, ctx).AsString());
        return 0;
    }
};

struct TranslateNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (ctx.transform)
            ctx.transform->Translate({In(0, g, ctx).AsFloat(),
                                      In(1, g, ctx).AsFloat(), 0.0f});
        return 0;
    }
};

struct SetPositionNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (ctx.transform)
            ctx.transform->localPosition = {In(0, g, ctx).AsFloat(),
                                            In(1, g, ctx).AsFloat(), 0.0f};
        return 0;
    }
};

struct RotateNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (ctx.transform) ctx.transform->Rotate({0, 0, In(0, g, ctx).AsFloat()});
        return 0;
    }
};

struct BranchNode : VsNode {
    int ExecOutCount() const override { return 2; } // 0 = true, 1 = false
    int Exec(NodeGraph& g, VsContext& ctx) override {
        return In(0, g, ctx).AsBool() ? 0 : 1;
    }
};

// Add the data input to a variable in place (a counter / accumulator). Equivalent
// to wiring GetVar -> Add -> SetVar, but one node.
struct AddVarNode : VsNode {
    std::string name;
    explicit AddVarNode(std::string n) : name(std::move(n)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        float cur = 0.0f;
        if (auto it = ctx.variables.find(name); it != ctx.variables.end()) cur = it->second.AsFloat();
        ctx.variables[name] = cur + In(0, g, ctx).AsFloat();
        return 0;
    }
};

struct SetScaleNode : VsNode {   // in0=x, in1=y, in2=z (defaults to 1)
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (ctx.transform)
            ctx.transform->localScale = {In(0, g, ctx, 1.0f).AsFloat(),
                                         In(1, g, ctx, 1.0f).AsFloat(),
                                         In(2, g, ctx, 1.0f).AsFloat()};
        return 0;
    }
};

struct SetRotationNode : VsNode {   // in0 = degrees about Z (absolute)
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (ctx.transform)
            ctx.transform->localRotation = Quat::Euler({0.0f, 0.0f, In(0, g, ctx).AsFloat()});
        return 0;
    }
};

struct DestroyNode : VsNode {   // remove this object from the scene
    int ExecOutCount() const override { return 0; }   // terminal
    int Exec(NodeGraph&, VsContext& ctx) override {
        if (ctx.gameObject && ctx.gameObject->scene())
            ctx.gameObject->scene()->Destroy(ctx.gameObject);
        return -1;
    }
};

// Fan-out: fire each exec output in order (then stop this chain). Drives the
// downstream chains itself, so it can branch control flow into several paths.
struct SequenceNode : VsNode {
    int count;
    explicit SequenceNode(int c) : count(c < 1 ? 1 : c) {}
    int ExecOutCount() const override { return count; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        for (int i = 0; i < count; ++i) {
            int nx = Next(i);
            if (nx >= 0) g.Run(nx, ctx);
        }
        return -1;   // we already ran the outputs
    }
};

// Passes control through only the first time it's reached; afterward it stops the
// chain. Great for one-shot setup inside OnUpdate.
struct OnceNode : VsNode {
    bool fired = false;
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph&, VsContext&) override {
        if (fired) return -1;
        fired = true;
        return 0;
    }
};

// A self-resetting timer: passes through at most once every `interval` seconds
// (accumulating deltaTime across frames), stopping the chain in between. Ideal
// for "every N seconds" spawners / pulses placed under OnUpdate.
struct TimerNode : VsNode {
    float interval;
    float acc = 0.0f;
    explicit TimerNode(float s) : interval(s > 0.0f ? s : 0.0f) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph&, VsContext& ctx) override {
        acc += ctx.deltaTime;
        if (acc >= interval) { acc = 0.0f; return 0; }
        return -1;
    }
};

// Alternates between its two exec outputs on each execution (out 0, then 1, ...).
struct FlipFlopNode : VsNode {
    bool state = false;
    int ExecOutCount() const override { return 2; }
    int Exec(NodeGraph&, VsContext&) override {
        state = !state;
        return state ? 0 : 1;
    }
};

// ---- More instructions (exec nodes) -----------------------------------
// Run the downstream chain `count` times in a row, then continue. Handy for
// spawning N things or repeating an effect.
struct RepeatNode : VsNode {
    int count;
    explicit RepeatNode(int c) : count(c < 0 ? 0 : c) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        int nx = Next(0);
        for (int i = 0; i < count && nx >= 0; ++i) g.Run(nx, ctx);
        return -1;
    }
};

// Flip a boolean variable (false<->true); creates it as true if unset.
struct ToggleNode : VsNode {
    std::string name;
    explicit ToggleNode(std::string n) : name(std::move(n)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph&, VsContext& ctx) override {
        bool cur = false;
        if (auto it = ctx.variables.find(name); it != ctx.variables.end()) cur = it->second.AsBool();
        ctx.variables[name] = !cur;
        return 0;
    }
};

struct LogNode : VsNode {   // severity: 0 info (Print covers it), 1 warn, 2 error
    int level;
    explicit LogNode(int lv) : level(lv) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        std::string msg = In(0, g, ctx).AsString();
        if (level >= 2) Log::Error("[vs] ", msg);
        else if (level == 1) Log::Warning("[vs] ", msg);
        else Log::Info("[vs] ", msg);
        return 0;
    }
};

// Instantiate a prefab at this object's position (replicated object creation /
// bullets / pickups, all from the graph).
struct SpawnPrefabNode : VsNode {
    std::string path;
    explicit SpawnPrefabNode(std::string p) : path(std::move(p)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph&, VsContext& ctx) override {
        if (ctx.gameObject && ctx.gameObject->scene()) {
            GameObject* g = SceneSerializer::InstantiateFromFile(*ctx.gameObject->scene(), path);
            if (g && g->transform && ctx.transform) g->transform->localPosition = ctx.transform->localPosition;
        }
        return 0;
    }
};

// A one-shot delay: blocks the chain until `seconds` have accumulated, then lets
// control through from then on (unlike Timer, which repeats). Combine with Once
// for a single deferred action.
struct WaitNode : VsNode {
    float seconds;
    float acc = 0.0f;
    explicit WaitNode(float s) : seconds(s) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph&, VsContext& ctx) override {
        acc += ctx.deltaTime;
        return acc >= seconds ? 0 : -1;
    }
};

// ---- Multiplayer (NetworkManager) -------------------------------------
// Find the scene's NetworkManager (optionally creating one — used by host/join).
inline NetworkManager* VsNet(VsContext& ctx, bool create = false) {
    if (!ctx.gameObject || !ctx.gameObject->scene()) return nullptr;
    Scene* s = ctx.gameObject->scene();
    NetworkManager* n = s->FindObjectOfType<NetworkManager>();
    if (!n && create) n = s->CreateGameObject("__Network")->AddComponent<NetworkManager>();
    return n;
}

struct NetConnectedNode : VsNode {
    VsValue Eval(int, NodeGraph&, VsContext& ctx) override { auto* n = VsNet(ctx); return n && n->IsConnected(); }
};
struct NetIsServerNode : VsNode {
    VsValue Eval(int, NodeGraph&, VsContext& ctx) override { auto* n = VsNet(ctx); return n && n->IsServer(); }
};
struct NetIsClientNode : VsNode {
    VsValue Eval(int, NodeGraph&, VsContext& ctx) override { auto* n = VsNet(ctx); return n && n->IsClient(); }
};
struct NetPeersNode : VsNode {
    VsValue Eval(int, NodeGraph&, VsContext& ctx) override { auto* n = VsNet(ctx); return n ? (float)n->PeerCount() : 0.0f; }
};
struct NetIdNode : VsNode {
    VsValue Eval(int, NodeGraph&, VsContext& ctx) override { auto* n = VsNet(ctx); return n ? (float)n->LocalId() : 0.0f; }
};
struct NetGetVarNode : VsNode {
    std::string key;
    explicit NetGetVarNode(std::string k) : key(std::move(k)) {}
    VsValue Eval(int, NodeGraph&, VsContext& ctx) override { auto* n = VsNet(ctx); return n ? n->GetVar(key) : std::string{}; }
};

struct NetHostNode : VsNode {
    int port;
    explicit NetHostNode(int p) : port(p) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph&, VsContext& ctx) override { if (auto* n = VsNet(ctx, true)) n->StartServer((std::uint16_t)port); return 0; }
};
struct NetJoinNode : VsNode {
    std::string host; int port;
    NetJoinNode(std::string h, int p) : host(std::move(h)), port(p) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph&, VsContext& ctx) override { if (auto* n = VsNet(ctx, true)) n->StartClient(host, (std::uint16_t)port); return 0; }
};
struct NetSendNode : VsNode {
    std::string channel;
    explicit NetSendNode(std::string c) : channel(std::move(c)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override { if (auto* n = VsNet(ctx)) n->Send(channel, In(0, g, ctx).AsString()); return 0; }
};
struct NetSetVarNode : VsNode {
    std::string key;
    explicit NetSetVarNode(std::string k) : key(std::move(k)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override { if (auto* n = VsNet(ctx)) n->SetVar(key, In(0, g, ctx).AsString()); return 0; }
};
struct NetDisconnectNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph&, VsContext& ctx) override { if (auto* n = VsNet(ctx)) n->Stop(); return 0; }
};

// ---- Steam ------------------------------------------------------------
struct SteamNameNode : VsNode {
    VsValue Eval(int, NodeGraph&, VsContext&) override { return okay::Steam::Get().UserName(); }
};
struct SteamUnlockedNode : VsNode {
    std::string id;
    explicit SteamUnlockedNode(std::string i) : id(std::move(i)) {}
    VsValue Eval(int, NodeGraph&, VsContext&) override { return okay::Steam::Get().IsAchievementUnlocked(id); }
};
struct SteamGetStatNode : VsNode {
    std::string name;
    explicit SteamGetStatNode(std::string n) : name(std::move(n)) {}
    VsValue Eval(int, NodeGraph&, VsContext&) override { return okay::Steam::Get().GetStat(name); }
};
struct SteamUnlockNode : VsNode {
    std::string id;
    explicit SteamUnlockNode(std::string i) : id(std::move(i)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph&, VsContext&) override { okay::Steam::Get().UnlockAchievement(id); okay::Steam::Get().StoreStats(); return 0; }
};
struct SteamSetStatNode : VsNode {
    std::string name;
    explicit SteamSetStatNode(std::string n) : name(std::move(n)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override { okay::Steam::Get().SetStat(name, In(0, g, ctx).AsFloat()); return 0; }
};
struct SteamStoreNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph&, VsContext&) override { okay::Steam::Get().StoreStats(); return 0; }
};
struct SteamLeaderboardNode : VsNode {
    std::string board;
    explicit SteamLeaderboardNode(std::string b) : board(std::move(b)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override { okay::Steam::Get().UploadLeaderboardScore(board, (std::int32_t)In(0, g, ctx).AsFloat()); return 0; }
};

/// Construct a node from its text-format type name and arguments.
/// Returns nullptr (and sets `err`) for an unknown type.
std::unique_ptr<VsNode> MakeNode(const std::string& type,
                                 const std::vector<std::string>& args,
                                 std::string* err);

} // namespace okay::vs
