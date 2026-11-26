// okay CLI inside another program. See CommandJob.hpp. Our own code: the commands are okaycli::Main (Cli.cpp); started
// programs go through Process.hpp; the worker thread is okay::thr (shared/OkayThreads.hpp, CLAUDE.md rule 21a: no <thread>
// or <mutex> here).
#include "CommandJob.hpp"

#include "Cli.hpp"
#include "CliInternal.hpp"
#include "OkayThreads.hpp"   // shared/: okay::thr threads and mutexes
#include "Process.hpp"

#include <exception>
#include <functional>
#include <utility>

namespace okaycli {

namespace {

thread_local bool t_inside = false;   // RunWith is running a command on this thread (a plain bool: no thread header)

/// Cuts what a command says and what its programs write into whole lines, one stream for the normal words and one for the
/// problems, and hands each line on.
struct LineCutter {
    std::function<void(OutLine&&)> emit;
    std::string partOut, partErr;
    void Feed(const std::string& bytes, bool problem, bool flush) {
        std::string& part = problem ? partErr : partOut;
        part += CleanOutput(bytes);
        std::size_t at = 0;
        for (;;) {
            const std::size_t nl = part.find('\n', at);
            if (nl == std::string::npos) break;
            Line(part.substr(at, nl - at), problem);
            at = nl + 1;
        }
        part.erase(0, at);
        if (flush && !part.empty()) { Line(part, problem); part.clear(); }
    }
    void Flush() { Feed(std::string(), false, true); Feed(std::string(), true, true); }
    void Line(std::string text, bool problem) {
        OutLine l;
        l.text = std::move(text);
        l.kind = problem ? OutLine::Err : OutLine::Out;
        if (emit) emit(std::move(l));
    }
};

/// Run the command on this thread, saying every line through `cut`. Returns the exit code; `stopped` when Stop was pressed.
int RunWith(const std::vector<std::string>& args, const std::string& folder, const std::atomic<bool>* stop, LineCutter& cut,
            bool& stopped) {
    Io io;
    io.sink = [&cut](const std::string& text, bool problem) { cut.Feed(text, problem, false); };
    io.cancel = stop;
    AttachedOutput attached;
    attached.output = [&cut](const std::string& bytes, bool problem) { cut.Feed(bytes, problem, false); };
    attached.stop = [stop] { return stop && stop->load(); };
    const AttachedOutput* before = GetAttachedOutput();
    const bool wasInside = t_inside;
    SetAttachedOutput(&attached);
    t_inside = true;
    int rc = 1;
    try {
        rc = Main(ArgsInFolder(args, folder), io);
    } catch (const Cancelled&) {
        rc = 130;
    } catch (const std::exception& e) {
        cut.Feed(std::string("The command stopped with an error: ") + e.what() + "\n", true, false);
        rc = 1;
    } catch (...) {
        cut.Feed("The command stopped with an unexpected error.\n", true, false);
        rc = 1;
    }
    t_inside = wasInside;
    SetAttachedOutput(before);
    cut.Flush();
    stopped = stop && stop->load();
    if (stopped) rc = 130;
    return rc;
}

} // namespace

namespace detail {
bool RunningInsideHost() { return t_inside; }
} // namespace detail

std::vector<std::string> ArgsInFolder(const std::vector<std::string>& args, const std::string& folder) {
    if (folder.empty()) return args;
    if (!args.empty() && (args[0] == "--project" || args[0] == "-C" || args[0].rfind("--project=", 0) == 0)) return args;
    std::vector<std::string> a;
    a.reserve(args.size() + 2);
    a.push_back("--project");
    a.push_back(folder);
    a.insert(a.end(), args.begin(), args.end());
    return a;
}

InProcessResult RunInProcess(const std::vector<std::string>& args, const std::string& folder, const std::atomic<bool>* stop) {
    InProcessResult r;
    LineCutter cut;
    cut.emit = [&r](OutLine&& l) { r.lines.push_back(std::move(l)); };
    r.exitCode = RunWith(args, folder, stop, cut, r.stopped);
    return r;
}

// ---- CommandJob ----------------------------------------------------------------------------------------------------

struct CommandJob::Shared {
    okay::thr::Mutex* mu = okay::thr::CreateMutex();
    std::vector<OutLine> pending;     // said and not handed over yet (under mu)
    std::atomic<bool> stop{false};
    std::atomic<bool> done{false};
    int exitCode = 1;                 // written before `done` is set
    bool stopped = false;
    std::vector<std::string> args;
    std::string folder;
    ~Shared() { okay::thr::DestroyMutex(mu); }

    void Run() {
        LineCutter cut;
        cut.emit = [this](OutLine&& l) {
            okay::thr::LockMutex(mu);
            pending.push_back(std::move(l));
            okay::thr::UnlockMutex(mu);
        };
        bool st = false;
        const int rc = RunWith(args, folder, &stop, cut, st);
        okay::thr::LockMutex(mu);
        exitCode = rc;
        stopped = st;
        okay::thr::UnlockMutex(mu);
        done.store(true);
    }
};

namespace {
int JobEntry(void* p) {
    std::shared_ptr<CommandJob::Shared>* owned = static_cast<std::shared_ptr<CommandJob::Shared>*>(p);
    (*owned)->Run();
    delete owned;   // the thread's own reference: the job may be gone already
    return 0;
}
} // namespace

CommandJob::CommandJob() = default;

CommandJob::~CommandJob() {
    if (shared_ && !shared_->done.load()) shared_->stop.store(true);   // the thread ends on its own (it owns a reference)
}

bool CommandJob::Start(const std::vector<std::string>& args, const std::string& folder) {
    if (running_) return false;
    shared_ = std::make_shared<Shared>();
    shared_->args = args;
    shared_->folder = folder;
    running_ = true;
    auto* owned = new std::shared_ptr<Shared>(shared_);
    if (okay::thr::Thread* t = okay::thr::CreateThread(&JobEntry, "okay-command", owned)) {
        okay::thr::DetachThread(t);
    } else {
        JobEntry(owned);   // the system gave no thread: run it here (Poll hands the result over next)
    }
    return true;
}

void CommandJob::Stop() {
    if (shared_) shared_->stop.store(true);
}

bool CommandJob::Running() const { return running_; }

bool CommandJob::Poll(std::vector<OutLine>& lines, int& exitCode, bool& stopped) {
    if (!running_ || !shared_) return false;
    const bool finished = shared_->done.load();   // read before the lines, so no line said before the end is left behind
    okay::thr::LockMutex(shared_->mu);
    for (OutLine& l : shared_->pending) lines.push_back(std::move(l));
    shared_->pending.clear();
    if (finished) { exitCode = shared_->exitCode; stopped = shared_->stopped; }
    okay::thr::UnlockMutex(shared_->mu);
    if (!finished) return false;
    running_ = false;
    return true;
}

} // namespace okaycli
