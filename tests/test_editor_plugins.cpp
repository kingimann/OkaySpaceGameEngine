#include "test_framework.hpp"
#include "okay/Scripting/OkayScriptVM.hpp"
#include "PluginScriptApi.hpp"   // the script_* calls a plugin adds to the Script Editor with
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace okay;
using V = vs::VsValue;

// Editor plugins are OkayScript files whose editor_* API comes from host
// builtins (OkayScriptVM::AddBuiltin). Run the shipped examples against a tiny
// fake editor and check they load and do what they say.
struct FakeObj { std::string name; float x = 0, y = 0, z = 0; };
static std::vector<FakeObj> g_objs;
static std::vector<int> g_sel;
static FakeObj* Find(const std::string& n) { for (auto& o : g_objs) if (o.name == n) return &o; return nullptr; }

static void Install(OkayScriptVM& vm) {
    auto s = [](std::vector<V>& a, std::size_t i) { return i < a.size() ? a[i].AsString() : std::string(); };
    auto f = [](std::vector<V>& a, std::size_t i) { return i < a.size() ? a[i].AsFloat() : 0.0f; };
    vm.AddBuiltin("editor_selection_count", [](std::vector<V>&) { return V{(float)g_sel.size()}; });
    vm.AddBuiltin("editor_selection_name", [f](std::vector<V>& a) { int i = (int)f(a, 0); return V{g_objs[g_sel[i]].name}; });
    vm.AddBuiltin("editor_object_count", [](std::vector<V>&) { return V{(float)g_objs.size()}; });
    vm.AddBuiltin("editor_object_name", [f](std::vector<V>& a) { return V{g_objs[(int)f(a, 0)].name}; });
    vm.AddBuiltin("editor_get_pos", [s](std::vector<V>& a) {
        FakeObj* o = Find(s(a, 0)); std::string ax = s(a, 1);
        return V{!o ? 0.0f : ax == "x" ? o->x : ax == "y" ? o->y : o->z};
    });
    vm.AddBuiltin("editor_set_pos", [s, f](std::vector<V>& a) {
        if (FakeObj* o = Find(s(a, 0))) { o->x = f(a, 1); o->y = f(a, 2); o->z = f(a, 3); } return V{};
    });
    vm.AddBuiltin("editor_rename", [s](std::vector<V>& a) { if (FakeObj* o = Find(s(a, 0))) o->name = s(a, 1); return V{}; });
    for (const char* n : {"editor_undo_point", "editor_log", "editor_hint", "editor_heading"})
        vm.AddBuiltin(n, [](std::vector<V>&) { return V{}; });
}
static std::string ReadAll(const std::string& p) { std::ifstream f(p); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }

