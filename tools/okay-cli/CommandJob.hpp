#pragma once
// okay CLI inside another program: run okay commands in this process, with no console, for a window that shows them (the
// editor's Command Line panel, shared/CommandLinePanel.hpp). The commands are the same ones the okay program runs
// (okaycli::Main): nothing is copied, and only okay's own commands can run (no shell, no system()). Programs a command
// starts itself (git, the editor's --build-game, the player, ctest) are still started by Process.hpp, as they are from the
// okay program; their output comes back here instead of a terminal (Process.hpp AttachedOutput).
//
//   RunInProcess   one command, waiting for it (tests, short commands)
//   CommandJob     one command on a worker thread (okay::thr, CLAUDE.md rule 21a), read once a frame, with Stop
//
// Our own code, written from the plain idea of a background job with a locked hand-over list.
#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "CommandInput.hpp"   // OutLine

namespace okaycli {

struct InProcessResult {
    int exitCode = 1;                ///< 0 done, 1 it did not work, 2 bad arguments, 130 stopped
    bool stopped = false;            ///< the person pressed Stop
    std::vector<OutLine> lines;      ///< what the command said, in order (Out and Err; no notes)
};

/// Run one okay command line (the words after "okay") in `folder` (the project: given to okay as --project, unless the
/// words already name one), here and now. Nothing is printed and the program never exits; an exception from the command
/// is caught and said as a problem line. `stop` (may be null) is checked at every message and while a started program runs.
InProcessResult RunInProcess(const std::vector<std::string>& args, const std::string& folder, const std::atomic<bool>* stop = nullptr);

/// The words okay gets for `args` in `folder`: --project FOLDER first when a folder is given and the words do not already
/// start with one (--project, -C or --project=). Other relative paths in the words are read from this program's folder.
std::vector<std::string> ArgsInFolder(const std::vector<std::string>& args, const std::string& folder);

/// One okay command at a time on its own thread. Start returns at once; Poll hands over the lines said since the last call
/// (call it once a frame). Dropping a job that still runs stops its command and lets the thread end on its own.
class CommandJob {
public:
    CommandJob();
    ~CommandJob();
    CommandJob(const CommandJob&) = delete;
    CommandJob& operator=(const CommandJob&) = delete;

    /// False (nothing started) while a command still runs.
    bool Start(const std::vector<std::string>& args, const std::string& folder);
    /// Ask the running command to stop (it ends at its next message, and a program it started is stopped).
    void Stop();
    /// Started and not finished yet (lines may still be waiting for Poll).
    bool Running() const;
    /// Append the lines said since the last call. True once, on the call that sees the command finished: `exitCode` and
    /// `stopped` are set then.
    bool Poll(std::vector<OutLine>& lines, int& exitCode, bool& stopped);

    struct Shared;   // the hand-over list and the flags, owned together by the job and its thread (CommandJob.cpp)

private:
    std::shared_ptr<Shared> shared_;
    bool running_ = false;
};

} // namespace okaycli
