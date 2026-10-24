#pragma once
// ---------------------------------------------------------------------------
// Visual-script functions: a Function node starts a reusable chain (its text
// field is the signature, e.g. "Damage(amount: Number) -> left: Number"),
// Return hands values back, Call Function runs it from anywhere in the same
// graph, and Set / Get Local keep variables that live for one call only.
// ---------------------------------------------------------------------------
#include "okay/VisualScript/NodeGraph.hpp"
#include "okay/VisualScript/VsTypes.hpp"
#include <string>
#include <vector>

namespace okay::vs {

// Function: exec out 0 = the body; data outs = the parameters of the running call.
struct FunctionStartNode : VsNode {
    FunctionSig sig;
    bool sigOk = false;
    explicit FunctionStartNode(const std::string& text) { sigOk = ParseFunctionSignature(text, sig); }
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph&, VsContext&) override { return 0; }
    VsValue Eval(int pin, NodeGraph& g, VsContext& ctx) override {
        if (const VsValue* v = g.InFunction() && g.CurrentFunction() == sig.name ? g.Arg(pin) : nullptr) return *v;
        g.Error(this, ctx, "parameter '" + (pin >= 0 && pin < (int)sig.params.size() ? sig.params[(std::size_t)pin].name : std::string("?")) +
                           "' of function '" + sig.name + "' was read outside a call to it (it reads 0)");
        return {};
    }
};

// Return: ends the function; data ins = the return values, in signature order.
struct ReturnNode : VsNode {
    std::string fn;
    explicit ReturnNode(std::string f) : fn(std::move(f)) {}
    int ExecOutCount() const override { return 0; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (!g.InFunction()) { g.Error(this, ctx, "Return ran outside a function; nothing was returned"); return -1; }
        const int start = g.FunctionNode(g.CurrentFunction());
        auto* fs = dynamic_cast<FunctionStartNode*>(g.Node(start));
        const std::size_t n = fs ? fs->sig.returns.size() : 0;
        std::vector<VsValue> vals;
        for (std::size_t i = 0; i < n; ++i) vals.push_back(In((int)i, g, ctx));
        g.SetReturn(std::move(vals));
        return -1;
    }
};

// Call Function: data ins = the arguments, data outs = the returned values (the
// last call's; read them after the call has run).
struct CallFunctionNode : VsNode {
    std::string fn;
    std::vector<VsValue> last;
    explicit CallFunctionNode(std::string f) : fn(std::move(f)) {}
    int ExecOutCount() const override { return 1; }
    VsValue Eval(int pin, NodeGraph&, VsContext&) override {
        return pin >= 0 && pin < (int)last.size() ? last[(std::size_t)pin] : VsValue{};
    }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        auto* fs = dynamic_cast<FunctionStartNode*>(g.Node(g.FunctionNode(fn)));
        std::vector<VsValue> args;
        if (fs) for (std::size_t i = 0; i < fs->sig.params.size(); ++i) args.push_back(In((int)i, g, ctx));
        std::vector<VsValue> rets;
        if (!g.CallFunction(fn, std::move(args), rets, this, ctx)) return g.Aborted() ? -1 : 0;
        if (fs) rets.resize(fs->sig.returns.size());   // no Return reached: the values read empty
        last = std::move(rets);
        return 0;
    }
};

// Set Local / Get Local: variables of the running call (gone when it returns).
struct SetLocalNode : VsNode {
    std::string name;
    explicit SetLocalNode(std::string n) : name(std::move(n)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (auto* l = g.Locals()) (*l)[name] = In(0, g, ctx);
        else g.Error(this, ctx, "Set Local '" + name + "' ran outside a function (use Set Var for graph variables)");
        return 0;
    }
};
struct GetLocalNode : VsNode {
    std::string name;
    explicit GetLocalNode(std::string n) : name(std::move(n)) {}
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        if (auto* l = g.Locals()) { auto it = l->find(name); return it != l->end() ? it->second : VsValue{}; }
        g.Error(this, ctx, "Get Local '" + name + "' read outside a function (it reads 0)");
        return {};
    }
};

} // namespace okay::vs
