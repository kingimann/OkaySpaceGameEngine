#include "test_framework.hpp"
#include "okay/Core/Backup.hpp"
#include "okay/Core/SaveData.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
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

BackupManager::Info Fake(std::int64_t t, std::uint64_t size = 100, int seq = 0) {
    BackupManager::Info i;
    i.timestamp = t;
    i.size = size;
    i.seq = seq;
    return i;
}

constexpr std::int64_t kDay = 86400;

} // namespace

int main() {
    RUN_SUITE("backup");

    std::int64_t now = 1700000000;
    auto clock = [&now] { return now; };

    // --- Timestamps in names ---------------------------------------------------------------------
    {
        CHECK(BackupManager::FormatStamp(0) == "19700101-000000");
        CHECK(BackupManager::FormatStamp(1700000000) == "20231114-221320");
        CHECK(BackupManager::FormatStamp(951782400) == "20000229-000000");     // a leap day
        CHECK(BackupManager::FormatStamp(951868800) == "20000301-000000");
        CHECK(BackupManager::FormatStamp(4102444800LL) == "21000101-000000");
        CHECK(BackupManager::FormatStamp(-5) == "19700101-000000");            // clamped
        std::int64_t t = 0;
        CHECK(BackupManager::ParseStamp("20231114-221320", t) && t == 1700000000);
        CHECK(BackupManager::ParseStamp("19700101-000000", t) && t == 0);
        for (std::int64_t v : {1LL, 86399LL, 86400LL, 951782399LL, 1234567890LL, 1700000000LL, 2000000000LL, 4102444799LL}) {
            std::int64_t back = -1;
            CHECK(BackupManager::ParseStamp(BackupManager::FormatStamp(v), back) && back == v);
        }
        CHECK(!BackupManager::ParseStamp("20231314-221320", t));    // month 13
        CHECK(!BackupManager::ParseStamp("20231114-246020", t));
        CHECK(!BackupManager::ParseStamp("2023111-221320", t));
        CHECK(!BackupManager::ParseStamp("20231114_221320", t));
        CHECK(!BackupManager::ParseStamp("20231114-22132x", t));
        CHECK(!BackupManager::ParseStamp("", t));

        CHECK(BackupManager::DayIndex(0) == 0);
        CHECK(BackupManager::DayIndex(86399) == 0);
        CHECK(BackupManager::DayIndex(86400) == 1);
        CHECK(BackupManager::DayIndex(-1) == -1);
        CHECK(BackupManager::DayIndex(82800, 3600) == 1);          // 23:00 UTC is already tomorrow at UTC+1
        CHECK(BackupManager::DayIndex(86400, -3600) == 0);
    }

    // --- The pruning rule: keep last N + one per day for M days ------------------------------------------
    {
        const std::int64_t today = 20 * kDay;
        const std::int64_t nowT = today + 12 * 3600;
        const std::vector<BackupManager::Info> list = {     // newest first
            Fake(today + 11 * 3600),            // 0
            Fake(today + 10 * 3600),            // 1
            Fake(today + 9 * 3600),             // 2  same day as 0 and 1
            Fake(today - kDay + 18 * 3600),     // 3  yesterday, newest of its day
            Fake(today - kDay + 8 * 3600),      // 4
            Fake(today - 2 * kDay + 12 * 3600), // 5
            Fake(today - 3 * kDay + 20 * 3600), // 6  3 days ago: outside a 3 day window
            Fake(today - 3 * kDay + 5 * 3600),  // 7
            Fake(today - 10 * kDay),            // 8
        };
        BackupPolicy p;
        p.keepLast = 2;
        p.keepDailyDays = 3;
        auto removed = BackupManager::SelectForRemoval(list, p, nowT);
        CHECK((removed == std::vector<std::size_t>{2, 4, 6, 7, 8}));

        p.keepDailyDays = 0;                                     // daily rule off: only the last N
        removed = BackupManager::SelectForRemoval(list, p, nowT);
        CHECK(removed.size() == 7 && removed.front() == 2);

        p.keepLast = 0;                                          // the newest is never pruned
        removed = BackupManager::SelectForRemoval(list, p, nowT);
        CHECK(removed.size() == 8 && std::find(removed.begin(), removed.end(), 0u) == removed.end());

        p.keepLast = 100;                                        // fewer backups than the limit: nothing goes
        removed = BackupManager::SelectForRemoval(list, p, nowT);
        CHECK(removed.empty());
        CHECK(BackupManager::SelectForRemoval({}, p, nowT).empty());

        // Size cap: oldest first, newest stays.
        std::vector<BackupManager::Info> sized;
        for (int i = 0; i < 5; ++i) sized.push_back(Fake(1000 - i, 100));
        BackupPolicy c;
        c.keepLast = 5; c.keepDailyDays = 0; c.maxTotalBytes = 250;
        removed = BackupManager::SelectForRemoval(sized, c, 1000);
        CHECK((removed == std::vector<std::size_t>{2, 3, 4}));
        c.maxTotalBytes = 10;                                    // smaller than one backup: still keep the newest
        removed = BackupManager::SelectForRemoval(sized, c, 1000);
        CHECK((removed == std::vector<std::size_t>{1, 2, 3, 4}));

        // Day boundaries follow the offset: at UTC+2 these two fall on different days, at UTC the same one.
        std::vector<BackupManager::Info> two = {Fake(10 * kDay + 23 * 3600), Fake(10 * kDay + 21 * 3600), Fake(10 * kDay + 1000)};
        BackupPolicy o;
        o.keepLast = 1; o.keepDailyDays = 5;
        CHECK((BackupManager::SelectForRemoval(two, o, 10 * kDay + 80000, 0) == std::vector<std::size_t>{1, 2}));
        CHECK((BackupManager::SelectForRemoval(two, o, 10 * kDay + 80000, 2 * 3600) == std::vector<std::size_t>{2}));
    }

    // --- Backup: names, listing, identical content, same-second backups ----------------------------------------
    {
        TempDir d("bk_basic");
        fs::create_directories(d.Sub("proj"));
        const std::string file = d.Sub("proj/Level1.okayscene");
        BackupManager::Config cfg;
        cfg.directory = d.Sub("proj/.backups");
        cfg.policy.keepLast = 100;
        BackupManager bm(cfg);
        bm.SetClock(clock);

        CHECK(!bm.Backup(file));                                  // nothing to back up yet
        CHECK(!bm.LastError().empty());
        CHECK(bm.List(file).empty());

        WriteRaw(file, "version 1");
        std::string path;
        bool created = false;
        CHECK(bm.Backup(file, &path, &created));
        CHECK(created && Exists(path) && ReadAll(path) == "version 1");
        CHECK(path.find("Level1.okayscene.") != std::string::npos);
        CHECK(path.find(".20231114-221320.bak") != std::string::npos);
        auto list = bm.List(file);
        CHECK(list.size() == 1);
        if (!list.empty()) {
            CHECK(list[0].timestamp == 1700000000 && list[0].source == "Level1.okayscene");
            CHECK(list[0].size == 9 && list[0].seq == 0 && list[0].path == path);
        }

        // Same content again: no new backup, but the call still says "a backup exists".
        std::string again;
        CHECK(bm.Backup(file, &again, &created));
        CHECK(!created && again == path);
        CHECK(bm.List(file).size() == 1);

        // Changed within the same second: a second file with a suffix; newest first.
        WriteRaw(file, "version 2");
        CHECK(bm.Backup(file, &again, &created));
        CHECK(created && again != path && again.find("-1.bak") != std::string::npos);
        list = bm.List(file);
        CHECK(list.size() == 2 && list[0].seq == 1 && list[1].seq == 0);
        CHECK(bm.Read(list[0], path) && path == "version 2");

        // Many in one second keep their order (-10 sorts after -9).
        for (int i = 3; i <= 12; ++i) { WriteRaw(file, "version " + std::to_string(i)); CHECK(bm.Backup(file)); }
        list = bm.List(file);
        CHECK(list.size() == 12);
        CHECK(!list.empty() && list[0].seq == 11);
        std::string content;
        CHECK(bm.Read(list[0], content) && content == "version 12");
        CHECK(bm.Read(list.back(), content) && content == "version 1");

        // skipIdentical off: every call makes a file.
        BackupManager::Config c2 = cfg;
        c2.directory = d.Sub("proj/.backups2");
        c2.skipIdentical = false;
        BackupManager b2(c2);
        b2.SetClock(clock);
        CHECK(b2.Backup(file));
        CHECK(b2.Backup(file));
        CHECK(b2.List(file).size() == 2);

        // A directory is not a file to back up.
        CHECK(!bm.Backup(d.Sub("proj")));
    }

    // --- SaveWithBackup ---------------------------------------------------------------------------------------------
    {
        TempDir d("bk_save");
        const std::string file = d.Sub("Scene.okayscene");
        BackupManager::Config cfg;
        cfg.directory = d.Sub("bak");
        BackupManager bm(cfg);
        bm.SetClock(clock);

        CHECK(bm.SaveWithBackup(file, "first"));                     // new file: nothing to keep yet
        CHECK(ReadAll(file) == "first");
        CHECK(bm.List(file).empty());
        now += 10;
        CHECK(bm.SaveWithBackup(file, "second"));
        CHECK(ReadAll(file) == "second");
        auto list = bm.List(file);
        CHECK(list.size() == 1);
        std::string c;
        CHECK(!list.empty() && bm.Read(list[0], c) && c == "first");
        now += 10;
        CHECK(bm.SaveWithBackup(file, "third"));
        list = bm.List(file);
        CHECK(list.size() == 2 && bm.Read(list[0], c) && c == "second" && bm.Read(list[1], c) && c == "first");
        CHECK(!Exists(file + ".tmp"));                               // the atomic write left no litter

        // Saving identical text twice makes one backup, not two.
        now += 10;
        CHECK(bm.SaveWithBackup(file, "third"));
        CHECK(bm.SaveWithBackup(file, "third"));
        CHECK(bm.List(file).size() == 3);

        // Binary content survives.
        const std::string bin("a\0b\r\n\xff", 6);
        now += 10;
        CHECK(bm.SaveWithBackup(file, bin));
        now += 10;
        CHECK(bm.SaveWithBackup(file, "after binary"));
        list = bm.List(file);
        CHECK(!list.empty() && bm.Read(list[0], c) && c == bin);

        // Writing into a missing folder fails and says so.
        CHECK(!bm.SaveWithBackup(d.Sub("nope/missing/Scene.okayscene"), "x"));
        CHECK(!bm.LastError().empty());
    }

    // --- Rotation: only the newest N survive -------------------------------------------------------------------------------
    {
        TempDir d("bk_rot");
        const std::string file = d.Sub("proj.okayproj");
        BackupManager::Config cfg;
        cfg.directory = d.Sub("bak");
        cfg.policy.keepLast = 3;
        cfg.policy.keepDailyDays = 0;
        BackupManager bm(cfg);
        bm.SetClock(clock);
        std::int64_t t = 1700000000;
        for (int i = 0; i < 8; ++i) {
            now = t;
            t += 60;
            CHECK(bm.SaveWithBackup(file, "state " + std::to_string(i)));
        }
        auto list = bm.List(file);
        CHECK(list.size() == 3);                                     // backups hold states 0..6; 4,5,6 are the newest
        std::string c;
        CHECK(list.size() == 3 && bm.Read(list[0], c) && c == "state 6" && bm.Read(list[1], c) && c == "state 5" &&
              bm.Read(list[2], c) && c == "state 4");
        CHECK(ReadAll(file) == "state 7");

        // Lowering the limit and pruning explicitly.
        BackupPolicy p = cfg.policy;
        p.keepLast = 1;
        bm.SetPolicy(p);
        CHECK(bm.Prune(file) == 2);
        CHECK(bm.List(file).size() == 1);
        CHECK(bm.Prune(file) == 0);
    }

    // --- Policy end to end: last N + one per day for M days --------------------------------------------------------------------
    {
        TempDir d("bk_daily");
        const std::string file = d.Sub("scene.okayscene");
        const std::int64_t day0 = 19600 * kDay;
        BackupManager::Config cfg;
        cfg.directory = d.Sub("bak");
        cfg.policy.keepLast = 100;                                   // keep everything while creating
        cfg.autoPrune = false;
        BackupManager bm(cfg);
        bm.SetClock(clock);
        for (int day = 0; day < 10; ++day) {
            for (int h : {1, 5, 9}) {
                now = day0 + day * kDay + h * 3600;
                WriteRaw(file, "day " + std::to_string(day) + " hour " + std::to_string(h));
                CHECK(bm.Backup(file));
            }
        }
        CHECK(bm.List(file).size() == 30);
        now = day0 + 9 * kDay + 12 * 3600;                            // today is day 9, noon
        BackupPolicy p;
        p.keepLast = 2;
        p.keepDailyDays = 4;                                          // days 9, 8, 7, 6
        bm.SetPolicy(p);
        CHECK(bm.Prune(file) == 25);
        auto list = bm.List(file);
        CHECK(list.size() == 5);
        std::vector<std::string> kept;
        for (const auto& b : list) { std::string c; bm.Read(b, c); kept.push_back(c); }
        CHECK((kept == std::vector<std::string>{"day 9 hour 9", "day 9 hour 5", "day 8 hour 9", "day 7 hour 9", "day 6 hour 9"}));

        // Tomorrow the oldest of those days drops out of the window.
        now += kDay;
        CHECK(bm.Prune(file) == 1);
        CHECK(bm.List(file).size() == 4);
    }

    // --- Size cap ----------------------------------------------------------------------------------------------------------------
    {
        TempDir d("bk_cap");
        const std::string file = d.Sub("big.okayscene");
        BackupManager::Config cfg;
        cfg.directory = d.Sub("bak");
        cfg.policy.keepLast = 10;
        cfg.policy.keepDailyDays = 0;
        cfg.policy.maxTotalBytes = 250;
        BackupManager bm(cfg);
        bm.SetClock(clock);
        for (int i = 0; i < 6; ++i) {
            now = 1700000000 + i * 5;
            WriteRaw(file, std::string(100, (char)('a' + i)));
            CHECK(bm.Backup(file));
        }
        auto list = bm.List(file);
        CHECK(list.size() == 2);                                      // 2 * 100 <= 250 < 3 * 100
        std::string c;
        CHECK(list.size() == 2 && bm.Read(list[0], c) && c == std::string(100, 'f'));

        // A file larger than the cap: the newest backup stays regardless.
        TempDir d2("bk_cap2");
        BackupManager::Config c2 = cfg;
        c2.directory = d2.Sub("bak");
        c2.policy.maxTotalBytes = 10;
        BackupManager b2(c2);
        b2.SetClock(clock);
        WriteRaw(file, std::string(100, 'x'));
        CHECK(b2.Backup(file));
        CHECK(b2.List(file).size() == 1);
    }

    // --- Restore ---------------------------------------------------------------------------------------------------------------------
    {
        TempDir d("bk_restore");
        const std::string file = d.Sub("Level.okayscene");
        BackupManager::Config cfg;
        cfg.directory = d.Sub("bak");
        cfg.policy.keepLast = 50;
        BackupManager bm(cfg);
        bm.SetClock(clock);
        for (int i = 0; i < 5; ++i) {
            now = 1700000000 + i * 100;
            CHECK(bm.SaveWithBackup(file, "state " + std::to_string(i)));
        }
        auto list = bm.List(file);
        CHECK(list.size() == 4);                                      // states 0..3 are backed up; 4 is current
        CHECK(ReadAll(file) == "state 4");

        now = 1700001000;
        CHECK(bm.Restore(list[2], file));                             // list[2] = state 1
        CHECK(ReadAll(file) == "state 1");
        CHECK(bm.List(file).size() == 5);                             // the replaced "state 4" was kept first
        std::string c;
        CHECK(bm.Read(bm.List(file)[0], c) && c == "state 4");
        CHECK(!Exists(file + ".tmp"));

        // The restore itself can be undone by restoring the newest backup.
        now = 1700001100;
        CHECK(bm.Restore(bm.List(file)[0], file));
        CHECK(ReadAll(file) == "state 4");

        // A backup that has vanished can't be restored; the file stays as it was.
        auto victim = bm.List(file).back();
        CHECK(bm.Delete(victim));
        CHECK(!Exists(victim.path));
        CHECK(!bm.Delete(victim));
        const std::size_t count = bm.List(file).size();
        CHECK(!bm.Restore(victim, file));
        CHECK(!bm.LastError().empty());
        CHECK(ReadAll(file) == "state 4" && bm.List(file).size() == count);
        std::string unused;
        CHECK(!bm.Read(victim, unused));

        // If the file being replaced can't be backed up first, the restore is refused.
        {
            BackupManager::Config tight = cfg;
            tight.maxFileBytes = 10;
            BackupManager tm(tight);
            tm.SetClock(clock);
            WriteRaw(file, std::string(50, 'z'));                     // current file is over tm's limit
            auto some = bm.List(file);
            CHECK(!some.empty());
            CHECK(!tm.Restore(some[0], file));
            CHECK(ReadAll(file) == std::string(50, 'z'));
            WriteRaw(file, "state 4");
        }

        // Restoring when the project file has been deleted just writes it.
        std::error_code ec;
        fs::remove(file, ec);
        CHECK(bm.Restore(bm.List(file)[1], file));
        CHECK(Exists(file));

        // Without backupBeforeRestore nothing extra is written.
        BackupManager::Config c2 = cfg;
        c2.backupBeforeRestore = false;
        BackupManager b2(c2);
        b2.SetClock(clock);
        const std::size_t n = b2.List(file).size();
        now += 50;
        CHECK(b2.Restore(b2.List(file)[0], file));
        CHECK(b2.List(file).size() == n);
    }

    // --- Same file name in different folders; unrelated files in the backup folder ---------------------------------------------------------
    {
        TempDir d("bk_names");
        fs::create_directories(d.Sub("a"));
        fs::create_directories(d.Sub("b"));
        const std::string fa = d.Sub("a/Main.okayscene");
        const std::string fb = d.Sub("b/Main.okayscene");
        BackupManager::Config cfg;
        cfg.directory = d.Sub("bak");
        cfg.policy.keepLast = 2;
        cfg.policy.keepDailyDays = 0;
        BackupManager bm(cfg);
        bm.SetClock(clock);
        for (int i = 0; i < 4; ++i) {
            now = 1700000000 + i * 10;
            bm.SaveWithBackup(fa, "A" + std::to_string(i));
            bm.SaveWithBackup(fb, "B" + std::to_string(i));
        }
        auto la = bm.List(fa), lb = bm.List(fb);
        CHECK(la.size() == 2 && lb.size() == 2);                      // each folder's file has its own history
        std::string c;
        CHECK(!la.empty() && bm.Read(la[0], c) && c == "A2");
        CHECK(!lb.empty() && bm.Read(lb[0], c) && c == "B2");
        CHECK(!la.empty() && la[0].source == "Main.okayscene");

        // Junk in the folder is ignored.
        WriteRaw(d.Sub("bak/notes.txt"), "hi");
        WriteRaw(d.Sub("bak/x.bak"), "no stamp");
        WriteRaw(d.Sub("bak/a.b.c.bak"), "bad stamp");
        WriteRaw(d.Sub("bak/f.12345678.99999999-999999.bak"), "impossible date");
        WriteRaw(d.Sub("bak/g.1234567.20231114-221320.bak"), "short hash");
        CHECK(bm.ListAll().size() == 4);
        CHECK(bm.List(fa).size() == 2);

        // PruneAll applies the policy to every file's history.
        BackupManager::Config c2 = cfg;
        c2.policy.keepLast = 1;
        BackupManager b2(c2);
        b2.SetClock(clock);
        CHECK(b2.PruneAll() == 2);
        CHECK(b2.List(fa).size() == 1 && b2.List(fb).size() == 1);
        CHECK(Exists(d.Sub("bak/notes.txt")));                         // not ours, not touched
    }

    // --- Limits and errors ---------------------------------------------------------------------------------------------------------------------
    {
        TempDir d("bk_limits");
        const std::string file = d.Sub("huge.okayscene");
        BackupManager::Config cfg;
        cfg.directory = d.Sub("bak");
        cfg.maxFileBytes = 10;
        BackupManager bm(cfg);
        bm.SetClock(clock);
        WriteRaw(file, std::string(50, 'x'));
        CHECK(!bm.Backup(file));
        CHECK(bm.LastError().find("too large") != std::string::npos);
        CHECK(bm.List(file).empty());
        // The save still happens (a backup is a courtesy) ...
        CHECK(bm.SaveWithBackup(file, "small"));
        CHECK(ReadAll(file) == "small");
        // ... unless the caller insists on one.
        WriteRaw(file, std::string(50, 'y'));
        BackupManager::Config strict = cfg;
        strict.requireBackup = true;
        BackupManager sm(strict);
        sm.SetClock(clock);
        CHECK(!sm.SaveWithBackup(file, "new"));
        CHECK(ReadAll(file) == std::string(50, 'y'));                 // untouched

        // No directory configured.
        BackupManager none;
        CHECK(!none.Backup(file) && !none.LastError().empty());
        CHECK(none.List(file).empty() && none.ListAll().empty());

        // The backup folder can't be created (a file is in the way).
        WriteRaw(d.Sub("blocker"), "file");
        BackupManager::Config blocked;
        blocked.directory = d.Sub("blocker/bak");
        BackupManager bb(blocked);
        WriteRaw(file, "ok");
        CHECK(!bb.Backup(file));
        CHECK(!bb.LastError().empty());
        CHECK(bb.SaveWithBackup(file, "still saved"));
        CHECK(ReadAll(file) == "still saved");
    }

    // --- The system clock is used when none is injected -------------------------------------------------------------------------------------------------
    {
        TempDir d("bk_sysclock");
        const std::string file = d.Sub("f.txt");
        WriteRaw(file, "x");
        BackupManager::Config cfg;
        cfg.directory = d.Sub("bak");
        BackupManager bm(cfg);
        CHECK(bm.Backup(file));
        auto list = bm.List(file);
        CHECK(list.size() == 1 && list[0].timestamp > 1600000000);
    }

    TEST_MAIN_RESULT();
}
