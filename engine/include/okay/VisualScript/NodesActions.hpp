#pragma once
// ---------------------------------------------------------------------------
// The Actions step library inside the node graph. One graph editor covers both
// styles: wire nodes, or drop no-code steps ("Move", "Play Sound", "Destroy",
// "Set Variable"...) picked from menus. Steps run through a private ActionList
// that never joins the scene, so they behave exactly like the Actions component.
//
//   Action        one step                       exec in -> out
//   Actions       a list of steps (with Wait,    exec in -> out once the list
//                 If/Repeat/While blocks...)     has finished (may take frames)
//   Condition     one Actions test               exec in -> True / False
//   Delay         wait N seconds, then go on     exec in -> out (later)
// ---------------------------------------------------------------------------
#include "okay/VisualScript/NodeGraph.hpp"
#include "okay/Components/ActionList.hpp"
#include <string>
#include <utility>
#include <vector>

namespace okay::vs {

struct ActionStepsNode : VsNode {
    ActionList runner;
    explicit ActionStepsNode(std::vector<ActionList::Item> steps) { runner.instructions = std::move(steps); }
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        runner.gameObject = ctx.gameObject;
        runner.transform  = ctx.transform;
        if (g.Resuming()) {
            if (!runner.IsRunning()) return 0;
            runner.Update(ctx.deltaTime);
        } else {
            if (runner.IsRunning()) return -1;   // still busy from an earlier run
            if (!runner.BeginRun()) return 0;
            runner.Update(0.0f);
        }
        return runner.IsRunning() ? NodeGraph::kSuspend : 0;
    }
};

struct ActionConditionNode : VsNode {
    ActionList tester;
    std::vector<ActionList::Item> conds;   // kept in place: some tests key state on the item's address
    explicit ActionConditionNode(std::vector<ActionList::Item> c) : conds(std::move(c)) {}
    int ExecOutCount() const override { return 2; }
    int Exec(NodeGraph&, VsContext& ctx) override {
        tester.gameObject = ctx.gameObject;
        tester.transform  = ctx.transform;
        return tester.TestConditions(conds) ? 0 : 1;
    }
    VsValue Eval(int, NodeGraph&, VsContext& ctx) override {
        tester.gameObject = ctx.gameObject;
        tester.transform  = ctx.transform;
        return tester.TestConditions(conds);
    }
};

// A real pause: parks the chain and continues it `seconds` later, once.
// (The older Wait node only lets a repeatedly-run chain through after a while.)
struct DelayNode : VsNode {
    float seconds;
    float left = 0.0f;
    bool  waiting = false;
    explicit DelayNode(float s) : seconds(s) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (!g.Resuming()) {
            if (waiting) return -1;   // already counting down
            if (seconds <= 0.0f) return 0;
            waiting = true; left = seconds;
            return NodeGraph::kSuspend;
        }
        left -= ctx.deltaTime;
        if (left > 0.0f) return NodeGraph::kSuspend;
        waiting = false;
        return 0;
    }
};

} // namespace okay::vs
