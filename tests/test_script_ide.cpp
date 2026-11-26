#include "test_framework.hpp"
#include "ScriptIde.hpp"
#include "PluginRegistry.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// The Script Editor's workspace model (editor/ScriptIde.hpp) and the plugin registry (editor/PluginRegistry.hpp), headless.
namespace fs = std::filesystem;
using namespace scriptide;

static void Write(const fs::path& p, const std::string& t) { fs::create_directories(p.parent_path()); std::ofstream(p, std::ios::binary) << t; }
static bool HasFix(const Diagnostic& d, const std::string& title) { for (const auto& f : d.fixes) if (f.title == title) return true; return false; }

static const char* kOkayA =
    "// Player\n"
    "public float speed = 5;\n"
    "void Update()\n"
    "{\n"
    "    Fire(speed);\n"
    "    // Fire in a comment\n"
    "    Debug.Log(\"Fire\");\n"
    "}\n";
static const char* kOkayB =
    "void Fire(float power)\n"
    "{\n"
    "    Debug.Log(power);\n"
    "}\n";
static const char* kLua =
    "local Script = {\n"
    "    speed = 1,\n"
    "}\n"
    "\n"
    "local function helper(x)\n"
    "    return x * 2 -- helper in a comment\n"
    "end\n"
    "\n"
    "function Script:Update(dt)\n"
    "    local v = helper(dt)\n"
    "    print(\"helper\", v)\n"
    "end\n"
    "\n"
    "return Script\n";

