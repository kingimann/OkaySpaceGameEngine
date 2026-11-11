#pragma once
// Logic and flow that the earlier nodes did not cover: Not and / Not or,
// list-wide All True / Any True, a do-while, a counted loop with a step,
// waits, edges, a counter that stops at a target, and a named state.
// Each one reports a node error instead of hanging when a loop cannot end.
#include "okay/VisualScript/Nodes.hpp"
#include "okay/VisualScript/NodesLoops.hpp"
#include <cmath>
#include <string>

namespace okay::vs {

inline const char* kStateKey = "state";

inline std::string StateOf(VsContext& ctx) {
    auto it = ctx.variables.find(kStateKey);
    return it == ctx.variables.end() ? std::string() : it->second.AsString();
}

inline bool LogicPresent(const VsValue& v) {
    if (v.IsObject()) return v.AsObjectId() != 0;
    if (v.IsString()) return !v.AsString().empty();
    return true;
}

// Not and: false on the first input skips the second (it may have side effects).
struct NotAndNode : VsNode {
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        if (!In(0, g, ctx).AsBool()) return true;
        return !In(1, g, ctx).AsBool();
    }
};
struct NotOrNode : VsNode {
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        if (In(0, g, ctx).AsBool()) return false;
        return !In(1, g, ctx).AsBool();
    }
};

// Every / any entry of a list. Stops at the first decisive entry.
struct ListBoolNode : VsNode {
    bool all;
    explicit ListBoolNode(bool a) : all(a) {}
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        const VsValue list = In(0, g, ctx);
        const auto arr = list.AsArray();
        if (!arr) {
            g.Error(this, ctx, "its List input isn't a list");
            return false;
        }
        if (all) {
            for (const VsValue& v : *arr) if (!v.AsBool()) return false;
            return true;
        }
        for (const VsValue& v : *arr) if (v.AsBool()) return true;
        return false;
    }
};

struct IsEvenNode : VsNode {
    bool odd;
    explicit IsEvenNode(bool o) : odd(o) {}
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        const int n = (int)std::lround(In(0, g, ctx).AsFloat());
        const bool even = (n % 2) == 0;
        return odd ? !even : even;
    }
};

// Item at Index, or an empty value when the index is outside the list.
struct PickByIndexNode : VsNode {
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        const VsValue list = In(1, g, ctx);
        const auto arr = list.AsArray();
        if (!arr) { g.Error(this, ctx, "its List input isn't a list"); return {}; }
        const int i = (int)std::lround(In(0, g, ctx).AsFloat());
        if (i < 0 || i >= (int)arr->size()) return {};
        return (*arr)[(std::size_t)i];
    }
};

// The first wired value that is present: a live object, or text that isn't empty.
// Numbers and yes/no values count as present, including zero and false.
struct FirstValidNode : VsNode {
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        for (int i = 0; i < 4; ++i) {
            if (!HasInput(i)) continue;
            const VsValue v = In(i, g, ctx);
            if (LogicPresent(v)) return v;
        }
        return {};
    }
};

// The first true condition wins. Later conditions are not read.
struct BranchInOrderNode : VsNode {
    int ExecOutCount() const override { return 4; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        for (int i = 0; i < 3; ++i)
            if (In(i, g, ctx).AsBool()) return i;
        return 3;
    }
};

// Body runs once, then again while Condition stays true. A body that is not
// wired, while the condition is true, is an error instead of a stuck loop.
struct DoWhileNode : VsNode {
    bool started = false;
    VsLoopLatch latch;
    int ExecOutCount() const override { return 2; }
    void OnSuspendRejected() override { latch.Finish(); started = false; }
    bool SaveFlow(VsFlowRecord& r) const override {
        if (!latch.busy) return false;
        latch.Save(r);
        r.b2 = started;
        return true;
    }
    void LoadFlow(const VsFlowRecord& r, NodeGraph& g) override { latch.Load(r, g); started = r.b2; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (!latch.Begin(g)) return -1;
        const int body = Next(0);
        if (!g.Resuming()) started = false;
        g.EnterLoop();
        while (!g.Aborted()) {
            if (!latch.inBody) {
                if (started && !In(0, g, ctx).AsBool()) break;
                if (body < 0) {
                    g.Error(this, ctx, "nothing is wired to Body, so a true condition would loop forever; the loop was skipped");
                    break;
                }
            }
            if (latch.Step(g, ctx, body) == NodeGraph::kSuspend) { g.LeaveLoop(); return NodeGraph::kSuspend; }
            if (g.TakeLoopSignal() == NodeGraph::kLoopBreak || g.Aborted()) break;
            started = true;
        }
        latch.Finish();
        g.LeaveLoop();
        return 1;
    }
};

