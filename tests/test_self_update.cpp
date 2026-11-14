// The launcher / editor self-update decisions (shared/SelfUpdate.hpp, launcher/UpdaterCore.hpp) with fake files in temp folders: the state file
// (hostile contents), the start-up verdict (confirmed / did not apply / failed before), the retry rule (no loop), the self-check, rollback,
// the all-or-nothing swap of several files, and hostile versions.json inputs (a plan is never made for a file without a SHA-256). The real
// process flow (helper waits, self-checks the new file, rolls back, relaunches) is in test_app_restart. No window, no network, nothing outside
// temp folders (never the source tree).
#include "test_framework.hpp"
#include "SelfUpdate.hpp"
#include "UpdaterCore.hpp"
#include "OfficialGame.hpp"

#include <chrono>
#include <fstream>
#include <map>
#include <sstream>

namespace fs = std::filesystem;
namespace su = okay::selfupdate;
using namespace okayversions;

namespace {
struct TempDir {
    fs::path path;
    TempDir() {
        static int n = 0;
        path = fs::temp_directory_path() / ("okay_selfupd_" + std::to_string(++n) + "_" + std::to_string((long long)std::chrono::steady_clock::now().time_since_epoch().count()));
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::create_directories(path, ec);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(path, ec); }
};
void Put(const fs::path& p, const std::string& t) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream(p, std::ios::binary | std::ios::trunc) << t;
}
std::string Get(const fs::path& p) { std::ifstream in(p, std::ios::binary); std::ostringstream ss; ss << in.rdbuf(); return ss.str(); }
bool Has(const std::string& h, const std::string& n) { return h.find(n) != std::string::npos; }
bool Exists(const fs::path& p) { std::error_code ec; return fs::exists(p, ec); }
std::string Sha(const std::string& s) { return okay::account::detail::Sha256Hex(s); }
}  // namespace

