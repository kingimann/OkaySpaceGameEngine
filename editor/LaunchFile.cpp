#include "LaunchFile.hpp"
#include "ProjectContextModel.hpp"

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
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
#  include <cerrno>
#  include <fcntl.h>
#  include <sys/wait.h>
#  include <unistd.h>
#endif

namespace oked {
namespace launch {

namespace {

Result Fail(std::string why) {
    Result r;
    r.error = std::move(why);
    return r;
}
Result Ok() {
    Result r;
    r.ok = true;
    return r;
}

#if defined(_WIN32)

// ---- Windows: ShellExecuteW with a fixed program and the wide path; no cmd.exe, so no character of a file name means anything ----

// The panel hands over UTF-8 (names from the project database) or the ANSI code page (names made for the file dialogs); a
// string that is not valid UTF-8 is read as ANSI.
std::wstring ToWide(const std::string& s) {
    if (s.empty()) return {};
    UINT cp = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    int n = MultiByteToWideChar(cp, flags, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) {
        cp = CP_ACP;
        flags = 0;
        n = MultiByteToWideChar(cp, flags, s.data(), static_cast<int>(s.size()), nullptr, 0);
    }
    if (n <= 0) return {};
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(cp, flags, s.data(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

std::wstring Backslashes(std::wstring w) {
    for (wchar_t& c : w) if (c == L'/') c = L'\\';
    return w;
}

bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
bool IsFolder(const std::wstring& p) {
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
}
bool IsFile(const std::wstring& p) {
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::wstring Env(const wchar_t* name) {
    DWORD n = GetEnvironmentVariableW(name, nullptr, 0);
    if (n == 0) return {};
    std::wstring v(static_cast<std::size_t>(n), L'\0');
    n = GetEnvironmentVariableW(name, &v[0], n);
    v.resize(n);
    return v;
}
std::wstring SystemDir() {
    wchar_t buf[MAX_PATH];
    const UINT n = GetSystemDirectoryW(buf, MAX_PATH);
    return n > 0 && n < MAX_PATH ? std::wstring(buf, n) : std::wstring();
}
std::wstring WindowsDir() {
    wchar_t buf[MAX_PATH];
    const UINT n = GetWindowsDirectoryW(buf, MAX_PATH);
    return n > 0 && n < MAX_PATH ? std::wstring(buf, n) : std::wstring();
}

// Run `file` with `params` through the shell's "open" verb. Callers pass a program they chose (VS Code, Notepad, Explorer) with
// the file as its parameter; only OpenWithDefaultApp passes the file itself, and only after LaunchRefusal said it does not run code.
Result ShellRun(const std::wstring& file, const std::wstring& params) {
    const HINSTANCE h = ShellExecuteW(nullptr, L"open", file.c_str(), params.empty() ? nullptr : params.c_str(), nullptr, SW_SHOWNORMAL);
    const INT_PTR code = reinterpret_cast<INT_PTR>(h);
    if (code > 32) return Ok();
    switch (code) {
        case SE_ERR_NOASSOC:
        case SE_ERR_ASSOCINCOMPLETE: return Fail("Windows has no program set to open this type of file.");
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:   return Fail("Windows could not find it.");
        case SE_ERR_ACCESSDENIED:    return Fail("Windows refused to open it (access denied).");
        default:                     return Fail("Windows could not open it (error " + std::to_string(static_cast<long long>(code)) + ").");
    }
}

// Visual Studio Code: the usual install folders, then a custom install whose "bin" folder (with code.cmd) is on PATH.
std::wstring FindVsCode() {
    std::vector<std::wstring> candidates;
    const std::wstring local = Env(L"LOCALAPPDATA"), pf = Env(L"ProgramFiles"), pf86 = Env(L"ProgramFiles(x86)");
    if (!local.empty()) candidates.push_back(local + L"\\Programs\\Microsoft VS Code\\Code.exe");
    if (!pf.empty()) candidates.push_back(pf + L"\\Microsoft VS Code\\Code.exe");
    if (!pf86.empty()) candidates.push_back(pf86 + L"\\Microsoft VS Code\\Code.exe");
    const std::wstring path = Env(L"PATH");
    std::size_t start = 0;
    while (start <= path.size()) {
        std::size_t end = path.find(L';', start);
        if (end == std::wstring::npos) end = path.size();
        std::wstring dir = path.substr(start, end - start);
        if (dir.size() >= 2 && dir.front() == L'"' && dir.back() == L'"') dir = dir.substr(1, dir.size() - 2);
        while (!dir.empty() && (dir.back() == L'\\' || dir.back() == L'/')) dir.pop_back();
        if (!dir.empty() && IsFile(dir + L"\\code.cmd")) {
            const std::size_t sl = dir.find_last_of(L"\\/");
            if (sl != std::wstring::npos) candidates.push_back(dir.substr(0, sl) + L"\\Code.exe");
        }
        start = end + 1;
    }
    for (const std::wstring& c : candidates) if (IsFile(c)) return c;
    return {};
}

#else

// ---- macOS and Linux: fork and execv, no shell ----

namespace fs = std::filesystem;

// A program on PATH (absolute PATH entries only: never the current folder, which can be a project folder).
std::string FindOnPath(const std::string& name) {
    const char* env = std::getenv("PATH");
    const std::string path = env ? env : "/usr/local/bin:/usr/bin:/bin";
    std::size_t start = 0;
    while (start <= path.size()) {
        std::size_t end = path.find(':', start);
        if (end == std::string::npos) end = path.size();
        const std::string dir = path.substr(start, end - start);
        start = end + 1;
        if (dir.empty() || dir[0] != '/') continue;
        const std::string full = dir + "/" + name;
        std::error_code ec;
        if (::access(full.c_str(), X_OK) == 0 && fs::is_regular_file(full, ec)) return full;
    }
    return {};
}

// Start `argv` (argv[0] a full path) and return at once. The program is started by a grandchild so that nothing is left
// to wait for and no zombie stays behind. Everything between the two forks is async-signal-safe.
Result Spawn(const std::vector<std::string>& argv) {
    std::vector<char*> a;
    for (const std::string& s : argv) a.push_back(const_cast<char*>(s.c_str()));
    a.push_back(nullptr);
    const pid_t first = ::fork();
    if (first < 0) return Fail("The system would not start another program.");
    if (first == 0) {
        const pid_t second = ::fork();
        if (second != 0) ::_exit(second < 0 ? 127 : 0);
        const int fd = ::open("/dev/null", O_RDWR);
        if (fd >= 0) {
            ::dup2(fd, 0);
            ::dup2(fd, 1);
            ::dup2(fd, 2);
            if (fd > 2) ::close(fd);
        }
        ::execv(a[0], a.data());
        ::_exit(127);
    }
    int status = 0;
    while (::waitpid(first, &status, 0) < 0 && errno == EINTR) {}
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) return Ok();
    return Fail("The system would not start another program.");
}

// A name that starts with '-' would be read as an option.
std::string NotAnOption(const std::string& path) { return !path.empty() && path[0] == '-' ? "./" + path : path; }

// The system's own "open this" program.
std::string OpenProgram() {
#if defined(__APPLE__)
    return FindOnPath("open");
#else
    return FindOnPath("xdg-open");
#endif
}

#endif

} // namespace

Result OpenWithDefaultApp(const std::string& path) {
    if (path.empty()) return Fail("No file was given.");
    // The last gate, whatever the menu checked: a program, installer, shortcut or script is never started from here.
    const std::string refused = okaypb::ctx::LaunchRefusal(path);
    if (!refused.empty()) return Fail(refused);
#if defined(_WIN32)
    const std::wstring file = Backslashes(ToWide(path));
    if (file.empty()) return Fail("The file name could not be read.");
    if (!Exists(file)) return Fail("The file is not there any more.");
    if (IsFolder(file)) return Fail("That is a folder: use Show in Explorer.");
    return ShellRun(file, std::wstring());
#else
    std::error_code ec;
    if (!fs::exists(fs::u8path(path), ec)) return Fail("The file is not there any more.");
    if (fs::is_directory(fs::u8path(path), ec)) return Fail("That is a folder: use Show in File Manager.");
    const std::string opener = OpenProgram();
    if (opener.empty()) return Fail("This system has no program to open files with (xdg-open or open).");
    return Spawn({opener, NotAnOption(path)});
#endif
}

Result OpenInTextEditor(const std::string& path) {
    if (path.empty()) return Fail("No file was given.");
#if defined(_WIN32)
    const std::wstring file = Backslashes(ToWide(path));
    if (file.empty()) return Fail("The file name could not be read.");
    if (file.find(L'"') != std::wstring::npos) return Fail("A file name cannot contain a quote.");
    if (!IsFile(file)) return Fail("The file is not there any more.");
    const std::wstring params = L"\"" + file + L"\"";
    const std::wstring code = FindVsCode();
    if (!code.empty()) {
        const Result r = ShellRun(code, params);
        if (r.ok) return r;
    }
    const std::wstring sys = SystemDir();
    if (!sys.empty() && IsFile(sys + L"\\notepad.exe")) return ShellRun(sys + L"\\notepad.exe", params);
    return Fail("No text editor was found (Visual Studio Code or Notepad).");
#else
    std::error_code ec;
    if (!fs::is_regular_file(fs::u8path(path), ec)) return Fail("The file is not there any more.");
    const std::string target = NotAnOption(path);
    const std::string code = FindOnPath("code");
    if (!code.empty()) return Spawn({code, target});
#if defined(__APPLE__)
    const std::string open = FindOnPath("open");
    if (!open.empty()) return Spawn({open, "-t", target});
#else
    for (const char* editor : {"gedit", "kate", "xed", "mousepad", "pluma", "gnome-text-editor", "kwrite"}) {
        const std::string e = FindOnPath(editor);
        if (!e.empty()) return Spawn({e, target});
    }
#endif
    return Fail("No text editor was found (install Visual Studio Code or a text editor).");
#endif
}

Result Reveal(const std::string& path, bool selectFile) {
    if (path.empty()) return Fail("No path was given.");
#if !defined(_WIN32)
    (void)selectFile;   // (a folder is told from a file on the disk)
#endif
#if defined(_WIN32)
    const std::wstring p = Backslashes(ToWide(path));
    if (p.empty()) return Fail("The path could not be read.");
    if (p.find(L'"') != std::wstring::npos) return Fail("A file name cannot contain a quote.");
    if (!Exists(p)) return Fail("It is not there any more.");
    const std::wstring win = WindowsDir();
    const std::wstring explorer = win.empty() ? std::wstring(L"explorer.exe") : win + L"\\explorer.exe";
    // Explorer is named here, never the file: a folder or a file is only shown, whatever its name looks like.
    if (selectFile || !IsFolder(p)) return ShellRun(explorer, L"/select,\"" + p + L"\"");
    return ShellRun(explorer, L"\"" + p + L"\"");
#else
    std::error_code ec;
    const bool isFolder = fs::is_directory(fs::u8path(path), ec);
    if (!isFolder && !fs::exists(fs::u8path(path), ec)) return Fail("It is not there any more.");
    const std::string opener = OpenProgram();
    if (opener.empty()) return Fail("This system has no program to show files with (xdg-open or open).");
#if defined(__APPLE__)
    if (!isFolder) return Spawn({opener, "-R", NotAnOption(path)});
    return Spawn({opener, NotAnOption(path)});
#else
    // (a folder is opened; for a file the folder that holds it, since xdg-open cannot pick a file)
    const std::string folder = isFolder ? path : fs::u8path(path).parent_path().generic_u8string();
    return Spawn({opener, NotAnOption(folder.empty() ? std::string(".") : folder)});
#endif
#endif
}

} // namespace launch
} // namespace oked
