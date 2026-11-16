// Builtins registered twice in OkayScriptVM.cpp (issue #888, findings 25 and 26).
//
// RegisterBuiltins() fills one name -> function table, so a second `b["name"] = ...`
// or `alias("name", ...)` silently replaces the first, and the later copy is the one
// scripts get. That is how `save_exists` (a file check) turned into the save-slot
// check, and how a `lerp_angle` that goes the long way round below -540 degrees
// replaced the correct one.
//
// This test:
//   1. checks the behaviour that was chosen for every builtin that had two copies;
//   2. scans the registration code and fails when any builtin name is registered
//      twice (the guard), after proving the scan sees every registered name.
//
// Written from the engine's own source and docs (docs/scripting.md, scripting_reference.md).
#include "test_framework.hpp"
#include "test_sandbox.hpp"
#include <Okay.hpp>
#include "okay/VisualScript/VsBuiltins.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace okay;
namespace fs = std::filesystem;

namespace {

// Run a script's top-level statements and read one global back.
vs::VsValue Run(const std::string& src, const char* name) {
    auto vm = CreateScriptVM("okayscript");
    std::string err;
    if (!vm->Load(src, &err)) {
        std::cerr << "  script did not load: " << err << "\n";
        return {};
    }
    return vm->GetGlobal(name);
}
float Num(const std::string& expr) {
    return Run("var r = " + expr + ";", "r").AsFloat();
}

// ---- The guard: every builtin name registered in RegisterBuiltins() --------
// A registration is `b["name"] = ...` (not `==`) or `alias("name", ...)`, outside
// comments, between the line that opens `void RegisterBuiltins()` and the first line
// after it that is exactly the 4-space closing brace of that function.
struct Registration { std::string name; int line; };

bool IsIdentChar(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

// Cut a `//` comment and `/* */` comments out of one line; `inBlock` carries a
// block comment across lines. String and character literals are kept as they are.
std::string StripComments(const std::string& line, bool& inBlock) {
    std::string out;
    char quote = 0;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        const char n = i + 1 < line.size() ? line[i + 1] : '\0';
        if (inBlock) {
            if (c == '*' && n == '/') { inBlock = false; ++i; }
            continue;
        }
        if (quote) {
            out += c;
            if (c == '\\' && n) { out += n; ++i; }
            else if (c == quote) quote = 0;
            continue;
        }
        if (c == '/' && n == '/') break;
        if (c == '/' && n == '*') { inBlock = true; ++i; continue; }
        if (c == '"' || c == '\'') quote = c;
        out += c;
    }
    return out;
}

// The name in `"name"` starting at text[pos] (pos is the opening quote), or "".
std::string QuotedAt(const std::string& text, std::size_t pos, std::size_t& end) {
    if (pos >= text.size() || text[pos] != '"') return {};
    const std::size_t close = text.find('"', pos + 1);
    if (close == std::string::npos) return {};
    end = close + 1;
    return text.substr(pos + 1, close - pos - 1);
}
std::size_t SkipSpaces(const std::string& text, std::size_t pos) {
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t')) ++pos;
    return pos;
}

std::vector<Registration> ScanRegistrations(const std::string& source, std::string* why) {
    std::vector<Registration> out;
    std::istringstream in(source);
    std::string line;
    int lineNo = 0;
    bool inside = false, inBlock = false, closed = false;
    while (std::getline(in, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!inside) {
            if (line.find("void RegisterBuiltins()") != std::string::npos) inside = true;
            continue;
        }
        if (line == "    }") { closed = true; break; }
        const std::string code = StripComments(line, inBlock);
        for (std::size_t p = 0; p < code.size(); ++p) {
            const bool wordStart = p == 0 || !(IsIdentChar(code[p - 1]) || code[p - 1] == '.' || code[p - 1] == '>' || code[p - 1] == ':');
            if (!wordStart) continue;
            std::size_t end = 0;
            if (code.compare(p, 2, "b[") == 0) {                       // b["name"] = ...
                const std::string name = QuotedAt(code, SkipSpaces(code, p + 2), end);
                if (name.empty()) continue;
                std::size_t q = SkipSpaces(code, end);
                if (q >= code.size() || code[q] != ']') continue;
                q = SkipSpaces(code, q + 1);
                if (q < code.size() && code[q] == '=' && (q + 1 >= code.size() || code[q + 1] != '='))
                    out.push_back({name, lineNo});
            } else if (code.compare(p, 5, "alias") == 0 && (p + 5 >= code.size() || !IsIdentChar(code[p + 5]))) {
                std::size_t q = SkipSpaces(code, p + 5);                 // alias("name", "target")
                if (q >= code.size() || code[q] != '(') continue;
                const std::string name = QuotedAt(code, SkipSpaces(code, q + 1), end);
                if (!name.empty()) out.push_back({name, lineNo});
            }
        }
    }
    if (why) {
        if (!inside) *why = "no `void RegisterBuiltins()` found";
        else if (!closed) *why = "RegisterBuiltins() has no closing `    }` line";
        else why->clear();
    }
    return out;
}

std::map<std::string, std::vector<int>> Repeats(const std::vector<Registration>& regs) {
    std::map<std::string, std::vector<int>> lines, out;
    for (const auto& r : regs) lines[r.name].push_back(r.line);
    for (auto& kv : lines) if (kv.second.size() > 1) out[kv.first] = kv.second;
    return out;
}

std::string ReadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

} // namespace

