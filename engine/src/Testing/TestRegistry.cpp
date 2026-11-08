#include "okay/Testing/TestRegistry.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iostream>
#include <map>

namespace okay::testing {

// ---- TestContext -----------------------------------------------------------

namespace {
TestContext*& CurrentSlot() { static TestContext* cur = nullptr; return cur; }
} // namespace

TestContext* TestContext::Current() { return CurrentSlot(); }

TestContext::Scope::Scope(TestContext& ctx) : m_prev(CurrentSlot()) { CurrentSlot() = &ctx; }
TestContext::Scope::~Scope() { CurrentSlot() = m_prev; }

void TestContext::Fail(const std::string& message, const char* file, int line) {
    m_failures.push_back({message, file ? file : "", line});
}

bool TestContext::Check(bool ok, const std::string& what, const char* file, int line) {
    if (!ok) Fail(what, file, line);
    return ok;
}

namespace detail {
bool Report(bool ok, const std::string& what, const char* file, int line) {
    if (ok) return true;
    if (TestContext* ctx = TestContext::Current()) ctx->Fail(what, file, line);
    else std::cerr << "  FAIL: " << what << "  (" << (file ? file : "") << ':' << line << ")\n";
    return false;
}
} // namespace detail

// ---- Registry --------------------------------------------------------------

TestRegistry& TestRegistry::Global() {
    static TestRegistry reg;
    return reg;
}

void TestRegistry::Add(TestCase tc) { m_cases.push_back(std::move(tc)); }

Registrar::Registrar(const char* suite, const char* name, void (*fn)(), const char* file, int line,
                     TestRegistry& registry) {
    TestCase tc;
    tc.suite = suite;
    tc.name = name;
    tc.body = fn;
    tc.file = file ? file : "";
    tc.line = line;
    registry.Add(std::move(tc));
}

namespace {
bool GlobMatch(const char* p, const char* s) {
    // Iterative wildcard match with one backtrack point (the last '*').
    const char* star = nullptr;
    const char* resume = nullptr;
    while (*s) {
        if (*p == '*') { star = p++; resume = s; }
        else if (*p == '?' || *p == *s) { ++p; ++s; }
        else if (star) { p = star + 1; s = ++resume; }
        else return false;
    }
    while (*p == '*') ++p;
    return *p == '\0';
}
} // namespace

bool TestRegistry::Matches(const std::string& pattern, const std::string& fullName) {
    if (pattern.empty()) return false;
    if (pattern.find_first_of("*?") == std::string::npos)
        return fullName.find(pattern) != std::string::npos;
    return GlobMatch(pattern.c_str(), fullName.c_str());
}

std::vector<const TestCase*> TestRegistry::Select(const std::vector<std::string>& include,
                                                  const std::vector<std::string>& exclude) const {
    std::vector<const TestCase*> out;
    for (const TestCase& tc : m_cases) {
        const std::string full = tc.FullName();
        bool in = include.empty();
        for (const auto& p : include) if (Matches(p, full)) { in = true; break; }
        if (!in) continue;
        bool ex = false;
        for (const auto& p : exclude) if (Matches(p, full)) { ex = true; break; }
        if (!ex) out.push_back(&tc);
    }
    std::stable_sort(out.begin(), out.end(), [](const TestCase* a, const TestCase* b) {
        if (a->suite != b->suite) return a->suite < b->suite;
        return a->name < b->name;
    });
    return out;
}

// ---- Formatting ------------------------------------------------------------

namespace {

using Clock = std::chrono::steady_clock;

long long Millis(double seconds) { return (long long)(seconds * 1000.0 + 0.5); }

// The detail lines under a test's result: failures, the exception text, the skip
// reason, and the notes (only when something went wrong, or in verbose mode).
void AppendDetails(std::string& out, const TestResult& r, bool verbose) {
    for (const Failure& f : r.failures) {
        out += "    ";
        if (!f.file.empty()) { out += f.file; out += ':'; out += std::to_string(f.line); out += ": "; }
        out += f.message;
        out += '\n';
    }
    if (r.status == TestStatus::Errored) out += "    exception: " + r.error + "\n";
    if (r.status == TestStatus::Skipped && !r.skipReason.empty()) out += "    reason: " + r.skipReason + "\n";
    if (verbose || r.status == TestStatus::Failed || r.status == TestStatus::Errored)
        for (const auto& n : r.notes) out += "    | " + n + "\n";
}

std::string ResultLine(const TestResult& r) {
    char tag[32];
    switch (r.status) {
        case TestStatus::Passed:  std::snprintf(tag, sizeof(tag), "[       OK ]"); break;
        case TestStatus::Failed:  std::snprintf(tag, sizeof(tag), "[  FAILED  ]"); break;
        case TestStatus::Errored: std::snprintf(tag, sizeof(tag), "[  ERROR   ]"); break;
        case TestStatus::Skipped: std::snprintf(tag, sizeof(tag), "[  SKIPPED ]"); break;
    }
    return std::string(tag) + " " + r.FullName() + " (" + std::to_string(Millis(r.seconds)) + " ms)\n";
}

std::string SummaryText(const RunSummary& s) {
    std::string out;
    out += "[==========] " + std::to_string(s.results.size()) + " test" + (s.results.size() == 1 ? "" : "s") +
           " ran (" + std::to_string(Millis(s.seconds)) + " ms total)\n";
    out += "[  PASSED  ] " + std::to_string(s.Passed()) + "\n";
    if (s.Skipped()) out += "[  SKIPPED ] " + std::to_string(s.Skipped()) + "\n";
    const int bad = s.Failed() + s.Errored();
    if (bad) {
        out += "[  FAILED  ] " + std::to_string(bad) + " test" + (bad == 1 ? "" : "s") + ", listed below:\n";
        for (const auto& r : s.results)
            if (r.status == TestStatus::Failed || r.status == TestStatus::Errored)
                out += "[  FAILED  ] " + r.FullName() + "\n";
    }
    return out;
}

std::string XmlEscape(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (unsigned char c : s) {
        switch (c) {
            case '&': o += "&amp;"; break;
            case '<': o += "&lt;"; break;
            case '>': o += "&gt;"; break;
            case '"': o += "&quot;"; break;
            case '\'': o += "&apos;"; break;
            default:
                if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') o += '?';   // not legal in XML 1.0
                else o += (char)c;
        }
    }
    return o;
}

std::string Seconds3(double s) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.3f", s);
    return buf;
}

std::string FailureText(const TestResult& r) {
    std::string t;
    for (const Failure& f : r.failures) {
        if (!f.file.empty()) t += f.file + ":" + std::to_string(f.line) + ": ";
        t += f.message + "\n";
    }
    return t;
}

} // namespace

