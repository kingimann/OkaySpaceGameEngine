#pragma once
// ---------------------------------------------------------------------------
// Loops and variable scopes: While, For Each, Break / Continue, the list nodes
// For Each walks over, scene / global variables and the Variable Changed events.
// ---------------------------------------------------------------------------
#include "okay/VisualScript/NodeGraph.hpp"
#include "okay/VisualScript/VsVariables.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include <cmath>
#include <string>

namespace okay::vs {

// While: runs Body again and again while Cond (in0) is true - checked before
// each run - then continues from Done (out1). A condition that never turns
// false is stopped by the event's step budget, like any runaway loop.
struct WhileNode : VsNode {
    int ExecOutCount() const override { return 2; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        const int body = Next(0);
        g.EnterLoop();
        while (!g.Aborted() && In(0, g, ctx).AsBool()) {
            if (body < 0) {
                g.Error(this, ctx, "Cond is true but nothing is wired to Body, so it would loop forever; the loop was skipped");
                break;
            }
            g.Run(body, ctx);
            if (g.TakeLoopSignal() == NodeGraph::kLoopBreak) break;
        }
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
    int ExecOutCount() const override { return 2; }
    VsValue Eval(int pin, NodeGraph&, VsContext&) override { return pin == 0 ? item : VsValue((float)index); }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        const VsValue list = In(0, g, ctx);
        const auto arr = list.AsArray();
        if (!arr) {
            if (HasInput(0)) g.Error(this, ctx, "its List input isn't a list (wire in New List, All With Tag, an Overlap's Objects...)");
            return 1;
        }
        const VsValue::Array items = *arr;
        const int body = Next(0);
        g.EnterLoop();
        for (std::size_t i = 0; i < items.size() && body >= 0; ++i) {
            item = items[i];
            index = (int)i;
            g.Run(body, ctx);
            if (g.TakeLoopSignal() == NodeGraph::kLoopBreak || g.Aborted()) break;
        }
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
                               " isn't inside the Body of a loop (For Loop, Repeat, While, For Each), so there's nothing to " +
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
        const long i = std::lround(In(1, g, ctx).AsFloat());
        if (i < 0 || i >= (long)a->size())
            g.Error(this, ctx, "index " + std::to_string(i) + " is outside the list (it has " + std::to_string(a->size()) +
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
        const long i = std::lround(In(1, g, ctx).AsFloat());
        if (i < 0 || i >= (long)a->size()) {
            g.Error(this, ctx, "index " + std::to_string(i) + " is outside the list (it has " + std::to_string(a->size()) +
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
        const VsValue v = In(0, g, ctx);
        VarMap& m = Store(ctx);
        if (op == Op::Set) m[name] = v;
        else {
            float cur = 0.0f;
            if (auto it = m.find(name); it != m.end()) cur = it->second.AsFloat();
            m[name] = cur + v.AsFloat();
        }
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
