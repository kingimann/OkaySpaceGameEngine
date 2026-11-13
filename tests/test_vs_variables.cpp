// Typed, declared visual-script variables (all types, all scopes, editable defaults) and the
// full list / map / struct / colour node set: value literals, declarations, copy versus
// reference, bounds and type errors reported with the node (never a crash), the graph checker's
// typed pins, hostile input, and save / scene round trips.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Core/SaveGame.hpp"
#include "okay/Core/SaveData.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/VisualScript/NodeGraph.hpp"
#include "okay/VisualScript/VsDecl.hpp"
#include "okay/VisualScript/VsVariables.hpp"

#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

using namespace okay;
namespace fs = std::filesystem;

static std::vector<vs::VsRuntimeError> g_errs;
static std::vector<std::string> g_printed;
static void OnErr(const vs::VsRuntimeError& e) { g_errs.push_back(e); }
static bool HasErr(const std::string& type, const std::string& part) {
    for (const auto& e : g_errs) if (e.nodeType == type && e.message.find(part) != std::string::npos && e.fileId >= 0) return true;
    return false;
}

// Run `text` on a fresh object's On Start; the component stays alive in `scene`.
struct Run {
    Scene scene{"vars"};
    VisualScriptComponent* vc = nullptr;
    bool loaded = false;
    explicit Run(const std::string& text) {
        vc = scene.CreateGameObject("Obj")->AddComponent<VisualScriptComponent>();
        std::string err;
        loaded = vc->LoadFromText(text, &err);
        if (!loaded) std::printf("  load failed: %s\n", err.c_str());
        g_errs.clear();
        scene.Start();
    }
    std::string Var(const char* n) const { return vc->GetVariable(n).AsString(); }
};

// OnStart (node 1) -> the exec chain of the given node ids.
static std::string Chain(const std::string& body, std::initializer_list<int> order) {
    std::string t = "node 1 OnStart\nentry OnStart 1\n" + body;
    int prev = 1;
    for (int id : order) { t += "exec " + std::to_string(prev) + " 0 " + std::to_string(id) + "\n"; prev = id; }
    return t;
}