// Count from First toward Last by Step (negative Step counts down). Step 0 is an error.
struct ForStepNode : VsNode {
    int index = 0, first = 0, last = 0, step = 1;
    VsLoopLatch latch;
    int ExecOutCount() const override { return 2; }
    VsValue Eval(int, NodeGraph&, VsContext&) override { return (float)index; }
    void OnSuspendRejected() override { latch.Finish(); }
    bool SaveFlow(VsFlowRecord& r) const override {
        if (!latch.busy) return false;
        latch.Save(r);
        r.i0 = index; r.i1 = first; r.i3 = last; r.f0 = (float)step;
        return true;
    }
    void LoadFlow(const VsFlowRecord& r, NodeGraph& g) override {
        latch.Load(r, g);
        index = r.i0; first = r.i1; last = r.i3; step = (int)r.f0;
    }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (!latch.Begin(g)) return -1;
        const int body = Next(0);
        if (!g.Resuming()) {
            first = (int)std::lround(In(0, g, ctx, VsValue{0.0f}).AsFloat());
            last  = (int)std::lround(In(1, g, ctx, VsValue{0.0f}).AsFloat());
            step  = (int)std::lround(In(2, g, ctx, VsValue{1.0f}).AsFloat());
            index = first;
            if (step == 0) {
                g.Error(this, ctx, "Step is 0, so the count would never move; the loop was skipped");
                latch.Finish();
                return 1;
            }
        }
        g.EnterLoop();
        while (body >= 0 && !g.Aborted()) {
            if (latch.Step(g, ctx, body) == NodeGraph::kSuspend) { g.LeaveLoop(); return NodeGraph::kSuspend; }
            if (g.TakeLoopSignal() == NodeGraph::kLoopBreak || g.Aborted() || index == last) break;
            const int next = index + step;
            if ((step > 0 && next > last) || (step < 0 && next < last)) break;
            index = next;
        }
        latch.Finish();
        g.LeaveLoop();
        return 1;
    }
};

// Parks until Condition is true, then continues. Checked again each frame.
struct WaitUntilNode : VsNode {
    bool waiting = false;
    int ExecOutCount() const override { return 1; }
    void OnSuspendRejected() override { waiting = false; }
    bool SaveFlow(VsFlowRecord& r) const override { if (!waiting) return false; r.b0 = true; return true; }
    void LoadFlow(const VsFlowRecord& r, NodeGraph&) override { waiting = r.b0; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (In(0, g, ctx).AsBool()) { waiting = false; return 0; }
        waiting = true;
        return NodeGraph::kSuspend;
    }
};

// Parks while Condition is true, then continues the frame it becomes false.
struct WaitWhileNode : VsNode {
    bool waiting = false;
    int ExecOutCount() const override { return 1; }
    void OnSuspendRejected() override { waiting = false; }
    bool SaveFlow(VsFlowRecord& r) const override { if (!waiting) return false; r.b0 = true; return true; }
    void LoadFlow(const VsFlowRecord& r, NodeGraph&) override { waiting = r.b0; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (!In(0, g, ctx).AsBool()) { waiting = false; return 0; }
        waiting = true;
        return NodeGraph::kSuspend;
    }
};

