// The launcher's Git buttons (launcher/GitArgs.hpp): user text (commit
// message, clone / publish URL, project path) must never be re-read by a shell,
// and a failing step must stop the job and fail the toast.
//   - POSIX quoting: each hostile string, run through /bin/sh -c with a
//     harmless `printf '%s\n'` standing in for git, prints back literally and
//     creates no PWNED file.
//   - Windows quoting: cannot run here, so the produced text is checked
//     against a small model of cmd.exe (percent expansion, then ^ and quote
//     handling) followed by the Windows argv rules: it must come back as one
//     argument equal to the input, with no special character left unescaped.
//   - The argv steps: the commit message is in a 0600 temp file, never on a
//     command line; URLs come after "--"; option-looking and ext:: URLs are
//     refused. With git installed, a real commit in a temp repo records the
//     hostile message byte for byte.
//   - RunSteps stops at a failing middle step.
// Linux only for the shell runs (popen); the rest is std-only.
#include "test_framework.hpp"
#include "../launcher/GitArgs.hpp"
#include "AppRestart.hpp"   // okay::restart::RunAndWait: the no-shell runner the launcher uses

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#if !defined(_WIN32)
#  include <sys/stat.h>
#  include <unistd.h>
#endif

using namespace okay::gitargs;
namespace fs = std::filesystem;

namespace {

const std::vector<std::string>& Hostile() {
    static const std::vector<std::string> v = {
        "`touch PWNED`", "$(touch PWNED)", "$HOME", "a\"b", "trailing\\", "%PATH%", "a&b", "a|b", "^",
        "line1\nline2", "it's", "'; touch PWNED; '", "\"; touch PWNED; \"", "a\\\"b", "x$(touch PWNED)y`touch PWNED`",
        "%HOME%x%", "!PATH!", "(a)<b>", "two  spaces", "", "C:\\Users\\me\\", "-rf",
    };
    return v;
}

fs::path MakeTempDir(const char* tag) {
    fs::path d = fs::temp_directory_path() / (std::string("okay_git_args_") + tag + "_" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d, ec);
    return d;
}

std::string ReadFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

#if !defined(_WIN32)
// Run `cmd` with /bin/sh -c (popen) in `dir`; return what it printed.
std::string ShellRun(const fs::path& dir, const std::string& cmd) {
    const std::string full = "cd " + PosixShellQuote(dir.string()) + " && " + cmd;
    std::FILE* p = ::popen(full.c_str(), "r");
    if (!p) return "<popen failed>";
    std::string out;
    char buf[256];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), p)) > 0) out.append(buf, n);
    ::pclose(p);
    return out;
}
#endif

void PosixQuoteTests() {
#if !defined(_WIN32)
    const fs::path dir = MakeTempDir("sh");
    int literal = 0, total = 0;
    for (const std::string& s : Hostile()) {
        ++total;
        // Exactly the shape the launcher used to build: program, then the quoted text.
        const std::string cmd = "printf '%s\\n' " + PosixShellQuote(s);
        const std::string got = ShellRun(dir, cmd);
        const bool ok = got == s + "\n";
        if (ok) ++literal;
        else std::printf("    not literal: [%s] -> [%s]\n", s.c_str(), got.c_str());
        CHECK(ok);
        // Also as a -C path and a message in one command line, as GitC did.
        const std::string cmd2 = "printf '%s|%s\\n' " + PosixShellQuote("/p/" + s) + " " + PosixShellQuote(s);
        CHECK(ShellRun(dir, cmd2) == "/p/" + s + "|" + s + "\n");
    }
    std::error_code ec;
    const bool pwned = fs::exists(dir / "PWNED", ec);
    std::printf("    POSIX: %d/%d hostile strings printed literally; PWNED file %s\n", literal, total,
                pwned ? "CREATED" : "not created");
    CHECK(!pwned);
    CHECK(PosixShellQuote("it's") == "'it'\\''s'");
    CHECK(PosixShellQuote("") == "''");
    fs::remove_all(dir, ec);
#endif
}

