// A damaged FIRST LINE in a prefs file (`game.okayprefs`) or a save-framework slot (`*.okayslot`) must still be
// noticed (the follow-up to #888 finding 47, which fixed the same hole in SaveFile and SaveGame).
//
// Prefs used to read its first line with atoi: `okayprefs 2` with a damaged digit read as version 1, and a
// version 1 file is not checked for its `#end` line, so a cut-off file with a damaged first line loaded as if
// it were whole; a clean higher number (`okayprefs 9`) was parsed as if this build knew it. Prefs also had no
// checksum, so any changed byte in a key or value loaded silently. Now Prefs writes `okayprefs 3` with `#end`
// and `#sum <16 hex>` (the same trailer as SaveFile), checks the trailer whenever it is there, refuses a first
// line with no clean version, and refuses (and keeps a copy of) a newer file.
//
// A slot read its `okayslot 1` line with atoi too: a damaged digit (`okayslot 7`) made a damaged slot look like
// one from a newer build, so its `.bak` was never used and the damaged file was kept as `.newer.bak`. Now the
// header checksum is checked before a slot is called newer.
//
// Old files that must keep loading, from the repository's history:
//  * Prefs #24: `okayprefs 1`, `key<TAB>value` lines, only `\\` and `\n` escaped (a raw tab in a value);
//  * Prefs #279: `okayprefs 1`, `\t` and `\r` escaped as well;
//  * Prefs #552 and #611: `okayprefs 2` and a last `#end` line;
//  * Prefs files with no header (Load always accepted them): `key<TAB>value` lines only;
//  * slots: `okayslot 1` is the only container version ever written (#572 on), with `headerSum`.
// Written from engine/src/Core/Prefs.cpp and SaveFramework.cpp and those old writers.
#include "test_framework.hpp"
#include "test_sandbox.hpp"
#include <Okay.hpp>
#include "okay/Core/Prefs.hpp"
#include "okay/Core/SaveData.hpp"
#include "okay/Core/SaveFramework.hpp"
#include "okay/Core/Log.hpp"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using namespace okay;
namespace fs = std::filesystem;