std::string RenderTextReport(const RunSummary& summary) {
    std::string out;
    for (const auto& r : summary.results) {
        out += ResultLine(r);
        AppendDetails(out, r, false);
    }
    out += SummaryText(summary);
    return out;
}

std::string RenderJUnitXml(const RunSummary& summary, const std::string& rootName) {
    // One <testsuite> per suite, in the order the suites first appear.
    std::vector<std::string> suites;
    std::map<std::string, std::vector<const TestResult*>> bySuite;
    for (const auto& r : summary.results) {
        if (!bySuite.count(r.suite)) suites.push_back(r.suite);
        bySuite[r.suite].push_back(&r);
    }
    std::string x = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    x += "<testsuites name=\"" + XmlEscape(rootName) + "\" tests=\"" + std::to_string(summary.results.size()) +
         "\" failures=\"" + std::to_string(summary.Failed()) + "\" errors=\"" + std::to_string(summary.Errored()) +
         "\" skipped=\"" + std::to_string(summary.Skipped()) + "\" time=\"" + Seconds3(summary.seconds) + "\">\n";
    for (const std::string& suite : suites) {
        const auto& rs = bySuite[suite];
        int fails = 0, errs = 0, skips = 0;
        double secs = 0.0;
        for (const TestResult* r : rs) {
            secs += r->seconds;
            if (r->status == TestStatus::Failed) ++fails;
            else if (r->status == TestStatus::Errored) ++errs;
            else if (r->status == TestStatus::Skipped) ++skips;
        }
        x += "  <testsuite name=\"" + XmlEscape(suite) + "\" tests=\"" + std::to_string(rs.size()) +
             "\" failures=\"" + std::to_string(fails) + "\" errors=\"" + std::to_string(errs) +
             "\" skipped=\"" + std::to_string(skips) + "\" time=\"" + Seconds3(secs) + "\">\n";
        for (const TestResult* r : rs) {
            x += "    <testcase classname=\"" + XmlEscape(r->suite) + "\" name=\"" + XmlEscape(r->name) +
                 "\" time=\"" + Seconds3(r->seconds) + "\"";
            if (!r->file.empty()) x += " file=\"" + XmlEscape(r->file) + "\" line=\"" + std::to_string(r->line) + "\"";
            const bool hasNotes = !r->notes.empty();
            if (r->status == TestStatus::Passed && !hasNotes) { x += "/>\n"; continue; }
            x += ">\n";
            if (r->status == TestStatus::Failed) {
                const std::string first = r->failures.empty() ? std::string("test failed") : r->failures.front().message;
                x += "      <failure message=\"" + XmlEscape(first) + "\" type=\"AssertionFailure\">" +
                     XmlEscape(FailureText(*r)) + "</failure>\n";
            } else if (r->status == TestStatus::Errored) {
                x += "      <error message=\"" + XmlEscape(r->error) + "\" type=\"Exception\">" +
                     XmlEscape(r->error + "\n" + FailureText(*r)) + "</error>\n";
            } else if (r->status == TestStatus::Skipped) {
                x += "      <skipped message=\"" + XmlEscape(r->skipReason) + "\"/>\n";
            }
            if (hasNotes) {
                std::string n;
                for (const auto& line : r->notes) n += line + "\n";
                x += "      <system-out>" + XmlEscape(n) + "</system-out>\n";
            }
            x += "    </testcase>\n";
        }
        x += "  </testsuite>\n";
    }
    x += "</testsuites>\n";
    return x;
}

