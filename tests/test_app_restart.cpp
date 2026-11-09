// Updater restart: the old launcher/editor started the new process while the
// old one still held the single-instance lock, and the Windows editor helper
// was a cmd.exe batch whose quotes cmd strips for some paths. These tests use
// a disposable temp install. They never touch a real OkaySpace installation.
#include "test_framework.hpp"
#include "AppRestart.hpp"

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;
using okay::restart::Request;

static std::string ReadFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

static void WriteFile(const fs::path& p, const std::string& body) {
    std::error_code ec;
    if (!p.parent_path().empty()) fs::create_directories(p.parent_path(), ec);
    std::ofstream o(p, std::ios::binary | std::ios::trunc);
    o << body;
}

static std::string Field(const std::string& text, const std::string& key) {
    const std::string prefix = key + "=";
    std::size_t start = 0;
    while (start < text.size()) {
        const auto end = text.find('\n', start);
        const std::string line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (line.compare(0, prefix.size(), prefix) == 0) return line.substr(prefix.size());
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return {};
}

static int CountChar(const std::string& s, char c) {
    int n = 0;
    for (char ch : s) if (ch == c) ++n;
    return n;
}

static fs::path Canon(const fs::path& p) {
    std::error_code ec;
    fs::path c = fs::weakly_canonical(p, ec);
    return ec ? p : c;
}

static std::wstring QuoteJoin(const std::vector<std::wstring>& args) {
    std::wstring cmd;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i) cmd.push_back(L' ');
        cmd += okay::restart::QuoteWindowsArg(args[i]);
    }
    return cmd;
}

// CommandLineToArgvW rules, so the quoting test doesn't need Windows.
static std::vector<std::wstring> ParseWindowsCommandLine(const std::wstring& cmd) {
    std::vector<std::wstring> args;
    std::size_t i = 0;
    while (i < cmd.size()) {
        while (i < cmd.size() && (cmd[i] == L' ' || cmd[i] == L'\t')) ++i;
        if (i >= cmd.size()) break;
        std::wstring arg;
        bool inQuotes = false;
        bool any = false;
        while (i < cmd.size()) {
            std::size_t bs = 0;
            while (i < cmd.size() && cmd[i] == L'\\') { ++bs; ++i; }
            if (i < cmd.size() && cmd[i] == L'"') {
                arg.append(bs / 2, L'\\');
                if (bs % 2 == 1) arg.push_back(L'"');
                else inQuotes = !inQuotes;
                ++i;
                any = true;
                continue;
            }
            if (bs) arg.append(bs, L'\\');
            if (i >= cmd.size()) break;
            if (!inQuotes && (cmd[i] == L' ' || cmd[i] == L'\t')) break;
            arg.push_back(cmd[i]);
            ++i;
            any = true;
        }
        if (any || inQuotes) args.push_back(arg);
    }
    return args;
}

static std::string FirstToken(const std::string& command) {
    std::string s = command;
    while (!s.empty() && (s.back() == '\r' || s.back() == ' ')) s.pop_back();
    const auto sp = s.find_first_of(" \t");
    return sp == std::string::npos ? s : s.substr(0, sp);
}

// %NAME% expansion the way a .bat file does it, for names that are set.
static std::string ExpandCmdPercent(std::string s) {
    std::string out;
    for (std::size_t i = 0; i < s.size();) {
        if (s[i] == '%') {
            const auto b = s.find('%', i + 1);
            if (b != std::string::npos) {
                const std::string name = s.substr(i + 1, b - i - 1);
                if (const char* v = std::getenv(name.c_str())) {
                    out += v;
                    i = b + 1;
                    continue;
                }
            }
        }
        out.push_back(s[i++]);
    }
    return out;
}

static int ChildMain(int argc, char** argv) {
    if (argc < 5) return 2;
    const fs::path result = argv[2];
    const fs::path up = argv[3];
    const fs::path lock = argv[4];
    bool got = false;
    const int fd = ::open(lock.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd >= 0) {
        got = ::flock(fd, LOCK_EX | LOCK_NB) == 0;
        if (got) ::flock(fd, LOCK_UN);
        ::close(fd);
    }
    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);
    std::ofstream o(result, std::ios::binary | std::ios::trunc);
    o << "lock=" << (got ? "free" : "busy") << "\n";
    o << "stays=" << (got ? "1" : "0") << "\n";
    o << "cwd=" << cwd.string() << "\n";
    o << "exe=" << okay::restart::Utf8(okay::restart::CurrentExecutable()) << "\n";
    o << "argc=" << argc << "\n";
    for (int i = 0; i < argc; ++i)
        o << "argv" << i << "=" << (argv[i] ? argv[i] : "") << "\n";
    o.flush();
    if (got) {
        std::ofstream mark(up, std::ios::binary | std::ios::app);
        mark << "up\n";
    }
    return 0;
}

