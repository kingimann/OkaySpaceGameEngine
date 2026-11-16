// Fn.<builtin> nodes have as many input pins as their builtin reads (#888 finding 22).
// Before the fix every Fn.* node had four pins and passed at most four arguments, so dist3 (6), remap (5) and
// raycast3 (6 to 9) silently gave 0 or "" from a graph. This test:
//   * runs Fn.dist3, Fn.remap and Fn.raycast3 in a real scene and compares with the text script;
//   * checks that the validator warns about a `default` or `data` line aimed at a pin the node does not have;
//   * reads engine/src/Scripting/OkayScriptVM.cpp and works out, for every builtin, the highest argument index its
//     code reads (a small scanner written for this test, see ScanArities), then checks every Fn.* node has at least
//     that many inputs;
//   * checks that an old four-pin graph still gives the same result and that unfed pins after the last fed one are
//     not passed (several builtins branch on the argument count).
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Scripting/OkayScriptVM.hpp"
#include "okay/VisualScript/NodeGraph.hpp"
#include "okay/VisualScript/VsBuiltins.hpp"
#include "okay/VisualScript/VsTypes.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace okay;

namespace {

std::vector<std::string> g_errs;
void OnErr(const vs::VsRuntimeError& e) { g_errs.push_back(e.nodeType + " " + e.message); }

// Load `graph` on a fresh object of `s`, start the scene and run one frame. Returns the component.
VisualScriptComponent* RunGraph(Scene& s, const std::string& graph) {
    GameObject* go = s.CreateGameObject("Logic");
    auto* vc = go->AddComponent<VisualScriptComponent>();
    std::string err;
    if (!vc->LoadFromText(graph, &err)) { std::printf("  load: %s\n", err.c_str()); return nullptr; }
    g_errs.clear();
    s.Start();
    s.Update(1.0f / 60.0f);
    for (const auto& e : g_errs) std::printf("  runtime error: %s\n", e.c_str());
    return vc;
}

// OnStart -> <type> with these typed pin values -> SetVar r.
std::string CallGraph(const std::string& type, const std::vector<std::pair<int, std::string>>& defaults) {
    std::string g = "node 1 OnStart\nentry OnStart 1\nnode 2 " + type + "\n";
    for (const auto& d : defaults) g += "default 2 " + std::to_string(d.first) + " " + d.second + "\n";
    g += "node 3 SetVar r\nexec 1 0 2\nexec 2 0 3\ndata 3 0 2 0\n";
    return g;
}

std::vector<std::pair<int, std::string>> Pins(const std::vector<std::string>& values) {
    std::vector<std::pair<int, std::string>> out;
    for (std::size_t i = 0; i < values.size(); ++i) out.push_back({(int)i, values[i]});
    return out;
}

vs::VsValue RunCall(const std::string& type, const std::vector<std::pair<int, std::string>>& defaults) {
    Scene s("FnArity");
    s.physicsEnabled = false;
    VisualScriptComponent* vc = RunGraph(s, CallGraph(type, defaults));
    return vc ? vc->GetVariable("r") : vs::VsValue();
}

bool HasPinWarning(const std::vector<vs::VsIssue>& issues, int node, int pin) {
    for (const auto& is : issues)
        if (is.node == node && is.pin == pin && is.severity == 1 && is.message.find("input pin") != std::string::npos) return true;
    return false;
}

// ---- A small reader for OkayScriptVM.cpp -------------------------------------------------------------------------
// Written for this test from the shape of the VM source: each builtin is `b["name"] = <lambda over
// std::vector<Value>& a>;` and reads its arguments as a[K], a.size() <op> K, or by passing `a` to a helper together
// with an index (N(a, 4), maskArg(a, 7), finish(a, t, 3, 4), lookup(a, b + 2)). Helpers declared as
// `auto name = [..](std::vector<Value>& a, ...)` count with the highest index they read themselves.
// It finds the highest index each builtin reads; arity = that + 1. Helpers that compute the index at run time
// are listed by hand in kHidden below.

// Comments dropped; inside string and char literals the brackets, ';' and ',' are blanked so they do not count.
std::string CleanSource(const std::string& t) {
    std::string o;
    o.reserve(t.size());
    std::size_t i = 0, n = t.size();
    while (i < n) {
        const char c = t[i];
        if (c == '/' && i + 1 < n && t[i + 1] == '/') { while (i < n && t[i] != '\n') ++i; continue; }
        if (c == '/' && i + 1 < n && t[i + 1] == '*') { const auto e = t.find("*/", i + 2); i = e == std::string::npos ? n : e + 2; continue; }
        if (c == '"' || c == '\'') {
            o += c;
            std::size_t j = i + 1;
            while (j < n && t[j] != c) {
                if (t[j] == '\\') { o += "__"; j += 2; continue; }
                const char x = t[j];
                o += (x == '(' || x == ')' || x == '[' || x == ']' || x == '{' || x == '}' || x == ';' || x == ',') ? '_' : x;
                ++j;
            }
            o += c;
            i = j + 1;
            continue;
        }
        o += c;
        ++i;
    }
    return o;
}

bool IsWord(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

// From `i` to the ';' that ends the statement (or the bracket that closes the enclosing one).
std::string Statement(const std::string& s, std::size_t i) {
    int depth = 0;
    std::size_t j = i;
    for (; j < s.size(); ++j) {
        const char c = s[j];
        if (c == '(' || c == '[' || c == '{') ++depth;
        else if (c == ')' || c == ']' || c == '}') { if (--depth < 0) break; }
        else if (c == ';' && depth == 0) break;
    }
    return s.substr(i, j - i);
}

std::string Trim(const std::string& x) {
    std::size_t a = 0, b = x.size();
    while (a < b && std::isspace((unsigned char)x[a])) ++a;
    while (b > a && std::isspace((unsigned char)x[b - 1])) --b;
    return x.substr(a, b - a);
}

// "7" -> 7; "b + 7" (an offset of 0 or 1 plus 7) -> 8; anything else -> -1.
int IndexLiteral(const std::string& raw) {
    const std::string x = Trim(raw);
    if (x.empty()) return -1;
    if (std::all_of(x.begin(), x.end(), [](char c) { return std::isdigit((unsigned char)c) != 0; }))
        return x.size() <= 3 ? std::stoi(x) : -1;
    const auto plus = x.find('+');
    if (plus == std::string::npos) return -1;
    const std::string l = Trim(x.substr(0, plus)), r = Trim(x.substr(plus + 1));
    if (l.empty() || !(std::isalpha((unsigned char)l[0]) || l[0] == '_')) return -1;
    if (!std::all_of(l.begin(), l.end(), IsWord)) return -1;
    if (r.empty() || r.size() > 3 || !std::all_of(r.begin(), r.end(), [](char c) { return std::isdigit((unsigned char)c) != 0; })) return -1;
    return std::stoi(r) + 1;
}

// The top-level comma-separated arguments of the call whose '(' is at `open`.
std::vector<std::string> CallArgs(const std::string& s, std::size_t open) {
    std::vector<std::string> args;
    std::string cur;
    int depth = 0;
    for (std::size_t i = open + 1; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '(' || c == '[' || c == '{') ++depth;
        else if (c == ')' || c == ']' || c == '}') { if (depth == 0) break; --depth; }
        if (c == ',' && depth == 0) { args.push_back(cur); cur.clear(); }
        else cur += c;
    }
    args.push_back(cur);
    return args;
}

// Highest argument index `body` reads through the parameter `p`, or -1.
int MaxIndex(const std::string& body, const std::string& p, const std::map<std::string, int>& helpers) {
    int best = -1;
    auto skipWs = [&](std::size_t k) { while (k < body.size() && std::isspace((unsigned char)body[k])) ++k; return k; };
    for (std::size_t at = body.find(p); at != std::string::npos; at = body.find(p, at + 1)) {
        if (at > 0 && (IsWord(body[at - 1]) || body[at - 1] == '.')) continue;
        std::size_t k = at + p.size();
        if (k < body.size() && IsWord(body[k])) continue;
        k = skipWs(k);
        if (k < body.size() && body[k] == '[') {
            const auto close = body.find(']', k);
            if (close != std::string::npos) best = std::max(best, IndexLiteral(body.substr(k + 1, close - k - 1)));
        } else if (body.compare(k, 7, ".size()") == 0) {
            k = skipWs(k + 7);
            int adjust = 0;
            std::size_t opLen = 0;
            if (body.compare(k, 2, ">=") == 0) { adjust = -1; opLen = 2; }
            else if (body.compare(k, 2, "<=") == 0) { adjust = 0; opLen = 2; }
            else if (body.compare(k, 2, "==") == 0 || body.compare(k, 2, "!=") == 0) { adjust = -1; opLen = 2; }
            else if (body.compare(k, 1, ">") == 0) { adjust = 0; opLen = 1; }
            else if (body.compare(k, 1, "<") == 0) { adjust = -1; opLen = 1; }
            if (!opLen) continue;
            k = skipWs(k + opLen);
            std::size_t e = k;   // the right side: a number, or `name + number`
            while (e < body.size() && (IsWord(body[e]) || body[e] == ' ' || body[e] == '+')) ++e;
            if (e < body.size() && body[e] == '.') continue;   // a float, not a count
            const int v = IndexLiteral(body.substr(k, e - k));
            if (v >= 0) best = std::max(best, v + adjust);
        }
    }
    // Calls that take `p` as one argument: their index arguments, and the helper's own reads.
    for (std::size_t i = 0; i < body.size(); ++i) {
        if (body[i] != '(' || i == 0) continue;
        std::size_t e = i;
        while (e > 0 && std::isspace((unsigned char)body[e - 1])) --e;
        std::size_t b = e;
        while (b > 0 && IsWord(body[b - 1])) --b;
        if (b == e) continue;
        const std::string name = body.substr(b, e - b);
        const std::vector<std::string> args = CallArgs(body, i);
        bool takesP = false;
        for (const auto& a : args) if (Trim(a) == p) takesP = true;
        if (!takesP) continue;
        for (const auto& a : args) { const int v = IndexLiteral(a); if (v >= 0 && v <= 15) best = std::max(best, v); }
        const auto h = helpers.find(name);
        if (h != helpers.end()) best = std::max(best, h->second);
    }
    return best;
}

// `std::vector<Value>& name` (optionally const) at or after `from`; fills `param` and returns the end, or npos.
std::size_t FindValueParam(const std::string& s, std::size_t from, std::size_t limit, std::string& param) {
    const std::string key = "std::vector<Value>";
    for (std::size_t at = s.find(key, from); at != std::string::npos && at < limit; at = s.find(key, at + 1)) {
        std::size_t k = at + key.size();
        while (k < s.size() && std::isspace((unsigned char)s[k])) ++k;
        if (k >= s.size() || s[k] != '&') continue;
        ++k;
        while (k < s.size() && std::isspace((unsigned char)s[k])) ++k;
        std::size_t e = k;
        while (e < s.size() && IsWord(s[e])) ++e;
        if (e == k) continue;
        param = s.substr(k, e - k);
        return e;
    }
    return std::string::npos;
}

// builtin -> highest index read + 1 (0 when it reads none).
std::map<std::string, int> ScanArities(const std::string& raw, int& registrations) {
    const std::string src = CleanSource(raw);
    std::map<std::string, int> helpers;
    for (std::size_t at = src.find("auto "); at != std::string::npos; at = src.find("auto ", at + 1)) {
        if (at > 0 && IsWord(src[at - 1])) continue;
        std::size_t k = at + 5, e;
        while (k < src.size() && src[k] == ' ') ++k;
        e = k;
        while (e < src.size() && IsWord(src[e])) ++e;
        if (e == k) continue;
        const std::string name = src.substr(k, e - k);
        k = e;
        while (k < src.size() && src[k] == ' ') ++k;
        if (k >= src.size() || src[k] != '=') continue;
        ++k;
        while (k < src.size() && std::isspace((unsigned char)src[k])) ++k;
        if (k >= src.size() || src[k] != '[') continue;
        const std::size_t capEnd = src.find(']', k);
        if (capEnd == std::string::npos) continue;
        std::size_t q = capEnd + 1;
        while (q < src.size() && std::isspace((unsigned char)src[q])) ++q;
        if (q >= src.size() || src[q] != '(') continue;
        // the first parameter must be the argument vector
        std::size_t r = q + 1;
        while (r < src.size() && std::isspace((unsigned char)src[r])) ++r;
        if (src.compare(r, 6, "const ") == 0) r += 6;
        if (src.compare(r, 18, "std::vector<Value>") != 0) continue;
        std::string p;
        if (FindValueParam(src, r, r + 1, p) == std::string::npos) continue;
        helpers[name] = MaxIndex(Statement(src, k), p, {});
    }
    std::map<std::string, int> arity;
    std::map<std::string, std::string> aliasOf;
    registrations = 0;
    for (std::size_t at = src.find("b[\""); at != std::string::npos; at = src.find("b[\"", at + 1)) {
        if (at > 0 && IsWord(src[at - 1])) continue;
        const std::size_t nameEnd = src.find("\"]", at + 3);
        if (nameEnd == std::string::npos) continue;
        const std::string name = src.substr(at + 3, nameEnd - at - 3);
        std::size_t k = nameEnd + 2;
        while (k < src.size() && std::isspace((unsigned char)src[k])) ++k;
        if (k + 1 >= src.size() || src[k] != '=' || src[k + 1] == '=') continue;
        ++k;
        while (k < src.size() && std::isspace((unsigned char)src[k])) ++k;
        const std::string rhs = Statement(src, k);
        ++registrations;
        if (Trim(rhs).rfind("b[\"", 0) == 0 && Trim(rhs).size() > 5 && Trim(rhs).back() == ']') {   // b["x"] = b["y"]
            const std::string t = Trim(rhs);
            aliasOf[name] = t.substr(3, t.size() - 5);
            continue;
        }
        int best = -1;
        std::string p;
        for (std::size_t from = 0; (from = FindValueParam(rhs, from, rhs.size(), p)) != std::string::npos;)
            best = std::max(best, MaxIndex(rhs.substr(from), p, helpers));
        arity[name] = best + 1;
        aliasOf.erase(name);
    }
    for (std::size_t at = src.find("alias(\""); at != std::string::npos; at = src.find("alias(\"", at + 1)) {
        if (at > 0 && IsWord(src[at - 1])) continue;
        const auto args = CallArgs(src, at + 5);
        if (args.size() != 2) continue;
        const std::string from = Trim(args[0]), to = Trim(args[1]);
        if (from.size() < 2 || to.size() < 2) continue;
        aliasOf[from.substr(1, from.size() - 2)] = to.substr(1, to.size() - 2);
    }
    for (const auto& kv : aliasOf) {
        std::string t = kv.second;
        for (int hop = 0; hop < 6 && aliasOf.count(t) && !arity.count(t); ++hop) t = aliasOf[t];
        auto it = arity.find(t);
        if (it != arity.end()) arity[kv.first] = it->second;
    }
    return arity;
}

// Builtins whose reads the scanner cannot see, checked by hand in OkayScriptVM.cpp (2026-11-15):
const std::pair<const char*, int> kHidden[] = {
    {"nav_path_exists", 6},   // navRoute(a, path) reads a[0..5] when six numbers are given
    {"nav_path_length", 6},
    {"ik_hand", 5},           // setGoal(a, hand): side, x, y, z, weight
    {"ik_foot", 5},
    {"tween_jump", 6},        // the done-function index is 4 or 5, computed from a[4]
    {"fog_set", 6},           // enabled, optional "#rrggbb", start, end, mode, density (a running index)
    {"ui_dialogue_show", 6},  // speaker, line, then up to four choices (a loop)
    {"capsule_cast", 11},     // [what,] ax ay bx by r dx dy dist layers ignore: trigArg(a, b + 9) with b = 1
};

} // namespace

