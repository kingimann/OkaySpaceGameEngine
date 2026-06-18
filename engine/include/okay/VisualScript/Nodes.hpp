#pragma once
#include "okay/VisualScript/NodeGraph.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Input/Input.hpp"
#include <memory>
#include <string>
#include <vector>

namespace okay::vs {

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

/// Construct a node from its text-format type name and arguments.
/// Returns nullptr (and sets `err`) for an unknown type.
std::unique_ptr<VsNode> MakeNode(const std::string& type,
                                 const std::vector<std::string>& args,
                                 std::string* err);

} // namespace okay::vs
