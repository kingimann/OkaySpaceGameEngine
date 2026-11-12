// Visual-script custom events with typed parameters and dispatchers: declarations, typed raise
// and handle, coercion and refusal with the node id, ordering, bind / unbind, cleanup when either
// object is destroyed or the scene is cleared, reentrancy cap, a Delay inside a handler, the
// debugger's view, the checker (graph and scene), scene / prefab / save round trips, hostile text
// and a fuzz sweep.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Core/SaveGame.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/VisualScript/NodeGraph.hpp"
#include "okay/VisualScript/NodesEvents.hpp"
#include "okay/VisualScript/VsDebug.hpp"
#include "okay/VisualScript/VsDecl.hpp"
#include "okay/VisualScript/VsEvents.hpp"
#include "okay/VisualScript/VsTypes.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace okay;
namespace fs = std::filesystem;

static std::vector<vs::VsRuntimeError> g_errs;
static std::vector<std::string> g_printed;
static void OnErr(const vs::VsRuntimeError& e) { g_errs.push_back(e); }
static bool HasErr(const std::string& type, const std::string& part, int node = -1) {
    for (const auto& e : g_errs)
        if (e.nodeType == type && e.message.find(part) != std::string::npos && e.fileId >= 0 && (node < 0 || e.fileId == node)) return true;
    return false;
}
static bool Printed(const std::string& s) { for (const auto& p : g_printed) if (p == s) return true; return false; }
static int IndexOf(const std::string& s) { for (std::size_t i = 0; i < g_printed.size(); ++i) if (g_printed[i] == s) return (int)i; return -1; }

static VisualScriptComponent* Make(Scene& s, const char* name, const std::string& text, bool expectLoad = true) {
    GameObject* go = s.CreateGameObject(name);
    auto* vc = go->AddComponent<VisualScriptComponent>();
    std::string err;
    const bool ok = vc->LoadFromText(text, &err);
    if (ok != expectLoad) std::printf("  load of '%s': %s\n", name, err.c_str());
    CHECK(ok == expectLoad);
    return vc;
}
static std::size_t Issues(const std::string& text, int severity = -1) {
    std::size_t n = 0;
    for (const auto& i : vs::ValidateGraphText(text)) if (severity < 0 || i.severity == severity) ++n;
    return n;
}
static bool IssueHas(const std::string& text, const std::string& part, int severity = -1) {
    for (const auto& i : vs::ValidateGraphText(text)) if ((severity < 0 || i.severity == severity) && i.message.find(part) != std::string::npos) return true;
    return false;
}

// A listener: event Hit(Number amount, Text who); the handler stores both in variables.
static const char* kListener =
    "event Hit(Number amount, Text who)\n"
    "node 1 OnCustomEvent Hit\nentry OnCustomEvent:Hit 1\n"
    "node 2 SetVar got\nnode 3 SetVar from\n"
    "exec 1 0 2\nexec 2 0 3\ndata 2 0 1 0\ndata 3 0 1 1\n";

