// The visual-script debugger's engine half: breakpoints by graph file + node,
// stepping into / over / out of functions, what a stop can see (event, call
// stack, arguments, locals), and graphs reloaded or destroyed while stopped.
// The handler here records each stop and decides the next step; the editor's
// handler freezes the game instead, then does the same.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/VisualScript/VsDebug.hpp"
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace okay;
using vs::VsDebugger;

namespace {

struct Stop {
    int node;
    std::string event;
    int depth;
    std::vector<std::string> stack;
    float x;    // object variable x at the stop
};
std::vector<Stop> g_stops;
std::vector<VsDebugger::Step> g_plan;   // step to ask for after each stop (None = continue)

void Handler(vs::NodeGraph& g, vs::VsNode& n, vs::VsContext& ctx) {
    Stop s{n.fileId, g.CurrentEvent(), g.CallDepth(), {}, 0.0f};
    for (const auto& f : g.DebugFrames()) s.stack.push_back(f.name);
    if (auto it = ctx.variables.find("x"); it != ctx.variables.end()) s.x = it->second.AsFloat();
    g_stops.push_back(s);
    const std::size_t i = g_stops.size() - 1;
    if (i < g_plan.size() && g_plan[i] != VsDebugger::Step::None) VsDebugger::Get().RequestStep(g_plan[i]);
}

VisualScriptComponent* Attach(GameObject* go, const std::string& path, const std::string& text) {
    std::ofstream(path) << text;
    auto* v = go->AddComponent<VisualScriptComponent>();
    std::string err;
    CHECK(v->LoadFromFile(path, &err));
    return v;
}

void Reset() {
    g_stops.clear();
    g_plan.clear();
    VsDebugger::Get().ClearBreakpoints();
    VsDebugger::Get().RequestStep(VsDebugger::Step::None);
    VsDebugger::Get().enabled = true;
    VsDebugger::Get().onStop = Handler;
}

const char* kCounter = R"VS(
node 1 OnStart
node 2 SetVar x
default 2 0 5
node 3 ForLoop
default 3 0 1
default 3 1 3
node 4 AddVar x
default 4 0 1
node 5 Print
default 5 0 "done"
exec 1 0 2
exec 2 0 3
exec 3 0 4
exec 3 1 5
entry OnStart 1
)VS";

// Twice(v) = v * 2 via a function; OnStart: x = Twice(4); Print.
const char* kFn = R"VS(
node 10 FunctionStart "Twice(v: Number) -> r: Number"
node 11 SetLocal tmp
node 12 Mul
node 13 Const 2
node 14 Return Twice
data 12 0 10 0
data 12 1 13 0
data 11 0 12 0
data 14 0 12 0
exec 10 0 11
exec 11 0 14
node 1 OnStart
node 2 CallFunction Twice
default 2 0 4
node 3 SetVar x
node 4 Print
data 3 0 2 0
default 4 0 "after"
exec 1 0 2
exec 2 0 3
exec 3 0 4
entry OnStart 1
)VS";

} // namespace

