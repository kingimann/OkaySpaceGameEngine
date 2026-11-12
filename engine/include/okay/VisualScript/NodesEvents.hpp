#pragma once
// ---------------------------------------------------------------------------
// Visual-script custom events with typed parameters, and dispatchers.
//
//   event Hit(Number amount, Object who)       declared in the graph (see VsDecl.hpp)
//   On Event Hit        entry node: data outputs are the parameters
//   Raise Event Hit     runs the target object's (default: this object's) handlers now
//   dispatcher Died(Object who)
//   Bind  Died -> OnBossDied    on another object's dispatcher; handler = event or function
//   Call Dispatcher Died        runs every bound handler, in the order they were bound
//
// Delivery is immediate and depth-first: a handler that raises another event runs that
// one to the end before it continues. A chain of raises more than NodeGraph::kMaxEventDepth
// deep is refused with an error on the raising node.
// ---------------------------------------------------------------------------
#include "okay/VisualScript/NodesWave4.hpp"
#include "okay/VisualScript/NodesFunctions.hpp"
#include "okay/VisualScript/VsEvents.hpp"
#include <cctype>
#include <memory>
#include <string>
#include <vector>

namespace okay::vs {

inline std::string VsEventNodeName(const std::string& text) {
    std::size_t a = 0, b = text.size();
    while (a < b && std::isspace((unsigned char)text[a])) ++a;
    while (b > a && std::isspace((unsigned char)text[b - 1])) --b;
    return text.substr(a, b - a);
}

/// How many handlers deep the running raise chain is (shared by every graph).
inline int& VsEventDepth() { static int d = 0; return d; }
struct VsEventDepthGuard {
    VsEventDepthGuard() { ++VsEventDepth(); }
    ~VsEventDepthGuard() { --VsEventDepth(); }
};

/// "Raise Event 'Hit' (node #7) of 'Player' (player.okayvs)": what the debugger shows as the raiser.
inline std::string VsEventOrigin(const char* what, const std::string& name, const VsNode* n, const NodeGraph& g, const VsContext& ctx) {
    std::string s = std::string(what) + " '" + name + "' (node #" + std::to_string(n ? n->fileId : -1) + ") of '" +
                    (ctx.gameObject ? ctx.gameObject->name : std::string("?")) + "'";
    if (!g.debugKey.empty()) s += " (" + g.debugKey + ")";
    return s;
}

/// Fit `args` to function `fn`'s parameter types (the function's signature has types only).
inline bool VsFitFunctionArgs(NodeGraph& lg, const std::string& fn, std::vector<VsValue>& args, std::string* err) {
    auto* fs = dynamic_cast<FunctionStartNode*>(lg.Node(lg.FunctionNode(fn)));
    if (!fs || !fs->sigOk) { if (err) *err = "there's no function named '" + fn + "'"; return false; }
    if (fs->sig.params.size() != args.size()) {
        if (err) *err = "function '" + fn + "' takes " + std::to_string(fs->sig.params.size()) + " parameter(s) but " + std::to_string(args.size()) + " were given";
        return false;
    }
    for (std::size_t i = 0; i < args.size(); ++i) {
        VarType vt; vt.base = fs->sig.params[i].type;
        std::string why;
        if (!VsCoerce(vt, args[i], lg.Structs(), &why)) {
            if (err) *err = "parameter '" + fs->sig.params[i].name + "' of function '" + fn + "' needs a " + PinTypeName(vt.base) + ": " + why;
            return false;
        }
    }
    return true;
}

// On Event: entry node of a declared event. Data outputs = the event's parameters.
struct OnCustomEventNode : VsNode {
    std::string name;
    explicit OnCustomEventNode(std::string n) : name(std::move(n)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph&, VsContext&) override { return 0; }
    VsValue Eval(int pin, NodeGraph& g, VsContext& ctx) override {
        if (const VsValue* v = g.EventArg(name, pin)) return *v;
        g.Error(this, ctx, "parameter #" + std::to_string(pin + 1) + " of event '" + name +
                           "' was read outside a handler for it (it reads 0)");
        return {};
    }
};

// Shared by Raise Event and Call Dispatcher: in0 = the target object (empty = this object),
// in1.. = the parameters. Data out 0 = how many handlers ran last time.
struct EventCallNode : VsNode {
    std::string name;
    VsValue last{0.0f};
    explicit EventCallNode(std::string n) : name(std::move(n)) {}
    int ExecOutCount() const override { return 1; }
    VsValue Eval(int pin, NodeGraph&, VsContext&) override { return pin == 0 ? last : VsValue{}; }

protected:
    /// The target object and the parameters fitted to this graph's declaration. False when an error
    /// was reported (nothing should run).
    bool Gather(NodeGraph& g, VsContext& ctx, const EventDecl& decl, GameObject*& target, std::vector<VsValue>& args, const char* verb) {
        target = ctx.gameObject;
        if (HasInput(0)) {
            const VsValue tv = In(0, g, ctx);
            target = VsResolveObject(ctx, tv);
            if (!target) {
                g.Error(this, ctx, std::string(verb) + ": " + (tv.IsObject() ? std::string("its Target object no longer exists (it was destroyed)")
                                                                              : "no object named '" + tv.AsString() + "' was found for its Target") +
                                   "; '" + name + "' was not sent");
                return false;
            }
        }
        if (!target) return false;
        args.clear();
        for (std::size_t i = 0; i < decl.params.size(); ++i)
            args.push_back(HasInput((int)i + 1) ? In((int)i + 1, g, ctx) : VsDefaultFor(decl.params[i].type, g.Structs()));
        std::string err;
        if (!g.FitEventArgs(decl, args, &err)) {
            g.Error(this, ctx, std::string(verb) + ": " + err + "; '" + name + "' was not sent");
            return false;
        }
        return true;
    }
    bool DepthOk(NodeGraph& g, VsContext& ctx, const char* verb) {
        if (VsEventDepth() < NodeGraph::kMaxEventDepth) return true;
        g.Error(this, ctx, std::string(verb) + ": '" + name + "' was sent more than " + std::to_string(NodeGraph::kMaxEventDepth) +
                           " deep in a row (a handler that raises the event that ran it?); this one was skipped");
        return false;
    }
};

// Raise Event: run the target's handlers of declared event <name> with the parameters.
struct RaiseEventNode : EventCallNode {
    using EventCallNode::EventCallNode;
    int Exec(NodeGraph& g, VsContext& ctx) override {
        last = VsValue(0.0f);
        const EventDecl* decl = g.FindEvent(name);
        if (!decl || decl->dispatcher) {
            g.Error(this, ctx, !decl ? "Raise Event: no event named '" + name + "' is declared in this graph (add it in the Events list)"
                                     : "Raise Event: '" + name + "' is a dispatcher: use Call Dispatcher");
            return 0;
        }
        GameObject* target = nullptr;
        std::vector<VsValue> args;
        if (!Gather(g, ctx, *decl, target, args, "Raise Event")) return 0;
        if (!DepthOk(g, ctx, "Raise Event")) return 0;
        VsEventDepthGuard depth;
        const std::string origin = VsEventOrigin("Raise Event", name, this, g, ctx);
        const std::uint64_t tid = target->InstanceId();
        int declaredOn = 0, ran = 0;
        for (VisualScriptComponent* vc : target->GetComponents<VisualScriptComponent>()) {
            if (!GameObject::FromInstanceId(tid)) break;   // a handler destroyed the target
            NodeGraph* tg = vc->Graph();
            const EventDecl* td = tg ? tg->FindEvent(name) : nullptr;
            if (!td || td->dispatcher) continue;
            ++declaredOn;
            std::vector<VsValue> a = args;
            std::string err;
            if (!tg->FitEventArgs(*td, a, &err)) { g.Error(this, ctx, "Raise Event: '" + target->name + "' can't take it: " + err); continue; }
            ran += vc->RunTypedEvent(name, a, ctx.gameObject, origin);
        }
        if (!declaredOn) g.Error(this, ctx, "Raise Event: '" + target->name + "' has no Visual Script that declares event '" + name + "'; nothing ran");
        last = VsValue((float)ran);
        return g.Aborted() ? -1 : 0;
    }
};

// Call Dispatcher: run every handler bound to the target's dispatcher <name>.
struct CallDispatcherNode : EventCallNode {
    using EventCallNode::EventCallNode;
    int Exec(NodeGraph& g, VsContext& ctx) override {
        last = VsValue(0.0f);
        const EventDecl* decl = g.FindEvent(name);
        if (!decl || !decl->dispatcher) {
            g.Error(this, ctx, !decl ? "Call Dispatcher: no dispatcher named '" + name + "' is declared in this graph (add it in the Events list)"
                                     : "Call Dispatcher: '" + name + "' is an event: use Raise Event");
            return 0;
        }
        GameObject* owner = nullptr;
        std::vector<VsValue> args;
        if (!Gather(g, ctx, *decl, owner, args, "Call Dispatcher")) return 0;
        bool declared = false;
        for (VisualScriptComponent* vc : owner->GetComponents<VisualScriptComponent>()) {
            const EventDecl* od = vc->Graph() ? vc->Graph()->FindEvent(name) : nullptr;
            if (od && od->dispatcher) declared = true;
        }
        if (!declared) {
            g.Error(this, ctx, "Call Dispatcher: '" + owner->name + "' has no Visual Script that declares dispatcher '" + name + "'; nothing ran");
            return 0;
        }
        if (!DepthOk(g, ctx, "Call Dispatcher")) return 0;
        VsEventDepthGuard depth;
        const std::string origin = VsEventOrigin("Call Dispatcher", name, this, g, ctx);
        const std::uint64_t oid = owner->InstanceId();
        int ran = 0;
        for (const VsEventBindings::Binding& b : VsEventBindings::Get().Snapshot(oid, name)) {
            GameObject* l = GameObject::FromInstanceId(b.listener);
            if (!l) { VsEventBindings::Get().ForgetObject(b.listener); continue; }   // destroyed: its bindings go
            if (g.Aborted()) break;
            for (VisualScriptComponent* vc : l->GetComponents<VisualScriptComponent>()) {
                NodeGraph* lg = vc->Graph();
                if (!lg) continue;
                std::vector<VsValue> a = args;
                std::string err;
                if (b.isFunction) {
                    if (lg->FunctionNode(b.handler) < 0) continue;
                    if (!VsFitFunctionArgs(*lg, b.handler, a, &err)) { g.Error(this, ctx, "Call Dispatcher: '" + l->name + "': " + err); continue; }
                    if (vc->RunBoundFunction(b.handler, a, ctx.gameObject, origin, &err)) ++ran;
                    else g.Error(this, ctx, "Call Dispatcher: '" + l->name + "': " + err);
                } else {
                    const EventDecl* he = lg->FindEvent(b.handler);
                    if (!he || he->dispatcher) continue;
                    if (!lg->FitEventArgs(*he, a, &err)) { g.Error(this, ctx, "Call Dispatcher: '" + l->name + "': " + err); continue; }
                    vc->RunTypedEvent(b.handler, a, ctx.gameObject, origin);
                    ++ran;
                }
            }
        }
        last = VsValue((float)ran);
        return g.Aborted() ? -1 : 0;
    }
};

// Bind / Unbind: this object's handler (an event or a function of this graph) on the dispatcher
// of in0 (empty = this object). Text: `Dispatcher -> Handler`.
struct BindEventNode : VsNode {
    std::string dispatcher, handler;
    bool unbind;
    BindEventNode(const std::string& text, bool un) : unbind(un) { ParseBindText(text, dispatcher, handler); }
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        const char* verb = unbind ? "Unbind" : "Bind";
        const EventDecl* decl = g.FindEvent(dispatcher);
        if (!decl || !decl->dispatcher) {
            g.Error(this, ctx, std::string(verb) + ": " + (dispatcher.empty() ? std::string("no dispatcher is picked")
                                                          : "no dispatcher named '" + dispatcher + "' is declared in this graph"));
            return 0;
        }
        if (handler.empty()) { g.Error(this, ctx, std::string(verb) + ": no handler is picked"); return 0; }
        if (!ctx.gameObject) return 0;
        GameObject* owner = ctx.gameObject;
        if (HasInput(0)) {
            const VsValue tv = In(0, g, ctx);
            owner = VsResolveObject(ctx, tv);
            if (!owner) {
                if (tv.IsObject()) return 0;   // the owner is gone, so there is nothing bound to it
                g.Error(this, ctx, std::string(verb) + ": no object named '" + tv.AsString() + "' was found for its Target");
                return 0;
            }
        }
        const std::uint64_t listener = ctx.gameObject->InstanceId();
        if (unbind) {
            VsEventBindings::Get().Unbind(owner->InstanceId(), dispatcher, listener, handler);
            return 0;
        }
        // The handler must be an event or a function of this graph with matching parameters.
        bool isFunction = false;
        if (const EventDecl* he = g.FindEvent(handler); he && !he->dispatcher) {
            std::string why;
            if (!EventSigCompatible(*decl, *he, &why)) {
                g.Error(this, ctx, "Bind: handler event '" + handler + "' doesn't fit dispatcher '" + dispatcher + "': " + why);
                return 0;
            }
        } else if (auto* fs = dynamic_cast<FunctionStartNode*>(g.Node(g.FunctionNode(handler)))) {
            isFunction = true;
            if (!fs->sigOk || fs->sig.params.size() != decl->params.size()) {
                g.Error(this, ctx, "Bind: function '" + handler + "' takes " + std::to_string(fs->sig.params.size()) +
                                   " parameter(s) but dispatcher '" + dispatcher + "' sends " + std::to_string(decl->params.size()));
                return 0;
            }
            for (std::size_t i = 0; i < decl->params.size(); ++i) {
                const PinType want = fs->sig.params[i].type;
                if (want != PinType::Any && !PinCompatible(decl->params[i].type.base, want)) {
                    g.Error(this, ctx, "Bind: parameter '" + fs->sig.params[i].name + "' of function '" + handler + "' is a " + PinTypeName(want) +
                                       " but dispatcher '" + dispatcher + "' sends a " + VarTypeText(decl->params[i].type));
                    return 0;
                }
            }
        } else {
            g.Error(this, ctx, "Bind: '" + handler + "' is neither an event nor a function declared in this graph");
            return 0;
        }
        // The owner must declare that dispatcher, with parameters this graph's copy fits.
        bool declared = false;
        std::string mismatch;
        for (VisualScriptComponent* vc : owner->GetComponents<VisualScriptComponent>()) {
            const EventDecl* od = vc->Graph() ? vc->Graph()->FindEvent(dispatcher) : nullptr;
            if (!od || !od->dispatcher) continue;
            std::string why;
            if (EventSigCompatible(*decl, *od, &why)) declared = true;
            else mismatch = why;
        }
        if (!declared) {
            g.Error(this, ctx, "Bind: '" + owner->name + "' " + (mismatch.empty() ? "has no Visual Script that declares dispatcher '" + dispatcher + "'"
                                                                                  : "declares dispatcher '" + dispatcher + "' differently (" + mismatch + ")"));
            return 0;
        }
        VsEventBindings::Binding b;
        b.owner = owner->InstanceId(); b.listener = listener; b.dispatcher = dispatcher; b.isFunction = isFunction; b.handler = handler;
        if (VsEventBindings::Get().Bind(b) == VsEventBindings::BindResult::TooMany)
            g.Error(this, ctx, "Bind: too many bindings (at most " + std::to_string(VsEventBindings::kMaxPerDispatcher) + " per dispatcher and " +
                               std::to_string(VsEventBindings::kMaxPerListener) + " per object); this one was not added");
        return 0;
    }
};

inline std::unique_ptr<VsNode> MakeEventNode(const std::string& type, const std::string& textIn) {
    const std::string text = VsEventNodeName(textIn);
    if (type == "OnCustomEvent")  return std::make_unique<OnCustomEventNode>(text);
    if (type == "RaiseEvent")     return std::make_unique<RaiseEventNode>(text);
    if (type == "CallDispatcher") return std::make_unique<CallDispatcherNode>(text);
    if (type == "BindEvent")      return std::make_unique<BindEventNode>(text, false);
    return std::make_unique<BindEventNode>(text, true);
}

} // namespace okay::vs