int main() {
    RUN_SUITE("okayscript_dupes");

    // A private temp folder: the current folder and the save folder while the test runs.
    okaytest::Sandbox sandbox("okayscript_dupes", OKAY_SOURCE_DIR);
    const fs::path dir = sandbox.Dir();

    // ---- save_exists(file) is the FILE check (finding 25) ----
    // It pairs with save / load / save_has / save_delete_file, which all take a file.
    // The slot check is has_save(slot).
    {
        const char* src = R"SCRIPT(
            var wrote = save("coins", 5, "slot1.okaysave");
            var there = save_exists("slot1.okaysave");     // 1: the file was just written
            var missing = save_exists("missing.okaysave"); // 0: no such file
            var coins = load("coins", 0, "slot1.okaysave");
            save("k", 1);
            var dflt = save_exists();                      // 1: the default save.okaysave
            var outside = save_exists("../slot1.okaysave"); // 0: outside the save folder
        )SCRIPT";
        auto vm = CreateScriptVM("okayscript");
        std::string err;
        CHECK(vm->Load(src, &err));
        CHECK(vm->GetGlobal("wrote").AsBool());
        CHECK(fs::exists(dir / "slot1.okaysave"));
        CHECK_NEAR(vm->GetGlobal("there").AsFloat(), 1.0f, 1e-6f);
        CHECK(vm->GetGlobal("there").AsString() == "1");           // prints 1, as before
        CHECK_NEAR(vm->GetGlobal("missing").AsFloat(), 0.0f, 1e-6f);
        CHECK_NEAR(vm->GetGlobal("coins").AsFloat(), 5.0f, 1e-6f);
        CHECK_NEAR(vm->GetGlobal("dflt").AsFloat(), 1.0f, 1e-6f);
        CHECK_NEAR(vm->GetGlobal("outside").AsFloat(), 0.0f, 1e-6f);
    }
    // The documented "first time playing" pattern: false before the first save, true after.
    {
        const char* src = R"SCRIPT(
            var first = 0;
            if (!save_exists("progress.okaysave")) { first = 1; save("coins", 0, "progress.okaysave"); }
            var again = 0;
            if (!save_exists("progress.okaysave")) { again = 1; }
        )SCRIPT";
        auto vm = CreateScriptVM("okayscript");
        std::string err;
        CHECK(vm->Load(src, &err));
        CHECK_NEAR(vm->GetGlobal("first").AsFloat(), 1.0f, 1e-6f);
        CHECK_NEAR(vm->GetGlobal("again").AsFloat(), 0.0f, 1e-6f);   // was 1 (progress wiped every run)
    }

    // ---- has_save(slot) is still the save-slot check ----
    {
        Scene s("Dupes");
        GameObject* g = s.CreateGameObject("Saver");
        auto* sc = g->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "function save_it() { return save_game(\"dupslot\") }\n"
            "function has_it() { return has_save(\"dupslot\") }\n"
            "function has_other() { return has_save(\"never-saved\") }\n"
            "function file_named_like_slot() { return save_exists(\"dupslot\") }\n"));
        s.Start();
        CHECK(sc->VM() != nullptr);
        if (sc->VM()) {
            CHECK_NEAR(sc->VM()->CallEventArgs("save_it", {}).AsFloat(), 1.0f, 1e-6f);
            CHECK_NEAR(sc->VM()->CallEventArgs("has_it", {}).AsFloat(), 1.0f, 1e-6f);
            CHECK_NEAR(sc->VM()->CallEventArgs("has_other", {}).AsFloat(), 0.0f, 1e-6f);
            // A slot is not a file called "dupslot": save_exists asks about files only.
            CHECK_NEAR(sc->VM()->CallEventArgs("file_named_like_slot", {}).AsFloat(), 0.0f, 1e-6f);
        }
        CHECK(SaveGame::Exists("dupslot"));
        SaveGame::Delete("dupslot");
    }

    // ---- lerp_angle takes the shortest way for every input (finding 26) ----
    // -600 degrees is the same direction as 120 (-600 + 720). The shortest turn from 0 to
    // 120 is +120, so halfway is +60. The broken copy gave -120.
    CHECK_NEAR(Num("lerp_angle(0, -600, 0.5)"), 60.0f, 1e-3f);
    CHECK_NEAR(Num("lerp_angle(0, -600, 1)"), 120.0f, 1e-3f);
    CHECK_NEAR(Num("lerp_angle(0, -720, 0.5)"), 0.0f, 1e-3f);    // same direction: no turn (was -180)
    CHECK_NEAR(Num("lerp_angle(0, -1000, 1)"), 80.0f, 1e-3f);    // -1000 + 1080 = 80 (was -280)
    CHECK_NEAR(Num("lerp_angle(10, 350, 1)"), -10.0f, 1e-3f);    // across 0, unchanged
    CHECK_NEAR(Num("lerp_angle(0, 90, 0.5)"), 45.0f, 1e-3f);
    CHECK_NEAR(Num("lerp_angle(350, 10, 0.5)"), 360.0f, 1e-3f);  // +10 from 350 (not back through 180)
    CHECK_NEAR(Num("Mathf.LerpAngle(0, -600, 0.5)"), 60.0f, 1e-3f);
    // Every whole-degree difference from -2000 to 2000: the step is at most half a turn
    // and lands on the target's direction.
    {
        std::string src = "var worst = 0; var off = 0;\n"
                          "for (var d = -2000; d <= 2000; d = d + 1) {\n"
                          "  var step = lerp_angle(0, d, 1);\n"
                          "  if (abs(step) > worst) { worst = abs(step); }\n"
                          "  var m = mod(step - d, 360);\n"
                          "  if (m > 0.01 && m < 359.99) { off = off + 1; }\n"
                          "}\n";
        auto vm = CreateScriptVM("okayscript");
        std::string err;
        CHECK(vm->Load(src, &err));
        CHECK(vm->GetGlobal("worst").AsFloat() <= 180.0f + 1e-3f);
        CHECK_NEAR(vm->GetGlobal("off").AsFloat(), 0.0f, 1e-6f);
    }

    // ---- format: the {0} {1} and {} version (the {}-only copy was dead) ----
    CHECK(Run(R"(var r = format("{0}/{1}", 3, 9);)", "r").AsString() == "3/9");
    CHECK(Run(R"(var r = format("hp={} of {}", 7, 9);)", "r").AsString() == "hp=7 of 9");
    CHECK(Run(R"(var r = string.Format("{1}-{0}", "a", "b");)", "r").AsString() == "b-a");

    // ---- inverse_lerp: the copy scripts already ran (fewer than 3 arguments -> 0) ----
    CHECK_NEAR(Num("inverse_lerp(0, 10, 5)"), 0.5f, 1e-5f);
    CHECK_NEAR(Num("inverse_lerp(0, 10, 20)"), 1.0f, 1e-5f);
    CHECK_NEAR(Num("inverse_lerp(-10, 10)"), 0.0f, 1e-5f);       // the dead copy gave 0.5
    CHECK_NEAR(Num("Mathf.InverseLerp(-10, 10)"), 0.0f, 1e-5f);

    // ---- math_repeat: the copy scripts ran, with a zero or negative length -> 0 ----
    CHECK_NEAR(Num("math_repeat(7, 3)"), 1.0f, 1e-5f);
    CHECK_NEAR(Num("math_repeat(-1, 3)"), 2.0f, 1e-5f);
    CHECK_NEAR(Num("math_repeat(7.5)"), 0.0f, 1e-5f);            // no length: 0, as before
    CHECK_NEAR(Num("math_repeat(-5, -3)"), 0.0f, 1e-5f);         // was -5 (outside the range)
    CHECK_NEAR(Num("math_repeat(5, -3)"), 0.0f, 1e-5f);          // was 2
    CHECK_NEAR(Num("Mathf.Repeat(-5, -3)"), 0.0f, 1e-5f);

    // ---- approximately: the copy scripts ran (one argument -> false) ----
    CHECK(Run("var r = approximately(1, 1.00001);", "r").AsBool());
    CHECK(!Run("var r = approximately(1, 1.1);", "r").AsBool());
    CHECK(!Run("var r = approximately(0.00001);", "r").AsBool());   // the dead copy said true

    // ---- delta_angle: the copy scripts ran (one argument -> 0) ----
    CHECK_NEAR(Num("delta_angle(10, 350)"), -20.0f, 1e-4f);
    CHECK_NEAR(Num("delta_angle(0, -600)"), 120.0f, 1e-4f);
    CHECK_NEAR(Num("delta_angle(90)"), 0.0f, 1e-4f);             // the dead copy gave -90
    CHECK_NEAR(Num("Mathf.DeltaAngle(350, 10)"), 20.0f, 1e-4f);

    // ---- clamp01: the two copies were the same ----
    CHECK_NEAR(Num("clamp01(1.5)"), 1.0f, 1e-6f);
    CHECK_NEAR(Num("clamp01(-2)"), 0.0f, 1e-6f);
    CHECK_NEAR(Num("Mathf.Clamp01(0.25)"), 0.25f, 1e-6f);

    // ---- vec3 / vec2: the builtin and the Vector3 / Vector2 alias were the same ----
    {
        const vs::VsValue v = Run("var r = vec3(1, 2, 3);", "r");
        CHECK(v.IsVec3() && v.AsVec3().x == 1.0f && v.AsVec3().y == 2.0f && v.AsVec3().z == 3.0f);
        const vs::VsValue w = Run("var r = vec2(4, 5);", "r");
        CHECK(w.IsVec3() && w.AsVec3().x == 4.0f && w.AsVec3().y == 5.0f && w.AsVec3().z == 0.0f);
    }

    // ---- log(...) prints (the documented alias); Mathf.Log is the natural log ----
    {
        std::vector<std::pair<Log::Level, std::string>> seen;
        auto old = Log::sink;
        Log::sink = [&seen](Log::Level lv, const std::string& msg) { seen.push_back({lv, msg}); };
        const vs::VsValue r = Run(R"(log("dupes-hello"); var r = log(5);)", "r");
        const vs::VsValue w = Run(R"(Debug.LogWarning("dupes-warn");)", "r");
        const vs::VsValue e = Run(R"(Debug.LogError("dupes-error");)", "r");
        Log::sink = old;
        CHECK_NEAR(r.AsFloat(), 0.0f, 1e-6f);    // print returns nothing (0); the natural log of 5 is 1.609
        bool printed = false, warned = false, errored = false;
        for (const auto& s : seen) {
            if (s.second.find("dupes-hello") != std::string::npos && s.first == Log::Level::Info) printed = true;
            if (s.second.find("dupes-warn") != std::string::npos && s.first == Log::Level::Warning) warned = true;
            if (s.second.find("dupes-error") != std::string::npos && s.first == Log::Level::Error) errored = true;
        }
        CHECK(printed);
        CHECK(warned);                           // Debug.LogWarning is a warning, not a plain print
        CHECK(errored);
        CHECK_NEAR(Num("Mathf.Log(1)"), 0.0f, 1e-5f);
        CHECK_NEAR(Num("Mathf.Log(2.7182818)"), 1.0f, 1e-4f);
    }

    // ---- face("Name") is look_at (2D turn about Z), as documented ----
    // ---- GetComponent("T") gives a component reference, not a bare true ----
    {
        Scene s("Face");
        GameObject* target = s.CreateGameObject("Target");
        target->transform->SetPosition({0.0f, 5.0f, 3.0f});
        GameObject* me = s.CreateGameObject("Turret");
        me->AddComponent<SpriteRenderer>();
        auto* sc = me->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "function dupes_face_target() { face(\"Target\") }\n"
            "function comp() { return GetComponent(\"SpriteRenderer\") }\n"
            "function nocomp() { return GetComponent(\"AudioSource\") }\n"));
        s.Start();
        if (sc->VM()) {
            sc->VM()->CallEventArgs("dupes_face_target", {});   // (a builtin name such as "turn" would win over a script function)
            const Quat want = Quat::Euler(0.0f, 0.0f, 90.0f);
            const Quat got = me->transform->localRotation;
            const float dot = want.x * got.x + want.y * got.y + want.z * got.z + want.w * got.w;
            CHECK_NEAR(std::fabs(dot), 1.0f, 1e-4f);
            CHECK(sc->VM()->CallEventArgs("comp", {}).AsString() == "@SpriteRenderer");
            CHECK(!sc->VM()->CallEventArgs("nocomp", {}).IsString());
        }
    }

    // ---- The script in docs/try_it.md, run twice as a Play button would ----
    {
        const char* tryIt =
            "void Start()\n"
            "{\n"
            "    if (!save_exists(\"slot1.okaysave\")) { print(\"first time playing\"); }\n"
            "    else { print(\"welcome back, coins =\", load(\"coins\", 0, \"slot1.okaysave\")); }\n"
            "    save(\"coins\", 5, \"slot1.okaysave\");\n"
            "    print(\"save_exists:\", save_exists(\"slot1.okaysave\"));\n"
            "    print(\"missing file:\", save_exists(\"missing.okaysave\"));\n"
            "    print(\"lerp_angle(0, -600, 0.5) =\", lerp_angle(0, -600, 0.5));\n"
            "    print(\"lerp_angle(0, -720, 0.5) =\", lerp_angle(0, -720, 0.5));\n"
            "    print(\"math_repeat(-5, -3) =\", math_repeat(-5, -3));\n"
            "}\n";
        Save::DeleteFile("slot1.okaysave");   // a fresh player: no file and no cached keys
        CHECK(!fs::exists(dir / "slot1.okaysave"));
        auto play = [&]() {
            std::vector<std::string> lines;
            auto old = Log::sink;
            Log::sink = [&lines](Log::Level, const std::string& msg) { lines.push_back(msg); };
            {
                Scene s("TryIt");
                auto* sc = s.CreateGameObject("Main Camera")->AddComponent<ScriptComponent>("okayscript");
                CHECK(sc->LoadSource(tryIt));
                s.Start();
            }
            Log::sink = old;
            std::string all;
            for (const auto& l : lines) all += l + "\n";
            return all;
        };
        auto has = [](const std::string& text, const char* what) {
            const bool ok = text.find(what) != std::string::npos;
            if (!ok) std::cerr << "  try-it output lacks \"" << what << "\":\n" << text;
            return ok;
        };
        const std::string first = play();
        CHECK(has(first, "first time playing"));
        CHECK(has(first, "save_exists: 1"));
        CHECK(has(first, "missing file: 0"));
        CHECK(has(first, "lerp_angle(0, -600, 0.5) = 60"));
        CHECK(has(first, "lerp_angle(0, -720, 0.5) = 0"));
        CHECK(has(first, "math_repeat(-5, -3) = 0"));
        const std::string second = play();
        CHECK(has(second, "welcome back, coins = 5"));
    }

    // ---- The guard: no builtin name is registered twice ----
    {
        // The scanner itself: it must see both kinds of registration and skip comments and `==`.
        const std::string sample =
            "    void RegisterBuiltins() {\n"
            "        auto& b = rt.builtins;\n"
            "        b[\"one\"] = f;  // b[\"two\"] = g;\n"
            "        /* b[\"three\"] = g; */ alias(\"four\", \"one\");\n"
            "        if (b[\"one\"] == x) {}\n"
            "        b[\"one\"] = g;\n"
            "        alias(\"four\", \"one\");\n"
            "        auto alias = [&b](const char* from, const char* to) {};\n"
            "    }\n"
            "        b[\"after\"] = f;\n";
        std::string why;
        const auto regs = ScanRegistrations(sample, &why);
        CHECK(why.empty());
        CHECK(regs.size() == 4);
        const auto rep = Repeats(regs);
        CHECK(rep.size() == 2 && rep.count("one") && rep.count("four"));
        CHECK(rep.count("one") && rep.at("one") == (std::vector<int>{3, 6}));
    }
    {
        const std::string path = std::string(OKAY_SOURCE_DIR) + "/engine/src/Scripting/OkayScriptVM.cpp";
        const std::string src = ReadFile(path);
        CHECK(!src.empty());
        std::string why;
        const auto regs = ScanRegistrations(src, &why);
        if (!why.empty()) std::cerr << "  scan of " << path << ": " << why << "\n";
        CHECK(why.empty());
        CHECK(regs.size() > 1000);   // the scan really read the table (about 1,400 names)

        const auto rep = Repeats(regs);
        for (const auto& kv : rep) {
            std::cerr << "  builtin \"" << kv.first << "\" is registered " << kv.second.size()
                      << " times in OkayScriptVM.cpp, at lines";
            for (int l : kv.second) std::cerr << ' ' << l;
            std::cerr << " (the last one wins; keep one)\n";
        }
        CHECK(rep.empty());

        // The scan must see every name the VM really has, and nothing more; otherwise a
        // name registered some other way (a computed name, a helper) could repeat unseen,
        // or an alias points at a builtin that does not exist and silently does nothing.
        std::set<std::string> scanned;
        for (const auto& r : regs) scanned.insert(r.name);
        OkayScriptVM vm;
        const std::vector<std::string> live = vm.BuiltinNames();
        const std::set<std::string> liveSet(live.begin(), live.end());
        int shown = 0;
        for (const auto& n : liveSet)
            if (!scanned.count(n) && shown++ < 20)
                std::cerr << "  builtin \"" << n << "\" exists but the scan did not see it registered\n";
        for (const auto& n : scanned)
            if (!liveSet.count(n) && shown++ < 20)
                std::cerr << "  \"" << n << "\" is registered in the source but the VM does not have it (alias to a missing builtin?)\n";
        CHECK(scanned == liveSet);
        CHECK(live.size() == liveSet.size());
    }
    // The visual-script node list (Fn.<builtin>) lists each builtin once.
    {
        const auto& nodes = vs::BuiltinNodes();
        std::set<std::string> types, builtins;
        for (const auto& n : nodes) { types.insert(n.type); builtins.insert(n.builtin); }
        CHECK(!nodes.empty());
        CHECK(types.size() == nodes.size());
        CHECK(builtins.size() == nodes.size());
        CHECK(types.count("Fn.save_exists") == 1 && types.count("Fn.has_save") == 1 && types.count("Fn.lerp_angle") == 1);
    }

    CHECK(sandbox.Verify());
    TEST_MAIN_RESULT();
}
