// Pin types and the visual-script graph checker (run before Play / Build).
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/ActionList.hpp"
#include "okay/VisualScript/ActionGraph.hpp"
#include "okay/VisualScript/VsTypes.hpp"
#include "okay/VisualScript/VsApi.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
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
        auto v = ValidateGraphText(R"VS(
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
)VS");
        CHECK(v.empty());
        for (const auto& i : v) std::fprintf(stderr, "  unexpected: %s\n", i.message.c_str());
    }
    // ---- Wires that can't work are errors, with a tip --------------------------
    {
        auto v = ValidateGraphText(R"VS(
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
)VS");
        CHECK(Errors(v) == 2);
        CHECK(Has(v, 0, 2, "Vector3"));
        CHECK(Has(v, 0, 2, "Vec X"));            // the tip
        CHECK(Has(v, 0, 5, "To Number"));
    }
    // ---- Number into a Bool input, object into Print, name into a Target: fine ---
    {
        auto v = ValidateGraphText(R"VS(
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
)VS");
        CHECK(Errors(v) == 0);
    }
    // ---- Unreachable nodes, idle events, unset variables -------------------------
    {
        auto v = ValidateGraphText(R"VS(
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
)VS");
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

    // ---- Function signatures --------------------------------------------------
    {
        FunctionSig f; std::string err;
        CHECK(ParseFunctionSignature("Damage(amount: Number, who: Object) -> left: Number, dead: Bool", f, &err));
        CHECK(f.name == "Damage" && f.params.size() == 2 && f.returns.size() == 2);
        CHECK(f.params[1].type == PinType::Object && f.returns[1].type == PinType::Bool);
        CHECK(ParseFunctionSignature("Reset()", f) && f.params.empty() && f.returns.empty());
        CHECK(ParseFunctionSignature("Reset", f) && f.name == "Reset");
        CHECK(ParseFunctionSignature("Mix(a, b: Vector3)", f) && f.params[0].type == PinType::Any);
        CHECK(!ParseFunctionSignature("(x: Number)", f, &err) && err.find("no name") != std::string::npos);
        CHECK(!ParseFunctionSignature("F(x: Numbr)", f, &err) && err.find("unknown type 'Numbr'") != std::string::npos);
        CHECK(!ParseFunctionSignature("F(x, x)", f, &err) && err.find("twice") != std::string::npos);
        CHECK(!ParseFunctionSignature("F(x", f, &err) && err.find("')'") != std::string::npos);
        CHECK(!ParseFunctionSignature("2F()", f, &err));
    }
    // ---- Function rules in the checker -------------------------------------------
    {
        auto v = ValidateGraphText(R"VS(
node 10 FunctionStart "Double(x: Number) -> r: Number"
node 11 Mul
node 12 Return Double
data 11 0 10 0
data 12 0 11 0
exec 10 0 12
node 20 FunctionStart "Unused()"
node 21 Print
exec 20 0 21
node 30 FunctionStart "Double(y: Number)"
node 40 FunctionStart "Slow()"
node 41 Delay 1
node 42 Return Double
exec 40 0 41
exec 41 0 42
node 0 OnStart
node 1 CallFunction Double
node 2 Const "abc"
node 3 Return Double
node 4 CallFunction Nope
node 5 SetLocal t
node 6 GetLocal t
node 7 Print
node 8 CallFunction Slow
data 1 0 2 0
data 7 0 6 0
data 5 0 10 0
exec 0 0 1
exec 1 0 4
exec 4 0 5
exec 5 0 7
exec 7 0 8
exec 8 0 3
entry OnStart 0
)VS");
        CHECK(Errors(v) > 0);
        CHECK(Has(v, 0, 1, "needs a Number"));                // Text into Double's Number parameter
        CHECK(Has(v, 0, 30, "already a function named 'Double'"));
        CHECK(Has(v, 0, 4, "no function named 'Nope'"));
        CHECK(Has(v, 0, 3, "outside a function"));
        CHECK(Has(v, 0, 41, "can't pause inside a function"));
        CHECK(Has(v, 0, 42, "returns from 'Double'"));
        CHECK(Has(v, 0, 5, "Set Local only works inside a function"));
        CHECK(Has(v, 0, 6, "Get Local only works inside a function"));
        CHECK(Has(v, 0, 10, "read by an event's chain"));
        CHECK(Has(v, 1, 20, "never called"));
        CHECK(!Has(v, 1, 21, "nothing runs"));                 // its body counts as reachable
        CHECK(!Has(v, 1, 11, "nothing runs"));
    }
    {   // wires to pins a function no longer has
        auto v = ValidateGraphText("node 10 FunctionStart \"F(a: Number) -> r: Number\"\nnode 11 Return F\nnode 12 Print\n"
                                   "data 11 0 10 0\ndata 12 0 10 1\nexec 10 0 11\n"
                                   "node 0 OnStart\nnode 1 CallFunction F\nnode 2 Const 1\ndata 1 0 2 0\ndata 1 1 2 0\n"
                                   "exec 0 0 1\nentry OnStart 0\n");
        CHECK(Has(v, 0, 12, "parameter #2 of 'F'"));
        CHECK(Has(v, 0, 1, "parameter #2 of 'F'"));
    }
    {   // a correct function graph is clean
        auto v = ValidateGraphText(R"VS(
node 10 FunctionStart "Double(x: Number) -> r: Number"
node 11 Mul
node 12 Const 2
node 13 Return Double
data 11 0 10 0
data 11 1 12 0
data 13 0 11 0
exec 10 0 13
node 0 OnStart
node 1 CallFunction Double
node 2 Const 21
node 3 Print
data 1 0 2 0
data 3 0 1 0
exec 0 0 1
exec 1 0 3
entry OnStart 0
)VS");
        CHECK(v.empty());
        for (const auto& i : v) std::fprintf(stderr, "  unexpected: %s\n", i.message.c_str());
    }

    // ---- Engine-API nodes: typed from the binding table, literals checked by it ----
    {
        const NodeSignature* sig = SignatureFor("Transform.Translate/3");
        CHECK(sig && sig->execIn && sig->ins == "NNNO" && sig->outs.empty());
        CHECK(TargetPinFor("Transform.Translate/3") == 3);
        CHECK(TargetPinFor("Mathf.Clamp/3") == -1);
        CHECK(OutputType("Rigidbody.velocity", 0) == PinType::Vec3);
        CHECK(InputType("Transform.position=", 0) == PinType::Vec3);
        CHECK(OutputType("GameObject.Find/1", 0) == PinType::Object);
        bool listed = false;
        for (const auto& t : AllNodeTypes()) listed = listed || t == "Rigidbody.AddForce/1";
        CHECK(listed);

        auto keyGraph = [](const std::string& extra) {
            return "node 0 OnUpdate\nnode 1 Branch\nnode 2 Input.GetKey/1\nnode 3 Print\n" + extra +
                   "data 1 0 2 0\nexec 0 0 1\nexec 1 0 3\nentry OnUpdate 0\n";
        };
        auto bad = ValidateGraphText(keyGraph("default 2 0 \"spacebar\"\n"));
        CHECK(Has(bad, 0, 2, "isn't a key name"));
        CHECK(Errors(ValidateGraphText(keyGraph("default 2 0 \"space\"\n"))) == 0);
        CHECK(Has(ValidateGraphText(keyGraph("")), 0, 2, "input 'key'"));   // nothing typed in
        // A Const wired straight in is checked the same way; anything else is left to Play.
        auto viaConst = ValidateGraphText("node 0 OnUpdate\nnode 1 SetVar a\nnode 2 Input.GetAxis/1\nnode 3 Const \"Sideways\"\n"
                                          "data 2 0 3 0\ndata 1 0 2 0\nexec 0 0 1\nentry OnUpdate 0\n");
        CHECK(Has(viaConst, 0, 2, "isn't an axis"));
        auto dyn = ValidateGraphText("node 0 OnUpdate\nnode 1 SetVar a\nnode 2 Input.GetAxis/1\nnode 3 GetVar which\n"
                                     "node 4 SetVar which\nnode 5 Const \"Vertical\"\ndata 4 0 5 0\n"
                                     "data 2 0 3 0\ndata 1 0 2 0\nexec 0 0 4\nexec 4 0 1\nentry OnUpdate 0\n");
        CHECK(!Has(dyn, 0, 2, "isn't an axis"));
        // Wrong kinds of value are refused like on any node.
        auto mism = ValidateGraphText("node 0 OnStart\nnode 1 Transform.Translate/1\nnode 2 Const \"hello\"\n"
                                      "data 1 0 2 0\nexec 0 0 1\nentry OnStart 0\n");
        CHECK(Has(mism, 0, 1, "Text"));
        // A binding that doesn't exist (renamed in the table?) says so.
        auto gone = ValidateGraphText("node 0 OnStart\nnode 1 Transform.Bogus/1\nexec 0 0 1\nentry OnStart 0\n");
        CHECK(Has(gone, 0, -1, "no engine binding"));
    }

    // ---- docs/engine_api.md is generated from the binding table and up to date ----
    {
        const std::string path = std::string(OKAY_SOURCE_DIR) + "/docs/engine_api.md";
        const std::string want = VsApiReference();
        if (const char* w = std::getenv("OKAY_WRITE_API_DOCS"); w && *w == '1') {
            std::ofstream(path, std::ios::binary) << want;
        }
        std::ifstream f(path, std::ios::binary);
        std::stringstream have; have << f.rdbuf();
        CHECK(have.str() == want);
        if (have.str() != want)
            std::fprintf(stderr, "  docs/engine_api.md is out of date: run OKAY_WRITE_API_DOCS=1 ./tests/test_vs_types\n");
        CHECK(want.find("| `GetComponent<Rigidbody>().AddForce(force)` | `Rigidbody.AddForce/1` | step |") != std::string::npos);
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