// ---- A model of `cmd /c <line>` followed by the program's argv parser ----------
// From `cmd /?` and the documented parse order: phase 1 expands %NAME% when
// NAME is a defined variable (an undefined one is left as typed on a command
// line); phase 2 walks the line, a " toggles "inside quotes", outside quotes
// ^ escapes the next character (the ^ is dropped) and & | < > ( ) end or
// redirect the command. Returns false if an unescaped special character is
// found outside quotes.
bool CmdModel(const std::string& line, const std::map<std::string, std::string>& env, std::string& out) {
    std::string p1;
    for (std::size_t i = 0; i < line.size();) {
        if (line[i] == '%') {
            const std::size_t j = line.find('%', i + 1);
            if (j != std::string::npos) {
                auto it = env.find(line.substr(i + 1, j - i - 1));
                if (it != env.end()) { p1 += it->second; i = j + 1; continue; }
                p1 += line.substr(i, j - i);   // keep "%NAME" and look again from the second %
                i = j;
                continue;
            }
        }
        p1 += line[i++];
    }
    out.clear();
    bool inQ = false;
    for (std::size_t i = 0; i < p1.size(); ++i) {
        const char c = p1[i];
        if (c == '"') { inQ = !inQ; out += c; continue; }
        if (!inQ && c == '^') { if (i + 1 < p1.size()) out += p1[++i]; continue; }
        if (!inQ && (c == '&' || c == '|' || c == '<' || c == '>' || c == '(' || c == ')')) return false;
        out += c;
    }
    return true;
}

// The Windows argv rules ("Parsing C command-line arguments"): 2n backslashes
// then " -> n backslashes and a quote toggle; 2n+1 backslashes then " -> n
// backslashes and a literal "; backslashes not before a " are literal;
// whitespace outside quotes separates arguments.
std::vector<std::string> ArgvModel(const std::string& line) {
    std::vector<std::string> args;
    std::string cur;
    bool inQ = false, have = false;
    for (std::size_t i = 0; i < line.size();) {
        const char c = line[i];
        if (c == '\\') {
            std::size_t n = 0;
            while (i < line.size() && line[i] == '\\') { ++n; ++i; }
            if (i < line.size() && line[i] == '"') {
                cur.append(n / 2, '\\');
                if (n % 2) { cur += '"'; ++i; }
            } else {
                cur.append(n, '\\');
            }
            have = true;
            continue;
        }
        if (c == '"') { inQ = !inQ; have = true; ++i; continue; }
        if (!inQ && (c == ' ' || c == '\t')) {
            if (have) { args.push_back(cur); cur.clear(); have = false; }
            ++i;
            continue;
        }
        cur += c; have = true; ++i;
    }
    if (have) args.push_back(cur);
    return args;
}

void WindowsQuoteTests() {
    const std::map<std::string, std::string> env = {{"PATH", "C:\\Windows"}, {"HOME", "C:\\Users\\me"},
                                                     {"x", "EXPANDED"}};
    int ok = 0, total = 0;
    for (const std::string& s : Hostile()) {
        std::string q;
        const bool hasNl = s.find('\n') != std::string::npos;
        const bool made = WindowsCmdQuote(s, q);
        if (hasNl) { CHECK(!made); CHECK(q.empty()); continue; }   // cmd cannot carry a newline: refused
        ++total;
        CHECK(made);
        std::string afterCmd;
        const bool safe = CmdModel(q, env, afterCmd);
        const std::vector<std::string> argv = ArgvModel(afterCmd);
        const bool round = safe && argv.size() == 1 && argv[0] == s;
        if (round) ++ok;
        else std::printf("    windows not literal: [%s] -> [%s]\n", s.c_str(), q.c_str());
        CHECK(round);
    }
    std::printf("    Windows (model): %d/%d hostile strings come back as one literal argument\n", ok, total);
    // Exact strings for the cases the finding names.
    std::string q;
    CHECK(WindowsCmdQuote("%PATH%", q) && q == "^\"^%PATH^%^\"");
    CHECK(WindowsCmdQuote("a&b", q) && q == "^\"a^&b^\"");
    CHECK(WindowsCmdQuote("a|b", q) && q == "^\"a^|b^\"");
    CHECK(WindowsCmdQuote("^", q) && q == "^\"^^^\"");
    CHECK(WindowsCmdQuote("a\"b", q) && q == "^\"a\\^\"b^\"");
    CHECK(WindowsCmdQuote("trailing\\", q) && q == "^\"trailing\\\\^\"");
    CHECK(!WindowsCmdQuote("a\rb", q));
    // The old GitQ text, through the same model, is NOT safe: this is the bug.
    std::string afterCmd;
    CHECK(!CmdModel("\"a\"&b\"", env, afterCmd));   // GitQ("a\"&b"): the inner " ends the quotes, & runs a command
    CHECK(CmdModel("\"%x%\"", env, afterCmd) && afterCmd == "\"EXPANDED\"");   // GitQ("%x%"): expanded
}

