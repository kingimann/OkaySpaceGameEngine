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

/// Run `argv` (argv[0] goes through ResolveProgram: started by its full path) in `cwd` ("" = the current folder) and wait.
/// `capture` true: stdout and stderr are collected into the result and standard input is empty. False: the program
/// shares this terminal (it can print and ask questions, as git does for a password).
RunResult Run(const std::vector<std::string>& argv, const std::string& cwd, bool capture);

/// Start `argv` and do not wait for it (the editor). True when it started.
bool StartDetached(const std::vector<std::string>& argv, const std::string& cwd);

/// How StartDetached starts a program that gets a window of its own.
struct DetachOptions {
    std::string pathFirst;     ///< "" = PATH as it is; else this folder is put first on PATH, for the new program only
    bool newConsole = false;   ///< Windows: the program gets its own console window (cmd.exe); POSIX: no difference
    std::string title;         ///< Windows: the new console window's title
};
bool StartDetached(const std::vector<std::string>& argv, const std::string& cwd, const DetachOptions& options);

/// A program running in the background whose output is read while it runs (the launcher's Command line page).
/// Start never waits; Poll reads what is ready without waiting, so it can be called once a frame. stdin is empty.
/// Windows: two pipes read with PeekNamedPipe, and a job object so Stop ends the program and everything it started.
/// POSIX: two non-blocking pipes, and the program leads its own process group so Stop ends the whole group.
/// No thread and no <mutex> (CLAUDE.md rule 21a). Not copyable.
class Child {
public:
    Child();
    ~Child();   ///< a program still running is stopped
    Child(const Child&) = delete;
    Child& operator=(const Child&) = delete;

    /// Start `argv` (argv[0] goes through ResolveProgram; the rest are passed one by one, never through a shell) in
    /// `cwd` ("" = the current folder). False with a plain reason when it could not start.
    bool Start(const std::vector<std::string>& argv, const std::string& cwd, std::string& error);
    /// Append what the program wrote since the last call to `out` and `err`. True while it runs or output is left.
    bool Poll(std::string& out, std::string& err);
    /// It was started and Poll has not yet seen it end.
    bool Running() const;
    /// End it (and what it started) now. Poll then reports it finished.
    void Stop();
    /// It ended because Stop was called.
    bool Stopped() const;
    /// Its exit code once it ended (-1 before).
    int ExitCode() const;

private:
    struct Impl;
    Impl* impl_;
};

/// The full path of a program found on PATH, or "" (Windows also tries ".exe"). Only absolute PATH folders are
/// searched, so a program in the current folder is never picked up by its bare name.
std::string FindOnPath(const std::string& program);
/// The full path that Run and StartDetached start for `program`: a path given with a folder (made absolute), else
/// FindOnPath. "" for a .cmd or .bat file (Windows would run those through cmd.exe) or when nothing is found.
std::string ResolveProgram(const std::string& program);

/// The full path of this running program ("" when the system does not say).
std::string SelfPath();

} // namespace okaycli
