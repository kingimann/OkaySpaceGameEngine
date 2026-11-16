// OkayScript robustness (issue #888, findings 32, 33 (the script half), 35, 36, 37 and 48).
//
//  35  An array or map that holds itself crashed the process (stack overflow) when printed,
//      turned into text or written as JSON. Now: [...] / {...} in text, a clear error in JSON.
//  36  The parser's depth cap only covered ( - ! ...; nested blocks, a ? b : c ? ..., () => () =>
//      and a = b = ... overflowed the stack in Load and in the editor's live checks.
//  37  pad_left was quadratic (pad_left("", 1000000) froze for seconds); len("hello") was 0.
//  33  Float -> int casts with NaN / inf / 1e30 (undefined behaviour), and two freezes:
//      spawn_wave(prefab, inf) and str_repeat("x", inf) looped two billion times.
//  48  set_volume(x) was not clamped to 0..1, and NaN got through every volume path.
//  32  Workshop item ids went through a 32-bit float, so a real id was rounded.
//
// Every case that crashed or hung before the fix runs in its own forked child process with a
// time limit (Linux), so before the fix it shows up here as a failed check ("crashed", "hung"),
// not as a crashed test run. Built for issue #888 from the engine's own source and docs; no
// other project's code.
#include "test_framework.hpp"
#include "test_sandbox.hpp"
#include <Okay.hpp>
#include "okay/Scripting/OkayScriptVM.hpp"
#include "okay/Audio/AudioMixer.hpp"
#include "okay/Core/DisplaySettings.hpp"
#include "okay/Core/Prefs.hpp"
#include "okay/Platform/Steam/Steam.hpp"
#if __has_include("okay/Platform/Steam/WorkshopId.hpp")
#include "okay/Platform/Steam/WorkshopId.hpp"
#define OKAY_TEST_HAS_WORKSHOP_ID 1
#endif

#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <vector>

#ifndef _WIN32
#include <csignal>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace okay;
using Value = vs::VsValue;