int main() {
    RUN_SUITE("vs_events");
    vs::SetRuntimeErrorHook(&OnErr);
    auto sink = Log::sink;
    Log::sink = [](Log::Level, const std::string& m) { g_printed.push_back(m); };
    vs::VsEventBindings::Get().Clear();

    // ---- Declarations: the line format, the limits, hostile text ----
    {
        vs::EventLine el; std::string err;
        CHECK(vs::ParseEventLine("Hit(Number amount, Object who)", el, &err) && el.name == "Hit" && el.params.size() == 2);
        CHECK(el.params[1].type == "Object" && el.params[1].name == "who");
        CHECK(vs::EventLineText(el) == "Hit(Number amount, Object who)");
        CHECK(vs::ParseEventLine("Ping()", el, &err) && el.params.empty());
        CHECK(vs::ParseEventLine("  Ping (  )  ", el, &err) && el.name == "Ping");
        const char* bad[] = {"", "Hit", "Hit(", "Hit)", "(Number a)", "Hit(Number)", "Hit(Number a,)", "Hit(, Number a)", "Hit(Number a, Text a)",
                             "1Hit(Number a)", "Hit(Number 1a)", "Hit Number a", "Hit(a)", "H it(Number a)"};
        for (const char* b : bad) { CHECK(!vs::ParseEventLine(b, el, &err)); CHECK(!err.empty()); }
        std::string many = "Big(";
        for (int i = 0; i < 33; ++i) many += std::string(i ? ", " : "") + "Number p" + std::to_string(i);
        CHECK(!vs::ParseEventLine(many + ")", el, &err));
        CHECK(vs::ParseEventLine(many.substr(0, many.rfind(',')) + ")", el, &err));   // 32 is fine

        std::string perr;
        auto g = vs::NodeGraph::Parse("struct P x:Number\nevent Hit(Number amount, P who, List<Number> xs)\ndispatcher Died(Object o)\n", &perr);
        CHECK(g && g->Events().size() == 2);
        if (g) {
            const vs::EventDecl* h = g->FindEvent("Hit");
            CHECK(h && !h->dispatcher && h->params.size() == 3 && h->params[1].type.IsStruct() && h->params[2].type.elem == vs::PinType::Number);
            CHECK(g->FindEvent("Died") && g->FindEvent("Died")->dispatcher);
            CHECK(vs::EventParamsText(*h) == "(Number amount, P who, List<Number> xs)");
        }
        const char* badGraphs[] = {"event Hit(Number a)\nevent Hit(Text b)\n", "event Hit(Number a)\ndispatcher Hit()\n", "event Hit(Wat a)\n",
                                   "event Hit(P a)\n", "event\n", "dispatcher\n", "event Hit(Number a\n", "event Hit(Number a, Number a)\n"};
        for (const char* b : badGraphs) { std::string e; CHECK(!vs::NodeGraph::Parse(b, &e)); CHECK(e.find("line 1") == 0 || e.find("line 2") == 0); }
        CHECK(vs::NodeGraph::Parse("event Hit(Number a) # note\n", &perr) != nullptr);
        std::string eventsMany;
        for (int i = 0; i < 1001; ++i) eventsMany += "event E" + std::to_string(i) + "()\n";
        CHECK(!vs::NodeGraph::Parse(eventsMany, &perr));
        vs::EventDecl a, b;
        std::string why;
        a.params.push_back({"x", {vs::PinType::Number, vs::PinType::Any, ""}});
        b.params.push_back({"x", {vs::PinType::Vec3, vs::PinType::Any, ""}});
        CHECK(!vs::EventSigCompatible(a, b, &why) && !why.empty());
        b.params[0].type.base = vs::PinType::Bool;
        CHECK(vs::EventSigCompatible(a, b));
        b.params.push_back(b.params[0]);
        CHECK(!vs::EventSigCompatible(a, b, &why));
        std::string d, h;
        CHECK(vs::ParseBindText("Died -> OnDied", d, h) && d == "Died" && h == "OnDied");
        CHECK(vs::ParseBindText("Died OnDied", d, h) && d == "Died" && h == "OnDied");
        CHECK(!vs::ParseBindText("Died", d, h) && d == "Died");
        CHECK(!vs::ParseBindText("", d, h));
    }

    // ---- Raise: typed delivery across objects, coercion, refusal with the node id ----
    {
        Scene s("raise");
        auto* b = Make(s, "B", kListener);
        auto* a = Make(s, "A",
            "event Hit(Number amount, Text who)\n"
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 RaiseEvent Hit\ndefault 2 0 \"B\"\ndefault 2 1 \"7\"\ndefault 2 2 4.5\n"   // "7" -> Number 7, 4.5 -> Text "4.5"
            "exec 1 0 2\n");
        g_errs.clear();
        s.Start();
        CHECK(a && b && g_errs.empty());
        CHECK(b->GetVariable("got").IsNumber() && b->GetVariable("got").AsFloat() == 7.0f);
        CHECK(b->GetVariable("from").IsString() && b->GetVariable("from").AsString() == "4.5");
    }
    {   // a value that can't fit is refused with the raising node's id, the event is not raised, the chain goes on
        Scene s("refuse");
        auto* b = Make(s, "B", kListener);
        Make(s, "A",
            "event Hit(Number amount, Text who)\n"
            "node 1 OnStart\nentry OnStart 1\n"
            "node 5 MakeVec3\nnode 2 RaiseEvent Hit\ndefault 2 0 \"B\"\ndefault 2 2 \"x\"\ndata 2 1 5 0\n"   // a Vector3 into Number
            "node 3 Print\ndefault 3 0 \"after\"\nexec 1 0 2\nexec 2 0 3\n");
        g_errs.clear(); g_printed.clear();
        s.Start();
        CHECK(HasErr("RaiseEvent", "parameter 1 'amount'", 2));
        CHECK(!b->GetVariable("got").IsNumber() || b->GetVariable("got").AsFloat() == 0.0f);
        CHECK(!b->Graph()->HasSuspended() && Printed("after"));
    }
    vs::VsEventBindings::Get().Clear();
    {   // undeclared in the raising graph, raising a dispatcher, wrong target kinds
        Scene s("undeclared");
        Make(s, "B", kListener);
        Make(s, "NoEvents", "node 1 OnStart\nentry OnStart 1\n");
        Make(s, "A",
            "event Hit(Number amount, Text who)\ndispatcher Died()\nnode 1 OnStart\nentry OnStart 1\n"
            "node 2 RaiseEvent Nope\nnode 3 RaiseEvent Died\nnode 4 RaiseEvent Hit\ndefault 4 0 \"NoEvents\"\nnode 5 RaiseEvent Hit\ndefault 5 0 \"Ghost\"\n"
            "exec 1 0 2\nexec 2 0 3\nexec 3 0 4\nexec 4 0 5\n");
        g_errs.clear();
        s.Start();
        CHECK(HasErr("RaiseEvent", "no event named 'Nope' is declared", 2));
        CHECK(HasErr("RaiseEvent", "is a dispatcher: use Call Dispatcher", 3));
        CHECK(HasErr("RaiseEvent", "'NoEvents' has no Visual Script that declares event 'Hit'", 4));
        CHECK(HasErr("RaiseEvent", "no object named 'Ghost'", 5));
    }

    // ---- Ordering: handlers run in order, depth first; a handler raising an event finishes it before going on ----
    {
        Scene s("order");
        Make(s, "A",
            "event One()\nevent Two()\n"
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 RaiseEvent One\nnode 3 Print\ndefault 3 0 \"start-done\"\n"
            "exec 1 0 2\nexec 2 0 3\n"
            // first handler of One: print, raise Two, print
            "node 10 OnCustomEvent One\nentry OnCustomEvent:One 10\nnode 11 Print\ndefault 11 0 \"one-a1\"\nnode 12 RaiseEvent Two\nnode 13 Print\ndefault 13 0 \"one-a2\"\n"
            "exec 10 0 11\nexec 11 0 12\nexec 12 0 13\n"
            // second handler of One (added later: runs second)
            "node 20 OnCustomEvent One\nentry OnCustomEvent:One 20\nnode 21 Print\ndefault 21 0 \"one-b\"\nexec 20 0 21\n"
            "node 30 OnCustomEvent Two\nentry OnCustomEvent:Two 30\nnode 31 Print\ndefault 31 0 \"two\"\nexec 30 0 31\n");
        g_printed.clear();
        s.Start();
        const int a1 = IndexOf("one-a1"), two = IndexOf("two"), a2 = IndexOf("one-a2"), bb = IndexOf("one-b"), done = IndexOf("start-done");
        CHECK(a1 >= 0 && a1 < two && two < a2 && a2 < bb && bb < done);
        CHECK(vs::VsEventDepth() == 0);
    }

    // ---- Parameters are per call: a nested raise of the same event doesn't change the outer handler's values ----
    {
        Scene s("nested");
        Make(s, "A",
            "event Step(Number n)\n"
            "node 1 OnStart\nentry OnStart 1\nnode 2 RaiseEvent Step\ndefault 2 1 1\nexec 1 0 2\n"
            "node 10 OnCustomEvent Step\nentry OnCustomEvent:Step 10\n"
            "node 11 Branch\nnode 12 Compare <\nnode 13 Const 2\ndata 12 0 10 0\ndata 12 1 13 0\ndata 11 0 12 0\n"
            "node 14 RaiseEvent Step\nnode 15 Add\nnode 16 Const 1\ndata 15 0 10 0\ndata 15 1 16 0\ndata 14 1 15 0\n"
            "node 17 ToString\ndata 17 0 10 0\nnode 18 Print\ndata 18 0 17 0\n"
            "exec 10 0 11\nexec 11 0 14\nexec 11 1 18\nexec 14 0 18\n");
        g_printed.clear(); g_errs.clear();
        s.Start();
        // Step(1) -> raises Step(2) -> raises nothing more (2 < 2 false) and prints 2; then Step(1)'s print is 1.
        CHECK(g_printed.size() == 2 && g_printed[0] == "2" && g_printed[1] == "1");
    }

    // ---- Reentrancy: an event that raises itself stops at the depth cap with a clear error on the node ----
    {
        Scene s("loop");
        auto* a = Make(s, "A",
            "event Again(Number n)\n"
            "node 1 OnStart\nentry OnStart 1\nnode 2 RaiseEvent Again\nexec 1 0 2\n"
            "node 10 OnCustomEvent Again\nentry OnCustomEvent:Again 10\nnode 11 AddVar count\nnode 12 Const 1\ndata 11 0 12 0\n"
            "node 13 RaiseEvent Again\nexec 10 0 11\nexec 11 0 13\n");
        g_errs.clear();
        s.Start();
        CHECK(HasErr("RaiseEvent", "more than 32 deep", 13));
        CHECK(a->GetVariable("count").AsFloat() == 32.0f);   // the handler ran 32 times, the 33rd raise was refused
        CHECK(vs::VsEventDepth() == 0);
        // and after that the graph still works: raising from outside runs the handler again
        g_errs.clear();
        const float before = a->GetVariable("count").AsFloat();
        a->RunTypedEvent("Again", {vs::VsValue(0.0f)}, nullptr, "test");
        CHECK(a->GetVariable("count").AsFloat() == before + 33.0f);   // the outside call's handler plus 32 nested ones
    }

    // ---- Dispatchers: bind, call in bind order, unbind, duplicates, function handlers, typed params ----
    {
        vs::VsEventBindings::Get().Clear();
        Scene s("disp");
        auto* boss = Make(s, "Boss",
            "dispatcher Died(Number code, Text who)\nevent Fire()\n"
            "node 1 OnCustomEvent Fire\nentry OnCustomEvent:Fire 1\nnode 2 CallDispatcher Died\ndefault 2 1 9\ndefault 2 2 \"boss\"\nexec 1 0 2\n"
            "node 3 OnStart\nentry OnStart 3\nnode 4 Print\ndefault 4 0 \"boss-start\"\nexec 3 0 4\n");
        // Listener 1: handler is a custom event; listener 2: a function. Both bind in OnStart.
        auto* l1 = Make(s, "L1",
            "dispatcher Died(Number code, Text who)\nevent Mourn(Number code, Text who)\n"
            "node 1 OnStart\nentry OnStart 1\nnode 2 BindEvent \"Died -> Mourn\"\ndefault 2 0 \"Boss\"\nnode 3 BindEvent \"Died -> Mourn\"\ndefault 3 0 \"Boss\"\nexec 1 0 2\nexec 2 0 3\n"   // bound twice = once
            "node 10 OnCustomEvent Mourn\nentry OnCustomEvent:Mourn 10\nnode 11 SetVar code\nnode 12 SetVar who\nnode 13 Print\ndefault 13 0 \"l1-mourns\"\n"
            "exec 10 0 11\nexec 11 0 12\nexec 12 0 13\ndata 11 0 10 0\ndata 12 0 10 1\n");
        auto* l2 = Make(s, "L2",
            "dispatcher Died(Number code, Text who)\n"
            "node 1 OnStart\nentry OnStart 1\nnode 2 BindEvent \"Died -> Cheer\"\ndefault 2 0 \"Boss\"\nexec 1 0 2\n"
            "node 10 FunctionStart \"Cheer(c: Number, w: Text)\"\nnode 11 SetVar fcode\nnode 12 SetVar fwho\nnode 13 Print\ndefault 13 0 \"l2-cheers\"\n"
            "exec 10 0 11\nexec 11 0 12\nexec 12 0 13\ndata 11 0 10 0\ndata 12 0 10 1\n");
        g_errs.clear(); g_printed.clear();
        s.Start();
        const std::uint64_t bid = boss->gameObject->InstanceId();
        CHECK(g_errs.empty());
        CHECK(vs::VsEventBindings::Get().Count() == 2 && vs::VsEventBindings::Get().CountFor(bid, "Died") == 2);
        boss->RunTypedEvent("Fire", {}, nullptr, "test");
        CHECK(g_errs.empty());
        CHECK(l1->GetVariable("code").AsFloat() == 9.0f && l1->GetVariable("who").AsString() == "boss");
        CHECK(l2->GetVariable("fcode").AsFloat() == 9.0f && l2->GetVariable("fwho").AsString() == "boss");
        CHECK(IndexOf("l1-mourns") >= 0 && IndexOf("l1-mourns") < IndexOf("l2-cheers"));   // bind order
        // Unbind: a graph that unbinds on an event
        const std::uint64_t l1id = l1->gameObject->InstanceId();
        CHECK(vs::VsEventBindings::Get().Unbind(bid, "Died", l1id, "Mourn"));
        CHECK(!vs::VsEventBindings::Get().Unbind(bid, "Died", l1id, "Mourn"));   // twice: nothing left
        g_printed.clear();
        boss->RunTypedEvent("Fire", {}, nullptr, "test");
        CHECK(!Printed("l1-mourns") && Printed("l2-cheers"));
        CHECK(vs::VsEventBindings::Get().Count() == 1);
        // the owner is destroyed: its bindings go, the listener keeps running
        s.Destroy(boss->gameObject);
        s.Update(0.016f);
        CHECK(vs::VsEventBindings::Get().Count() == 0);
        CHECK(vs::VsEventBindings::Get().CountListening(l2->gameObject->InstanceId()) == 0);
    }
    {   // Unbind node, Call Dispatcher results, listener destroyed mid-life, scene unloaded
        vs::VsEventBindings::Get().Clear();
        Scene s("disp2");
        auto* boss = Make(s, "Boss",
            "dispatcher Ping()\nevent Fire()\nnode 1 OnCustomEvent Fire\nentry OnCustomEvent:Fire 1\nnode 2 CallDispatcher Ping\nnode 3 Print\ndefault 3 0 \"called\"\n"
            "node 4 ToString\ndata 4 0 2 0\nnode 5 Print\ndata 5 0 4 0\nexec 1 0 2\nexec 2 0 5\n");
        auto mk = [&](const char* name, const char* extra) {
            return Make(s, name, std::string("dispatcher Ping()\nevent On()\nevent Off()\nnode 1 OnCustomEvent On\nentry OnCustomEvent:On 1\nnode 2 BindEvent \"Ping -> Hear\"\ndefault 2 0 \"Boss\"\nexec 1 0 2\n"
                "node 3 OnCustomEvent Off\nentry OnCustomEvent:Off 3\nnode 4 UnbindEvent \"Ping -> Hear\"\ndefault 4 0 \"Boss\"\nexec 3 0 4\n"
                "event Hear()\nnode 5 OnCustomEvent Hear\nentry OnCustomEvent:Hear 5\nnode 6 AddVar heard\nnode 7 Const 1\ndata 6 0 7 0\nexec 5 0 6\n") + extra);
        };
        auto* x = mk("X", ""); auto* y = mk("Y", ""); auto* z = mk("Z", "");
        s.Start();
        for (auto* v : {x, y, z}) v->RunTypedEvent("On", {}, nullptr, "t");
        CHECK(vs::VsEventBindings::Get().Count() == 3);
        g_printed.clear();
        boss->RunTypedEvent("Fire", {}, nullptr, "t");
        CHECK(g_printed.size() == 1 && g_printed[0] == "3");   // the node says how many handlers ran
        CHECK(x->GetVariable("heard").AsFloat() == 1.0f && y->GetVariable("heard").AsFloat() == 1.0f && z->GetVariable("heard").AsFloat() == 1.0f);
        y->RunTypedEvent("Off", {}, nullptr, "t");
        CHECK(vs::VsEventBindings::Get().Count() == 2);
        // a listener is destroyed: its binding goes, the rest still hear
        const std::uint64_t zid = z->gameObject->InstanceId();
        s.Destroy(z->gameObject);
        s.Update(0.016f);
        CHECK(vs::VsEventBindings::Get().Count() == 1 && vs::VsEventBindings::Get().CountListening(zid) == 0);
        g_printed.clear();
        boss->RunTypedEvent("Fire", {}, nullptr, "t");
        CHECK(g_printed.size() == 1 && g_printed[0] == "1");
        CHECK(x->GetVariable("heard").AsFloat() == 2.0f && y->GetVariable("heard").AsFloat() == 1.0f);
        // the scene is cleared: nothing is left
        s.Clear();
        CHECK(vs::VsEventBindings::Get().Count() == 0);
    }
    {   // a scene that goes away with live bindings (no Clear): the table is cleaned by ~Scene
        vs::VsEventBindings::Get().Clear();
        {
            Scene s("gone");
            Make(s, "Boss", "dispatcher Ping()\n");
            Make(s, "L", "dispatcher Ping()\nevent Hear()\nnode 1 OnStart\nentry OnStart 1\nnode 2 BindEvent \"Ping -> Hear\"\ndefault 2 0 \"Boss\"\nexec 1 0 2\n"
                         "node 5 OnCustomEvent Hear\nentry OnCustomEvent:Hear 5\n");
            s.Start();
            CHECK(vs::VsEventBindings::Get().Count() == 1);
        }
        CHECK(vs::VsEventBindings::Get().Count() == 0);
    }
    {   // bind errors: undeclared dispatcher, missing handler, wrong shape, mismatched owner, no owner
        vs::VsEventBindings::Get().Clear();
        Scene s("binderr");
        Make(s, "Boss", "dispatcher Died(Number code)\n");
        Make(s, "Other", "event Died(Number code)\n");   // an event, not a dispatcher
        Make(s, "Wrong", "dispatcher Died(Text a, Text b)\n");
        Make(s, "L",
            "dispatcher Died(Number code)\nevent Mourn(Number code)\nevent TwoArgs(Number a, Number b)\nevent Plain()\n"
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 BindEvent \"Nope -> Mourn\"\ndefault 2 0 \"Boss\"\n"                       // undeclared dispatcher
            "node 3 BindEvent \"Died -> Missing\"\ndefault 3 0 \"Boss\"\n"                      // handler is nothing
            "node 4 BindEvent \"Died -> TwoArgs\"\ndefault 4 0 \"Boss\"\n"                      // wrong parameter count
            "node 5 BindEvent \"Died -> Mourn\"\ndefault 5 0 \"Other\"\n"                       // owner declares an event, not the dispatcher
            "node 6 BindEvent \"Died -> Mourn\"\ndefault 6 0 \"Wrong\"\n"                       // owner declares it differently
            "node 7 BindEvent \"Died -> Mourn\"\ndefault 7 0 \"Ghost\"\n"                       // no such object
            "node 8 BindEvent \"Died\"\n"                                                         // no handler picked
            "exec 1 0 2\nexec 2 0 3\nexec 3 0 4\nexec 4 0 5\nexec 5 0 6\nexec 6 0 7\nexec 7 0 8\n");
        g_errs.clear();
        s.Start();
        CHECK(HasErr("BindEvent", "no dispatcher named 'Nope'", 2));
        CHECK(HasErr("BindEvent", "'Missing' is neither an event nor a function", 3));
        CHECK(HasErr("BindEvent", "doesn't fit dispatcher 'Died'", 4));
        CHECK(HasErr("BindEvent", "'Other' has no Visual Script that declares dispatcher 'Died'", 5));
        CHECK(HasErr("BindEvent", "declares dispatcher 'Died' differently", 6));
        CHECK(HasErr("BindEvent", "no object named 'Ghost'", 7));
        CHECK(HasErr("BindEvent", "no handler is picked", 8));
        CHECK(vs::VsEventBindings::Get().Count() == 0);
    }
    {   // reentrancy through a dispatcher: the handler calls the dispatcher that ran it
        vs::VsEventBindings::Get().Clear();
        Scene s("disploop");
        auto* a = Make(s, "A",
            "dispatcher Tick()\nevent Hear()\nnode 1 OnStart\nentry OnStart 1\nnode 2 BindEvent \"Tick -> Hear\"\nnode 3 CallDispatcher Tick\nexec 1 0 2\nexec 2 0 3\n"
            "node 5 OnCustomEvent Hear\nentry OnCustomEvent:Hear 5\nnode 6 AddVar n\nnode 7 Const 1\ndata 6 0 7 0\nnode 8 CallDispatcher Tick\nexec 5 0 6\nexec 6 0 8\n");
        g_errs.clear();
        s.Start();
        CHECK(HasErr("CallDispatcher", "more than 32 deep", 8));
        CHECK(a->GetVariable("n").AsFloat() == 32.0f && vs::VsEventDepth() == 0);
    }

    // ---- A Delay inside a handler: the parameters are still there when it carries on ----
    {
        Scene s("delay");
        auto* a = Make(s, "A",
            "event Slow(Number n)\nnode 1 OnCustomEvent Slow\nentry OnCustomEvent:Slow 1\nnode 2 Delay 0.1\nnode 3 SetVar later\ndata 3 0 1 0\nexec 1 0 2\nexec 2 0 3\n"
            "node 4 OnStart\nentry OnStart 4\nnode 5 RaiseEvent Slow\ndefault 5 1 42\nnode 6 RaiseEvent Slow\ndefault 6 1 7\nexec 4 0 5\nexec 5 0 6\n");
        g_errs.clear();
        s.Start();
        CHECK(a->Graph()->HasSuspended());
        CHECK(a->GetVariable("later").AsFloat() == 0.0f);
        for (int i = 0; i < 4; ++i) s.Update(0.05f);
        CHECK(g_errs.empty());
        CHECK(a->GetVariable("later").AsFloat() == 42.0f);   // not the parameter of the later raise, nor 0
        CHECK(!a->Graph()->HasSuspended() && a->Graph()->CallDepth() == 0);
    }

    // ---- The debugger: handler frames name the raising node, hold the parameters; breakpoints stop in handlers ----
    {
        vs::VsEventBindings::Get().Clear();
        Scene s("dbg");
        auto* b = Make(s, "B", kListener);
        b->SetPath("b_events.okayvs");
        auto* a = Make(s, "A",
            "event Hit(Number amount, Text who)\nnode 1 OnStart\nentry OnStart 1\nnode 2 RaiseEvent Hit\ndefault 2 0 \"B\"\ndefault 2 1 12\ndefault 2 2 \"zed\"\nexec 1 0 2\n");
        a->SetPath("a_events.okayvs");
        std::vector<vs::NodeGraph::DebugFrame> frames;
        std::string stoppedKey; int stoppedNode = -1; std::string stoppedEvent;
        auto& dbg = vs::VsDebugger::Get();
        dbg.SetBreakpoint("b_events.okayvs", 2, true);   // the SetVar inside the handler
        dbg.onStop = [&](vs::NodeGraph& g, vs::VsNode& n, vs::VsContext&) {
            frames = g.DebugFrames(); stoppedKey = g.debugKey; stoppedNode = n.fileId; stoppedEvent = g.CurrentEvent();
        };
        s.Start();
        dbg.ClearBreakpoints(); dbg.onStop = nullptr;
        CHECK(stoppedKey == "b_events.okayvs" && stoppedNode == 2 && stoppedEvent == "event Hit");
        CHECK(frames.size() == 1 && frames[0].isEvent && frames[0].name == "event Hit");
        CHECK(frames[0].args.size() == 2 && frames[0].args[0].AsFloat() == 12.0f && frames[0].args[1].AsString() == "zed");
        CHECK(frames[0].paramNames.size() == 2 && frames[0].paramNames[0] == "amount" && frames[0].paramNames[1] == "who");
        CHECK(frames[0].origin.find("Raise Event 'Hit' (node #2) of 'A' (a_events.okayvs)") != std::string::npos);
        CHECK(b->GetVariable("got").AsFloat() == 12.0f);   // execution carried on after the stop
    }

    // ---- The checker: graph rules ----
    {
        const std::string head = "event Hit(Number amount, Text who)\ndispatcher Died(Number code)\nevent Mourn(Number code)\nevent Mourn2(Vector3 v)\n";
        auto start = [](const std::string& body) { return "node 1 OnStart\nentry OnStart 1\n" + body; };
        CHECK(Issues(head + start("node 2 RaiseEvent Hit\nexec 1 0 2\nnode 3 OnCustomEvent Hit\nentry OnCustomEvent:Hit 3\nnode 4 Print\nexec 3 0 4\n"), 0) == 0);
        CHECK(IssueHas(start("node 2 RaiseEvent Nope\nexec 1 0 2\n"), "no event named 'Nope' declared in this graph", 0));
        CHECK(IssueHas(head + start("node 2 RaiseEvent Died\nexec 1 0 2\n"), "use Call Dispatcher", 0));
        CHECK(IssueHas(head + start("node 2 CallDispatcher Hit\nexec 1 0 2\n"), "use Raise Event", 0));
        CHECK(IssueHas(head + start("node 2 CallDispatcher Zzz\nexec 1 0 2\n"), "no dispatcher named 'Zzz'", 0));
        CHECK(IssueHas(head + start("node 2 Print\nexec 1 0 2\nnode 3 OnCustomEvent Died\nentry OnCustomEvent:Died 3\nexec 3 0 2\n"), "is a dispatcher", 0));
        CHECK(IssueHas(start("node 2 OnCustomEvent Gone\nentry OnCustomEvent:Gone 2\nnode 3 Print\nexec 2 0 3\n"), "no event named 'Gone'", 0));
        // wires into typed pins: a Text into the Number parameter, a Vector3 into the Text one
        CHECK(IssueHas(head + start("node 2 RaiseEvent Hit\nexec 1 0 2\nnode 3 Const \"hi\"\nnode 4 MakeVec3\ndata 2 1 3 0\ndata 2 2 4 0\n"), "wired into an input that needs a Number", 0));
        CHECK(!IssueHas(head + start("node 2 RaiseEvent Hit\nexec 1 0 2\nnode 4 MakeVec3\ndata 2 2 4 0\n"), "wired into an input", 0));   // anything reads as Text
        CHECK(IssueHas(head + start("node 2 RaiseEvent Hit\nexec 1 0 2\nnode 3 Const 5\ndata 2 1 3 0\n"), "", 0) == false);   // a Number into Number is fine
        // wire to a parameter the event no longer has, from the handler side and the raise side
        CHECK(IssueHas(head + start("node 2 RaiseEvent Hit\nexec 1 0 2\nnode 3 Const 5\ndata 2 5 3 0\n"), "parameter #5 of 'Hit', which it doesn't have", 0));
        CHECK(IssueHas(head + start("node 2 Print\nexec 1 0 2\nnode 3 OnCustomEvent Hit\nentry OnCustomEvent:Hit 3\nexec 3 0 2\ndata 2 0 3 4\n"), "parameter #5 of event 'Hit'", 0));
        // typed handler outputs: the Number parameter into a Vec3 input is refused
        CHECK(IssueHas(head + start("node 3 OnCustomEvent Hit\nentry OnCustomEvent:Hit 3\nnode 4 Translate\nexec 3 0 4\ndata 4 0 3 1\n"), "a Text from 'OnCustomEvent'", 0));
        CHECK(!IssueHas(head + start("node 3 OnCustomEvent Hit\nentry OnCustomEvent:Hit 3\nnode 4 Translate\nexec 3 0 4\ndata 4 0 3 0\n"), "wired into an input", 0));
        // parameters read outside their handler
        CHECK(IssueHas(head + start("node 3 OnCustomEvent Hit\nentry OnCustomEvent:Hit 3\nnode 2 Print\nexec 1 0 2\ndata 2 0 3 0\n"), "read by a chain that isn't one of its handlers", 0));
        CHECK(!IssueHas(head + start("node 3 OnCustomEvent Hit\nentry OnCustomEvent:Hit 3\nnode 2 Print\nexec 3 0 2\ndata 2 0 3 0\n"), "read by a chain", 0));
        // raising on itself with no handler node is worth a word
        CHECK(IssueHas(head + start("node 2 RaiseEvent Hit\nexec 1 0 2\n"), "nothing in this graph has an On Event node", 1));
        // bind / unbind
        CHECK(Issues(head + start("node 2 BindEvent \"Died -> Mourn\"\nexec 1 0 2\nnode 3 OnCustomEvent Mourn\nentry OnCustomEvent:Mourn 3\nnode 4 Print\nexec 3 0 4\n"), 0) == 0);
        CHECK(IssueHas(head + start("node 2 BindEvent \"Hit -> Mourn\"\nexec 1 0 2\n"), "is an event, not a dispatcher", 0));
        CHECK(IssueHas(head + start("node 2 BindEvent \"Died -> Mourn2\"\nexec 1 0 2\n"), "doesn't fit dispatcher 'Died'", 0));
        CHECK(IssueHas(head + start("node 2 BindEvent \"Died -> Hit\"\nexec 1 0 2\n"), "doesn't fit dispatcher 'Died'", 0));
        CHECK(IssueHas(head + start("node 2 BindEvent \"Died -> Died\"\nexec 1 0 2\n"), "is a dispatcher, not a handler", 0));
        CHECK(IssueHas(head + start("node 2 BindEvent \"Died -> Nothing\"\nexec 1 0 2\n"), "neither an event nor a function", 0));
        CHECK(IssueHas(head + start("node 2 BindEvent Died\nexec 1 0 2\n"), "no handler is picked", 0));
        CHECK(IssueHas(head + start("node 2 UnbindEvent \"Zip -> Mourn\"\nexec 1 0 2\n"), "no dispatcher named 'Zip'", 0));
        CHECK(IssueHas(head + start("node 2 BindEvent \"Died -> Mourn\"\nexec 1 0 2\n"), "has no On Event node in this graph", 1));
        // function handlers: count and types
        const std::string fn = "node 9 FunctionStart \"Cheer(c: Number)\"\nnode 8 FunctionStart \"Bad(a: Number, b: Number)\"\nnode 7 FunctionStart \"Vec(v: Vector3)\"\n";
        CHECK(Issues(head + fn + start("node 2 BindEvent \"Died -> Cheer\"\nexec 1 0 2\n"), 0) == 0);
        CHECK(IssueHas(head + fn + start("node 2 BindEvent \"Died -> Bad\"\nexec 1 0 2\n"), "takes 2 parameters but dispatcher 'Died' sends 1", 0));
        CHECK(IssueHas(head + fn + start("node 2 BindEvent \"Died -> Vec\"\nexec 1 0 2\n"), "is a Vector3 but dispatcher 'Died' sends a Number", 0));
    }
    {   // the checker keeps a clean graph clean: handler entries without an `entry` line count as events too
        const std::string g = "event Hit(Number a)\nnode 1 OnCustomEvent Hit\nnode 2 Print\nexec 1 0 2\ndata 2 0 1 0\n";
        CHECK(Issues(g) == 0);
        std::string err;
        auto parsed = vs::NodeGraph::Parse(g, &err);
        CHECK(parsed && parsed->Entries("OnCustomEvent:Hit").size() == 1);
    }

    // ---- The checker: scene rules across graphs ----
    {
        Scene s("scenecheck");
        Make(s, "Door", "event Open(Number angle)\nnode 1 OnCustomEvent Open\nentry OnCustomEvent:Open 1\nnode 2 Print\nexec 1 0 2\n");
        Make(s, "Lever",
            "event Open(Number angle)\nevent Close()\nnode 1 OnStart\nentry OnStart 1\n"
            "node 2 RaiseEvent Open\ndefault 2 0 \"Door\"\nnode 3 RaiseEvent Close\ndefault 3 0 \"Door\"\nnode 4 RaiseEvent Open\ndefault 4 0 \"Nowhere\"\n"
            "exec 1 0 2\nexec 2 0 3\nexec 3 0 4\n");
        Make(s, "Gate", "event Open(Text how)\nnode 1 OnCustomEvent Open\nentry OnCustomEvent:Open 1\nnode 2 Print\nexec 1 0 2\n");
        Make(s, "Pusher", "event Open(Number a, Number b)\nnode 1 OnStart\nentry OnStart 1\nnode 2 RaiseEvent Open\ndefault 2 0 \"Gate\"\nexec 1 0 2\n");
        const auto issues = vs::ValidateSceneGraphs(s);
        bool noClose = false, ghost = false, mismatchWarn = false, gateErr = false;
        for (const auto& i : issues) {
            if (i.object == "Lever" && i.issue.node == 3 && i.issue.severity == 0 && i.issue.message.find("'Door' has no Visual Script that declares event 'Close'") != std::string::npos) noClose = true;
            if (i.object == "Lever" && i.issue.node == 4 && i.issue.severity == 1 && i.issue.message.find("'Nowhere' isn't an object") != std::string::npos) ghost = true;
            if (i.issue.severity == 1 && i.issue.node == -1 && i.issue.message.find("declared differently") != std::string::npos) mismatchWarn = true;
            if (i.object == "Pusher" && i.issue.node == 2 && i.issue.severity == 0 && i.issue.message.find("declares event 'Open' differently") != std::string::npos) gateErr = true;
            CHECK(!(i.object == "Lever" && i.issue.node == 2 && i.issue.severity == 0));   // Open(Number) on Door is a match
        }
        CHECK(noClose && ghost && mismatchWarn && gateErr);
    }

    // ---- Round trips: scene file, prefab, a play-time save ----
    {
        const std::string text = "event Hit(Number amount, Text who)\ndispatcher Died(Object who)\nnode 1 OnCustomEvent Hit\nentry OnCustomEvent:Hit 1\n";
        Scene a("round");
        a.CreateGameObject("G")->AddComponent<VisualScriptComponent>()->LoadFromText(text);
        const std::string ser = SceneSerializer::Serialize(a);
        Scene b("round2");
        std::string err;
        CHECK(SceneSerializer::Deserialize(b, ser, &err));
        auto* vb = b.Find("G") ? b.Find("G")->GetComponent<VisualScriptComponent>() : nullptr;
        CHECK(vb && vb->Graph() && vb->Graph()->Events().size() == 2);
        if (vb && vb->Graph()) {
            CHECK(vb->Graph()->FindEvent("Hit") && vb->Graph()->FindEvent("Hit")->params.size() == 2);
            CHECK(vb->Graph()->FindEvent("Died") && vb->Graph()->FindEvent("Died")->dispatcher);
            CHECK(vb->Graph()->Entries("OnCustomEvent:Hit").size() == 1);
        }
        CHECK(vb && vb->Source() == text);
    }
    {   // a prefab keeps events; two instances deliver independently; a prefab override of the graph text is honoured
        vs::VsEventBindings::Get().Clear();
        Scene a("pf");
        GameObject* src = a.CreateGameObject("Proto");
        src->AddComponent<VisualScriptComponent>()->LoadFromText(kListener);
        const std::string prefab = SceneSerializer::SerializeObject(*src);
        Scene b("pf2");
        std::string err;
        GameObject* x = SceneSerializer::InstantiateFromText(b, prefab, &err);
        GameObject* y = SceneSerializer::InstantiateFromText(b, prefab, &err);
        CHECK(x && y);
        b.Start();
        auto* vx = x->GetComponent<VisualScriptComponent>();
        auto* vy = y->GetComponent<VisualScriptComponent>();
        CHECK(vx->RunTypedEvent("Hit", {vs::VsValue(3.0f), vs::VsValue(std::string("x"))}, nullptr, "t") == 1);
        CHECK(vx->GetVariable("got").AsFloat() == 3.0f && vy->GetVariable("got").AsFloat() == 0.0f);
        // the instance's graph overridden: a different parameter list for the same event name
        CHECK(vy->LoadFromText("event Hit(Text only)\nnode 1 OnCustomEvent Hit\nentry OnCustomEvent:Hit 1\nnode 2 SetVar got\ndata 2 0 1 0\nexec 1 0 2\n"));
        CHECK(vy->RunTypedEvent("Hit", {vs::VsValue(std::string("s"))}, nullptr, "t") == 1 && vy->GetVariable("got").AsString() == "s");
        CHECK(vx->GetVariable("got").AsFloat() == 3.0f);
        // the override survives saving the instance back as a prefab and instancing it again; the original prefab is untouched
        const std::string overridden = SceneSerializer::SerializeObject(*y);
        Scene c("pf3");
        GameObject* z = SceneSerializer::InstantiateFromText(c, overridden, &err);
        GameObject* orig = SceneSerializer::InstantiateFromText(c, prefab, &err);
        CHECK(z && orig);
        c.Start();
        auto* vz = z->GetComponent<VisualScriptComponent>();
        auto* vo = orig->GetComponent<VisualScriptComponent>();
        CHECK(vz->Graph()->FindEvent("Hit")->params.size() == 1 && vo->Graph()->FindEvent("Hit")->params.size() == 2);
        CHECK(vz->RunTypedEvent("Hit", {vs::VsValue(std::string("q"))}, nullptr, "t") == 1 && vz->GetVariable("got").AsString() == "q");
        CHECK(vo->RunTypedEvent("Hit", {vs::VsValue(9.0f), vs::VsValue(std::string("w"))}, nullptr, "t") == 1 && vo->GetVariable("got").AsFloat() == 9.0f);
    }
    {   // bindings are runtime state: a save written with live bindings loads without duplicating them
        vs::VsEventBindings::Get().Clear();
        const fs::path dir = fs::temp_directory_path() / "okay_test_vs_events";
        fs::remove_all(dir); fs::create_directories(dir);
        Save::SetBaseDir(dir.string());
        Scene s("save");
        Make(s, "Boss", "dispatcher Ping()\n");
        Make(s, "L", "dispatcher Ping()\nevent Hear()\nnode 1 OnStart\nentry OnStart 1\nnode 2 BindEvent \"Ping -> Hear\"\ndefault 2 0 \"Boss\"\nexec 1 0 2\n"
                     "node 5 OnCustomEvent Hear\nentry OnCustomEvent:Hear 5\n");
        s.Start();
        CHECK(vs::VsEventBindings::Get().Count() == 1);
        std::string err;
        CHECK(SaveGame::Save(s, "e", "events", &err));
        CHECK(SaveGame::LoadNow(s, "e", &err));
        s.Start();
        CHECK(vs::VsEventBindings::Get().Count() <= 1);   // rebuilt by On Start, never doubled
        auto* back = s.Find("L") ? s.Find("L")->GetComponent<VisualScriptComponent>() : nullptr;
        CHECK(back && back->Graph() && back->Graph()->FindEvent("Hear"));
        fs::remove_all(dir);
    }

    // ---- The shipped example: Lever raises a typed event on Door, Door calls a dispatcher, Light's bound function runs ----
    {
        vs::VsEventBindings::Get().Clear();
        auto read = [](const char* name) {
            std::ifstream f(std::string(OKAY_SOURCE_DIR) + "/examples/visual_script/" + name);
            return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        };
        const std::string lever = read("custom_events_lever.okayvs"), door = read("custom_events_door.okayvs"), light = read("custom_events_light.okayvs");
        CHECK(!lever.empty() && !door.empty() && !light.empty());
        CHECK(vs::ValidateGraphText(lever).empty() && vs::ValidateGraphText(door).empty() && vs::ValidateGraphText(light).empty());
        Scene s("example");
        Make(s, "Lever", lever); Make(s, "Door", door); Make(s, "Light", light);
        CHECK(vs::ValidateSceneGraphs(s).empty());
        g_errs.clear(); g_printed.clear();
        s.Start();
        for (int i = 0; i < 20; ++i) s.Update(0.05f);
        CHECK(g_errs.empty());
        CHECK(Printed("Door: pulled by the Lever with force 3"));
        CHECK(Printed("Light: the door opened to 30 degrees"));
        CHECK(IndexOf("Door: pulled by the Lever with force 3") < IndexOf("Light: the door opened to 30 degrees"));
        CHECK(vs::VsEventBindings::Get().Count() == 1);
    }

    // ---- Hostile input and a fuzz sweep ----
    {
        const char* nasty[] = {
            "event\n", "event (\n", "event )(\n", "dispatcher X(\n", "event X(Number)\n", "event X(,)\n", "event \"\"\n", "event X(Number a Number b)\n",
            "node 1 RaiseEvent\n", "node 1 OnCustomEvent\nentry OnCustomEvent: 1\n", "node 1 BindEvent ->\n", "node 1 BindEvent \"-> ->\"\n",
            "event X(List<Number> a)\nnode 1 RaiseEvent X\ndefault 1 1 \"[[[[\"\n", "node 1 UnbindEvent \"\"\n", "node 1 CallDispatcher \"a b c d\"\n",
            "event X()\nevent X()\n", "dispatcher X(Object o, Object p, Object q)\nnode 1 CallDispatcher X\ndata 1 99 1 0\n",
        };
        for (const char* n : nasty) { std::string err; (void)vs::NodeGraph::Parse(n, &err); (void)vs::ValidateGraphText(n); }
        CHECK(true);
        // nodes with no/odd arguments run without crashing (errors, never a crash)
        Scene s("hostile");
        auto* v = Make(s, "H",
            "node 1 OnStart\nentry OnStart 1\nnode 2 RaiseEvent\nnode 3 CallDispatcher\nnode 4 BindEvent\nnode 5 UnbindEvent\nnode 6 OnCustomEvent\nentry OnCustomEvent: 6\n"
            "exec 1 0 2\nexec 2 0 3\nexec 3 0 4\nexec 4 0 5\n");
        g_errs.clear();
        s.Start();
        CHECK(v && !g_errs.empty() && !v->Graph()->HasSuspended());
    }
    {   // a deterministic fuzz: random text through the parser and checker; random raise / bind / destroy sequences keep the table consistent
        unsigned long long seed = 4242;
        auto rnd = [&]() { seed = seed * 6364136223846793005ULL + 1442695040888963407ULL; return (unsigned)(seed >> 33); };
        const std::string alphabet = "eventdispatchrNumbTxO(),<>:- \"\n0123456789ab";
        int parsedEvents = 0;
        for (int i = 0; i < 6000; ++i) {
            std::string t; const int n = (int)(rnd() % 40);
            for (int k = 0; k < n; ++k) t += alphabet[rnd() % alphabet.size()];
            std::string err;
            auto g = vs::NodeGraph::Parse("event " + t + "\n", &err);
            if (g && !g->Events().empty()) ++parsedEvents;
            (void)vs::NodeGraph::Parse("dispatcher " + t + "\nnode 1 BindEvent \"" + t + "\"\n", &err);
            (void)vs::ValidateGraphText("event A(Number x)\nnode 1 RaiseEvent " + t + "\nnode 2 OnCustomEvent " + t + "\nentry OnCustomEvent:" + t + " 2\n");
            vs::EventLine el; (void)vs::ParseEventLine(t, el);
            std::string d, h; (void)vs::ParseBindText(t, d, h);
        }
        CHECK(parsedEvents > 0);
        vs::VsEventBindings::Get().Clear();
        Scene s("fuzz");
        const char* graphText =
            "dispatcher Tick(Number n)\nevent Hear(Number n)\nevent Go()\n"
            "node 1 OnCustomEvent Go\nentry OnCustomEvent:Go 1\nnode 2 CallDispatcher Tick\nexec 1 0 2\n"
            "node 3 OnCustomEvent Hear\nentry OnCustomEvent:Hear 3\nnode 4 AddVar hits\nnode 5 Const 1\ndata 4 0 5 0\nexec 3 0 4\n"
            "event Join()\nnode 6 OnCustomEvent Join\nentry OnCustomEvent:Join 6\nnode 7 BindEvent \"Tick -> Hear\"\nnode 8 UnbindEvent \"Tick -> Hear\"\nexec 6 0 7\n"
            "event Leave()\nnode 9 OnCustomEvent Leave\nentry OnCustomEvent:Leave 9\nexec 9 0 8\n";
        std::vector<GameObject*> objs;
        std::vector<VisualScriptComponent*> comps;
        for (int i = 0; i < 6; ++i) { auto* vc = Make(s, ("O" + std::to_string(i)).c_str(), graphText); objs.push_back(vc->gameObject); comps.push_back(vc); }
        s.Start();
        std::vector<char> alive(objs.size(), 1);
        for (int step = 0; step < 3000; ++step) {
            const std::size_t i = rnd() % objs.size();
            if (!alive[i]) continue;
            const unsigned op = rnd() % 10;
            if (op < 4) { comps[i]->RunTypedEvent("Join", {}, nullptr, "f"); }
            else if (op < 6) { comps[i]->RunTypedEvent("Leave", {}, nullptr, "f"); }
            else if (op < 9) { comps[i]->RunTypedEvent("Go", {}, nullptr, "f"); }
            else if (step % 50 == 0) { s.Destroy(objs[i]); s.Update(0.016f); alive[i] = 0; }
            CHECK(vs::VsEventDepth() == 0);
        }
        // every binding left belongs to a live object (bindings of the destroyed ones were dropped)
        std::size_t liveCount = 0;
        for (std::size_t k = 0; k < objs.size(); ++k) if (alive[k]) liveCount += vs::VsEventBindings::Get().CountListening(comps[k]->gameObject->InstanceId());
        CHECK(liveCount == vs::VsEventBindings::Get().Count());
        s.Clear();
        CHECK(vs::VsEventBindings::Get().Count() == 0);
    }

    vs::VsEventBindings::Get().Clear();
    vs::SetRuntimeErrorHook(nullptr);
    Log::sink = sink;
    TEST_MAIN_RESULT();
}