int main() {
    RUN_SUITE("vs_fn_arity");
    vs::SetRuntimeErrorHook(&OnErr);
    Time::SetTimeScale(1);

    // ---- dist3 and remap: the graph gives what the text script gives ---------------------------------------------
    {
        OkayScriptVM vm;
        const float text = vm.CallBuiltin("dist3", {vs::VsValue(0.0f), vs::VsValue(0.0f), vs::VsValue(0.0f),
                                                    vs::VsValue(3.0f), vs::VsValue(4.0f), vs::VsValue(0.0f)}).AsFloat();
        CHECK_NEAR(text, 5.0f, 1e-4f);
        const float graph = RunCall("Fn.dist3", Pins({"0", "0", "0", "3", "4", "0"})).AsFloat();
        std::printf("  Fn.dist3 0,0,0,3,4,0 -> %g (text script %g)\n", graph, text);
        CHECK_NEAR(graph, 5.0f, 1e-4f);
    }
    {
        OkayScriptVM vm;
        const float text = vm.CallBuiltin("remap", {vs::VsValue(0.5f), vs::VsValue(0.0f), vs::VsValue(1.0f),
                                                    vs::VsValue(0.0f), vs::VsValue(100.0f)}).AsFloat();
        CHECK_NEAR(text, 50.0f, 1e-3f);
        const float graph = RunCall("Fn.remap", Pins({"0.5", "0", "1", "0", "100"})).AsFloat();
        std::printf("  Fn.remap 0.5,0,1,0,100 -> %g (text script %g)\n", graph, text);
        CHECK_NEAR(graph, 50.0f, 1e-3f);
    }

    // ---- raycast3 from above a box, pointing down, hits it ------------------------------------------------------
    {
        Scene s("FnRay");
        s.physicsEnabled = false;
        GameObject* box = s.CreateGameObject("Crate");
        box->transform->localPosition = {0, 0, 0};
        box->AddComponent<BoxCollider3D>()->size = {2, 2, 2};
        VisualScriptComponent* vc = RunGraph(s, CallGraph("Fn.raycast3", Pins({"0", "10", "0", "0", "-1", "0"})));
        const std::string hit = vc ? vc->GetVariable("r").AsString() : std::string("(no graph)");
        std::printf("  Fn.raycast3 down from y=10 -> \"%s\"\n", hit.c_str());
        CHECK(hit == "Crate");
        // with the optional max distance (pin 6) too short, it misses
        Scene s2("FnRayShort");
        s2.physicsEnabled = false;
        s2.CreateGameObject("Crate")->AddComponent<BoxCollider3D>()->size = {2, 2, 2};
        VisualScriptComponent* vc2 = RunGraph(s2, CallGraph("Fn.raycast3", Pins({"0", "10", "0", "0", "-1", "0", "3"})));
        CHECK(vc2 && vc2->GetVariable("r").AsString().empty());
    }

    // ---- Old four-pin graphs keep their meaning -----------------------------------------------------------------
    {
        // remap with four typed pins: outHi stays its default 1, as before.
        CHECK_NEAR(RunCall("Fn.remap", Pins({"0.5", "0", "1", "0"})).AsFloat(), 0.5f, 1e-4f);
        CHECK_NEAR(RunCall("Fn.clamp", Pins({"5", "0", "3"})).AsFloat(), 3.0f, 1e-4f);
        // Unfed pins after the last fed one are not passed: array() of two typed pins has two items.
        vs::VsValue arr = RunCall("Fn.array", Pins({"1", "2"}));
        CHECK(arr.IsArray() && arr.AsArray() && arr.AsArray()->size() == 2);
        // A gap keeps the pin's place: pins 0 and 2 typed -> three arguments, the middle one empty.
        arr = RunCall("Fn.array", {{0, "1"}, {2, "3"}});
        CHECK(arr.IsArray() && arr.AsArray() && arr.AsArray()->size() == 3);
        if (arr.IsArray() && arr.AsArray() && arr.AsArray()->size() == 3) CHECK_NEAR((*arr.AsArray())[2].AsFloat(), 3.0f, 1e-4f);
        // A saved graph (the playground's math row) loads and runs without errors.
        Scene s("Old");
        s.physicsEnabled = false;
        VisualScriptComponent* vc = RunGraph(s,
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 Fn.lerp\ndefault 2 0 0\ndefault 2 1 10\ndefault 2 2 0.25\n"
            "node 3 SetVar r\nexec 1 0 2\nexec 2 0 3\ndata 3 0 2 0\n");
        CHECK(vc && g_errs.empty());
        if (vc) CHECK_NEAR(vc->GetVariable("r").AsFloat(), 2.5f, 1e-4f);
    }

    // ---- The shipped example the owner opens (docs/try_it.md, section 15) ---------------------------------------
    {
        Scene scene;
        std::string err;
        const std::string path = std::string(OKAY_SOURCE_DIR) + "/examples/node_playground/fn_args.okayscene";
        CHECK(SceneSerializer::LoadFromFile(scene, path, &err));
        if (!err.empty()) std::printf("  load: %s\n", err.c_str());
        g_errs.clear();
        scene.Start();
        for (int i = 0; i < 3; ++i) scene.Update(1.0f / 60.0f);
        GameObject* readout = scene.Find("Readout");
        auto* text = readout ? readout->GetComponent<TextRenderer>() : nullptr;
        const std::string shown = text ? text->text : std::string("(no Readout text)");
        std::printf("  fn_args.okayscene shows \"%s\"\n", shown.c_str());
        CHECK(shown == "dist3 = 5   remap = 50");
        CHECK(g_errs.empty());
        auto* vc = readout ? readout->GetComponent<VisualScriptComponent>() : nullptr;
        CHECK(vc != nullptr);
        if (vc) {
            int pinIssues = 0;
            for (const auto& is : vs::ValidateGraphText(vc->Source())) {
                std::printf("  example issue: node %d: %s\n", is.node, is.message.c_str());
                ++pinIssues;
            }
            CHECK(pinIssues == 0);
        }
    }

    // ---- The validator warns about pins a node does not have -----------------------------------------------------
    {
        const std::string bad =
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 Fn.dist3\ndefault 2 0 0\ndefault 2 6 9\n"     // dist3 has pins 0..5
            "node 3 Fn.print\ndefault 3 4 100\n"                   // print has pins 0..3
            "node 4 Const 7\n"
            "node 5 Fn.remap\ndata 5 9 4 0\n"                      // remap has pins 0..4
            "exec 1 0 2\nexec 2 0 3\nexec 3 0 5\n";
        const auto issues = vs::ValidateGraphText(bad);
        for (const auto& is : issues) std::printf("  issue: node %d pin %d: %s\n", is.node, is.pin, is.message.c_str());
        CHECK(HasPinWarning(issues, 2, 6));
        CHECK(HasPinWarning(issues, 3, 4));
        CHECK(HasPinWarning(issues, 5, 9));
        const std::string good =
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 Fn.dist3\ndefault 2 0 0\ndefault 2 5 9\n"
            "node 3 Fn.remap\ndefault 3 4 100\n"
            "exec 1 0 2\nexec 2 0 3\n";
        const auto ok = vs::ValidateGraphText(good);
        int pinIssues = 0;
        for (const auto& is : ok) if (is.message.find("input pin") != std::string::npos) ++pinIssues;
        CHECK(pinIssues == 0);
    }

    // ---- Every Fn.* node has at least as many inputs as its builtin reads ---------------------------------------
    {
        std::ifstream in(std::string(OKAY_SOURCE_DIR) + "/engine/src/Scripting/OkayScriptVM.cpp");
        CHECK(in.good());
        std::stringstream ss;
        ss << in.rdbuf();
        int registrations = 0;
        std::map<std::string, int> arity = ScanArities(ss.str(), registrations);
        std::printf("  scanned %d builtin registrations\n", registrations);
        CHECK(registrations > 900);
        // The scanner itself: known arities from the code.
        CHECK(arity["dist3"] == 6);
        CHECK(arity["remap"] == 5);
        CHECK(arity["raycast3"] == 9);
        CHECK(arity["capsule_cast3"] == 13);
        CHECK(arity["clamp"] == 3);
        CHECK(arity["Physics.Raycast"] == 9);   // an alias of raycast3
        for (const auto& h : kHidden) arity[h.first] = std::max(arity[h.first], h.second);

        int checked = 0, tooFew = 0, wide = 0, maxPins = 0;
        for (const auto& n : vs::BuiltinNodes()) {
            const vs::NodeSignature* sig = vs::SignatureFor(n.type);
            CHECK(sig != nullptr);
            if (!sig) continue;
            ++checked;
            const int pins = (int)sig->ins.size();
            maxPins = std::max(maxPins, pins);
            if (pins > 4) ++wide;
            CHECK(pins >= 4);   // old graphs' pins 0..3 keep their meaning
            const auto it = arity.find(n.builtin);
            const int need = it == arity.end() ? 0 : it->second;
            if (pins < need) {
                ++tooFew;
                std::printf("  %s has %d inputs but its builtin reads %d arguments\n", n.type.c_str(), pins, need);
            }
            CHECK(sig->outs == "A" && sig->execIn);
        }
        std::printf("  %d Fn nodes checked, %d with more than four inputs, most inputs %d, %d too few\n", checked, wide, maxPins, tooFew);
        CHECK(checked > 900);
        CHECK(tooFew == 0);
        CHECK(maxPins == vs::kBuiltinMaxPins);   // capsule_cast3: 13
        CHECK(vs::BuiltinPinCount("print") == 4 && vs::BuiltinPinCount("dist3") == 6);
        CHECK(std::string(vs::BuiltinPinName(5)) == "Sixth" && std::string(vs::BuiltinPinName(12)) == "Thirteenth");
    }

    TEST_MAIN_RESULT();
}
