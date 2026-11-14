#pragma once
// The decision logic of a program (launcher or editor) replacing ITSELF, kept free of windows, threads and the network so test
// binaries can link it (tests/test_self_update.cpp, tests/test_app_restart.cpp). Docs: docs/updater.md.
//
// What can go wrong when a program swaps its own file: the rename fails (antivirus lock, read-only folder), the swapped-in file does not
// start, or it starts but is not the version that was published. The pieces here:
//   * a small state file next to the program (okay_selfupdate.state) that remembers "swapped from A to B, attempt N" or "failed: reason";
//   * a self-check the NEW program answers before it is started for real (`--okay-selfcheck <version>`: exit 0 only when it is that version);
//   * the rule that decides, at the next start, whether the update was confirmed, did not apply, or failed earlier, and what to tell the person;
//   * a retry rule so an update that keeps failing is not attempted again and again by the automatic check (a person can always retry);
//   * a rollback that puts the previous program file back.
// Nothing here touches a path other than the program file, its ".old" backup, its ".failed" leftover and the state file.
#include "ProjectVersion.hpp"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

namespace okay::selfupdate {
namespace fs = std::filesystem;

inline constexpr const char* kSelfCheckFlag = "--okay-selfcheck";
inline constexpr int kSelfCheckOk = 0;
inline constexpr int kSelfCheckBadArgs = 2;
inline constexpr int kSelfCheckWrongVersion = 3;
/// The automatic check gives up on a target version after this many failed attempts (a person clicking "Retry" always can).
inline constexpr int kMaxAutoAttempts = 2;
inline constexpr std::size_t kMaxStateBytes = 4096;
inline constexpr std::size_t kMaxReasonChars = 400;

enum class Phase { None, Swapped, Failed };

struct State {
    Phase phase = Phase::None;
    std::string from;        ///< version that was running when the update started
    std::string to;          ///< version that was being installed
    std::string reason;      ///< plain words, set when phase == Failed
    int attempts = 0;        ///< attempts at installing `to` (counted when the swap is done or fails)
    int notified = 0;        ///< 1 once the person has been shown the failure dialog
};

inline const char* PhaseName(Phase p) { return p == Phase::Swapped ? "swapped" : p == Phase::Failed ? "failed" : "none"; }

inline fs::path StatePath(const fs::path& programDir) { return programDir / "okay_selfupdate.state"; }
inline fs::path BackupPath(const fs::path& program) { fs::path p = program; p += ".old"; return p; }
inline fs::path FailedPath(const fs::path& program) { fs::path p = program; p += ".failed"; return p; }

/// One line of reason text: no control characters, bounded.
inline std::string CleanText(const std::string& s, std::size_t cap = kMaxReasonChars) {
    std::string o;
    for (unsigned char c : s) {
        if (o.size() >= cap) break;
        o += (c < 32 || c == 127) ? ' ' : (char)c;
    }
    return o;
}

inline bool PlainVersion(const std::string& v) { return !v.empty() && v.size() <= 24 && okayver::NormalizeVersion(v) == v; }

inline std::string Serialize(const State& s) {
    std::ostringstream o;
    o << "phase=" << PhaseName(s.phase) << "\n"
      << "from=" << s.from << "\n"
      << "to=" << s.to << "\n"
      << "attempts=" << s.attempts << "\n"
      << "notified=" << s.notified << "\n"
      << "reason=" << CleanText(s.reason) << "\n";
    return o.str();
}

/// Reads the state text. Anything that is not exactly what Serialize writes (garbage, huge, unknown phase, bad versions or counts) is
/// "no state": a damaged file can never cause a rollback or an action, only a fresh start.
inline State Parse(const std::string& text) {
    State s;
    if (text.empty() || text.size() > kMaxStateBytes) return s;
    std::istringstream in(text);
    std::string line, phase;
    bool havePhase = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "phase") { phase = v; havePhase = true; }
        else if (k == "from") s.from = v;
        else if (k == "to") s.to = v;
        else if (k == "reason") s.reason = CleanText(v);
        else if (k == "attempts" || k == "notified") {
            if (v.empty() || v.size() > 3 || v.find_first_not_of("0123456789") != std::string::npos) return State{};
            (k == "attempts" ? s.attempts : s.notified) = std::atoi(v.c_str());
        }
    }
    if (!havePhase) return State{};
    if (phase == "swapped") s.phase = Phase::Swapped;
    else if (phase == "failed") s.phase = Phase::Failed;
    else return State{};
    if (!PlainVersion(s.to) || (!s.from.empty() && !PlainVersion(s.from))) return State{};
    return s;
}

