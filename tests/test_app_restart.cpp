// Updater restart: the old launcher/editor started the new process while the
// old one still held the single-instance lock, and the Windows editor helper
// was a cmd.exe batch whose quotes cmd strips for some paths. These tests use
// a disposable temp install. They never touch a real OkaySpace installation.
#include "test_framework.hpp"
#include "AppRestart.hpp"

#include <cerrno>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <shellapi.h>
#else
#  include <fcntl.h>
#  include <sys/file.h>
#  include <sys/wait.h>
#  include <unistd.h>
#endif

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

static void SetEnv(const char* key, const char* value) {
#if defined(_WIN32)
    _putenv_s(key, value ? value : "");   // an empty value removes the variable on Windows (a "0" here made every scenario report version 0)
#else
    if (value) ::setenv(key, value, 1);
    else ::unsetenv(key);
#endif
}

#if defined(_WIN32)
static std::string MutexName(const fs::path& lock) {
    std::string id = lock.parent_path().filename().string();
    for (char& c : id)
        if (c == '\\' || c == '/' || c == ' ') c = '_';
    return "Local\\OkayRst_" + id;
}
#endif

// Exclusive lock held until release (or process exit). On Windows this is both
// a byte-range file lock and a named mutex, the same kind of lock the launcher
// and editor use to refuse a second instance.
struct HeldLock {
#if defined(_WIN32)
    HANDLE file = INVALID_HANDLE_VALUE;
    HANDLE mutex = nullptr;
#else
    int fd = -1;
#endif
    bool Acquire(const fs::path& lock) {
#if defined(_WIN32)
        file = CreateFileW(lock.wstring().c_str(), GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        OVERLAPPED ov{};
        if (!LockFileEx(file, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &ov)) return false;
        mutex = CreateMutexA(nullptr, FALSE, MutexName(lock).c_str());
        return mutex != nullptr;
#else
        fd = ::open(lock.c_str(), O_RDWR | O_CREAT, 0644);
        return fd >= 0 && ::flock(fd, LOCK_EX) == 0;
#endif
    }
    void Release() {
#if defined(_WIN32)
        if (file != INVALID_HANDLE_VALUE) {
            OVERLAPPED ov{};
            UnlockFileEx(file, 0, 1, 0, &ov);
            CloseHandle(file);
            file = INVALID_HANDLE_VALUE;
        }
        if (mutex) { CloseHandle(mutex); mutex = nullptr; }
#else
        if (fd >= 0) { ::flock(fd, LOCK_UN); ::close(fd); fd = -1; }
#endif
    }
};

static void ProbeLock(const fs::path& lock, bool& fileFree, bool& mutexFree) {
    fileFree = false;
    mutexFree = false;
#if defined(_WIN32)
    HANDLE file = CreateFileW(lock.wstring().c_str(), GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        OVERLAPPED ov{};
        fileFree = LockFileEx(file, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &ov) != 0;
        if (fileFree) UnlockFileEx(file, 0, 1, 0, &ov);
        CloseHandle(file);
    }
    HANDLE mutex = CreateMutexA(nullptr, FALSE, MutexName(lock).c_str());
    mutexFree = mutex && GetLastError() != ERROR_ALREADY_EXISTS;
    if (mutex) CloseHandle(mutex);
#else
    const int fd = ::open(lock.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd >= 0) {
        fileFree = ::flock(fd, LOCK_EX | LOCK_NB) == 0;
        if (fileFree) ::flock(fd, LOCK_UN);
        ::close(fd);
    }
    mutexFree = fileFree;
#endif
}

static int ChildMain(int argc, char** argv) {
    if (argc < 5) return 2;
    const fs::path result = argv[2];
    const fs::path up = argv[3];
    const fs::path lock = argv[4];
    bool got = false;
    bool mutexFree = false;
    ProbeLock(lock, got, mutexFree);
    if (const char* sf = std::getenv("OKAY_TEST_STATE_FILE"); sf && *sf) okay::selfupdate::Clear(sf);   // what a new launcher does when it finds itself at the expected version
    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);
    std::ofstream o(result, std::ios::binary | std::ios::trunc);
    o << "lock=" << (got ? "free" : "busy") << "\n";
    o << "mutex=" << (mutexFree ? "free" : "busy") << "\n";
    o << "stays=" << (got ? "1" : "0") << "\n";
    o << "cwd=" << cwd.string() << "\n";
    o << "exe=" << okay::restart::Utf8(okay::restart::CurrentExecutable()) << "\n";
    o << "argc=" << argc << "\n";
    for (int i = 0; i < argc; ++i)
        o << "argv" << i << "=" << (argv[i] ? argv[i] : "") << "\n";
#if defined(_WIN32)
    int wargc = 0;
    if (LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc)) {
        o << "wargc=" << wargc << "\n";
        for (int i = 0; i < wargc; ++i)
            o << "warg" << i << "=" << okay::restart::Utf8(fs::path(wargv[i])) << "\n";
        LocalFree(wargv);
    }
