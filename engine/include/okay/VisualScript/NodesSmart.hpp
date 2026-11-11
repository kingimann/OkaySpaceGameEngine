#pragma once
// Higher-level game logic: timing, fuzzy choices, a small behaviour tree,
// spatial queries, a remembered value, and a few debug steps. Each one is a
// single node. A behaviour node sets `outcome` (0 succeeded, 1 failed) so a
// parent can read it after running the child. Trees finish in one frame.
#include "okay/VisualScript/Nodes.hpp"
#include "okay/Core/SaveData.hpp"
#include "okay/Components/LineRenderer.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include "okay/Physics/Physics3D.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace okay::vs {

inline const char* kRememberPrefix = "vs.";

inline float SmartClock(float& clock, float dt) { clock += dt; return clock; }

inline bool SmartEdge(bool now, bool& seen, bool& last) {
    const bool fire = seen && now && !last;
    last = now;
    seen = true;
    return fire;
}

// True after Condition has stayed true for Seconds. A false frame starts over.
struct HeldLongNode : VsNode {
    float acc = 0.0f;
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        const float need = std::max(0.0f, In(1, g, ctx, VsValue{0.5f}).AsFloat());
        if (!In(0, g, ctx).AsBool()) { acc = 0.0f; return false; }
        acc += ctx.deltaTime;
        return acc >= need;
    }
};

// True when First and Second have each turned true within Seconds of each other.
struct HappenedNearNode : VsNode {
    float clock = 0.0f;
    float lastA = -1.0f, lastB = -1.0f;
    bool seenA = false, seenB = false, prevA = false, prevB = false;
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        clock += ctx.deltaTime;
        if (SmartEdge(In(0, g, ctx).AsBool(), seenA, prevA)) lastA = clock;
        if (SmartEdge(In(1, g, ctx).AsBool(), seenB, prevB)) lastB = clock;
        const float window = std::max(0.0f, In(2, g, ctx, VsValue{1.0f}).AsFloat());
        if (lastA < 0.0f || lastB < 0.0f) return false;
        return std::fabs(lastA - lastB) <= window;
    }
};

// Passes every Seconds while Condition stays true. A false frame starts the wait over.
struct EveryWhileNode : VsNode {
    float acc = 0.0f;
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (!In(0, g, ctx).AsBool()) { acc = 0.0f; return -1; }
        const float need = std::max(0.0f, In(1, g, ctx, VsValue{1.0f}).AsFloat());
        acc += ctx.deltaTime;
        if (acc < need) return -1;
        acc = 0.0f;
        return 0;
    }
};

// One step from Current toward Target, limited by Speed per second.
struct EaseTowardNode : VsNode {
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        const float cur = In(0, g, ctx).AsFloat();
        const float target = In(1, g, ctx).AsFloat();
        const float speed = std::max(0.0f, In(2, g, ctx, VsValue{1.0f}).AsFloat());
        const float step = speed * std::max(0.0f, ctx.deltaTime);
        const float d = target - cur;
        if (std::fabs(d) <= step) return target;
        return cur + (d > 0.0f ? step : -step);
    }
};

// Parks, and Value moves from From to To over Seconds. Then the flow continues.
struct BlendTimeNode : VsNode {
    float acc = 0.0f, from = 0.0f, to = 1.0f, seconds = 1.0f, cur = 0.0f;
    bool waiting = false;
    int ExecOutCount() const override { return 1; }
    VsValue Eval(int, NodeGraph&, VsContext&) override { return cur; }
    void OnSuspendRejected() override { waiting = false; }
    bool SaveFlow(VsFlowRecord& r) const override {
        if (!waiting) return false;
        r.b0 = true; r.f0 = acc; r.f1 = from; r.i0 = (int)(to * 1000.0f); r.i1 = (int)(seconds * 1000.0f);
        return true;
    }
    void LoadFlow(const VsFlowRecord& r, NodeGraph&) override {
        waiting = r.b0; acc = r.f0; from = r.f1; to = r.i0 / 1000.0f; seconds = r.i1 / 1000.0f;
    }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (!g.Resuming()) {
            if (waiting) return -1;
            from = In(0, g, ctx, VsValue{0.0f}).AsFloat();
            to = In(1, g, ctx, VsValue{1.0f}).AsFloat();
            seconds = std::max(0.0f, In(2, g, ctx, VsValue{1.0f}).AsFloat());
            acc = 0.0f;
            cur = from;
            if (seconds <= 0.0f) { cur = to; return 0; }
            waiting = true;
            return NodeGraph::kSuspend;
        }
        acc += ctx.deltaTime;
        const float t = seconds <= 0.0f ? 1.0f : std::min(1.0f, acc / seconds);
        cur = from + (to - from) * t;
        if (t >= 1.0f) { waiting = false; return 0; }
        return NodeGraph::kSuspend;
    }
};