int main() {
    RUN_SUITE("vs_debug");
    Log::sink = [](Log::Level, const std::string&) {};

    // ---- A breakpoint stops before its node runs, every time it's reached --------
    {
        Reset();
        VsDebugger::Get().SetBreakpoint("dbg_counter.okayvs", 2, true);   // Set Var x = 5
        VsDebugger::Get().SetBreakpoint("dbg_counter.okayvs", 4, true);   // inside the loop
        Scene s("D1"); GameObject* go = s.CreateGameObject("Counter");
        auto* v = Attach(go, "dbg_counter.okayvs", kCounter);
        s.Start(); s.Update(1.0f / 60.0f);
        CHECK(g_stops.size() == 4);                       // Set Var once, loop body 3 times
        if (g_stops.size() == 4) {
            CHECK(g_stops[0].node == 2 && g_stops[0].x == 0.0f);   // x not set yet: stopped before
            CHECK(g_stops[1].node == 4 && g_stops[1].x == 5.0f);
            CHECK(g_stops[3].node == 4 && g_stops[3].x == 7.0f);
            CHECK(g_stops[0].event == "OnStart" && g_stops[0].depth == 0);
        }
        CHECK(v->GetVariable("x").AsFloat() == 8.0f);       // the graph still ran normally
        // Another file's graph with the same node ids doesn't stop.
        g_stops.clear();
        Scene s2("D1b"); GameObject* other = s2.CreateGameObject("Other");
        Attach(other, "dbg_other.okayvs", kCounter);
        s2.Start(); s2.Update(1.0f / 60.0f);
        CHECK(g_stops.empty());
        // Removing a breakpoint, or switching the debugger off, stops nothing.
        VsDebugger::Get().SetBreakpoint("dbg_counter.okayvs", 4, false);
        VsDebugger::Get().enabled = false;
        g_stops.clear();
        Scene s3("D1c"); Attach(s3.CreateGameObject("C"), "dbg_counter.okayvs", kCounter);
        s3.Start(); s3.Update(1.0f / 60.0f);
        CHECK(g_stops.empty());
    }

    // ---- Step Into goes into a function, Over steps past it, Out leaves it -------
    {
        Reset();
        VsDebugger::Get().SetBreakpoint("dbg_fn.okayvs", 2, true);   // the Call Function
        g_plan = {VsDebugger::Step::Into, VsDebugger::Step::Into, VsDebugger::Step::Out, VsDebugger::Step::None};
        Scene s("D2"); GameObject* go = s.CreateGameObject("Fn");
        auto* v = Attach(go, "dbg_fn.okayvs", kFn);
        s.Start(); s.Update(1.0f / 60.0f);
        // Call (depth 0) -> into Function start (depth 1) -> Set Local (depth 1) -> out: Set Var x (depth 0)
        CHECK(g_stops.size() == 4);
        if (g_stops.size() == 4) {
            CHECK(g_stops[0].node == 2 && g_stops[0].depth == 0);
            CHECK(g_stops[1].node == 10 && g_stops[1].depth == 1);
            CHECK(g_stops[1].stack.size() == 1 && g_stops[1].stack[0] == "Twice");
            CHECK(g_stops[2].node == 11 && g_stops[2].depth == 1);
            CHECK(g_stops[3].node == 3 && g_stops[3].depth == 0);
        }
        CHECK(v->GetVariable("x").AsFloat() == 8.0f);

        Reset();
        VsDebugger::Get().SetBreakpoint("dbg_fn.okayvs", 2, true);
        g_plan = {VsDebugger::Step::Over, VsDebugger::Step::Over, VsDebugger::Step::None};
        Scene s2("D2b"); Attach(s2.CreateGameObject("Fn"), "dbg_fn.okayvs", kFn);
        s2.Start(); s2.Update(1.0f / 60.0f);
        // Over the call: next stop is Set Var x (the function's nodes are skipped), then Print.
        CHECK(g_stops.size() == 3);
        if (g_stops.size() == 3) {
            CHECK(g_stops[1].node == 3 && g_stops[1].depth == 0);
            CHECK(g_stops[2].node == 4);
        }
    }

    // ---- What a stop inside a function can read: arguments and locals ------------
    {
        Reset();
        VsDebugger::Get().SetBreakpoint("dbg_fn.okayvs", 14, true);   // the Return
        std::vector<vs::VsValue> args; float local = -1.0f;
        VsDebugger::Get().onStop = [&](vs::NodeGraph& g, vs::VsNode&, vs::VsContext&) {
            auto frames = g.DebugFrames();
            if (!frames.empty()) {
                args = frames.back().args;
                if (auto it = frames.back().locals.find("tmp"); it != frames.back().locals.end()) local = it->second.AsFloat();
            }
        };
        Scene s("D3"); Attach(s.CreateGameObject("Fn"), "dbg_fn.okayvs", kFn);
        s.Start(); s.Update(1.0f / 60.0f);
        CHECK(args.size() == 1 && args[0].AsFloat() == 4.0f);
        CHECK(local == 8.0f);
    }

    // ---- Saving the graph while stopped in it: the reload waits until it finishes --
    {
        Reset();
        VsDebugger::Get().SetBreakpoint("dbg_counter.okayvs", 4, true);
        Scene s("D4"); GameObject* go = s.CreateGameObject("Counter");
        auto* v = Attach(go, "dbg_counter.okayvs", kCounter);
        int stops = 0;
        vs::NodeGraph* during = nullptr;
        VsDebugger::Get().onStop = [&](vs::NodeGraph& g, vs::VsNode&, vs::VsContext&) {
            if (stops++ == 0) {
                during = &g;
                // What the editor's Save does: reload every object using the file.
                CHECK(v->LoadFromText("node 1 OnUpdate\nnode 2 AddVar ticks\ndefault 2 0 1\nexec 1 0 2\nentry OnUpdate 1\n"));
                CHECK(v->Graph() == &g);   // still the running graph
            }
        };
        s.Start();
        CHECK(stops == 3 && v->GetVariable("x").AsFloat() == 8.0f);   // the old graph finished its loop
        s.Update(1.0f / 60.0f);                                       // swapped in now: On Update runs
        CHECK(v->Graph() != during);
        CHECK(v->GetVariable("ticks").AsFloat() == 1.0f);
        CHECK(v->Graph()->debugKey == "dbg_counter.okayvs");
    }

    // ---- A step whose graph is destroyed carries on to the next step anywhere -----
    {
        Reset();
        VsDebugger::Get().SetBreakpoint("dbg_counter.okayvs", 5, true);   // Print, the last node
        g_plan = {VsDebugger::Step::Over, VsDebugger::Step::None};
        Scene s("D5");
        GameObject* a = s.CreateGameObject("A");
        Attach(a, "dbg_counter.okayvs", kCounter);
        s.Start(); s.Update(1.0f / 60.0f);
        CHECK(g_stops.size() == 1);
        s.Destroy(a); s.Update(1.0f / 60.0f);                              // its graph is gone now
        Attach(s.CreateGameObject("B"), "dbg_fn.okayvs", kFn);
        s.Update(1.0f / 60.0f);                                            // B starts: first step node stops
        CHECK(g_stops.size() == 2);
        if (g_stops.size() == 2) CHECK(g_stops[1].node == 1 && g_stops[1].event == "OnStart");   // B's On Start event itself
    }

    VsDebugger::Get().onStop = nullptr;
    VsDebugger::Get().ClearBreakpoints();
    for (const char* f : {"dbg_counter.okayvs", "dbg_other.okayvs", "dbg_fn.okayvs"}) std::remove(f);
    Log::sink = nullptr;
    TEST_MAIN_RESULT();
}
