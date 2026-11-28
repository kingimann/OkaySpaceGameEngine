#pragma once
// The launcher's command line: the rules behind its "Command line" page and its "Open command line" button, with no
// ImGui, so tests/test_launcher_cli.cpp checks them headless. The page itself is CommandLineUI.cpp.
//
// The page has tabs. Each runs one of:
//   * a shell (PowerShell or Command Prompt on Windows, the person's $SHELL elsewhere) on a pseudo terminal
//     (okaycli::Pty), shown by the launcher's own terminal emulator (Terminal.hpp): ShellSession below. It is the person's
//     own shell on their own computer, the same as opening PowerShell, so nothing they type is limited; nothing from the
//     network or from other people is ever sent to it.
//   * "okay only", the safe mode kept from before (Session below). Only the engine's own `okay` program is run. The
//     typed line is split into words here and handed to okay as a list of arguments (tools/okay-cli/Process.hpp Child:
//     CreateProcessW / fork + execv), never to a shell, so nothing typed can start another program or chain commands.
//     Tab completion comes from okay's own command table (tools/okay-cli/CommandTable.hpp), the same list its help and
//     its bash / PowerShell completion scripts use. Output is read while the command runs, without waiting
//     (Session::Poll, once a frame), so the launcher never stalls.
//
// Our own code, written from the Win32 CreateProcess and cmd.exe documentation and POSIX fork / exec.
#include <cstddef>
#include <string>
#include <vector>

#include "PageLayout.hpp"  // launcher: Rect, Overlaps, ButtonW (pure maths, no ImGui)
#include "CommandInput.hpp"   // tools/okay-cli: words, Tab completion, history, output lines (shared with the editor's panel)
#include "Process.hpp"   // tools/okay-cli: the one place that starts a program
#include "Terminal.hpp"  // launcher: the terminal emulator a shell tab shows

namespace okaylcli {

// ---- words and arguments ------------------------------------------------------------------------------------------

// The rules of a typed line, Tab completion, the history and the output lines are okay's own (tools/okay-cli/CommandInput.hpp),
// shared with the editor's Command Line panel (shared/CommandLinePanel.hpp): one copy.
using okaycli::SplitWords;
using okaycli::LineKind;
using okaycli::Classify;
using okaycli::Completion;
using okaycli::Complete;
using okaycli::History;
using okaycli::OutLine;
using okaycli::CleanOutput;

/// The argument list that runs `line` with okay: { okayExe, word, word ... }. A leading "okay" or "okay.exe" the person
/// typed out of habit is dropped. False with a plain reason (no okay program, an unclosed quote, nothing to run).
bool BuildArgv(const std::string& okayExe, const std::string& line, std::vector<std::string>& argv, std::string& error);

// ---- a session: the lines on the page and the command running --------------------------------------------------

class Session {
public:
    explicit Session(std::size_t maxLines = 5000) : log_(maxLines) {}
    void SetProgram(const std::string& okayExe) { okay_ = okayExe; }
    void SetFolder(const std::string& folder) { folder_ = folder; }
    const std::string& Folder() const { return folder_; }
    const std::string& Program() const { return okay_; }

    /// Run a typed line: `clear` empties the page, anything else goes to okay in the project's folder. The line is added
    /// to the history. False (with a note on the page) when it could not start or a command is still running.
    bool Submit(const std::string& line);
    /// Read what the running command wrote since the last call, without waiting. Call once a frame.
    void Poll();
    /// End the running command (and what it started).
    void Stop();
    bool Running() const { return child_.Running(); }
    void Clear() { log_.Clear(); }
    void Note(const std::string& text) { log_.Add(text, OutLine::Note); }