// ---- Running ---------------------------------------------------------------

RunSummary TestRegistry::Run(const RunOptions& options) const {
    std::ostream& out = options.out ? *options.out : std::cout;
    RunSummary summary;
    const auto selected = Select(options.include, options.exclude);
    out << "[==========] Running " << selected.size() << " test" << (selected.size() == 1 ? "" : "s") << "\n";
    const auto runStart = Clock::now();
    for (const TestCase* tc : selected) {
        out << "[ RUN      ] " << tc->FullName() << "\n" << std::flush;
        TestResult r;
        r.suite = tc->suite; r.name = tc->name; r.file = tc->file; r.line = tc->line;
        TestContext ctx;
        const auto t0 = Clock::now();
        {
            TestContext::Scope scope(ctx);
            try {
                if (tc->body) tc->body();
            } catch (const AbortTest&) {
                // the failure is already recorded
            } catch (const SkipTest& s) {
                r.status = TestStatus::Skipped;
                r.skipReason = s.reason;
            } catch (const std::exception& e) {
                r.status = TestStatus::Errored;
                r.error = e.what();
            } catch (...) {
                r.status = TestStatus::Errored;
                r.error = "unknown exception";
            }
        }
        r.seconds = std::chrono::duration<double>(Clock::now() - t0).count();
        r.failures = ctx.Failures();
        r.notes = ctx.Notes();
        if (r.status == TestStatus::Passed && ctx.Failed()) r.status = TestStatus::Failed;
        if (r.status == TestStatus::Skipped && ctx.Failed()) r.status = TestStatus::Failed;   // a skip does not hide a failure
        std::string text = ResultLine(r);
        AppendDetails(text, r, options.verbose);
        out << text << std::flush;
        const bool bad = r.status == TestStatus::Failed || r.status == TestStatus::Errored;
        summary.results.push_back(std::move(r));
        if (bad && options.failFast) break;
    }
    summary.seconds = std::chrono::duration<double>(Clock::now() - runStart).count();
    out << SummaryText(summary) << std::flush;
    return summary;
}

// ---- Command line ----------------------------------------------------------

