// The OkayScript (text) debugger: line breakpoints, stepping, call stack, locals,
// watch expressions, pause-on-error, a failing handler, and the cost while detached.
// Headless: the VM runs without a host; the "editor" is the onStop callback.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Scripting/OkayScriptVM.hpp"
#include "okay/Scripting/ScriptDebug.hpp"
#include <chrono>
#include <cstdio>

using namespace okay;
using Step = vs::VsDebugger::Step;

// line 1 is the first line of the raw string.
static const char* kScript = R"(int total = 0;
int Double(int x) {
    int y = x * 2;
    return y;
}
void Run() {
    int a = 3;
    int b = Double(a);
    total = total + b;
    total = total + 1;
}
void Boom() {
    int z = 5;
    throw "kaboom";
}
)";

struct Stops { std::vector<int> lines; std::vector<std::string> frames; };

static std::unique_ptr<OkayScriptVM> Load(const std::string& key, const char* src = kScript) {
    auto vm = std::make_unique<OkayScriptVM>();
    vm->SetDebugKey(key);
    std::string err;
    CHECK(vm->Load(src, &err));
    return vm;
}
static void Reset() {
    auto& d = ScriptDebugger::Get();
    d.SetOnStop(nullptr); d.ClearBreakpoints(); d.SetPauseOnError(false); d.SetEnabled(true);
    d.RequestStep(Step::None);
}
static float Num(const vs::VsValue& v) { return v.AsFloat(); }