// Seconds since it started. Reset clears it and does not pass.
struct StopwatchNode : VsNode {
    float acc = 0.0f;
    int ExecOutCount() const override { return 1; }
    VsValue Eval(int, NodeGraph&, VsContext&) override { return acc; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (HasInput(0) && In(0, g, ctx).AsBool()) { acc = 0.0f; return -1; }
        acc += ctx.deltaTime;
        return 0;
    }
};

// Bands of a 0 to 1 value. Slightly is the low band, Mostly the middle, Very the top.
struct FuzzyBandNode : VsNode {
    int band; // 0 slightly, 1 mostly, 2 very
    explicit FuzzyBandNode(int b) : band(b) {}
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        float v = In(0, g, ctx).AsFloat();
        if (v < 0.0f) v = 0.0f; else if (v > 1.0f) v = 1.0f;
        if (band == 0) return v >= 0.15f && v < 0.45f;
        if (band == 1) return v >= 0.45f && v < 0.75f;
        return v >= 0.75f;
    }
};

// Index of the highest score, counting from 0. A tie keeps the earlier one.
struct PickBestNode : VsNode {
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        int best = 0;
        float top = In(0, g, ctx).AsFloat();
        for (int i = 1; i < 4; ++i) {
            const float s = In(i, g, ctx).AsFloat();
            if (s > top) { top = s; best = i; }
        }
        return (float)best;
    }
};

// Next list entry, with no repeat until every entry has been used once.
struct ShuffleBagNode : VsNode {
    std::vector<VsValue> bag;
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        if (bag.empty()) {
            const auto arr = In(0, g, ctx).AsArray();
            if (!arr) { g.Error(this, ctx, "its List input isn't a list"); return {}; }
            bag.assign(arr->begin(), arr->end());
            for (int i = (int)bag.size() - 1; i > 0; --i) {
                const int j = (int)(VsRandom01() * (float)(i + 1));
                std::swap(bag[(std::size_t)i], bag[(std::size_t)j]);
            }
        }
        if (bag.empty()) return {};
        VsValue v = bag.back();
        bag.pop_back();
        return v;
    }
};

// True at random, about Rate times per second. Rate 0 never passes. A huge rate passes every frame.
struct ChanceEachSecondNode : VsNode {
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        const float rate = std::max(0.0f, In(0, g, ctx).AsFloat());
        float p = rate * std::max(0.0f, ctx.deltaTime);
        if (p > 1.0f) p = 1.0f;
        return VsRandom01() < p;
    }
};

inline int SmartRunChild(VsNode* self, NodeGraph& g, VsContext& ctx, int c) {
    if (c < 0) return -1;
    if (g.Run(c, ctx) == NodeGraph::RunStatus::Suspended) {
        g.Error(self, ctx, "a behaviour step paused the frame (a Wait inside the tree); the tree stopped");
        return -2;
    }
    VsNode* n = g.Node(c);
    return n ? n->outcome : 0;
}

// Runs the first child that succeeds.
struct TryFirstNode : VsNode {
    int ExecOutCount() const override { return 3; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        outcome = 1;
        for (int i = 0; i < 3; ++i) {
            const int s = SmartRunChild(this, g, ctx, Next(i));
            if (s == -2) { outcome = 1; return -1; }
            if (s == 0 && Next(i) >= 0) { outcome = 0; return -1; }
        }
        return -1;
    }
};

