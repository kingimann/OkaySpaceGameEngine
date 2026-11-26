#pragma once
// okay CLI: the rules of a typed command line, shared by every place a person types okay commands into a window: the
// launcher's Command line page (launcher/CommandLine.hpp, "okay only" tabs) and the Command Line panel
// (shared/CommandLinePanel.hpp, in the editor). Moved here from launcher/CommandLine.cpp so both use one copy:
// splitting a line into words (no shell), Tab completion from okay's own command table (CommandTable.hpp), the history
// for Up and Down, and the list of output lines with their kinds. Standard library only (plus the engine's header-only
// StringUtil), so the launcher compiles it without the engine. Our own code.
#include <cstddef>
#include <string>
#include <vector>

namespace okaycli {

// ---- words ---------------------------------------------------------------------------------------------------------

/// Split a typed line into words. Spaces and tabs separate words; "double" or 'single' quotes keep spaces inside one
/// word; inside double quotes \" is a quote and \\ a backslash; elsewhere a backslash is kept as it is (Windows paths).
/// False with a plain reason for an unclosed quote.
bool SplitWords(const std::string& line, std::vector<std::string>& words, std::string& error);

/// A word that only names the program ("okay" or "okay.exe", any case): people type it out of habit; it is dropped.
bool IsOkayWord(const std::string& word);

/// What a typed line asks for.
enum class LineKind { Empty, Clear, Okay };
/// `clear` or `cls` (the window's own command), nothing, or a command for okay.
LineKind Classify(const std::string& line);

// ---- Tab completion ------------------------------------------------------------------------------------------------

struct Completion {
    std::string line;                  ///< the line after Tab (the same line when nothing matched)
    std::vector<std::string> choices;  ///< every word that fits, when more than one does (shown under the line)
};
/// Complete the last word of `line` from okay's command table: the command name, then its sub-command (`vc sa` ->
/// `vc save `), and command names after `help`. One match is completed with a space after it; several are completed
/// as far as they agree and listed in `choices`.
Completion Complete(const std::string& line);

// ---- history -------------------------------------------------------------------------------------------------------

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

// ---- output lines --------------------------------------------------------------------------------------------------

struct OutLine {
    enum Kind { Out, Err, Note };   ///< okay's normal words, its problems, and the window's own notes (the command, how it ended)
    std::string text;
    Kind kind = Out;
};

/// Drop terminal colour codes (ESC [ ... letter) and carriage returns from program output.
std::string CleanOutput(const std::string& text);

/// The lines a window shows, at most `maxLines` (the oldest go first), with the whole text kept joined for Copy.
class OutputLog {
public:
    explicit OutputLog(std::size_t maxLines = 5000) : maxLines_(maxLines) {}
    void Add(const std::string& text, OutLine::Kind kind);
    /// Add program output that may hold several lines or end in the middle of one: whole lines are added, the rest is
    /// kept in `partial` until the next call (or added at once with `flush`). Colour codes are dropped.
    void Feed(std::string& partial, const std::string& bytes, OutLine::Kind kind, bool flush);
    void Clear();
    const std::vector<OutLine>& Lines() const { return lines_; }
    /// Every line joined with newlines (what Copy all copies).
    const std::string& Text() const { return text_; }
    /// Goes up whenever the lines change (a view scrolls to the end when it moves).
    unsigned Revision() const { return revision_; }

private:
    void RebuildText();
    std::vector<OutLine> lines_;
    std::string text_;
    std::size_t maxLines_;
    unsigned revision_ = 0;
};

} // namespace okaycli
