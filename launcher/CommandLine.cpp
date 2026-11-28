// The launcher's command line. See CommandLine.hpp. Our own code: word splitting, completion and history are
// tools/okay-cli/CommandInput.cpp (shared with the editor's Command Line panel); programs start only through
// tools/okay-cli/Process.cpp; the command list is tools/okay-cli/CommandTable.cpp.
#include "CommandLine.hpp"


#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <system_error>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace okaylcli {
namespace fs = std::filesystem;

// ---- git for a project --------------------------------------------------------------------------------------------

std::vector<std::string> GitArgv(const std::string& git, const std::string& folder, const GitStep& step) {
    std::vector<std::string> argv = {git.empty() ? std::string("git") : git, "-C", folder};
    argv.insert(argv.end(), step.begin(), step.end());
    return argv;
}

int RunGitSteps(const std::string& folder, const std::vector<GitStep>& steps, const std::string& git) {
    int rc = -1;
    for (const GitStep& step : steps) {
        const okaycli::RunResult r = okaycli::Run(GitArgv(git, folder, step), std::string(), true);
        rc = r.started ? r.exitCode : -1;
    }
    return rc;
}

bool GitWorks(const std::string& git) {
    const okaycli::RunResult r = okaycli::Run({git.empty() ? std::string("git") : git, "--version"}, std::string(), true);
    return r.started && r.exitCode == 0;
}

// ---- words and arguments ------------------------------------------------------------------------------------------
// SplitWords, Classify, Complete, History and the output lines: tools/okay-cli/CommandInput.cpp (shared with the editor).

bool BuildArgv(const std::string& okayExe, const std::string& line, std::vector<std::string>& argv, std::string& error) {
    argv.clear();
    if (okayExe.empty()) {
        error = "The okay program (okay.exe) was not found in the Tools folder. Reinstall or update to get it.";
        return false;
    }
    std::vector<std::string> words;
    if (!SplitWords(line, words, error)) return false;
    if (!words.empty() && okaycli::IsOkayWord(words[0])) words.erase(words.begin());
    if (words.empty()) { error = "Type a command for okay, for example: help"; return false; }
    argv.push_back(okayExe);
    argv.insert(argv.end(), words.begin(), words.end());
    return true;
}

// ---- a session ----------------------------------------------------------------------------------------------------

bool Session::Submit(const std::string& line) {
    history_.Push(line);
    switch (Classify(line)) {
    case LineKind::Empty: return false;
    case LineKind::Clear: Clear(); return true;
    case LineKind::Okay: break;
    }
    if (child_.Running()) { Note("A command is still running. Wait for it, or press Stop."); return false; }
    log_.Add("> " + line, OutLine::Note);
    std::vector<std::string> argv;
    std::string error;
    if (!BuildArgv(okay_, line, argv, error)) { Note(error); return false; }
    if (!child_.Start(argv, folder_, error)) { Note(error); return false; }
    running_ = true;
    return true;
}

void Session::Poll() {
    if (!running_) return;
    std::string out, err;
    const bool still = child_.Poll(out, err);
    log_.Feed(outPartial_, out, OutLine::Out, !still);
    log_.Feed(errPartial_, err, OutLine::Err, !still);
    if (!still) {
        running_ = false;
        if (child_.Stopped()) Note("Stopped.");
        else if (child_.ExitCode() == 0) Note("Done.");
        else Note("Finished with exit code " + std::to_string(child_.ExitCode()) + ".");
    }
}

void Session::Stop() {
    if (child_.Running()) child_.Stop();
}

// ---- the programs of their own -------------------------------------------------------------------------------------

const std::vector<ToolProgram>& ToolPrograms() {
    static const std::vector<ToolProgram> k = {
        {"Browser", "The OkaySpace browser in a window of its own (the same browser as the Browser page).",
         {"OkaySpaceBrowser.exe", "okay-browser.exe", "okay-browser"}, ToolTarget::Address},
        {"Script Editor", "The Script Editor in a window of its own.",
         {"OkaySpaceScriptEditor.exe", "okay-script-editor.exe", "okay-script-editor"}, ToolTarget::Folder},
        {"Visual Script", "The Visual Script editor in a window of its own.",
         {"OkaySpaceVisualScript.exe", "okay-visual-script.exe", "okay-visual-script"}, ToolTarget::Folder},
    };
    return k;
}