// Runs each child. Stops at the first failure.
struct AllSucceedNode : VsNode {
    int ExecOutCount() const override { return 3; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        bool any = false;
        outcome = 1;
        for (int i = 0; i < 3; ++i) {
            if (Next(i) < 0) continue;
            any = true;
            const int s = SmartRunChild(this, g, ctx, Next(i));
            if (s == -2 || s != 0) { outcome = 1; return -1; }
        }
        outcome = any ? 0 : 1;
        return -1;
    }
};

struct FlipOutcomeNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        const int s = SmartRunChild(this, g, ctx, Next(0));
        outcome = (s == 0) ? 1 : 0;
        return -1;
    }
};

struct AlwaysSucceedNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (Next(0) >= 0) SmartRunChild(this, g, ctx, Next(0));
        outcome = 0;
        return -1;
    }
};

// Runs Child until it fails, at most 16 times. Still succeeding then is an error.
struct RepeatUntilFailNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (Next(0) < 0) { g.Error(this, ctx, "nothing is wired to Child"); outcome = 1; return -1; }
        for (int i = 0; i < 16; ++i) {
            const int s = SmartRunChild(this, g, ctx, Next(0));
            if (s == -2) { outcome = 1; return -1; }
            if (s != 0) { outcome = 0; return -1; }
        }
        g.Error(this, ctx, "the child was still succeeding after 16 tries; the repeat stopped");
        outcome = 1;
        return -1;
    }
};

// Runs Child, then ignores it until Seconds have passed.
struct WaitBetweenNode : VsNode {
    float acc = 0.0f;
    bool cooling = false;
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        const float need = std::max(0.0f, In(0, g, ctx, VsValue{1.0f}).AsFloat());
        if (cooling) {
            acc += ctx.deltaTime;
            if (acc < need) { outcome = 1; return -1; }
            cooling = false;
        }
        const int s = SmartRunChild(this, g, ctx, Next(0));
        acc = 0.0f;
        cooling = true;
        outcome = (s == 0 || s == -1) ? 0 : 1;
        return -1;
    }
};

struct OutcomeLeafNode : VsNode {
    int which;
    explicit OutcomeLeafNode(int w) : which(w) {}
    int Exec(NodeGraph&, VsContext&) override { outcome = which; return -1; }
};

// Succeeds when Condition is true, and fails when it is not.
struct CheckOutcomeNode : VsNode {
    int ExecOutCount() const override { return 2; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        const bool ok = In(0, g, ctx).AsBool();
        outcome = ok ? 0 : 1;
        return ok ? 0 : 1;
    }
};

struct InConeNode : VsNode {
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        if (!ctx.transform) return false;
        const VsValue tv = In(0, g, ctx);
        if (!tv.IsObject() || !tv.AsObjectId()) return false;
        GameObject* to = nullptr;
        if (ctx.gameObject && ctx.gameObject->scene()) {
            for (const auto& up : ctx.gameObject->scene()->Objects())
                if (up && up->InstanceId() == tv.AsObjectId()) { to = up.get(); break; }
        }
        if (!to || !to->transform) return false;
        Vec3 d = to->transform->Position() - ctx.transform->Position();
        const float dist = d.Magnitude();
        if (dist < 1e-4f) return true;
        d = d * (1.0f / dist);
        const float cone = In(1, g, ctx, VsValue{60.0f}).AsFloat();
        const float need = std::cos(cone * 0.5f * 0.0174533f);
        const Vec3 fwd = ctx.transform->Forward().Normalized();
        return Vec3::Dot(fwd, d) >= need;
    }
};

struct LineOfSightNode : VsNode {
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        if (!ctx.transform || !ctx.gameObject || !ctx.gameObject->scene()) return false;
        Scene* s = ctx.gameObject->scene();
        if (!s->physicsEnabled) {
            g.Error(this, ctx, "line of sight needs physics turned on");
            return false;
        }
        const VsValue tv = In(0, g, ctx);
        GameObject* to = nullptr;
        if (tv.IsObject()) {
            for (const auto& up : s->Objects())
                if (up && up->InstanceId() == tv.AsObjectId()) { to = up.get(); break; }
        }
        if (!to || !to->transform) return false;
        Vec3 d = to->transform->Position() - ctx.transform->Position();
        const float dist = d.Magnitude();
        if (dist < 1e-4f) return true;
        d = d * (1.0f / dist);
        RaycastHit3D h = s->physics3D().Raycast(*s, ctx.transform->Position(), d, dist, ctx.gameObject);
        if (!h.hit) return true;
        if (h.gameObject == to) return true;
        return h.distance >= dist - 0.05f;
    }
};