inline State Read(const fs::path& file) {
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) return State{};
    const auto sz = fs::file_size(file, ec);
    if (ec || sz > kMaxStateBytes) return State{};
    std::ifstream in(file, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return Parse(ss.str());
}

/// Writes the state through a temp file and a rename, so a crash leaves the old state or the new one, never half of one.
inline bool Write(const fs::path& file, const State& s, std::string& error) {
    std::error_code ec;
    fs::path tmp = file; tmp += ".tmp";
    {
        std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
        if (!o) { error = "couldn't write " + file.filename().string(); return false; }
        o << Serialize(s);
        o.flush();
        if (!o) { fs::remove(tmp, ec); error = "couldn't write " + file.filename().string(); return false; }
    }
    fs::rename(tmp, file, ec);
    if (ec) { fs::remove(tmp, ec); error = "couldn't save " + file.filename().string() + ": " + ec.message(); return false; }
    return true;
}

inline void Clear(const fs::path& file) {
    std::error_code ec;
    fs::remove(file, ec);
    fs::path tmp = file; tmp += ".tmp";
    fs::remove(tmp, ec);
}

// ---- the new program's answer before it is started for real ----------------------------------------------------------------------------

/// `program --okay-selfcheck <version>`: -1 when the flag is not there (a normal start); 0 when this program is exactly <version>;
/// 3 when it is another version; 2 when the version argument is missing or not a version. Call it first in main, before any window.
inline int SelfCheck(int argc, char** argv, const std::string& runningVersion) {
    for (int i = 1; i < argc; ++i) {
        if (!argv[i] || std::strcmp(argv[i], kSelfCheckFlag) != 0) continue;
        if (i + 1 >= argc || !argv[i + 1]) return kSelfCheckBadArgs;
        const std::string want = argv[i + 1];
        if (!PlainVersion(want)) return kSelfCheckBadArgs;
        return okayver::NormalizeVersion(runningVersion) == want ? kSelfCheckOk : kSelfCheckWrongVersion;
    }
    return -1;
}

/// Why a self-check result is not good, in plain words ("" when it is).
inline std::string SelfCheckProblem(int exitCode, const std::string& expected) {
    if (exitCode == kSelfCheckOk) return {};
    if (exitCode == -2) return "the new file did not answer its start-up check in time (it was stopped)";
    if (exitCode < 0) return "the new file could not be started (it may be damaged, blocked by security software, or not allowed to run)";
    if (exitCode == kSelfCheckWrongVersion) return "the new file is not v" + expected + " (it reports another version)";
    return "the new file failed its start-up check (code " + std::to_string(exitCode) + ")";
}

// ---- the decision at the next start -----------------------------------------------------------------------------------------------------

enum class StartupKind {
    Nothing,        ///< no update was in progress
    Confirmed,      ///< the swapped-in program is running: the update is complete
    NotApplied,     ///< an update was swapped in but the program that started is not the new version
    FailedBefore,   ///< an earlier attempt failed (and was rolled back): tell the person
};

struct Startup {
    StartupKind kind = StartupKind::Nothing;
    std::string message;         ///< for the person: what happened, why, what to do ("" when nothing to say)
    std::string logLine;         ///< for okay_update.log
    State next;                  ///< the state to store (when !clearState)
    bool clearState = false;     ///< delete the state file
    bool removeBackup = false;   ///< the old program file is no longer needed
    bool resetVersionFile = false;   ///< VERSION.txt must say `running`, not the version that did not apply
    bool showDialog = false;     ///< open the update dialog (first time only; later starts show the sidebar note)
};

inline const char* kManualAdvice =
    "Your current version still works. Close any security software that may be blocking the file, then use Retry in the update dialog. "
    "If it keeps failing, download the full installer from the release page.";