std::vector<FoundProgram> FindPrograms(const std::vector<std::string>& dirs) {
    std::vector<FoundProgram> found;
    std::error_code ec;
    for (const ToolProgram& p : ToolPrograms()) {
        std::string path;
        for (const std::string& d : dirs) {
            if (d.empty()) continue;
            for (const std::string& f : p.files) {
                const fs::path full = fs::u8path(d) / fs::u8path(f);
                if (fs::is_regular_file(full, ec)) { path = full.u8string(); break; }
            }
            if (!path.empty()) break;
        }
        if (!path.empty()) found.push_back({p.label, p.tip, path, p.target});
    }
    return found;
}

std::vector<std::string> ProgramDirs(const std::string& launcherDir) {
    if (launcherDir.empty()) return {};
    return {launcherDir, (fs::u8path(launcherDir) / "Tools").u8string()};
}

std::vector<std::string> ToolArgv(const FoundProgram& p, const std::string& target) {
    std::vector<std::string> argv = {p.path};
    if (target.empty()) return argv;
    if (p.target == ToolTarget::Address) {
        // Only a web address: "about:" pages, files and options (--play) are never handed to the browser from here.
        const auto starts = [&](const char* s) { return target.compare(0, std::char_traits<char>::length(s), s) == 0; };
        if (starts("https://") || starts("http://")) argv.push_back(target);
        return argv;
    }
    std::error_code ec;
    fs::path folder = fs::absolute(fs::u8path(target), ec);
    if (ec || folder.empty()) return argv;
    argv.push_back(folder.lexically_normal().u8string());
    return argv;
}

std::string MissingProgramMessage(const std::string& label) {
    for (const ToolProgram& p : ToolPrograms())
        if (p.label == label)
            return label + " is not installed: the launcher's Tools folder has no " + p.files[0] +
                   ". Update or reinstall OkaySpace to get it.";
    return "The launcher does not know a program called \"" + label + "\".";
}

bool StartTool(const std::vector<FoundProgram>& found, const std::string& label, const std::string& target, std::string& message) {
    const FoundProgram* p = nullptr;
    for (const FoundProgram& f : found)
        if (f.label == label) { p = &f; break; }
    if (!p) { message = MissingProgramMessage(label); return false; }
    const std::vector<std::string> argv = ToolArgv(*p, target);
    std::string cwd = fs::u8path(p->path).parent_path().u8string();
    if (p->target == ToolTarget::Folder && argv.size() > 1) {
        std::error_code ec;
        if (!fs::is_directory(fs::u8path(argv[1]), ec)) { message = "The project folder was not found: " + argv[1]; return false; }
        cwd = argv[1];
    }
    if (!okaycli::StartDetached(argv, cwd)) { message = "Could not start " + label + " (" + p->path + ")."; return false; }
    message = label + " opened.";
    if (p->target == ToolTarget::Folder && argv.size() > 1) {   // "Script Editor opened on My Game."
        fs::path folder = fs::u8path(argv[1]);
        if (folder.filename().empty()) folder = folder.parent_path();   // a folder written with a trailing slash
        message = label + " opened on " + folder.filename().u8string() + ".";
    }
    return true;
}

// ---- shells -------------------------------------------------------------------------------------------------------

std::vector<ShellKind> AvailableShells() {
#if defined(_WIN32)
    return {ShellKind::PowerShell, ShellKind::CommandPrompt, ShellKind::Okay};
#else
    return {ShellKind::UserShell, ShellKind::Okay};
#endif
}

ShellKind DefaultShell() { return AvailableShells().front(); }

namespace {
#if defined(_WIN32)
std::string SystemFolder() {
    wchar_t dir[MAX_PATH + 1] = {};
    const UINT n = GetSystemDirectoryW(dir, MAX_PATH);
    if (n == 0 || n > MAX_PATH) return "";
    return fs::path(std::wstring(dir, n)).u8string();
}
#endif
bool IsProgram(const std::string& p) {
    std::error_code ec;
    if (p.empty() || !fs::path(p).is_absolute() || !fs::is_regular_file(fs::u8path(p), ec)) return false;
#if !defined(_WIN32)
    if (::access(p.c_str(), X_OK) != 0) return false;
#endif
    return true;
}
std::string HomeFolder() {
#if defined(_WIN32)
    const char* h = std::getenv("USERPROFILE");
#else
    const char* h = std::getenv("HOME");
#endif
    return h ? std::string(h) : std::string();
}
} // namespace