int main() {
    RUN_SUITE("script_ide");

    // ---- fuzzy + Quick Open ranking ----
    {
        CHECK(FuzzyScore("plc", "PlayerController.okay") > 0);
        CHECK(FuzzyScore("xyz", "PlayerController.okay") < 0);
        CHECK(FuzzyScore("pc", "PlayerController.okay") > FuzzyScore("pc", "SpaceCraft.okay"));   // word starts score more
        const std::vector<std::string> files = {"/p/Assets/Scripts/Enemy.okay", "/p/Assets/Scripts/Player.okay",
                                                "/p/Assets/Scripts/PlayerCamera.lua", "/p/Assets/UI/Pause.okay"};
        // Empty query: recent first (in order), then the rest alphabetically.
        auto r = RankQuickOpen("", files, {"/p/Assets/UI/Pause.okay", "/p/Assets/Scripts/Player.okay"}, "/p");
        CHECK(r.size() == 4);
        CHECK(r[0].path == "/p/Assets/UI/Pause.okay" && r[0].recent);
        CHECK(r[1].path == "/p/Assets/Scripts/Player.okay");
        CHECK(r[2].label == "Assets/Scripts/Enemy.okay");
        // A query: name matches; a recent file wins over an equal match.
        r = RankQuickOpen("play", files, {"/p/Assets/Scripts/PlayerCamera.lua"}, "/p");
        CHECK(r.size() == 2);
        CHECK(r[0].path == "/p/Assets/Scripts/PlayerCamera.lua");
        r = RankQuickOpen("play", files, {}, "/p");
        CHECK(r.size() == 2 && r[0].path == "/p/Assets/Scripts/Player.okay");   // shorter name: the tighter match
        r = RankQuickOpen("ui/pa", files, {}, "/p");                              // by path
        CHECK(!r.empty() && r[0].path == "/p/Assets/UI/Pause.okay");
        const auto q = ParseQuickOpenQuery("Player:12");
        CHECK(q.text == "Player" && q.line == 12);
        CHECK(ParseQuickOpenQuery("a:b").line == 0);
        // Recent files list
        RecentFiles rf(3);
        rf.Push("a"); rf.Push("b"); rf.Push("c"); rf.Push("a"); rf.Push("d");
        CHECK(rf.Items().size() == 3 && rf.Items()[0] == "d" && rf.Items()[1] == "a" && rf.Items()[2] == "c");
        RecentFiles rf2(3); rf2.Parse(rf.Serialize());
        CHECK(rf2.Items() == rf.Items());
    }

    // ---- project scripts and the file tree ----
    const fs::path root = fs::temp_directory_path() / ("okay_script_ide_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    {
        Write(root / "Assets/Scripts/Player.okay", kOkayA);
        Write(root / "Assets/Scripts/Weapons/Gun.okay", kOkayB);
        Write(root / "Assets/Scripts/Camera.lua", kLua);
        Write(root / "Assets/readme.txt", "not a script");
        Write(root / ".okay/history/x.okay", "hidden");
        const auto files = ListProjectScripts(root.generic_string());
        CHECK(files.size() == 3);
        const auto tree = BuildFileTree(files, root.generic_string(), "");
        // Assets/ Scripts/ Weapons/ Gun.okay, Camera.lua, Player.okay
        CHECK(tree.size() == 6);
        CHECK(tree[0].folder && tree[0].name == "Assets" && tree[0].depth == 0);
        CHECK(tree[2].folder && tree[2].name == "Weapons" && tree[2].depth == 2);
        CHECK(!tree[3].folder && tree[3].name == "Gun.okay" && tree[3].depth == 3);
        CHECK(tree[4].name == "Camera.lua" && tree[5].name == "Player.okay");
        const auto filtered = BuildFileTree(files, root.generic_string(), "gun");
        CHECK(filtered.size() == 4 && filtered.back().name == "Gun.okay");

        // The symbol index, built on the worker thread.
        SymbolIndexBuilder b;
        CHECK(b.Start(files));
        b.Wait();
        SymbolIndex idx;
        CHECK(b.Poll(idx));
        CHECK(!b.Poll(idx));            // once per build
        CHECK(idx.FileCount() == 3);
        const std::string gun = (root / "Assets/Scripts/Weapons/Gun.okay").generic_string();
        const std::string player = (root / "Assets/Scripts/Player.okay").generic_string();
        const std::string cam = (root / "Assets/Scripts/Camera.lua").generic_string();
        // Definition across files, references skip comments and strings.
        auto defs = idx.Definitions("Fire");
        CHECK(defs.size() == 1 && defs[0].file == gun && defs[0].line == 1);
        auto refs = idx.References("Fire");
        CHECK(refs.size() == 2);        // the call in Player and the definition in Gun
        CHECK(idx.WordAt(player, 5, 6) == "Fire");
        // Lua: local function, method, table
        auto ls = idx.FileSymbols(cam);
        bool helper = false, update = false, table = false;
        for (const auto& s : ls) {
            if (s.name == "helper" && s.kind == 'f' && s.line == 5) helper = true;
            if (s.name == "Update" && s.display == "Script:Update" && s.line == 9) update = true;
            if (s.name == "Script" && s.kind == 'C') table = true;
        }
        CHECK(helper && update && table);
        CHECK(idx.References("helper").size() == 2);   // definition + call, not the comment or the string
        // Rename across files with a preview.
        RenamePlan p = PlanRename(idx, "Fire", "Shoot");
        CHECK(p.error.empty());
        CHECK(p.count == 2 && p.changes.size() == 2);
        CHECK(p.newTexts[player].find("Shoot(speed);") != std::string::npos);
        CHECK(p.newTexts[player].find("// Fire in a comment") != std::string::npos);
        CHECK(p.newTexts[player].find("Debug.Log(\"Fire\")") != std::string::npos);
        CHECK(p.newTexts[gun].rfind("void Shoot(float power)", 0) == 0);
        bool sawBefore = false;
        for (const auto& c : p.changes) if (c.file == gun && c.before == "void Fire(float power)" && c.after == "void Shoot(float power)") sawBefore = true;
        CHECK(sawBefore);
        CHECK(!PlanRename(idx, "Fire", "speed").error.empty());     // already used
        CHECK(!PlanRename(idx, "Fire", "return").error.empty());    // a keyword
        CHECK(!PlanRename(idx, "Nothing", "X").error.empty());      // not used
        RenamePlan lp = PlanRename(idx, "helper", "twice");
        CHECK(lp.error.empty() && lp.count == 2);
        CHECK(lp.newTexts[cam].find("-- helper in a comment") != std::string::npos);
        CHECK(lp.newTexts[cam].find("\"helper\"") != std::string::npos);
        // Unsaved text of an open tab replaces the file's.
        idx.SetFile(player, "void Update() { }\n");
        CHECK(idx.References("Fire").size() == 1);
    }

    // ---- find / replace ----
    {
        const std::string t = "speed = 1\nSpeed = 2\nspeedy = 3\n";
        FindOptions o;
        CHECK(FindAll(t, "speed", o).size() == 3);
        o.matchCase = true;
        CHECK(FindAll(t, "speed", o).size() == 2);
        o.wholeWord = true;
        auto m = FindAll(t, "speed", o);
        CHECK(m.size() == 1 && m[0].line == 1 && m[0].col == 0);
        FindOptions re; re.regex = true;
        m = FindAll(t, "s\\w+ = (\\d)", re);
        CHECK(m.size() == 3 && m[2].line == 3);
        int n = 0;
        CHECK(ReplaceAll(t, "(\\w+) = (\\d)", "$2 => $1", re, &n) == "1 => speed\n2 => Speed\n3 => speedy\n" && n == 3);
        std::string err;
        CHECK(FindAll(t, "(", re, &err).empty() && !err.empty());
        FindOptions plain;
        CHECK(ReplaceAll("a.b.c", ".", "/", plain, &n) == "a/b/c" && n == 2);   // plain text: '.' is a dot
        const auto fm = FindInTexts({{"a.okay", "x\nfoo\n"}, {"b.lua", "foo foo\n"}}, "foo", plain);
        CHECK(fm.size() == 3 && fm[0].file == "a.okay" && fm[0].match.line == 2 && fm[2].lineText == "foo foo");
    }

    // ---- comment toggle, call at caret, edits ----
    {
        CHECK(ToggleComment("a\n  b\n\nc", 1, 3, "//") == "// a\n  // b\n\nc");
        CHECK(ToggleComment("// a\n  // b\nc", 1, 2, "//") == "a\n  b\nc");
        CHECK(ToggleComment("// a\nb", 1, 2, "--") == "-- // a\n-- b");    // mixed: comment all
        CHECK(ToggleComment("x = 1", 1, 1, "--") == "-- x = 1");
        const std::string call = "move(1, foo(2, 3), ";
        auto ci = CallAtCaret(call, call.size());
        CHECK(ci.name == "move" && ci.argIndex == 2);
        ci = CallAtCaret("Input.GetAxis(", 14);
        CHECK(ci.name == "Input.GetAxis" && ci.argIndex == 0);
        CHECK(CallAtCaret("x = 1", 5).name.empty());
        CHECK(ApplyEdits("abc\ndef\n", {{2, 1, 1, "X"}, {1, 0, 0, ">"}}) == ">abc\ndXf\n");
    }

    // ---- lint with quick fixes ----
    {
        const std::vector<std::string> known = {"print", "pairs", "Vector3", "Input", "math"};
        const std::string lua =
            "local Script = {}\n"
            "function Script:Update(dt)\n"
            "    local unused = 5\n"
            "    prnt(dt)\n"
            "    print(total)\n"
            "end\n"
            "return Script\n";
        auto ds = BasicLint("lua", lua, known);
        const Diagnostic* typo = nullptr; const Diagnostic* undecl = nullptr; const Diagnostic* unused = nullptr;
        for (const auto& d : ds) {
            if (d.message.find("'prnt'") != std::string::npos) typo = &d;
            if (d.message.find("'total' is never declared") != std::string::npos) undecl = &d;
            if (d.message.find("'unused' is never used") != std::string::npos) unused = &d;
        }
        CHECK(typo && HasFix(*typo, "Change to 'print'"));
        CHECK(undecl && HasFix(*undecl, "Declare 'total'"));
        CHECK(unused && HasFix(*unused, "Remove 'unused'"));
        if (typo) CHECK(ApplyEdits(lua, typo->fixes[0].edits).find("    print(dt)\n") != std::string::npos);
        if (undecl) CHECK(ApplyEdits(lua, undecl->fixes[0].edits).find("    local total = nil\n    print(total)") != std::string::npos);
        if (unused) CHECK(ApplyEdits(lua, unused->fixes[0].edits).find("unused") == std::string::npos);
        CHECK(BasicLint("lua", kLua, known).empty());   // a clean script has no findings
        const std::string okay =
            "public float speed = 5;\n"
            "void Update()\n"
            "{\n"
            "    var spare = 2;\n"
            "    transform.position += Vector3.right * speed * Time.deltaTime;\n"
            "    Debug.Log(scor);\n"
            "    return;\n"
            "}\n";
        ds = BasicLint("okayscript", okay, {"Vector3", "Time", "Debug", "transform"});
        bool scor = false, spare = false;
        for (const auto& d : ds) {
            if (d.message.find("'scor' is never declared") != std::string::npos) scor = HasFix(d, "Declare 'scor'");
            if (d.message.find("'spare' is never used") != std::string::npos) spare = true;
            CHECK(d.message.find("'speed'") == std::string::npos);
        }
        CHECK(scor && spare);
        // "Did you mean X?" from a checker's message becomes a fix.
        Diagnostic d; d.line = 2; d.col = 12;   // a column inside the word works too
        d.message = "Input has no member 'GetKeyy'. Did you mean GetKey?";
        QuickFix f = FixFromSuggestion(d, "x = 1\nif Input.GetKeyy(\"a\") then end\n");
        CHECK(f.title == "Change to 'GetKey'" && ApplyEdits("x = 1\nif Input.GetKeyy(\"a\") then end\n", f.edits) == "x = 1\nif Input.GetKey(\"a\") then end\n");
        CHECK(NearestName("Updat", {"Start", "Update"}) == "Update");
        CHECK(NearestName("zzzz", {"Start", "Update"}).empty());
    }

    // ---- templates ----
    {
        const auto lt = TemplatesFor("lua");
        CHECK(lt.size() >= 3 && lt[0].body.find("function Script:Start()") != std::string::npos);
        const auto ot = TemplatesFor("okayscript");
        CHECK(ot.size() >= 3 && ot[0].body.find("void Update()") != std::string::npos);
        CHECK(FillTemplate("// {Name} {Name}", "Door") == "// Door Door");
    }

    // ---- local history ----
    {
        LocalHistory h(root.generic_string(), 3, 1u << 20);
        const std::string f = (root / "Assets/Scripts/Player.okay").generic_string();
        CHECK(h.Record(f, "v1", 1000, "Saved"));
        CHECK(!h.Record(f, "v1", 2000, "Saved"));            // the same text is not kept twice
        CHECK(h.Record(f, "v2", 3000, "Saved"));
        CHECK(!h.MaybeRecord(f, "v3", 3000 + 1000, 60000));    // not a minute yet
        CHECK(h.MaybeRecord(f, "v3", 3000 + 61000, 60000));
        CHECK(h.Record(f, "v4", 100000, "Saved"));
        auto vs = h.Versions(f);
        CHECK(vs.size() == 3);                                // capped at 3 per script, the oldest gone
        CHECK(h.Load(vs[0]) == "v4" && h.Load(vs[2]) == "v2" && vs[1].label == "Auto");
        CHECK(h.FolderFor(f).find("/.okay/history/") != std::string::npos);
        LocalHistory small(root.generic_string(), 50, 10);
        const std::string g = (root / "Assets/Scripts/Camera.lua").generic_string();
        small.Record(g, "123456", 200000, "Saved");
        small.Record(g, "abcdef", 200001, "Saved");          // over 10 bytes in all: the oldest copies go
        std::size_t total = 0;
        for (auto& e : small.Versions(g)) total += e.bytes;
        for (auto& e : small.Versions(f)) total += e.bytes;
        CHECK(total <= 10);
        const auto diff = LineDiff("a\nb\nc\n", "a\nB\nc\nd\n");
        std::string ops;
        for (const auto& l : diff) ops += l.op;
        CHECK(ops == " -+ +" || ops == " +- +");
    }

    // ---- the plugin registry: commands, palette ranking, shortcuts, providers, owners ----
    {
        using namespace pluginreg;
        Shortcut s;
        CHECK(ParseShortcut("ctrl+shift+p", s) && s.ctrl && s.shift && !s.alt && s.key == "P");
        CHECK(FormatShortcut(s) == "Ctrl+Shift+P");
        CHECK(ParseShortcut("Alt+Enter", s) && FormatShortcut(s) == "Alt+Enter");
        CHECK(ParseShortcut("F12", s) && s.key == "F12");
        CHECK(ParseShortcut("Ctrl+/", s) && s.key == "/");
        CHECK(!ParseShortcut("Ctrl+", s) && !ParseShortcut("A+B", s));
        PluginRegistry reg;
        int ran = 0;
        CHECK(reg.AddCommand({"a.format", "Format Document", "Edit", "ctrl+alt+l", "", [&] { ++ran; }}));
        CHECK(reg.AddCommand({"a.goto", "Go to Line", "Go", "Ctrl+G", "", [&] { ran += 10; }}));
        CHECK(reg.AddCommand({"p.todo", "Insert TODO Note", "TODO Tools", "Ctrl+Alt+T", "plug.okay", [&] { ran += 100; }}));
        CHECK(!reg.AddCommand({"", "x", "", "", "", [] {}}));
        CHECK(reg.FindCommand("a.format")->shortcut == "Ctrl+Alt+L");
        auto ranked = RankCommands("fmt", reg.Commands());
        CHECK(!ranked.empty() && ranked[0]->id == "a.format");
        ranked = RankCommands("todo", reg.Commands());
        CHECK(!ranked.empty() && ranked[0]->id == "p.todo");   // a title match first
        CHECK(RankCommands("", reg.Commands()).size() == 3);
        Shortcut ct; ParseShortcut("Ctrl+Alt+T", ct);
        CHECK(reg.CommandForShortcut(ct) && reg.CommandForShortcut(ct)->id == "p.todo");
        CHECK(reg.Run("p.todo") && ran == 100);
        reg.AddLint("plug.okay", "lua", [](const DocContext& c) {
            std::vector<Diagnostic> v;
            if (c.text.find("bad") != std::string::npos) { Diagnostic d; d.line = 1; d.message = "bad word"; v.push_back(d); }
            return v;
        });
        reg.AddLint("other.okay", "*", [](const DocContext&) -> std::vector<Diagnostic> { throw 1; });   // a failing provider is skipped
        reg.SetOwnerName("plug.okay", "Plug");
        DocContext ctx; ctx.language = "lua"; ctx.text = "bad";
        auto lint = reg.Lint(ctx);
        CHECK(lint.size() == 1 && lint[0].source == "Plug");
        ctx.language = "okayscript";
        CHECK(reg.Lint(ctx).empty());                              // the lint is for Lua only
        reg.AddCompletion("plug.okay", "", [](const DocContext& c) { return std::vector<CompletionItem>{{"plugWord" + c.word, "", "", ""}}; });
        ctx.word = "X";
        auto comp = reg.Complete(ctx);
        CHECK(comp.size() == 1 && comp[0].label == "plugWordX" && comp[0].insert == "plugWordX" && comp[0].owner == "plug.okay");
        reg.AddFormatter("plug.okay", "okayscript", [](const DocContext& c, std::string& out) { out = c.text + "!"; return true; });
        std::string formatted;
        CHECK(reg.Format(ctx, formatted) && formatted == "bad!");
        reg.AddHover("plug.okay", "", [](const DocContext& c) { return c.word == "X" ? std::string("about X") : std::string(); });
        CHECK(reg.Hover(ctx) == "about X");
        reg.AddSnippet({"fori", "for loop", "for (i = 0; i < n; i = i + 1) {\n}\n", "okayscript", "plug.okay"});
        CHECK(reg.Snippets("okayscript").size() == 1 && reg.Snippets("lua").empty());
        reg.AddPanel({"", "TODOs", "plug.okay", [] {}});
        CHECK(reg.Panels().size() == 1);
        // Turning the plugin off removes everything it added and nothing else.
        CHECK(reg.RemoveOwner("plug.okay") == 7);
        CHECK(reg.Commands().size() == 2 && reg.Panels().empty() && reg.Complete(ctx).empty() && !reg.Format(ctx, formatted));
    }

    std::error_code ec;
    fs::remove_all(root, ec);
    TEST_MAIN_RESULT();
}