namespace {

const float kNaN = std::numeric_limits<float>::quiet_NaN();
const float kInf = std::numeric_limits<float>::infinity();

// ---- Console capture ---------------------------------------------------------
std::vector<std::string> g_log;
void CaptureLog() {
    Log::sink = [](Log::Level, const std::string& m) { if (g_log.size() < 200) g_log.push_back(m); };
}
bool Logged(const std::string& part) {
    for (const auto& m : g_log) if (m.find(part) != std::string::npos) return true;
    return false;
}

double MsSince(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

// ---- Run a case in a child process -------------------------------------------
// Before the fix these cases killed the process (stack overflow) or never returned. The child
// gets `seconds` (alarm) and at most 1.5 GB of address space (so a runaway string can't take
// the machine's memory); the parent reports how it ended.
bool Isolated(const char* name, unsigned seconds, const std::function<bool()>& body) {
#ifndef _WIN32
    std::cout.flush();
    std::cerr.flush();
    const pid_t pid = fork();
    if (pid == 0) {
        rlimit lim{};
        lim.rlim_cur = lim.rlim_max = (rlim_t)1536 * 1024 * 1024;
        setrlimit(RLIMIT_AS, &lim);
        alarm(seconds);
        bool ok = false;
        try { ok = body(); } catch (const std::exception& e) { std::cerr << "    exception: " << e.what() << "\n"; }
        std::cout.flush();
        std::cerr.flush();
        _exit(ok ? 0 : 1);
    }
    int st = 0;
    if (pid < 0 || waitpid(pid, &st, 0) != pid) { std::cerr << "  [" << name << "] could not fork\n"; return false; }
    if (WIFEXITED(st)) {
        if (WEXITSTATUS(st) == 0) return true;
        std::cerr << "  [" << name << "] a check failed\n";
        return false;
    }
    if (WIFSIGNALED(st)) {
        const int sig = WTERMSIG(st);
        if (sig == SIGALRM) std::cerr << "  [" << name << "] hung: still running after " << seconds << " s\n";
        else std::cerr << "  [" << name << "] crashed (signal " << sig << (sig == SIGSEGV ? ", SIGSEGV" : sig == SIGABRT ? ", SIGABRT" : "") << ")\n";
    }
    return false;
#else
    (void)seconds;
    const bool ok = body();
    if (!ok) std::cerr << "  [" << name << "] a check failed\n";
    return ok;
#endif
}
#define ISOLATED(name, seconds, ...) CHECK(Isolated(name, seconds, [&]() -> bool { __VA_ARGS__ }))

// ---- Scripts -----------------------------------------------------------------
// Load a script with no scene; its top level runs once.
struct Vm {
    std::unique_ptr<IScriptVM> vm = CreateScriptVM("okayscript");
    std::string error;
    bool loaded = false;
    explicit Vm(const std::string& src) { loaded = vm->Load(src, &error); }
    Value G(const char* n) const { return vm->GetGlobal(n); }
    std::string S(const char* n) const { return G(n).AsString(); }
};
// A scene with one scripted object ("Logic"), started; builtins that need a host or a
// scene (spawn_wave, grid_*, overlap3) run through it.
struct Host {
    Scene s{"Robust"};
    GameObject* go = nullptr;
    ScriptComponent* sc = nullptr;
    explicit Host(const std::string& src = "function start() {}\n") {
        s.physicsEnabled = false;
        go = s.CreateGameObject("Logic");
        sc = go->AddComponent<ScriptComponent>("okayscript");
        sc->LoadSource(src);
        s.Start();
    }
    OkayScriptVM* vm() const { return dynamic_cast<OkayScriptVM*>(sc->VM()); }
    Value Call(const char* fn, std::vector<Value> args, std::string* err = nullptr) {
        OkayScriptVM* v = vm();
        return v ? v->CallBuiltin(fn, std::move(args), err) : Value{};
    }
};
Value Call(const char* fn, std::vector<Value> args, std::string* err = nullptr) {
    static std::unique_ptr<IScriptVM> vm = [] { auto v = CreateScriptVM("okayscript"); v->Load("var _ = 0;"); return v; }();
    auto* o = dynamic_cast<OkayScriptVM*>(vm.get());
    return o ? o->CallBuiltin(fn, std::move(args), err) : Value{};
}
float N(const char* fn, std::vector<Value> args) { return Call(fn, std::move(args)).AsFloat(); }
std::string T(const char* fn, std::vector<Value> args) { return Call(fn, std::move(args)).AsString(); }

// A flat list of names as "[a, b]" (for overlap3's result).
std::string ValueTextForTest(const Value& v) {
    std::string s = "[";
    if (auto a = v.AsArray())
        for (std::size_t i = 0; i < a->size(); ++i) { if (i) s += ", "; s += (*a)[i].AsString(); }
    return s + "]";
}

std::string Repeat(const std::string& s, int n) { std::string o; o.reserve(s.size() * (std::size_t)n); for (int i = 0; i < n; ++i) o += s; return o; }

// ============================== Finding 35 ==============================
void SelfContaining() {
    RUN_SUITE("35: arrays and maps that hold themselves");
    const char* arraySelf = "var a = []; push(a, a);\n";

    ISOLATED("str(a), a holds itself", 20, {
        Vm v(std::string(arraySelf) + "var s = str(a); var t = to_str(a);");
        return v.loaded && v.S("s") == "[[...]]" && v.S("t") == "[[...]]";
    });
    ISOLATED("print(a), a holds itself", 20, {
        CaptureLog();
        Vm v(std::string(arraySelf) + "print(a); print(\"after\");");
        return v.loaded && Logged("[[...]]") && Logged("after");
    });
    ISOLATED("\"text\" + a and $\"{a}\"", 20, {
        Vm v("var a = [1, 2]; a[0] = a; var s = \"a is \" + a; var t = $\"{a}\"; var u = a + \"!\";");
        return v.loaded && v.S("s") == "a is [[...], 2]" && v.S("t") == "[[...], 2]" && v.S("u") == "[[...], 2]!";
    });
    ISOLATED("map holds itself", 20, {
        CaptureLog();
        Vm v("var m = map(); m[\"me\"] = m; m[\"n\"] = 1; var s = str(m); print(m);");
        return v.loaded && v.S("s") == "{me: {...}, n: 1}" && Logged("{me: {...}, n: 1}");
    });
    ISOLATED("tree with parent links: text", 20, {
        Vm v("var root = map(); root[\"kids\"] = []; var kid = map(); kid[\"parent\"] = root; kid[\"name\"] = \"k\";\n"
             "push(root[\"kids\"], kid); var s = str(root);");
        return v.loaded && v.S("s") == "{kids: [{name: k, parent: {...}}]}";
    });
    ISOLATED("to_json of a cycle is a clear error", 20, {
        Vm v(std::string(arraySelf) +
             "var err = \"\"; var j = \"unset\";\n"
             "try { j = to_json(a); } catch (e) { err = e; }\n"
             "var root = map(); root[\"kids\"] = []; var kid = map(); kid[\"parent\"] = root; push(root[\"kids\"], kid);\n"
             "var err2 = \"\"; try { json_stringify(root); } catch (e) { err2 = e; }");
        const std::string e = v.S("err"), e2 = v.S("err2");
        if (!v.loaded) std::cerr << "    load: " << v.error << "\n";
        else std::cerr << "    to_json error: " << e << "\n";
        return v.loaded && v.S("j") == "unset" && e.find("contains itself") != std::string::npos &&
               e2.find("contains itself") != std::string::npos;
    });
    ISOLATED("== and join with a cycle", 20, {
        Vm v(std::string(arraySelf) + "var eq = a == \"x\"; var j = join([a, 1], \";\"); var f = format(\"{0}\", a);");
        return v.loaded && !v.G("eq").AsBool() && v.S("j") == "[[...]];1" && v.S("f") == "[[...]]";
    });

    // Not cycles: the same array twice is written twice; deep but finite nests are capped.
    {
        Vm v("var x = [1]; var y = [x, x]; var s = str(y); var m = map(); m[\"a\"] = x; m[\"b\"] = x; var t = str(m);"
             "var j = to_json(y);");
        CHECK(v.loaded);
        CHECK(v.S("s") == "[[1], [1]]");
        CHECK(v.S("t") == "{a: [1], b: [1]}");
        CHECK(v.S("j") == "[[1],[1]]");
    }
    {
        // Ordinary values read exactly as before.
        Vm v("var s = str([1, 2.5, \"x\", true, [3]]); var m = map(); m[\"k\"] = [1]; var t = str(m); var e = str([]);"
             "var j = to_json([1, \"a\", map()]);");
        CHECK(v.S("s") == "[1, 2.5, x, true, [3]]");
        CHECK(v.S("t") == "{k: [1]}");
        CHECK(v.S("e") == "[]");
        CHECK(v.S("j") == "[1,\"a\",{}]");
    }
    ISOLATED("300-deep nest: text and JSON are capped", 20, {
        Vm v("var a = []; var i = 0; while (i < 300) { var b = []; push(b, a); a = b; i = i + 1; }\n"
             "var s = str(a); var err = \"\"; try { to_json(a); } catch (e) { err = e; }\n"
             "var c = []; i = 0; while (i < 200) { var d = []; push(d, c); c = d; i = i + 1; }\n"
             "var back = to_json(from_json(to_json(c))) == to_json(c);");
        const std::string s = v.S("s");
        return v.loaded && s.find("[...]") != std::string::npos && s.size() < 1000 &&
               v.S("err").find("nested too deeply") != std::string::npos && v.G("back").AsBool();
    });
}

// ============================== Finding 36 ==============================
bool NestedTooDeep(const std::string& src, const char* what) {
    std::unique_ptr<IScriptVM> vm = CreateScriptVM("okayscript");
    std::string loadErr, validateErr;
    const bool loaded = vm->Load(src, &loadErr);
    const bool valid = vm->Validate(src, &validateErr);
    const auto diags = vm->ValidateAll(src);
    bool diag = false;
    for (const auto& d : diags) if (d.message.find("nested too deeply") != std::string::npos) diag = true;
    const bool ok = !loaded && loadErr.find("nested too deeply") != std::string::npos &&
                    !valid && validateErr.find("nested too deeply") != std::string::npos && diag;
    std::cerr << "    " << what << ": " << (loaded ? "loaded" : loadErr) << "\n";
    return ok;
}

void DeepNesting() {
    RUN_SUITE("36: deep nesting is a parse error, not a stack overflow");
    const int n = 100000;
    ISOLATED("if (true) { x 100000", 60, {
        return NestedTooDeep("function start() {\n" + Repeat("if (true) {", n) + Repeat("}", n) + "\n}\n", "if");
    });
    ISOLATED("while (false) { x 100000", 60, {
        return NestedTooDeep("function start() {\n" + Repeat("while (false) {", n) + Repeat("}", n) + "\n}\n", "while");
    });
    ISOLATED("{ { { x 100000", 60, {
        return NestedTooDeep("function start() {\n" + Repeat("{", n) + Repeat("}", n) + "\n}\n", "blocks");
    });
    ISOLATED("true ? ... : 0 x 100000", 60, {
        return NestedTooDeep("var r = " + Repeat("true ? ", n) + "1" + Repeat(" : 0", n) + ";\n", "ternary");
    });
    ISOLATED("() => x 100000", 60, {
        return NestedTooDeep("var f = " + Repeat("() => ", n) + "1;\n", "lambda");
    });
    ISOLATED("a = a = ... x 100000", 60, {
        return NestedTooDeep("var a = 0;\n" + Repeat("a = ", n) + "1;\n", "assignment");
    });
    ISOLATED("namespace A { x 100000", 60, {
        return NestedTooDeep(Repeat("namespace A {", n) + Repeat("}", n) + "\n", "namespace");
    });
    ISOLATED("if (x) stmt without braces x 100000", 60, {
        return NestedTooDeep("function start() {\n" + Repeat("if (true) ", n) + "print(1);\n}\n", "if, no braces");
    });

    // Ordinary nesting still works, and the operator count still starts again per statement.
    {
        Vm v("var r = 0; var f = () => 7;\n" + Repeat("if (true) {", 40) + "r = (((1 + 2) * 3) > 4) ? f() : 0;" + Repeat("}", 40) +
             "\nvar q = true ? 1 : false ? 2 : 3;\nvar a = 0; var b = 0; a = b = 5;\n");
        CHECK(v.loaded);
        if (!v.loaded) std::cerr << "    " << v.error << "\n";
        CHECK_NEAR(v.G("r").AsFloat(), 7.0f, 1e-6f);
        CHECK_NEAR(v.G("q").AsFloat(), 1.0f, 1e-6f);
        CHECK_NEAR(v.G("a").AsFloat(), 5.0f, 1e-6f);
    }
    {
        std::string body;
        for (int i = 0; i < 1500; ++i) body += "x = x + 1 * 2 - 1;\n";   // 4500 operators in one function
        Vm v("var x = 0;\nfunction start() {\n" + body + "}\n" + Repeat("x = x + 1;\n", 1500));
        CHECK(v.loaded);
        CHECK_NEAR(v.G("x").AsFloat(), 1500.0f, 1e-3f);
    }
    {
        std::string chain = "var r = 0; var k = 150;\nif (k == 0) { r = 0; }";   // a long else-if chain still parses
        for (int i = 1; i < 200; ++i) chain += " else if (k == " + std::to_string(i) + ") { r = " + std::to_string(i) + "; }";
        Vm v(chain + "\n");
        CHECK(v.loaded);
        CHECK_NEAR(v.G("r").AsFloat(), 150.0f, 1e-6f);
    }
}

// ============================== Finding 37 ==============================
void PadAndLen() {
    RUN_SUITE("37: pad_left is linear; len of text is its length");
    ISOLATED("pad_left(\"\", 1000000) under 200 ms", 30, {
        Vm v("var _ = 0;");
        auto* o = dynamic_cast<OkayScriptVM*>(v.vm.get());
        const auto t0 = std::chrono::steady_clock::now();
        const Value r = o ? o->CallBuiltin("pad_left", {Value{std::string()}, Value{1000000.0f}}) : Value{};
        const double ms = MsSince(t0);
        std::cerr << "    pad_left(\"\", 1000000): " << ms << " ms, length " << r.AsString().size() << "\n";
        return ms < 200.0 && r.AsString().size() == 1000000u;
    });
    CHECK(T("pad_left", {Value{"7"}, Value{3.0f}, Value{"0"}}) == "007");
    CHECK(T("pad_left", {Value{"7"}, Value{3.0f}}) == "  7");
    CHECK(T("pad_left", {Value{"long"}, Value{2.0f}}) == "long");
    CHECK(T("pad_right", {Value{"7"}, Value{3.0f}, Value{"-"}}) == "7--");
    CHECK(T("pad_left", {Value{"x"}, Value{kInf}}).size() == (1u << 20));   // the #719 cap
    CHECK(T("pad_left", {Value{"x"}, Value{kNaN}}) == "x");

    CHECK_NEAR(N("len", {Value{"hello"}}), 5.0f, 1e-6f);
    CHECK_NEAR(N("len", {Value{"12"}}), 2.0f, 1e-6f);
    CHECK_NEAR(N("len", {Value{""}}), 0.0f, 1e-6f);
    CHECK_NEAR(N("len", {Value{3.0f}, Value{4.0f}}), 5.0f, 1e-6f);   // still a 2D vector length
    CHECK_NEAR(N("len", {Value{-3.0f}}), 3.0f, 1e-6f);
    {
        Vm v("var n = len([1, 2, 3]); var m = map(); m[\"a\"] = 1; var c = len(m); var s = len(\"hello\");");
        CHECK_NEAR(v.G("n").AsFloat(), 3.0f, 1e-6f);
        CHECK_NEAR(v.G("c").AsFloat(), 1.0f, 1e-6f);
        CHECK_NEAR(v.G("s").AsFloat(), 5.0f, 1e-6f);
    }
}

// ============================== Finding 33 ==============================
void Casts() {
    RUN_SUITE("33: float -> int casts with NaN, inf and 1e30");
    // Bit ops: saturate (1e30 / inf -> 2147483647, -1e30 -> -2147483648, NaN -> 0).
    CHECK_NEAR(N("bit_and", {Value{1e30f}, Value{1.0f}}), 1.0f, 0.0);
    CHECK_NEAR(N("bit_and", {Value{kNaN}, Value{1.0f}}), 0.0f, 0.0);
    CHECK_NEAR(N("bit_or", {Value{kInf}, Value{0.0f}}), 2147483647.0f, 0.0);
    CHECK_NEAR(N("bit_xor", {Value{-1e30f}, Value{0.0f}}), -2147483648.0f, 0.0);
    CHECK_NEAR(N("bit_not", {Value{kNaN}}), -1.0f, 0.0);
    CHECK_NEAR(N("shl", {Value{1.0f}, Value{31.0f}}), -2147483648.0f, 0.0);
    CHECK_NEAR(N("shl", {Value{-1.0f}, Value{1.0f}}), -2.0f, 0.0);
    CHECK_NEAR(N("shl", {Value{-1.0f}, Value{-1.0f}}), -2147483648.0f, 0.0);   // count & 31 = 31
    CHECK_NEAR(N("shl", {Value{1.0f}, Value{kNaN}}), 1.0f, 0.0);
    CHECK_NEAR(N("shr", {Value{-8.0f}, Value{1.0f}}), -4.0f, 0.0);            // keeps the sign
    CHECK_NEAR(N("shr", {Value{kInf}, Value{30.0f}}), 1.0f, 0.0);
    CHECK_NEAR(N("bit_set", {Value{0.0f}, Value{31.0f}}), -2147483648.0f, 0.0);
    CHECK_NEAR(N("bit_set", {Value{0.0f}, Value{1e30f}}), -2147483648.0f, 0.0);  // index 2147483647 & 31
    CHECK_NEAR(N("bit_clear", {Value{-1.0f}, Value{kNaN}}), -2.0f, 0.0);
    CHECK_NEAR(N("bit_toggle", {Value{kInf}, Value{0.0f}}), 2147483646.0f, 0.0);
    CHECK(Call("bit_test", {Value{1e30f}, Value{0.0f}}).AsBool());
    CHECK(T("hex", {Value{1e30f}}) == "0x7fffffff");
    CHECK(T("hex", {Value{kNaN}}) == "0x0");
    CHECK(T("hex", {Value{-1.0f}}) == "0xffffffff");
    CHECK(T("hex", {Value{255.0f}}) == "0xff");
    CHECK_NEAR(N("bit_and", {Value{12.0f}, Value{10.0f}}), 8.0f, 0.0);         // in range: unchanged

    // int.Parse / Convert.ToInt32: "nan" -> 0, "inf" / "1e30" saturate.
    CHECK_NEAR(N("int.Parse", {Value{"nan"}}), 0.0f, 0.0);
    CHECK(N("int.Parse", {Value{"inf"}}) > 9.0e18f);
    CHECK(N("int.Parse", {Value{"1e30"}}) > 9.0e18f);
    CHECK(N("int.Parse", {Value{"-1e30"}}) < -9.0e18f);
    CHECK(N("Convert.ToInt32", {Value{"-inf"}}) < -9.0e18f);
    CHECK_NEAR(N("int.Parse", {Value{"42.9"}}), 42.0f, 0.0);
    CHECK_NEAR(N("int.Parse", {Value{"-7.5"}}), -7.0f, 0.0);

    // Display settings: a value that is not a number changes nothing; huge ones clamp.
    {
        CHECK_NEAR(N("set_display_resolution", {Value{1280.0f}, Value{720.0f}}), 1.0f, 0.0);
        CHECK(DisplaySettings::Width() == 1280);
        CHECK_NEAR(N("set_display_resolution", {Value{kNaN}, Value{600.0f}}), 0.0f, 0.0);
        CHECK_NEAR(N("set_display_resolution", {Value{800.0f}, Value{kInf}}), 0.0f, 0.0);
        CHECK(DisplaySettings::Width() == 1280 && DisplaySettings::Height() == 720);
        CHECK_NEAR(N("set_display_resolution", {Value{1e30f}, Value{1e30f}}), 1.0f, 0.0);
        CHECK(DisplaySettings::Width() == DisplaySettings::kMaxWidth && DisplaySettings::Height() == DisplaySettings::kMaxHeight);
        CHECK_NEAR(N("set_display_resolution", {Value{-1e30f}, Value{-1e30f}}), 1.0f, 0.0);
        CHECK(DisplaySettings::Width() == DisplaySettings::kMinWidth);
        CHECK_NEAR(N("set_refresh_rate", {Value{60.0f}}), 1.0f, 0.0);
        CHECK_NEAR(N("set_refresh_rate", {Value{kNaN}}), 0.0f, 0.0);
        CHECK(DisplaySettings::RefreshHz() == 60);
        CHECK_NEAR(N("set_refresh_rate", {Value{1e30f}}), 1.0f, 0.0);
        CHECK(DisplaySettings::RefreshHz() == 1000);
        CHECK_NEAR(N("set_monitor", {Value{kNaN}}), 0.0f, 0.0);
        CHECK_NEAR(N("set_monitor", {Value{kInf}}), 0.0f, 0.0);
        Call("set_monitor", {Value{1e30f}});
        CHECK(DisplaySettings::MonitorIndex() >= -1);
        Call("set_monitor", {Value{"99999999999999999999"}});   // digits past int: clamps, no overflow
        CHECK(DisplaySettings::MonitorIndex() >= -1);
        CHECK(Call("get_refresh_rates", {Value{1e30f}, Value{kNaN}}).IsArray());
        CHECK(Call("get_refresh_rates", {Value{kInf}, Value{-1e30f}}).IsArray());
    }

    // Grid path costs and obstacles: a cell that is not a number is refused.
    {
        Host h;
        CHECK_NEAR(h.Call("grid_block", {Value{kNaN}, Value{1.0f}}).AsFloat(), 0.0f, 0.0);
        CHECK_NEAR(h.Call("grid_block", {Value{1.0f}, Value{kInf}}).AsFloat(), 0.0f, 0.0);
        CHECK_NEAR(h.Call("grid_block", {Value{1e30f}, Value{-1e30f}}).AsFloat(), 1.0f, 0.0);
        CHECK_NEAR(h.Call("grid_block", {Value{2.0f}, Value{3.0f}}).AsFloat(), 1.0f, 0.0);
        CHECK_NEAR(h.Call("grid_set_tile_cost", {Value{kNaN}, Value{2.0f}}).AsFloat(), 0.0f, 0.0);
        CHECK_NEAR(h.Call("grid_set_tile_cost", {Value{1e30f}, Value{2.0f}}).AsFloat(), 1.0f, 0.0);
        CHECK_NEAR(h.Call("grid_set_cell_cost", {Value{kInf}, Value{0.0f}, Value{2.0f}}).AsFloat(), 0.0f, 0.0);
        CHECK_NEAR(h.Call("grid_set_cell_cost", {Value{1e30f}, Value{0.0f}, Value{2.0f}}).AsFloat(), 1.0f, 0.0);
        CHECK_NEAR(h.Call("grid_set_cell_cost", {Value{"Logic"}, Value{4.0f}, Value{kNaN}, Value{2.0f}}).AsFloat(), 0.0f, 0.0);
    }

    // Leaderboard scores saturate; NaN uploads nothing.
    {
        Call("steam_leaderboard", {Value{"robust_big"}, Value{1e30f}});
        Call("steam_leaderboard", {Value{"robust_low"}, Value{-kInf}});
        Call("steam_leaderboard", {Value{"robust_nan"}, Value{kNaN}});
        Call("steam_leaderboard", {Value{"robust_ok"}, Value{500.0f}});
        auto top = [](const char* b) { return Steam::Get().DownloadLeaderboardTop(b, 5); };
        CHECK(top("robust_big").size() == 1 && top("robust_big")[0].score == 2147483647);
        CHECK(top("robust_low").size() == 1 && top("robust_low")[0].score <= 0);   // the simulation keeps the best, which starts at 0
        CHECK(top("robust_nan").empty());
        CHECK(top("robust_ok").size() == 1 && top("robust_ok")[0].score == 500);
    }

    // Layer masks (overlap3 and the other 3D queries): NaN and <= 0 mean every layer, past 2^32 every bit.
    {
        Host h;
        GameObject* box = h.s.CreateGameObject("Crate");
        auto* bc = box->AddComponent<BoxCollider3D>();
        bc->size = {1, 1, 1};
        h.go->transform->localPosition = {0, 50, 0};
        auto names = [&](Value mask) {
            Value r = h.Call("overlap3", {Value{"sphere"}, Value{Vec3{0, 0, 0}}, Value{2.0f}, mask});
            return ValueTextForTest(r);
        };
        CHECK(names(Value{kNaN}) == "[Crate]");
        CHECK(names(Value{1e30f}) == "[Crate]");
        CHECK(names(Value{kInf}) == "[Crate]");
        CHECK(names(Value{-1.0f}) == "[Crate]");
        CHECK(names(Value{1.0f}) == "[Crate]");   // layer 0's bit
        CHECK(names(Value{2.0f}) == "[]");        // only layer 1
    }

    // The two freezes. A prefab that is not there, so nothing is ever spawned.
    ISOLATED("spawn_wave(missing, 1e30 / inf / NaN) returns at once", 30, {
        CaptureLog();
        Host h;
        bool ok = true;
        for (float count : {1e30f, kInf, kNaN, 5000.0f}) {
            g_log.clear();
            const auto t0 = std::chrono::steady_clock::now();
            const float made = h.Call("spawn_wave", {Value{"no_such_prefab_robust.okayprefab"}, Value{count}}).AsFloat();
            const double ms = MsSince(t0);
            const bool said = Logged("spawn_wave") && Logged("too many");
            std::cerr << "    spawn_wave(missing, " << count << "): " << ms << " ms, made " << made << (said ? ", said why" : ", no message") << "\n";
            ok = ok && made == 0.0f && ms < 200.0 && said;
        }
        g_log.clear();
        const auto t0 = std::chrono::steady_clock::now();
        const float made = h.Call("spawn_wave", {Value{"no_such_prefab_robust.okayprefab"}, Value{500.0f}}).AsFloat();
        ok = ok && made == 0.0f && MsSince(t0) < 200.0;   // a missing prefab stops at the first try
        return ok;
    });
    ISOLATED("str_repeat(\"x\", inf / 1e30) returns at once", 30, {
        bool ok = true;
        for (float count : {kInf, 1e30f, 2e6f}) {
            std::string err;
            const auto t0 = std::chrono::steady_clock::now();
            const Value r = Call("str_repeat", {Value{"x"}, Value{count}}, &err);
            const double ms = MsSince(t0);
            std::cerr << "    str_repeat(\"x\", " << count << "): " << ms << " ms, error: " << (err.empty() ? "(none)" : err) << "\n";
            ok = ok && ms < 200.0 && err.find("str_repeat") != std::string::npos && err.find("too long") != std::string::npos;
        }
        std::string err;
        ok = ok && Call("str_repeat", {Value{""}, Value{kInf}}, &err).AsString().empty() && err.empty();
        return ok;
    });
    CHECK(T("str_repeat", {Value{"ab"}, Value{3.0f}}) == "ababab");
    CHECK(T("str_repeat", {Value{"ab"}, Value{2.9f}}) == "abab");
    CHECK(T("str_repeat", {Value{"ab"}, Value{-2.0f}}).empty());
    CHECK(T("str_repeat", {Value{"ab"}, Value{kNaN}}).empty());
    CHECK(T("str_repeat", {Value{"x"}, Value{1048576.0f}}).size() == 1048576u);
}

// ============================== Finding 48 ==============================
void VolumeClamp() {
    RUN_SUITE("48: set_volume is clamped to 0..1 and NaN-safe");
    const float before = AudioMixer::masterVolume;
    Call("set_volume", {Value{2.0f}});
    CHECK_NEAR(AudioMixer::masterVolume, 1.0f, 0.0);
    Call("set_volume", {Value{-1.0f}});
    CHECK_NEAR(AudioMixer::masterVolume, 0.0f, 0.0);
    Call("set_volume", {Value{0.5f}});
    CHECK_NEAR(AudioMixer::masterVolume, 0.5f, 0.0);
    Call("set_volume", {Value{kNaN}});
    CHECK(std::isfinite(AudioMixer::masterVolume));
    CHECK_NEAR(AudioMixer::masterVolume, 0.5f, 0.0);   // ignored
    Call("set_master_volume", {Value{kInf}});
    CHECK_NEAR(AudioMixer::masterVolume, 0.5f, 0.0);
    {
        Vm v("set_volume(5); var a = volume(); set_volume(0.25); var b = volume();");
        CHECK_NEAR(v.G("a").AsFloat(), 1.0f, 0.0);
        CHECK_NEAR(v.G("b").AsFloat(), 0.25f, 0.0);
    }
    Call("set_group_volume", {Value{"music"}, Value{0.75f}});
    Call("set_group_volume", {Value{"music"}, Value{kNaN}});
    CHECK_NEAR(AudioMixer::groupVolume[(int)AudioGroup::Music], 0.75f, 0.0);
    Call("set_group_volume", {Value{"music"}, Value{3.0f}});
    CHECK_NEAR(AudioMixer::groupVolume[(int)AudioGroup::Music], 1.0f, 0.0);

    // A NaN saved in the prefs no longer comes back as the master volume.
    AudioMixer::masterVolume = 0.6f;
    Prefs::SetFloat("audio.master", kNaN);
    Prefs::SetFloat("audio.music", kNaN);
    AudioMixer::LoadSettings();
    CHECK(std::isfinite(AudioMixer::masterVolume));
    CHECK(std::isfinite(AudioMixer::groupVolume[(int)AudioGroup::Music]));
    Prefs::SetFloat("audio.master", 0.4f);
    AudioMixer::LoadSettings();
    CHECK_NEAR(AudioMixer::masterVolume, 0.4f, 1e-6f);

    // NaN never reaches the output.
    {
        Scene s("Mix");
        AudioMixer::masterVolume = kNaN;
        std::vector<float> out(2 * 256, 0.0f);
        AudioMixer::RenderStereo(s, out.data(), 256);
        bool finite = true;
        for (float x : out) finite = finite && std::isfinite(x);
        CHECK(finite);
    }
    AudioMixer::masterVolume = before;
}

// ============================== Finding 32 ==============================
void Workshop() {
    RUN_SUITE("32: Workshop item ids keep every digit");
#ifdef OKAY_TEST_HAS_WORKSHOP_ID
    const std::uint64_t big = 2147483648123ull;
    std::uint64_t id = 0;
    CHECK(WorkshopIdToText(big) == "2147483648123");
    CHECK(WorkshopIdFromText("2147483648123", id) && id == big);
    CHECK(WorkshopIdFromText(" 2847193562 ", id) && id == 2847193562ull);
    CHECK(WorkshopIdFromText("18446744073709551615", id) && id == 18446744073709551615ull);
    id = 7;
    CHECK(!WorkshopIdFromText("18446744073709551616", id) && id == 7);   // past 2^64 - 1
    CHECK(!WorkshopIdFromText("-1", id));
    CHECK(!WorkshopIdFromText("+5", id));
    CHECK(!WorkshopIdFromText("1e5", id));
    CHECK(!WorkshopIdFromText("12a", id));
    CHECK(!WorkshopIdFromText("", id));
    CHECK(!WorkshopIdFromText("0", id));
    CHECK(id == 7);
    CHECK(WorkshopIdFromNumber(1000.0, id) && id == 1000);
    CHECK(WorkshopIdFromNumber(16777215.0, id) && id == 16777215);
    CHECK(!WorkshopIdFromNumber(16777216.0, id));   // a float may have rounded to this
    CHECK(!WorkshopIdFromNumber(2847193600.0, id));
    CHECK(!WorkshopIdFromNumber(-1.0, id));
    CHECK(!WorkshopIdFromNumber(0.0, id));
    CHECK(!WorkshopIdFromNumber(1.5, id));
    CHECK(!WorkshopIdFromNumber(std::nan(""), id));
    CHECK(!WorkshopIdFromNumber(HUGE_VAL, id));
    CHECK(!WorkshopIdFromNumber(1e30, id));
#else
    std::cerr << "  okay/Platform/Steam/WorkshopId.hpp is missing (the fix is not in this build)\n";
    CHECK(false);
#endif

    // Through the script API, with the simulation backend (no Steam here): its ids are as big
    // as real ones, past what a float holds. publish gives text; path / unsubscribe / subscribe
    // take it back and reach the same item.
    Steam::Shutdown();
    CaptureLog();
    Vm v("var id = steam_workshop_publish(\"Robust Map\", \"/maps/robust\", \"a level\");\n"
         "var p = steam_workshop_path(id);\n"
         "var u = steam_workshop_unsubscribe(id);\n"
         "var s = steam_workshop_subscribe(id);\n"
         "var text = $\"{id}\";\n"
         "var bad1 = steam_workshop_path(-1);\n"
         "var bad2 = steam_workshop_subscribe(float.Parse(\"nan\"));\n"
         "var bad3 = steam_workshop_path(\"12x\");\n");
    CHECK(v.loaded);
    const Value idv = v.G("id");
    std::cerr << "    published id: " << idv.AsString() << " (a " << (idv.IsString() ? "text" : "number") << ")\n";
    CHECK(idv.IsString());
    CHECK(idv.AsString().size() >= 10);
    std::uint64_t exact = 0;
#ifdef OKAY_TEST_HAS_WORKSHOP_ID
    CHECK(WorkshopIdFromText(idv.AsString(), exact));
#endif
    CHECK(exact > (1ull << 24));
    CHECK(Steam::Get().WorkshopItemPath(exact) == "/maps/robust");
    CHECK(v.S("p") == "/maps/robust");
    CHECK_NEAR(v.G("u").AsFloat(), 1.0f, 0.0);
    CHECK_NEAR(v.G("s").AsFloat(), 1.0f, 0.0);
    CHECK(Steam::Get().WorkshopIsSubscribed(exact));
    CHECK(v.S("text") == idv.AsString());
    CHECK(v.S("bad1").empty());
    CHECK_NEAR(v.G("bad2").AsFloat(), 0.0f, 0.0);
    CHECK(v.S("bad3").empty());
    CHECK(Logged("is not a Workshop item id"));
    {
        Vm w("var id = steam_workshop_publish(\"Second\", \"/maps/two\"); var ok = id != 0; var same = id == $\"{id}\";\n"
             "var p = steam_workshop_path(id);");
        CHECK(w.G("ok").AsBool());
        CHECK(w.G("same").AsBool());
        CHECK(w.S("p") == "/maps/two");
    }
}

} // namespace

int main() {
    okaytest::Sandbox sandbox("okayscript_robust", OKAY_SOURCE_DIR);
    RUN_SUITE("okayscript_robust");
    SelfContaining();
    DeepNesting();
    PadAndLen();
    Casts();
    VolumeClamp();
    Workshop();
    Log::sink = nullptr;
    CHECK(sandbox.Verify());
    TEST_MAIN_RESULT();
}