int main() {
    RUN_SUITE("self-update state file");
    {
        su::State s; s.phase = su::Phase::Failed; s.from = "2.855.0"; s.to = "2.856.0"; s.attempts = 2; s.notified = 1; s.reason = "rename failed: access denied";
        const su::State back = su::Parse(su::Serialize(s));
        CHECK(back.phase == su::Phase::Failed && back.from == "2.855.0" && back.to == "2.856.0" && back.attempts == 2 && back.notified == 1 && back.reason == s.reason);
        // Hostile or damaged contents are "no state", never an action.
        const char* bad[] = {"", "garbage", "phase=swapped\n", "phase=swapped\nto=notaversion\n", "phase=bogus\nto=1.2.3\n", "phase=failed\nto=1.2.3\nattempts=99999999999\n",
                             "phase=failed\nto=1.2.3\nattempts=-1\n", "phase=failed\nto=../../etc\n", "phase=failed\nto=1.2.3\nfrom=zzz\n"};
        for (const char* b : bad) CHECK(su::Parse(b).phase == su::Phase::None);
        CHECK(su::Parse(std::string(su::kMaxStateBytes + 1, 'a')).phase == su::Phase::None);
        CHECK(su::Parse(std::string("phase=swapped\nto=1.2.3\n", 24) + std::string("\0\xff\xfe junk", 8)).phase == su::Phase::Swapped);   // junk lines are skipped, not trusted
        su::State multi; multi.phase = su::Phase::Failed; multi.to = "1.2.3"; multi.reason = "line one\nline two\r\x01" + std::string(2000, 'x');
        const std::string text = su::Serialize(multi);
        CHECK(std::count(text.begin(), text.end(), '\n') == 6 && text.size() < su::kMaxStateBytes);   // a reason can not add lines or grow the file

        TempDir t;
        const fs::path f = su::StatePath(t.path);
        CHECK(su::Read(f).phase == su::Phase::None);                      // no file
        std::string err;
        CHECK(su::Write(f, s, err) && su::Read(f).to == "2.856.0" && !Exists(fs::path(f.string() + ".tmp")));
        Put(f, std::string(su::kMaxStateBytes + 10, 'z'));
        CHECK(su::Read(f).phase == su::Phase::None);                      // too big to be ours
        su::Clear(f);
        CHECK(!Exists(f));
        CHECK(!su::Write(t.path / "no" / "such" / "dir" / "x.state", s, err) && !err.empty());   // a failed write says so
    }

    RUN_SUITE("self-update: the start-up verdict");
    {
        su::State sw; sw.phase = su::Phase::Swapped; sw.from = "2.855.0"; sw.to = "2.856.0"; sw.attempts = 1;
        su::Startup v = su::EvaluateStartup(su::State{}, "2.855.0");
        CHECK(v.kind == su::StartupKind::Nothing && v.message.empty() && !v.clearState);
        // The new launcher is running: confirmed, state and backup go.
        v = su::EvaluateStartup(sw, "2.856.0");
        CHECK(v.kind == su::StartupKind::Confirmed && v.clearState && v.removeBackup && v.message.empty() && Has(v.logLine, "confirmed"));
        v = su::EvaluateStartup(sw, "2.900.0");   // an even newer one is fine too
        CHECK(v.kind == su::StartupKind::Confirmed && v.clearState);
        // The swap says done but the old version came up: the person is told, with the reason and what to do; VERSION.txt is reset.
        v = su::EvaluateStartup(sw, "2.855.0");
        CHECK(v.kind == su::StartupKind::NotApplied && !v.clearState && v.showDialog && v.resetVersionFile);
        CHECK(Has(v.message, "v2.856.0 did not apply") && Has(v.message, "still v2.855.0") && Has(v.message, "Retry") && Has(v.message, "installer"));
        CHECK(v.next.phase == su::Phase::Failed && v.next.attempts == 1 && Has(v.next.reason, "still v2.855.0"));
        // A rolled-back update (the helper wrote Failed): shown once as a dialog, afterwards only the sidebar note.
        su::State fl; fl.phase = su::Phase::Failed; fl.from = "2.855.0"; fl.to = "2.856.0"; fl.attempts = 1; fl.reason = "The new version did not pass its start-up check.";
        v = su::EvaluateStartup(fl, "2.855.0");
        CHECK(v.kind == su::StartupKind::FailedBefore && v.showDialog && v.next.notified == 1 && v.resetVersionFile && Has(v.message, "start-up check") && Has(v.message, "Retry"));
        fl.notified = 1;
        v = su::EvaluateStartup(fl, "2.855.0");
        CHECK(v.kind == su::StartupKind::FailedBefore && !v.showDialog && !v.message.empty());
        // Running the target (or newer) clears an old failure.
        v = su::EvaluateStartup(fl, "2.856.0");
        CHECK(v.kind == su::StartupKind::Confirmed && v.clearState);
        // A dev build ("dev" is older than every release) never crashes the rule.
        CHECK(su::EvaluateStartup(sw, "dev").kind == su::StartupKind::NotApplied);
    }

    RUN_SUITE("self-update: retry rule (no loop)");
    {
        su::State none;
        su::State a1 = su::AfterAttempt(none, "2.855.0", "2.856.0", false, "rename failed");
        CHECK(a1.phase == su::Phase::Failed && a1.attempts == 1 && a1.reason == "rename failed");
        su::State a2 = su::AfterAttempt(a1, "2.855.0", "2.856.0", false, "again");
        CHECK(a2.attempts == 2);
        std::string why;
        CHECK(su::AutoRetryAllowed(none, "2.856.0", why) && why.empty());
        CHECK(su::AutoRetryAllowed(a1, "2.856.0", why));                          // one failure: the next start may try again
        CHECK(!su::AutoRetryAllowed(a2, "2.856.0", why) && Has(why, "failed 2 times") && Has(why, "Retry"));   // two: the automatic check stops
        CHECK(su::AutoRetryAllowed(a2, "2.857.0", why));                          // a newer release is a new chance
        const su::State a3 = su::AfterAttempt(a2, "2.855.0", "2.857.0", false, "x");
        CHECK(a3.attempts == 1);                                                  // the count is per target
        CHECK(su::AfterAttempt(a2, "2.855.0", "2.856.0", true, "").phase == su::Phase::Swapped);   // a person's Retry that worked
        su::State sw = su::AfterAttempt(none, "2.855.0", "2.856.0", true, "ignored");
        CHECK(sw.phase == su::Phase::Swapped && sw.reason.empty() && su::AutoRetryAllowed(sw, "2.856.0", why));
    }

    RUN_SUITE("self-update: self-check");
    {
        auto run = [](std::vector<std::string> args, const char* version) {
            std::vector<char*> av;
            for (auto& s : args) av.push_back(s.data());
            return su::SelfCheck((int)av.size(), av.data(), version);
        };
        CHECK(run({"p"}, "2.856.0") == -1);                                       // a normal start
        CHECK(run({"p", "--launcher", "7"}, "2.856.0") == -1);
        CHECK(run({"p", "--okay-selfcheck", "2.856.0"}, "2.856.0") == su::kSelfCheckOk);
        CHECK(run({"p", "--okay-selfcheck", "2.856.0"}, "2.855.0") == su::kSelfCheckWrongVersion);
        CHECK(run({"p", "--okay-selfcheck", "2.856.0"}, "dev") == su::kSelfCheckWrongVersion);
        CHECK(run({"p", "--okay-selfcheck"}, "2.856.0") == su::kSelfCheckBadArgs);
        CHECK(run({"p", "--okay-selfcheck", "latest"}, "2.856.0") == su::kSelfCheckBadArgs);
        CHECK(run({"p", "--okay-selfcheck", "2.856.0 && rm"}, "2.856.0") == su::kSelfCheckBadArgs);
        CHECK(su::SelfCheckProblem(0, "2.856.0").empty());
        CHECK(Has(su::SelfCheckProblem(-1, "2.856.0"), "could not be started") && Has(su::SelfCheckProblem(3, "2.856.0"), "not v2.856.0") && Has(su::SelfCheckProblem(2, "2.856.0"), "code 2"));
    }

    RUN_SUITE("self-update: rollback");
    {
        TempDir t;
        const fs::path exe = t.path / "OkaySpace.exe", old = su::BackupPath(exe);
        Put(exe, "NEW-BAD"); Put(old, "OLD-GOOD");
        su::RollbackResult r = su::RollBack(exe, old);
        CHECK(r.ok && Get(exe) == "OLD-GOOD" && !Exists(old) && Get(su::FailedPath(exe)) == "NEW-BAD");   // the failed file is kept, not destroyed
        // No backup: nothing changes, and it says why.
        TempDir t2;
        const fs::path e2 = t2.path / "OkaySpace.exe";
        Put(e2, "NEW");
        r = su::RollBack(e2, su::BackupPath(e2));
        CHECK(!r.ok && Has(r.message, "no previous file") && Get(e2) == "NEW" && !Exists(su::FailedPath(e2)));
        Put(su::BackupPath(e2), "");
        r = su::RollBack(e2, su::BackupPath(e2));
        CHECK(!r.ok && Has(r.message, "empty") && Get(e2) == "NEW");
        // The program file is missing (a scanner removed it): the backup is simply put back.
        TempDir t3;
        const fs::path e3 = t3.path / "OkaySpace.exe";
        Put(su::BackupPath(e3), "OLD");
        CHECK(su::RollBack(e3, su::BackupPath(e3)).ok && Get(e3) == "OLD");
        // A stale .failed from an earlier rollback does not block a new one.
        TempDir t4;
        const fs::path e4 = t4.path / "OkaySpace.exe";
        Put(e4, "NEW2"); Put(su::BackupPath(e4), "OLD2"); Put(su::FailedPath(e4), "STALE");
        CHECK(su::RollBack(e4, su::BackupPath(e4)).ok && Get(e4) == "OLD2" && Get(su::FailedPath(e4)) == "NEW2");
    }

    RUN_SUITE("updater: all-or-nothing swap of several files");
    {
        auto setup = [](const fs::path& dir) {
            Put(dir / "OkaySpace.exe", "old launcher");
            Put(dir / "OkayEngine.exe", "old engine");
            Put(dir / "OkaySpacePlayer.exe", "old player");
            for (const char* n : {"OkaySpace.exe", "OkayEngine.exe", "OkaySpacePlayer.exe"}) Put(dir / (std::string(n) + ".new"), std::string("new ") + n);
        };
        std::vector<okayupdater::CommitItem> items;
        TempDir t;
        auto fresh = [&] { std::error_code ec; fs::remove_all(t.path, ec); fs::create_directories(t.path, ec); };
        auto mk = [&] {
            items = {{"OkaySpace.exe", t.path / "OkaySpace.exe", true}, {"OkayEngine.exe", t.path / "OkayEngine.exe", false}, {"OkaySpacePlayer.exe", t.path / "OkaySpacePlayer.exe", false}};
        };
        // Good path: all swapped; the running program's backup is kept, the others' are not.
        setup(t.path); mk();
        okayupdater::Outcome o = okayupdater::CommitAll(items, 1);
        CHECK(o.ok && Get(t.path / "OkaySpace.exe") == "new OkaySpace.exe" && Get(t.path / "OkayEngine.exe") == "new OkayEngine.exe" && Get(t.path / "OkaySpacePlayer.exe") == "new OkaySpacePlayer.exe");
        CHECK(Get(t.path / "OkaySpace.exe.old") == "old launcher" && !Exists(t.path / "OkayEngine.exe.old") && !Exists(t.path / "OkaySpacePlayer.exe.old") && !Exists(t.path / "OkayEngine.exe.new"));
        // The LAST file cannot be swapped (its staged file vanished): the first two are put back, nothing is half installed.
        fresh();
        setup(t.path); fs::remove(t.path / "OkaySpacePlayer.exe.new"); mk();
        o = okayupdater::CommitAll(items, 1);
        CHECK(!o.ok && Has(o.reason, "OkaySpacePlayer.exe") && Has(o.reason, "put back"));
        CHECK(Get(t.path / "OkaySpace.exe") == "old launcher" && Get(t.path / "OkayEngine.exe") == "old engine" && Get(t.path / "OkaySpacePlayer.exe") == "old player");
        CHECK(!Exists(t.path / "OkaySpace.exe.new") && !Exists(t.path / "OkayEngine.exe.new") && !Exists(t.path / "OkaySpace.exe.old") && !Exists(t.path / "OkayEngine.exe.old"));
        // The FIRST (the running launcher) cannot be swapped: nothing else is touched.
        fresh();
        setup(t.path); fs::remove(t.path / "OkaySpace.exe.new"); mk();
        o = okayupdater::CommitAll(items, 1);
        CHECK(!o.ok && Has(o.reason, "OkaySpace.exe") && Has(o.reason, "Nothing was changed"));
        CHECK(Get(t.path / "OkayEngine.exe") == "old engine" && !Exists(t.path / "OkayEngine.exe.new"));
        // Too small is refused like a missing one.
        fresh();
        setup(t.path); mk();
        o = okayupdater::CommitAll(items, 100000);
        CHECK(!o.ok && Has(o.reason, "too small") && Get(t.path / "OkaySpace.exe") == "old launcher");
        // A fresh install (no previous file) rolls back by removing what it added.
        fresh();
        Put(t.path / "OkaySpace.exe.new", "new L"); Put(t.path / "OkayEngine.exe.new", "new E");
        items = {{"OkaySpace.exe", t.path / "OkaySpace.exe", true}, {"OkayEngine.exe", t.path / "OkayEngine.exe", false}, {"OkaySpacePlayer.exe", t.path / "OkaySpacePlayer.exe", false}};
        o = okayupdater::CommitAll(items, 1);
        CHECK(!o.ok && !Exists(t.path / "OkaySpace.exe") && !Exists(t.path / "OkayEngine.exe"));
    }

    RUN_SUITE("self-update: the whole apply flow on fake files");
    {
        // Download (staged and checked) -> swap -> state -> restart verdicts, as RunUpdateCheck and the next start do it.
        TempDir t;
        const std::string newBody = "NEW LAUNCHER BYTES", oldBody = "OLD LAUNCHER BYTES";
        const fs::path exe = t.path / "OkaySpace.exe";
        Put(exe, oldBody);
        std::map<std::string, std::string> server{{"https://x.test/v2.856.0/OkaySpace.exe", newBody}};
        okayupdater::FetchFn fetch = [&](const std::string& u, const fs::path& to, std::string& err) {
            auto it = server.find(u);
            if (it == server.end()) { err = "404"; return false; }
            Put(to, it->second);
            return true;
        };
        FileInfo good; good.size = newBody.size(); good.sha256 = Sha(newBody);
        const std::string url = "https://x.test/v2.856.0/OkaySpace.exe";
        // 1. a failed apply is recorded with the reason and retried once more at the next start, then not again automatically
        std::string why;
        okayupdater::Outcome st = okayupdater::Stage("OkaySpace.exe", url, exe, nullptr, fetch);          // no checksum for it
        CHECK(!st.ok && Has(st.reason, "OkaySpace.exe") && Get(exe) == oldBody && !Exists(fs::path(exe.string() + ".new")));
        su::State state = su::AfterAttempt(su::Read(su::StatePath(t.path)), "2.855.0", "2.856.0", false, st.reason);
        std::string err;
        CHECK(su::Write(su::StatePath(t.path), state, err));
        CHECK(su::AutoRetryAllowed(su::Read(su::StatePath(t.path)), "2.856.0", why));               // 1st failure: next start retries
        state = su::AfterAttempt(su::Read(su::StatePath(t.path)), "2.855.0", "2.856.0", false, "second");
        su::Write(su::StatePath(t.path), state, err);
        CHECK(!su::AutoRetryAllowed(su::Read(su::StatePath(t.path)), "2.856.0", why));              // 2nd: stop (no loop)
        // 2. the person retries and it works: swapped, state says so, the previous file is kept for a rollback
        st = okayupdater::Stage("OkaySpace.exe", url, exe, &good, fetch);
        CHECK(st.ok);
        CHECK(okayupdater::CommitAll({{"OkaySpace.exe", exe, true}}, 1).ok && Get(exe) == newBody && Get(su::BackupPath(exe)) == oldBody);
        state = su::AfterAttempt(su::Read(su::StatePath(t.path)), "2.855.0", "2.856.0", true, "");
        su::Write(su::StatePath(t.path), state, err);
        // 3a. next start, the new launcher runs: confirmed, the backup and state are cleaned up by the caller
        su::Startup v = su::EvaluateStartup(su::Read(su::StatePath(t.path)), "2.856.0");
        CHECK(v.kind == su::StartupKind::Confirmed && v.removeBackup && v.clearState);
        // 3b. instead the new file does not start: rolled back, the old launcher is in place and says why at its next start
        su::RollbackResult rb = su::RollBack(exe, su::BackupPath(exe));
        CHECK(rb.ok && Get(exe) == oldBody);
        su::State failed = su::Read(su::StatePath(t.path));
        failed.phase = su::Phase::Failed; failed.reason = "the new file could not be started";
        su::Write(su::StatePath(t.path), failed, err);
        v = su::EvaluateStartup(su::Read(su::StatePath(t.path)), "2.855.0");
        CHECK(v.kind == su::StartupKind::FailedBefore && v.showDialog && Has(v.message, "could not be started") && v.resetVersionFile);
    }

    RUN_SUITE("updater: the checksum list is required (hostile versions.json)");
    {
        const std::string sha(64, 'a');
        auto file = [&](const std::string& name, const std::string& body) { return "\"" + name + "\": " + body; };
        auto entry = [&](const std::string& files) {
            return "{\"schema\":1,\"latest\":\"2.856.0\",\"lts\":\"2.850.1\",\"archive\":\"https://g.test/v{version}/{file}\",\"bucketPath\":\"v{version}/{file}\","
                   "\"versions\":[{\"version\":\"2.856.0\",\"channel\":\"latest\",\"inBucket\":true,\"files\":{" + files + "}}]}";
        };
        const std::string okFile = "{\"size\":120000,\"sha256\":\"" + sha + "\"}";
        const std::string three = file(okayrel::IndexName(okayrel::HostPlatform(), okayrel::Role::Editor), okFile) + "," + file(okayrel::IndexName(okayrel::HostPlatform(), okayrel::Role::Player), okFile) + "," + file(okayrel::IndexName(okayrel::HostPlatform(), okayrel::Role::Launcher), okFile);   // the host's names (Windows: the .exe files);
        const std::string bucket = "https://b.test/releases/";
        // The invariant for ANY text: a plan that is ok has a 64-hex checksum and a size for every downloaded file.
        auto invariant = [&](const std::string& text) {
            const Index ix = ParseIndex(text);
            const InstallPlan p = PlanInstall(ix, "2.856.0", bucket, {}, false);
            if (!p.ok) return true;
            for (const PlanFile& f : p.files)
                if (f.localCopy.empty() && !(f.sha256.size() == 64 && f.size > 0 && f.size <= kMaxFileBytes && f.verified)) return false;
            return !p.files.empty();
        };
        CHECK(invariant(entry(three)) && PlanInstall(ParseIndex(entry(three)), "2.856.0", bucket, {}, false).ok);   // sanity: the good one works
        std::vector<std::string> hostile = {
            "", "{", "[]", "null", "\"x\"", std::string("\0\0\0", 3), "{\"schema\":1}", "{\"schema\":1,\"versions\":{}}", "{\"schema\":1,\"versions\":[1,2,3]}",
            entry(""), entry(file("OkayEngine.exe", "{}")), entry(file("OkayEngine.exe", "{\"size\":5}")), entry(file("OkayEngine.exe", "{\"sha256\":\"" + sha + "\"}")),
            entry(file("OkayEngine.exe", "{\"size\":120000,\"sha256\":123}")), entry(file("OkayEngine.exe", "{\"size\":120000,\"sha256\":[\"" + sha + "\"]}")),
            entry(file("OkayEngine.exe", "{\"size\":120000,\"sha256\":\"" + std::string(63, 'a') + "\"}")),
            entry(file("OkayEngine.exe", "{\"size\":120000,\"sha256\":\"" + std::string(65, 'a') + "\"}")),
            entry(file("OkayEngine.exe", "{\"size\":120000,\"sha256\":\"" + std::string(64, 'g') + "\"}")),
            entry(file("OkayEngine.exe", "{\"size\":-1,\"sha256\":\"" + sha + "\"}")), entry(file("OkayEngine.exe", "{\"size\":0,\"sha256\":\"" + sha + "\"}")),
            entry(file("OkayEngine.exe", "{\"size\":99999999999999999999,\"sha256\":\"" + sha + "\"}")), entry(file("OkayEngine.exe", "{\"size\":1e30,\"sha256\":\"" + sha + "\"}")),
            entry(file("../OkayEngine.exe", okFile) + "," + file("OkaySpacePlayer.exe", okFile)), entry(file("OkayEngine.exe", "\"" + sha + "\"")),
            entry(file("OkayEngine.exe", okFile) + "," + file("OkayEngine.exe", "{}")),   // a duplicate key with no checksum after a good one
            std::string(200000, '[') + std::string(200000, ']'),                           // deep nesting
            entry(three).substr(0, entry(three).size() / 2),                               // truncated
        };
        for (const std::string& h : hostile) CHECK(invariant(h));
        // Specific refusals, with their reasons.
        CHECK(!PlanInstall(ParseIndex(entry(file("OkayEngine.exe", okFile))), "2.856.0", bucket, {}, false).ok);   // the player is not listed
        const InstallPlan noSha = PlanInstall(ParseIndex(entry(file("OkayEngine.exe", "{\"size\":120000}") + "," + file("OkaySpacePlayer.exe", okFile))), "2.856.0", bucket, {}, false);
        CHECK(!noSha.ok && Has(noSha.error, "no checksum"));
        CHECK(!PlanInstall(Index{}, "2.856.0", bucket, {}, false).ok);
        // The launcher updater's own gate, on the same inputs.
        const Index ix = ParseIndex(entry(three));
        CHECK(okayupdater::IndexProblem(ix, ix.Find("2.856.0"), "2.856.0").empty());
        CHECK(Has(okayupdater::IndexProblem(Index{}, nullptr, "2.856.0"), "could not be loaded"));
        CHECK(Has(okayupdater::IndexProblem(ix, ix.Find("2.999.0"), "2.999.0"), "does not list v2.999.0"));
        const Index fb = FallbackIndex("2.856.0", "", "", true);
        CHECK(Has(okayupdater::IndexProblem(fb, fb.Find("2.856.0"), "2.856.0"), "has no checksum list"));
        FileInfo weak; weak.size = 1; weak.sha256 = "abc";
        CHECK(Has(okayupdater::RequireChecksum("OkaySpace.exe", &weak), "no valid SHA-256") && Has(okayupdater::RequireChecksum("OkaySpace.exe", nullptr), "does not list this file"));
        // Several addresses: each is tried and each is checked; a wrong file at the first does not stop a good one at the second.
        TempDir t;
        std::map<std::string, std::string> server{{"https://a.test/f", "WRONG-BYTES-HERE"}, {"https://b.test/f", "RIGHT"}};
        okayupdater::FetchFn fetch = [&](const std::string& u, const fs::path& to, std::string& err) {
            auto it = server.find(u);
            if (it == server.end()) { err = "404"; return false; }
            Put(to, it->second);
            return true;
        };
        FileInfo rightInfo; rightInfo.size = 5; rightInfo.sha256 = Sha("RIGHT");
        const fs::path dest = t.path / "f.exe";
        okayupdater::Outcome o = okayupdater::StageFromUrls("f.exe", {"https://a.test/f", "https://nope.test/f", "https://b.test/f"}, dest, &rightInfo, fetch);
        CHECK(o.ok && Get(fs::path(dest.string() + ".new")) == "RIGHT");
        o = okayupdater::StageFromUrls("f.exe", {"https://a.test/f"}, dest, &rightInfo, fetch);
        CHECK(!o.ok && Has(o.reason, "not the published file"));
        o = okayupdater::StageFromUrls("f.exe", {}, dest, &rightInfo, fetch);
        CHECK(!o.ok && Has(o.reason, "nowhere to download"));
        o = okayupdater::StageFromUrls("f.exe", {"https://b.test/f"}, dest, nullptr, fetch);
        CHECK(!o.ok && Has(o.reason, "does not list this file") && !Exists(fs::path(dest.string() + ".new")));
    }

    TEST_MAIN_RESULT();
}
