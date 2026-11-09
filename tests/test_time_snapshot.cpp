// 1.1 third pass: TimeSnapshot (clocks, time scale and random streams in a save document), the
// frame statistics CSV export and the loop's console commands. Fake frame times, fixed seeds, no
// sleeping. Files go to a temp folder that is removed at the end.
#include "test_framework.hpp"
#include "okay/Core/Clock.hpp"
#include "okay/Core/Console.hpp"
#include "okay/Core/FrameStats.hpp"
#include "okay/Core/Game.hpp"
#include "okay/Core/GameLoop.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Core/LoopConsole.hpp"
#include "okay/Core/Random.hpp"
#include "okay/Core/SaveFramework.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Core/TimeSnapshot.hpp"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace okay;

namespace {

bool Near(double a, double b, double tol = 1e-6) { return std::fabs(a - b) <= tol; }

void Fresh() {
    Game::SetPauseOnFocusLoss(false);
    Game::NotifyFocus(true);
    Game::Reset();
    Time::ResetForTesting();
    Time::SetFixedDeltaTime(0.02f);
    ClockSet::Global().Reset();
    Random::Shared().Seed(0xC0FFEEull);
}

std::string Slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream o; o << f.rdbuf();
    return o.str();
}

std::vector<std::string> Lines(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream is(s);
    for (std::string l; std::getline(is, l);) out.push_back(l);
    return out;
}

}  // namespace