// Fires once on the frame Condition changes. The first frame only remembers it.
struct EdgeNode : VsNode {
    bool rising;
    bool seen = false;
    bool last = false;
    explicit EdgeNode(bool up) : rising(up) {}
    int ExecOutCount() const override { return 1; }
    bool SaveFlow(VsFlowRecord& r) const override {
        if (!seen) return false;
        r.b0 = true; r.b1 = last;
        return true;
    }
    void LoadFlow(const VsFlowRecord& r, NodeGraph&) override { seen = r.b0; last = r.b1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        const bool now = In(0, g, ctx).AsBool();
        const bool fire = seen && (rising ? (now && !last) : (!now && last));
        last = now;
        seen = true;
        return fire ? 0 : -1;
    }
};

// Counts arrivals. Each passes until the count reaches Target, then Reached passes once.
struct CountUntilNode : VsNode {
    int count = 0;
    bool reached = false;
    int ExecOutCount() const override { return 2; }
    VsValue Eval(int, NodeGraph&, VsContext&) override { return (float)count; }
    bool SaveFlow(VsFlowRecord& r) const override {
        if (count == 0 && !reached) return false;
        r.i0 = count; r.b0 = reached;
        return true;
    }
    void LoadFlow(const VsFlowRecord& r, NodeGraph&) override { count = r.i0; reached = r.b0; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (HasInput(1) && In(1, g, ctx).AsBool()) { count = 0; reached = false; return -1; }
        if (reached) return -1;
        const int target = std::max(1, (int)std::lround(In(0, g, ctx, VsValue{1.0f}).AsFloat()));
        ++count;
        if (count >= target) { reached = true; return 1; }
        return 0;
    }
};

// First or Second, in proportion to the two weights. A zero total is an error.
struct WeightedBranchNode : VsNode {
    int ExecOutCount() const override { return 2; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        const float a = std::max(0.0f, In(0, g, ctx, VsValue{1.0f}).AsFloat());
        const float b = std::max(0.0f, In(1, g, ctx, VsValue{1.0f}).AsFloat());
        if (a + b <= 0.0f) {
            g.Error(this, ctx, "both weights are 0, so there is nothing to pick; the first branch was used");
            return 0;
        }
        return VsRandom01() * (a + b) < a ? 0 : 1;
    }
};

// Passes once after Condition has stayed true for Seconds. A false frame starts the wait over.
struct WaitSteadyNode : VsNode {
    float acc = 0.0f;
    bool held = false;
    int ExecOutCount() const override { return 1; }
    bool SaveFlow(VsFlowRecord& r) const override {
        if (acc == 0.0f && !held) return false;
        r.f0 = acc; r.b0 = held;
        return true;
    }
    void LoadFlow(const VsFlowRecord& r, NodeGraph&) override { acc = r.f0; held = r.b0; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        const bool on = In(0, g, ctx).AsBool();
        if (!on) { acc = 0.0f; held = false; return -1; }
        if (held) return -1;
        const float need = std::max(0.0f, In(1, g, ctx, VsValue{0.5f}).AsFloat());
        acc += ctx.deltaTime;
        if (acc < need) return -1;
        held = true;
        return 0;
    }
};

// Cycles First, Second, Third, then First again.
struct StepThroughNode : VsNode {
    int index = 0;
    int ExecOutCount() const override { return 3; }
    bool SaveFlow(VsFlowRecord& r) const override { if (index == 0) return false; r.i0 = index; return true; }
    void LoadFlow(const VsFlowRecord& r, NodeGraph&) override { index = r.i0; }
    int Exec(NodeGraph&, VsContext&) override {
        const int out = index % 3;
        index = (index + 1) % 3;
        return out;
    }
};

