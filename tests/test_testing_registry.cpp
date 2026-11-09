// Tests for the play-test registry and runner (okay/Testing/TestRegistry.hpp):
// registration, filtering, running, the summary, JUnit XML, the text report and
// the command line (RunMain) with its exit codes.
#include "test_framework.hpp"
#include "okay/Testing/TestRegistry.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

using namespace okay::testing;

namespace {

int g_afterRequire = 0;
int g_ran = 0;

void PassBody() { ++g_ran; OKAY_EXPECT(1 + 1 == 2); OKAY_EXPECT_NEAR(0.1 + 0.2, 0.3, 1e-9); OKAY_EXPECT_EQ(3, 3); }
void FailTwiceBody() {
    ++g_ran;
    OKAY_EXPECT(1 == 2);                  // recorded, the test carries on
    OKAY_EXPECT_EQ(std::string("a"), std::string("b"));
    OKAY_NOTE("context line");
}
void RequireBody() { ++g_ran; OKAY_REQUIRE(false); ++g_afterRequire; }
void ThrowBody() { ++g_ran; throw std::runtime_error("kaboom <&>"); }
void ThrowIntBody() { ++g_ran; throw 42; }
void SkipBody() { ++g_ran; OKAY_SKIP("not on this machine"); }
void NearFailBody() { OKAY_EXPECT_NEAR(1.0, 2.0, 0.5); }
void NanBody() { OKAY_EXPECT_NEAR(std::nan(""), 1.0, 1e9); }
void XmlBody() { OKAY_FAIL("quote \" amp & lt < gt > tab\t ctl\x01 end"); }

std::string Slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool Contains(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

TestRegistry MakeRegistry() {
    TestRegistry reg;
    Registrar a("movement", "walks", &PassBody, "a.cpp", 10, reg);
    Registrar b("movement", "jumps", &FailTwiceBody, "a.cpp", 20, reg);
    Registrar c("ui", "menu_opens", &PassBody, "b.cpp", 5, reg);
    Registrar d("ui", "menu_throws", &ThrowBody, "b.cpp", 15, reg);
    Registrar e("ui", "menu_skipped", &SkipBody, "b.cpp", 25, reg);
    return reg;
}

} // namespace

int main() {
    RUN_SUITE("testing_registry");
    const std::string out = std::string(OKAY_TEST_OUTPUT_DIR) + "/testing_registry";
    std::filesystem::create_directories(out);

    // --- Matches: plain words are substrings, * and ? are wildcards ---
    {
        CHECK(TestRegistry::Matches("movement", "movement.walks"));
        CHECK(TestRegistry::Matches("walks", "movement.walks"));
        CHECK(!TestRegistry::Matches("jump", "movement.walks"));
        CHECK(TestRegistry::Matches("movement.*", "movement.walks"));
        CHECK(!TestRegistry::Matches("ui.*", "movement.walks"));
        CHECK(TestRegistry::Matches("*.walks", "movement.walks"));
        CHECK(TestRegistry::Matches("m?vement.w*s", "movement.walks"));
        CHECK(!TestRegistry::Matches("m?vement.w*x", "movement.walks"));
        CHECK(TestRegistry::Matches("*", "anything.at_all"));
        CHECK(TestRegistry::Matches("a*b*c", "aXXbYYc"));
        CHECK(!TestRegistry::Matches("a*b*c", "aXXbYYd"));
        CHECK(!TestRegistry::Matches("", "x.y"));
    }

    // --- Select: filters, excludes, order does not depend on registration ---
    {
        TestRegistry reg = MakeRegistry();
        CHECK(reg.Cases().size() == 5);
        auto all = reg.Select({});
        CHECK(all.size() == 5);
        // sorted by suite then name: movement.jumps, movement.walks, ui.menu_opens, ui.menu_skipped, ui.menu_throws
        CHECK(all[0]->FullName() == "movement.jumps");
        CHECK(all[1]->FullName() == "movement.walks");
        CHECK(all[2]->FullName() == "ui.menu_opens");
        CHECK(all[4]->FullName() == "ui.menu_throws");
        CHECK(reg.Select({"ui"}).size() == 3);
        CHECK(reg.Select({"movement.*"}).size() == 2);
        CHECK(reg.Select({"walks", "menu_opens"}).size() == 2);
        CHECK(reg.Select({"ui"}, {"throws"}).size() == 2);
        CHECK(reg.Select({}, {"ui.*"}).size() == 2);
        CHECK(reg.Select({"nothing_like_this"}).empty());
        reg.Clear();
        CHECK(reg.Cases().empty());
    }

    // --- Run: every outcome ---
    {
        TestRegistry reg = MakeRegistry();
        Registrar req("misc", "require_stops", &RequireBody, "c.cpp", 1, reg);
        Registrar thr("misc", "throws_int", &ThrowIntBody, "c.cpp", 2, reg);
        g_ran = 0; g_afterRequire = 0;
        std::ostringstream log;
        RunOptions opt; opt.out = &log;
        RunSummary s = reg.Run(opt);
        CHECK(s.results.size() == 7);
        CHECK(g_ran == 7);
        CHECK(g_afterRequire == 0);                  // OKAY_REQUIRE ended the body
        CHECK(s.Passed() == 2);
        CHECK(s.Failed() == 2);                      // jumps + require_stops
        CHECK(s.Errored() == 2);                     // menu_throws + throws_int
        CHECK(s.Skipped() == 1);
        CHECK(!s.Ok());
        CHECK(s.ExitCode() == 1);
        auto find = [&](const char* name) -> const TestResult* {
            for (const auto& r : s.results) if (r.FullName() == name) return &r;
            return nullptr;
        };
        const TestResult* jumps = find("movement.jumps");
        CHECK(jumps && jumps->status == TestStatus::Failed);
        if (jumps) {
            CHECK(jumps->failures.size() == 2);       // OKAY_EXPECT kept going
            CHECK(jumps->failures[0].message == "1 == 2");
            CHECK(jumps->failures[0].file == "a.cpp" || jumps->failures[0].line > 0);
            CHECK(Contains(jumps->failures[1].message, "\"a\"") || Contains(jumps->failures[1].message, "a vs b"));
            CHECK(jumps->notes.size() == 1 && jumps->notes[0] == "context line");
            CHECK(jumps->file == "a.cpp" && jumps->line == 20);
        }
        const TestResult* thr2 = find("ui.menu_throws");
        CHECK(thr2 && thr2->status == TestStatus::Errored && Contains(thr2->error, "kaboom"));
        const TestResult* thr3 = find("misc.throws_int");
        CHECK(thr3 && thr3->status == TestStatus::Errored && thr3->error == "unknown exception");
        const TestResult* skip = find("ui.menu_skipped");
        CHECK(skip && skip->status == TestStatus::Skipped && skip->skipReason == "not on this machine");
        const TestResult* req2 = find("misc.require_stops");
        CHECK(req2 && req2->status == TestStatus::Failed && req2->failures.size() == 1);

        // The console text names every outcome and lists the failures at the end.
        const std::string text = log.str();
        CHECK(Contains(text, "[ RUN      ] movement.jumps"));
        CHECK(Contains(text, "[       OK ] movement.walks"));
        CHECK(Contains(text, "[  FAILED  ] movement.jumps"));
        CHECK(Contains(text, "[  ERROR   ] ui.menu_throws"));
        CHECK(Contains(text, "[  SKIPPED ] ui.menu_skipped"));
        CHECK(Contains(text, "context line"));            // notes show with a failure
        CHECK(Contains(text, "7 tests ran"));
        CHECK(Contains(text, "[  FAILED  ] 4 tests"));
    }

    // --- fail-fast, filters on Run, verbose notes ---
    {
        TestRegistry reg = MakeRegistry();
        std::ostringstream log;
        RunOptions opt; opt.out = &log; opt.failFast = true;
        RunSummary s = reg.Run(opt);
        // movement.jumps fails first (sorted), so only it ran
        CHECK(s.results.size() == 1 && s.results[0].FullName() == "movement.jumps");

        RunOptions only; only.out = &log; only.include = {"walks"};
        CHECK(reg.Run(only).results.size() == 1);
        CHECK(reg.Run(only).Ok());
        CHECK(reg.Run(only).ExitCode() == 0);

        // notes of a passing test show only with verbose
        TestRegistry r2;
        Registrar noted("n", "passes_with_note", +[] { OKAY_NOTE("hello note"); }, "n.cpp", 1, r2);
        std::ostringstream quiet, loud;
        RunOptions q; q.out = &quiet; r2.Run(q);
        RunOptions v; v.out = &loud; v.verbose = true; r2.Run(v);
        CHECK(!Contains(quiet.str(), "hello note"));
        CHECK(Contains(loud.str(), "hello note"));
    }

    // --- Skipped alone is a pass ---
    {
        TestRegistry reg;
        Registrar s("x", "skipped", &SkipBody, "x.cpp", 1, reg);
        std::ostringstream log;
        RunOptions opt; opt.out = &log;
        RunSummary r = reg.Run(opt);
        CHECK(r.Ok() && r.ExitCode() == 0 && r.Skipped() == 1 && r.Passed() == 0);
    }

    // --- Assertion macros ---
    {
        TestRegistry reg;
        Registrar a("m", "near_fail", &NearFailBody, "m.cpp", 1, reg);
        Registrar b("m", "nan", &NanBody, "m.cpp", 2, reg);
        std::ostringstream log;
        RunOptions opt; opt.out = &log;
        RunSummary r = reg.Run(opt);
        CHECK(r.Failed() == 2);                       // NaN never counts as near
        bool sawValues = false;
        for (const auto& res : r.results)
            for (const auto& f : res.failures) if (Contains(f.message, "1 vs 2")) sawValues = true;
        CHECK(sawValues);
    }

    // --- Context outside a run: macros print instead of crashing ---
    {
        CHECK(TestContext::Current() == nullptr);
        TestContext ctx;
        {
            TestContext::Scope scope(ctx);
            CHECK(TestContext::Current() == &ctx);
            OKAY_EXPECT(false);
            OKAY_FAIL("direct " << 7);
        }
        CHECK(TestContext::Current() == nullptr);
        CHECK(ctx.Failures().size() == 2);
        CHECK(ctx.Failures()[1].message == "direct 7");
    }

    // --- JUnit XML ---
    {
        TestRegistry reg = MakeRegistry();
        Registrar x("zz", "xml_escape", &XmlBody, "z.cpp", 9, reg);
        std::ostringstream log;
        RunOptions opt; opt.out = &log;
        RunSummary s = reg.Run(opt);
        const std::string xml = RenderJUnitXml(s);
        CHECK(xml.compare(0, 38, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>") == 0);
        CHECK(Contains(xml, "<testsuites name=\"okay-playtest\" tests=\"6\" failures=\"2\" errors=\"1\" skipped=\"1\""));
        CHECK(Contains(xml, "<testsuite name=\"movement\" tests=\"2\" failures=\"1\""));
        CHECK(Contains(xml, "<testsuite name=\"ui\" tests=\"3\" failures=\"0\" errors=\"1\" skipped=\"1\""));
        CHECK(Contains(xml, "<testcase classname=\"movement\" name=\"walks\""));
        CHECK(Contains(xml, "<failure message=\"1 == 2\" type=\"AssertionFailure\">"));
        CHECK(Contains(xml, "<error message=\"kaboom &lt;&amp;&gt;\" type=\"Exception\">"));
        CHECK(Contains(xml, "<skipped message=\"not on this machine\"/>"));
        CHECK(Contains(xml, "<system-out>context line"));
        // special characters are escaped, control characters cannot appear
        CHECK(Contains(xml, "quote &quot; amp &amp; lt &lt; gt &gt; tab\t ctl? end"));
        CHECK(!Contains(xml, "\x01"));
        // a custom root name
        CHECK(Contains(RenderJUnitXml(s, "my <suite>"), "<testsuites name=\"my &lt;suite&gt;\""));
        // tags balance
        auto count = [&](const std::string& t) { std::size_t n = 0, p = 0; while ((p = xml.find(t, p)) != std::string::npos) { ++n; p += t.size(); } return n; };
        CHECK(count("<testsuite ") == count("</testsuite>"));
        CHECK(count("<testcase ") == 6);
        CHECK(Contains(xml, "</testsuites>"));

        // empty run
        RunSummary empty;
        const std::string ex = RenderJUnitXml(empty);
        CHECK(Contains(ex, "tests=\"0\" failures=\"0\" errors=\"0\" skipped=\"0\""));
    }

    // --- Text report ---
    {
        TestRegistry reg = MakeRegistry();
        std::ostringstream log;
        RunOptions opt; opt.out = &log;
        RunSummary s = reg.Run(opt);
        const std::string t = RenderTextReport(s);
        CHECK(Contains(t, "[       OK ] movement.walks"));
        CHECK(Contains(t, "[  FAILED  ] movement.jumps"));
        CHECK(Contains(t, "1 == 2"));
        CHECK(Contains(t, "reason: not on this machine"));
        CHECK(Contains(t, "5 tests ran"));
        CHECK(!Contains(t, "[ RUN      ]"));              // the report has results, not progress
    }

    // --- RunMain: command line, exit codes, report files ---
    {
        TestRegistry reg = MakeRegistry();
        auto run = [&](std::vector<std::string> args, std::string* text = nullptr) {
            std::ostringstream o;
            const int code = RunMain(args, reg, &o);
            if (text) *text = o.str();
            return code;
        };
        std::string text;
        CHECK(run({"--list"}, &text) == 0);
        CHECK(Contains(text, "movement.walks\n") && Contains(text, "ui.menu_throws\n"));
        CHECK(!Contains(text, "[ RUN"));                  // --list runs nothing
        CHECK(run({"--list", "--filter=ui.*"}, &text) == 0);
        CHECK(Contains(text, "ui.menu_opens") && !Contains(text, "movement"));
        CHECK(run({"--filter", "walks"}, &text) == 0);    // passes: only walks ran
        CHECK(Contains(text, "1 test ran"));
        CHECK(run({"menu_opens"}, &text) == 0);           // a bare pattern is a filter too
        CHECK(run({"--filter=walks,menu_opens"}, &text) == 0);
        CHECK(Contains(text, "2 tests ran"));
        CHECK(run({"jumps"}, &text) == 1);                // a failing test: exit 1
        CHECK(run({"--exclude=jumps,throws", "--exclude", "skipped"}, &text) == 0);
        CHECK(Contains(text, "2 tests ran"));
        CHECK(run({"--filter=nothing_like_this"}, &text) == 3);   // nothing matched
        CHECK(Contains(text, "no test matches"));
        CHECK(run({"--filter=nothing_like_this", "--allow-empty"}, &text) == 0);
        CHECK(run({"--bogus"}, &text) == 2);              // usage error
        CHECK(Contains(text, "unknown option --bogus") && Contains(text, "usage:"));
        CHECK(run({"--filter"}, &text) == 2);             // a value is missing
        CHECK(run({"--junit"}, &text) == 2);
        CHECK(run({"--help"}, &text) == 0 && Contains(text, "usage:"));
        CHECK(run({"--fail-fast"}, &text) == 1 && Contains(text, "1 test ran"));
        CHECK(run({"-v", "walks"}, &text) == 0);

        // report files
        const std::string junit = out + "/report.xml", plain = out + "/report.txt";
        std::filesystem::remove(junit); std::filesystem::remove(plain);
        CHECK(run({"--junit=" + junit, "--text", plain, "ui"}, &text) == 1);
        const std::string xml = Slurp(junit), txt = Slurp(plain);
        CHECK(Contains(xml, "<testsuite name=\"ui\" tests=\"3\""));
        CHECK(Contains(txt, "[  ERROR   ] ui.menu_throws"));
        // a report that cannot be written is a usage-level failure
        CHECK(run({"--junit=" + out + "/no_such_dir/x.xml", "walks"}, &text) == 2);
        CHECK(Contains(text, "cannot write"));

        // argc/argv entry
        char a0[] = "prog", a1[] = "--list";
        char* argv[] = {a0, a1, nullptr};
        std::ostringstream o;
        CHECK(RunMain(2, argv, reg, &o) == 0 && Contains(o.str(), "movement.walks"));
    }

    // --- The global registry takes OKAY_PLAYTEST registrations (this TU adds one) ---
    {
        CHECK(TestRegistry::Global().Select({"registry_selfcheck"}).size() == 1);
    }

    TEST_MAIN_RESULT();
}

// A test registered through the macro, into the global registry.
OKAY_PLAYTEST_IN(registry_selfcheck, macro_registered) { OKAY_EXPECT(true); }
