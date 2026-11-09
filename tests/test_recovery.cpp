#include "test_framework.hpp"
#include "okay/Core/Recovery.hpp"
#include "okay/Core/SaveData.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace okay;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path path;
    explicit TempDir(const std::string& tag) {
        static int n = 0;
        path = fs::temp_directory_path() /
               ("okay_test_" + tag + "_" + std::to_string(++n) + "_" +
                std::to_string((long long)std::chrono::steady_clock::now().time_since_epoch().count()));
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::create_directories(path, ec);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(path, ec); }
    std::string Str() const { return path.string(); }
    std::string Sub(const std::string& s) const { return (path / s).string(); }
};

std::string ReadAll(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
void WriteRaw(const std::string& p, const std::string& s) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(s.data(), (std::streamsize)s.size());
}
bool Exists(const std::string& p) { std::error_code ec; return fs::exists(p, ec); }
int CountExt(const std::string& dir, const std::string& ext) {
    int n = 0;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        if (it->path().string().size() >= ext.size() &&
            it->path().string().compare(it->path().string().size() - ext.size(), ext.size(), ext) == 0) ++n;
    return n;
}

RecoveryManager::Config Cfg(const TempDir& d, const std::string& session) {
    RecoveryManager::Config c;
    c.directory = d.Sub("recovery");
    c.sessionId = session;
    return c;
}

using AR = RecoveryManager::AutosaveResult;

} // namespace