// Success when Condition becomes true, Timed Out when Seconds pass first.
struct WaitLimitNode : VsNode {
    float acc = 0.0f;
    bool waiting = false;
    int ExecOutCount() const override { return 2; }
    void OnSuspendRejected() override { waiting = false; }
    bool SaveFlow(VsFlowRecord& r) const override {
        if (!waiting) return false;
        r.b0 = true; r.f0 = acc;
        return true;
    }
    void LoadFlow(const VsFlowRecord& r, NodeGraph&) override { waiting = r.b0; acc = r.f0; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        // A new event while this is already parked must not restart the clock.
        // Resume (the next frame) is what advances it.
        if (!g.Resuming()) {
            if (waiting) return -1;
            acc = 0.0f;
        }
        if (In(0, g, ctx).AsBool()) { waiting = false; return 0; }
        const float limit = std::max(0.0f, In(1, g, ctx, VsValue{1.0f}).AsFloat());
        if (g.Resuming()) acc += ctx.deltaTime;
        if (acc >= limit) { waiting = false; return 1; }
        waiting = true;
        return NodeGraph::kSuspend;
    }
};

struct SetStateNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        ctx.variables[kStateKey] = In(0, g, ctx, VsValue{std::string("idle")});
        return 0;
    }
};
struct GetStateNode : VsNode {
    VsValue Eval(int, NodeGraph&, VsContext& ctx) override { return StateOf(ctx); }
};
struct IsInStateNode : VsNode {
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        return StateOf(ctx) == In(0, g, ctx, VsValue{std::string("idle")}).AsString();
    }
};
struct StateEdgeNode : VsNode {
    bool enter;
    bool seen = false;
    bool last = false;
    explicit StateEdgeNode(bool e) : enter(e) {}
    int ExecOutCount() const override { return 1; }
    bool SaveFlow(VsFlowRecord& r) const override {
        if (!seen) return false;
        r.b0 = true; r.b1 = last;
        return true;
    }
    void LoadFlow(const VsFlowRecord& r, NodeGraph&) override { seen = r.b0; last = r.b1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        const bool now = StateOf(ctx) == In(0, g, ctx, VsValue{std::string("idle")}).AsString();
        const bool fire = seen && (enter ? (now && !last) : (!now && last));
        last = now;
        seen = true;
        return fire ? 0 : -1;
    }
};

inline std::unique_ptr<VsNode> MakeLogicRoundNode(const std::string& type, const std::vector<std::string>&) {
    if (type == "NotAnd") return std::make_unique<NotAndNode>();
    if (type == "NotOr") return std::make_unique<NotOrNode>();
    if (type == "AllTrue") return std::make_unique<ListBoolNode>(true);
    if (type == "AnyTrue") return std::make_unique<ListBoolNode>(false);
    if (type == "IsEven") return std::make_unique<IsEvenNode>(false);
    if (type == "IsOdd") return std::make_unique<IsEvenNode>(true);
    if (type == "PickByIndex") return std::make_unique<PickByIndexNode>();
    if (type == "FirstValid") return std::make_unique<FirstValidNode>();
    if (type == "BranchInOrder") return std::make_unique<BranchInOrderNode>();
    if (type == "DoWhile") return std::make_unique<DoWhileNode>();
    if (type == "ForStep") return std::make_unique<ForStepNode>();
    if (type == "WaitUntil") return std::make_unique<WaitUntilNode>();
    if (type == "WaitWhile") return std::make_unique<WaitWhileNode>();
    if (type == "BecameTrue") return std::make_unique<EdgeNode>(true);
    if (type == "BecameFalse") return std::make_unique<EdgeNode>(false);
    if (type == "CountUntil") return std::make_unique<CountUntilNode>();
    if (type == "WeightedBranch") return std::make_unique<WeightedBranchNode>();
    if (type == "WaitSteady") return std::make_unique<WaitSteadyNode>();
    if (type == "StepThrough") return std::make_unique<StepThroughNode>();
    if (type == "WaitLimit") return std::make_unique<WaitLimitNode>();
    if (type == "SetState") return std::make_unique<SetStateNode>();
    if (type == "GetState") return std::make_unique<GetStateNode>();
    if (type == "IsInState") return std::make_unique<IsInStateNode>();
    if (type == "WhenStateBecomes") return std::make_unique<StateEdgeNode>(true);
    if (type == "WhenStateLeaves") return std::make_unique<StateEdgeNode>(false);
    return nullptr;
}

} // namespace okay::vs
