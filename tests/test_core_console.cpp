#include "test_framework.hpp"
#include <Okay.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace okay;
using Strs = std::vector<std::string>;

namespace { struct A { int v; }; struct B { int v; }; }

int main() {
    RUN_SUITE("core_console");

    // ---- EventBus: existing behaviour kept ----
    {
        EventBus bus; std::vector<int> got;
        const int t = bus.Subscribe<A>([&](const A& e) { got.push_back(e.v); });
        bus.Publish(A{1});
        bus.Unsubscribe<A>(t);
        bus.Publish(A{2});
        CHECK((got == std::vector<int>{1}) && bus.SubscriberCount<A>() == 0);
        // snapshot dispatch: a handler added during dispatch first sees the NEXT event
        got.clear();
        bus.Subscribe<A>([&](const A&) { bus.Subscribe<A>([&](const A& e) { got.push_back(100 + e.v); }); });
        bus.Publish(A{1});
        CHECK(got.empty());
        bus.Publish(A{2});
        CHECK((got == std::vector<int>{102}));
    }
    {   // a handler removed during dispatch before its turn is skipped
        EventBus bus; int hits = 0; int tok2 = 0;
        bus.Subscribe<A>([&](const A&) { bus.Unsubscribe<A>(tok2); });
        tok2 = bus.Subscribe<A>([&](const A&) { ++hits; });
        bus.Publish(A{0});
        CHECK(hits == 0);
        bus.Publish(A{0});
        CHECK(hits == 0);
    }

    // ---- priority ----
    {
        EventBus bus; std::string order;
        bus.Subscribe<A>([&](const A&) { order += "n1 "; });
        bus.Subscribe<A>([&](const A&) { order += "hi "; }, 10);
        bus.Subscribe<A>([&](const A&) { order += "lo "; }, -5);
        bus.Subscribe<A>([&](const A&) { order += "n2 "; });
        bus.Subscribe<A>([&](const A&) { order += "hi2 "; }, 10);
        bus.Publish(A{0});
        CHECK(order == "hi hi2 n1 n2 lo ");     // higher first, ties in subscription order
    }
    // ---- once ----
    {
        EventBus bus; int once = 0, always = 0;
        bus.SubscribeOnce<A>([&](const A&) { ++once; });
        bus.Subscribe<A>([&](const A&) { ++always; });
        CHECK(bus.SubscriberCount<A>() == 2);
        bus.Publish(A{0}); bus.Publish(A{0}); bus.Publish(A{0});
        CHECK(once == 1 && always == 3 && bus.SubscriberCount<A>() == 1);
        // re-entrant publish from inside a once handler does not run it twice
        EventBus b2; int n = 0;
        b2.SubscribeOnce<A>([&](const A& e) { ++n; if (e.v < 3) b2.Publish(A{e.v + 1}); });
        b2.Publish(A{0});
        CHECK(n == 1);
        // once + priority ordering
        EventBus b3; std::string o;
        b3.Subscribe<A>([&](const A&) { o += "a"; });
        b3.SubscribeOnce<A>([&](const A&) { o += "b"; }, 5);
        b3.Publish(A{0}); b3.Publish(A{0});
        CHECK(o == "baa");
    }
    // ---- RAII token ----
    {
        EventBus bus; int hits = 0;
        {
            Subscription s = bus.Listen<A>([&](const A&) { ++hits; });
            CHECK(s.Active() && bus.SubscriberCount<A>() == 1);
            bus.Publish(A{0});
            Subscription moved = std::move(s);
            CHECK(!s.Active() && moved.Active());
            bus.Publish(A{0});
            CHECK(hits == 2);
        }
        CHECK(bus.SubscriberCount<A>() == 0);
        bus.Publish(A{0});
        CHECK(hits == 2);
        Subscription s2 = bus.Listen<A>([&](const A&) { ++hits; });
        s2.Unsubscribe(); s2.Unsubscribe();           // twice is fine
        CHECK(!s2.Active() && bus.SubscriberCount<A>() == 0);
        { Subscription kept = bus.Listen<B>([&](const B&) { ++hits; }); kept.Release(); }
        CHECK(bus.SubscriberCount<B>() == 1);          // released: stays subscribed
        Subscription once = bus.Listen<A>([&](const A&) { ++hits; }, 0, true);
        bus.Publish(A{0}); bus.Publish(A{0});
        CHECK(hits == 3);
        // token outliving the bus does nothing (and does not crash)
        Subscription outlive;
        { EventBus tmp; outlive = tmp.Listen<A>([](const A&) {}); }
        CHECK(!outlive.Active());
        outlive.Unsubscribe();
    }
    // ---- queued publish ----
    {
        EventBus bus; std::vector<int> got;
        bus.Subscribe<A>([&](const A& e) { got.push_back(e.v); });
        bus.Queue(A{1}); bus.Queue(A{2});
        CHECK(got.empty() && bus.QueuedCount() == 2);
        CHECK(bus.Flush() == 2);
        CHECK((got == std::vector<int>{1, 2}) && bus.QueuedCount() == 0);
        CHECK(bus.Flush() == 0);
        // an event queued by a handler waits for the next flush
        got.clear();
        bus.Subscribe<B>([&](const B& e) { got.push_back(e.v); if (e.v == 1) bus.Queue(B{2}); });
        bus.Queue(B{1});
        bus.Flush();
        CHECK((got == std::vector<int>{1}) && bus.QueuedCount() == 1);
        bus.Flush();
        CHECK((got == std::vector<int>{1, 2}));
        // queued copy is taken at Queue time
        got.clear(); int v = 7; EventBus b2; b2.Subscribe<A>([&](const A& e) { got.push_back(e.v); });
        b2.Queue(A{v}); v = 9; b2.Flush();
        CHECK((got == std::vector<int>{7}));
        b2.Queue(A{1}); b2.ClearQueue(); CHECK(b2.Flush() == 0);
    }

    // ---- Cvars ----
    Cvars::Clear();
    {
        Cvar& shadows = Cvars::Int("r.shadows", 1, "shadow quality", 0, 3, true);
        Cvar& scale = Cvars::Float("r.scale", 1.0f, "render scale", 0.25f, 2.0f);
        Cvar& god = Cvars::Bool("cheat.god", false, "take no damage");
        Cvar& name = Cvars::String("player.name", "Pilot", "display name", true);
        CHECK(&Cvars::Int("r.shadows", 5) == &shadows);          // same name: the first registration
        CHECK(Cvars::Find("r.shadows") == &shadows && Cvars::Find("nope") == nullptr);
        CHECK(shadows.AsInt() == 1 && shadows.IsDefault() && shadows.HasRange());
        CHECK(shadows.Description() == "shadow quality" && std::string(shadows.TypeName()) == "int");
        CHECK(shadows.Set("2") && shadows.AsInt() == 2 && !shadows.IsDefault());
        CHECK(shadows.Set("99") && shadows.AsInt() == 3);          // clamped to the range
        CHECK(shadows.Set("-4") && shadows.AsInt() == 0);
        std::string err;
        CHECK(!shadows.Set("abc", &err) && !err.empty() && shadows.AsInt() == 0);   // unchanged on bad input
        CHECK(!shadows.Set("1.5") && !shadows.Set(""));
        CHECK(scale.Set("0.5") && scale.AsFloat() == 0.5f);
        CHECK(scale.Set("10") && scale.AsFloat() == 2.0f);
        CHECK(!scale.Set("nan") && scale.AsFloat() == 2.0f);
        CHECK(god.Set("on") && god.AsBool() && god.AsString() == "true");
        CHECK(god.Set("0") && !god.AsBool());
        CHECK(!god.Set("maybe", &err) && err.find("bool") != std::string::npos);
        CHECK(name.Set("Ace Pilot") && name.AsString() == "Ace Pilot");
        shadows.Reset(); CHECK(shadows.AsInt() == 1);
        CHECK(Cvars::Int("lo", 50, "", 0, 10).AsInt() == 10);       // default clamped into range too

        // change callbacks: only on a real change
        int calls = 0; int last = -1;
        const int cb = shadows.OnChange([&](const Cvar& c) { ++calls; last = c.AsInt(); });
        shadows.SetInt(2); shadows.SetInt(2); shadows.Set("2");
        CHECK(calls == 1 && last == 2);
        shadows.Set("3");
        CHECK(calls == 2 && last == 3);
        shadows.RemoveOnChange(cb); shadows.SetInt(0);
        CHECK(calls == 2);
        // cross-type setters
        shadows.SetFloat(2.7f); CHECK(shadows.AsInt() == 2);
        god.SetInt(5); CHECK(god.AsBool());
        name.SetInt(12); CHECK(name.AsString() == "12" && name.AsInt() == 12);

        CHECK(Cvars::Set("r.shadows", "1") && shadows.AsInt() == 1);
        CHECK(!Cvars::Set("nope", "1", &err) && err.find("unknown") != std::string::npos);
        CHECK(Cvars::SetFromAssignment("cheat.god=true") && god.AsBool());
        CHECK(!Cvars::SetFromAssignment("nonsense") && !Cvars::SetFromAssignment("=1"));
        CHECK(Cvars::All("r.").size() == 2 && Cvars::All().size() == 5);
        CHECK(Cvars::All()[0]->Name() == "cheat.god");               // sorted
    }
    // ---- persistence ----
    {
        const std::string path = (std::filesystem::temp_directory_path() / "okay_cvars_test.cfg").string();
        std::filesystem::remove(path);
        Cvars::Find("r.shadows")->SetInt(2);                         // persist, non default
        Cvars::Find("cheat.god")->SetBool(true);                     // not persistent
        Cvars::Find("player.name")->Set("Zed");                      // persist string
        Cvars::Find("r.scale")->Set("0.5");                          // not persistent
        CHECK(Cvars::Save(path));
        { std::ifstream f(path); std::stringstream ss; ss << f.rdbuf(); const std::string t = ss.str();
          CHECK(t.find("r.shadows 2") != std::string::npos && t.find("player.name Zed") != std::string::npos);
          CHECK(t.find("cheat.god") == std::string::npos && t.find("r.scale") == std::string::npos); }
        Cvars::ResetAll();
        CHECK(Cvars::Find("r.shadows")->AsInt() == 1 && Cvars::Find("player.name")->AsString() == "Pilot");
        CHECK(Cvars::Load(path));
        CHECK(Cvars::Find("r.shadows")->AsInt() == 2 && Cvars::Find("player.name")->AsString() == "Zed");
        // a value for a cvar registered later is held, then applied on registration
        { std::ofstream f(path, std::ios::app); f << "# comment\nlater.value 42\nbadline\n"; }
        CHECK(Cvars::Load(path));
        CHECK(Cvars::Int("later.value", 0, "", 0, 0, true).AsInt() == 42);
        // unregistered names survive a Save (other build's settings)
        { std::ofstream f(path, std::ios::app); f << "future.setting abc\n"; }
        Cvars::Load(path);
        CHECK(Cvars::Save(path));
        { std::ifstream f(path); std::stringstream ss; ss << f.rdbuf();
          CHECK(ss.str().find("future.setting abc") != std::string::npos); }
        CHECK(!Cvars::Load((std::filesystem::temp_directory_path() / "okay_missing_cvars.cfg").string()));
        std::filesystem::remove(path);
    }

    // ---- Console ----
    {
        Console::Reset();
        Cvars::ResetAll();
        std::vector<std::string> seen;
        Console::Register("spawn", [&](const Strs& a) { seen = a; return std::string("spawned ") + (a.empty() ? "nothing" : a[0]); },
                          "spawn a thing", "spawn <name> [count]");
        auto r = Console::Execute("spawn crate 3");
        CHECK(r.ok && r.output == "spawned crate" && (seen == Strs{"crate", "3"}));
        r = Console::Execute("   spawn   \"big crate\"  ");
        CHECK((seen == Strs{"big crate"}));
        CHECK((Console::Tokenize("a \"b c\" d\\e \"q\\\"x\" \"\"") == Strs{"a", "b c", "d\\e", "q\"x", ""}));

        // cvars by name: "name value" sets, "name" prints
        r = Console::Execute("r.shadows 3");
        CHECK(r.ok && Cvars::Find("r.shadows")->AsInt() == 3 && r.output.find("r.shadows = 3") != std::string::npos);
        r = Console::Execute("r.shadows");
        CHECK(r.ok && r.output.find("shadow quality") != std::string::npos && r.output.find("default 1") != std::string::npos);
        r = Console::Execute("r.shadows lots");
        CHECK(!r.ok && r.output.compare(0, 6, "error:") == 0 && Cvars::Find("r.shadows")->AsInt() == 3);
        r = Console::Execute("player.name Ace Pilot");                  // string cvar takes the rest of the line
        CHECK(Cvars::Find("player.name")->AsString() == "Ace Pilot");
        r = Console::Execute("set r.scale 0.75"); CHECK(r.ok && Cvars::Find("r.scale")->AsFloat() == 0.75f);
        r = Console::Execute("get r.scale");      CHECK(r.ok && r.output.find("0.75") != std::string::npos);
        r = Console::Execute("toggle cheat.god"); CHECK(r.ok && Cvars::Find("cheat.god")->AsBool());
        r = Console::Execute("toggle r.shadows"); CHECK(!r.ok);          // not a bool
        r = Console::Execute("reset r.shadows");  CHECK(r.ok && Cvars::Find("r.shadows")->AsInt() == 1);
        r = Console::Execute("set nope 1");       CHECK(!r.ok);
        r = Console::Execute("set r.shadows");    CHECK(!r.ok);
        r = Console::Execute("bogus");            CHECK(!r.ok && r.output.find("unknown command") != std::string::npos);
        r = Console::Execute("echo hello   world"); CHECK(r.output == "hello world");
        r = Console::Execute("");                 CHECK(r.ok && r.output.empty());
        r = Console::Execute("# note");           CHECK(r.ok && r.output.empty());
        r = Console::Execute("// note");          CHECK(r.ok && r.output.empty());

        // help / list
        r = Console::Execute("help");
        CHECK(r.ok && r.output.find("spawn - spawn a thing") != std::string::npos && r.output.find("list") != std::string::npos);
        r = Console::Execute("help spawn");       CHECK(r.output.find("spawn <name> [count]") != std::string::npos);
        r = Console::Execute("help r.scale");     CHECK(r.output.find("render scale") != std::string::npos);
        r = Console::Execute("help nothing");     CHECK(!r.ok);
        r = Console::Execute("list");
        CHECK(r.ok && r.output.find("r.shadows") != std::string::npos && r.output.find("cheat.god") != std::string::npos);
        r = Console::Execute("list r.");
        CHECK(r.output.find("r.scale") != std::string::npos && r.output.find("cheat.god") == std::string::npos);
        CHECK(r.output.find("0..3") != std::string::npos || r.output.find("0.25..2") != std::string::npos);
        r = Console::Execute("list zzz");         CHECK(r.output.find("no variables") != std::string::npos);

        // history
        Console::ClearHistory();
        Console::Execute("echo a"); Console::Execute("echo b"); Console::Execute("echo b"); Console::Execute("echo c");
        CHECK((Console::History() == Strs{"echo a", "echo b", "echo c"}));     // repeats of the last line collapse
        r = Console::Execute("history");
        CHECK(r.output.find("1  echo a") != std::string::npos);
        Console::SetHistoryLimit(2);
        CHECK(Console::History().size() == 2 && Console::History()[0] == "echo c");
        for (int i = 0; i < 5; ++i) Console::Execute("echo " + std::to_string(i));
        CHECK(Console::History().size() == 2 && Console::History().back() == "echo 4");
        Console::SetHistoryLimit(100);

        // output log + completion + script + unregister
        Console::ClearLines();
        Console::Execute("echo shown");
        CHECK(!Console::Lines().empty() && Console::Lines().back() == "shown");
        Console::Print("two\nlines");
        CHECK(Console::Lines().size() >= 3 && Console::Lines().back() == "lines");
        CHECK((Console::Complete("sp") == Strs{"spawn"}));
        { auto c = Console::Complete("r."); CHECK(c.size() == 2 && c[0] == "r.scale"); }
        auto results = Console::ExecuteScript("echo one\n# skip\nr.shadows 2\nbad\n");
        CHECK(results.size() == 5 && results[0].output == "one" && !results[3].ok && Cvars::Find("r.shadows")->AsInt() == 2);
        CHECK(Console::Unregister("spawn") && !Console::HasCommand("spawn") && !Console::Unregister("spawn"));
        CHECK(!Console::Execute("spawn x").ok);
        // a command may register another command
        Console::Register("mk", [](const Strs&) { Console::Register("made", [](const Strs&) { return std::string("m"); }); return std::string(); });
        Console::Execute("mk");
        CHECK(Console::Execute("made").output == "m");
        Console::Reset();
        CHECK(Console::HasCommand("help") && !Console::HasCommand("made"));     // built-ins come back
    }

    // ---- Debug overlay lines ----
    {
        DebugOverlay::ClearLines();
        DebugOverlay::Stats s; s.fps = 59.94f; s.frameMs = 16.7f; s.objectCount = 42; s.frame = 1000; s.timeScale = 0.5f;
        auto lines = DebugOverlay::BuildLines(s);
        CHECK(lines.size() == 2);
        CHECK(lines[0] == "FPS: 59.9 (16.7 ms)");
        CHECK(lines[1] == "Objects: 42");
        auto d = DebugOverlay::BuildLines(s, true);
        CHECK(d.size() == 5 && d[2] == "Frame: 1000" && d[3] == "Time scale: 0.50" && d[4].find("OkaySpace") == 0);
        s.paused = true;
        CHECK(DebugOverlay::BuildLines(s, true)[3] == "Paused");
        int n = 0;
        const int id = DebugOverlay::AddLine([&] { return "Bullets: " + std::to_string(++n); });
        DebugOverlay::AddLine([] { return std::string(); });          // empty lines are skipped
        lines = DebugOverlay::BuildLines(s);
        CHECK(lines.size() == 3 && lines[2] == "Bullets: 1");
        DebugOverlay::RemoveLine(id);
        CHECK(DebugOverlay::BuildLines(s).size() == 2);
        DebugOverlay::ClearLines();
        // the on/off switch is a cvar
        CHECK(!DebugOverlay::Enabled() && DebugOverlay::BuildLines(s, false, false).empty());
        Console::Execute("debug.overlay 1");
        CHECK(DebugOverlay::Enabled() && DebugOverlay::BuildLines(s, false, false).size() == 2);
        DebugOverlay::Toggle(); CHECK(!DebugOverlay::Enabled());
        // Capture from a real scene
        Scene scene("o");
        scene.CreateGameObject("a"); scene.CreateGameObject("b");
        DebugOverlay::Stats c = DebugOverlay::Capture(&scene);
        CHECK(c.objectCount == scene.Objects().size() && c.objectCount >= 2);
        CHECK(DebugOverlay::Capture(nullptr).objectCount == 0);
    }

    Cvars::Clear();
    TEST_MAIN_RESULT();
}