void StepsTests() {
    const std::string msg = "Rename `Player` $(touch PWNED) $HOME \"quoted\" C:\\";
    const fs::path dir = MakeTempDir("msg");
    const std::string mf = WriteMessageFile(dir, msg);
    CHECK(!mf.empty());
    CHECK(ReadFile(mf) == msg);
#if !defined(_WIN32)
    struct stat st{};
    CHECK(::stat(mf.c_str(), &st) == 0 && (st.st_mode & 0777) == 0600);
#endif
    const std::string mf2 = WriteMessageFile(dir, msg);
    CHECK(!mf2.empty() && mf2 != mf);   // a fresh name each time

    const auto cp = CommitPushSteps("/proj/$(touch PWNED)", mf);
    CHECK(cp.size() == 3);
    for (const auto& a : cp) {
        CHECK(a.size() >= 4 && a[0] == "git" && a[1] == "-C" && a[2] == "/proj/$(touch PWNED)");
        for (const auto& w : a) CHECK(w.find("Rename") == std::string::npos);   // the message is never an argument
    }
    CHECK(cp[1] == (Argv{"git", "-C", "/proj/$(touch PWNED)", "commit", "-F", mf}));

    const std::string evilUrl = "https://github.com/a/b$(touch ~/pwned).git";
    const auto cl = CloneSteps("/parent", evilUrl, CloneFolderName(evilUrl));
    CHECK(cl.size() == 1);
    CHECK(cl[0] == (Argv{"git", "-C", "/parent", "clone", "--", evilUrl, "pwned)"}));   // URL is one argument after "--"
    const auto pub = PublishSteps("/proj", evilUrl, mf);
    CHECK(pub.size() == 6);
    CHECK(pub[4] == (Argv{"git", "-C", "/proj", "remote", "add", "origin", "--", evilUrl}));

    // Option-looking and command-running URLs are refused (no steps at all).
    CHECK(CloneSteps("/p", "--upload-pack=touch PWNED", "x").empty());
    CHECK(CloneSteps("/p", "-uupload", "x").empty());
    CHECK(CloneSteps("/p", "ext::sh -c touch% PWNED", "x").empty());
    CHECK(CloneSteps("/p", "EXT::sh", "x").empty());
    CHECK(CloneSteps("/p", "fd::3", "x").empty());
    CHECK(CloneSteps("/p", "https://a/b\nc", "x").empty());
    CHECK(PublishSteps("/p", "--x", mf).empty());
    CHECK(UrlOk("https://github.com/user/repo.git"));
    CHECK(UrlOk("git@github.com:user/repo.git"));

    CHECK(CloneFolderName("https://github.com/user/repo.git") == "repo");
    CHECK(CloneFolderName("https://github.com/user/-rf") == "_rf");
    CHECK(CloneFolderName("https://github.com/user/..") == "ClonedProject");
    CHECK(CloneFolderName("") == "ClonedProject");

    CHECK(StepName(cp[1]) == "git commit");
    CHECK(StepName(pub[4]) == "git remote");

    // Stop at a failing middle step; later steps never run.
    std::vector<std::string> ran;
    auto fake = [&](const Argv& a) { ran.push_back(StepName(a)); return a[3] == "commit" ? 1 : 0; };
    const StepsResult r = RunSteps(cp, fake);
    CHECK(!r.ok);
    CHECK(r.failedIndex == 1);
    CHECK(r.rc == 1);
    CHECK(r.ran == 2);
    CHECK(ran == (std::vector<std::string>{"git add", "git commit"}));   // no push after a failed commit
    std::printf("    commit fails -> ok=%d failedIndex=%zu ran=%zu (push not run)\n", (int)r.ok, r.failedIndex, r.ran);
    const StepsResult good = RunSteps(cp, [](const Argv&) { return 0; });
    CHECK(good.ok && good.ran == 3);
    CHECK(!RunSteps({}, [](const Argv&) { return 0; }).ok);   // nothing to do is not success

    std::error_code ec;
    fs::remove_all(dir, ec);
}

