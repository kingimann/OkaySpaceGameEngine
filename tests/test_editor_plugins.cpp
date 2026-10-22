#include "test_framework.hpp"
#include "okay/Scripting/OkayScriptVM.hpp"
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
    TEST_MAIN_RESULT();
}