int main() {
    RUN_SUITE("time_snapshot");
    Log::console = false;
    namespace fs = std::filesystem;
    const fs::path tmp = fs::temp_directory_path() / "okay_time_snapshot_test";
    fs::remove_all(tmp);
    fs::create_directories(tmp);

    // ---- capture, write, read, apply: the clocks come back as they were ------------------------------------------------------------------------
    {
        Fresh();
        Clock& world = Clock::Get("world");
        world.SetScale(0.5f);
        Clock& stun = Clock::Get("stun");
        stun.SetSource(Clock::Source::Scaled);
        stun.SetHoldsWhileGamePaused(true);
        stun.Pause();
        for (int i = 0; i < 10; ++i) ClockSet::Global().Tick(0.1f, 0.1f, false);
        Clock::Get("ui").SetScale(1.5f);
        world.RampScale(4.0f, 8.0f);
        ClockSet::Global().Tick(2.0f, 2.0f, false);                 // the ramp is a quarter of the way
        Time::SetTimeScale(0.75f);
        Random::Shared().Seed(99);
        for (int i = 0; i < 7; ++i) Random::Shared().NextUInt64();

        const TimeSnapshot snap = TimeSnapshot::Capture();
        CHECK(snap.timeScale == 0.75f);
        CHECK(snap.clocks.size() == 5);                             // game, ui, cinematic, world, stun
        CHECK(snap.randoms.count("shared") == 1);
        const double worldElapsed = world.Elapsed();
        const float worldScale = world.Scale();

        SaveDocument doc;
        snap.Write(doc.Writer("time"));
        // through the text form a slot file stores
        SaveDocument stored;
        CHECK(SaveDocument::Parse(doc.Serialize(), stored));
        TimeSnapshot loaded;
        CHECK(TimeSnapshot::Read(stored.Reader("time"), loaded));
        CHECK(loaded == snap);                                      // every field, bit for bit (doubles included)

        // the world moves on: everything differs...
        Clock::Get("world").Reset();
        Clock::Get("stun").Reset();
        Clock::Get("ui").SetScale(1.0f);
        Time::SetTimeScale(1.0f);
        for (int i = 0; i < 50; ++i) Random::Shared().NextUInt64();
        for (int i = 0; i < 10; ++i) ClockSet::Global().Tick(0.1f, 0.1f, false);
        CHECK(world.Elapsed() != worldElapsed);

        // ...and loading puts it back
        const std::uint64_t nextExpected = [&] { Random r; r.SetState(snap.randoms.at("shared")); return r.NextUInt64(); }();
        loaded.Apply();
        CHECK(world.Elapsed() == worldElapsed);                     // exact double
        CHECK(world.Scale() == worldScale && world.Ramping() && Near(world.RampTarget(), 4.0) && Near(world.RampSecondsLeft(), 6.0));
        CHECK(stun.Paused() && stun.Elapsed() == snap.clocks[4].elapsed);
        CHECK(Clock::Get("ui").Scale() == 1.5f);
        CHECK(Time::BaseTimeScale() == 0.75f);
        CHECK(Random::Shared().NextUInt64() == nextExpected);       // the next roll is the roll the player would have had
        // the ramp carries on where it was
        ClockSet::Global().Tick(1.0f, 1.0f, false);
        CHECK(Near(world.RampSecondsLeft(), 5.0));
        // a clock the saved game had and this process does not is created with the saved settings
        ClockSet::Global().Remove("stun");
        CHECK(!Clock::Exists("stun"));
        loaded.Apply();
        CHECK(Clock::Exists("stun") && Clock::Get("stun").GetSource() == Clock::Source::Scaled && Clock::Get("stun").HoldsWhileGamePaused() && Clock::Get("stun").Paused());
        // built-in clocks keep their code-defined source: a save cannot turn "ui" into a scaled clock
        TimeSnapshot tampered = loaded;
        for (auto& c : tampered.clocks) if (c.name == "ui") { c.source = Clock::Source::Scaled; c.holdsWhileGamePaused = true; }
        tampered.Apply();
        CHECK(Clock::Get("ui").GetSource() == Clock::Source::Unscaled && !Clock::Get("ui").HoldsWhileGamePaused());
    }

    // ---- what it must not touch -----------------------------------------------------------------------------------------------------------------
    {
        Fresh();
        for (int i = 0; i < 30; ++i) { Time::Step(0.1f); ClockSet::Global().TickFromTime(); }
        const double elapsed = Time::ElapsedTimeD(), unscaled = Time::UnscaledTime();
        const std::uint64_t frames = Time::FrameCount();
        const TimeSnapshot snap = TimeSnapshot::Capture();
        for (int i = 0; i < 30; ++i) { Time::Step(0.1f); ClockSet::Global().TickFromTime(); }
        Game::PushPause("menu");
        Time::HitStop(1.0f, 0.0f);
        snap.Apply();
        CHECK(Time::ElapsedTimeD() > elapsed && Time::UnscaledTime() > unscaled && Time::FrameCount() == frames + 30);   // process clocks keep going forward
        CHECK(Game::Paused() && Game::IsPausedBy("menu") && Time::HitStopActive());                                     // session state is not a save game's business
        Game::PopPause("menu");
        Time::CancelHitStop();
        // Apply options: each part can be left out
        Fresh();
        Time::SetTimeScale(0.3f);
        Clock::Get("opt").SetScale(0.4f);
        const TimeSnapshot s2 = TimeSnapshot::Capture();
        Time::SetTimeScale(1.0f); Clock::Get("opt").SetScale(1.0f);
        Random::Shared().Seed(5);
        const std::uint64_t before = Random::Shared().GetState().s0;
        TimeSnapshot::ApplyOptions none; none.clocks = none.timeScale = none.random = false;
        s2.Apply(ClockSet::Global(), none);
        CHECK(Time::BaseTimeScale() == 1.0f && Clock::Get("opt").Scale() == 1.0f && Random::Shared().GetState().s0 == before);
        TimeSnapshot::ApplyOptions onlyClocks = none; onlyClocks.clocks = true;
        s2.Apply(ClockSet::Global(), onlyClocks);
        CHECK(Time::BaseTimeScale() == 1.0f && Clock::Get("opt").Scale() == 0.4f);
        Time::SetTimeScale(1.0f);
    }

    // ---- streams of your own, by name -----------------------------------------------------------------------------------------------------------------
    {
        Fresh();
        Random loot(2024), world(7);
        for (int i = 0; i < 5; ++i) { loot.NextUInt64(); world.NextUInt64(); }
        TimeSnapshot snap = TimeSnapshot::Capture(ClockSet::Global(), /*sharedRandom=*/false);
        CHECK(snap.randoms.empty());
        snap.AddRandom("loot", loot).AddRandom("world", world);
        SaveDocument doc;
        snap.Write(doc.Writer("t"));
        const std::uint64_t nextLoot = Random(loot).NextUInt64(), nextWorld = Random(world).NextUInt64();
        for (int i = 0; i < 40; ++i) { loot.NextUInt64(); world.NextUInt64(); }
        TimeSnapshot back;
        CHECK(TimeSnapshot::Read(doc.Reader("t"), back));
        CHECK(back.randoms.size() == 2);
        CHECK(back.ApplyRandom("loot", loot) && back.ApplyRandom("world", world));
        CHECK(loot.NextUInt64() == nextLoot && world.NextUInt64() == nextWorld);
        Random other(1);
        const Random::State untouched = other.GetState();
        CHECK(!back.ApplyRandom("missing", other) && other.GetState().s0 == untouched.s0);
        // generator state with the high bit set survives the signed text form
        Random hi; hi.SetState({0xFFFFFFFFFFFFFFF0ull, 0x8000000000000001ull});
        TimeSnapshot big;
        big.AddRandom("hi", hi);
        SaveDocument d2; big.Write(d2.Writer("t"));
        SaveDocument d3; CHECK(SaveDocument::Parse(d2.Serialize(), d3));
        TimeSnapshot big2; CHECK(TimeSnapshot::Read(d3.Reader("t"), big2));
        CHECK(big2.randoms.at("hi").s0 == 0xFFFFFFFFFFFFFFF0ull && big2.randoms.at("hi").s1 == 0x8000000000000001ull);
    }

    // ---- reading: nothing written, an older block, hostile numbers -------------------------------------------------------------------------------------------
    {
        TimeSnapshot keep;
        keep.timeScale = 0.9f;
        CHECK(!TimeSnapshot::Read(SaveReader(SaveBlock()), keep) && keep.timeScale == 0.9f);   // no snapshot in this block: nothing changed
        SaveBlock b;
        b.SetInt("v", 1); b.SetFloat("timeScale", -5.0f);
        b.SetInt("clocks.n", 2000000);                                  // a hostile count is capped, not looped for
        b.SetInt("randoms.n", -4);
        TimeSnapshot t;
        CHECK(TimeSnapshot::Read(SaveReader(b), t));
        CHECK(t.timeScale == 0.0f && t.clocks.empty() && t.randoms.empty());
        SaveBlock c;
        c.SetInt("v", 1); c.SetInt("clocks.n", 3);
        SaveWriter w(c);
        { SaveWriter cw = w.Scope("clocks").Scope("0"); cw.String("name", "a"); cw.String("elapsed", "-7"); cw.Float("scale", 1e30f); cw.Int64("ticks", -5); }
        { SaveWriter cw = w.Scope("clocks").Scope("1"); cw.String("name", "b"); cw.String("elapsed", "not a number"); }
        { SaveWriter cw = w.Scope("clocks").Scope("2"); cw.Float("scale", 2.0f); }   // no name: skipped
        TimeSnapshot t2;
        CHECK(TimeSnapshot::Read(SaveReader(c), t2));
        CHECK(t2.clocks.size() == 2);
        CHECK(t2.clocks[0].elapsed == 0.0 && t2.clocks[0].scale == Time::kMaxTimeScale && t2.clocks[0].ticks == 0);
        CHECK(t2.clocks[1].elapsed == 0.0 && t2.clocks[1].scale == 1.0f);
        SaveBlock d;
        d.SetInt("v", 1); d.SetInt("randoms.n", 2);
        SaveWriter wd(d);
        wd.Scope("randoms").Scope("0").String("name", "half");           // state missing: skipped
        { SaveWriter rw = wd.Scope("randoms").Scope("1"); rw.String("name", "ok"); rw.Int64("s0", 5); rw.Int64("s1", 6); }
        TimeSnapshot t3;
        CHECK(TimeSnapshot::Read(SaveReader(d), t3) && t3.randoms.size() == 1 && t3.randoms.at("ok").s0 == 5);
        // applying a read snapshot with huge elapsed or garbage never poisons a clock
        Fresh();
        t2.Apply();
        CHECK(Clock::Get("a").Elapsed() == 0.0 && Clock::Get("a").Scale() == Time::kMaxTimeScale);
        ClockSet::Global().Remove("a"); ClockSet::Global().Remove("b");
        // a document that holds other blocks next to it: the snapshot lives in its own block
        SaveDocument doc;
        doc.Writer("game").Int("gold", 120);
        TimeSnapshot::Capture().Write(doc.Writer("time"));
        CHECK(doc.Has("game") && doc.Has("time") && doc.Reader("game").Int("gold", 0) == 120);
        CHECK(doc.RoundTrips());
    }

    // ---- SaveStore: a whole slot on disk ------------------------------------------------------------------------------------------------------------------------
    {
        Fresh();
        Clock::Get("slot").SetScale(0.6f);
        for (int i = 0; i < 25; ++i) ClockSet::Global().Tick(0.016f, 0.016f, false);
        const double slotElapsed = Clock::Get("slot").Elapsed();
        SaveStore store((tmp / "saves").string());
        SaveDocument doc;
        TimeSnapshot::Capture().Write(doc.Writer("time"));
        SaveSlotInfo meta;
        std::string err;
        CHECK(store.Write("s1", doc, meta, &err));
        Clock::Get("slot").Reset();
        SaveDocument read;
        CHECK(store.Read("s1", read));
        TimeSnapshot snap;
        CHECK(TimeSnapshot::Read(read.Reader("time"), snap));
        snap.Apply();
        CHECK(Clock::Get("slot").Elapsed() == slotElapsed && Clock::Get("slot").Scale() == 0.6f);
        ClockSet::Global().Remove("slot");
    }

    // ---- FrameStats CSV ----------------------------------------------------------------------------------------------------------------------------------------
    {
        FrameStats st(4);
        CHECK(st.ToCsv() == "frame,start_ms,frame_ms,fps\n");           // no frames: just the header
        CHECK(st.ToCsv(false).empty());
        for (double ms : {10.0, 20.0, 40.0, 8.0, 16.0}) st.Push(ms);   // 5 pushed, the window holds the last 4
        const auto lines = Lines(st.ToCsv());
        CHECK(lines.size() == 5 && lines[0] == "frame,start_ms,frame_ms,fps");
        CHECK(lines[1] == "1,10.0000,20.0000,50.00");                   // frame 0 left the window; the numbering is the loop's
        CHECK(lines[2] == "2,30.0000,40.0000,25.00");
        CHECK(lines[3] == "3,70.0000,8.0000,125.00");
        CHECK(lines[4] == "4,78.0000,16.0000,62.50");
        FrameStats z(8);
        z.Push(0.0); z.Push(std::nan("")); z.Push(-3.0);                // bad frame times are 0 ms frames: fps 0, not inf or nan
        const auto zl = Lines(z.ToCsv(false));
        CHECK(zl.size() == 3 && zl[0] == "0,0.0000,0.0000,0.00" && zl[2] == "2,0.0000,0.0000,0.00");
        FrameStats off(0);
        off.Push(5.0);
        CHECK(off.ToCsv(false).empty());
        // the file
        const std::string path = (tmp / "frames.csv").string();
        std::string err;
        CHECK(st.SaveCsv(path, &err) && err.empty());
        CHECK(Slurp(path) == st.ToCsv());
        CHECK(!st.SaveCsv("", &err) && !err.empty());
        CHECK(!st.SaveCsv((tmp / "no" / "such" / "dir" / "x.csv").string(), &err) && err.find("could not write") == 0);
        // a big window stays linear and every row is well formed
        FrameStats big(FrameStats::kMaxWindow);
        for (int i = 0; i < 100000; ++i) big.Push(16.0 + (i % 7));
        const std::string text = big.ToCsv();
        CHECK(Lines(text).size() == 100001);
    }

    // ---- LoopConsole -------------------------------------------------------------------------------------------------------------------------------------------------
    {
        Fresh();
        Console::Reset();
        GameLoop loop;
        {
            LoopConsole commands(loop);
            CHECK(Console::HasCommand("time.stats") && Console::HasCommand("time.stats_csv") && Console::HasCommand("time.scale") &&
                  Console::HasCommand("time.debug_scale") && Console::HasCommand("time.pause") && Console::HasCommand("time.step"));
            for (int i = 0; i < 10; ++i) loop.BeginFrame(0.016);
            loop.BeginFrame(0.3);                                         // one hitch, clamped
            Console::Result r = Console::Execute("time.stats");
            CHECK(r.ok && r.output.find("11 frames") != std::string::npos && r.output.find("1 hitch") != std::string::npos);
            CHECK(r.output.find("1 clamped") != std::string::npos && r.output.find("worst 300 ms") != std::string::npos);

            // scale
            r = Console::Execute("time.scale");
            CHECK(r.ok && r.output == "time.scale = 1");
            r = Console::Execute("time.scale 0.25");
            CHECK(r.ok && r.output == "time.scale = 0.25" && Time::BaseTimeScale() == 0.25f);
            for (const char* bad : {"time.scale abc", "time.scale nan", "time.scale inf", "time.scale -1", "time.scale 0.5x", "time.scale \"\""}) {
                r = Console::Execute(bad);
                CHECK(!r.ok);
            }
            CHECK(Time::BaseTimeScale() == 0.25f);                         // none of them changed it
            r = Console::Execute("time.scale 1e9");
            CHECK(r.ok && Time::BaseTimeScale() == Time::kMaxTimeScale);   // capped like every scale
            Console::Execute("time.scale 1");

            // debug scale
            r = Console::Execute("time.debug_scale 4");
            CHECK(r.ok && loop.DebugTimeScale() == 4.0f && r.output == "debug scale = 4");
            CHECK(!Console::Execute("time.debug_scale -2").ok && !Console::Execute("time.debug_scale nan").ok && loop.DebugTimeScale() == 4.0f);
            Console::Execute("time.debug_scale 1");

            // pause and step
            r = Console::Execute("time.step");
            CHECK(!r.ok && r.output.find("not paused") != std::string::npos);
            r = Console::Execute("time.pause on");
            CHECK(r.ok && Game::Paused() && Game::IsPausedBy("manual"));
            const std::uint64_t f0 = Time::FrameCount();
            r = Console::Execute("time.step 3");
            CHECK(r.ok && loop.StepsRemaining() == 3);
            for (int i = 0; i < 5; ++i) loop.BeginFrame(0.016);
            CHECK(Time::FrameCount() == f0 + 5 && loop.StepsRemaining() == 0 && Game::Paused());
            CHECK(!Console::Execute("time.step 0").ok && !Console::Execute("time.step x").ok && !Console::Execute("time.step 2000000").ok);
            r = Console::Execute("time.pause off");
            CHECK(r.ok && !Game::Paused() && r.output == "running");
            r = Console::Execute("time.pause");                            // toggles
            CHECK(Game::Paused());
            Console::Execute("time.pause");
            CHECK(!Game::Paused());
            CHECK(!Console::Execute("time.pause maybe").ok);
            Game::PushPause("menu");
            r = Console::Execute("time.pause off");                        // someone else's pause stays
            CHECK(r.ok && Game::Paused() && r.output == "still paused by: menu");
            Game::PopPause("menu");

            // csv
            const std::string csv = (tmp / "console.csv").string();
            r = Console::Execute("time.stats_csv \"" + csv + "\"");
            CHECK(r.ok && r.output.find("wrote") == 0 && Lines(Slurp(csv)).size() == loop.Stats().Size() + 1);
            CHECK(!Console::Execute("time.stats_csv").ok);
            CHECK(!Console::Execute("time.stats_csv \"" + (tmp / "nope" / "x.csv").string() + "\"").ok);
        }
        // the commands go when the object goes
        CHECK(!Console::HasCommand("time.stats") && !Console::HasCommand("time.step"));
        // two loops, two prefixes
        GameLoop other;
        LoopConsole a(loop, "client."), b(other, "server.");
        other.BeginFrame(0.016); other.BeginFrame(0.016);
        CHECK(Console::Execute("client.stats").output.find("16 frames") != std::string::npos);
        CHECK(Console::Execute("server.stats").output.find("2 frames") != std::string::npos);
        CHECK(a.Prefix() == "client." && b.Prefix() == "server.");
        Console::Reset();
    }

    fs::remove_all(tmp);
    Fresh();
    TEST_MAIN_RESULT();
}