inline Startup EvaluateStartup(const State& st, const std::string& running) {
    Startup r;
    if (st.phase == Phase::None) return r;
    const bool reached = okayver::CompareVersions(running, st.to) >= 0;
    if (reached) {   // the new version (or an even newer one) is what runs now: whatever happened on the way, it is done
        r.kind = StartupKind::Confirmed;
        r.clearState = true;
        r.removeBackup = true;
        r.logLine = st.phase == Phase::Swapped ? "update to v" + st.to + " confirmed: v" + running + " is running"
                                               : "v" + running + " is running now; the earlier failed update to v" + st.to + " no longer matters";
        return r;
    }
    r.next = st;
    if (st.phase == Phase::Swapped) {
        r.kind = StartupKind::NotApplied;
        r.next.phase = Phase::Failed;
        r.next.reason = "After the restart the launcher is still v" + running + ", not v" + st.to + ".";
        r.next.notified = 1;
        r.resetVersionFile = true;
        r.showDialog = true;
        r.message = "The update to v" + st.to + " did not apply. " + r.next.reason + " " + kManualAdvice;
        r.logLine = "update to v" + st.to + " did not apply: running v" + running + " (attempt " + std::to_string(st.attempts) + ")";
        return r;
    }
    r.kind = StartupKind::FailedBefore;
    r.resetVersionFile = true;
    r.showDialog = st.notified == 0;
    r.next.notified = 1;
    r.message = "The update to v" + st.to + " was not installed: " + (st.reason.empty() ? std::string("unknown reason") : st.reason) +
                (st.reason.empty() || st.reason.back() == '.' ? " " : ". ") + kManualAdvice;
    r.logLine = "earlier update to v" + st.to + " failed (attempt " + std::to_string(st.attempts) + "): " + st.reason;
    return r;
}

// ---- the retry rule ---------------------------------------------------------------------------------------------------------------------

/// May the AUTOMATIC check (at launch, not a click) try to install `target`? Not when it already failed kMaxAutoAttempts times: that is
/// what stops a bad release from being downloaded and rolled back on every start. `why` says it in words. A person's click is never asked.
inline bool AutoRetryAllowed(const State& st, const std::string& target, std::string& why) {
    why.clear();
    if (st.phase == Phase::Failed && st.to == target && st.attempts >= kMaxAutoAttempts) {
        why = "The update to v" + target + " failed " + std::to_string(st.attempts) + " times, so it is not tried again automatically. Use Retry to try once more.";
        return false;
    }
    return true;
}

/// The state to store when an attempt at `to` ends (swapped = the new file is in place, restart pending; else failed with `reason`).
inline State AfterAttempt(const State& previous, const std::string& from, const std::string& to, bool swapped, const std::string& reason) {
    State s;
    s.from = from;
    s.to = to;
    s.attempts = (previous.phase != Phase::None && previous.to == to ? previous.attempts : 0) + 1;
    s.notified = 0;
    if (swapped) s.phase = Phase::Swapped;
    else { s.phase = Phase::Failed; s.reason = CleanText(reason); }
    return s;
}

// ---- rollback ----------------------------------------------------------------------------------------------------------------------------

struct RollbackResult {
    bool ok = false;
    std::string message;
};

/// Put the previous program file (`backup`) back in place of `program`. The file that failed is kept as program.failed, so nothing is lost.
/// On any failure the program file is left as it was and the message says where the previous one is.
inline RollbackResult RollBack(const fs::path& program, const fs::path& backup) {
    std::error_code ec;
    if (backup.empty() || !fs::is_regular_file(backup, ec) || ec)
        return {false, "there is no previous file to go back to (" + backup.filename().string() + " is missing)"};
    std::error_code sz;
    if (fs::file_size(backup, sz) == 0 || sz)
        return {false, "the previous file (" + backup.filename().string() + ") is empty, so it was not put back"};
    const fs::path failed = FailedPath(program);
    fs::remove(failed, ec);
    bool moved = false;
    if (fs::exists(program, ec)) {
        ec.clear();
        fs::rename(program, failed, ec);
        if (ec) return {false, "couldn't move the new file aside (" + ec.message() + "); the previous file is still at " + backup.string()};
        moved = true;
    }
    ec.clear();
    fs::rename(backup, program, ec);
    if (ec) {
        std::error_code e2;
        if (moved) fs::rename(failed, program, e2);
        return {false, "couldn't put the previous file back (" + ec.message() + "); it is still at " + backup.string()};
    }
    return {true, "restored the previous file"};
}

}  // namespace okay::selfupdate
