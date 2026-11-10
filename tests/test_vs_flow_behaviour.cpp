// Flow nodes, each run in a real scene for enough frames. Edge cases: zero,
// negative, NaN, an empty list, and a value of the wrong kind. Flow nodes do
// not take an object target; a missing object is not one of their inputs.
#include "test_framework.hpp"
#include "VisualScriptEditor.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/SpriteRenderer.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include <Okay.hpp>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <cstdlib>
#include <sstream>
#include <string>
#include <filesystem>
#include <fstream>

using namespace okay;
using namespace okay::editor::vsed;

namespace {

struct B {
    int n = 1;
    std::string s;
    int N(const std::string& type, const std::string& arg = {}) {
        const int i = n++;
        s += "node " + std::to_string(i) + " " + type;
        if (!arg.empty()) s += " " + arg;
        s += "\n";
        return i;
    }
    int Num(float v) { std::ostringstream o; o << v; return N("Const", o.str()); }
    int Bool(bool v) { return N("Const", v ? "true" : "false"); }
    void E(int a, int pin, int b) {
        s += "exec " + std::to_string(a) + " " + std::to_string(pin) + " " + std::to_string(b) + "\n";
    }
    void D(int c, int ip, int p, int op = 0) {
        s += "data " + std::to_string(c) + " " + std::to_string(ip) + " " + std::to_string(p) + " " +
             std::to_string(op) + "\n";
    }
    void Entry(const char* name, int id) { s += std::string("entry ") + name + " " + std::to_string(id) + "\n"; }
    // On Start -> node, with an optional amount added to `var` on the way out.
    int StartTo(const std::string& type, const std::string& arg, const char* var, float amount) {
        const int st = N("OnStart");
        const int node = N(type, arg);
        const int add = N("AddVar", var);
        const int one = Num(amount);
        D(add, 0, one);
        E(st, 0, node);
        E(node, 0, add);
        Entry("OnStart", st);
        return node;
    }
};

VisualScriptComponent* Attach(Scene& scene, const std::string& text, std::string* err) {
    auto* go = scene.CreateGameObject("Demo");
    auto* vs = go->AddComponent<VisualScriptComponent>();
    CHECK(vs->LoadFromText(text, err));
    if (err && !err->empty()) std::printf("  parse: %s\n", err->c_str());
    return vs;
}

float Var(VisualScriptComponent* vs, const char* name) { return vs->GetVariable(name).AsFloat(); }

std::string FlowDemoGraph() {
    B b;
    auto on = [&](int node) { const int st = b.N("OnStart"); b.E(st, 0, node); b.Entry("OnStart", st); return st; };

    // For Loop 1..3 adds 1 three times.
    {
        const int loop = b.N("ForLoop");
        const int add = b.N("AddVar", "loop");
        b.D(loop, 0, b.Num(1)); b.D(loop, 1, b.Num(3)); b.D(add, 0, b.Num(1));
        b.E(loop, 0, add); on(loop);
    }
    // For Each of 10 and 20.
    {
        const int set = b.N("SetVar", "items");
        const int addA = b.N("ListAdd"), addB = b.N("ListAdd"), each = b.N("ForEach");
        const int sum = b.N("AddVar", "each");
        b.D(set, 0, b.N("NewList"));
        b.D(addA, 0, b.N("GetVar", "items")); b.D(addA, 1, b.Num(10));
        b.D(addB, 0, b.N("GetVar", "items")); b.D(addB, 1, b.Num(20));
        b.D(each, 0, b.N("GetVar", "items")); b.D(sum, 0, each, 0);
        b.E(set, 0, addA); b.E(addA, 0, addB); b.E(addB, 0, each); b.E(each, 0, sum);
        on(set);
    }
    // While c < 3, add 1.
    {
        const int wh = b.N("While"), cmp = b.N("Compare", "<"), add = b.N("AddVar", "c");
        b.D(cmp, 0, b.N("GetVar", "c")); b.D(cmp, 1, b.Num(3)); b.D(wh, 0, cmp); b.D(add, 0, b.Num(1));
        b.E(wh, 0, add); on(wh);
    }
    // Repeat 4.
    {
        const int rep = b.N("Repeat", "4"), add = b.N("AddVar", "reps");
        b.D(add, 0, b.Num(1)); b.E(rep, 0, add); on(rep);
    }
    // Flip Flop twice: First adds 1, Second adds 10.
    {
        const int rep = b.N("Repeat", "2"), ff = b.N("FlipFlop");
        const int a = b.N("AddVar", "patha"), bvar = b.N("AddVar", "pathb");
        b.D(a, 0, b.Num(1)); b.D(bvar, 0, b.Num(10));
        b.E(rep, 0, ff); b.E(ff, 0, a); b.E(ff, 1, bvar); on(rep);
    }
    // Gate open.
    {
        const int g = b.N("Gate"), add = b.N("AddVar", "gatev");
        b.D(g, 0, b.Bool(true)); b.D(add, 0, b.Num(5)); b.E(g, 0, add); on(g);
    }
    // Do a Number of Times: 2 passes inside 5 repeats.
    {
        const int rep = b.N("Repeat", "5"), dn = b.N("DoN", "2"), add = b.N("AddVar", "don");
        b.D(add, 0, b.Num(1)); b.E(rep, 0, dn); b.E(dn, 0, add); on(rep);
    }
    // Run Once inside 3 repeats.
    {
        const int rep = b.N("Repeat", "3"), once = b.N("Once"), add = b.N("AddVar", "oncev");
        b.D(add, 0, b.Num(1)); b.E(rep, 0, once); b.E(once, 0, add); on(rep);
    }
    // Random Branch with chance 1 always takes True.
    {
        const int ch = b.N("ChanceBranch"), add = b.N("AddVar", "ch");
        b.D(ch, 0, b.Num(1)); b.D(add, 0, b.Num(4)); b.E(ch, 0, add); on(ch);
    }
    // Every Number of Times: 2 passes out of 4 when Times is 2.
    {
        const int rep = b.N("Repeat", "4"), ev = b.N("EveryN"), add = b.N("AddVar", "en");
        b.D(ev, 0, b.Num(2)); b.D(add, 0, b.Num(1)); b.E(rep, 0, ev); b.E(ev, 0, add); on(rep);
    }
    // Switch Number: 1 takes One.
    {
        const int sw = b.N("SwitchInt"), add = b.N("AddVar", "sw");
        b.D(sw, 0, b.Num(1)); b.D(add, 0, b.Num(7)); b.E(sw, 1, add); on(sw);
    }
    // Counter three times; the body stores Count.
    {
        const int rep = b.N("Repeat", "3"), ctr = b.N("Counter"), set = b.N("SetVar", "ctr");
        b.D(set, 0, ctr, 0); b.E(rep, 0, ctr); b.E(ctr, 0, set); on(rep);
    }
    b.StartTo("Cooldown", "1", "cd", 1);
    {
        const int st = b.N("OnStart"), dly = b.N("Delay", "0.25"), add = b.N("AddVar", "delayed");
        b.D(add, 0, b.Num(1)); b.E(st, 0, dly); b.E(dly, 0, add); b.Entry("OnStart", st);
    }
    {
        const int st = b.N("OnStart"), w = b.N("Wait", "0.25"), add = b.N("AddVar", "waited");
        b.D(add, 0, b.Num(1)); b.E(st, 0, w); b.E(w, 0, add); b.Entry("OnStart", st);
    }
    // Break Loop once the count reaches 3.
    {
        const int loop = b.N("ForLoop"), seq = b.N("Sequence", "2"), add = b.N("AddVar", "brk");
        const int br = b.N("Branch"), cmp = b.N("Compare", ">="), stop = b.N("Break");
        b.D(loop, 0, b.Num(1)); b.D(loop, 1, b.Num(8)); b.D(add, 0, b.Num(1));
        b.D(cmp, 0, b.N("GetVar", "brk")); b.D(cmp, 1, b.Num(3)); b.D(br, 0, cmp);
        b.E(loop, 0, seq); b.E(seq, 0, add); b.E(seq, 1, br); b.E(br, 0, stop); on(loop);
    }
    // Continue Loop skips index 2. 1+3+4 = 8.
    {
        const int loop = b.N("ForLoop"), br = b.N("Branch"), cmp = b.N("Compare", "==");
        const int skip = b.N("Continue"), add = b.N("AddVar", "cont");
        b.D(loop, 0, b.Num(1)); b.D(loop, 1, b.Num(4));
        b.D(cmp, 0, loop, 0); b.D(cmp, 1, b.Num(2)); b.D(br, 0, cmp); b.D(add, 0, loop, 0);
        b.E(loop, 0, br); b.E(br, 0, skip); b.E(br, 1, add); on(loop);
    }
    // Define Function Double, call it with 4.
    {
        const int fs = b.N("FunctionStart", "\"Double(n: Number) -> out: Number\"");
        const int mul = b.N("Mul"), ret = b.N("Return", "Double");
        b.D(mul, 0, fs, 0); b.D(mul, 1, b.Num(2)); b.D(ret, 0, mul); b.E(fs, 0, ret);
        const int call = b.N("CallFunction", "Double"), set = b.N("SetVar", "fun");
        b.D(call, 0, b.Num(4)); b.D(set, 0, call, 0); b.E(call, 0, set); on(call);
    }
    // Branch true.
    {
        const int br = b.N("Branch"), add = b.N("AddVar", "branched");
        b.D(br, 0, b.Bool(true)); b.D(add, 0, b.Num(1)); b.E(br, 0, add); on(br);
    }
    // Sequence runs First then Second.
    {
        const int seq = b.N("Sequence", "2");
        const int a = b.N("AddVar", "seqa"), bvar = b.N("AddVar", "seqb");
        b.D(a, 0, b.Num(1)); b.D(bvar, 0, b.Num(10));
        b.E(seq, 0, a); b.E(seq, 1, bvar); on(seq);
    }

    // What you watch while playing: every half second, Ticks and X go up.
    const int upd = b.N("OnUpdate");
    const int timer = b.N("Timer", "0.5");
    const int ticks = b.N("AddVar", "ticks");
    const int move = b.N("Translate");
    const int set = b.N("SetText", "Readout");
    const int fmt = b.N("Format", "\"Ticks {0}   X {1}   Loop {2}\"");
    b.D(ticks, 0, b.Num(1));
    b.D(move, 0, b.Num(1)); b.D(move, 1, b.Num(0)); b.D(move, 2, b.Num(0));
    b.D(fmt, 0, b.N("GetVar", "ticks")); b.D(fmt, 1, b.N("GetX")); b.D(fmt, 2, b.N("GetVar", "loop"));
    b.D(set, 0, fmt);
    b.E(upd, 0, timer); b.E(timer, 0, ticks); b.E(ticks, 0, move); b.E(move, 0, set);
    b.Entry("OnUpdate", upd);

    // Show the start results immediately, before the first tick.
    const int show = b.N("OnStart");
    b.E(show, 0, set);
    b.Entry("OnStart", show);
    return b.s;
}

void AddDemoObjects(Scene& scene, const std::string& graph) {
    auto* cam = scene.CreateGameObject("Main Camera");
    cam->transform->SetPosition({0, 0, 12});
    cam->AddComponent<Camera>();
    auto* help = scene.CreateGameObject("Instructions");
    auto* ht = help->AddComponent<TextRenderer>();
    ht->text = "Flow playground. Press Play.\n"
               "Every half second, Ticks goes up by 1 and the square steps right.\n"
               "Loop stays 3. After about 2 seconds, Ticks should read 4.";
    ht->screenSpace = true; ht->screenPos = {24, 28}; ht->pixelSize = 2.2f;
    ht->size = {760, 150}; ht->wrap = true; ht->color = Color::White;
    auto* read = scene.CreateGameObject("Readout");
    auto* rt = read->AddComponent<TextRenderer>();
    rt->text = "Press Play";
    rt->screenSpace = true; rt->screenPos = {24, 200}; rt->pixelSize = 3.2f;
    rt->size = {760, 80}; rt->color = Color{0.45f, 1.0f, 0.55f, 1.0f};
    auto* runner = scene.CreateGameObject("Runner");
    auto* sp = runner->AddComponent<SpriteRenderer>();
    sp->glyph = 'O'; sp->color = Color{0.3f, 0.95f, 0.45f, 1}; sp->size = {1.2f, 1.2f};
    auto* vs = runner->AddComponent<VisualScriptComponent>();
    std::string err;
    CHECK(vs->LoadFromText(graph, &err));
    CHECK(err.empty());
}

void ExpectDemo(Scene& scene, const char* where) {
    auto* runner = scene.Find("Runner");
    auto* read = scene.Find("Readout");
    CHECK(runner && read);
    if (!runner || !read) return;
    auto* vs = runner->GetComponent<VisualScriptComponent>();
    auto* rt = read->GetComponent<TextRenderer>();
    CHECK(vs && rt);
    if (!vs) return;
    auto near = [&](const char* name, float want) {
        const float got = Var(vs, name);
        if (std::fabs(got - want) > 0.01f)
            std::printf("  %s: %s is %g, wanted %g\n", where, name, got, want);
        CHECK(std::fabs(got - want) < 0.01f);
    };
    near("loop", 3); near("each", 30); near("c", 3); near("reps", 4);
    near("patha", 1); near("pathb", 10); near("gatev", 5); near("don", 2);
    near("oncev", 1); near("ch", 4); near("en", 2); near("sw", 7); near("ctr", 3);
    near("cd", 1); near("delayed", 1); near("waited", 1); near("brk", 3);
    near("cont", 8); near("fun", 8); near("branched", 1); near("seqa", 1); near("seqb", 10);
    near("ticks", 4);
    CHECK_NEAR(runner->transform->Position().x, 4.0f, 0.01f);
    CHECK(rt->text.find("Ticks 4") != std::string::npos);
    CHECK(vs->Graph() && vs->Graph()->ErrorCount() == 0);
    if (vs->Graph() && vs->Graph()->ErrorCount())
        std::printf("  %s errors: %s\n", where, vs->Graph()->LastError().c_str());
}

void Play(Scene& scene) {
    scene.Start();
    for (int i = 0; i < 4; ++i) scene.Update(0.5f);
}

} // namespace

