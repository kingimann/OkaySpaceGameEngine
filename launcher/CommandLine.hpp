#pragma once
// The launcher's command line: the rules behind its "Command line" page and its "Open command line" button, with no
// ImGui, so tests/test_launcher_cli.cpp checks them headless. The page itself is CommandLineUI.cpp.
//
//   * Only the engine's own `okay` program is run. The typed line is split into words here and handed to okay as a list
//     of arguments (tools/okay-cli/Process.hpp Child: CreateProcessW / fork + execv), never to a shell, so nothing typed
//     can start another program or chain commands.
//   * Tab completion comes from okay's own command table (tools/okay-cli/CommandTable.hpp), the same list its help and
//     its bash / PowerShell completion scripts use.
//   * Output is read while the command runs, without waiting (Session::Poll, once a frame), so the launcher never stalls.
//
// Our own code, written from the Win32 CreateProcess and cmd.exe documentation and POSIX fork / exec.
#include <cstddef>
#include <string>
#include <vector>

#include "PageLayout.hpp"  // launcher: Rect, Overlaps, ButtonW (pure maths, no ImGui)
#include "Process.hpp"   // tools/okay-cli: the one place that starts a program

namespace okaylcli {

// ---- words and arguments ------------------------------------------------------------------------------------------

/// Split a typed line into words. Spaces and tabs separate words; "double" or 'single' quotes keep spaces inside one
/// word; inside double quotes \" is a quote and \\ a backslash; elsewhere a backslash is kept as it is (Windows paths).
/// False with a plain reason for an unclosed quote.
bool SplitWords(const std::string& line, std::vector<std::string>& words, std::string& error);

/// What a typed line asks for.
enum class LineKind { Empty, Clear, Okay };
/// `clear` or `cls` (the page's own command), nothing, or a command for okay.
LineKind Classify(const std::string& line);

/// The argument list that runs `line` with okay: { okayExe, word, word ... }. A leading "okay" or "okay.exe" the person
/// typed out of habit is dropped. False with a plain reason (no okay program, an unclosed quote, nothing to run).
bool BuildArgv(const std::string& okayExe, const std::string& line, std::vector<std::string>& argv, std::string& error);

// ---- Tab completion -----------------------------------------------------------------------------------------------

struct Completion {
    std::string line;                  ///< the line after Tab (the same line when nothing matched)
    std::vector<std::string> choices;  ///< every word that fits, when more than one does (shown under the line)
};
/// Complete the last word of `line` from okay's command table: the command name, then its sub-command (`vc sa` ->
/// `vc save `), and command names after `help`. One match is completed with a space after it; several are completed
/// as far as they agree and listed in `choices`.
Completion Complete(const std::string& line);

// ---- history ------------------------------------------------------------------------------------------------------

/// The lines run before, for Up and Down. The newest is last; a line equal to the newest is not added twice.
class History {
public:
    explicit History(std::size_t limit = 200) : limit_(limit) {}
    void Push(const std::string& line);
    /// Up: the line before the one shown (the newest first). `current` is what is in the box now, kept for Down.
    /// False when there is nothing older.
    bool Up(const std::string& current, std::string& line);
    /// Down: the newer line, or the line that was being typed before Up. False when already there.
    bool Down(std::string& line);
    std::size_t Size() const { return lines_.size(); }
    const std::vector<std::string>& Lines() const { return lines_; }

private:
    std::vector<std::string> lines_;
    std::size_t limit_;
    std::size_t pos_ = 0;      ///< lines_.size() = not browsing
    std::string draft_;        ///< what was typed before the first Up
};

// ---- a session: the lines on the page and the command running --------------------------------------------------

struct OutLine {
    enum Kind { Out, Err, Note };   ///< okay's stdout, its stderr, and the page's own notes (the command, how it ended)
    std::string text;
    Kind kind = Out;
};

class Session {
public:
    explicit Session(std::size_t maxLines = 5000) : maxLines_(maxLines) {}
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
    void Clear();
    void Note(const std::string& text) { AddLine(text, OutLine::Note); }

    const std::vector<OutLine>& Lines() const { return lines_; }
    /// Every line joined with newlines (what the output box shows and Copy all copies).
    const std::string& Text() const { return text_; }
    /// Goes up whenever the lines change (the page scrolls to the end when it moves).
    unsigned Revision() const { return revision_; }
    History& GetHistory() { return history_; }

private:
    void AddLine(const std::string& text, OutLine::Kind kind);
    void Feed(std::string& partial, const std::string& bytes, OutLine::Kind kind, bool flush);
    void RebuildText();

    okaycli::Child child_;
    History history_;
    std::vector<OutLine> lines_;
    std::string text_, outPartial_, errPartial_;
    std::string okay_, folder_;
    std::size_t maxLines_;
    unsigned revision_ = 0;
    bool running_ = false;   ///< Poll's view, to say once that the command ended
};

/// Drop terminal colour codes (ESC [ ... letter) and carriage returns from okay's output.
std::string CleanOutput(const std::string& text);

// ---- a terminal window --------------------------------------------------------------------------------------------

/// The program and arguments that open a terminal window: Windows cmd.exe from the system folder with /K and a short
/// greeting; elsewhere the desktop's terminal (x-terminal-emulator, gnome-terminal, konsole or xterm), "" when none.
std::vector<std::string> TerminalArgv();
/// Open a terminal window in `folder` with `toolsDir` first on PATH for that window only, so `okay ...` works there.
/// False with a plain reason.
bool OpenTerminal(const std::string& folder, const std::string& toolsDir, std::string& error);

// ---- the page's layout --------------------------------------------------------------------------------------------

using okay::launcherlayout::Rect;   // launcher/PageLayout.hpp: the launcher pages' one box type and overlap test

/// Where everything on the page goes inside a `w` x `h` area (the page body under the section header).
/// Top row: the project picker, then Open command line, Copy all and Clear on the right. Then the one-line hint, the
/// output (all the height that is left) and the input row: the line, Run and Stop.
struct PageLayout {
    Rect project, terminal, copy, clear, hint, output, input, run, stop;
    std::vector<Rect> All() const { return {project, terminal, copy, clear, hint, output, input, run, stop}; }
};
/// The buttons' widths (FitButtonW in the launcher: the label plus padding, so text never spills).
struct PageButtons { float terminal = 160, copy = 84, clear = 64, run = 64, stop = 64; };
PageLayout LayoutPage(float w, float h, float frameH, float lineH, const PageButtons& b = PageButtons());

} // namespace okaylcli