int main() {
    RUN_SUITE("vs_variables");
    vs::SetRuntimeErrorHook(&OnErr);
    auto sink = Log::sink;
    Log::sink = [](Log::Level, const std::string&) {};

    // ---- Value literals: every kind round-trips; hostile text is refused, not run away with ----
    {
        const char* good[] = {"5", "-1.5", "true", "\"hi \\\"there\\\"\"", "(1, 2, 3)", "#ff8800", "#ff880080",
                              "[]", "{}", "[1, 2, [3, 4], {a: 5}]", "{x: (1, 2, 3), \"two words\": #00ff00}"};
        for (const char* g : good) {
            vs::VsValue v, w; std::string err;
            CHECK(vs::VsParseLiteral(g, v, &err));
            CHECK(vs::VsParseLiteral(vs::VsToLiteral(v), w, &err));
            CHECK(vs::VsToLiteral(v) == vs::VsToLiteral(w));
        }
        const char* bad[] = {"", "[", "[1,", "{a:", "{a 1}", "(1, 2)", "(1, 2, 3, 4)", "#12345", "#gg0000", "\"open", "1e999",
                             "[1] x", "@@", "nan", "{:1}", "[,]"};
        for (const char* b : bad) {
            vs::VsValue v; std::string err;
            const bool ok = vs::VsParseLiteral(b, v, &err);
            if (std::string(b) == "@@") continue;   // an object name, whatever it is
            CHECK(!ok);
            CHECK(!ok == !err.empty());
        }
        std::string deep(200, '['); deep += std::string(200, ']');
        vs::VsValue v; std::string err;
        CHECK(!vs::VsParseLiteral(deep, v, &err));                     // nested far too deep
        std::string many = "[";
        for (int i = 0; i < 100500; ++i) many += "1,";
        many += "1]";
        CHECK(!vs::VsParseLiteral(many, v, &err));                     // too many items
        CHECK(!vs::VsParseLiteral(std::string(2'000'000, 'a'), v, &err));
        // A list that contains itself prints (cut off) instead of recursing forever.
        vs::VsValue self = vs::VsValue::MakeArray();
        self.AsArray()->push_back(self);
        CHECK(!vs::VsToLiteral(self).empty());
        CHECK(!vs::VsDescribe(self).empty());
        CHECK(vs::VsDeepCopy(self).IsArray());
    }

    // ---- Types and coercion ----
    {
        std::vector<vs::StructDef> none;
        vs::VarType t; std::string err;
        CHECK(vs::ParseVarType("List<Vector3>", none, t, &err) && t.base == vs::PinType::List && t.elem == vs::PinType::Vec3);
        CHECK(vs::VarTypeText(t) == "List<Vector3>");
        CHECK(!vs::ParseVarType("Banana", none, t, &err));
        CHECK(!vs::ParseVarType("List<Banana>", none, t, &err));
        CHECK(!vs::ParseVarType("Number<Text>", none, t, &err));
        vs::VarType num; num.base = vs::PinType::Number;
        vs::VsValue v("3");
        CHECK(vs::VsCoerce(num, v, none, &err) && v.IsNumber() && v.AsFloat() == 3.0f);   // "3" -> 3
        v = vs::VsValue(Vec3{1, 2, 3});
        CHECK(!vs::VsCoerce(num, v, none, &err) && err.find("expected Number, got Vector3") != std::string::npos);
        vs::VarType col; col.base = vs::PinType::Color;
        v = vs::VsValue(Vec3{1, 0, 0});
        CHECK(vs::VsCoerce(col, v, none, &err) && v.IsColor() && v.AsColor().r == 1.0f && v.AsColor().a == 1.0f);
        vs::VarType ln; ln.base = vs::PinType::List; ln.elem = vs::PinType::Number;
        vs::VsValue list; CHECK(vs::VsParseLiteral("[1, \"2\", 3]", list));
        const auto same = list.AsArray();
        CHECK(vs::VsCoerce(ln, list, none, &err) && list.AsArray() == same && (*same)[1].IsNumber());   // converted in place
        CHECK(vs::VsParseLiteral("[1, (1, 2, 3)]", list));
        CHECK(!vs::VsCoerce(ln, list, none, &err) && err.find("item 1") != std::string::npos);
        CHECK(vs::VsCompare(vs::VsValue(1.0f), vs::VsValue(2.0f)) < 0);
        CHECK(vs::VsCompare(vs::VsValue(std::numeric_limits<float>::quiet_NaN()), vs::VsValue(0.0f)) < 0);
        CHECK(vs::VsCompare(vs::VsValue(5.0f), vs::VsValue("a")) < 0);   // numbers before text
    }

    // ---- Declarations load, and bad ones are refused with the line ----
    {
        std::string err;
        CHECK(vs::NodeGraph::Parse("decl hp Number object 100\ndecl tint Color scene \"#ff8800\"\ndecl pts List<Vector3> global \"[(0,0,0), (1,1,1)]\"\n", &err));
        CHECK(!vs::NodeGraph::Parse("decl hp Banana object 1\n", &err) && err.find("line 1") != std::string::npos);
        CHECK(!vs::NodeGraph::Parse("decl hp Number object abc\n", &err));
        CHECK(!vs::NodeGraph::Parse("decl hp Number bogus 1\n", &err));
        CHECK(!vs::NodeGraph::Parse("decl 9x Number object 1\n", &err));
        CHECK(!vs::NodeGraph::Parse("decl hp Number object 1\ndecl hp Number object 2\n", &err) && err.find("twice") != std::string::npos);
        CHECK(vs::NodeGraph::Parse("decl hp Number object 1\ndecl hp Number scene 2\n", &err));      // same name, other scope
        CHECK(!vs::NodeGraph::Parse("struct A x:B\n", &err));                                         // B isn't declared above
        CHECK(!vs::NodeGraph::Parse("struct A x:A\n", &err));                                         // a struct can't hold itself
        CHECK(vs::NodeGraph::Parse("struct B y:Number\nstruct A x:B\ndecl a A object\n", &err));      // nested, in order
        CHECK(!vs::NodeGraph::Parse("struct A x:Number\nstruct A y:Number\n", &err));
        CHECK(!vs::NodeGraph::Parse("decl a Nope object\n", &err));
        std::string spam;
        for (int i = 0; i < 2100; ++i) spam += "decl v" + std::to_string(i) + " Number object 0\n";
        CHECK(!vs::NodeGraph::Parse(spam, &err));                                                     // capped
        CHECK(vs::NodeGraph::Parse("var old 5\nvar name \"x\"\n", &err));                             // old lines still load
    }

    // ---- Defaults reach the variable; each object gets its own list; scopes ----
    {
        const std::string text =
            "struct Enemy hp:Number=100 \"pos:Vector3=(1, 2, 3)\" \"tint:Color=#ff0000\"\n"
            "decl hp Number object 42\ndecl on Bool object true\ndecl name Text object \"Bob\"\n"
            "decl pos Vector3 object \"(1, 2, 3)\"\ndecl tint Color object \"#00ff00\"\n"
            "decl who Object object Player\n"
            "decl bag List<Number> object \"[1, 2, 3]\"\ndecl stats Map<Number> object \"{str: 5}\"\ndecl e Enemy object\n"
            "decl lives Number scene 3\ndecl best Number global 9\nnode 1 OnStart\nentry OnStart 1\n";
        Scene scene("d");
        auto* a = scene.CreateGameObject("A")->AddComponent<VisualScriptComponent>();
        auto* b = scene.CreateGameObject("B")->AddComponent<VisualScriptComponent>();
        std::string err;
        CHECK(a->LoadFromText(text, &err) && b->LoadFromText(text, &err));
        vs::ClearGlobalVariables();
        scene.Start();
        CHECK(a->GetVariable("hp").AsFloat() == 42.0f);
        CHECK(a->GetVariable("on").AsBool());
        CHECK(a->GetVariable("name").AsString() == "Bob");
        CHECK(a->GetVariable("pos").IsVec3() && a->GetVariable("pos").AsVec3().z == 3.0f);
        CHECK(a->GetVariable("tint").IsColor() && a->GetVariable("tint").AsColor().g == 1.0f);
        CHECK(a->GetVariable("who").AsString() == "Player");
        CHECK(a->GetVariable("bag").AsString() == "[1, 2, 3]");
        CHECK(a->GetVariable("e").IsMap() && a->GetVariable("e").AsMap()->at("hp").AsFloat() == 100.0f);
        CHECK(a->GetVariable("e").AsMap()->at("tint").IsColor());
        // Independent copies: A's list change doesn't touch B's.
        a->GetVariable("bag").AsArray()->push_back(vs::VsValue(4.0f));
        CHECK(a->GetVariable("bag").AsArray()->size() == 4 && b->GetVariable("bag").AsArray()->size() == 3);
        CHECK(a->GetVariable("bag").AsArray() != b->GetVariable("bag").AsArray());
        a->GetVariable("e").AsMap()->at("hp") = vs::VsValue(1.0f);
        CHECK(b->GetVariable("e").AsMap()->at("hp").AsFloat() == 100.0f);
        // Scene / global defaults live in their own scope, once.
        CHECK(vs::SceneVariables(&scene).at("lives").AsFloat() == 3.0f);
        CHECK(vs::GlobalVariables().at("best").AsFloat() == 9.0f);
        CHECK(a->Variables().count("lives") == 0 && a->Variables().count("best") == 0);
        vs::ClearGlobalVariables();
    }

    // ---- A declared type is enforced on Set Var, with the node reported ----
    {
        Run r(Chain("decl hp Number object 10\nnode 2 SetVar hp\ndefault 2 0 \"7\"\n"
                    "node 3 SetVar hp\nnode 4 MakeVec3\nnode 5 SetVar free\ndefault 5 0 \"anything\"\ndata 3 0 4 0\n", {2, 3, 5}));
        CHECK(r.loaded);
        CHECK(r.vc->GetVariable("hp").IsNumber() && r.vc->GetVariable("hp").AsFloat() == 7.0f);   // "7" became 7, the vector was refused
        CHECK(HasErr("SetVar", "expected Number, got Vector3"));
        CHECK(r.Var("free") == "anything");                                                       // undeclared: anything goes
    }
    {   // a list of numbers refuses a vector item (reported), a scene-scoped one too
        Run r(Chain("decl nums List<Number> object \"[1]\"\ndecl lives Number scene 3\nnode 2 GetVar nums\nnode 3 SetVar nums\nnode 4 MakeVec3\n"
                    "node 5 NewList\nnode 6 ListAdd\nnode 7 SetSceneVar lives\ndefault 7 0 \"x\"\ndata 6 0 5 0\ndata 6 1 4 0\ndata 3 0 5 0\n", {7, 3}));
        CHECK(r.loaded);
        CHECK(HasErr("SetSceneVar", "expected Number"));
        CHECK(vs::SceneVariables(&r.scene).at("lives").AsFloat() == 3.0f);
    }
    {   // Add Var adds vectors
        Run r(Chain("decl p Vector3 object \"(1, 1, 1)\"\nnode 2 MakeVec3\ndefault 2 0 1\ndefault 2 1 2\ndefault 2 2 3\nnode 3 AddVar p\ndata 3 0 2 0\n", {3}));
        CHECK(r.vc->GetVariable("p").AsVec3().y == 3.0f && r.vc->GetVariable("p").AsVec3().z == 4.0f);
    }

    // ---- List nodes ----
    {
        // sort, insert, set, remove item, reverse, find
        Run r(Chain("decl a List object \"[3, 1, 2]\"\nnode 2 GetVar a\n"
                    "node 3 ListSort\ndata 3 0 2 0\n"
                    "node 4 ListInsert\ndata 4 0 2 0\ndefault 4 1 1\ndefault 4 2 9\n"
                    "node 5 ListSet\ndata 5 0 2 0\ndefault 5 1 0\ndefault 5 2 7\n"
                    "node 6 ListRemoveItem\ndata 6 0 2 0\ndefault 6 1 9\n"
                    "node 7 ListReverse\ndata 7 0 2 0\n"
                    "node 8 ListFind\ndata 8 0 2 0\ndefault 8 1 7\n"
                    "node 9 SetVar at\ndata 9 0 8 0\nnode 10 SetVar found\ndata 10 0 8 1\n"
                    "node 11 SetVar removed\ndata 11 0 6 0\n"
                    "node 12 ListFind\ndata 12 0 2 0\ndefault 12 1 99\nnode 13 SetVar missing\ndata 13 0 12 0\n",
                    {3, 4, 5, 6, 11, 7, 9, 10, 13}));
        CHECK(r.loaded);
        CHECK(r.Var("a") == "[3, 2, 7]");           // [1,2,3] -> [1,9,2,3] -> [7,9,2,3] -> [7,2,3] -> reversed
        CHECK(r.Var("at") == "2" && r.Var("found") == "true" && r.Var("removed") == "true");
        CHECK(r.Var("missing") == "-1");
        CHECK(g_errs.empty());
    }
    {
        // descending text sort, mixed list sorts by kind, clear
        Run r(Chain("decl t List object \"[\\\"b\\\", \\\"c\\\", \\\"a\\\"]\"\ndecl m List object \"[\\\"x\\\", 2, true, 1]\"\nnode 2 GetVar t\nnode 3 ListSort\ndata 3 0 2 0\ndefault 3 1 true\n"
                    "node 4 GetVar m\nnode 5 ListSort\ndata 5 0 4 0\nnode 6 NewList\nnode 7 ListAdd\ndata 7 0 6 0\ndefault 7 1 1\nnode 8 ListClear\ndata 8 0 6 0\nnode 9 SetVar cleared\ndata 9 0 6 0\n",
                    {3, 5, 7, 8, 9}));
        CHECK(r.Var("t") == "[c, b, a]");
        CHECK(r.Var("m") == "[1, 2, true, x]");
        CHECK(r.Var("cleared") == "[]");
        CHECK(g_errs.empty());
    }
    {
        // copy versus reference: a plain Set Var shares the list, Copy List does not; deep copies nested lists
        Run r(Chain("decl src List object \"[1, [2, 3]]\"\nnode 2 GetVar src\nnode 3 SetVar alias\ndata 3 0 2 0\n"
                    "node 4 ListCopy\ndata 4 0 2 0\nnode 5 SetVar shallow\ndata 5 0 4 0\n"
                    "node 6 ListCopy\ndata 6 0 2 0\ndefault 6 1 true\nnode 7 SetVar deep\ndata 7 0 6 0\n"
                    "node 8 ListAdd\ndata 8 0 2 0\ndefault 8 1 9\n", {3, 5, 7, 8}));
        auto src = r.vc->GetVariable("src").AsArray();
        CHECK(src->size() == 3);                                                          // the Add hit the original
        CHECK(r.vc->GetVariable("alias").AsArray() == src);                              // a reference
        CHECK(r.vc->GetVariable("shallow").AsArray() != src && r.vc->GetVariable("shallow").AsArray()->size() == 2);
        CHECK((*r.vc->GetVariable("shallow").AsArray())[1].AsArray() == (*src)[1].AsArray());   // inner list still shared
        CHECK(r.vc->GetVariable("deep").AsArray() != src);
        CHECK((*r.vc->GetVariable("deep").AsArray())[1].AsArray() != (*src)[1].AsArray());       // inner list copied
    }
    {
        // bounds: every out-of-range / NaN / huge index is reported with its node and changes nothing
        Run r(Chain("decl a List object \"[1, 2]\"\nnode 2 GetVar a\n"
                    "node 3 ListInsert\ndata 3 0 2 0\ndefault 3 1 5\ndefault 3 2 0\n"
                    "node 4 ListSet\ndata 4 0 2 0\ndefault 4 1 -1\ndefault 4 2 0\n"
                    "node 5 ListRemoveAt\ndata 5 0 2 0\ndefault 5 1 nan\n"
                    "node 6 ListGet\ndata 6 0 2 0\ndefault 6 1 1e30\nnode 7 SetVar got\ndata 7 0 6 0\n"
                    "node 8 ListInsert\ndata 8 0 2 0\ndefault 8 1 2\ndefault 8 2 3\n"
                    "node 9 ListSort\nnode 10 ListClear\n", {3, 4, 5, 7, 8, 9, 10}));
        CHECK(HasErr("ListInsert", "outside the list"));
        CHECK(HasErr("ListSet", "outside the list"));
        CHECK(HasErr("ListRemoveAt", "outside the list"));
        CHECK(HasErr("ListGet", "outside the list"));
        CHECK(HasErr("ListSort", "isn't a list"));                                       // nothing wired into a list pin
        CHECK(HasErr("ListClear", "isn't a list"));
        CHECK(r.Var("a") == "[1, 2, 3]");                                                // only the in-range insert at the end landed
    }
    {
        // a list pin fed a non-list never crashes any collection node
        Run r(Chain("node 2 Const 5\nnode 3 ListInsert\ndata 3 0 2 0\nnode 4 ListFind\ndata 4 0 2 0\nnode 5 ListCopy\ndata 5 0 2 0\n"
                    "node 6 MapClear\ndata 6 0 2 0\nnode 7 MapCopy\ndata 7 0 2 0\nnode 8 StructGet x\ndata 8 0 2 0\nnode 9 StructSet x\ndata 9 0 2 0\n"
                    "node 10 SetVar r1\ndata 10 0 4 0\nnode 11 SetVar r2\ndata 11 0 5 0\nnode 12 SetVar r3\ndata 12 0 7 0\nnode 13 SetVar r4\ndata 13 0 8 0\n",
                    {3, 6, 9, 10, 11, 12, 13}));
        CHECK(r.loaded);
        CHECK(r.Var("r1") == "-1" && r.Var("r2") == "[]" && r.Var("r3") == "{}");
        CHECK(!g_errs.empty());
    }

    // ---- Map and struct nodes ----
    {
        Run r(Chain("decl m Map object \"{a: 1, b: 2}\"\nnode 2 GetVar m\nnode 3 MapCount\ndata 3 0 2 0\nnode 4 SetVar n\ndata 4 0 3 0\n"
                    "node 5 MapCopy\ndata 5 0 2 0\nnode 6 MapClear\ndata 6 0 2 0\nnode 7 SetVar copy\ndata 7 0 5 0\nnode 8 SetVar after\ndata 8 0 3 0\n",
                    {4, 7, 6, 8}));
        CHECK(r.Var("n") == "2");
        CHECK(r.vc->GetVariable("copy").AsMap()->size() == 2);       // the copy kept its entries
        CHECK(r.vc->GetVariable("m").AsMap()->empty() && r.Var("after") == "0");
    }
    {
        Run r(Chain("struct Enemy hp:Number=100 \"pos:Vector3=(1, 2, 3)\"\ndecl e Enemy object\nnode 2 GetVar e\n"
                    "node 3 StructGet hp\ndata 3 0 2 0\nnode 4 SetVar before\ndata 4 0 3 0\n"
                    "node 5 StructSet hp\ndata 5 0 2 0\ndefault 5 1 55\n"
                    "node 6 StructSet hp\ndata 6 0 2 0\ndefault 6 1 oops\n"
                    "node 7 StructSet nope\ndata 7 0 2 0\ndefault 7 1 1\n"
                    "node 8 StructGet nope\ndata 8 0 2 0\nnode 9 SetVar bad\ndata 9 0 8 0\n"
                    "node 10 StructNew Enemy\nnode 11 SetVar fresh\ndata 11 0 10 0\nnode 12 StructNew Ghost\nnode 13 SetVar none\ndata 13 0 12 0\n",
                    {4, 5, 6, 7, 9, 11, 13}));
        CHECK(r.Var("before") == "100");
        CHECK(r.vc->GetVariable("e").AsMap()->at("hp").AsFloat() == 55.0f);           // the bad "oops" write changed nothing
        CHECK(HasErr("StructSet", "expected Number"));
        CHECK(HasErr("StructSet", "no field 'nope'"));
        CHECK(HasErr("StructGet", "no field 'nope'"));
        CHECK(HasErr("StructNew", "no struct named 'Ghost'"));
        CHECK(r.vc->GetVariable("fresh").AsMap()->at("pos").AsVec3().y == 2.0f);
    }
    {   // colours
        Run r(Chain("node 2 MakeColor\ndefault 2 0 1\ndefault 2 1 5\ndefault 2 2 -2\nnode 3 SetVar c\ndata 3 0 2 0\n"
                    "node 4 SplitColor\ndata 4 0 2 0\nnode 5 SetVar g\ndata 5 0 4 1\nnode 6 SetVar a\ndata 6 0 4 3\n", {3, 5, 6}));
        CHECK(r.Var("c") == "#ffff00ff");                   // clamped
        CHECK(r.vc->GetVariable("c").AsColor().g == 1.0f && r.vc->GetVariable("c").AsColor().b == 0.0f);
        CHECK(r.Var("g") == "1" && r.Var("a") == "1");
    }

    // ---- The checker: typed pins and element types ----
    {
        auto count = [](const std::vector<vs::VsIssue>& v, int sev) { int n = 0; for (const auto& i : v) if (i.severity == sev) ++n; return n; };
        // a Vector3 variable into a Number input is refused; a Number one is fine
        const std::string base = "decl p Vector3 object\ndecl n Number object\nnode 1 OnStart\nentry OnStart 1\nnode 2 GetVar p\nnode 3 GetVar n\nnode 4 Add\nexec 1 0 5\nnode 5 Print\n";
        CHECK(count(vs::ValidateGraphText(base + "data 4 0 2 0\ndata 5 0 4 0\n"), 0) >= 1);
        CHECK(count(vs::ValidateGraphText(base + "data 4 0 3 0\ndata 5 0 4 0\n"), 0) == 0);
        // an item of the wrong type into a typed list
        const std::string lst = "decl nums List<Number> object\ndecl pts List<Vector3> object\nnode 1 OnStart\nentry OnStart 1\nnode 2 GetVar nums\nnode 3 MakeVec3\n"
                                "node 4 ListAdd\nexec 1 0 4\ndata 4 0 2 0\ndata 4 1 3 0\n";
        auto bad = vs::ValidateGraphText(lst);
        CHECK(count(bad, 0) >= 1);
        bool named = false;
        for (const auto& i : bad) if (i.message.find("Vector3") != std::string::npos && i.message.find("Number") != std::string::npos) named = true;
        CHECK(named);
        CHECK(count(vs::ValidateGraphText("decl nums List<Number> object\nnode 1 OnStart\nentry OnStart 1\nnode 2 GetVar nums\nnode 3 Const 4\n"
                                          "node 4 ListAdd\nexec 1 0 4\ndata 4 0 2 0\ndata 4 1 3 0\n"), 0) == 0);
        // a typed New List
        CHECK(count(vs::ValidateGraphText("node 1 OnStart\nentry OnStart 1\nnode 2 NewList Number\nnode 3 MakeVec3\nnode 4 ListAdd\nexec 1 0 4\ndata 4 0 2 0\ndata 4 1 3 0\n"), 0) >= 1);
        // List Get yields the element type: List<Vector3> -> a Number input is refused
        CHECK(count(vs::ValidateGraphText("decl pts List<Vector3> object\nnode 1 OnStart\nentry OnStart 1\nnode 2 GetVar pts\nnode 3 ListGet\nnode 4 Add\nnode 5 Print\nexec 1 0 5\n"
                                          "data 3 0 2 0\ndata 4 0 3 0\ndata 5 0 4 0\n"), 0) >= 1);
        // a struct field's type reaches Set Field
        CHECK(count(vs::ValidateGraphText("struct E hp:Number\ndecl e E object\nnode 1 OnStart\nentry OnStart 1\nnode 2 GetVar e\nnode 3 MakeVec3\nnode 4 StructSet hp\n"
                                          "exec 1 0 4\ndata 4 0 2 0\ndata 4 1 3 0\n"), 0) >= 1);
        // a graph whose declaration doesn't load says so
        const auto broken = vs::ValidateGraphText("decl x Banana object\n");
        CHECK(broken.size() == 1 && broken[0].message.find("can't be loaded") != std::string::npos);
        // a declared variable that is only read isn't "never set": it has a default
        CHECK(count(vs::ValidateGraphText("decl n Number object 1\nnode 1 OnStart\nentry OnStart 1\nnode 2 GetVar n\nnode 3 Print\nexec 1 0 3\ndata 3 0 2 0\n"), 1) == 0);
        // colour and vector pins connect, colour into a Number doesn't
        CHECK(vs::PinCompatible(vs::PinType::Vec3, vs::PinType::Color) && vs::PinCompatible(vs::PinType::Color, vs::PinType::Vec3));
        CHECK(!vs::PinCompatible(vs::PinType::Color, vs::PinType::Number));
        vs::FunctionSig sig; std::string err;
        CHECK(vs::ParseFunctionSignature("Tint(c: Color) -> out: Color", sig, &err) && sig.params[0].type == vs::PinType::Color);
    }

    // ---- Round trips: scene file, a play-time save ----
    {
        const std::string text = "struct S a:Number=2\ndecl bag List<Number> object \"[1, 2]\"\ndecl s S object\ndecl c Color scene \"#102030\"\nnode 1 OnStart\nentry OnStart 1\n";
        Scene a("round");
        a.CreateGameObject("G")->AddComponent<VisualScriptComponent>()->LoadFromText(text);
        const std::string ser = SceneSerializer::Serialize(a);
        Scene b("round2");
        std::string err;
        CHECK(SceneSerializer::Deserialize(b, ser, &err));
        auto* vb = b.Find("G") ? b.Find("G")->GetComponent<VisualScriptComponent>() : nullptr;
        CHECK(vb && vb->Graph() && vb->Graph()->Decls().size() == 3 && vb->Graph()->Structs().size() == 1);
        if (vb && vb->Graph()) {
            CHECK(vs::VsToLiteral(vb->Graph()->FindDecl("bag", vs::VarScope::Object)->value) == "[1, 2]");
            CHECK(vb->Graph()->FindDecl("c", vs::VarScope::Scene)->value.IsColor());
        }
    }
    {   // a prefab instance keeps the declarations and starts with its own copy of every default
        const std::string text = "decl bag List<Number> object \"[1, 2]\"\nnode 1 OnStart\nentry OnStart 1\n";
        Scene a("pf");
        GameObject* src = a.CreateGameObject("Proto");
        src->AddComponent<VisualScriptComponent>()->LoadFromText(text);
        const std::string prefab = SceneSerializer::SerializeObject(*src);
        Scene b("pf2");
        std::string err;
        GameObject* x = SceneSerializer::InstantiateFromText(b, prefab, &err);
        GameObject* y = SceneSerializer::InstantiateFromText(b, prefab, &err);
        CHECK(x && y);
        b.Start();
        auto* vx = x ? x->GetComponent<VisualScriptComponent>() : nullptr;
        auto* vy = y ? y->GetComponent<VisualScriptComponent>() : nullptr;
        CHECK(vx && vy && vx->GetVariable("bag").AsString() == "[1, 2]");
        if (vx && vy) {
            vx->GetVariable("bag").AsArray()->push_back(vs::VsValue(3.0f));
            CHECK(vy->GetVariable("bag").AsArray()->size() == 2);
        }
    }
    {   // a deterministic fuzz: random text never crashes the parser and what parses prints back to the same value
        unsigned long long seed = 12345;
        auto rnd = [&]() { seed = seed * 6364136223846793005ULL + 1442695040888963407ULL; return (unsigned)(seed >> 33); };
        const std::string alphabet = "[]{}()\",:#@.-+ 0123456789abcdeftrunalsE\\\n";
        int parsed = 0;
        for (int i = 0; i < 20000; ++i) {
            std::string t; const int n = (int)(rnd() % 24);
            for (int k = 0; k < n; ++k) t += alphabet[rnd() % alphabet.size()];
            vs::VsValue v; std::string err;
            if (vs::VsParseLiteral(t, v, &err)) {
                ++parsed;
                vs::VsValue w;
                CHECK(vs::VsParseLiteral(vs::VsToLiteral(v), w) && vs::VsToLiteral(w) == vs::VsToLiteral(v));
            }
            (void)vs::NodeGraph::Parse("decl x List<Number> object \"" + t + "\"\n", &err);
            (void)vs::NodeGraph::Parse("struct S " + t + "\n", &err);
        }
        CHECK(parsed > 100);
    }
    {
        const fs::path dir = fs::temp_directory_path() / "okay_test_vs_variables";
        fs::remove_all(dir); fs::create_directories(dir);
        Save::SetBaseDir(dir.string());
        Scene s("L");
        auto* vc = s.CreateGameObject("G")->AddComponent<VisualScriptComponent>();
        vc->LoadFromText("okayvs 1\n");
        s.Start();
        vs::VsValue list, map;
        CHECK(vs::VsParseLiteral("[(1, 2, 3), (4, 5, 6)]", list) && vs::VsParseLiteral("{hp: 5, tint: #ff0000, \"two words\": [1, \"x\"]}", map));
        vc->SetVariable("pts", list);
        vc->SetVariable("info", map);
        vc->SetVariable("tint", vs::VsValue::Color(0.2f, 0.4f, 0.6f, 1.0f));
        std::string err;
        CHECK(SaveGame::Save(s, "v", "vars", &err));
        vc->SetVariable("pts", vs::VsValue(0.0f)); vc->SetVariable("info", vs::VsValue(0.0f)); vc->SetVariable("tint", vs::VsValue(0.0f));
        CHECK(SaveGame::LoadNow(s, "v", &err));
        auto* back = s.Find("G") ? s.Find("G")->GetComponent<VisualScriptComponent>() : nullptr;
        CHECK(back != nullptr);
        if (back) {
            CHECK(vs::VsToLiteral(back->GetVariable("pts")) == "[(1, 2, 3), (4, 5, 6)]");
            CHECK(back->GetVariable("info").IsMap() && back->GetVariable("info").AsMap()->at("hp").AsFloat() == 5.0f);
            CHECK(back->GetVariable("info").AsMap()->at("tint").IsColor());
            CHECK(back->GetVariable("tint").IsColor() && std::fabs(back->GetVariable("tint").AsColor().g - 0.4f) < 0.01f);
        }
        fs::remove_all(dir);
    }

    // ---- The shipped example: an array of vectors, a map and a struct, looped over, mutated and printed ----
    {
        std::ifstream f(std::string(OKAY_SOURCE_DIR) + "/examples/visual_script/declared_variables.okayvs");
        std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        CHECK(!text.empty());
        std::vector<std::string> lines;
        auto sinkKeep = Log::sink;
        Log::sink = [](Log::Level, const std::string& m) { g_printed.push_back(m); };
        Run r(text);
        Log::sink = sinkKeep;
        CHECK(r.loaded);
        CHECK(g_errs.empty());
        CHECK(vs::ValidateGraphText(text).empty());                       // the checker finds nothing to say
        CHECK(r.Var("total") == "7");                                      // 0 + 1 + 2 + 4 (the x of each vector)
        CHECK(r.vc->GetVariable("stats").AsMap()->at("coins").AsFloat() == 7.0f);
        CHECK(r.vc->GetVariable("boss").AsMap()->at("hp").AsFloat() == 55.0f);
        CHECK(vs::VsToLiteral(r.vc->GetVariable("route")) == "[(4, 0, 0), (2, 0, 1), (1, 5, 1), (0, 0, 0)]");   // inserted, then sorted largest first
        bool printedRoute = false, printedStats = false;
        for (const auto& m : g_printed) {
            if (m.find("route=[(4, 0, 0), (2, 0, 1), (1, 5, 1), (0, 0, 0)]  total=7") != std::string::npos) printedRoute = true;
            if (m.find("stats={coins: 7, kills: 0}") != std::string::npos && m.find("boss={hp: 55,") != std::string::npos) printedStats = true;
        }
        CHECK(printedRoute && printedStats);
    }

    // ---- Hostile graph text never crashes the loader ----
    {
        const char* nasty[] = {
            "decl\n", "decl x\n", "decl x Number\n", "decl x Number object 1 2 3\n", "struct\n", "struct A\n", "struct A :Number\n", "struct A x:\n",
            "struct A x:Number=\n", "struct A \"x:List<\"\n", "decl x List<List<Number>> object\n", "decl x Number object \"\"\n",
            "decl x List object \"[[[[[[[[[[[[[[[[[[[[[[[[1]]]]]]]]]]]]]]]]]]]]]]]]\"\n", "decl x Text object \"\\\n",
        };
        for (const char* n : nasty) { std::string err; (void)vs::NodeGraph::Parse(n, &err); (void)vs::ValidateGraphText(n); }
        CHECK(true);
    }

    vs::SetRuntimeErrorHook(nullptr);
    Log::sink = sink;
    TEST_MAIN_RESULT();
}