int main() {
    RUN_SUITE("editor_plugins");
    const std::string dir = std::string(OKAY_SOURCE_DIR) + "/examples/editor-plugins/";

    {   // Align Tools: align Y to the first object, then drop everything to y = 0.
        g_objs = {{"A", 1, 5, 0}, {"B", 3, 2, 1}, {"C", 7, 9, 2}};
        g_sel = {0, 1, 2};
        OkayScriptVM vm; Install(vm);
        std::string err;
        CHECK(vm.Load(ReadAll(dir + "AlignTools.okay"), &err));
        CHECK(err.empty());
        vm.SetGlobal("gap", V{2.0f});
        vm.CallEvent("on_gui");                 // no UI host installed: widgets are no-ops
        // Call the helpers the panel's buttons use.
        OkayScriptVM vm2; Install(vm2);
        CHECK(vm2.Load(ReadAll(dir + "AlignTools.okay") + "\nalign(\"y\")\n", &err));
        CHECK(g_objs[1].y == 5.0f && g_objs[2].y == 5.0f);
        OkayScriptVM vm3; Install(vm3);
        CHECK(vm3.Load(ReadAll(dir + "AlignTools.okay") + "\ndistribute()\ndrop()\n", &err));
        CHECK(g_objs[1].x == 3.0f && g_objs[2].x == 5.0f);   // x0 + i * gap
        CHECK(g_objs[0].y == 0.0f && g_objs[2].y == 0.0f);
    }
    {   // Clean Up Names: "Cube (3)" -> "Cube".
        g_objs = {{"Cube (3)"}, {"Tree"}, {"Rock (12)"}};
        OkayScriptVM vm; Install(vm);
        std::string err;
        CHECK(vm.Load(ReadAll(dir + "CleanupNames.okay"), &err));
        vm.CallEvent("on_menu");
        CHECK(g_objs[0].name == "Cube" && g_objs[1].name == "Tree" && g_objs[2].name == "Rock");
    }
    {   // TODO Tools: a Script Editor command and a lint rule, registered through the script_* calls.
        pluginreg::PluginRegistry reg;
        const std::string owner = dir + "TodoTools.okay";
        OkayScriptVM vm; Install(vm);
        oked::RegisterScriptEditorApi(vm, reg, owner, "TODO Tools");
        std::string err;
        CHECK(vm.Load(ReadAll(owner), &err));
        CHECK(err.empty());
        vm.CallStart();
        const pluginreg::Command* cmd = reg.FindCommand("todo.insert");
        CHECK(cmd && cmd->title == "Insert TODO Note" && cmd->shortcut == "Ctrl+Alt+T" && cmd->category == "TODO Tools");
        // The palette finds it, its shortcut finds it, and running it inserts into the open script.
        auto ranked = pluginreg::RankCommands("todo", reg.Commands());
        CHECK(!ranked.empty() && ranked[0]->id == "todo.insert");
        pluginreg::Shortcut sc; pluginreg::ParseShortcut("ctrl+alt+t", sc);
        CHECK(reg.CommandForShortcut(sc) == cmd);
        std::string inserted, lang = "lua";
        auto& host = oked::ScriptEditorHostApi();
        host.language = [&] { return lang; };
        host.insert = [&](const std::string& t) { inserted += t; };
        CHECK(reg.Run("todo.insert"));
        CHECK(inserted == "-- TODO: ");
        // Its lint: a TODO is a note, a FIXME a warning with a quick fix; they reach the Script Editor's Problems list.
        pluginreg::DocContext ctx;
        ctx.language = "okayscript"; ctx.path = "Player.okay";
        ctx.text = "void Update()\n{\n    // TODO: jump\n    move(1); // FIXME speed\n}\n";
        auto problems = pluginreg::ScriptProblems(reg, ctx, {});
        CHECK(problems.size() == 2);
        if (problems.size() == 2) {
            CHECK(problems[0].line == 3 && problems[0].severity == 2 && problems[0].message == "TODO: jump" && problems[0].source == "TODO Tools");
            CHECK(problems[1].line == 4 && problems[1].severity == 1 && problems[1].fixes.size() == 1);
            if (!problems[1].fixes.empty()) {
                CHECK(problems[1].fixes[0].title == "Change FIXME to TODO");
                CHECK(scriptide::ApplyEdits(ctx.text, problems[1].fixes[0].edits).find("// TODO speed") != std::string::npos);
            }
        }
        // The editor's own findings come first on the same line; turning the plugin off removes its command and lint.
        scriptide::Diagnostic own; own.line = 4; own.message = "own";
        problems = pluginreg::ScriptProblems(reg, ctx, {own});
        CHECK(problems.size() == 3 && problems[1].message == "own");
        CHECK(reg.RemoveOwner(owner) == 2);
        CHECK(!reg.FindCommand("todo.insert") && pluginreg::ScriptProblems(reg, ctx, {}).empty());
        host = oked::ScriptEditorHost{};
    }
    {   // Every other script_* registration: completion, formatter, hover, snippet, panel.
        pluginreg::PluginRegistry reg;
        OkayScriptVM vm;
        oked::RegisterScriptEditorApi(vm, reg, "p.okay", "P");
        std::string err;
        CHECK(vm.Load("start() {\n"
                      "  script_completion(\"words\")\n"
                      "  script_formatter(\"fmt\", \"lua\")\n"
                      "  script_hover(\"hov\")\n"
                      "  script_snippet(\"fori\", \"for i = 1, 10 do\\nend\", \"lua\", \"numeric for\")\n"
                      "  script_panel(\"Notes\", \"draw\")\n"
                      "}\n"
                      "words(prefix, language) { script_suggest(prefix + \"Thing\", \"from P\") }\n"
                      "fmt(text, language) { return trim(text) }\n"
                      "hov(word, language) {\n  if (word == \"speed\") return \"how fast\"\n  return \"\"\n}\n"
                      "drawn = 0\n"
                      "draw() { drawn = drawn + 1 }\n", &err));
        vm.CallStart();
        pluginreg::DocContext ctx; ctx.language = "lua"; ctx.word = "my"; ctx.text = "  x = 1  ";
        auto items = reg.Complete(ctx);
        CHECK(items.size() == 1 && items[0].label == "myThing" && items[0].detail == "from P");
        std::string out;
        CHECK(reg.Format(ctx, out) && out == "x = 1");
        ctx.language = "okayscript";
        CHECK(!reg.Format(ctx, out));                 // its formatter is for Lua only
        ctx.word = "speed";
        CHECK(reg.Hover(ctx) == "how fast");
        auto sn = reg.Snippets("lua");
        CHECK(sn.size() == 1 && sn[0].prefix == "fori" && sn[0].title == "numeric for");
        CHECK(reg.Panels().size() == 1 && reg.Panels()[0].title == "Notes");
        if (!reg.Panels().empty()) reg.Panels()[0].draw();
        CHECK(vm.GetGlobal("drawn").AsFloat() == 1.0f);
    }
    TEST_MAIN_RESULT();
}