namespace {

void SplitPatterns(const std::string& list, std::vector<std::string>& into) {
    std::size_t pos = 0;
    while (pos <= list.size()) {
        std::size_t end = list.find(',', pos);
        if (end == std::string::npos) end = list.size();
        if (end > pos) into.push_back(list.substr(pos, end - pos));
        pos = end + 1;
    }
}

void PrintUsage(std::ostream& out) {
    out << "usage: okay-playtest [options] [pattern ...]\n"
           "  patterns match \"suite.name\"; `*` and `?` are wildcards, a plain word matches as a substring\n"
           "  --list                  list the selected tests and exit\n"
           "  --filter=PAT[,PAT...]   run only tests matching a pattern (same as bare patterns)\n"
           "  --exclude=PAT[,PAT...]  skip tests matching a pattern\n"
           "  --junit=FILE            also write a JUnit XML report\n"
           "  --text=FILE             also write the plain text report\n"
           "  --verbose, -v           show notes of passing tests too\n"
           "  --fail-fast             stop at the first failing test\n"
           "  --allow-empty           a filter that selects no test is not an error\n"
           "exit code: 0 passed, 1 a test failed, 2 usage error, 3 no test matched\n";
}

bool WriteFile(const std::string& path, const std::string& data) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << data;
    return (bool)f;
}

} // namespace

int RunMain(const std::vector<std::string>& args, TestRegistry& registry, std::ostream* outPtr) {
    std::ostream& out = outPtr ? *outPtr : std::cout;
    RunOptions opt;
    opt.out = &out;
    bool list = false, allowEmpty = false;
    std::string junitPath, textPath;

    // `--name=value` or `--name value`.
    auto valueOf = [&](std::size_t& i, const std::string& arg, const char* name, std::string& val) -> int {
        const std::string eq = std::string(name) + "=";
        if (arg.compare(0, eq.size(), eq) == 0) { val = arg.substr(eq.size()); return 1; }
        if (arg == name) {
            if (i + 1 >= args.size()) return -1;
            val = args[++i];
            return 1;
        }
        return 0;
    };

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        std::string v;
        int got;
        if (a == "--help" || a == "-h") { PrintUsage(out); return 0; }
        else if (a == "--list") list = true;
        else if (a == "--verbose" || a == "-v") opt.verbose = true;
        else if (a == "--fail-fast") opt.failFast = true;
        else if (a == "--allow-empty") allowEmpty = true;
        else if ((got = valueOf(i, a, "--filter", v)) != 0) { if (got < 0) { out << "--filter needs a value\n"; return 2; } SplitPatterns(v, opt.include); }
        else if ((got = valueOf(i, a, "--exclude", v)) != 0) { if (got < 0) { out << "--exclude needs a value\n"; return 2; } SplitPatterns(v, opt.exclude); }
        else if ((got = valueOf(i, a, "--junit", v)) != 0) { if (got < 0) { out << "--junit needs a file\n"; return 2; } junitPath = v; }
        else if ((got = valueOf(i, a, "--text", v)) != 0) { if (got < 0) { out << "--text needs a file\n"; return 2; } textPath = v; }
        else if (!a.empty() && a[0] == '-') { out << "unknown option " << a << "\n"; PrintUsage(out); return 2; }
        else SplitPatterns(a, opt.include);
    }

    const auto selected = registry.Select(opt.include, opt.exclude);
    if (list) {
        for (const TestCase* tc : selected) out << tc->FullName() << "\n";
        return 0;
    }
    if (selected.empty() && !allowEmpty) {
        out << "no test matches the filter (" << registry.Cases().size() << " registered)\n";
        return 3;
    }

    const RunSummary summary = registry.Run(opt);
    int code = summary.ExitCode();
    if (!junitPath.empty() && !WriteFile(junitPath, RenderJUnitXml(summary))) {
        out << "cannot write " << junitPath << "\n";
        code = 2;
    }
    if (!textPath.empty() && !WriteFile(textPath, RenderTextReport(summary))) {
        out << "cannot write " << textPath << "\n";
        code = 2;
    }
    return code;
}

int RunMain(int argc, char** argv, TestRegistry& registry, std::ostream* out) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.push_back(argv[i]);
    return RunMain(args, registry, out);
}

} // namespace okay::testing
