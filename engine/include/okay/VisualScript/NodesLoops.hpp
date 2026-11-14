#pragma once
// ---------------------------------------------------------------------------
// Loops and variable scopes: While, For Each, Break / Continue, the list nodes
// For Each walks over, scene / global variables and the Variable Changed events.
// ---------------------------------------------------------------------------
#include "okay/VisualScript/NodeGraph.hpp"
#include "okay/VisualScript/VsVariables.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

namespace okay::vs {

// While: runs Body again and again while Cond (in0) is true - checked before
// each run - then continues from Done (out1). A condition that never turns
// false is stopped by the event's step budget, like any runaway loop.
struct WhileNode : VsNode {
    VsLoopLatch latch;
    int ExecOutCount() const override { return 2; }
    void OnSuspendRejected() override { latch.Finish(); }
    bool SaveFlow(VsFlowRecord& r) const override {
        if (!latch.busy) return false;
        latch.Save(r);
        return true;
    }
    void LoadFlow(const VsFlowRecord& r, NodeGraph& g) override { latch.Load(r, g); }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (!latch.Begin(g)) return -1;   // already parked; Resume owns the rest
        const int body = Next(0);
        g.EnterLoop();
        while (!g.Aborted()) {
            if (!latch.inBody) {
                if (!In(0, g, ctx).AsBool()) break;
                if (body < 0) {
                    g.Error(this, ctx, "Cond is true but nothing is wired to Body, so it would loop forever; the loop was skipped");
                    break;
                }
            }
            if (latch.Step(g, ctx, body) == NodeGraph::kSuspend) { g.LeaveLoop(); return NodeGraph::kSuspend; }
            if (g.TakeLoopSignal() == NodeGraph::kLoopBreak || g.Aborted()) break;
        }
        latch.Finish();
        g.LeaveLoop();
        return 1;
    }
};

// For Each: runs Body once per item of the List (in0), with Item (out0) and
// Index (out1) set, then Done. It walks the list as it was when the loop
// started, so the body may add to or remove from it safely.
struct ForEachNode : VsNode {
    VsValue item;
    int index = -1;
    VsLoopLatch latch;
    std::vector<VsValue> items;   // kept so the next For Each reuses the buffer
    int ExecOutCount() const override { return 2; }
    VsValue Eval(int pin, NodeGraph&, VsContext&) override { return pin == 0 ? item : VsValue((float)index); }
    void OnSuspendRejected() override { latch.Finish(); }
    bool SaveFlow(VsFlowRecord& r) const override {
        if (!latch.busy) return false;
        latch.Save(r);
        r.i0 = index;
        r.values = items;
        return true;
    }
    void LoadFlow(const VsFlowRecord& r, NodeGraph& g) override {
        latch.Load(r, g);
        index = r.i0;
        items = r.values;
        if (index >= 0 && index < (int)items.size()) item = items[(std::size_t)index];
    }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (!latch.Begin(g)) return -1;
        const int body = Next(0);
        if (!g.Resuming()) {
            items.clear();
            const VsValue list = In(0, g, ctx);
            const auto arr = list.AsArray();
            if (!arr) {
                if (HasInput(0)) {
                    const char* extra = list.IsMap() ? " (a map needs For Each Map)" : "";
                    g.Error(this, ctx, std::string("its List input isn't a list (wire in New List, All With Tag, an Overlap's Objects...)") + extra);
                }
                latch.Finish();
                return 1;
            }
            items.assign(arr->begin(), arr->end());
            index = items.empty() ? -1 : 0;
        }
        g.EnterLoop();
        while (body >= 0 && index >= 0 && index < (int)items.size() && !g.Aborted()) {
            item = items[(std::size_t)index];
            if (latch.Step(g, ctx, body) == NodeGraph::kSuspend) { g.LeaveLoop(); return NodeGraph::kSuspend; }
            if (g.TakeLoopSignal() == NodeGraph::kLoopBreak || g.Aborted()) break;
            if (index + 1 >= (int)items.size()) break;
            ++index;
        }
        latch.Finish();
        g.LeaveLoop();
        return 1;
    }
};

// Break: leave the loop whose Body this is (the loop goes on to Done).
// Continue: skip the rest of this run of the Body (the loop goes on to the next).
struct LoopJumpNode : VsNode {
    bool isBreak;
    explicit LoopJumpNode(bool brk) : isBreak(brk) {}
    int ExecOutCount() const override { return 0; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (!g.InLoop()) {
            g.Error(this, ctx, std::string(isBreak ? "Break" : "Continue") +
                               " isn't inside the Body of a loop (For Loop, Repeat, While, For Each, For Each Map), so there's nothing to " +
                               (isBreak ? "leave" : "skip"));
            return -1;
        }
        g.SignalLoop(isBreak ? NodeGraph::kLoopBreak : NodeGraph::kLoopContinue);
        return -1;
    }
};

