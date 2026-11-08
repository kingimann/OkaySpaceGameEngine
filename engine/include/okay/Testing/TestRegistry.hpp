#pragma once
// ---------------------------------------------------------------------------
// A tiny test registry and runner for play-mode tests (see PlayTest.hpp).
//
//   #include "okay/Testing/PlayTest.hpp"
//   using namespace okay::testing;
//
//   OKAY_PLAYTEST(coin_spins_up) {
//       PlayTest t;
//       t.LoadSceneFile("scenes/level1.okayscene");
//       t.RunFrames(60);
//       OKAY_REQUIRE(t.FindObject("Coin") != nullptr);
//       OKAY_EXPECT_NEAR(t.FindObject("Coin")->transform->Position().y, 1.0f, 0.01f);
//   }
//
//   int main(int argc, char** argv) { return okay::testing::RunMain(argc, argv); }
//
// Failures are recorded on the running test (OKAY_EXPECT keeps going, OKAY_REQUIRE
// stops the test). RunMain parses the command line (--filter, --list, --junit,
// --text ...), runs the matching tests, prints a summary and returns the exit code.
// ---------------------------------------------------------------------------
#include <cmath>
#include <cstddef>
#include <functional>
#include <iosfwd>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace okay::testing {

/// One recorded problem (a failed expectation, or an unexpected error log line).
struct Failure {
    std::string message;
    std::string file;
    int line = 0;
};

/// Thrown by OKAY_REQUIRE to stop the running test body (the failure is already
/// recorded). Test code never needs to catch it.
struct AbortTest {};
/// Thrown by OKAY_SKIP to end the running test as skipped.
struct SkipTest { std::string reason; };

/// What one running test has recorded so far. The runner makes one per test and
/// publishes it through TestContext::Current() while the test body runs.
class TestContext {
public:
    void Fail(const std::string& message, const char* file = "", int line = 0);
    /// Record a failure unless `ok`. Returns `ok`.
    bool Check(bool ok, const std::string& what, const char* file = "", int line = 0);
    /// A line of context shown with the failure (or always with --verbose).
    void Note(const std::string& line) { m_notes.push_back(line); }

    bool Failed() const { return !m_failures.empty(); }
    const std::vector<Failure>& Failures() const { return m_failures; }
    const std::vector<std::string>& Notes() const { return m_notes; }

    /// The context of the test being run on this thread's runner, or nullptr outside a run.
    static TestContext* Current();

    /// Makes `ctx` the current context for the lifetime of this object.
    class Scope {
    public:
        explicit Scope(TestContext& ctx);
        ~Scope();
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
    private:
        TestContext* m_prev;
    };

private:
    std::vector<Failure> m_failures;
    std::vector<std::string> m_notes;
};

namespace detail {
/// Records the outcome on the current test (or prints it, outside a run).
bool Report(bool ok, const std::string& what, const char* file, int line);
template <class T>
auto Show(const T& v, int) -> decltype((void)(std::declval<std::ostringstream&>() << v), std::string()) {
    std::ostringstream o; o << v; return o.str();
}
template <class T>
std::string Show(const T&, long) { return "<value>"; }
inline std::string Show(bool v, int) { return v ? "true" : "false"; }
} // namespace detail

enum class TestStatus { Passed, Failed, Errored, Skipped };

/// A registered test.
struct TestCase {
    std::string suite;
    std::string name;
    std::function<void()> body;
    std::string file;
    int line = 0;
    std::string FullName() const { return suite + "." + name; }
};

/// What running one test produced.
struct TestResult {
    std::string suite, name, file;
    int line = 0;
    TestStatus status = TestStatus::Passed;
    double seconds = 0.0;
    std::vector<Failure> failures;
    std::vector<std::string> notes;
    std::string skipReason;
    std::string error;     // the exception text, for Errored
    std::string FullName() const { return suite + "." + name; }
};

struct RunSummary {
    std::vector<TestResult> results;
    double seconds = 0.0;

    int Count(TestStatus s) const {
        int n = 0;
        for (const auto& r : results) if (r.status == s) ++n;
        return n;
    }
    int Passed() const { return Count(TestStatus::Passed); }
    int Failed() const { return Count(TestStatus::Failed); }
    int Errored() const { return Count(TestStatus::Errored); }
    int Skipped() const { return Count(TestStatus::Skipped); }
    /// True when nothing failed or errored (skips are fine).
    bool Ok() const { return Failed() == 0 && Errored() == 0; }
    /// 0 when Ok(), else 1.
    int ExitCode() const { return Ok() ? 0 : 1; }
};

struct RunOptions {
    std::vector<std::string> include;   // glob patterns on "suite.name"; empty = everything
    std::vector<std::string> exclude;
    bool verbose = false;               // show each test's notes even when it passes
    bool failFast = false;              // stop after the first failure
    std::ostream* out = nullptr;        // progress output (default std::cout)
};

class TestRegistry {
public:
    /// The process-wide registry OKAY_PLAYTEST adds to.
    static TestRegistry& Global();

    void Add(TestCase tc);
    const std::vector<TestCase>& Cases() const { return m_cases; }
    void Clear() { m_cases.clear(); }

    /// The tests whose "suite.name" matches any `include` pattern (all when empty)
    /// and none of the `exclude` ones, sorted by suite then name so the order does
    /// not depend on link order.
    std::vector<const TestCase*> Select(const std::vector<std::string>& include,
                                        const std::vector<std::string>& exclude = {}) const;