int main() {
    RUN_SUITE("vs_flow_behaviour");
    auto sink = Log::sink;
    Log::sink = [](Log::Level, const std::string&) {};

    // Every Seconds keeps leftover time, so a 1.2s frame does not make the next one late.
    {
        Scene scene; std::string err;
        auto* vs = Attach(scene, "node 1 OnUpdate\nnode 2 Timer 1\nnode 3 AddVar t\nnode 4 Const 1\n"
                                  "data 3 0 4 0\nexec 1 0 2\nexec 2 0 3\nentry OnUpdate 1\n", &err);
        scene.Start();
        scene.Update(1.2f); CHECK_NEAR(Var(vs, "t"), 1.0f, 0.01f);
        scene.Update(0.9f); CHECK_NEAR(Var(vs, "t"), 2.0f, 0.01f);
    }
    // Zero seconds: every time. Negative is the same, not a hang.
    {
        Scene scene; std::string err;
        auto* vs = Attach(scene, "node 1 OnUpdate\nnode 2 Timer 0\nnode 3 AddVar t\nnode 4 Const 1\n"
                                  "data 3 0 4 0\nexec 1 0 2\nexec 2 0 3\nentry OnUpdate 1\n", &err);
        scene.Start(); scene.Update(0.1f); scene.Update(0.1f);
        CHECK_NEAR(Var(vs, "t"), 2.0f, 0.01f);
    }
    // Wait Seconds and Wait Once with zero, negative, or NaN do not park the chain.
    for (const char* node : {"Delay 0", "Delay -2", "Delay nan", "Wait 0", "Wait -1", "Wait nan"}) {
        Scene scene; std::string err;
        const std::string text = std::string("node 1 OnStart\nnode 2 ") + node +
            "\nnode 3 AddVar d\nnode 4 Const 1\ndata 3 0 4 0\nexec 1 0 2\nexec 2 0 3\nentry OnStart 1\n";
        auto* vs = Attach(scene, text, &err);
        scene.Start();
        CHECK_NEAR(Var(vs, "d"), 1.0f, 0.01f);
        if (std::fabs(Var(vs, "d") - 1.0f) > 0.01f) std::printf("  immediate pass failed for %s\n", node);
    }
    // A real Wait Seconds pauses, then finishes on a later frame.
    {
        Scene scene; std::string err;
        auto* vs = Attach(scene, "node 1 OnStart\nnode 2 Delay 0.5\nnode 3 AddVar d\nnode 4 Const 1\n"
                                  "data 3 0 4 0\nexec 1 0 2\nexec 2 0 3\nentry OnStart 1\n", &err);
        scene.Start(); CHECK_NEAR(Var(vs, "d"), 0.0f, 0.01f);
        scene.Update(0.2f); CHECK_NEAR(Var(vs, "d"), 0.0f, 0.01f);
        scene.Update(0.4f); CHECK_NEAR(Var(vs, "d"), 1.0f, 0.01f);
    }
    // Gate: shut on false and on NaN. Open when unwired.
    {
        Scene scene; std::string err;
        auto* vs = Attach(scene, "node 1 OnStart\nnode 2 Gate\nnode 3 Const nan\nnode 4 AddVar g\nnode 5 Const 1\n"
                                  "data 2 0 3 0\ndata 4 0 5 0\nexec 1 0 2\nexec 2 0 4\nentry OnStart 1\n", &err);
        scene.Start(); CHECK_NEAR(Var(vs, "g"), 0.0f, 0.01f);
    }
    {
        Scene scene; std::string err;
        auto* vs = Attach(scene, "node 1 OnStart\nnode 2 Gate\nnode 3 AddVar g\nnode 4 Const 1\n"
                                  "data 3 0 4 0\nexec 1 0 2\nexec 2 0 3\nentry OnStart 1\n", &err);
        scene.Start(); CHECK_NEAR(Var(vs, "g"), 1.0f, 0.01f);
    }
    // Switch Number: negative and NaN take Default, not a crash.
    {
        Scene scene; std::string err;
        auto* vs = Attach(scene, "node 1 OnStart\nnode 2 SwitchInt\nnode 3 Const nan\nnode 4 AddVar s\nnode 5 Const 1\n"
                                  "data 2 0 3 0\ndata 4 0 5 0\nexec 1 0 2\nexec 2 3 4\nentry OnStart 1\n", &err);
        scene.Start(); CHECK_NEAR(Var(vs, "s"), 1.0f, 0.01f);
    }
    {
        Scene scene; std::string err;
        auto* vs = Attach(scene, "node 1 OnStart\nnode 2 SwitchInt\nnode 3 Const -4\nnode 4 AddVar s\nnode 5 Const 1\n"
                                  "data 2 0 3 0\ndata 4 0 5 0\nexec 1 0 2\nexec 2 3 4\nentry OnStart 1\n", &err);
        scene.Start(); CHECK_NEAR(Var(vs, "s"), 1.0f, 0.01f);
    }
    // For Loop counts down. A text wired into First is the wrong kind: it reads as 0, and does not crash.
    {
        Scene scene; std::string err;
        auto* vs = Attach(scene, "node 1 OnStart\nnode 2 ForLoop\nnode 3 Const 3\nnode 4 Const 1\nnode 5 AddVar s\n"
                                  "data 2 0 3 0\ndata 2 1 4 0\ndata 5 0 2 0\nexec 1 0 2\nexec 2 0 5\nentry OnStart 1\n", &err);
        scene.Start(); CHECK_NEAR(Var(vs, "s"), 6.0f, 0.01f);   // 3+2+1
    }
    {
        Scene scene; std::string err;
        auto* vs = Attach(scene, "node 1 OnStart\nnode 2 ForLoop\nnode 3 Const \"nope\"\nnode 4 Const 1\nnode 5 AddVar s\n"
                                  "data 2 0 3 0\ndata 2 1 4 0\ndata 5 0 2 0\nexec 1 0 2\nexec 2 0 5\nentry OnStart 1\n", &err);
        scene.Start(); CHECK_NEAR(Var(vs, "s"), 1.0f, 0.01f);   // 0 and 1
        CHECK(vs->Graph()->ErrorCount() == 0);
    }
    // Repeat zero and negative do nothing.
    {
        Scene scene; std::string err;
        auto* vs = Attach(scene, "node 1 OnStart\nnode 2 Repeat 0\nnode 3 AddVar s\nnode 4 Const 1\n"
                                  "data 3 0 4 0\nexec 1 0 2\nexec 2 0 3\nentry OnStart 1\n", &err);
        scene.Start(); CHECK_NEAR(Var(vs, "s"), 0.0f, 0.01f);
    }
    {
        Scene scene; std::string err;
        auto* vs = Attach(scene, "node 1 OnStart\nnode 2 Repeat -5\nnode 3 AddVar s\nnode 4 Const 1\n"
                                  "data 3 0 4 0\nexec 1 0 2\nexec 2 0 3\nentry OnStart 1\n", &err);
        scene.Start(); CHECK_NEAR(Var(vs, "s"), 0.0f, 0.01f);
    }
    // Empty list: For Each's body does not run, and it is not an error. A number is the wrong kind.
    {
        Scene scene; std::string err;
        auto* vs = Attach(scene, "node 1 OnStart\nnode 2 ForEach\nnode 3 NewList\nnode 4 AddVar s\nnode 5 Const 1\n"
                                  "data 2 0 3 0\ndata 4 0 5 0\nexec 1 0 2\nexec 2 0 4\nentry OnStart 1\n", &err);
        scene.Start(); CHECK_NEAR(Var(vs, "s"), 0.0f, 0.01f);
        CHECK(vs->Graph()->ErrorCount() == 0);
    }
    {
        Scene scene; std::string err;
        auto* vs = Attach(scene, "node 1 OnStart\nnode 2 ForEach\nnode 3 Const 5\nnode 4 AddVar s\nnode 5 Const 1\n"
                                  "data 2 0 3 0\ndata 4 0 5 0\nexec 1 0 2\nexec 2 0 4\nentry OnStart 1\n", &err);
        scene.Start(); CHECK_NEAR(Var(vs, "s"), 0.0f, 0.01f);
        CHECK(vs->Graph()->ErrorCount() > 0);
    }
    // Break Loop outside a loop reports an error instead of crashing.
    {
        Scene scene; std::string err;
        auto* vs = Attach(scene, "node 1 OnStart\nnode 2 Break\nexec 1 0 2\nentry OnStart 1\n", &err);
        scene.Start(); CHECK(vs->Graph()->ErrorCount() > 0);
    }
    // Every Number of Times: NaN means every time, not a crash.
    {
        Scene scene; std::string err;
        auto* vs = Attach(scene, "node 1 OnStart\nnode 2 EveryN\nnode 3 Const nan\nnode 4 AddVar s\nnode 5 Const 1\n"
                                  "data 2 0 3 0\ndata 4 0 5 0\nexec 1 0 2\nexec 2 0 4\nentry OnStart 1\n", &err);
        scene.Start(); CHECK_NEAR(Var(vs, "s"), 1.0f, 0.01f);
    }
    // Cooldown blocks until time passes.
    {
        Scene scene; std::string err;
        auto* vs = Attach(scene, "node 1 OnUpdate\nnode 2 Cooldown 1\nnode 3 AddVar s\nnode 4 Const 1\n"
                                  "data 3 0 4 0\nexec 1 0 2\nexec 2 0 3\nentry OnUpdate 1\n", &err);
        scene.Start();
        scene.Update(0.1f); CHECK_NEAR(Var(vs, "s"), 1.0f, 0.01f);
        scene.Update(0.1f); CHECK_NEAR(Var(vs, "s"), 1.0f, 0.01f);
        Time::Step(2.0f);
        scene.Update(0.1f); CHECK_NEAR(Var(vs, "s"), 2.0f, 0.01f);
    }
    // Random Branch: chance 0 never takes True, chance 1 always does.
    {
        Scene scene; std::string err;
        auto* vs = Attach(scene, "node 1 OnStart\nnode 2 ChanceBranch\nnode 3 Const 0\nnode 4 AddVar s\nnode 5 Const 1\n"
                                  "data 2 0 3 0\ndata 4 0 5 0\nexec 1 0 2\nexec 2 0 4\nentry OnStart 1\n", &err);
        scene.Start(); CHECK_NEAR(Var(vs, "s"), 0.0f, 0.01f);
    }

    // The playground scene: every flow node, then the thing you see in Play.
    const std::string graph = FlowDemoGraph();
    std::string playgrounds;   // the logic-runtime scenes' graphs
    {
        std::error_code pec;
        for (const auto& ent : std::filesystem::directory_iterator(std::string(OKAY_SOURCE_DIR) + "/examples/visual_script/playground", pec)) {
            if (ent.path().extension() != ".okayvs") continue;
            std::ifstream in(ent.path());
            playgrounds.append(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            playgrounds += "\n";
        }
    }
    for (const VsDef& d : Registry()) {
        if (d.cat != CAT_FLOW) continue;
        // These four are exercised, with a scene, by test_vs_runtime (maps, latch, switch, run graph).
        if (!std::strcmp(d.type, "SwitchCases") || !std::strcmp(d.type, "LatchGate") ||
            !std::strcmp(d.type, "ForEachMap") || !std::strcmp(d.type, "RunGraph")) continue;
        // Flow nodes that came in with the logic runtime are shown by their own
        // Node Playground scene (VsPlaygroundForNode), not by this one.
        const bool listed = graph.find(" " + std::string(d.type) + "\n") != std::string::npos ||
                            graph.find(" " + std::string(d.type) + " ") != std::string::npos ||
                            VsPlaygroundForNode(d.type) != nullptr ||
                            playgrounds.find(" " + std::string(d.type) + "\n") != std::string::npos ||
                            playgrounds.find(" " + std::string(d.type) + " ") != std::string::npos;
        CHECK(listed);
        if (!listed) std::printf("  demo graph is missing %s\n", d.type);
    }
    {
        Scene scene; AddDemoObjects(scene, graph); Play(scene); ExpectDemo(scene, "built");
        const std::string path = std::string(OKAY_SOURCE_DIR) + "/examples/node_playground/flow.okayscene";
        if (const char* w = std::getenv("OKAY_WRITE_PLAYGROUND"); w && w[0] == '1') {
            Scene writing; AddDemoObjects(writing, graph);
            CHECK(SceneSerializer::SaveToFile(writing, path));
            std::printf("  wrote %s\n", path.c_str());
        }
        Scene loaded; std::string err;
        CHECK(SceneSerializer::LoadFromFile(loaded, path, &err));
        if (!err.empty()) std::printf("  load: %s\n", err.c_str());
        Play(loaded); ExpectDemo(loaded, "file");
    }

    Log::sink = sink;
    TEST_MAIN_RESULT();
}