// ---- Lists -------------------------------------------------------------
// A list is shared: Set Var x = New List, then List Add to x's value, and every
// node reading x sees the item.
inline VsValue::ArrayRef VsListIn(VsNode* n, const VsValue& v, NodeGraph& g, VsContext& ctx) {
    auto a = v.AsArray();
    if (!a) g.Error(n, ctx, "its List input isn't a list (wire in New List, a variable holding one, All With Tag...)");
    return a;
}

/// An index pin as a whole number. False for NaN / inf / absurdly large values (the caller
/// reports them), so a hostile index never reaches a float -> long cast it can't hold.
inline bool VsIndexFrom(const VsValue& v, long& out) {
    const float f = v.AsFloat();
    if (!std::isfinite(f) || std::fabs(f) > 1.0e9f) return false;
    out = std::lround(f);
    return true;
}
inline std::string VsIndexText(const VsValue& v) { return VsValue::FormatNumber(v.AsFloat()); }

struct NewListNode : VsNode {   // out0 = a new, empty list
    VsValue Eval(int, NodeGraph&, VsContext&) override { return VsValue::MakeArray(); }
};

struct ListAddNode : VsNode {   // in0 = list, in1 = item
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (auto a = VsListIn(this, In(0, g, ctx), g, ctx)) a->push_back(In(1, g, ctx));
        return 0;
    }
};

struct ListRemoveAtNode : VsNode {   // in0 = list, in1 = index
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        auto a = VsListIn(this, In(0, g, ctx), g, ctx);
        if (!a) return 0;
        const VsValue iv = In(1, g, ctx);
        long i = 0;
        if (!VsIndexFrom(iv, i) || i < 0 || i >= (long)a->size())
            g.Error(this, ctx, "index " + VsIndexText(iv) + " is outside the list (it has " + std::to_string(a->size()) +
                               " item(s), numbered from 0); nothing was removed");
        else
            a->erase(a->begin() + i);
        return 0;
    }
};

struct ListGetNode : VsNode {   // in0 = list, in1 = index -> item
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        auto a = VsListIn(this, In(0, g, ctx), g, ctx);
        if (!a) return {};
        const VsValue iv = In(1, g, ctx);
        long i = 0;
        if (!VsIndexFrom(iv, i) || i < 0 || i >= (long)a->size()) {
            g.Error(this, ctx, "index " + VsIndexText(iv) + " is outside the list (it has " + std::to_string(a->size()) +
                               " item(s), numbered from 0)");
            return {};
        }
        return (*a)[(std::size_t)i];
    }
};

struct ListLengthNode : VsNode {   // in0 = list -> how many items
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        auto a = In(0, g, ctx).AsArray();
        return a ? (float)a->size() : 0.0f;
    }
};

struct ListContainsNode : VsNode {   // in0 = list, in1 = item -> bool
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        auto a = VsListIn(this, In(0, g, ctx), g, ctx);
        if (!a) return false;
        const VsValue want = In(1, g, ctx);
        for (const VsValue& v : *a) {
            if (VsSameValue(v, want)) return true;
            // A name typed into the pin matches the object it names.
            if (v.IsObject() && want.IsString() && v.AsString() == want.AsString()) return true;
        }
        return false;
    }
};


// ---- More list nodes: insert, set, find, remove, sort, reverse, clear, copy ----
// A list or map is a reference: every variable and pin holding it sees a change.
// Copy List / Copy Map make an independent one. Every node reports a bad index or a
// non-list input through the graph's error hook (with the node's id) and changes nothing.
struct ListInsertNode : VsNode {   // in0 = list, in1 = index (0..length), in2 = item
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        auto a = VsListIn(this, In(0, g, ctx), g, ctx);
        if (!a) return 0;
        const VsValue iv = In(1, g, ctx);
        long i = 0;
        if (!VsIndexFrom(iv, i) || i < 0 || i > (long)a->size()) {
            g.Error(this, ctx, "index " + VsIndexText(iv) + " is outside the list (it has " + std::to_string(a->size()) +
                               " item(s); you can insert at 0 to " + std::to_string(a->size()) + "); nothing was inserted");
            return 0;
        }
        a->insert(a->begin() + i, In(2, g, ctx));
        return 0;
    }
};