    /// Run the selected tests one after another and print progress to options.out.
    RunSummary Run(const RunOptions& options = {}) const;

    /// Glob match: `*` any run, `?` any one character. A pattern with neither is a
    /// substring match, so "movement" selects every test with that in its name.
    static bool Matches(const std::string& pattern, const std::string& fullName);

private:
    std::vector<TestCase> m_cases;
};

/// Registers a test at static-initialization time (what OKAY_PLAYTEST expands to).
struct Registrar {
    Registrar(const char* suite, const char* name, void (*fn)(), const char* file, int line,
              TestRegistry& registry = TestRegistry::Global());
};

/// JUnit XML (one <testsuite> per suite) for CI systems.
std::string RenderJUnitXml(const RunSummary& summary, const std::string& rootName = "okay-playtest");
/// The same plain text the console shows, as one string.
std::string RenderTextReport(const RunSummary& summary);

/// The command-line entry: parses `args` (without the program name), runs, prints.
///   --list                  print the selected test names and exit 0
///   --filter=PAT[,PAT...]   only tests matching a pattern (also --filter PAT, or bare PAT)
///   --exclude=PAT[,PAT...]  skip tests matching a pattern
///   --junit=FILE            also write JUnit XML
///   --text=FILE             also write the plain text report
///   --verbose, -v           show notes of passing tests too
///   --fail-fast             stop at the first failing test
///   --allow-empty           a filter that selects nothing is not an error
///   --help, -h              usage
/// Returns 0 all passed (or skipped), 1 a test failed, 2 bad command line or an
/// unwritable report file, 3 nothing matched the filter.
int RunMain(const std::vector<std::string>& args, TestRegistry& registry = TestRegistry::Global(),
            std::ostream* out = nullptr);
/// RunMain for a plain main(argc, argv).
int RunMain(int argc, char** argv, TestRegistry& registry = TestRegistry::Global(),
            std::ostream* out = nullptr);

} // namespace okay::testing

#define OKAY_PT_CAT2(a, b) a##b
#define OKAY_PT_CAT(a, b) OKAY_PT_CAT2(a, b)

/// Define and register a test: OKAY_PLAYTEST(name) { ... } in suite "playtest".
#define OKAY_PLAYTEST(name) OKAY_PLAYTEST_IN(playtest, name)
/// Define and register a test in a named suite (the JUnit class name).
#define OKAY_PLAYTEST_IN(suite, name)                                                         \
    static void OKAY_PT_CAT(okay_pt_##suite##_, name)();                                      \
    static const ::okay::testing::Registrar OKAY_PT_CAT(okay_pt_reg_##suite##_, name)(        \
        #suite, #name, &OKAY_PT_CAT(okay_pt_##suite##_, name), __FILE__, __LINE__);           \
    static void OKAY_PT_CAT(okay_pt_##suite##_, name)()

/// Record a failure (and carry on) unless `cond` holds.
#define OKAY_EXPECT(cond) ::okay::testing::detail::Report(!!(cond), #cond, __FILE__, __LINE__)
/// Record a failure and stop the test unless `cond` holds.
#define OKAY_REQUIRE(cond)                                                                    \
    do { if (!::okay::testing::detail::Report(!!(cond), #cond, __FILE__, __LINE__))           \
             throw ::okay::testing::AbortTest{}; } while (0)
/// |a - b| <= eps.
#define OKAY_EXPECT_NEAR(a, b, eps)                                                           \
    do {                                                                                      \
        const double okay_a_ = (double)(a), okay_b_ = (double)(b), okay_e_ = (double)(eps);   \
        std::ostringstream okay_m_;                                                           \
        okay_m_ << "|" #a " - " #b "| <= " #eps "  (" << okay_a_ << " vs " << okay_b_ << ")"; \
        ::okay::testing::detail::Report(std::fabs(okay_a_ - okay_b_) <= okay_e_ &&            \
                                        okay_a_ == okay_a_ && okay_b_ == okay_b_,             \
                                        okay_m_.str(), __FILE__, __LINE__);                   \
    } while (0)
/// a == b, with both values in the message.
#define OKAY_EXPECT_EQ(a, b)                                                                  \
    do {                                                                                      \
        const auto& okay_a_ = (a); const auto& okay_b_ = (b);                                 \
        ::okay::testing::detail::Report(okay_a_ == okay_b_,                                   \
            std::string(#a " == " #b "  (") + ::okay::testing::detail::Show(okay_a_, 0) +     \
            " vs " + ::okay::testing::detail::Show(okay_b_, 0) + ")", __FILE__, __LINE__);    \
    } while (0)
/// Fail unconditionally with a message.
#define OKAY_FAIL(msg)                                                                        \
    do { std::ostringstream okay_m_; okay_m_ << msg;                                          \
         ::okay::testing::detail::Report(false, okay_m_.str(), __FILE__, __LINE__); } while (0)
/// End the test as skipped.
#define OKAY_SKIP(reason) throw ::okay::testing::SkipTest{reason}
/// Add a context line shown with the test's failures.
#define OKAY_NOTE(msg)                                                                        \
    do { std::ostringstream okay_m_; okay_m_ << msg;                                          \
         if (auto* okay_c_ = ::okay::testing::TestContext::Current()) okay_c_->Note(okay_m_.str()); } while (0)