std::vector<std::string> ShellArgv(ShellKind kind, std::string& error) {
    error.clear();
#if defined(_WIN32)
    const std::string sys = SystemFolder();
    if (kind == ShellKind::PowerShell) {
        const std::string ps = sys.empty() ? std::string() : (fs::u8path(sys) / "WindowsPowerShell" / "v1.0" / "powershell.exe").u8string();
        if (IsProgram(ps)) return {ps, "-NoLogo"};
        error = "Windows PowerShell was not found in the system folder.";
        return {};
    }
    if (kind == ShellKind::CommandPrompt) {
        const std::string cmd = sys.empty() ? std::string() : (fs::u8path(sys) / "cmd.exe").u8string();
        if (IsProgram(cmd)) return {cmd};
        error = "cmd.exe was not found in the system folder.";
        return {};
    }
#else
    if (kind == ShellKind::UserShell) {
        if (const char* sh = std::getenv("SHELL"))
            if (IsProgram(sh)) return {sh};
        for (const char* p : {"/bin/bash", "/bin/sh"})
            if (IsProgram(p)) return {p};
        error = "No shell was found on this computer.";
        return {};
    }
#endif
    error = kind == ShellKind::Okay ? "okay only runs the okay program, not a shell." : "That shell is not on this computer.";
    return {};
}

std::string ShellLabel(ShellKind kind) {
    switch (kind) {
    case ShellKind::PowerShell: return "PowerShell";
    case ShellKind::CommandPrompt: return "Command Prompt";
    case ShellKind::Okay: return "okay only";
    case ShellKind::UserShell: {
        std::string err;
        const std::vector<std::string> argv = ShellArgv(kind, err);
        return argv.empty() ? std::string("Shell") : fs::path(argv[0]).filename().string();
    }
    }
    return "Shell";
}

bool ShellSession::Start(ShellKind kind, const std::string& folder, const std::string& toolsDir, std::string& error) {
    const std::vector<std::string> argv = ShellArgv(kind, error);
    if (argv.empty()) {
        term_.Feed("\x1b[31m" + error + "\x1b[0m\r\n");
        return false;
    }
    return StartArgv(argv, folder, toolsDir, error);
}

bool ShellSession::StartArgv(const std::vector<std::string>& argv, const std::string& folder, const std::string& toolsDir,
                             std::string& error) {
    okaycli::Pty::Options o;
    o.argv = argv;
    std::error_code ec;
    o.cwd = !folder.empty() && fs::is_directory(fs::u8path(folder), ec) ? folder : HomeFolder();
    o.pathFirst = toolsDir;
    o.cols = term_.Cols();
    o.rows = term_.Rows();
    endedNoted_ = false;
    if (!pty_.Start(o, error)) {
        term_.Feed("\x1b[31m" + error + "\x1b[0m\r\n");
        return false;
    }
    started_ = true;
    return true;
}

void ShellSession::Poll() {
    if (!started_) return;
    std::string out;
    pty_.Read(out);
    if (!out.empty()) term_.Feed(out);
    const std::string reply = term_.TakeReply();
    if (!reply.empty()) pty_.Write(reply);
    if (!pty_.Running() && !endedNoted_) {
        endedNoted_ = true;
        if (term_.AltScreen()) term_.Feed("\x1b[?1049l");   // a full-screen program that died: back to the text
        const std::string how = pty_.Stopped() ? std::string("The shell was stopped")
                                               : "The shell ended (exit code " + std::to_string(pty_.ExitCode()) + ")";
        term_.Feed("\r\n\x1b[0;90m[" + how + ". Press Enter to start it again.]\x1b[0m\r\n");
    }
}

bool ShellSession::Send(const std::string& bytes) { return !bytes.empty() && pty_.Write(bytes); }

void ShellSession::Resize(int cols, int rows) {
    if (cols == term_.Cols() && rows == term_.Rows()) return;
    term_.Resize(cols, rows);
    pty_.Resize(cols, rows);
}

void ShellSession::Stop() { pty_.Stop(); }

// ---- a terminal window --------------------------------------------------------------------------------------------

