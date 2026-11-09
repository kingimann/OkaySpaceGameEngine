// Saves and prefs, hardening pass: the shared crash-safe file write, SaveFile's end marker,
// checksum and backup, the Save facade's errors and locking, Prefs' recovery and locking, and
// SaveGame's slot listing (docs/save_games.md, "What changed (hardening pass)").
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Core/SaveData.hpp"
#include "okay/Core/SaveGame.hpp"
#include <atomic>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <limits>
#include <string>
#include <thread>
#include <vector>
#if defined(__linux__)
#  include <unistd.h>
#endif

using namespace okay;
namespace fs = std::filesystem;

namespace {

void WriteText(const fs::path& p, const std::string& s) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f << s;
}
std::string ReadText(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
bool Contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

bool IsRoot() {
#if defined(__linux__)
    return geteuid() == 0;
#else
    return true;
#endif
}

} // namespace

int main() {
    RUN_SUITE("save_hardening");
    const fs::path dir = fs::temp_directory_path() / "okay_test_save_hardening";
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::error_code ec;

    // ---- The shared write: success, reasons, nothing half written ----
    {
        const std::string p = (dir / "w" / "a.txt").string();
        std::string err;
        CHECK(!savefmt::AtomicWriteChecked(p, "x", &err));                   // no such folder
        CHECK(Contains(err, "no such folder"));
        CHECK(!fs::exists(p) && !fs::exists(p + ".tmp"));
        savefmt::WriteOptions mk;
        mk.createDirs = true;
        CHECK(savefmt::AtomicWriteChecked(p, "hello", &err, mk) && err.empty());
        CHECK(ReadText(p) == "hello" && !fs::exists(p + ".tmp"));
        savefmt::WriteOptions durable;
        durable.sync = true;
        CHECK(savefmt::AtomicWriteChecked(p, std::string("two\0parts", 9), &err, durable));
        CHECK(ReadText(p) == std::string("two\0parts", 9));
        CHECK(savefmt::AtomicWriteChecked(p, "", &err) && ReadText(p).empty());   // empty content is a file, not a failure

        // A folder where the file should go: refused, the folder stays.
        const fs::path d = dir / "w" / "isdir";
        fs::create_directories(d / "inner");
        CHECK(!savefmt::AtomicWriteChecked(d.string(), "x", &err) && Contains(err, "can't replace"));
        CHECK(fs::is_directory(d / "inner") && !fs::exists(d.string() + ".tmp"));
        // The temp name taken by a folder: refused with a reason, the old file survives.
        const fs::path keep = dir / "w" / "keep.txt";
        WriteText(keep, "old");
        fs::create_directories(keep.string() + ".tmp");
        CHECK(!savefmt::AtomicWriteChecked(keep.string(), "new", &err) && Contains(err, "can't create"));
        CHECK(ReadText(keep) == "old");
        fs::remove_all(keep.string() + ".tmp");

#if defined(__linux__)
        // A full disk: the temp file is /dev/full, which fails every write with ENOSPC.
        if (fs::exists("/dev/full")) {
            const fs::path full = dir / "w" / "full.txt";
            WriteText(full, "survivor");
            fs::create_symlink("/dev/full", full.string() + ".tmp", ec);
            CHECK(!ec);
            CHECK(!savefmt::AtomicWriteChecked(full.string(), std::string(100000, 'z'), &err));
            CHECK(Contains(err, "the disk is full"));
            CHECK(ReadText(full) == "survivor");
            CHECK(!fs::exists(full.string() + ".tmp") && !fs::is_symlink(full.string() + ".tmp"));
        }
        // A read-only folder (root ignores the permission, so only checked as another user).
        if (!IsRoot()) {
            const fs::path ro = dir / "ro";
            fs::create_directories(ro);
            WriteText(ro / "f.txt", "kept");
            fs::permissions(ro, fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace, ec);
            CHECK(!savefmt::AtomicWriteChecked((ro / "f.txt").string(), "new", &err) && Contains(err, "permission denied"));
            CHECK(ReadText(ro / "f.txt") == "kept");
            fs::permissions(ro, fs::perms::owner_all, fs::perm_options::replace, ec);
        }
#endif
    }

    // ---- Writers racing on one path take turns: every write succeeds, the file is always whole ----
    {
        const std::string p = (dir / "race.txt").string();
        std::atomic<int> failed{0};
        std::vector<std::thread> ts;
        for (int t = 0; t < 8; ++t)
            ts.emplace_back([&, t] {
                for (int i = 0; i < 60; ++i) {
                    const std::string body(20000 + t, (char)('a' + t));
                    if (!savefmt::AtomicWrite(p, body)) ++failed;
                }
            });
        for (auto& t : ts) t.join();
        CHECK(failed == 0);
        const std::string end = ReadText(p);
        CHECK(end.size() >= 20000 && end.size() < 20008 && end.find_first_not_of(end[0]) == std::string::npos);
        CHECK(end.size() == (std::size_t)(20000 + (end[0] - 'a')));   // one writer's whole payload, not a mix
        CHECK(!fs::exists(p + ".tmp"));
    }

    // ---- Numbers: infinity and NaN survive, ints are clamped, bad vectors give the default ----
    {
        SaveFile s;
        s.SetFloat("inf", std::numeric_limits<float>::infinity());
        s.SetFloat("ninf", -std::numeric_limits<float>::infinity());
        s.SetFloat("nan", std::numeric_limits<float>::quiet_NaN());
        s.SetVec3("v", Vec3{std::numeric_limits<float>::infinity(), 1.5f, -2.0f});
        const std::string p = (dir / "nums.okaysave").string();
        CHECK(s.Save(p));
        SaveFile in;
        CHECK(in.Load(p));
        CHECK(std::isinf(in.GetFloat("inf", 5.0f)) && in.GetFloat("inf") > 0);
        CHECK(std::isinf(in.GetFloat("ninf", 5.0f)) && in.GetFloat("ninf") < 0);
        CHECK(std::isnan(in.GetFloat("nan", 5.0f)));
        CHECK(std::isinf(in.GetVec3("v").x) && in.GetVec3("v").y == 1.5f && in.GetVec3("v").z == -2.0f);
        SaveFile r;
        r.SetRaw("big", "i:99999999999");
        r.SetRaw("small", "i:-99999999999");
        r.SetRaw("exp", "f:1e+09");
        r.SetRaw("frac", "f:3.75");
        r.SetRaw("text", "s:abc");
        r.SetRaw("short", "v:1,2");
        CHECK(r.GetInt("big") == std::numeric_limits<int>::max());
        CHECK(r.GetInt("small") == std::numeric_limits<int>::min());
        CHECK(r.GetInt("exp") == 1000000000);
        CHECK(r.GetInt("frac") == 3);
        CHECK(r.GetInt("text") == 0);
        CHECK(r.GetVec3("short", Vec3{9, 9, 9}).x == 9.0f);            // not three numbers: the default
    }

    // ---- SaveFile: end marker, checksum, backup, damaged files ----
    {
        const std::string p = (dir / "f.okaysave").string();
        SaveFile s;
        s.SetInt("coins", 120);
        s.SetString("name", "Hero");
        s.SetString("h\xC3\xA9ros-\xE5\xB1\xB1\xE7\x94\xB0", "Gr\xC3\xBC\xC3\x9F" "e \xF0\x9F\x98\x80");   // unicode keys and values
        s.SetString("nul", std::string("a\0b", 3));
        s.SetString("big", std::string(2 * 1024 * 1024, 'x') + "end");
        CHECK(s.Save(p));
        const std::string text = ReadText(p);
        CHECK(text.compare(0, 11, "okaysave 3\n") == 0);
        CHECK(Contains(text, "\n#end\n#sum "));
        SaveFile in;
        CHECK(in.Load(p) && in.LastError().empty() && !in.LoadedFromBackup());
        CHECK(in.GetInt("coins") == 120);
        CHECK(in.GetString("h\xC3\xA9ros-\xE5\xB1\xB1\xE7\x94\xB0") == "Gr\xC3\xBC\xC3\x9F" "e \xF0\x9F\x98\x80");
        CHECK(in.GetString("nul") == std::string("a\0b", 3));
        CHECK(in.GetString("big").size() == 2 * 1024 * 1024 + 3);
        CHECK(!fs::exists(p + ".bak"));                            // the first save has nothing to back up
        CHECK(s.Save(p) && ReadText(p) == text);                   // the same keys write the same bytes

        // Cut short (also inside a line): refused, with a reason; no backup to fall back on.
        const std::string cut = (dir / "cut.okaysave").string();
        WriteText(cut, text.substr(0, 200));
        SaveFile c;
        CHECK(!c.Load(cut) && Contains(c.LastError(), "cut short") && c.Count() == 0);
        // Edited: the checksum says so.
        std::string edited = text;
        edited.replace(edited.find("coins"), 5, "c0ins");
        const std::string ed = (dir / "edited.okaysave").string();
        WriteText(ed, edited);
        SaveFile e;
        CHECK(!e.Load(ed) && Contains(e.LastError(), "checksum"));
        // A zero-filled file (a power cut on some file systems) is not "an empty save".
        const std::string zero = (dir / "zero.okaysave").string();
        WriteText(zero, std::string(80, '\0'));
        SaveFile z;
        CHECK(!z.Load(zero) && Contains(z.LastError(), "isn't a save"));
        // A missing file: false, and no error text (it is just "no save yet").
        SaveFile m;
        CHECK(!m.Load((dir / "none.okaysave").string()) && m.LastError().empty());
        // A file saved with CR LF line ends (a tool changed them) still loads.
        std::string crlf;
        for (char ch : text) { if (ch == '\n') crlf += '\r'; crlf += ch; }
        const std::string cr = (dir / "crlf.okaysave").string();
        WriteText(cr, crlf);
        SaveFile k;
        CHECK(k.Load(cr) && k.GetInt("coins") == 120);

        // Saving over keeps the previous good file as .bak; a damaged main falls back to it.
        s.SetInt("coins", 130);
        CHECK(s.Save(p));
        CHECK(fs::exists(p + ".bak") && ReadText(p + ".bak") == text);
        std::string main = ReadText(p);
        WriteText(p, main.substr(0, main.size() - 30));
        SaveFile b;
        CHECK(b.Load(p) && b.LoadedFromBackup() && b.GetInt("coins") == 120);
        CHECK(Contains(b.LastError(), "backup"));
        // Saving after that does not let the damaged file replace the good backup.
        b.SetInt("coins", 140);
        CHECK(b.Save(p));
        CHECK(ReadText(p + ".bak") == text);
        SaveFile after;
        CHECK(after.Load(p) && !after.LoadedFromBackup() && after.GetInt("coins") == 140);
        // A damaged file with no good backup is set aside, not silently overwritten.
        const std::string lone = (dir / "lone.okaysave").string();
        WriteText(lone, "okaysave 3\nhalf=i:1");
        SaveFile n;
        n.SetInt("fresh", 1);
        CHECK(n.Save(lone));
        CHECK(ReadText(lone + ".corrupt") == "okaysave 3\nhalf=i:1");
        // Files written before the end marker load as they did.
        const std::string old = (dir / "old2.okaysave").string();
        WriteText(old, "okaysave 2\nkeep=i:7\nname=s:a\\nb\n");
        SaveFile o;
        CHECK(o.Load(old) && o.GetInt("keep") == 7 && o.GetString("name") == "a\nb");
        CHECK(o.Save(old) && ReadText(old).compare(0, 11, "okaysave 3\n") == 0 && ReadText(old + ".bak") == "okaysave 2\nkeep=i:7\nname=s:a\\nb\n");
    }

    // ---- The Save facade: folders made, failures reported, safe from several threads ----
    {
        Save::SetBaseDir((dir / "base").string());
        Save::SetInt("a", 1, "slots/one/data.okaysave");               // the folders do not exist yet
        CHECK(Save::LastError().empty() && fs::exists(dir / "base" / "slots" / "one" / "data.okaysave"));
        CHECK(Save::GetInt("a", 0, "slots/one/data.okaysave") == 1);
        // A folder that can't be made: the value stays in memory, the reason is readable.
        WriteText(dir / "blocker", "a file");
        Save::SetBaseDir((dir / "blocker" / "inner").string());
        Save::SetInt("lost", 5);
        CHECK(!Save::LastError().empty() && Save::GetInt("lost") == 5);
        CHECK(!Save::Flush(Save::DefaultFile()));
        Save::SetBaseDir((dir / "base").string());
        Save::SetInt("ok", 1, "after.okaysave");
        CHECK(Save::LastError().empty());
        // A damaged file reads from its backup; deleting the file takes the backup with it.
        Save::SetInt("x", 1, "dmg.okaysave");
        Save::SetInt("x", 2, "dmg.okaysave");                           // .bak now holds x = 1
        const fs::path dmg = dir / "base" / "dmg.okaysave";
        CHECK(fs::exists(dmg.string() + ".bak"));
        const std::string body = ReadText(dmg);
        WriteText(dmg, body.substr(0, body.size() - 10));
        CHECK(Save::Reload("dmg.okaysave") && Save::GetInt("x", 0, "dmg.okaysave") == 1);
        CHECK(Contains(Save::LastError(), "backup"));
        Save::SetInt("x", 3, "dmg.okaysave");                           // saving heals it
        CHECK(Save::Reload("dmg.okaysave") && Save::GetInt("x", 0, "dmg.okaysave") == 3 && Save::LastError().empty());
        CHECK(Save::DeleteFile("dmg.okaysave") && !fs::exists(dmg) && !fs::exists(dmg.string() + ".bak"));
        CHECK(!Save::Reload("dmg.okaysave") && !Save::Has("x", "dmg.okaysave"));

        // Several threads setting and reading keys of one file.
        std::vector<std::thread> ts;
        std::atomic<int> wrong{0};
        for (int t = 0; t < 4; ++t)
            ts.emplace_back([&, t] {
                for (int i = 0; i < 40; ++i) {
                    const std::string key = "t" + std::to_string(t);
                    Save::SetInt(key, i, "threads.okaysave");
                    if (Save::GetInt(key, -1, "threads.okaysave") < 0) ++wrong;
                }
            });
        for (auto& t : ts) t.join();
        CHECK(wrong == 0);
        SaveFile fresh;
        CHECK(fresh.Load((dir / "base" / "threads.okaysave").string()) && fresh.Count() == 4);
        for (int t = 0; t < 4; ++t) CHECK(fresh.GetInt("t" + std::to_string(t)) == 39);
        Save::SetBaseDir("");
    }

    // ---- Prefs: CR LF files, zero-filled files, backup, unicode, large values, threads ----
    {
        Prefs::Clear();
        const std::string crlf = (dir / "crlf.okayprefs").string();
        WriteText(crlf, "okayprefs 2\r\nk\tv\r\nmulti\ta\\nb\r\n#end\r\n");
        CHECK(Prefs::Load(crlf));
        CHECK(Prefs::GetString("k") == "v" && Prefs::GetString("multi") == "a\nb");

        Prefs::Clear();
        Prefs::SetString("keep", "me");
        const std::string zero = (dir / "zero.okayprefs").string();
        WriteText(zero, std::string(40, '\0'));
        CHECK(!Prefs::Load(zero) && Contains(Prefs::LastError(), "isn't a prefs file"));
        CHECK(Prefs::GetString("keep") == "me");                        // the live keys are untouched

        // Unicode keys and values, a 1 MB value, and the same keys write the same bytes.
        Prefs::Clear();
        Prefs::SetString("n\xC3\xA4me-\xE5\xB1\xB1", "v\xC3\xA4lue \xF0\x9F\x98\x80");
        Prefs::SetString("big", std::string(1024 * 1024, 'p') + "!");
        Prefs::SetInt("n", 3);
        const std::string p = (dir / "p.okayprefs").string();
        CHECK(Prefs::Save(p) && Prefs::LastError().empty());
        const std::string first = ReadText(p);
        CHECK(Prefs::Save(p) && ReadText(p) == first);
        Prefs::Clear();
        CHECK(Prefs::Load(p));
        CHECK(Prefs::GetString("n\xC3\xA4me-\xE5\xB1\xB1") == "v\xC3\xA4lue \xF0\x9F\x98\x80");
        CHECK(Prefs::GetString("big").size() == 1024 * 1024 + 1);

        // The previous good file is the .bak; a cut-off main falls back to it.
        Prefs::SetInt("n", 4);
        CHECK(Prefs::Save(p));
        CHECK(ReadText(p + ".bak") == first);
        const std::string main = ReadText(p);
        WriteText(p, main.substr(0, main.size() - 5));                  // the end line is cut
        Prefs::SetInt("n", 99);
        CHECK(Prefs::Load(p) && Prefs::LastLoadUsedBackup() && Prefs::GetInt("n") == 3);
        CHECK(Contains(Prefs::LastError(), "backup"));
        CHECK(Prefs::Save(p) && ReadText(p + ".bak") == first);        // a damaged file never replaces the good backup
        CHECK(Prefs::Load(p) && !Prefs::LastLoadUsedBackup() && Prefs::LastError().empty());
        // No backup and a cut-off file: refused, live keys stay (as before).
        const std::string lone = (dir / "lone.okayprefs").string();
        WriteText(lone, "okayprefs 2\nk\tv\n");
        Prefs::SetString("live", "1");
        CHECK(!Prefs::Load(lone) && Prefs::GetString("live") == "1" && !Prefs::LastLoadUsedBackup());
        // A save that fails says why.
        CHECK(!Prefs::Save((dir / "no" / "such" / "p.okayprefs").string()) && Contains(Prefs::LastError(), "no such folder"));
        CHECK(Prefs::Save(p) && Prefs::LastError().empty());

        // A float stored as text reads as an int.
        Prefs::SetFloat("bigf", 1.0e9f);
        CHECK(Prefs::GetInt("bigf") == 1000000000);
        Prefs::SetString("huge", "99999999999");
        CHECK(Prefs::GetInt("huge", -7) == -7);

        // Threads: setting, reading, saving and listening at once, while a reader checks the
        // file is never half written.
        Prefs::Clear();
        std::atomic<int> heard{0};
        const int id = Prefs::AddListener([&](const std::string&) { ++heard; });
        const std::string tp = (dir / "threads.okayprefs").string();
        std::atomic<bool> done{false};
        std::atomic<int> torn{0};
        std::thread reader([&] {
            while (!done) {
                const std::string text = ReadText(tp);
                if (text.empty()) continue;
                if (text.compare(0, 12, "okayprefs 2\n") != 0 || text.size() < 5 || text.compare(text.size() - 5, 5, "#end\n") != 0) ++torn;
            }
        });
        std::vector<std::thread> ts;
        std::atomic<int> wrong{0};
        for (int t = 0; t < 4; ++t)
            ts.emplace_back([&, t] {
                for (int i = 1; i <= 100; ++i) {
                    const std::string key = "k" + std::to_string(t);
                    Prefs::SetInt(key, i);
                    if (Prefs::GetInt(key, -1) < 1) ++wrong;
                    if (i % 10 == 0) Prefs::Save(tp);
                }
            });
        for (auto& t : ts) t.join();
        done = true;
        reader.join();
        Prefs::RemoveListener(id);
        CHECK(wrong == 0 && heard == 400 && torn == 0);
        CHECK(Prefs::Save(tp));
        Prefs::Clear();
        CHECK(Prefs::Load(tp));
        for (int t = 0; t < 4; ++t) CHECK(Prefs::GetInt("k" + std::to_string(t)) == 100);
        Prefs::Clear();
    }

    // ---- SaveGame: slot names, listing, options, damaged and newer slots, failures ----
    Save::SetBaseDir((dir / "game").string());
    {
        CHECK(!SaveGame::ValidSlot("con") && !SaveGame::ValidSlot("CON") && !SaveGame::ValidSlot("nul"));
        CHECK(!SaveGame::ValidSlot("com1") && !SaveGame::ValidSlot("LPT9") && !SaveGame::ValidSlot("aux") && !SaveGame::ValidSlot("prn"));
        CHECK(SaveGame::ValidSlot("console") && SaveGame::ValidSlot("com10") && SaveGame::ValidSlot("slot_1"));

        Scene s("Level");
        s.CreateGameObject("Hero");
        s.Start();
        std::string err;

        // A long label does not push the time out of the header.
        CHECK(SaveGame::Save(s, "long", std::string(20000, 'L'), &err));
        SaveSlotInfo li = SaveGame::Info("long");
        CHECK(li.exists && li.label.size() == 20000 && li.savedAt > 0 && li.scene == "Level" && li.playTime >= 0.0f);
        CHECK(li.sizeBytes > 20000 && !li.corrupt && !li.incompatible && !li.fromBackup);

        // Thumbnail, game version and custom fields come back in Info and List.
        SaveOptions so;
        so.label = "Chapter 2";
        so.thumbnail = "shot.png";
        so.gameVersion = "1.4.2";
        so.fields["difficulty"] = "hard\tmode";
        so.fields["place"] = "The Mine\n(deep)";
        WriteText(fs::path(Save::Resolve("saves/shot.png")), "png");
        CHECK(SaveGame::Save(s, "opt", so, &err));
        SaveSlotInfo oi = SaveGame::Info("opt");
        CHECK(oi.label == "Chapter 2" && oi.gameVersion == "1.4.2");
        CHECK(oi.fields.size() == 2 && oi.fields["difficulty"] == "hard\tmode" && oi.fields["place"] == "The Mine\n(deep)");
        CHECK(oi.thumbnail == Save::Resolve("saves/shot.png") && fs::exists(oi.thumbnail));
        CHECK(oi.savedAtMs == oi.savedAt * 1000);
        CHECK(Contains(SaveGame::ThumbnailPath("opt"), "saves/opt.png"));
        bool inList = false;
        for (const auto& e : SaveGame::List()) if (e.slot == "opt") inList = e.gameVersion == "1.4.2" && e.fields.size() == 2;
        CHECK(inList);
        // Delete removes a bare-name thumbnail with the slot, but never a path the game gave.
        const fs::path mine = dir / "mine.png";
        WriteText(mine, "png");
        so.thumbnail = mine.string();
        CHECK(SaveGame::Save(s, "own", so, &err));
        CHECK(SaveGame::Info("own").thumbnail == mine.string());
        CHECK(SaveGame::Delete("own") && fs::exists(mine));
        CHECK(SaveGame::Delete("opt") && !fs::exists(Save::Resolve("saves/shot.png")));
        // A slot saved without options reads as before.
        CHECK(SaveGame::Save(s, "plain", "Plain", &err));
        const SaveSlotInfo pi = SaveGame::Info("plain");
        CHECK(pi.label == "Plain" && pi.thumbnail.empty() && pi.gameVersion.empty() && pi.fields.empty());

        // Damaged slots are listed and flagged, never hidden.
        WriteText(SaveGame::SlotPath("junk"), "hello\n");
        const SaveSlotInfo ji = SaveGame::Info("junk");
        CHECK(ji.exists && ji.corrupt && !ji.error.empty());
        // A file cut short after its header: the header-only read shows it, the verified read flags it.
        const std::string whole = ReadText(SaveGame::SlotPath("plain"));
        WriteText(SaveGame::SlotPath("cut"), whole.substr(0, whole.size() / 2));
        CHECK(!SaveGame::Info("cut").corrupt && SaveGame::Info("cut").label == "Plain");
        CHECK(SaveGame::Info("cut", true).corrupt && Contains(SaveGame::Info("cut", true).error, "cut short"));
        CHECK(!SaveGame::Check("cut", &err) && Contains(err, "cut short"));
        CHECK(SaveGame::Check("plain") && !SaveGame::Check("nothing-here", &err) && Contains(err, "No save"));
        bool sawJunk = false, sawCut = false;
        for (const auto& e : SaveGame::List(true)) {
            if (e.slot == "junk") sawJunk = e.corrupt;
            if (e.slot == "cut") sawCut = e.corrupt;
        }
        CHECK(sawJunk && sawCut);
        // A cut main with a good backup: verified info says the values come from the backup, Check agrees.
        CHECK(SaveGame::Save(s, "plain", "Second", &err));             // "Plain" is now the .bak
        const std::string second = ReadText(SaveGame::SlotPath("plain"));
        WriteText(SaveGame::SlotPath("plain"), second.substr(0, second.size() / 2));
        const SaveSlotInfo bi = SaveGame::Info("plain", true);
        CHECK(bi.fromBackup && !bi.corrupt && bi.label == "Plain" && Contains(bi.error, "backup"));
        CHECK(SaveGame::Check("plain", &err) && Contains(err, "backup"));

        // A slot from a newer build: listed and flagged, never loaded, and kept when saved over.
        const std::string future = "okaygame 99\nlabel\tfrom the future\nsavedAt\t4102444800\n--scene--\n--state--\n";
        WriteText(SaveGame::SlotPath("future"), future);
        const SaveSlotInfo fi = SaveGame::Info("future");
        CHECK(fi.exists && fi.incompatible && !fi.corrupt && fi.label == "from the future" && Contains(fi.error, "newer"));
        bool sawFuture = false;
        for (const auto& e : SaveGame::List()) if (e.slot == "future") sawFuture = e.incompatible;
        CHECK(sawFuture);
        CHECK(!SaveGame::LoadNow(s, "future", &err) && Contains(err, "newer"));
        CHECK(SaveGame::Save(s, "future", "now older", &err));
        CHECK(ReadText(SaveGame::SlotPath("future") + ".newer.bak") == future);
        CHECK(SaveGame::Info("future").label == "now older" && !SaveGame::Info("future").incompatible);
        CHECK(SaveGame::Save(s, "future", "again", &err));              // the kept copy is not replaced by an older one
        CHECK(ReadText(SaveGame::SlotPath("future") + ".newer.bak") == future);
        CHECK(SaveGame::Delete("future") && !fs::exists(SaveGame::SlotPath("future") + ".newer.bak"));

        // Newest first, damaged ones (no time) last.
        const auto list = SaveGame::List();
        CHECK(!list.empty());
        bool ordered = true;
        for (std::size_t i = 1; i < list.size(); ++i) if (list[i - 1].savedAt < list[i].savedAt) ordered = false;
        CHECK(ordered && list.back().savedAt == 0);

        // Failures say why and leave the slot as it was.
        CHECK(SaveGame::Save(s, "keepme", "first", &err));
        const std::string keptText = ReadText(SaveGame::SlotPath("keepme"));
        fs::create_directories(SaveGame::SlotPath("keepme") + ".tmp");
        CHECK(!SaveGame::Save(s, "keepme", "second", &err) && Contains(err, "keepme") && Contains(err, "not changed"));
        CHECK(ReadText(SaveGame::SlotPath("keepme")) == keptText);
        fs::remove_all(SaveGame::SlotPath("keepme") + ".tmp");
#if defined(__linux__)
        if (fs::exists("/dev/full")) {
            fs::create_symlink("/dev/full", SaveGame::SlotPath("keepme") + ".tmp", ec);
            CHECK(!SaveGame::Save(s, "keepme", "third", &err) && Contains(err, "the disk is full"));
            CHECK(ReadText(SaveGame::SlotPath("keepme")) == keptText);
            CHECK(SaveGame::Save(s, "keepme", "fourth", &err) && SaveGame::Info("keepme").label == "fourth");   // the disk came back
        }
#endif
        CHECK(!SaveGame::Save(s, "con", "x", &err) && Contains(err, "valid slot name"));
    }
    Save::SetBaseDir("");

    fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