int main() {
    auto& d = ScriptDebugger::Get();

    RUN_SUITE("detached: nothing stops, flag is off");
    {
        Reset();
        CHECK(!ScriptDebugger::s_attached);
        auto vm = Load("t.os");
        vm->CallEvent("Run");
        CHECK_NEAR(Num(vm->GetGlobal("total")), 7.0, 1e-6);
        d.SetBreakpoint("t.os", 9, true);          // a breakpoint without a handler does not attach
        CHECK(!ScriptDebugger::s_attached);
        d.ClearBreakpoints();
    }

    RUN_SUITE("breakpoint, locals, call stack, watch, step, step into, remove, run to end");
    {
        Reset();
        auto vm = Load("t.os");
        std::vector<int> lines; std::vector<ScriptStopReason> why;
        std::vector<std::vector<ScriptDebugFrame>> stacks;
        ScriptDebugVars lastLocals; vs::VsValue watch; std::string werr; bool wok = false, refused = true;
        std::string refusedMsg;
        Step next = Step::None;
        d.SetOnStop([&](const ScriptStop& s) {
            lines.push_back(s.line); why.push_back(s.reason);
            stacks.push_back(s.ctx->Frames());
            lastLocals = s.ctx->Locals(0);
            if (s.line == 8) {                       // watch + refusal tests at the first stop
                wok = s.ctx->Evaluate("a * 10 + total", watch, &werr);
                refused = !s.ctx->Evaluate("total = 99", watch, &refusedMsg);
                vs::VsValue tmp; std::string e2;
                CHECK(!s.ctx->Evaluate("Run()", tmp, &e2));
                CHECK(!s.ctx->Evaluate("total++", tmp, &e2));
                CHECK(!s.ctx->Evaluate("1 +", tmp, &e2));      // a syntax error is an error, not a crash
                CHECK(!e2.empty());
                CHECK(s.ctx->Evaluate("max(a, 2)", tmp, &e2));
                CHECK_NEAR(Num(tmp), 3.0, 1e-6);
            }
            if (next != Step::None) d.RequestStep(next);
        });
        d.SetBreakpoint("t.os", 8, true);
        CHECK(ScriptDebugger::s_attached);
        vm->CallEvent("Run");
        CHECK(lines.size() == 1 && lines[0] == 8);
        CHECK(why[0] == ScriptStopReason::Breakpoint);
        CHECK(stacks[0].size() == 2 && stacks[0][0].name == "Run()" && stacks[0][0].line == 8);
        CHECK(stacks[0][1].name == "(script)");
        bool hasA = false;
        for (auto& kv : lastLocals) if (kv.first == "a") { hasA = true; CHECK_NEAR(Num(kv.second), 3.0, 1e-6); }
        CHECK(hasA);
        CHECK(wok); CHECK_NEAR(Num(watch), 30.0, 1e-6);
        CHECK(refused); CHECK(!refusedMsg.empty());
        CHECK_NEAR(Num(vm->GetGlobal("total")), 7.0, 1e-6);   // watch/refusals did not change anything

        // Step into: from line 8 the next stop is inside Double (line 3), depth 3.
        lines.clear(); stacks.clear(); why.clear(); next = Step::Into;
        d.SetBreakpoint("t.os", 8, true);
        vm->CallEvent("Run");
        CHECK(lines.size() >= 2 && lines[0] == 8 && lines[1] == 3);
        CHECK(stacks[1].size() == 3 && stacks[1][0].name == "Double()" && stacks[1][0].line == 3);
        CHECK(stacks[1][1].name == "Run()" && stacks[1][1].line == 8);   // where it was called from
        CHECK(why[1] == ScriptStopReason::Step);
        bool hasX = false;
        for (auto& kv : lastLocals) (void)kv;
        (void)hasX;

        d.SetBreakpoint("t.os", 8, false); d.RequestStep(Step::None);   // a step still pending would stop at the very next line
        // Step over: from 8 the next stop is 9 (Double's lines are skipped), then 10.
        lines.clear(); next = Step::Over;
        d.SetBreakpoint("t.os", 8, true);
        vm->CallEvent("Run");
        CHECK(lines.size() >= 3);
        CHECK(lines[0] == 8 && lines[1] == 9 && lines[2] == 10);
        CHECK(std::find(lines.begin(), lines.end(), 3) == lines.end());

        // Step out: from inside Double (break at line 3) the next stop is back in Run (line 9).
        lines.clear(); next = Step::None;
        d.RequestStep(Step::None);
        d.ClearBreakpoints(); d.SetBreakpoint("t.os", 3, true);
        d.SetOnStop([&](const ScriptStop& s) {
            lines.push_back(s.line);
            if (lines.size() == 1) d.RequestStep(Step::Out);
        });
        vm->CallEvent("Run");
        CHECK(lines.size() >= 2 && lines[0] == 3 && lines[1] == 9);

        // Remove the breakpoint: runs straight through, handler never called, flag off again.
        lines.clear(); d.SetBreakpoint("t.os", 3, false);
        CHECK(!ScriptDebugger::s_attached);
        vm->CallEvent("Run");
        CHECK(lines.empty());
        CHECK(Num(vm->GetGlobal("total")) > 7.0);   // it ran to the end (total keeps growing each call)
        Reset();
    }

    RUN_SUITE("a breakpoint in another script does not stop this one");
    {
        Reset();
        auto vm = Load("a.os");
        int hits = 0;
        d.SetOnStop([&](const ScriptStop&) { ++hits; });
        d.SetBreakpoint("other.os", 8, true);
        vm->CallEvent("Run");
        CHECK(hits == 0);
        Reset();
    }

    RUN_SUITE("pause on error, once, before the stack unwinds");
    {
        Reset();
        auto vm = Load("t.os");
        int hits = 0; std::string msg; std::vector<ScriptDebugFrame> st; int line = 0; ScriptStopReason r{};
        d.SetOnStop([&](const ScriptStop& s) { ++hits; msg = s.message; st = s.ctx->Frames(); line = s.line; r = s.reason; });
        d.SetPauseOnError(true);
        CHECK(ScriptDebugger::s_attached);
        vm->CallEvent("Boom");
        CHECK(hits == 1);
        CHECK(r == ScriptStopReason::Error);
        CHECK(msg.find("kaboom") != std::string::npos);
        CHECK(line == 14);
        CHECK(!st.empty() && st[0].name == "Boom()");
        Reset();
    }

    RUN_SUITE("a failing / throwing stop handler does not crash and the script finishes");
    {
        Reset();
        auto vm = Load("t.os");
        int hits = 0;
        d.SetOnStop([&](const ScriptStop&) { ++hits; throw std::runtime_error("handler bug"); });
        d.SetBreakpoint("t.os", 9, true);
        vm->CallEvent("Run");
        CHECK(hits == 1);
        CHECK(!d.InStop());
        Reset();
    }

    RUN_SUITE("disabled: nothing stops (finishing a frame when Play stops)");
    {
        Reset();
        auto vm = Load("t.os");
        int hits = 0;
        d.SetOnStop([&](const ScriptStop&) { ++hits; });
        d.SetBreakpoint("t.os", 9, true);
        d.SetEnabled(false);
        vm->CallEvent("Run");
        CHECK(hits == 0);
        Reset();
    }

    RUN_SUITE("cost while detached: hot loop");
    {
        Reset();
        const char* loop = "int n = 0;\nvoid Hot() {\n  int i = 0;\n  while (i < 200000) { i = i + 1; n = n + 2; }\n}\n";
        auto vm = Load("hot.os", loop);
        auto time = [&] {
            double best = 1e9;
            for (int r = 0; r < 5; ++r) {
                auto t0 = std::chrono::steady_clock::now();
                vm->CallEvent("Hot");
                double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                if (ms < best) best = ms;
            }
            return best;
        };
        const double detached = time();
        // Handler set but no breakpoint, no step: still detached (flag false).
        d.SetOnStop([](const ScriptStop&) {});
        CHECK(!ScriptDebugger::s_attached);
        const double idleHandler = time();
        // A breakpoint on ANOTHER script keeps the flag on: the cost of the per-statement check.
        d.SetBreakpoint("elsewhere.os", 1, true);
        const double attachedMiss = time();
        std::printf("  hot loop (400k statements): detached %.2f ms, handler-only %.2f ms, attached-miss %.2f ms\n",
                    detached, idleHandler, attachedMiss);
        CHECK(idleHandler < detached * 1.5 + 2.0);   // the same code path: only noise
        CHECK(attachedMiss < detached * 8.0 + 20.0); // generous: the attached path is not free, only the detached one is
        Reset();
    }

    RUN_SUITE("the shipped example scene: breakpoint inside a function a scene script calls");
    {
        Reset();
        const std::string dir = std::string(OKAY_SOURCE_DIR) + "/examples/script_debugger/";
        Scene scene("S");
        std::string err; std::vector<std::string> warns;
        CHECK(SceneSerializer::LoadFromFile(scene, dir + "script_debugger.okayscene", &err, &warns));
        GameObject* go = scene.Find("Scorekeeper");
        CHECK(go != nullptr);
        ScriptComponent* sc = go ? go->GetComponent<ScriptComponent>() : nullptr;
        CHECK(sc != nullptr);
        if (sc) {
            // The path written beside the scene resolves to the real file, so the key matches the editor's.
            const std::string key = ScriptDebugger::KeyForPath(sc->Path());
            CHECK(!sc->Path().empty());
            CHECK(key.size() > 12 && key.find("script_debugger/debug_me.os") != std::string::npos);
            int hits = 0; std::string fn; ScriptDebugVars loc;
            d.SetOnStop([&](const ScriptStop& st) {
                ++hits; fn = st.ctx->Frames()[0].name; loc = st.ctx->Locals(0);
            });
            d.SetBreakpoint(key, 7, true);          // `return result;` inside Square
            scene.Start();
            for (int i = 0; i < 130; ++i) scene.Update(1.0f / 60.0f);
            CHECK(hits == 2);                         // AddScore runs at frame 60 and 120
            CHECK(fn == "Square()");
            bool sawResult = false;
            for (auto& kv : loc) if (kv.first == "result") sawResult = true;
            CHECK(sawResult);
        }
        Reset();
    }

    TEST_MAIN_RESULT();
}