struct InsideAreaNode : VsNode {
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        if (!ctx.transform) return false;
        const Vec3 c = In(0, g, ctx).AsVec3();
        Vec3 sz = In(1, g, ctx, VsValue{Vec3{2, 2, 2}}).AsVec3();
        sz.x = std::fabs(sz.x) * 0.5f; sz.y = std::fabs(sz.y) * 0.5f; sz.z = std::fabs(sz.z) * 0.5f;
        const Vec3 p = ctx.transform->Position();
        return std::fabs(p.x - c.x) <= sz.x && std::fabs(p.y - c.y) <= sz.y && std::fabs(p.z - c.z) <= sz.z;
    }
};

inline void SmartVisit(VsContext& ctx, const std::string& tag, float radius, bool farthest,
                       int& count, GameObject*& pick) {
    count = 0; pick = nullptr;
    if (!ctx.gameObject || !ctx.gameObject->scene()) return;
    const Vec3 me = ctx.transform ? ctx.transform->Position() : Vec3{0, 0, 0};
    float best = farthest ? -1.0f : 1e30f;
    for (const auto& up : ctx.gameObject->scene()->Objects()) {
        GameObject* o = up.get();
        if (!o || !o->active || !o->transform || o == ctx.gameObject) continue;
        if (!tag.empty() && o->tag != tag) continue;
        const float d = (o->transform->Position() - me).Magnitude();
        if (radius > 0.0f && d > radius) continue;
        ++count;
        if (!farthest && d < best) { best = d; pick = o; }
        if (farthest && d > best) { best = d; pick = o; }
    }
}

struct NearbyNode : VsNode {
    int mode; // 0 count, 1 nearest, 2 farthest
    explicit NearbyNode(int m) : mode(m) {}
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        const float radius = In(0, g, ctx, VsValue{0.0f}).AsFloat();
        const std::string tag = HasInput(1) ? In(1, g, ctx).AsString() : std::string();
        int count = 0; GameObject* pick = nullptr;
        SmartVisit(ctx, tag, radius, mode == 2, count, pick);
        if (mode == 0) return (float)count;
        return VsValue::Object(pick ? pick->InstanceId() : 0);
    }
};

struct SortByDistanceNode : VsNode {
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        const auto arr = In(0, g, ctx).AsArray();
        if (!arr) { g.Error(this, ctx, "its List input isn't a list"); return VsValue::MakeArray(); }
        const Vec3 me = ctx.transform ? ctx.transform->Position() : Vec3{0, 0, 0};
        Scene* s = ctx.gameObject ? ctx.gameObject->scene() : nullptr;
        struct Item { float d; VsValue v; };
        std::vector<Item> items;
        items.reserve(arr->size());
        for (const VsValue& v : *arr) {
            float d = 1e30f;
            if (s && v.IsObject()) {
                for (const auto& up : s->Objects())
                    if (up && up->InstanceId() == v.AsObjectId() && up->transform) {
                        d = (up->transform->Position() - me).Magnitude();
                        break;
                    }
            }
            items.push_back({d, v});
        }
        std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.d < b.d; });
        VsValue out = VsValue::MakeArray();
        for (const Item& it : items) out.AsArray()->push_back(it.v);
        return out;
    }
};

struct WhenChangedNode : VsNode {
    bool seen = false;
    std::string last;
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        const std::string now = In(0, g, ctx).AsString();
        const bool fire = seen && now != last;
        last = now;
        seen = true;
        return fire ? 0 : -1;
    }
};