#if !defined(_WIN32)
// End to end with real git, if present: init, add, commit -F with the hostile
// message, through RunAndWait (no shell). The log must hold the message as typed.
void RealGitTest() {
    if (okay::restart::RunAndWait({"git", "--version"}) != 0) {
        std::printf("    SKIP real git: git not found\n");
        return;
    }
    const fs::path dir = MakeTempDir("repo");
    const fs::path repo = dir / "proj $(touch PWNED) `x`";
    std::error_code ec;
    fs::create_directories(repo, ec);
    { std::ofstream(repo / "a.txt") << "hi\n"; }
    ::setenv("GIT_CONFIG_NOSYSTEM", "1", 1);
    ::setenv("GIT_CONFIG_GLOBAL", "/dev/null", 1);
    ::setenv("GIT_AUTHOR_NAME", "t", 1); ::setenv("GIT_AUTHOR_EMAIL", "t@t", 1);
    ::setenv("GIT_COMMITTER_NAME", "t", 1); ::setenv("GIT_COMMITTER_EMAIL", "t@t", 1);
    const std::string msg = "Rename `Player` script, Price $5, Note $(id -un), Fix \"jump\", Moved saves to C:\\";
    const std::string mf = WriteMessageFile(dir, msg);
    std::vector<Argv> steps = InitSteps(repo.string());
    const auto cp = CommitPushSteps(repo.string(), mf);
    steps.push_back(cp[0]);
    steps.push_back(cp[1]);   // no push: there is no remote
    const StepsResult r = RunSteps(steps, [](const Argv& a) { return okay::restart::RunAndWait(a); });
    CHECK(r.ok);
    const fs::path out = dir / "log.txt";
    const std::string cmd = "git -C " + PosixShellQuote(repo.string()) + " log -1 --format=%B > " +
                            PosixShellQuote(out.string());
    CHECK(std::system(cmd.c_str()) == 0);
    std::string got = ReadFile(out);
    while (!got.empty() && got.back() == '\n') got.pop_back();
    CHECK(got == msg);
    std::printf("    real git: recorded message %s\n", got == msg ? "matches byte for byte" : ("DIFFERS: " + got).c_str());
    CHECK(!fs::exists(dir / "PWNED", ec) && !fs::exists(repo / "PWNED", ec));
    // A failing step (push with no remote) stops the job and is reported.
    const StepsResult r2 = RunSteps(cp, [](const Argv& a) { return okay::restart::RunAndWait(a); });
    CHECK(!r2.ok && r2.failedIndex == 1);   // nothing new to commit -> commit fails, push not run
    fs::remove_all(dir, ec);
}
#endif

}   // namespace

int main() {
    RUN_SUITE("launcher git args");
    PosixQuoteTests();
    WindowsQuoteTests();
    StepsTests();
#if !defined(_WIN32)
    RealGitTest();
#endif
    TEST_MAIN_RESULT();
}
