#pragma once
#include <functional>
#include <string>
#include <vector>

namespace okay {

/// A text console: commands plus the Cvar registry, driven by lines of text. The UI
/// (an in-game overlay, the editor Console panel, a terminal) reads a line from the
/// player and calls Execute(); this class does the rest and returns the text to show.
///
///     Console::Register("spawn", [](const std::vector<std::string>& a) { ...; return std::string("ok"); },
///                       "spawn a thing", "spawn <name> [count]");
///     Console::Execute("spawn crate 3");
///     Console::Execute("r.shadows 0");      // set a cvar
///     Console::Execute("r.shadows");        // print it
///
/// Built-in commands: help [name], list [prefix], set <name> <value>, get <name>,
/// toggle <bool-cvar>, reset <name>, history, echo <text>, exec <file> (run a script file's lines).
/// A line is split into words on spaces; "double quotes" keep spaces in one word. Blank
/// lines and lines starting with # or // do nothing. Main thread only. A command may call Execute
/// itself (up to 32 levels deep; deeper gives an "error:" result instead of overflowing the stack).
class Console {
public:
    /// A command receives its words (without the command name) and returns text to show
    /// (empty for nothing). Return a string starting with "error:" to report a failure.
    using Command = std::function<std::string(const std::vector<std::string>& args)>;

    struct Result {
        bool ok = true;
        std::string output;      ///< what to show the player (may be several lines)
    };

    /// Add (or replace) a command. Names are case-sensitive and have no spaces.
    static void Register(const std::string& name, Command fn, const std::string& help = "",
                         const std::string& usage = "");
    static bool Unregister(const std::string& name);
    static bool HasCommand(const std::string& name);

    /// Run one line. The line is added to the history and its output to the output log.
    static Result Execute(const std::string& line);
    /// Run each line of a script (e.g. an autoexec file's contents). Stops at nothing: every line runs.
    static std::vector<Result> ExecuteScript(const std::string& text);

    /// Split a line into words (quotes group, a backslash escapes a quote inside quotes).
    static std::vector<std::string> Tokenize(const std::string& line);

    // ---- History (what the player typed, oldest first) ----
    static const std::vector<std::string>& History();
    static void SetHistoryLimit(std::size_t n);   ///< default 100
    static void ClearHistory();

    // ---- Output log (every line printed, for the UI to draw) ----
    static void Print(const std::string& text);
    static const std::vector<std::string>& Lines();
    static void ClearLines();

    /// Names (commands and cvars) starting with `prefix`, sorted, for tab completion.
    static std::vector<std::string> Complete(const std::string& prefix);

    /// Remove every command (built-ins are re-added on next use), the history and the output log.
    static void Reset();
};

} // namespace okay
