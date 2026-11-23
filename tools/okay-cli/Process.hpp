#pragma once
// okay CLI: the ONE place that starts another program (git, the editor, the player, python, ctest).
// Same idea as the curl seam in okay/Platform/NativeFetch.hpp: one small file owns process start-up, and every other
// part of the CLI asks it. Windows uses CreateProcessW; Linux and macOS use fork + execvp. No shell is ever used, so a
// file name with spaces or quotes is passed as it is. Written from the Win32 CreateProcess documentation and POSIX fork/exec/poll (the
// Windows argument quoting is shared/AppRestart.hpp QuoteWindowsArg). No <thread> or
// <mutex> (CLAUDE.md rule 21a): the two output pipes are read with poll() on POSIX, and on Windows stderr goes to a
// temporary file while stdout is read from a pipe.
#include <string>
#include <vector>

namespace okaycli {

struct RunResult {
    bool started = false;   ///< false: the program could not be started (not installed, not on PATH)
    int exitCode = -1;      ///< the program's exit code (when started)
    std::string out;        ///< what it wrote to stdout (only when captured)
    std::string err;        ///< what it wrote to stderr (only when captured)
};

/// Run `argv` (argv[0] is looked up on PATH when it has no folder in it) in `cwd` ("" = the current folder) and wait.
/// `capture` true: stdout and stderr are collected into the result and standard input is empty. False: the program
/// shares this terminal (it can print and ask questions, as git does for a password).
RunResult Run(const std::vector<std::string>& argv, const std::string& cwd, bool capture);

/// Start `argv` and do not wait for it (the editor). True when it started.
bool StartDetached(const std::vector<std::string>& argv, const std::string& cwd);

/// The full path of a program found on PATH, or "" (Windows also tries ".exe").
std::string FindOnPath(const std::string& program);

/// The full path of this running program ("" when the system does not say).
std::string SelfPath();

} // namespace okaycli
