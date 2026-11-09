#include "test_framework.hpp"
#include "okay/Debug/ConsoleModel.hpp"

#include <cstdio>
#include <string>
#include <vector>
#ifndef _WIN32
#include <thread>
#endif

using namespace okay;
using L = Log::Level;

static int CountOf(const std::string& s, const std::string& pat) {
    int n = 0; for (size_t p = s.find(pat); p != std::string::npos; p = s.find(pat, p + pat.size())) ++n; return n;
}

int main() {
    RUN_SUITE("debug_console");

    // keep the test output quiet and leave the global Log as it was
    const bool oldConsole = Log::console;
    const Log::Sink oldSink = Log::sink;
    const Log::Level oldMin = Log::minLevel;
    Log::console = false;
    Log::sink = nullptr;

    // ---- ring buffer wrap ---------------------------------------------------------------
    {
        ConsoleModel c(5);
        for (int i = 0; i < 12; ++i) c.Add(L::Info, "msg " + std::to_string(i));
        CHECK(c.Size() == 5 && c.TotalReceived() == 12);
        std::vector<ConsoleEntry> e = c.Entries();
        CHECK(e.size() == 5 && e[0].message == "msg 7" && e[4].message == "msg 11");
        CHECK(e[0].seq == 7 && e[4].seq == 11);
        c.SetCapacity(2);
        e = c.Entries();
        CHECK(e.size() == 2 && e[0].message == "msg 10" && e[1].message == "msg 11");
        c.Add(L::Info, "msg 12");
        e = c.Entries();
        CHECK(e.size() == 2 && e[0].message == "msg 11" && e[1].message == "msg 12");
        c.SetCapacity(10);
        c.Add(L::Info, "msg 13");
        CHECK(c.Size() == 3 && c.Entries()[0].message == "msg 11" && c.Entries()[2].message == "msg 13");
        c.Clear();
        CHECK(c.Size() == 0 && c.Rows().empty());
        CHECK(c.TotalReceived() == 14);                              // history of the counter survives Clear
        ConsoleModel zero(0);
        zero.Add(L::Info, "x");
        CHECK(zero.Size() == 0 && zero.TotalReceived() == 1);
    }

    // ---- collapse and uncollapse ------------------------------------------------------------
    {
        ConsoleModel c;
        CHECK(c.Collapse());                                          // on by default
        double now = 0.0;
        c.SetClock([&now] { return now; });
        c.SetFrame(10);
        now = 1.0; c.Add(L::Info, "A");
        now = 2.0; c.Add(L::Info, "A"); c.NextFrame();
        now = 3.0; c.Add(L::Info, "A");
        now = 4.0; c.Add(L::Info, "B");
        now = 5.0; c.Add(L::Info, "A");
        now = 6.0; c.Add(L::Warning, "A");                            // same text, other level: separate row
        now = 7.0; c.Add(L::Warning, "A", "net");                     // other category: separate row

        std::vector<ConsoleRow> rows = c.Rows();
        CHECK(rows.size() == 5);
        if (rows.size() == 5) {
            CHECK(rows[0].entry.message == "A" && rows[0].repeat == 3);
            CHECK_NEAR(rows[0].entry.timeSec, 1.0, 1e-9);            // first occurrence
            CHECK_NEAR(rows[0].lastTimeSec, 3.0, 1e-9);              // and the last
            CHECK(rows[0].entry.frame == 10 && rows[0].lastFrame == 11);
            CHECK(rows[0].lastSeq == 2 && rows[0].entry.seq == 0);
            CHECK(rows[1].entry.message == "B" && rows[1].repeat == 1);
            CHECK(rows[2].entry.message == "A" && rows[2].repeat == 1);   // a run, not a global count
            CHECK(rows[3].entry.level == L::Warning && rows[3].repeat == 1);
            CHECK(rows[4].entry.category == "net" && rows[4].repeat == 1);
        }
        c.SetCollapse(false);
        CHECK(!c.Collapse() && c.Rows().size() == 7);
        c.SetCollapse(true);
        CHECK(c.Rows().size() == 5);
        CHECK(c.Rows(ConsoleFilter(), false).size() == 7);           // explicit view, settings untouched
        CHECK(c.Collapse());

        // collapsing happens after filtering: Error X, Info Y, Error X shows one row with an error filter
        ConsoleModel d;
        d.Add(L::Error, "X"); d.Add(L::Info, "Y"); d.Add(L::Error, "X");
        CHECK(d.Rows().size() == 3);
        ConsoleFilter f; f.levelMask = ConsoleModel::LevelBit(L::Error);
        d.SetFilter(f);
        std::vector<ConsoleRow> er = d.Rows();
        CHECK(er.size() == 1 && er[0].repeat == 2);
        CHECK(d.Entries().size() == 3);                              // the raw data is untouched
    }

    // ---- filters --------------------------------------------------------------------------------
    {
        ConsoleModel c;
        c.Add(L::Trace, "trace line");
        c.Add(L::Info, "loaded level", "assets");
        c.Add(L::Warning, "texture is large", "assets");
        c.Add(L::Error, "failed to open Assets/Hero.okayscene:12", "assets");
        c.Add(L::Error, "NullReference [Object: World/Player]", "script");
        c.Add(L::Info, "frame done", "core");
        CHECK(c.Rows().size() == 6);

        ConsoleFilter f;
        f.levelMask = ConsoleModel::LevelBit(L::Warning) | ConsoleModel::LevelBit(L::Error);
        CHECK(c.Rows(f, true).size() == 3);
        f.levelMask = ConsoleModel::LevelBit(L::Trace);
        CHECK(c.Rows(f, true).size() == 1);
        f.levelMask = 0;
        CHECK(c.Rows(f, true).empty());                               // nothing selected, nothing shown
        f = ConsoleFilter(); f.category = "assets";
        CHECK(c.Rows(f, true).size() == 3);
        f.category = "ASSETS"; CHECK(c.Rows(f, true).empty());       // categories match exactly
        f = ConsoleFilter(); f.text = "TEXTURE";                       // text is case insensitive by default
        CHECK(c.Rows(f, true).size() == 1);
        f.caseSensitive = true; CHECK(c.Rows(f, true).empty());
        f.text = "texture"; CHECK(c.Rows(f, true).size() == 1);
        f = ConsoleFilter(); f.text = "script";                       // matches the category too
        CHECK(c.Rows(f, true).size() == 1);
        f = ConsoleFilter(); f.text = "World/Player";                 // and the source path
        CHECK(c.Rows(f, true).size() == 1);
        f = ConsoleFilter(); f.text = "hero";
        CHECK(c.Rows(f, true).size() == 1);
        // combined: level and text and category
        f = ConsoleFilter(); f.levelMask = ConsoleModel::LevelBit(L::Error); f.category = "assets"; f.text = "open";
        CHECK(c.Rows(f, true).size() == 1);
        f.text = "nothing like this"; CHECK(c.Rows(f, true).empty());

        // the model's own filter drives Rows() and CopyAll()
        f = ConsoleFilter(); f.levelMask = ConsoleModel::LevelBit(L::Error);
        c.SetFilter(f);
        CHECK(c.GetFilter().levelMask == ConsoleModel::LevelBit(L::Error));
        CHECK(c.Rows().size() == 2);
        CHECK(CountOf(c.CopyAll(), "\n") == 2);
        CHECK(CountOf(c.CopyAll(false), "\n") == 6);                  // everything, whatever the filter
        c.SetFilter(ConsoleFilter());
        CHECK(c.Rows().size() == 6);

        ConsoleCounts n = c.Counts();
        CHECK(n.trace == 1 && n.info == 2 && n.warning == 1 && n.error == 2);
    }

    // ---- search -----------------------------------------------------------------------------------------
    {
        ConsoleModel c;
        c.Add(L::Info, "alpha one");
        c.Add(L::Info, "beta two");
        c.Add(L::Info, "alpha three");
        c.Add(L::Info, "gamma four");
        c.Add(L::Info, "alpha five");
        CHECK(c.Find("alpha") == 0);
        CHECK(c.Find("alpha", 1) == 2);
        CHECK(c.Find("alpha", 3) == 4);
        CHECK(c.Find("alpha", 5) == 0);                                // past the end: from the start
        CHECK(c.Find("beta", 3) == 1);                                  // wraps around
        CHECK(c.Find("alpha", 3, false) == 2);                          // backwards
        CHECK(c.Find("alpha", 0, false) == 0);
        CHECK(c.Find("beta", 0, false) == 1);                           // backwards wrap
        CHECK(c.Find("nothing") == -1 && c.Find("") == -1);
        CHECK(c.Find("ALPHA", 0, true, true) == -1);                    // case sensitive
        CHECK(c.Find("ALPHA") == 0);
        std::vector<size_t> all = c.FindAll("alpha");
        CHECK(all.size() == 3 && all[0] == 0 && all[1] == 2 && all[2] == 4);
        CHECK(c.FindAll("zzz").empty() && c.FindAll("").empty());
        // search indexes are rows of the current view
        ConsoleFilter f; f.text = "alpha";
        c.SetFilter(f);
        CHECK(c.Find("five") == 2);
        ConsoleModel empty;
        CHECK(empty.Find("x") == -1);
    }

    // ---- pause, pause on error ----------------------------------------------------------------------------
    {
        ConsoleModel c;
        c.Add(L::Info, "one");
        c.SetPaused(true);
        CHECK(c.Paused());
        c.Add(L::Info, "two"); c.Add(L::Error, "three");
        CHECK(c.Size() == 1 && c.DroppedWhilePaused() == 2);
        c.SetPaused(false);
        c.Add(L::Info, "four");
        CHECK(c.Size() == 2 && c.Entries()[1].message == "four");

        // the error pause request flag
        CHECK(!c.PauseOnError() && !c.PauseRequested());
        c.Add(L::Error, "ignored: pause on error is off");
        CHECK(!c.PauseRequested());
        c.SetPauseOnError(true);
        c.Add(L::Warning, "a warning does not ask");
        CHECK(!c.PauseRequested() && !c.ConsumePauseRequest());
        c.Add(L::Error, "boom");
        CHECK(c.PauseRequested());
        CHECK(c.ConsumePauseRequest());                                  // once
        CHECK(!c.ConsumePauseRequest() && !c.PauseRequested());
        c.Add(L::Error, "boom"); c.Add(L::Error, "boom");
        CHECK(c.ConsumePauseRequest() && !c.ConsumePauseRequest());       // repeated errors raise it once until consumed
        c.Add(L::Error, "boom");
        c.Clear();
        CHECK(!c.PauseRequested());                                      // clearing the console drops a pending request
        c.SetPaused(true);
        c.Add(L::Error, "dropped, so no request");
        CHECK(!c.PauseRequested());
    }

    // ---- source references --------------------------------------------------------------------------------------
    {
        ConsoleSource s = ParseConsoleSource("NullReference [Object: World/Props/Crate] in Update");
        CHECK(s.kind == ConsoleSource::Kind::Object && s.path == "World/Props/Crate" && s.line == 0 && s.Valid());
        s = ParseConsoleSource("[object:  A/B ]");
        CHECK(s.kind == ConsoleSource::Kind::Object && s.path == "A/B");
        s = ParseConsoleSource("bad [GameObject: Player] and [Asset: x.png]");
        CHECK(s.kind == ConsoleSource::Kind::Object && s.path == "Player");   // the earliest tag wins
        s = ParseConsoleSource("missing [Asset: Assets/crate.png]");
        CHECK(s.kind == ConsoleSource::Kind::Asset && s.path == "Assets/crate.png" && s.line == 0);
        s = ParseConsoleSource("[File: Assets/script.okayscript:44]");
        CHECK(s.kind == ConsoleSource::Kind::Asset && s.path == "Assets/script.okayscript" && s.line == 44);
        s = ParseConsoleSource("Failed to parse Assets/Level1.okayscene:12");
        CHECK(s.kind == ConsoleSource::Kind::Asset && s.path == "Assets/Level1.okayscene" && s.line == 12 && s.column == 0);
        s = ParseConsoleSource("error in script.okayscript:12:5: unexpected token");
        CHECK(s.path == "script.okayscript" && s.line == 12 && s.column == 5);
        s = ParseConsoleSource("(see level.okayscene:7).");
        CHECK(s.path == "level.okayscene" && s.line == 7);
        s = ParseConsoleSource("C:\\proj\\src\\main.cpp:77: error: oops");
        CHECK(s.path == "C:\\proj\\src\\main.cpp" && s.line == 77);
        s = ParseConsoleSource("loading 'Assets/c.json:3'");
        CHECK(s.kind == ConsoleSource::Kind::Asset && s.path == "Assets/c.json" && s.line == 3);   // quotes are not part of it
        // things that look similar but are not references
        CHECK(!ParseConsoleSource("took 12.5:30 seconds").Valid());
        CHECK(!ParseConsoleSource("version v1.2:30").Valid());
        CHECK(!ParseConsoleSource("listening on localhost:8080").Valid());
        CHECK(!ParseConsoleSource("plain text, nothing here").Valid());
        CHECK(!ParseConsoleSource("[Object: ]").Valid());
        CHECK(!ParseConsoleSource("[Object: unterminated").Valid());
        CHECK(!ParseConsoleSource("file.okayscene").Valid());              // no line, no tag: not guessed
        CHECK(!ParseConsoleSource("").Valid());
        // entries carry the parsed source
        ConsoleModel c;
        c.Add(L::Error, "bad value [Object: World/Enemy]");
        c.Add(L::Error, "syntax error at game.okayscript:9");
        c.Add(L::Info, "no source");
        std::vector<ConsoleEntry> e = c.Entries();
        CHECK(e[0].source.kind == ConsoleSource::Kind::Object && e[0].source.path == "World/Enemy");
        CHECK(e[1].source.kind == ConsoleSource::Kind::Asset && e[1].source.line == 9);
        CHECK(!e[2].source.Valid());
    }

    // ---- attach to the Log -------------------------------------------------------------------------------------------
    {
        int previousCalls = 0;
        std::string previousLast;
        Log::sink = [&](Log::Level, const std::string& m) { ++previousCalls; previousLast = m; };

        ConsoleModel c;
        CHECK(!c.Attached());
        CHECK(c.Attach() && c.Attached());
        CHECK(!c.Attach());                                               // already attached

        double now = 5.0;
        c.SetClock([&now] { return now; });
        c.SetFrame(33);
        Log::Info("hello ", 42);
        CHECK(previousCalls == 1 && previousLast == "hello 42");           // the earlier sink still gets everything
        Log::Warning("careful");
        Log::Error("[net] lost ", 3, " packets");                           // a leading [tag] is the category
        Log::Error("bad thing [Object: World/Boss]");                       // not a category: a source reference
        Log::Category cat("physics");
        cat.Info("via category object");                                    // the sink sees no category: it stays empty
        CHECK(previousCalls == 5);
        std::vector<ConsoleEntry> e = c.Entries();
        CHECK(e.size() == 5);
        if (e.size() == 5) {
            CHECK(e[0].level == L::Info && e[0].message == "hello 42" && e[0].category.empty());
            CHECK(e[0].frame == 33 && std::abs(e[0].timeSec - 5.0) < 1e-9);
            CHECK(e[1].level == L::Warning && e[1].message == "careful");
            CHECK(e[2].level == L::Error && e[2].category == "net" && e[2].message == "lost 3 packets");
            CHECK(e[3].category.empty() && e[3].source.kind == ConsoleSource::Kind::Object && e[3].source.path == "World/Boss");
            CHECK(e[4].message == "via category object" && e[4].category.empty());
        }
        c.SetParseCategoryPrefix(false);
        Log::Info("[tag] kept whole");
        CHECK(c.Entries().back().category.empty() && c.Entries().back().message == "[tag] kept whole");
        c.SetParseCategoryPrefix(true);

        // log level filtering happens in the Log, so filtered messages never reach the console
        Log::minLevel = Log::Level::Warning;
        Log::Info("below the threshold");
        Log::minLevel = Log::Level::Trace;
        CHECK(c.Size() == 6);

        // detach restores the earlier sink
        c.Detach();
        CHECK(!c.Attached());
        int before = previousCalls;
        Log::Info("after detach");
        CHECK(previousCalls == before + 1 && c.Size() == 6);
        c.Detach();                                                        // harmless twice

        // attach again, then someone installs a sink on top: our part keeps working underneath
        CHECK(c.Attach());
        Log::Sink inner = Log::sink;
        int topCalls = 0;
        Log::sink = [&, inner](Log::Level l, const std::string& m) { ++topCalls; inner(l, m); };
        Log::Info("through both");
        CHECK(topCalls == 1 && c.Size() == 7);
        c.Detach();                                                        // not the top hook: it just goes quiet
        Log::Info("quiet");
        CHECK(topCalls == 2 && c.Size() == 7);
        CHECK(c.Attach());                                                 // a new hook on top; the old one stays quiet
        Log::Info("once");
        CHECK(c.Size() == 8 && c.Entries().back().message == "once");      // not recorded twice
        c.Detach();

        // a model that goes away while another sink sits on top of its hook leaves a hook that does nothing
        {
            Log::Sink saved = Log::sink;
            {
                ConsoleModel gone;
                gone.Attach();
                Log::Sink inner2 = Log::sink;
                Log::sink = [inner2](Log::Level l, const std::string& m) { inner2(l, m); };
                Log::Info("seen");
                CHECK(gone.Size() == 1);
            }
            Log::Info("after the model is gone");                          // must not crash
            Log::sink = saved;
            CHECK(true);
        }

        // two models side by side
        ConsoleModel a, b;
        a.Attach(); b.Attach();
        Log::Info("both");
        CHECK(a.Size() == 1 && b.Size() == 1);
        a.Detach();
        Log::Info("only b");
        CHECK(a.Size() == 1 && b.Size() == 2);
        b.Detach();
        Log::sink = nullptr;
    }

    // ---- text export ------------------------------------------------------------------------------------------------------
    {
        ConsoleModel c;
        double now = 1.5;
        c.SetClock([&now] { return now; });
        c.SetFrame(7);
        c.Add(L::Error, "boom", "net");
        c.Add(L::Error, "boom", "net");
        c.Add(L::Info, "plain");
        std::string text = c.CopyAll();
        CHECK(CountOf(text, "\n") == 2);
        CHECK(text.find("[1.500s f7] ERROR [net] boom (x2)") != std::string::npos);
        CHECK(text.find("[1.500s f7] INFO plain\n") != std::string::npos);
        std::string raw = c.CopyAll(false);
        CHECK(CountOf(raw, "\n") == 3 && raw.find("(x2)") == std::string::npos);
        CHECK(std::string(ConsoleModel::LevelName(L::Warning)) == "WARN" && std::string(ConsoleModel::LevelName(L::Trace)) == "TRACE");
        CHECK(ConsoleModel().CopyAll().empty());
    }

#ifndef _WIN32
    // ---- thread safety smoke test: threads log through the Log while another reads -------------------------------------------
    {
        ConsoleModel c(100000);
        c.Attach();
        const int kThreads = 4, kLines = 1500;
        std::vector<std::thread> th;
        for (int t = 0; t < kThreads; ++t)
            th.emplace_back([kLines, t] {
                for (int i = 0; i < kLines; ++i) Log::Info("thread ", t, " line ", i);
            });
        size_t maxSeen = 0;
        for (int k = 0; k < 200; ++k) {                                   // the UI side polls meanwhile
            std::vector<ConsoleRow> rows = c.Rows();
            if (rows.size() > maxSeen) maxSeen = rows.size();
            (void)c.Counts();
        }
        for (auto& x : th) x.join();
        c.Detach();
        CHECK(c.Size() == (size_t)kThreads * kLines);
        CHECK(c.TotalReceived() == (std::uint64_t)kThreads * kLines);
        CHECK(maxSeen <= c.Size());
        // each thread's lines arrive in order
        std::vector<int> next((size_t)kThreads, 0);
        bool ordered = true;
        for (const ConsoleEntry& e : c.Entries()) {
            int t = 0, i = 0;
            if (std::sscanf(e.message.c_str(), "thread %d line %d", &t, &i) == 2) {
                if (i != next[(size_t)t]++) ordered = false;
            }
        }
        CHECK(ordered);
    }
#endif

    Log::sink = oldSink;
    Log::console = oldConsole;
    Log::minLevel = oldMin;
    TEST_MAIN_RESULT();
}