struct ListSetNode : VsNode {   // in0 = list, in1 = index, in2 = item
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        auto a = VsListIn(this, In(0, g, ctx), g, ctx);
        if (!a) return 0;
        const VsValue iv = In(1, g, ctx);
        long i = 0;
        if (!VsIndexFrom(iv, i) || i < 0 || i >= (long)a->size()) {
            g.Error(this, ctx, "index " + VsIndexText(iv) + " is outside the list (it has " + std::to_string(a->size()) +
                               " item(s), numbered from 0); nothing was changed");
            return 0;
        }
        (*a)[(std::size_t)i] = In(2, g, ctx);
        return 0;
    }
};

struct ListFindNode : VsNode {   // in0 = list, in1 = item -> out0 index (-1 = not there), out1 found
    VsValue Eval(int pin, NodeGraph& g, VsContext& ctx) override {
        long at = -1;
        if (auto a = VsListIn(this, In(0, g, ctx), g, ctx)) {
            const VsValue want = In(1, g, ctx);
            for (std::size_t i = 0; i < a->size(); ++i) {
                const VsValue& v = (*a)[i];
                if (VsSameValue(v, want) || (v.IsObject() && want.IsString() && v.AsString() == want.AsString())) { at = (long)i; break; }
            }
        }
        return pin == 1 ? VsValue(at >= 0) : VsValue((float)at);
    }
};

struct ListRemoveItemNode : VsNode {   // in0 = list, in1 = item -> out0 removed? (the first match goes)
    bool removed = false;
    int ExecOutCount() const override { return 1; }
    VsValue Eval(int, NodeGraph&, VsContext&) override { return removed; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        removed = false;
        auto a = VsListIn(this, In(0, g, ctx), g, ctx);
        if (!a) return 0;
        const VsValue want = In(1, g, ctx);
        for (std::size_t i = 0; i < a->size(); ++i) {
            const VsValue& v = (*a)[i];
            if (VsSameValue(v, want) || (v.IsObject() && want.IsString() && v.AsString() == want.AsString())) {
                a->erase(a->begin() + (long)i);
                removed = true;
                break;
            }
        }
        return 0;
    }
};

struct ListSortNode : VsNode {   // in0 = list, in1 = descending?
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        auto a = VsListIn(this, In(0, g, ctx), g, ctx);
        if (!a) return 0;
        const bool desc = In(1, g, ctx).AsBool();
        std::stable_sort(a->begin(), a->end(), [desc](const VsValue& x, const VsValue& y) {
            return desc ? VsCompare(y, x) < 0 : VsCompare(x, y) < 0;
        });
        return 0;
    }
};

struct ListReverseNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (auto a = VsListIn(this, In(0, g, ctx), g, ctx)) std::reverse(a->begin(), a->end());
        return 0;
    }
};

struct ListClearNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (auto a = VsListIn(this, In(0, g, ctx), g, ctx)) a->clear();
        return 0;
    }
};

struct ListCopyNode : VsNode {   // in0 = list, in1 = deep? -> a new list
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        auto a = VsListIn(this, In(0, g, ctx), g, ctx);
        if (!a) return VsValue::MakeArray();
        if (In(1, g, ctx).AsBool()) return VsDeepCopy(VsValue(a));
        return VsValue(std::make_shared<VsValue::Array>(*a));
    }
};

// List Append: add every item of Other to the end of List (Other is read first, so appending a list
// to itself doubles it instead of running away).
struct ListAppendNode : VsNode {   // in0 = list, in1 = other list
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        auto a = VsListIn(this, In(0, g, ctx), g, ctx);
        if (!a) return 0;
        const VsValue ov = In(1, g, ctx);
        auto o = ov.AsArray();
        if (!o) { g.Error(this, ctx, "its Other input isn't a list; nothing was added"); return 0; }
        const VsValue::Array items = *o;
        a->insert(a->end(), items.begin(), items.end());
        return 0;
    }
};

// List Slice: a new list of Count items starting at Start. No Count wired = everything from Start.
// Start 0..length is fine (length gives an empty list); Count past the end stops at the end.
struct ListSliceNode : VsNode {   // in0 = list, in1 = start, in2 = count -> a new list
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        VsValue out = VsValue::MakeArray();
        auto a = VsListIn(this, In(0, g, ctx), g, ctx);
        if (!a) return out;
        const VsValue sv = In(1, g, ctx);
        long start = 0;
        if (!VsIndexFrom(sv, start) || start < 0 || start > (long)a->size()) {
            g.Error(this, ctx, "start " + VsIndexText(sv) + " is outside the list (it has " + std::to_string(a->size()) +
                               " item(s), numbered from 0); the slice is empty");
            return out;
        }
        long count = (long)a->size() - start;
        if (HasInput(2)) {
            const VsValue cv = In(2, g, ctx);
            long c = 0;
            if (!VsIndexFrom(cv, c) || c < 0) {
                g.Error(this, ctx, "count " + VsIndexText(cv) + " isn't a whole number of zero or more; the slice is empty");
                return out;
            }
            count = std::min(count, c);
        }
        out.AsArray()->assign(a->begin() + start, a->begin() + start + count);
        return out;
    }
};