#endif
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
    HeldLock held;
    if (!held.Acquire(lock)) {
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

// Returns 0 when the process exits, 1 on timeout (process killed), -1 if it
// could not be started. `exitCode` is the process exit code when the return is 0.
static int SpawnAndWait(const std::vector<std::string>& args, int timeoutMs, int& exitCode) {
    exitCode = -1;
    if (args.empty()) return -1;
#if defined(_WIN32)
    std::wstring cmd;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i) cmd.push_back(L' ');
        cmd += okay::restart::QuoteWindowsArg(okay::restart::Utf8ToWide(args[i]));
    }
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');
    const std::wstring exe = okay::restart::Utf8ToWide(args[0]);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), buf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
        return -1;
    const DWORD w = WaitForSingleObject(pi.hProcess, (DWORD)timeoutMs);
    if (w == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 5000);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return 1;
    }
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    exitCode = (int)code;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
#else
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
        int status = 0;
        const pid_t r = ::waitpid(pid, &status, WNOHANG);
        if (r == pid) {
            exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            return 0;
        }
        if (r < 0 && errno != EINTR) return -1;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    ::kill(pid, SIGKILL);
    int status = 0;
    ::waitpid(pid, &status, 0);
    return 1;
#endif
}

static unsigned long SpawnSleeper(const fs::path& self) {
#if defined(_WIN32)
    std::wstring cmd = okay::restart::QuoteWindowsArg(self.wstring()) + L" --restart-sleep";
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(self.wstring().c_str(), buf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
        return 0;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return pi.dwProcessId;
#else
    (void)self;
    const pid_t pid = ::fork();
    if (pid < 0) return 0;
    if (pid == 0) { ::sleep(30); _exit(0); }
    return static_cast<unsigned long>(pid);
#endif
}

static void KillPid(unsigned long pid) {
    if (pid == 0) return;
#if defined(_WIN32)
    HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, (DWORD)pid);
    if (!h) return;
    TerminateProcess(h, 1);
    WaitForSingleObject(h, 5000);
    CloseHandle(h);
#else
    ::kill(static_cast<pid_t>(pid), SIGKILL);
    int status = 0;
    ::waitpid(static_cast<pid_t>(pid), &status, 0);
#endif
}