struct DoUnlessNode : VsNode {
    int ExecOutCount() const override { return 2; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        const bool cond = In(0, g, ctx).AsBool();
        const bool unless = In(1, g, ctx).AsBool();
        return (cond && !unless) ? 0 : 1;
    }
};

inline std::string RememberKey(const std::string& name) { return std::string(kRememberPrefix) + name; }

struct RememberValueNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        const std::string key = RememberKey(In(0, g, ctx, VsValue{std::string("flag")}).AsString());
        const VsValue v = In(1, g, ctx);
        if (v.IsBool()) Save::SetString(key, v.AsBool() ? "b:1" : "b:0");
        else if (v.IsString()) Save::SetString(key, std::string("t:") + v.AsString());
        else Save::SetString(key, std::string("n:") + v.AsString());
        return 0;
    }
};

struct RecallValueNode : VsNode {
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        const std::string key = RememberKey(In(0, g, ctx, VsValue{std::string("flag")}).AsString());
        const std::string s = Save::GetString(key, "");
        if (s.size() >= 2 && s[1] == ':') {
            if (s[0] == 'b') return s.substr(2) == "1";
            if (s[0] == 'n') return VsValue{std::strtof(s.c_str() + 2, nullptr)};
            if (s[0] == 't') return s.substr(2);
        }
        return s;
    }
};

struct HealthEdgeNode : VsNode {
    bool full;
    bool seen = false, last = false;
    explicit HealthEdgeNode(bool f) : full(f) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        auto* hp = ctx.gameObject ? ctx.gameObject->GetComponent<HealthStat>() : nullptr;
        if (!hp) { g.Error(this, ctx, "this object has no health"); return -1; }
        const bool now = full ? (hp->maxHealth > 0.0f && hp->health >= hp->maxHealth)
                              : (hp->health <= 0.0f);
        const bool fire = seen && now && !last;
        last = now;
        seen = true;
        return fire ? 0 : -1;
    }
};

struct TimedBuff { float add = 0, mul = 1, left = 0; };

struct AddBuffNode : VsNode {
    std::vector<TimedBuff> buffs;
    int ExecOutCount() const override { return 1; }
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        const float dt = ctx.deltaTime;
        float add = 0.0f, mul = 1.0f;
        std::vector<TimedBuff> keep;
        for (TimedBuff b : buffs) {
            b.left -= dt;
            if (b.left <= 0.0f) continue;
            add += b.add; mul *= b.mul; keep.push_back(b);
        }
        buffs.swap(keep);
        return (In(0, g, ctx).AsFloat() + add) * mul;
    }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if ((int)buffs.size() >= 8) {
            g.Error(this, ctx, "this stat already has 8 timed buffs; the new one was ignored");
            return 0;
        }
        TimedBuff b;
        b.add = In(1, g, ctx).AsFloat();
        b.mul = In(2, g, ctx, VsValue{1.0f}).AsFloat();
        b.left = std::max(0.0f, In(3, g, ctx, VsValue{1.0f}).AsFloat());
        if (b.left > 0.0f) buffs.push_back(b);
        return 0;
    }
};

struct AdvanceObjectiveNode : VsNode {
    int count = 0;
    int ExecOutCount() const override { return 2; }
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        const float total = std::max(1.0f, In(0, g, ctx, VsValue{1.0f}).AsFloat());
        return std::min(1.0f, (float)count / total);
    }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (HasInput(1) && In(1, g, ctx).AsBool()) { count = 0; return -1; }
        const int total = std::max(1, (int)std::lround(In(0, g, ctx, VsValue{3.0f}).AsFloat()));
        if (count >= total) return -1;
        ++count;
        return count >= total ? 1 : 0;
    }
};

struct AssertThisNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (!In(0, g, ctx).AsBool()) {
            std::string msg = HasInput(1) ? In(1, g, ctx).AsString() : std::string();
            if (msg.empty()) msg = "assertion failed";
            g.Error(this, ctx, msg);
        }
        return 0;
    }
};

struct LogOnceNode : VsNode {
    bool fired = false;
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (HasInput(1) && In(1, g, ctx).AsBool()) { fired = false; return -1; }
        if (fired) return -1;
        fired = true;
        Log::Info(In(0, g, ctx).AsString());
        return 0;
    }
};