int main() {
    RUN_SUITE("recovery");

    std::int64_t now = 1700000000;
    auto clock = [&now] { return now; };
    auto dead = [](long) { return false; };
    auto alive = [](long) { return true; };

    // --- Session lifecycle: lock written, removed on clean exit ---------------------------
    {
        TempDir d("rec_life");
        RecoveryManager a(Cfg(d, "sessA"));
        a.SetClock(clock);
        CHECK(!a.Active());
        CHECK(a.AutosavePayload("x") == AR::Failed);          // no session yet
        CHECK(!a.LastError().empty());
        CHECK(a.StartSession());
        CHECK(a.Active() && a.SessionId() == "sessA");
        CHECK(Exists(d.Sub("recovery/autosave-sessA.lock")));
        CHECK(!a.UncleanShutdownDetected());
        CHECK(a.AutosavePayload("hello", "Level1.okayscene") == AR::Written);
        CHECK(Exists(d.Sub("recovery/autosave-sessA-0.autosave")));
        CHECK(a.AutosaveCount() == 1 && a.LastAutosaveTime() == now);
        a.EndSession();
        CHECK(!a.Active());
        CHECK(!Exists(d.Sub("recovery/autosave-sessA.lock")));
        CHECK(!Exists(d.Sub("recovery/autosave-sessA-0.autosave")));   // a clean exit leaves nothing
        a.EndSession();                                                // twice is fine

        // EndSession(false) keeps the autosaves (the lock still goes).
        RecoveryManager b(Cfg(d, "sessB"));
        b.SetClock(clock);
        b.StartSession();
        b.AutosavePayload("keep me");
        b.EndSession(false);
        CHECK(!Exists(d.Sub("recovery/autosave-sessB.lock")));
        CHECK(Exists(d.Sub("recovery/autosave-sessB-0.autosave")));
    }

    // --- Rotation over N slots; candidates carry time, size, label ---------------------------------
    {
        TempDir d("rec_rot");
        auto cfg = Cfg(d, "rot");
        cfg.slots = 3;
        RecoveryManager a(cfg);
        a.SetClock(clock);
        a.StartSession();
        const std::vector<std::string> p = {"payload-one", "payload-two!", "payload-three", "payload-four", "payload-five!!"};
        for (std::size_t i = 0; i < p.size(); ++i) {
            now = 1700000100 + (std::int64_t)i * 10;
            CHECK(a.AutosavePayload(p[i], "scene" + std::to_string(i)) == AR::Written);
        }
        CHECK(CountExt(d.Sub("recovery"), ".autosave") == 3);          // never more than `slots` files
        CHECK(ReadAll(d.Sub("recovery/autosave-rot-0.autosave")).find("payload-four") != std::string::npos);
        CHECK(ReadAll(d.Sub("recovery/autosave-rot-1.autosave")).find("payload-five") != std::string::npos);
        CHECK(ReadAll(d.Sub("recovery/autosave-rot-2.autosave")).find("payload-three") != std::string::npos);
        CHECK(a.NextAutosavePath() == d.Sub("recovery/autosave-rot-2.autosave") || a.NextAutosavePath().find("rot-2.autosave") != std::string::npos);

        auto c = a.FindRecoverable(true);
        CHECK(c.size() == 3);
        if (c.size() == 3) {
            CHECK(c[0].timestamp == 1700000140 && c[1].timestamp == 1700000130 && c[2].timestamp == 1700000120);
            CHECK(c[0].seq == 5 && c[1].seq == 4 && c[2].seq == 3);
            CHECK(c[0].size == p[4].size() && c[1].size == p[3].size() && c[2].size == p[2].size());
            CHECK(c[0].label == "scene4" && c[0].sessionId == "rot");
            std::string out;
            CHECK(a.Restore(c[0], out) && out == p[4]);
            CHECK(a.Restore(c[2], out) && out == p[2]);
        }
        CHECK(a.FindRecoverable(false).empty());                         // own files only with the flag

        // Identical text is not rewritten; with skipUnchanged off it is.
        const int count = a.AutosaveCount();
        CHECK(a.AutosavePayload(p[4]) == AR::Skipped);
        CHECK(a.AutosaveCount() == count);
        CHECK(a.AutosavePayload("changed") == AR::Written);

        // One slot: the file is overwritten each time.
        TempDir d2("rec_one");
        auto cfg1 = Cfg(d2, "one");
        cfg1.slots = 1;
        cfg1.skipUnchanged = false;
        RecoveryManager one(cfg1);
        one.SetClock(clock);
        one.StartSession();
        one.AutosavePayload("same");
        one.AutosavePayload("same");
        one.AutosavePayload("same");
        CHECK(CountExt(d2.Sub("recovery"), ".autosave") == 1);
        CHECK(one.AutosaveCount() == 3);
    }

    // --- Tick: interval, provider, disabled ---------------------------------------------------------------
    {
        TempDir d("rec_tick");
        auto cfg = Cfg(d, "tick");
        cfg.intervalSeconds = 60.0f;
        RecoveryManager a(cfg);
        a.SetClock(clock);
        int calls = 0;
        bool dirty = true;
        a.SetProvider([&](std::string& payload, std::string& label) {
            ++calls;
            if (!dirty) return false;
            payload = "doc " + std::to_string(calls);
            label = "scene";
            return true;
        });
        CHECK(!a.Tick(100.0f));                     // no session: nothing happens
        CHECK(a.StartSession());
        CHECK(!a.Tick(30.0f));
        CHECK(calls == 0);
        CHECK(a.Tick(30.0f));                       // 60 s elapsed
        CHECK(calls == 1 && a.AutosaveCount() == 1);
        CHECK(!a.Tick(59.0f));
        CHECK(a.Tick(1.0f));
        CHECK(a.AutosaveCount() == 2);
        dirty = false;                              // the document is clean: provider says no
        CHECK(!a.Tick(60.0f));
        CHECK(a.AutosaveCount() == 2);
        dirty = true;
        a.SetEnabled(false);
        CHECK(!a.Tick(1000.0f));
        a.SetEnabled(true);
        CHECK(!a.Tick(1.0f));                       // the disabled time doesn't count
        CHECK(a.Tick(60.0f));
        a.SetInterval(0.0f);                        // 0 turns the timer off
        CHECK(!a.Tick(1000.0f));
        CHECK(a.Interval() == 0.0f);

        // Autosave() works without Tick; without a provider it does nothing.
        RecoveryManager b(Cfg(d, "tick2"));
        b.SetClock(clock);
        b.StartSession();
        CHECK(b.Autosave() == AR::Skipped);
        b.SetProvider([](std::string& p, std::string& l) { p = "now"; l = ""; return true; });
        CHECK(b.Autosave() == AR::Written);
    }

    // --- Failed write retries soon, not a full interval later ------------------------------------------------
    {
        TempDir d("rec_retry");
        auto cfg = Cfg(d, "retry");
        cfg.intervalSeconds = 60.0f;
        cfg.retrySeconds = 10.0f;
        cfg.maxPayloadBytes = 100;
        RecoveryManager a(cfg);
        a.SetClock(clock);
        a.StartSession();
        int calls = 0;
        std::string text(500, 'x');
        a.SetProvider([&](std::string& p, std::string& l) { ++calls; p = text; l = ""; return true; });
        CHECK(!a.Tick(60.0f));
        CHECK(calls == 1 && !a.LastError().empty());
        CHECK(a.AutosavePayload(text) == AR::Failed);
        calls = 0;
        CHECK(!a.Tick(9.0f) && calls == 0);
        CHECK(!a.Tick(1.0f) && calls == 1);          // retried after ~10 s
        text = "small";
        CHECK(!a.Tick(9.0f));
        CHECK(a.Tick(1.0f));                         // now it fits
        CHECK(a.LastError().empty());
    }

    // --- Simulated crash: the lock is left behind ----------------------------------------------------------------
    {
        TempDir d("rec_crash");
        {
            RecoveryManager a(Cfg(d, "dead1"));
            a.SetClock(clock);
            a.StartSession();
            now = 1700001000; a.AutosavePayload("first state", "Level1.okayscene");
            now = 1700001060; a.AutosavePayload("second state, longer", "Level1.okayscene");
            // ... the process dies here: no EndSession().
        }
        CHECK(Exists(d.Sub("recovery/autosave-dead1.lock")));

        RecoveryManager b(Cfg(d, "new1"));
        b.SetClock(clock);
        b.SetProcessProbe(dead);                     // dead1's process is gone
        CHECK(b.StartSession());
        CHECK(b.UncleanShutdownDetected());
        CHECK(b.CrashedSessions().size() == 1 && b.CrashedSessions()[0] == "dead1");
        CHECK(!Exists(d.Sub("recovery/autosave-dead1.lock")));          // the stale lock is cleared
        CHECK(Exists(d.Sub("recovery/autosave-new1.lock")));

        auto c = b.FindRecoverable();
        CHECK(c.size() == 2);
        if (c.size() == 2) {
            CHECK(c[0].unclean && c[1].unclean);
            CHECK(c[0].sessionId == "dead1");
            CHECK(c[0].timestamp == 1700001060 && c[1].timestamp == 1700001000);
            CHECK(c[0].size == std::string("second state, longer").size());
            CHECK(c[0].label == "Level1.okayscene");
            std::string out;
            CHECK(b.Restore(c[0], out) && out == "second state, longer");

            // Restore into a file, then discard the candidates.
            const std::string dest = d.Sub("restored.okayscene");
            CHECK(b.RestoreToFile(c[1], dest));
            CHECK(ReadAll(dest) == "first state");
            CHECK(b.Discard(c[0]));
            CHECK(!Exists(c[0].path));
            CHECK(!b.Discard(c[0]));                                     // already gone
            CHECK(b.FindRecoverable().size() == 1);
            CHECK(b.DiscardAll() == 1);
            CHECK(b.FindRecoverable().empty());
        }

        // The next start sees no crash: the old session was dealt with.
        b.EndSession();
        RecoveryManager e(Cfg(d, "new2"));
        e.SetProcessProbe(dead);
        e.StartSession();
        CHECK(!e.UncleanShutdownDetected());
        CHECK(e.FindRecoverable().empty());
    }

    // --- A crash before the first autosave is still reported -------------------------------------------------------
    {
        TempDir d("rec_crash0");
        { RecoveryManager a(Cfg(d, "early")); a.StartSession(); }
        RecoveryManager b(Cfg(d, "late"));
        b.SetProcessProbe(dead);
        b.StartSession();
        CHECK(b.UncleanShutdownDetected());
        CHECK(b.FindRecoverable().empty());
    }

    // --- Clean exit leaves nothing for the next run -------------------------------------------------------------------
    {
        TempDir d("rec_clean");
        {
            RecoveryManager a(Cfg(d, "tidy"));
            a.StartSession();
            a.AutosavePayload("work");
            a.EndSession();
        }
        RecoveryManager b(Cfg(d, "next"));
        b.SetProcessProbe(dead);
        b.StartSession();
        CHECK(!b.UncleanShutdownDetected());
        CHECK(b.FindRecoverable().empty());
    }

    // --- A running peer is not a crash and its autosaves are not offered --------------------------------------------------
    {
        TempDir d("rec_peer");
        RecoveryManager a(Cfg(d, "peer"));
        a.StartSession();
        a.AutosavePayload("peer work");

        RecoveryManager b(Cfg(d, "me"));
        b.SetProcessProbe(alive);
        b.StartSession();
        CHECK(!b.UncleanShutdownDetected());
        CHECK(b.FindRecoverable().empty());
        CHECK(Exists(d.Sub("recovery/autosave-peer.lock")));             // its lock is untouched
        CHECK(b.DiscardAll() == 0);                                       // and its files are safe
        CHECK(Exists(d.Sub("recovery/autosave-peer-0.autosave")));

        // The same files are offered once the peer's process is gone.
        RecoveryManager c(Cfg(d, "later"));
        c.SetProcessProbe(dead);
        c.StartSession();
        CHECK(c.UncleanShutdownDetected());
        CHECK(c.FindRecoverable().size() == 1);
    }

    // --- The real process check: this process is alive, a made-up pid is not ----------------------------------------------------------
    {
        TempDir d("rec_pid");
        RecoveryManager a(Cfg(d, "self"));     // its lock records our own pid
        a.StartSession();
        a.AutosavePayload("mine");
        RecoveryManager b(Cfg(d, "other"));    // default probe: asks the OS
        b.StartSession();
        CHECK(!b.UncleanShutdownDetected());
        CHECK(b.FindRecoverable().empty());

        WriteRaw(d.Sub("recovery/autosave-ghost.lock"), "OKAYLOCK 1\nsession=ghost\npid=2147483646\nstart=1\n");
        WriteRaw(d.Sub("recovery/autosave-junk.lock"), "not a lock at all");   // unreadable lock = dead
        RecoveryManager c(Cfg(d, "third"));
        c.StartSession();
        CHECK(c.UncleanShutdownDetected());
        const auto& crashed = c.CrashedSessions();
        CHECK(std::find(crashed.begin(), crashed.end(), "ghost") != crashed.end());
        CHECK(std::find(crashed.begin(), crashed.end(), "junk") != crashed.end());
        CHECK(std::find(crashed.begin(), crashed.end(), "self") == crashed.end());
        CHECK(Exists(d.Sub("recovery/autosave-self.lock")));
        CHECK(c.FindRecoverable().empty());                               // self is alive: its file stays hidden
    }

    // --- Torn and damaged files are skipped; an older valid slot is still offered -------------------------------------------------------
    {
        TempDir d("rec_torn");
        {
            auto cfg = Cfg(d, "tornA");
            cfg.slots = 4;
            RecoveryManager a(cfg);
            a.SetClock(clock);
            a.StartSession();
            const std::string body(400, 'q');
            for (int i = 0; i < 4; ++i) {
                now = 1700002000 + i * 10;
                a.AutosavePayload(body + std::to_string(i), "scene");
            }
            // crash
        }
        const std::string rec = d.Sub("recovery");
        const std::string f3 = rec + "/autosave-tornA-3.autosave";   // newest
        const std::string f2 = rec + "/autosave-tornA-2.autosave";
        const std::string f1 = rec + "/autosave-tornA-1.autosave";
        const std::string f0 = rec + "/autosave-tornA-0.autosave";
        const std::string good3 = ReadAll(f3);

        WriteRaw(f3, good3.substr(0, good3.size() / 2 + 40));           // cut off inside the payload
        { std::string s = ReadAll(f2); s[s.size() - 100] ^= 0x55; WriteRaw(f2, s); }   // one flipped byte
        WriteRaw(f1, ReadAll(f1) + "trailing garbage");                 // too long
        // f0 stays valid.

        auto cfg = Cfg(d, "tornB");
        cfg.removeCorruptOnStart = false;
        RecoveryManager b(cfg);
        b.SetClock(clock);
        b.SetProcessProbe(dead);
        b.StartSession();
        auto c = b.FindRecoverable();
        CHECK(c.size() == 1);
        if (!c.empty()) {
            CHECK(c[0].path == f0 && c[0].seq == 1);
            std::string out;
            CHECK(b.Restore(c[0], out) && out == std::string(400, 'q') + "0");
        }
        CHECK(b.SkippedFiles().size() == 3);
        auto reasonOf = [&](const std::string& path) {
            for (const auto& s : b.SkippedFiles()) if (s.path == path) return s.reason;
            return std::string("<not skipped>");
        };
        CHECK(reasonOf(f3) == "truncated");
        CHECK(reasonOf(f2) == "checksum mismatch");
        CHECK(reasonOf(f1) == "size mismatch");

        // Other ways to be torn.
        WriteRaw(rec + "/autosave-tornC-0.autosave", "");                         // empty
        WriteRaw(rec + "/autosave-tornC-1.autosave", "OKAYREC 1\nsession=tornC\nseq=1\ntim");   // cut inside the header
        WriteRaw(rec + "/autosave-tornC-2.autosave", "garbage garbage\n\nmore garbage");
        WriteRaw(rec + "/autosave-tornC-3.autosave", "OKAYREC 7\nsession=x\nseq=1\ntime=1\nlabel=\nbytes=0\nfnv=cbf29ce484222325\n\n");   // newer format
        WriteRaw(rec + "/autosave-tornC-4.autosave", "OKAYREC 1\nsession=x\nseq=abc\ntime=1\nlabel=\nbytes=0\nfnv=cbf29ce484222325\n\n"); // bad number
        c = b.FindRecoverable();
        CHECK(c.size() == 1);
        CHECK(reasonOf(rec + "/autosave-tornC-0.autosave") == "empty file");
        CHECK(reasonOf(rec + "/autosave-tornC-1.autosave") == "bad header");
        CHECK(reasonOf(rec + "/autosave-tornC-2.autosave") == "bad header");
        CHECK(reasonOf(rec + "/autosave-tornC-3.autosave") == "newer format");
        CHECK(reasonOf(rec + "/autosave-tornC-4.autosave") == "bad header");

        // A candidate that is damaged after listing fails to restore cleanly.
        std::string out = "stale";
        std::string err;
        const auto snapshot = c[0];
        WriteRaw(snapshot.path, "damaged now");
        CHECK(!b.Restore(snapshot, out, &err));
        CHECK(out.empty() && err.find("damaged") != std::string::npos);
        CHECK(!b.RestoreToFile(snapshot, d.Sub("never.txt")));
        CHECK(!Exists(d.Sub("never.txt")));

        // RemoveCorrupt deletes the unreadable ones but keeps a newer-format file.
        const int removed = b.RemoveCorrupt();
        CHECK(removed >= 6);
        CHECK(!Exists(f3) && !Exists(f2) && !Exists(f1));
        CHECK(Exists(rec + "/autosave-tornC-3.autosave"));
    }

    // --- Corrupt files of dead sessions are cleared at start by default ------------------------------------------------------------------
    {
        TempDir d("rec_clearcorrupt");
        fs::create_directories(d.Sub("recovery"));
        WriteRaw(d.Sub("recovery/autosave-old-0.autosave"), "torn");
        RecoveryManager a(Cfg(d, "new"));
        a.SetProcessProbe(dead);
        a.StartSession();
        CHECK(!Exists(d.Sub("recovery/autosave-old-0.autosave")));
    }

    // --- Half-written temp files of dead sessions are cleaned; a live peer's are left ------------------------------------------------------
    {
        TempDir d("rec_tmp");
        fs::create_directories(d.Sub("recovery"));
        WriteRaw(d.Sub("recovery/autosave-ghost-0.autosave.tmp"), "half");
        WriteRaw(d.Sub("recovery/autosave-ghost.lock.tmp"), "half");
        WriteRaw(d.Sub("recovery/autosave-busy.lock"), "OKAYLOCK 1\nsession=busy\npid=4242\nstart=1\n");
        WriteRaw(d.Sub("recovery/autosave-busy-0.autosave.tmp"), "in flight");
        WriteRaw(d.Sub("recovery/unrelated.tmp"), "not ours");
        RecoveryManager a(Cfg(d, "fresh"));
        a.SetProcessProbe([](long pid) { return pid == 4242; });
        a.StartSession();
        CHECK(!Exists(d.Sub("recovery/autosave-ghost-0.autosave.tmp")));
        CHECK(!Exists(d.Sub("recovery/autosave-ghost.lock.tmp")));
        CHECK(Exists(d.Sub("recovery/autosave-busy-0.autosave.tmp")));
        CHECK(Exists(d.Sub("recovery/unrelated.tmp")));
        CHECK(!a.UncleanShutdownDetected());
    }

    // --- Old sessions are trimmed to maxOtherSessions (newest kept) ---------------------------------------------------------------------------
    {
        TempDir d("rec_trim");
        for (int i = 0; i < 4; ++i) {
            RecoveryManager a(Cfg(d, "old" + std::to_string(i)));
            a.SetClock([i] { return (std::int64_t)(1700003000 + i * 100); });
            a.StartSession();
            a.AutosavePayload("session " + std::to_string(i));
        }
        auto cfg = Cfg(d, "now");
        cfg.maxOtherSessions = 2;
        RecoveryManager b(cfg);
        b.SetProcessProbe(dead);
        b.StartSession();
        auto c = b.FindRecoverable();
        CHECK(c.size() == 2);
        if (c.size() == 2) CHECK(c[0].sessionId == "old3" && c[1].sessionId == "old2");
        CHECK(!Exists(d.Sub("recovery/autosave-old0-0.autosave")));
    }

    // --- OnSaved drops this session's autosaves ---------------------------------------------------------------------------------------------------
    {
        TempDir d("rec_saved");
        RecoveryManager a(Cfg(d, "sv"));
        a.StartSession();
        a.AutosavePayload("v1");
        a.AutosavePayload("v2");
        CHECK(CountExt(d.Sub("recovery"), ".autosave") == 2);
        a.OnSaved();
        CHECK(CountExt(d.Sub("recovery"), ".autosave") == 0);
        CHECK(a.AutosavePayload("v2") == AR::Written);     // same text again after a save is written again
        CHECK(a.Active());
    }

    // --- Encode / Decode round trip --------------------------------------------------------------------------------------------------------------------
    {
        static const char kBin[] = "line1\n\nline3\0binary\r\n\xff\xfe";
        const std::string payload(kBin, sizeof(kBin) - 1);
        const std::string label = "my=scene\nwith\\odd chars";
        const std::string file = RecoveryManager::Encode("s1", 42, 1234567890, label, payload);
        RecoveryManager::Candidate meta;
        std::string out, why;
        CHECK(RecoveryManager::Decode(file, meta, out, &why));
        CHECK(out == payload && out.size() == payload.size());
        CHECK(meta.sessionId == "s1" && meta.seq == 42 && meta.timestamp == 1234567890);
        CHECK(meta.label == label && meta.size == payload.size());

        // Empty payload and empty label.
        const std::string empty = RecoveryManager::Encode("s2", 1, 5, "", "");
        CHECK(RecoveryManager::Decode(empty, meta, out, &why) && out.empty() && meta.label.empty());

        // Every truncation of a valid file is rejected (never a half payload).
        int accepted = 0;
        for (std::size_t n = 0; n < file.size(); ++n) {
            if (RecoveryManager::Decode(file.substr(0, n), meta, out, nullptr)) ++accepted;
        }
        CHECK(accepted == 0);
        // Flipping any single payload byte is caught.
        const std::size_t bodyStart = file.size() - payload.size();
        int missed = 0;
        for (std::size_t i = bodyStart; i < file.size(); ++i) {
            std::string bad = file;
            bad[i] ^= 0x01;
            if (RecoveryManager::Decode(bad, meta, out, nullptr)) ++missed;
        }
        CHECK(missed == 0);
    }

    // --- Setup failures ------------------------------------------------------------------------------------------------------------------------------------------
    {
        TempDir d("rec_fail");
        RecoveryManager none(RecoveryManager::Config{});             // no directory
        CHECK(!none.StartSession() && !none.LastError().empty());

        WriteRaw(d.Sub("afile"), "i am a file");
        RecoveryManager::Config cfg;
        cfg.directory = d.Sub("afile");                               // a file where the directory should be
        RecoveryManager bad(cfg);
        CHECK(!bad.StartSession());
        CHECK(!bad.Active() && !bad.LastError().empty());
        CHECK(bad.AutosavePayload("x") == AR::Failed);

        // Default config values are sane (slots clamped, base name fixed).
        RecoveryManager::Config odd = Cfg(d, "odd/../name");
        odd.slots = 0;
        odd.baseName = "we ird/base";
        RecoveryManager o(odd);
        CHECK(o.StartSession());
        CHECK(o.SessionId() == "odd_.._name" || o.SessionId().find('/') == std::string::npos);
        CHECK(o.AutosavePayload("a") == AR::Written);
        CHECK(o.AutosavePayload("b") == AR::Written);                // one slot at least
        CHECK(CountExt(d.Sub("recovery"), ".autosave") == 1);
    }

    // --- The directory may vanish mid-session; generated ids are unique; base names are separate -------------------------------------------------------------------
    {
        TempDir d("rec_misc");
        RecoveryManager::Config c1;
        c1.directory = d.Sub("recovery");
        RecoveryManager a(c1), b(c1);
        a.StartSession();
        b.StartSession();
        CHECK(!a.SessionId().empty() && a.SessionId() != b.SessionId());
        for (char ch : a.SessionId()) CHECK((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || ch == '_');

        std::error_code ec;
        fs::remove_all(d.Sub("recovery"), ec);                        // user cleaned the folder
        CHECK(a.AutosavePayload("still works") == AR::Written);
        CHECK(Exists(a.NextAutosavePath()) || CountExt(d.Sub("recovery"), ".autosave") == 1);

        // Two document kinds in one directory never see each other.
        TempDir d2("rec_kinds");
        auto scene = Cfg(d2, "k1");
        scene.baseName = "scene";
        auto prefab = Cfg(d2, "k1");
        prefab.baseName = "prefab";
        { RecoveryManager s(scene); s.StartSession(); s.AutosavePayload("scene text"); }
        { RecoveryManager p(prefab); p.StartSession(); p.AutosavePayload("prefab text"); }
        auto s2 = scene; s2.sessionId = "k2";
        RecoveryManager sNext(s2);
        sNext.SetProcessProbe(dead);
        sNext.StartSession();
        auto found = sNext.FindRecoverable();
        CHECK(found.size() == 1);
        std::string out;
        if (found.size() == 1) CHECK(sNext.Restore(found[0], out) && out == "scene text");
        CHECK(sNext.DiscardAll() == 1);
        CHECK(CountExt(d2.Sub("recovery"), ".autosave") == 1);        // the prefab one is untouched
    }

    TEST_MAIN_RESULT();
}
