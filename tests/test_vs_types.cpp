// Pin types and the visual-script graph checker (run before Play / Build).
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/ActionList.hpp"
#include "okay/VisualScript/ActionGraph.hpp"
#include "okay/VisualScript/VsTypes.hpp"
#include <cstdio>
#include <string>

using namespace okay;
using namespace okay::vs;

static bool Has(const std::vector<VsIssue>& v, int sev, int node, const std::string& part) {
    for (const auto& i : v)
        if (i.severity == sev && (node < 0 || i.node == node) && i.message.find(part) != std::string::npos) return true;
    return false;
}
static int Errors(const std::vector<VsIssue>& v) { int n = 0; for (const auto& i : v) n += i.severity == 0; return n; }

int main() {
    RUN_SUITE("vs_types");

    // ---- The table covers every node type, and every type in it builds ---------
    {
        const auto types = AllNodeTypes();
        CHECK(types.size() >= 250);
        int built = 0;
        for (const std::string& t : types) {
            std::string err;
            auto n = MakeNode(t, {}, &err);
            if (n) ++built; else std::fprintf(stderr, "  type %s doesn't build: %s\n", t.c_str(), err.c_str());
            const NodeSignature* s = SignatureFor(t);
            CHECK(s != nullptr);
            // A node with a Target input has it as its last input, typed Object.
            const int tp = TargetPinFor(t);
            if (s && tp >= 0) { CHECK(tp == (int)s->ins.size() - 1); CHECK(s->ins.back() == 'O'); }
            // Event nodes take no exec input; their runtime node has an exec output.
            if (s && t.rfind("On", 0) == 0 && t != "Once") CHECK(!s->execIn);
        }
        CHECK(built == (int)types.size());
        CHECK(SignatureFor("OnKey:space") != nullptr);
        CHECK(SignatureFor("NoSuchNode") == nullptr);
    }

    // ---- Compatibility rules ------------------------------------------------------
    {
        CHECK(PinCompatible(PinType::Number, PinType::Bool));
        CHECK(PinCompatible(PinType::Bool, PinType::Number));
        CHECK(PinCompatible(PinType::Vec3, PinType::Text));
        CHECK(PinCompatible(PinType::Object, PinType::Text));
        CHECK(PinCompatible(PinType::Text, PinType::Object));       // a name
        CHECK(PinCompatible(PinType::Object, PinType::Bool));       // still exists?
        CHECK(PinCompatible(PinType::Any, PinType::Vec3));
        CHECK(PinCompatible(PinType::Number, PinType::Any));
        CHECK(!PinCompatible(PinType::Vec3, PinType::Number));
        CHECK(!PinCompatible(PinType::Number, PinType::Vec3));
        CHECK(!PinCompatible(PinType::Text, PinType::Number));
        CHECK(!PinCompatible(PinType::Object, PinType::Vec3));
        CHECK(!PinCompatible(PinType::List, PinType::Number));
        CHECK(ConstType("3.5") == PinType::Number);
        CHECK(ConstType("true") == PinType::Bool);
        CHECK(ConstType("\"hi\"") == PinType::Text);
        CHECK(ConstType("hello") == PinType::Text);
        CHECK(OutputType("GetPosition", 0) == PinType::Vec3);
        CHECK(InputType("Translate", 3) == PinType::Object);         // its Target
        CHECK(OutputType("Raycast3D", 4) == PinType::Object);
    }

    // ---- A correct graph has no issues -----------------------------------------
    {
        auto v = ValidateGraphText(R"(
node 0 OnUpdate
node 1 AxisX
node 2 Const 5
node 3 Mul
node 4 DeltaTime
node 5 Mul
node 6 Translate
data 3 0 1 0
data 3 1 2 0
data 5 0 3 0
data 5 1 4 0
data 6 0 5 0
exec 0 0 6
entry OnUpdate 0
)");
        CHECK(v.empty());
        for (const auto& i : v) std::fprintf(stderr, "  unexpected: %s\n", i.message.c_str());
    }
    // ---- Wires that can't work are errors, with a tip --------------------------
    {
        auto v = ValidateGraphText(R"(
node 0 OnStart
node 1 GetPosition
node 2 Add
node 3 SetVar s
node 4 Const "abc"
node 5 Mul
node 6 SetVar m
data 2 0 1 0
data 3 0 2 0
data 5 0 4 0
data 6 0 5 0
exec 0 0 3
exec 3 0 6
entry OnStart 0
)");
        CHECK(Errors(v) == 2);
        CHECK(Has(v, 0, 2, "Vector3"));
        CHECK(Has(v, 0, 2, "Vec X"));            // the tip
        CHECK(Has(v, 0, 5, "To Number"));
    }
    // ---- Number into a Bool input, object into Print, name into a Target: fine ---
    {
        auto v = ValidateGraphText(R"(
node 0 OnStart
node 1 Const 1
node 2 Branch
node 3 Self
node 4 Print
node 5 SetPosition
node 6 Const "Box"
data 2 0 1 0
data 4 0 3 0
data 5 3 6 0
exec 0 0 2
exec 2 0 4
exec 4 0 5
entry OnStart 0
)");
        CHECK(Errors(v) == 0);
    }
    // ---- Unreachable nodes, idle events, unset variables -------------------------
    {
        auto v = ValidateGraphText(R"(
node 0 OnStart
node 1 Print
node 2 Print
node 3 OnUpdate
node 4 GetVar speed
node 5 SetVar copy
data 5 0 4 0
exec 0 0 5
entry OnStart 0
entry OnUpdate 3
)");
        CHECK(Has(v, 1, 1, "nothing runs"));
        CHECK(Has(v, 1, 2, "nothing runs"));
        CHECK(Has(v, 1, 3, "does nothing"));
        CHECK(Has(v, 1, 4, "'speed' is read but"));
        CHECK(Errors(v) == 0);   // all warnings: it still runs
    }
    // ---- Key names that would silently be another key ---------------------------
    {
        auto v = ValidateGraphText("node 0 OnKey F1\nnode 1 Print\nexec 0 0 1\nentry OnKey:F1 0\n"
                                   "node 2 OnKey escape\nnode 3 Print\nexec 2 0 3\nentry OnKey:escape 2\n"
                                   "node 4 Key lshift\nnode 5 Branch\ndata 5 0 4 0\nexec 2 0 5\n");
        CHECK(Has(v, 0, 0, "'F1' isn't a key"));
        CHECK(Has(v, 0, 4, "'lshift'"));
        CHECK(!Has(v, 0, 2, "isn't a key"));
    }
    // ---- Prefabs that aren't there (checked against the project folder) ----------
    {
        { Scene t("T"); auto* b = t.CreateGameObject("B"); SceneSerializer::SaveObjectToFile(*b, "vs_types_ok.okayprefab"); }
        auto v = ValidateGraphText("node 0 OnStart\nnode 1 Spawn vs_types_ok.okayprefab\nnode 2 Spawn nope/missing.okayprefab\n"
                                   "exec 0 0 1\nexec 1 0 2\nentry OnStart 0\n", ".");
        CHECK(!Has(v, 0, 1, "doesn't exist"));
        CHECK(Has(v, 0, 2, "doesn't exist"));
        std::remove("vs_types_ok.okayprefab");
    }
    // ---- A graph that doesn't load at all ----------------------------------------
    {
        auto v = ValidateGraphText("node 0 Bogus\n");
        CHECK(Errors(v) == 1 && Has(v, 0, -1, "can't be loaded"));
    }
    // ---- Graphs made from Actions lists check clean of errors --------------------
    {
        ActionList al;
        al.trigger = ActionList::Trigger::OnKey; al.triggerKey = "e";
        al.conditions = {{"var_gt", {"coins", "0"}}};
        al.instructions = {{"add_var", {"coins", "-1"}}, {"play_sound", {}}, {"wait", {"0.5"}}};
        auto v = ValidateGraphText(ActionListToGraph(al));
        CHECK(Errors(v) == 0);
        for (const auto& i : v) if (i.severity == 0) std::fprintf(stderr, "  actions graph: %s\n", i.message.c_str());
    }

    // ---- Scene-wide check (what Play and Build run) names the object and node ----
    {
        Scene sc("S");
        GameObject* a = sc.CreateGameObject("Player");
        a->AddComponent<VisualScriptComponent>()->LoadFromText(
            "node 0 OnStart\nnode 1 GetPosition\nnode 2 Add\nnode 3 SetVar s\ndata 2 0 1 0\ndata 3 0 2 0\nexec 0 0 3\nentry OnStart 0\n");
        a->AddComponent<VisualScriptComponent>()->LoadFromText("node 0 OnStart\nnode 1 Print\nexec 0 0 1\nentry OnStart 0\n");
        auto all = ValidateSceneGraphs(sc);
        int errs = 0; for (const auto& si : all) errs += si.issue.severity == 0;
        CHECK(errs == 1);
        bool named = false;
        for (const auto& si : all)
            if (si.issue.severity == 0) named = si.object == "Player" && si.nodeType == "Add" &&
                                                si.Describe().find("Visual Script on 'Player', node 'Add' (#2)") == 0;
        CHECK(named);
    }

    TEST_MAIN_RESULT();
}