struct DrawDebugLineNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (!ctx.gameObject) { g.Error(this, ctx, "this node needs an object"); return 0; }
        auto* line = ctx.gameObject->GetComponent<LineRenderer>();
        if (!line) line = ctx.gameObject->AddComponent<LineRenderer>();
        line->useWorldSpace = true;
        line->width = 0.08f;
        line->color = Color{1.0f, 0.85f, 0.2f, 1.0f};
        line->SetPositionCount(2);
        line->SetPosition(0, In(0, g, ctx).AsVec3());
        line->SetPosition(1, In(1, g, ctx, VsValue{Vec3{0, 1, 0}}).AsVec3());
        return 0;
    }
};

inline std::unique_ptr<VsNode> MakeSmartNode(const std::string& type, const std::vector<std::string>&) {
    if (type == "HeldLong") return std::make_unique<HeldLongNode>();
    if (type == "HappenedNear") return std::make_unique<HappenedNearNode>();
    if (type == "EveryWhile") return std::make_unique<EveryWhileNode>();
    if (type == "EaseToward") return std::make_unique<EaseTowardNode>();
    if (type == "BlendTime") return std::make_unique<BlendTimeNode>();
    if (type == "Stopwatch") return std::make_unique<StopwatchNode>();
    if (type == "IsSlightly") return std::make_unique<FuzzyBandNode>(0);
    if (type == "IsMostly") return std::make_unique<FuzzyBandNode>(1);
    if (type == "IsVery") return std::make_unique<FuzzyBandNode>(2);
    if (type == "PickBest") return std::make_unique<PickBestNode>();
    if (type == "ShuffleBag") return std::make_unique<ShuffleBagNode>();
    if (type == "ChanceEachSecond") return std::make_unique<ChanceEachSecondNode>();
    if (type == "TryFirst") return std::make_unique<TryFirstNode>();
    if (type == "AllSucceed") return std::make_unique<AllSucceedNode>();
    if (type == "FlipOutcome") return std::make_unique<FlipOutcomeNode>();
    if (type == "AlwaysSucceed") return std::make_unique<AlwaysSucceedNode>();
    if (type == "RepeatUntilFail") return std::make_unique<RepeatUntilFailNode>();
    if (type == "WaitBetween") return std::make_unique<WaitBetweenNode>();
    if (type == "SucceedNow") return std::make_unique<OutcomeLeafNode>(0);
    if (type == "FailNow") return std::make_unique<OutcomeLeafNode>(1);
    if (type == "CheckOutcome") return std::make_unique<CheckOutcomeNode>();
    if (type == "InCone") return std::make_unique<InConeNode>();
    if (type == "LineOfSight") return std::make_unique<LineOfSightNode>();
    if (type == "InsideArea") return std::make_unique<InsideAreaNode>();
    if (type == "CountNearby") return std::make_unique<NearbyNode>(0);
    if (type == "NearestObject") return std::make_unique<NearbyNode>(1);
    if (type == "FarthestObject") return std::make_unique<NearbyNode>(2);
    if (type == "SortByDistance") return std::make_unique<SortByDistanceNode>();
    if (type == "WhenChanged") return std::make_unique<WhenChangedNode>();
    if (type == "DoUnless") return std::make_unique<DoUnlessNode>();
    if (type == "RememberValue") return std::make_unique<RememberValueNode>();
    if (type == "RecallValue") return std::make_unique<RecallValueNode>();
    if (type == "HealthEmpty") return std::make_unique<HealthEdgeNode>(false);
    if (type == "HealthFull") return std::make_unique<HealthEdgeNode>(true);
    if (type == "AddBuff") return std::make_unique<AddBuffNode>();
    if (type == "AdvanceObjective") return std::make_unique<AdvanceObjectiveNode>();
    if (type == "AssertThis") return std::make_unique<AssertThisNode>();
    if (type == "LogOnce") return std::make_unique<LogOnceNode>();
    if (type == "DrawDebugLine") return std::make_unique<DrawDebugLineNode>();
    return nullptr;
}

} // namespace okay::vs