// List Pop: remove the last item and give it back. An empty list is not an error: Popped is false.
struct ListPopNode : VsNode {   // in0 = list -> out0 item, out1 popped?
    VsValue item;
    bool popped = false;
    int ExecOutCount() const override { return 1; }
    VsValue Eval(int pin, NodeGraph&, VsContext&) override { return pin == 0 ? item : VsValue(popped); }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        item = VsValue{};
        popped = false;
        auto a = VsListIn(this, In(0, g, ctx), g, ctx);
        if (!a || a->empty()) return 0;
        item = a->back();
        a->pop_back();
        popped = true;
        return 0;
    }
};

// ---- Colors --------------------------------------------------------------------
struct MakeColorNode : VsNode {   // in0..in3 = r, g, b, a (0..1; a defaults to 1)
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        auto ch = [&](int i, float d) {
            const float f = In(i, g, ctx, d).AsFloat();
            return std::isnan(f) ? 0.0f : std::fmin(std::fmax(f, 0.0f), 1.0f);
        };
        return VsValue::Color(ch(0, 0.0f), ch(1, 0.0f), ch(2, 0.0f), ch(3, 1.0f));
    }
};

struct SplitColorNode : VsNode {   // in0 = colour -> r, g, b, a
    VsValue Eval(int pin, NodeGraph& g, VsContext& ctx) override {
        const VsColor c = In(0, g, ctx).AsColor();
        return pin == 0 ? c.r : pin == 1 ? c.g : pin == 2 ? c.b : c.a;
    }
};

// Every active object with this tag, as a list (For Each walks it).
struct AllWithTagNode : VsNode {
    std::string tag;
    explicit AllWithTagNode(std::string t) : tag(std::move(t)) {}
    VsValue Eval(int, NodeGraph&, VsContext& ctx) override {
        VsValue out = VsValue::MakeArray();
        if (!ctx.gameObject || !ctx.gameObject->scene()) return out;
        for (const auto& up : ctx.gameObject->scene()->Objects())
            if (up && up->active && up->tag == tag) out.AsArray()->push_back(VsValue::Object(up->InstanceId()));
        return out;
    }
};

// ---- Scene / global variables -------------------------------------------
// Scene: shared by every graph in the scene. Global: kept across scene loads.
struct ScopedVarNode : VsNode {
    enum class Scope { Scene, Global } scope;
    enum class Op { Get, Set, Add } op;
    std::string name;
    ScopedVarNode(Scope s, Op o, std::string n) : scope(s), op(o), name(std::move(n)) {}
    VarMap& Store(VsContext& ctx) const {
        if (scope == Scope::Global) return GlobalVariables();
        return SceneVariables(ctx.gameObject ? ctx.gameObject->scene() : nullptr);
    }
    int ExecOutCount() const override { return op == Op::Get ? 0 : 1; }
    VsValue Eval(int, NodeGraph&, VsContext& ctx) override {
        VarMap& m = Store(ctx);
        auto it = m.find(name);
        return it != m.end() ? it->second : VsValue{};
    }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        VsValue v = In(0, g, ctx);
        VarMap& m = Store(ctx);
        if (op == Op::Add) {
            VsValue cur;
            if (auto it = m.find(name); it != m.end()) cur = it->second;
            v = (cur.IsVec3() || v.IsVec3()) ? VsValue(cur.AsVec3() + v.AsVec3()) : VsValue(cur.AsFloat() + v.AsFloat());
        }
        std::string err;
        if (!g.CoerceForStore(name, scope == Scope::Global ? VarScope::Global : VarScope::Scene, v, &err)) {
            g.Error(this, ctx, std::string(op == Op::Set ? "Set " : "Add ") + (scope == Scope::Global ? "Global" : "Scene") +
                               " Var '" + name + "': " + err + "; the variable keeps its value");
            return 0;
        }
        m[name] = std::move(v);
        return 0;
    }
};

// On Variable Changed (this object's / the scene's / a global variable): an
// event with the new Value (out0) and the Old one (out1). The component checks
// the watched variables after every event and frame and fires these.
struct VarChangedEventNode : VsNode {
    enum class Scope { Object, Scene, Global } scope;
    std::string name;    // the variable it watches
    VsValue value, old;
    VarChangedEventNode(Scope s, std::string n) : scope(s), name(std::move(n)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph&, VsContext&) override { return 0; }
    VsValue Eval(int pin, NodeGraph&, VsContext&) override { return pin == 0 ? value : old; }
};

} // namespace okay::vs
