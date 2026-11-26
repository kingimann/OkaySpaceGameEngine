#pragma once
// okay CLI: the ONE place that starts another program (git, the editor, the player, python, ctest).
// Same idea as the curl seam in okay/Platform/NativeFetch.hpp: one small file owns process start-up, and every other
// part of the CLI asks it. Windows uses CreateProcessW; Linux and macOS use fork + execvp. No shell is ever used, so a
// file name with spaces or quotes is passed as it is. Written from the Win32 CreateProcess documentation and POSIX fork/exec/poll (the
// Windows argument quoting is shared/AppRestart.hpp QuoteWindowsArg). No <thread> or
// <mutex> (CLAUDE.md rule 21a): the two output pipes are read with poll() on POSIX, and on Windows stderr goes to a
// temporary file while stdout is read from a pipe.
#include <functional>
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

/// A program run while okay itself runs inside a window (the editor's Command Line panel, CommandJob.hpp) has no terminal to
/// share: while an AttachedOutput is set on this thread, Run(..., capture = false) starts the program as a Child instead (no
/// console window, empty standard input), hands what it writes to `output` as it comes (`problem` true for stderr), and
/// stops it (and what it started) when `stop` says so. Captured runs (capture = true) are not changed. Null: off (the default).
struct AttachedOutput {
    std::function<void(const std::string& bytes, bool problem)> output;
    std::function<bool()> stop;
};
void SetAttachedOutput(const AttachedOutput* attached);
const AttachedOutput* GetAttachedOutput();

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

/// A program running on a pseudo terminal: what it writes, colours and cursor moves included, is read as one byte
/// stream for a terminal emulator (launcher/Terminal.hpp), and keys go to it as the bytes a terminal sends. The
/// launcher's Command line page runs the person's shell this way.
/// Windows: the pseudo console (ConPTY: CreatePseudoConsole with two pipes, STARTUPINFOEXW with
/// PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, ResizePseudoConsole, ClosePseudoConsole; Windows 10 1809 or newer, looked up in
/// kernel32 at run time) and a job object, so Stop and closing the launcher end the shell and what it started.
/// POSIX: posix_openpt / grantpt / unlockpt, the program in its own session with the terminal as its controlling one,
/// the size through TIOCSWINSZ. Read never waits (call it once a frame). No thread and no <mutex> (CLAUDE.md rule 21a).
class Pty {
public:
    struct Options {
        std::vector<std::string> argv;   ///< argv[0] is a full path (never looked up in the current folder)
        std::string cwd;                 ///< "" = the current folder
        std::string pathFirst;           ///< put first on PATH for the program ("" = PATH as it is)
        int cols = 80, rows = 24;
    };
    Pty();
    ~Pty();   ///< a program still running is stopped
    Pty(const Pty&) = delete;
    Pty& operator=(const Pty&) = delete;

    /// False with a plain reason when it could not start.
    bool Start(const Options& options, std::string& error);
    /// Append what the program wrote since the last call. True while it runs.
    bool Read(std::string& out);
    /// Send bytes to the program (keys, typed text, a paste, a terminal's replies). False when it is not running.
    bool Write(const std::string& bytes);
    /// The terminal's new size in cells.
    void Resize(int cols, int rows);
    /// End it and everything it started.
    void Stop();
    bool Running() const;
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