    const std::vector<OutLine>& Lines() const { return log_.Lines(); }
    /// Every line joined with newlines (what the output box shows and Copy all copies).
    const std::string& Text() const { return log_.Text(); }
    /// Goes up whenever the lines change (the page scrolls to the end when it moves).
    unsigned Revision() const { return log_.Revision(); }
    History& GetHistory() { return history_; }

private:
    okaycli::Child child_;
    History history_;
    okaycli::OutputLog log_;   ///< the lines (tools/okay-cli/CommandInput.hpp, shared with the editor's panel)
    std::string outPartial_, errPartial_;
    std::string okay_, folder_;
    bool running_ = false;   ///< Poll's view, to say once that the command ended
};

// ---- shells -------------------------------------------------------------------------------------------------------

/// What a tab runs. Okay is the safe mode (only the okay program, an argument list, no shell).
enum class ShellKind { PowerShell, CommandPrompt, UserShell, Okay };
/// The kinds this computer offers, the default first: Windows PowerShell, Command Prompt, okay only; elsewhere the
/// person's shell, okay only.
std::vector<ShellKind> AvailableShells();
ShellKind DefaultShell();
/// The name shown on a tab and in the picker: "PowerShell", "Command Prompt", "okay only", or the shell's own name
/// ("bash", "zsh") for UserShell.
std::string ShellLabel(ShellKind kind);
/// The shell's program and arguments. Windows: powershell.exe (-NoLogo) and cmd.exe from the system folder, never from
/// PATH or the project. Elsewhere: $SHELL when it is an absolute path to a program, else /bin/bash, else /bin/sh.
/// Empty with a plain reason when there is none (and always for Okay).
std::vector<std::string> ShellArgv(ShellKind kind, std::string& error);

/// One shell tab: the terminal and the shell on its pseudo terminal.
class ShellSession {
public:
    explicit ShellSession(int cols = 100, int rows = 30, std::size_t scrollback = 5000) : term_(cols, rows, scrollback) {}
    /// Start the shell in `folder` with `toolsDir` first on PATH (so `okay` works). False with the reason, which is
    /// also written into the terminal.
    bool Start(ShellKind kind, const std::string& folder, const std::string& toolsDir, std::string& error);
    /// The same with an exact program and arguments (the tests run /bin/sh this way).
    bool StartArgv(const std::vector<std::string>& argv, const std::string& folder, const std::string& toolsDir, std::string& error);
    /// Read the shell's output into the terminal and send back the terminal's replies. Call once a frame. When the
    /// shell ends, a line saying so is written into the terminal.
    void Poll();
    /// Keys, typed text or a paste, as bytes (Terminal.hpp KeySequence / CharSequence / PasteSequence).
    bool Send(const std::string& bytes);
    /// Paste text the way the shell asked for (bracketed or not).
    bool Paste(const std::string& text) { return Send(okayterm::PasteSequence(text, term_.BracketedPaste())); }
    /// The terminal and the pseudo terminal get the new size (in cells).
    void Resize(int cols, int rows);
    void Stop();
    bool Running() const { return pty_.Running(); }
    bool Started() const { return started_; }
    int ExitCode() const { return pty_.ExitCode(); }
    okayterm::Terminal& Term() { return term_; }
    const okayterm::Terminal& Term() const { return term_; }

private:
    okayterm::Terminal term_;
    okaycli::Pty pty_;
    bool started_ = false, endedNoted_ = false;
};

// ---- a terminal window --------------------------------------------------------------------------------------------

/// The program and arguments that open a terminal window: Windows cmd.exe from the system folder with /K and a short
/// greeting; elsewhere the desktop's terminal (x-terminal-emulator, gnome-terminal, konsole or xterm), "" when none.
std::vector<std::string> TerminalArgv();
/// Open a terminal window in `folder` with `toolsDir` first on PATH for that window only, so `okay ...` works there.
/// False with a plain reason.
bool OpenTerminal(const std::string& folder, const std::string& toolsDir, std::string& error);

// ---- git for a project (the Projects page's Clone, Pull, Commit and push, Publish and new-project Git buttons) --------

/// One git step: the words after `git -C <folder>`. Each word is one argument to git: a commit message, URL or folder
/// with quotes, `$(...)`, `;` or `&` in it stays text, because no shell ever sees it (#888 F50: the buttons used to build
/// a shell line with hand-made quotes, so a commit message could run a command).
using GitStep = std::vector<std::string>;
/// The argument list of one step: { git, "-C", folder, step... }.
std::vector<std::string> GitArgv(const std::string& git, const std::string& folder, const GitStep& step);
/// Run `steps` one after another (okaycli::Run, output captured, no console window, no shell). Every step runs, as
/// before; the last one's exit code is the result (-1 when git could not start). `git` "" means "git" from PATH.
int RunGitSteps(const std::string& folder, const std::vector<GitStep>& steps, const std::string& git = std::string());
/// git can be started (`git --version` exits 0).
bool GitWorks(const std::string& git = std::string());

// ---- the programs of their own -------------------------------------------------------------------------------------

/// What a program opens when it is given one argument (its own main.cpp): the browser takes an address
/// (browser/main.cpp: ADDRESS ...), the Script Editor and Visual Script a project folder (FOLDER | FILE ...).
enum class ToolTarget { Folder, Address };
/// A program the page offers to start in a window of its own (owner, 2026-11-26: the browser, the command line, the Script
/// Editor and Visual Script are programs of their own too). Found by its file name; not offered when no file is there.
struct ToolProgram {
    std::string label, tip;
    std::vector<std::string> files;   ///< the file names it may have (the Windows release name first)
    ToolTarget target = ToolTarget::Folder;
};
/// Every such program the launcher knows: Browser, Script Editor and Visual Script.
const std::vector<ToolProgram>& ToolPrograms();
struct FoundProgram {
    std::string label, tip, path;
    ToolTarget target = ToolTarget::Folder;
};
/// The ToolPrograms() that exist in one of `dirs` (the launcher's folder, then its Tools folder), in the table's order.
std::vector<FoundProgram> FindPrograms(const std::vector<std::string>& dirs);
/// Where FindPrograms looks for the launcher's programs: the launcher's own folder, then its Tools folder.
std::vector<std::string> ProgramDirs(const std::string& launcherDir);

/// The argument list that starts `p` on `target` (owner, 2026-11-28: the Script Editor, Visual Script and the browser start
/// from the launcher's Create and Browser pages): { p.path } alone when `target` is "" or not something `p` opens, else
/// { p.path, target } as one argument (no shell sees it). A folder is made absolute, so it never reads as an option. An
/// address must be http or https; anything else (our own about: pages, a file, an option such as --play) is left out, so
/// the browser is only ever started to browse: these are editing and browsing tools, nothing is played from here.
std::vector<std::string> ToolProgramArgv(const FoundProgram& p, const std::string& target);
/// The plain sentence shown when the program called `label` is not installed (an older install): it names the program and
/// the file the launcher looked for, and says how to get it.
std::string MissingProgramMessage(const std::string& label);
/// Start the program called `label` from `found` (FindPrograms) on `target` (ToolProgramArgv), in a window of its own and without
/// waiting for it (okaycli::StartDetached, no shell). It runs in the project folder, or the program's own folder. False when
/// it is missing, the folder is gone or it could not start; `message` is what to show the person either way.
bool StartTool(const std::vector<FoundProgram>& found, const std::string& label, const std::string& target, std::string& message);

// ---- the page's layout --------------------------------------------------------------------------------------------

using okay::launcherlayout::Rect;   // launcher/PageLayout.hpp: the launcher pages' one box type and overlap test

/// Where everything on the page goes inside a `w` x `h` area (the page body under the section header).
/// Row 1: the tabs ("+" makes a new one). Row 2: the shell picker and the project picker, then Open in external window,
/// Copy, Clear and the font size buttons on the right (on their own row when narrow; the font buttons are left out
/// when even that row has no room: Ctrl+= and Ctrl+- still work). Then the one-line hint and the terminal or output
/// (all the height that is left). In okay-only mode the input row goes at the bottom: the line, Run and Stop.
/// A Rect with w == 0 is not shown.
struct PageLayout {
    Rect tabs, shell, project, terminal, copy, clear, smaller, bigger, hint, output, input, run, stop;
    std::vector<Rect> All() const {
        std::vector<Rect> v;
        for (const Rect& r : {tabs, shell, project, terminal, copy, clear, smaller, bigger, hint, output, input, run, stop})
            if (r.w > 0) v.push_back(r);
        return v;
    }
};
/// The buttons' widths (FitButtonW in the launcher: the label plus padding, so text never spills).
struct PageButtons { float shell = 150, terminal = 160, copy = 84, clear = 64, font = 28, run = 64, stop = 64; };
PageLayout LayoutPage(float w, float h, float frameH, float lineH, const PageButtons& b = PageButtons(), bool okayMode = true);

/// How many terminal cells fit in a `w` x `h` box with cells of `cellW` x `cellH` (at least 2 x 1).
void CellsFor(float w, float h, float cellW, float cellH, int& cols, int& rows);

} // namespace okaylcli