static int HolderMain(int argc, char** argv) {
    // --restart-holder target source result up cwd log lock [extra args...]
    if (argc < 9) return 2;
    const fs::path target = argv[2];
    const fs::path source = argv[3];
    const fs::path result = argv[4];
    const fs::path up = argv[5];
    const fs::path cwd = argv[6];
    const fs::path log = argv[7];
    const fs::path lock = argv[8];
    const int fd = ::open(lock.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd < 0 || ::flock(fd, LOCK_EX) != 0) {
        std::perror("holder lock");
        return 2;
    }
    Request req;
    req.executable = target;
    req.arguments = {"--restart-child", result.string(), up.string(), lock.string()};
    for (int i = 9; i < argc; ++i) req.arguments.push_back(argv[i]);
    req.workingDirectory = cwd;
    req.waitForPid = okay::restart::CurrentPid();
    req.logFile = log;
    req.replaceTarget = target;
    req.replaceSource = source;
    req.replaceBackup = target;
    req.replaceBackup += ".old";
    req.minBytes = 0;
    req.keepBackup = true;
    req.timeoutMs = 20000;
    std::string err;
    if (!okay::restart::StartHelper(req, err)) {
        std::fprintf(stderr, "holder: %s\n", err.c_str());
        return 2;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    std::error_code ec;
    if (fs::exists(result, ec)) {
        std::fprintf(stderr, "holder: new process started while the old one still held the lock\n%s\n",
                     ReadFile(log).c_str());
        return 3;
    }
    return 0;
}

static int SpawnAndWait(const std::vector<std::string>& args, int timeoutMs, int& status) {
    const pid_t pid = ::fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        std::vector<char*> av;
        av.reserve(args.size() + 1);
        for (const auto& s : args) av.push_back(const_cast<char*>(s.c_str()));
        av.push_back(nullptr);
        ::execv(av[0], av.data());
        _exit(127);
    }
    const int steps = timeoutMs / 50;
    for (int i = 0; i < steps; ++i) {
        const pid_t r = ::waitpid(pid, &status, WNOHANG);
        if (r == pid) return 0;
        if (r < 0 && errno != EINTR) return -1;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    ::kill(pid, SIGKILL);
    ::waitpid(pid, &status, 0);
    return 1;
}

int main(int argc, char** argv) {
    if (int code = okay::restart::DispatchHelper(argc, argv); code >= 0) return code;
    if (argc > 1 && std::string(argv[1]) == "--restart-child") return ChildMain(argc, argv);
    if (argc > 1 && std::string(argv[1]) == "--restart-holder") return HolderMain(argc, argv);

    ::unsetenv(okay::restart::kFailAfterBackupEnv);
    RUN_SUITE("app restart");

    CHECK(okay::restart::kWindowsModulePathCapacity > 260);

    // --- Windows command line: the new quoter round-trips. The old cmd /c line does not. ---
    {
        const std::vector<std::wstring> samples = {
            L"C:\\Program Files\\Okay\\OkaySpace.exe",
            L"C:\\Program Files (x86)\\Okay\\OkaySpace.exe",
            L"C:\\Okay & Co\\OkaySpace.exe",
            L"C:\\Users\\\u0418\u043c\u0430\u043d\\Okay Space\\OkaySpace.exe",
            L"C:\\path with space\\",
            L"hello world",
            L"",
            L"plain",
        };
        for (const auto& one : samples) {
            const auto parsed = ParseWindowsCommandLine(QuoteJoin({one, L"--launcher", L"42"}));
            CHECK(parsed.size() == 3);
            if (parsed.size() == 3) {
                CHECK(parsed[0] == one);
                CHECK(parsed[1] == L"--launcher");
                CHECK(parsed[2] == L"42");
            }
        }
        const std::string paren = "C:\\Program Files (x86)\\Okay\\okay_update.bat";
        const auto stripped = okay::restart::LegacyCmdSlashC(
            okay::restart::LegacyInnerCmdRemainder(paren), true);
        CHECK(!stripped.preserved);
        CHECK(FirstToken(stripped.command) == "C:\\Program");
        CHECK(okay::restart::LegacyEditorRestartCommand(paren).find("start \"\" /min cmd /c \"") == 0);

        const std::string spaced = "C:\\Program Files\\Okay\\okay_update.bat";
        const auto kept = okay::restart::LegacyCmdSlashC(okay::restart::LegacyInnerCmdRemainder(spaced), true);
        const auto dropped = okay::restart::LegacyCmdSlashC(okay::restart::LegacyInnerCmdRemainder(spaced), false);
        CHECK(kept.preserved);
        CHECK(!dropped.preserved);
        CHECK(FirstToken(dropped.command) == "C:\\Program");

        const std::string amp = "C:\\Okay & Co\\okay_update.bat";
        CHECK(!okay::restart::LegacyCmdSlashC(okay::restart::LegacyInnerCmdRemainder(amp), true).preserved);

        ::setenv("OKAY_RESTART_PCT", "EXPANDED", 1);
        const std::string bat = "move /y \"C:\\Users\\%OKAY_RESTART_PCT%\\app.exe\" \"C:\\Users\\%OKAY_RESTART_PCT%\\app.exe\"";
        const std::string expanded = ExpandCmdPercent(bat);
        CHECK(expanded.find("%OKAY_RESTART_PCT%") == std::string::npos);
        CHECK(expanded.find("EXPANDED") != std::string::npos);
        ::unsetenv("OKAY_RESTART_PCT");
    }

    // --- Request file keeps args, cwd, and non-ASCII paths. ---
    {
        Request req;
        req.executable = fs::path("Okay Space") / "\u0418\u043c\u0430\u043d" / "OkayApp";
        req.workingDirectory = fs::path("work dir") / "\u0418\u043c\u0430\u043d";
        req.arguments = {"--launcher", "99", "hello world", "line\nbreak", "\u0418\u043c\u0430\u043d"};
        req.waitForPid = 1234;
        req.timeoutMs = 4321;
        req.minBytes = 100000;
        req.keepBackup = true;
        req.logFile = fs::path("Okay Space") / "okay_update.log";
        fs::path written;
        std::string err;
        CHECK(okay::restart::WriteRequest(req, written, err));
        Request back;
        CHECK(okay::restart::LoadRequest(written, back, err));
        CHECK(back.executable == req.executable);
        CHECK(back.workingDirectory == req.workingDirectory);
        CHECK(back.arguments == req.arguments);
        CHECK(back.waitForPid == 1234);
        CHECK(back.timeoutMs == 4321);
        CHECK(back.minBytes == 100000);
        CHECK(back.keepBackup);
        std::error_code ec;
        fs::remove(written, ec);
    }

    // --- Direct swap: failure restores, a too-small file is refused. ---
    {
        std::error_code ec;
        const fs::path dir = fs::temp_directory_path(ec) / ("okay-restart-swap-" + std::to_string(::getpid()));
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        const fs::path dest = dir / "app";
        const fs::path staged = dir / "app.new";
        const fs::path backup = dir / "app.old";
        const fs::path decoy = dir / "Projects" / "My Game" / "Assets" / "scene.okayscene";
        WriteFile(dest, "OLD_VERSION\n");
        WriteFile(decoy, "scene-v1");
        WriteFile(staged, "tiny");
        auto sw = okay::restart::SwapFile(dest, staged, backup, 100000, true);
        CHECK(!sw.ok);
        CHECK(sw.error.find("too small") != std::string::npos);
        CHECK(ReadFile(dest) == "OLD_VERSION\n");
        CHECK(ReadFile(decoy) == "scene-v1");

        WriteFile(staged, "NEW_VERSION\n");
        ::setenv(okay::restart::kFailAfterBackupEnv, "1", 1);
        sw = okay::restart::SwapFile(dest, staged, backup, 0, true);
        ::unsetenv(okay::restart::kFailAfterBackupEnv);
        CHECK(!sw.ok);
        CHECK(sw.error.find("restored") != std::string::npos);
        CHECK(ReadFile(dest) == "OLD_VERSION\n");
        CHECK(ReadFile(staged) == "NEW_VERSION\n");
        CHECK(!fs::exists(backup, ec));
        CHECK(ReadFile(decoy) == "scene-v1");

        sw = okay::restart::SwapFile(dest, staged, backup, 0, true);
        CHECK(sw.ok);
        CHECK(ReadFile(dest) == "NEW_VERSION\n");
        CHECK(ReadFile(backup) == "OLD_VERSION\n");
        CHECK(ReadFile(decoy) == "scene-v1");
        fs::remove_all(dir, ec);
    }

    // --- Launch failures and the wait timeout leave the old process and files recoverable. ---
    {
        std::error_code ec;
        const fs::path dir = fs::temp_directory_path(ec) / ("okay-restart-fail-" + std::to_string(::getpid()));
        fs::remove_all(dir, ec);
        fs::create_directories(dir / "decoy", ec);
        const fs::path dest = dir / "app";
        const fs::path staged = dir / "app.new";
        const fs::path backup = dir / "app.old";
        const fs::path log = dir / "okay_update.log";
        const fs::path result = dir / "result.txt";
        WriteFile(dest, "OLD_VERSION\n");
        WriteFile(dir / "decoy" / "settings.cfg", "volume=1\n");

        Request missing = {};
        missing.executable = dest;
        missing.replaceTarget = dest;
        missing.replaceSource = dir / "no-such-staged";
        missing.replaceBackup = backup;
        missing.logFile = log;
        missing.minBytes = 0;
        CHECK(okay::restart::RunHelper(missing) != 0);
        CHECK(ReadFile(dest) == "OLD_VERSION\n");
        CHECK(ReadFile(log).find("staged update is missing") != std::string::npos);
        CHECK(!fs::exists(result, ec));

        WriteFile(staged, "not a program\n");
        fs::remove(log, ec);
        Request bad = {};
        bad.executable = dest;
        bad.replaceTarget = dest;
        bad.replaceSource = staged;
        bad.replaceBackup = backup;
        bad.logFile = log;
        bad.keepBackup = true;
        bad.workingDirectory = dir;
        CHECK(okay::restart::RunHelper(bad) != 0);
        CHECK(ReadFile(dest) == "not a program\n");
        CHECK(ReadFile(backup) == "OLD_VERSION\n");
        const std::string badLog = ReadFile(log);
        CHECK(badLog.find("launch failed") != std::string::npos);
        CHECK(badLog.find("previous version kept") != std::string::npos);
        CHECK(ReadFile(dir / "decoy" / "settings.cfg") == "volume=1\n");

        fs::remove(log, ec);
        Request cwd = {};
        cwd.executable = okay::restart::CurrentExecutable();
        cwd.workingDirectory = dir / "missing-cwd";
        cwd.arguments = {"--restart-child", result.string(), (dir / "up.txt").string(), (dir / "lock").string()};
        cwd.logFile = log;
        CHECK(okay::restart::RunHelper(cwd) != 0);
        CHECK(ReadFile(log).find("launch failed") != std::string::npos);
        CHECK(!fs::exists(result, ec));

        const pid_t sleeper = ::fork();
        if (sleeper == 0) { ::sleep(30); _exit(0); }
        CHECK(sleeper > 0);
        if (sleeper > 0) {
            fs::remove(log, ec);
            Request wait = {};
            wait.executable = okay::restart::CurrentExecutable();
            wait.waitForPid = static_cast<unsigned long>(sleeper);
            wait.timeoutMs = 350;
            wait.logFile = log;
            wait.arguments = {"--restart-child", result.string(), (dir / "up.txt").string(), (dir / "lock").string()};
            CHECK(okay::restart::RunHelper(wait) != 0);
            const std::string waitLog = ReadFile(log);
            CHECK(waitLog.find("timed out") != std::string::npos);
            CHECK(waitLog.find("no files were changed") != std::string::npos);
            CHECK(okay::restart::PidAlive(static_cast<unsigned long>(sleeper)));
            CHECK(!fs::exists(result, ec));
            ::kill(sleeper, SIGKILL);
            int st = 0;
            ::waitpid(sleeper, &st, 0);
        }
        fs::remove_all(dir, ec);
    }

    // --- Old order: start the new process while this one holds the lock. It exits. ---
    {
        std::error_code ec;
        const fs::path dir = fs::temp_directory_path(ec) / ("okay-restart-race-" + std::to_string(::getpid()));
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        const fs::path lock = dir / "single.lock";
        const fs::path result = dir / "result.txt";
        const fs::path up = dir / "up.txt";
        const int fd = ::open(lock.c_str(), O_RDWR | O_CREAT, 0644);
        CHECK(fd >= 0);
        CHECK(::flock(fd, LOCK_EX) == 0);
        const fs::path self = okay::restart::CurrentExecutable();
        int status = 0;
        const int wr = SpawnAndWait(
            {self.string(), "--restart-child", result.string(), up.string(), lock.string(), "hello world", "\u0418\u043c\u0430\u043d"},
            5000, status);
        CHECK(wr == 0);
        CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
        const std::string body = ReadFile(result);
        CHECK(Field(body, "lock") == "busy");
        CHECK(Field(body, "stays") == "0");
        CHECK(!fs::exists(up, ec));
        ::flock(fd, LOCK_UN);
        ::close(fd);
        fs::remove_all(dir, ec);
    }

    // --- Helper waits, replaces, and the new process stays up exactly once. ---
    {
        std::error_code ec;
        const fs::path root = fs::temp_directory_path(ec) / ("okay-restart-e2e-" + std::to_string(::getpid()));
        fs::remove_all(root, ec);
        const fs::path install = root / "Okay Space" / "\u0418\u043c\u0430\u043d";
        const fs::path work = root / "work dir";
        fs::create_directories(install / "Projects" / "My Game" / "Assets", ec);
        fs::create_directories(work, ec);
        const fs::path target = install / "OkayApp";
        const fs::path source = install / "OkayApp.new";
        const fs::path decoy = install / "Projects" / "My Game" / "Assets" / "scene.okayscene";
        const fs::path settings = install / "launcher.cfg";
        const fs::path result = root / "result.txt";
        const fs::path up = root / "up.txt";
        const fs::path log = install / "okay_update.log";
        const fs::path lock = root / "single.lock";
        WriteFile(target, "OLD_VERSION\n");
        WriteFile(decoy, "scene-v1");
        WriteFile(settings, "volume=1\n");
        const fs::path self = okay::restart::CurrentExecutable();
        CHECK(fs::copy_file(self, source, fs::copy_options::overwrite_existing, ec));
        fs::permissions(source,
                        fs::perms::owner_read | fs::perms::owner_write |
                        fs::perms::group_read | fs::perms::others_read,
                        ec);
        const auto sourceBytes = fs::file_size(source, ec);
        int status = 0;
        const int wr = SpawnAndWait({
            self.string(), "--restart-holder",
            target.string(), source.string(), result.string(), up.string(),
            work.string(), log.string(), lock.string(),
            "hello world", "\u0418\u043c\u0430\u043d"
        }, 15000, status);
        if (wr != 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
            std::cerr << ReadFile(log) << "\n";
        CHECK(wr == 0);
        CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
        bool saw = false;
        for (int i = 0; i < 50 && !saw; ++i) {
            saw = fs::exists(result, ec);
            if (!saw) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (!saw) std::cerr << ReadFile(log) << "\n";
        CHECK(saw);
        const std::string body = ReadFile(result);
        CHECK(Field(body, "lock") == "free");
        CHECK(Field(body, "stays") == "1");
        CHECK(Canon(Field(body, "cwd")) == Canon(work));
        CHECK(Canon(Field(body, "exe")) == Canon(target));
        CHECK(Field(body, "argc") == "7");
        CHECK(Field(body, "argv5") == "hello world");
        CHECK(Field(body, "argv6") == "\u0418\u043c\u0430\u043d");
        CHECK(CountChar(ReadFile(up), '\n') == 1);
        CHECK(fs::file_size(target, ec) == sourceBytes);
        CHECK(!fs::exists(source, ec));
        const std::string installed = ReadFile(target);
        CHECK(installed.size() >= 4 && installed[0] == '\x7f' && installed[1] == 'E');
        CHECK(ReadFile(target.string() + ".old") == "OLD_VERSION\n");
        CHECK(ReadFile(decoy) == "scene-v1");
        CHECK(ReadFile(settings) == "volume=1\n");
        const auto perms = fs::status(target, ec).permissions();
        CHECK((perms & fs::perms::owner_exec) != fs::perms::none);
        const std::string trace = ReadFile(log);
        const auto w = trace.find("waiting for process");
        const auto e = trace.find("has exited");
        const auto r = trace.find("replaced ");
        const auto l = trace.find("launched ");
        CHECK(w != std::string::npos && e != std::string::npos && r != std::string::npos && l != std::string::npos);
        CHECK(w < e && e < r && r < l);
        CHECK(CountChar(trace.substr(trace.find("launched ")), '\n') >= 1);
        int launches = 0;
        for (std::size_t p = 0; (p = trace.find("launched ", p)) != std::string::npos; p += 9) ++launches;
        CHECK(launches == 1);
        fs::remove_all(root, ec);
    }

    CHECK(okay::restart::RunAndWait({"/bin/true"}) == 0);
    CHECK(okay::restart::RunAndWait({"/no/such/okay-restart-bin"}) == -1);

    std::cout << okaytest::g_checks << " checks, " << okaytest::g_failures << " failures\n";
    return okaytest::g_failures == 0 ? 0 : 1;
}