namespace {

void WriteText(const std::string& p, const std::string& s) {
    std::error_code ec;
    const fs::path parent = fs::path(p).parent_path();
    if (!parent.empty()) fs::create_directories(parent, ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << s;
}
std::string ReadText(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
bool Contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }
void RemoveFile(const std::string& p) { std::error_code ec; fs::remove(p, ec); }
std::string ReplaceFirst(std::string s, const std::string& from, const std::string& to) {
    const std::size_t at = s.find(from);
    if (at != std::string::npos) s.replace(at, from.size(), to);
    return s;
}

// The live prefs as the bytes Prefs::Save writes for them (sorted keys), so two stores can be compared whole.
std::string PrefsDump(const std::string& dir) {
    const std::string d = dir + "/dump.okayprefs";
    RemoveFile(d);
    RemoveFile(d + ".bak");
    Prefs::Save(d);
    std::string text = ReadText(d);
    RemoveFile(d);
    return text;
}

// `body` (the header and key lines) with a correct `#end` / `#sum` trailer.
std::string Seal(const std::string& body) {
    const std::string upToEnd = body + "#end\n";
    return upToEnd + "#sum " + savefmt::Hex16(savefmt::Fnv1a(upToEnd.data(), upToEnd.size())) + "\n";
}

// A slot container laid out the way every writer since #572 laid it out (built here by hand, line by line).
std::string Container(const std::string& firstLine, int dataVersion, const std::string& label, const std::string& payload) {
    std::string h = firstLine + "\n";
    h += "dataVersion\t" + std::to_string(dataVersion) + "\n";
    h += "gameVersion\t1.0\n";
    h += "savedAtMs\t1700000000000\n";
    h += "playTime\t12.5\n";
    h += "scene\tLevel\n";
    h += "label\t" + label + "\n";
    h += "kind\tmanual\n";
    h += "thumb\t\n";
    h += "payloadBytes\t" + std::to_string(payload.size()) + "\n";
    h += "payloadSum\t" + savefmt::Hex16(savefmt::Fnv1a(payload.data(), payload.size())) + "\n";
    h += "headerSum\t" + savefmt::Hex16(savefmt::Fnv1a(h.data(), h.size())) + "\n";
    return h + payload;
}

SaveDocument Doc(int coins, const std::string& name) {
    SaveDocument d;
    SaveWriter w = d.Writer("player");
    w.Int("coins", coins);
    w.String("name", name);
    d.Writer("world").Float("time", 3.5f);
    return d;
}

} // namespace

int main() {
    RUN_SUITE("prefs_slot_header");
    okaytest::Sandbox sandbox("prefs_slot_header", OKAY_SOURCE_DIR);
    const std::string dir = sandbox.Dir().generic_string();

    std::vector<std::string> warnings;
    Log::console = false;
    Log::sink = [&](Log::Level lv, const std::string& msg) { if (lv == Log::Level::Warning) warnings.push_back(msg); };

    // =====================================================================
    // Prefs
    // =====================================================================
    const std::string pp = dir + "/game.okayprefs";
    // The good backup every case below can fall back to.
    const std::string goodBak = "okayprefs 2\nvolume\t0.25\n#end\n";
    auto loadPrefs = [&](const std::string& main, const std::string& bak) {
        WriteText(pp, main);
        if (bak.empty()) RemoveFile(pp + ".bak"); else WriteText(pp + ".bak", bak);
        Prefs::Clear();
        Prefs::SetString("live", "yes");   // what the game had before the load
        return Prefs::Load(pp);
    };

    // ---- the finding: a damaged first line on a cut-off file ----
    {
        // `okayprefs 2` with its digit damaged, and the `#end` line lost: the file is cut short.
        CHECK(loadPrefs("okayprefs ,\nvolume\t0.9\n", goodBak));
        std::cout << "  damaged first line + cut off: used backup " << Prefs::LastLoadUsedBackup()
                  << ", volume " << Prefs::GetFloat("volume") << " (the backup has 0.25)\n";
        CHECK(Prefs::LastLoadUsedBackup());
        CHECK(Prefs::GetFloat("volume") == 0.25f);
        CHECK(Contains(Prefs::LastError(), "format version"));
        // The same with no backup: refused, the live keys stay.
        CHECK(!loadPrefs("okayprefs ,\nvolume\t0.9\n", ""));
        CHECK(Prefs::GetString("live") == "yes" && !Prefs::Has("volume"));
        // `okayprefs` with nothing after it, a second word, a zero: all damage (no writer ever wrote them).
        for (const char* head : {"okayprefs", "okayprefs ", "okayprefs 0", "okayprefs 2 x", "okayprefs2", "okayprefs -2", "okayprefs  2"}) {
            const bool ok = loadPrefs(std::string(head) + "\nvolume\t0.9\n#end\n", goodBak);
            CHECK(ok && Prefs::LastLoadUsedBackup() && Prefs::GetFloat("volume") == 0.25f);
            if (!(ok && Prefs::LastLoadUsedBackup())) std::cerr << "  header '" << head << "' was not treated as damage\n";
        }
    }

    // ---- a clean newer version is refused, kept, and never loaded ----
    {
        const std::string newer = "okayprefs 9\nvolume\t0.9\nnewKey\tz\n#end\n";
        const bool ok = loadPrefs(newer, goodBak);
        std::cout << "  okayprefs 9: load " << ok << ", used backup " << Prefs::LastLoadUsedBackup() << ", error '" << Prefs::LastError() << "'\n";
        CHECK(!ok);
        CHECK(!Prefs::LastLoadUsedBackup());                         // an older backup would undo the newer build's settings
        CHECK(Prefs::GetString("live") == "yes" && !Prefs::Has("newKey"));
        CHECK(Contains(Prefs::LastError(), "newer"));
        // Saving now replaces it, but a copy is kept for the newer build.
        Prefs::SetFloat("volume", 0.5f);
        CHECK(Prefs::Save(pp));
        CHECK(ReadText(pp + ".newer.bak") == newer);
        CHECK(ReadText(pp + ".bak") == goodBak);                     // the newer file did not become the backup
        CHECK(Prefs::Load(pp) && Prefs::GetFloat("volume") == 0.5f);
        RemoveFile(pp + ".newer.bak");
        // A newer file that has our trailer: checked first, so a good sum is "newer" and a bad one is damage.
        const std::string sealed = Seal("okayprefs 7\nvolume\t0.9\n");
        CHECK(!loadPrefs(sealed, goodBak) && Contains(Prefs::LastError(), "newer"));
        CHECK(loadPrefs(ReplaceFirst(sealed, "0.9", "0.8"), goodBak) && Prefs::LastLoadUsedBackup());
        // A huge number is newer, not damage.
        CHECK(!loadPrefs("okayprefs 99999999999999\nvolume\t0.9\n", goodBak) && Contains(Prefs::LastError(), "newer"));
    }

    // ---- the trailer is checked whatever the first line says ----
    {
        Prefs::Clear();
        Prefs::SetFloat("volume", 0.75f);
        Prefs::SetString("name", "a\tb\nc\\d");
        CHECK(Prefs::Save(pp));
        const std::string saved = ReadText(pp);
        CHECK(saved.compare(0, 12, "okayprefs 3\n") == 0);
        CHECK(Contains(saved, "\n#end\n#sum "));
        CHECK(loadPrefs(saved, "") && !Prefs::LastLoadUsedBackup() && Prefs::GetFloat("volume") == 0.75f &&
              Prefs::GetString("name") == "a\tb\nc\\d" && !Prefs::Has("live"));
        // A changed value: the sum fails, the backup is used, and the message says how to edit by hand.
        const std::string edited = ReplaceFirst(saved, "0.75", "0.95");
        CHECK(loadPrefs(edited, goodBak) && Prefs::LastLoadUsedBackup() && Prefs::GetFloat("volume") == 0.25f);
        CHECK(Contains(Prefs::LastError(), "checksum"));
        // The first line changed to an older version, to no version, or to another word: the sum still runs.
        for (const char* head : {"okayprefs 2", "okayprefs 1", "okayprefz 3", "okayprefs x"}) {
            CHECK(loadPrefs(ReplaceFirst(edited, "okayprefs 3", head), goodBak) && Prefs::LastLoadUsedBackup());
        }
        // Damaged first line and an edited value (the try-it steps), no backup: refused, live keys stay.
        CHECK(!loadPrefs(ReplaceFirst(edited, "okayprefs 3", "okayprefs #"), ""));
        CHECK(Prefs::GetString("live") == "yes");
        // The hand-edit way: first line `okayprefs 2` and the `#sum` line deleted.
        std::string byHand = ReplaceFirst(edited, "okayprefs 3", "okayprefs 2");
        byHand = byHand.substr(0, byHand.find("#sum "));
        CHECK(loadPrefs(byHand, goodBak) && !Prefs::LastLoadUsedBackup() && Prefs::GetFloat("volume") == 0.95f);
        // A version 3 file without its trailer is cut short.
        CHECK(loadPrefs(saved.substr(0, saved.find("#end")), goodBak) && Prefs::LastLoadUsedBackup());
        // A header-less file that has an end line is not the header-less layout (no writer made one).
        CHECK(loadPrefs("volume\t0.9\n#end\n", goodBak) && Prefs::LastLoadUsedBackup());
        // A header-less file must start with a key line.
        CHECK(loadPrefs("okayprefz 2\nvolume\t0.9\n", goodBak) && Prefs::LastLoadUsedBackup());
    }

    // ---- genuine old files still load ----
    {
        // No header (only `key<TAB>value` lines).
        CHECK(loadPrefs("volume\t0.6\nname\tx\n", "") && Prefs::GetFloat("volume") == 0.6f && Prefs::GetString("name") == "x" && !Prefs::Has("live"));
        // #24: okayprefs 1, a raw tab inside a value, only \\ and \n escaped.
        CHECK(loadPrefs("okayprefs 1\nvolume\t0.750000\ngreet\tline1\\nline2\twith\\\\slash\n", "") &&
              Prefs::GetFloat("volume") == 0.75f && Prefs::GetString("greet") == "line1\nline2\twith\\slash");
        // #279: okayprefs 1 with \t and \r escaped.
        CHECK(loadPrefs("okayprefs 1\nk\\tey\ta\\rb\\tc\n", "") && Prefs::GetString("k\tey") == "a\rb\tc");
        // #552 / #611: okayprefs 2 with the end line; with CR LF; with a byte order mark an editor added.
        CHECK(loadPrefs("okayprefs 2\nvolume\t0.4\n#end\n", "") && Prefs::GetFloat("volume") == 0.4f && !Prefs::LastLoadUsedBackup());
        CHECK(loadPrefs("okayprefs 2\r\nvolume\t0.4\r\n#end\r\n", "") && Prefs::GetFloat("volume") == 0.4f);
        CHECK(loadPrefs("\xEF\xBB\xBFokayprefs 2\nvolume\t0.4\n#end\n", "") && Prefs::GetFloat("volume") == 0.4f);
        CHECK(loadPrefs("\xEF\xBB\xBF" + Seal("okayprefs 3\nvolume\t0.3\n"), "") && Prefs::GetFloat("volume") == 0.3f);
        // A version 2 file still has to end with its end line.
        CHECK(loadPrefs("okayprefs 2\nvolume\t0.4\n", goodBak) && Prefs::LastLoadUsedBackup());
        // An empty file is an empty store (as before).
        CHECK(loadPrefs("", "") && !Prefs::Has("live"));
    }

    // ---- Prefs fuzz: 5000 single-byte changes of a file this build wrote ----
    {
        Prefs::Clear();
        Prefs::SetFloat("volume", 0.25f);
        CHECK(Prefs::Save(pp));
        const std::string bak = ReadText(pp);   // the first save: the backup
        Prefs::Clear();
        Prefs::SetFloat("volume", 0.75f);
        Prefs::SetInt("score", 1234);
        Prefs::SetString("name", "Ann\tLee\nline");
        Prefs::SetString("keys.jump", "Space");
        Prefs::SetBool("fullscreen", true);
        CHECK(Prefs::Save(pp));
        const std::string good = ReadText(pp);
        const std::string goodDump = PrefsDump(dir);
        std::mt19937_64 rng(0x5EED0048ull);
        int silentWrong = 0, sameData = 0, backup = 0, refused = 0, newer = 0, headerWrong = 0, shown = 0;
        const std::size_t firstLineEnd = good.find('\n');
        for (int i = 0; i < 5000; ++i) {
            std::string m = good;
            const std::size_t at = (std::size_t)(rng() % m.size());
            const unsigned char old = (unsigned char)m[at];
            const unsigned char now = (unsigned char)((old + 1 + rng() % 255) & 0xFF);   // never the same byte
            m[at] = (char)now;
            const bool ok = loadPrefs(m, bak);
            if (!ok) { ++refused; if (Contains(Prefs::LastError(), "newer")) ++newer; }
            else if (Prefs::LastLoadUsedBackup()) ++backup;
            else if (PrefsDump(dir) == goodDump) ++sameData;
            else {
                ++silentWrong;
                if (at <= firstLineEnd) ++headerWrong;
                if (shown++ < 3) std::cerr << "  prefs silent wrong: byte " << at << " " << (int)old << "->" << (int)now << "\n";
            }
        }
        std::cout << "  prefs fuzz (5000 single-byte changes): silent wrong " << silentWrong << " (" << headerWrong
                  << " in the first line), same data " << sameData << ", backup used " << backup << ", refused " << refused
                  << " (as newer " << newer << ")\n";
        CHECK(silentWrong == 0);
        CHECK(newer == 0);   // a damaged file is never mistaken for one from a newer build
        CHECK(silentWrong + sameData + backup + refused == 5000);
    }
    RemoveFile(pp); RemoveFile(pp + ".bak"); RemoveFile(pp + ".corrupt");

    // =====================================================================
    // SaveStore slots (`okayslot 1`)
    // =====================================================================
    SaveStore store(dir + "/slots");
    store.SetClock([] { return 1700000000000LL; });
    const std::string slot = store.SlotPath("s1");
    std::string err;
    CHECK(store.Write("s1", Doc(10, "first"), SaveSlotInfo(), &err));
    CHECK(store.Write("s1", Doc(20, "second"), SaveSlotInfo(), &err));
    const std::string goodSlot = ReadText(slot), goodSlotBak = ReadText(slot + ".bak");
    CHECK(goodSlot.compare(0, 11, "okayslot 1\n") == 0 && !goodSlotBak.empty());
    auto readSlot = [&](const std::string& main, const std::string& bak, SaveDocument& out, SaveLoadReport& rep, std::string& e) {
        WriteText(slot, main);
        if (bak.empty()) RemoveFile(slot + ".bak"); else WriteText(slot + ".bak", bak);
        RemoveFile(slot + ".newer.bak");
        out = SaveDocument();
        e.clear();
        return store.Read("s1", out, nullptr, &e, &rep);
    };

    // ---- the hole: a damaged version digit made a damaged slot look newer ----
    {
        SaveDocument d; SaveLoadReport rep; std::string e;
        const bool ok = readSlot(ReplaceFirst(goodSlot, "okayslot 1", "okayslot 7"), goodSlotBak, d, rep, e);
        std::cout << "  okayslot 7 (damaged digit): read " << ok << ", used backup " << rep.usedBackup << ", error '" << e << "'\n";
        CHECK(ok && rep.usedBackup && d == Doc(10, "first"));
        const SaveSlotInfo info = store.Info("s1");
        CHECK(!info.incompatible && info.fromBackup);
        // Saving over it: the damaged file is not kept as .newer.bak and does not become the .bak.
        CHECK(store.Write("s1", Doc(30, "third"), SaveSlotInfo(), &err));
        CHECK(!fs::exists(slot + ".newer.bak"));
        CHECK(ReadText(slot + ".bak") == goodSlotBak);
        // Other damaged first lines: no clean number.
        for (const char* head : {"okayslot x", "okayslot 0", "okayslot 1 ", "okayslot  1", "okayslot 1x"}) {
            const bool r = readSlot(ReplaceFirst(goodSlot, "okayslot 1", head), goodSlotBak, d, rep, e);
            CHECK(r && rep.usedBackup);
        }
    }

    // ---- a genuine newer slot is refused, kept and never read ----
    {
        const std::string payload = Doc(99, "future").Serialize();
        const std::string newer = Container("okayslot 2", 1, "from the future", payload);
        SaveDocument d; SaveLoadReport rep; std::string e;
        CHECK(!readSlot(newer, goodSlotBak, d, rep, e) && !rep.usedBackup && Contains(e, "newer"));
        CHECK(store.Info("s1").incompatible);
        // A newer layout that has no headerSum line at all is still "newer".
        CHECK(!readSlot("okayslot 3\nsomething\tnew\n", goodSlotBak, d, rep, e) && !rep.usedBackup && Contains(e, "newer"));
        // Writing over a newer slot keeps it as .newer.bak.
        readSlot(newer, goodSlotBak, d, rep, e);
        CHECK(store.Write("s1", Doc(40, "fourth"), SaveSlotInfo(), &err));
        CHECK(ReadText(slot + ".newer.bak") == newer);
        CHECK(ReadText(slot + ".bak") == goodSlotBak);
        // A newer number whose header sum fails is damage, not newer.
        CHECK(readSlot(ReplaceFirst(newer, "from the future", "from the fuxure"), goodSlotBak, d, rep, e) && rep.usedBackup);
    }

    // ---- the only container version ever written still loads (laid out by hand, as #572 wrote it) ----
    {
        const SaveDocument want = Doc(5, "legacy");
        SaveDocument d; SaveLoadReport rep; std::string e;
        CHECK(readSlot(Container("okayslot 1", 1, "old", want.Serialize()), "", d, rep, e) && !rep.usedBackup && d == want);
        CHECK(store.Info("s1").label == "old");
    }

    // ---- slot fuzz: 5000 single-byte changes ----
    {
        std::mt19937_64 rng(0x5EED0049ull);
        const SaveDocument second = Doc(20, "second"), first = Doc(10, "first");
        int silentWrong = 0, sameData = 0, backup = 0, refused = 0, newer = 0, shown = 0;
        for (int i = 0; i < 5000; ++i) {
            std::string m = goodSlot;
            const std::size_t at = (std::size_t)(rng() % m.size());
            const unsigned char old = (unsigned char)m[at];
            const unsigned char now = (unsigned char)((old + 1 + rng() % 255) & 0xFF);
            m[at] = (char)now;
            SaveDocument d; SaveLoadReport rep; std::string e;
            const bool ok = readSlot(m, goodSlotBak, d, rep, e);
            if (!ok) { ++refused; if (Contains(e, "newer")) ++newer; }
            else if (rep.usedBackup) { ++backup; CHECK(d == first); }
            else if (d == second) ++sameData;
            else {
                ++silentWrong;
                if (shown++ < 3) std::cerr << "  slot silent wrong: byte " << at << " " << (int)old << "->" << (int)now << "\n";
            }
        }
        std::cout << "  slot fuzz (5000 single-byte changes): silent wrong " << silentWrong << ", same data " << sameData
                  << ", backup used " << backup << ", refused " << refused << " (as newer " << newer << ")\n";
        CHECK(silentWrong == 0);
        CHECK(newer == 0);
        CHECK(silentWrong + sameData + backup + refused == 5000);
    }

    Prefs::Clear();
    Log::sink = nullptr;
    Log::console = true;
    CHECK(sandbox.Verify());
    TEST_MAIN_RESULT();
}