std::vector<std::string> TerminalArgv() {
#if defined(_WIN32)
    // cmd.exe from the system folder, never one found on PATH or in the project. /K keeps the window open after the
    // greeting; each word is its own argument, so nothing needs quoting.
    wchar_t dir[MAX_PATH + 1] = {};
    const UINT n = GetSystemDirectoryW(dir, MAX_PATH);
    if (n == 0 || n > MAX_PATH) return {};
    const std::string cmd = (fs::path(std::wstring(dir, n)) / "cmd.exe").u8string();
    return {cmd, "/K", "echo", "OkaySpace", "command", "line.", "Type", "okay", "help", "to", "see", "what", "it", "can", "do."};
#else
    for (const char* t : {"x-terminal-emulator", "gnome-terminal", "konsole", "xterm"}) {
        const std::string p = okaycli::FindOnPath(t);
        if (!p.empty()) return {p};
    }
    return {};
#endif
}

bool OpenTerminal(const std::string& folder, const std::string& toolsDir, std::string& error) {
    const std::vector<std::string> argv = TerminalArgv();
    if (argv.empty()) { error = "No terminal program was found on this computer."; return false; }
    std::error_code ec;
    if (folder.empty() || !fs::is_directory(fs::u8path(folder), ec)) { error = "The project folder was not found."; return false; }
    okaycli::DetachOptions o;
    o.pathFirst = toolsDir;
    o.newConsole = true;
    o.title = "OkaySpace command line";
    if (!okaycli::StartDetached(argv, folder, o)) { error = "The terminal window could not be opened."; return false; }
    return true;
}

// ---- the page's layout --------------------------------------------------------------------------------------------

PageLayout LayoutPage(float w, float h, float frameH, float lineH, const PageButtons& b, bool okayMode) {
    PageLayout L;
    const float gap = 8.0f;
    L.tabs = {0, 0, w, frameH};
    float y = frameH + 6.0f;
    // The buttons, right to left: A+, A-, Clear, Copy, Open in external window.
    auto buttonsW = [&](bool font) { return b.terminal + gap + b.copy + gap + b.clear + (font ? 2 * (gap + b.font) : 0.0f); };
    auto placeButtons = [&](float rowY, bool font) {
        float x = w;
        if (font) {
            L.bigger = {x - b.font, rowY, b.font, frameH};
            x -= b.font + gap;
            L.smaller = {x - b.font, rowY, b.font, frameH};
            x -= b.font + gap;
        }
        L.clear = {x - b.clear, rowY, b.clear, frameH};
        x -= b.clear + gap;
        L.copy = {x - b.copy, rowY, b.copy, frameH};
        x -= b.copy + gap;
        L.terminal = {x - b.terminal, rowY, b.terminal, frameH};
        return x - b.terminal - gap;   // what is left on the left
    };
    const float pickersMin = b.shell + gap + 160.0f;
    if (w - buttonsW(true) - gap >= pickersMin) {   // one row: the pickers, then the buttons
        const float left = placeButtons(y, true);
        L.shell = {0, y, b.shell, frameH};
        L.project = {b.shell + gap, y, std::max(0.0f, left - b.shell - gap), frameH};
        y += frameH;
    } else {   // narrow: the pickers on one row (or two), the buttons under them
        if (w >= pickersMin) {
            L.shell = {0, y, b.shell, frameH};
            L.project = {b.shell + gap, y, w - b.shell - gap, frameH};
            y += frameH + 6.0f;
        } else {
            L.shell = {0, y, w, frameH};
            y += frameH + 6.0f;
            L.project = {0, y, w, frameH};
            y += frameH + 6.0f;
        }
        placeButtons(y, buttonsW(true) <= w);
        y += frameH;
    }
    L.hint = {0, y + 6.0f, w, lineH};
    const float outY = L.hint.B() + 8.0f;
    if (okayMode) {   // the input row: the line, then Run and Stop
        const float rowY = h - frameH;
        L.stop = {w - b.stop, rowY, b.stop, frameH};
        L.run = {w - b.stop - gap - b.run, rowY, b.run, frameH};
        L.input = {0, rowY, std::max(0.0f, L.run.x - gap), frameH};
        L.output = {0, outY, w, std::max(0.0f, rowY - 8.0f - outY)};
    } else {
        L.output = {0, outY, w, std::max(0.0f, h - outY)};
    }
    return L;
}

void CellsFor(float w, float h, float cellW, float cellH, int& cols, int& rows) {
    cols = cellW > 0 ? (int)(w / cellW) : 2;
    rows = cellH > 0 ? (int)(h / cellH) : 1;
    cols = std::max(2, std::min(cols, 1000));
    rows = std::max(1, std::min(rows, 500));
}

} // namespace okaylcli