static bool HasArg(const std::string& body, const std::string& value) {
    std::size_t start = 0;
    while (start < body.size()) {
        const auto end = body.find('\n', start);
        const std::string line = body.substr(start, end == std::string::npos ? std::string::npos : end - start);
        const auto eq = line.find('=');
        if (eq != std::string::npos) {
            const std::string key = line.substr(0, eq);
            if ((key.rfind("argv", 0) == 0 || key.rfind("warg", 0) == 0) && line.substr(eq + 1) == value)
                return true;
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return false;
}

int main(int argc, char** argv) {
    if (int code = okay::restart::DispatchHelper(argc, argv); code >= 0) return code;
    {   // This binary plays "the new launcher": `--okay-selfcheck <v>` answers with the version OKAY_TEST_VERSION (default 9.9.9).
        const char* v = std::getenv("OKAY_TEST_VERSION");
        if (int code = okay::selfupdate::SelfCheck(argc, argv, v && *v ? v : "9.9.9"); code >= 0) return code;
    }
    if (argc > 1 && std::string(argv[1]) == "--restart-child") return ChildMain(argc, argv);
    if (argc > 1 && std::string(argv[1]) == "--restart-holder") return HolderMain(argc, argv);
    if (argc > 1 && std::string(argv[1]) == "--restart-sleep") {
#if defined(_WIN32)
        Sleep(30000);
#else
        ::sleep(30);
#endif
        return 0;
    }

    SetEnv(okay::restart::kFailAfterBackupEnv, nullptr);
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

        SetEnv("OKAY_RESTART_PCT", "EXPANDED");
        const std::string bat = "move /y \"C:\\Users\\%OKAY_RESTART_PCT%\\app.exe\" \"C:\\Users\\%OKAY_RESTART_PCT%\\app.exe\"";
        const std::string expanded = ExpandCmdPercent(bat);
        CHECK(expanded.find("%OKAY_RESTART_PCT%") == std::string::npos);
        CHECK(expanded.find("EXPANDED") != std::string::npos);
        SetEnv("OKAY_RESTART_PCT", nullptr);
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
        req.expectVersion = "2.856.0";
        req.rollbackTarget = fs::path("Okay Space") / "OkaySpace.exe";
        req.replaceBackup = fs::path("Okay Space") / "OkaySpace.exe.old";
        req.stateFile = fs::path("Okay Space") / "okay_selfupdate.state";
        req.confirmMs = 777;
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
        CHECK(back.expectVersion == "2.856.0" && back.rollbackTarget == req.rollbackTarget && back.replaceBackup == req.replaceBackup &&
              back.stateFile == req.stateFile && back.confirmMs == 777);
        std::error_code ec;
        fs::remove(written, ec);
    }

    // --- Direct swap: failure restores, a too-small file is refused. ---
    {
        std::error_code ec;
        const fs::path dir = fs::temp_directory_path(ec) / ("okay-restart-swap-" + std::to_string(okay::restart::CurrentPid()));
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
        SetEnv(okay::restart::kFailAfterBackupEnv, "1");
        sw = okay::restart::SwapFile(dest, staged, backup, 0, true);
        SetEnv(okay::restart::kFailAfterBackupEnv, nullptr);
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
        const fs::path dir = fs::temp_directory_path(ec) / ("okay-restart-fail-" + std::to_string(okay::restart::CurrentPid()));
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

        const fs::path self = okay::restart::CurrentExecutable();
        const unsigned long sleeper = SpawnSleeper(self);
        CHECK(sleeper != 0);
        if (sleeper != 0) {
            fs::remove(log, ec);
            Request wait = {};
            wait.executable = self;
            wait.waitForPid = sleeper;
            wait.timeoutMs = 350;
            wait.logFile = log;
            wait.arguments = {"--restart-child", result.string(), (dir / "up.txt").string(), (dir / "lock").string()};
            CHECK(okay::restart::RunHelper(wait) != 0);
            const std::string waitLog = ReadFile(log);
            CHECK(waitLog.find("timed out") != std::string::npos);
            CHECK(waitLog.find("no files were changed") != std::string::npos);
            CHECK(okay::restart::PidAlive(sleeper));
            CHECK(!fs::exists(result, ec));
            KillPid(sleeper);
        }
        fs::remove_all(dir, ec);
    }

    // --- Verified restart: the new program must be the published version before it starts; otherwise the previous one is put back. ---
    {
        std::error_code ec;
        const fs::path root = fs::temp_directory_path(ec) / ("okay-restart-verify-" + std::to_string(okay::restart::CurrentPid()));
        const fs::path self = okay::restart::CurrentExecutable();
        const std::string selfBytes = ReadFile(self);
        namespace su = okay::selfupdate;
#if defined(_WIN32)
        const char* exeName = "OkaySpace.exe";
#else
        const char* exeName = "OkaySpace";
#endif
        // One scenario: the launcher swapped `target` already (old file in target.old), then the helper runs. `newBytes` is the new file.
        struct Outcome { int rc = -1; std::string log, targetBytes, failedBytes, resultBody; su::State state; bool resultThere = false, failedThere = false; };
        auto scenario = [&](const std::string& tag, const std::string& newBytes, const std::string& oldBytes, bool keepOld, const char* testVersion, bool newClearsState) {
            const fs::path dir = root / tag;
            fs::remove_all(dir, ec);
            fs::create_directories(dir, ec);
            const fs::path target = dir / exeName, backup = su::BackupPath(target), state = su::StatePath(dir);
            const fs::path log = dir / "okay_update.log", result = dir / "result.txt", up = dir / "up.txt", lock = dir / "lock";
            WriteFile(target, newBytes);
            if (keepOld) WriteFile(backup, oldBytes);
            if (newBytes.rfind(selfBytes, 0) == 0) fs::permissions(target, fs::perms::owner_all, ec);
            if (keepOld && oldBytes.rfind(selfBytes, 0) == 0) fs::permissions(backup, fs::perms::owner_all, ec);
            std::string err;
            su::State sw; sw.phase = su::Phase::Swapped; sw.from = "9.9.8"; sw.to = "9.9.9"; sw.attempts = 1;
            CHECK(su::Write(state, sw, err));
            Request req;
            req.executable = target;
            req.rollbackTarget = target;
            req.replaceBackup = backup;
            req.stateFile = state;
            req.expectVersion = "9.9.9";
            req.confirmMs = 400;
            req.logFile = log;
            req.workingDirectory = dir;
            req.arguments = {"--restart-child", result.string(), up.string(), lock.string()};
            SetEnv("OKAY_TEST_VERSION", testVersion);
            SetEnv("OKAY_TEST_STATE_FILE", newClearsState ? state.string().c_str() : nullptr);
            Outcome o;
            // A scenario that stalls is reported with its log so far (CI would otherwise only show a bare timeout), then the test stops.
            std::mutex wdMx; std::condition_variable wdCv; bool wdDone = false;
            std::thread watchdog([&] {
                std::unique_lock<std::mutex> lk(wdMx);
                if (!wdCv.wait_for(lk, std::chrono::seconds(40), [&] { return wdDone; })) {
                    std::fprintf(stderr, "--- scenario %s STALLED for 40 s; log so far:\n%s\n", tag.c_str(), ReadFile(log).c_str());
                    std::fflush(stderr);
                    std::_Exit(3);
                }
            });
            o.rc = okay::restart::RunHelper(req);
            { std::lock_guard<std::mutex> lk(wdMx); wdDone = true; }
            wdCv.notify_all();
            watchdog.join();
            SetEnv("OKAY_TEST_VERSION", nullptr);
            SetEnv("OKAY_TEST_STATE_FILE", nullptr);
            // The launched child writes its result file a moment after it starts.
            for (int i = 0; i < 50 && !fs::exists(result, ec); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
            o.log = ReadFile(log);
            o.targetBytes = ReadFile(target);
            o.failedThere = fs::exists(su::FailedPath(target), ec);
            if (o.failedThere) o.failedBytes = ReadFile(su::FailedPath(target));
            o.resultThere = fs::exists(result, ec);
            if (o.resultThere) o.resultBody = ReadFile(result);
            o.state = su::Read(state);
            std::fprintf(stderr, "--- scenario %s: rc=%d, target %zu bytes, result file %s, state phase %d\n%s\n", tag.c_str(), o.rc,
                         o.targetBytes.size(), o.resultThere ? "there" : "missing", (int)o.state.phase, o.log.c_str());   // shown by ctest -V
            return o;
        };
        const std::string oldProg = selfBytes + "OLD-LAUNCHER", newProg = selfBytes + "NEW-LAUNCHER";

        // 1. the new file is the published version: self-check passes, it is launched once, it confirms, nothing is rolled back.
        Outcome o = scenario("good", newProg, oldProg, true, nullptr, true);
        CHECK(o.rc == 0 && o.log.find("self-check passed") != std::string::npos && o.log.find("confirmed the update") != std::string::npos);
        CHECK(o.resultThere && !o.failedThere && o.targetBytes == newProg && o.state.phase == su::Phase::None);
        // 1b. a new file that starts but never confirms is left alone (it may just be slow); the log says so.
        o = scenario("slow", newProg, oldProg, true, nullptr, false);
        CHECK(o.rc == 0 && o.log.find("did not confirm within") != std::string::npos && o.targetBytes == newProg && o.state.phase == su::Phase::Swapped);

        // 2. the new file reports another version (the swapped-in build is not the published one): rolled back, the old one is started.
        o = scenario("wrongver", newProg, oldProg, true, "9.9.8", false);
        CHECK(o.rc != 0 && o.log.find("self-check of the new program failed") != std::string::npos && o.log.find("rolled back") != std::string::npos);
        CHECK(o.targetBytes == oldProg && o.failedThere && o.failedBytes == newProg);                  // the previous launcher is back; the bad file is kept aside
        CHECK(o.state.phase == su::Phase::Failed && o.state.to == "9.9.9" && o.state.attempts == 1 && o.state.notified == 0 &&
              o.state.reason.find("start-up check") != std::string::npos && o.state.reason.find("previous version was put back") != std::string::npos);
        CHECK(o.resultThere);                                                                          // the previous launcher was started
        int launches = 0;
        for (std::size_t p = 0; (p = o.log.find("launched the previous program", p)) != std::string::npos; p += 10) ++launches;
        CHECK(launches == 1);
        // ...and at its next start the verdict is what the person sees.
        const su::Startup v = su::EvaluateStartup(o.state, "9.9.8");
        CHECK(v.kind == su::StartupKind::FailedBefore && v.showDialog && v.message.find("start-up check") != std::string::npos);

        // 3. the new file cannot be started at all (damaged, or a scanner replaced it with something else): same rollback.
        o = scenario("broken", "this is not a program\n", oldProg, true, nullptr, false);
        CHECK(o.rc != 0 && o.log.find("could not be started") != std::string::npos && o.log.find("rolled back") != std::string::npos);
        CHECK(o.targetBytes == oldProg && o.failedBytes == "this is not a program\n" && o.state.phase == su::Phase::Failed && o.resultThere);

        // 4. nothing to go back to: say so, change nothing else, and do NOT start a file nobody has checked.
        o = scenario("nobackup", "this is not a program\n", "", false, nullptr, false);
        CHECK(o.rc != 0 && o.log.find("rollback failed") != std::string::npos && o.log.find("no previous file") != std::string::npos);
        CHECK(!o.resultThere && o.state.phase == su::Phase::Failed && o.state.reason.find("could not be put back") != std::string::npos);
        fs::remove_all(root, ec);
    }

    // --- Old order: start the new process while this one holds the lock. It exits. ---
    {
        std::error_code ec;
        const fs::path dir = fs::temp_directory_path(ec) / ("okay-restart-race-" + std::to_string(okay::restart::CurrentPid()));
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        const fs::path lock = dir / "single.lock";
        const fs::path result = dir / "result.txt";
        const fs::path up = dir / "up.txt";
        HeldLock held;
        CHECK(held.Acquire(lock));
        const fs::path self = okay::restart::CurrentExecutable();
        int code = -1;
        const int wr = SpawnAndWait(
            {okay::restart::Utf8(self), "--restart-child", okay::restart::Utf8(result), okay::restart::Utf8(up),
             okay::restart::Utf8(lock), "hello world"},
            5000, code);
        CHECK(wr == 0);
        CHECK(code == 0);
        const std::string body = ReadFile(result);
        CHECK(Field(body, "lock") == "busy");
        CHECK(Field(body, "mutex") == "busy");
        CHECK(Field(body, "stays") == "0");
        CHECK(!fs::exists(up, ec));
        held.Release();
        fs::remove_all(dir, ec);
    }

    // --- Helper waits, replaces, and the new process stays up exactly once. ---
    {
        std::error_code ec;
        const fs::path root = fs::temp_directory_path(ec) / ("okay-restart-e2e-" + std::to_string(okay::restart::CurrentPid()));
        fs::remove_all(root, ec);
        // Spaces and parentheses: the old cmd.exe helper dropped this kind of path.
        const fs::path install = root / "Program Files (x86)" / "Okay";
        const fs::path work = root / "work dir";
        fs::create_directories(install / "Projects" / "My Game" / "Assets", ec);
        fs::create_directories(work, ec);
#if defined(_WIN32)
        const fs::path target = install / "OkayApp.exe";
        const fs::path source = install / "OkayApp.exe.new";
#else
        const fs::path target = install / "OkayApp";
        const fs::path source = install / "OkayApp.new";
#endif
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
        const std::string openArg = "C:\\Program Files (x86)\\Okay\\My Game.okayscene";
        int code = -1;
        const int wr = SpawnAndWait({
            okay::restart::Utf8(self), "--restart-holder",
            okay::restart::Utf8(target), okay::restart::Utf8(source), okay::restart::Utf8(result),
            okay::restart::Utf8(up), okay::restart::Utf8(work), okay::restart::Utf8(log),
            okay::restart::Utf8(lock),
            "--launcher", "42", "--open", openArg, "hello world"
        }, 15000, code);
        if (wr != 0 || code != 0)
            std::cerr << ReadFile(log) << "\n";
        CHECK(wr == 0);
        CHECK(code == 0);
        bool saw = false;
        for (int i = 0; i < 50 && !saw; ++i) {
            saw = fs::exists(result, ec);
            if (!saw) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (!saw) std::cerr << ReadFile(log) << "\n";
        CHECK(saw);
        const std::string body = ReadFile(result);
        CHECK(Field(body, "lock") == "free");
        CHECK(Field(body, "mutex") == "free");
        CHECK(Field(body, "stays") == "1");
        CHECK(Canon(Field(body, "cwd")) == Canon(work));
        CHECK(Canon(Field(body, "exe")) == Canon(target));
        CHECK(Field(body, "exe").find("Program Files (x86)") != std::string::npos);
        CHECK(HasArg(body, "--launcher"));
        CHECK(HasArg(body, "42"));
        CHECK(HasArg(body, openArg));
        CHECK(HasArg(body, "hello world"));
        CHECK(CountChar(ReadFile(up), '\n') == 1);
        CHECK(fs::file_size(target, ec) == sourceBytes);
        CHECK(!fs::exists(source, ec));
        const std::string installed = ReadFile(target);
#if defined(_WIN32)
        CHECK(installed.size() >= 2 && installed[0] == 'M' && installed[1] == 'Z');
#else
        CHECK(installed.size() >= 4 && installed[0] == '\x7f' && installed[1] == 'E');
        const auto perms = fs::status(target, ec).permissions();
        CHECK((perms & fs::perms::owner_exec) != fs::perms::none);
#endif
        fs::path backup = target;
        backup += ".old";
        CHECK(ReadFile(backup) == "OLD_VERSION\n");
        CHECK(ReadFile(decoy) == "scene-v1");
        CHECK(ReadFile(settings) == "volume=1\n");
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

#if defined(_WIN32)
    CHECK(okay::restart::RunAndWait({"cmd", "/c", "exit", "0"}) == 0);
    CHECK(okay::restart::RunAndWait({"C:\\no\\such\\okay-restart-bin.exe"}) == -1);
#else
    CHECK(okay::restart::RunAndWait({"/bin/true"}) == 0);
    CHECK(okay::restart::RunAndWait({"/no/such/okay-restart-bin"}) == -1);
#endif

    std::cout << okaytest::g_checks << " checks, " << okaytest::g_failures << " failures\n";
    return okaytest::g_failures == 0 ? 0 : 1;
}
