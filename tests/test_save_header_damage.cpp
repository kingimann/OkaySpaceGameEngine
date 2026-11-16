// A save whose FIRST LINE is damaged must still be noticed (issue #888, finding 47 and its addendum).
//
// SaveFile (`save("gold", 120)`, files that start `okaysave 3` and end `#end` / `#sum <16 hex>`) and SaveGame
// (slots that start `okaygame 2` and end `--end--` / `checksum\t<16 hex>`) used to check their end marker and
// checksum only when the first line named the current format. A flipped byte in that line turned the file into
// an "old format" file: no checksum, edited or cut-off data accepted, the good `.bak` never used.
//
// The rules this test holds them to:
//  * a file that has the end marker and checksum line is always checked, whatever its first line says;
//  * a first line that starts `okaysave` / `okaygame` but has no clean version number is damaged;
//  * a clean version above this build's is "newer" and is never parsed;
//  * a headerless SaveFile (the oldest layout) must start with a `key=value` line;
//  * genuine old files (no header, `okaysave 1`, `okaysave 2`, `okaygame 1`) still load.
// Written from the save writers in engine/src/Core/SaveFile.cpp and SaveGame.cpp and the old writers in the
// repository's history (SaveData.hpp before #552 and #611, SaveGame.cpp at #498).
#include "test_framework.hpp"
#include "test_sandbox.hpp"
#include <Okay.hpp>
#include "okay/Core/SaveData.hpp"
#include "okay/Core/SaveGame.hpp"
#include "okay/Core/Log.hpp"
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <string>
#include <vector>

using namespace okay;
namespace fs = std::filesystem;

namespace {

using Data = std::map<std::string, std::string>;

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

Data Snap(const SaveFile& f) { return Data(f.Raw().begin(), f.Raw().end()); }

// What one SaveFile::Load gave.
struct Got {
    bool ok = false;
    bool fromBackup = false;
    Data data;
    std::string error;
};

// Put `main` (and `bak`, or no .bak when it is empty) on disk at `p`, then Load it into a fresh store.
Got LoadFile(const std::string& p, const std::string& main, const std::string& bak) {
    WriteText(p, main);
    if (bak.empty()) RemoveFile(p + ".bak"); else WriteText(p + ".bak", bak);
    SaveFile f;
    Got g;
    g.ok = f.Load(p);
    g.fromBackup = f.LoadedFromBackup();
    g.data = Snap(f);
    g.error = f.LastError();
    return g;
}

// `body` (the header and key lines) with a correct `#end` / `#sum` trailer, as SaveFile writes it.
std::string SealSaveFile(const std::string& body) {
    const std::string upToEnd = body + "#end\n";
    return upToEnd + "#sum " + savefmt::Hex16(savefmt::Fnv1a(upToEnd.data(), upToEnd.size())) + "\n";
}

// A slot's text with its checksum line worked out again (so the file is internally consistent).
std::string ResealSlot(const std::string& text) {
    const std::string tail = "--end--\nchecksum\t";
    const std::size_t at = text.rfind(tail);
    if (at == std::string::npos) return text;
    const std::string head = text.substr(0, at + 8);   // up to and including "--end--\n"
    return head + "checksum\t" + savefmt::Hex16(savefmt::Fnv1a(head.data(), head.size())) + "\n";
}

std::string ReplaceFirst(std::string s, const std::string& from, const std::string& to, bool* found = nullptr) {
    const std::size_t at = s.find(from);
    if (found) *found = at != std::string::npos;
    if (at != std::string::npos) s.replace(at, from.size(), to);
    return s;
}

std::string ShowData(const Data& d) {
    std::string o;
    for (const auto& kv : d) o += "[" + kv.first + "]=[" + kv.second + "] ";
    return o;
}

// A loaded slot's player position, or NaNs when there is no Player.
Vec3 PlayerAt(Scene& s) {
    GameObject* p = s.Find("Player");
    if (!p) return Vec3{NAN, NAN, NAN};
    return p->transform->Position();
}
bool Near(const Vec3& a, float x, float y, float z) {
    return std::fabs(a.x - x) < 1e-4f && std::fabs(a.y - y) < 1e-4f && std::fabs(a.z - z) < 1e-4f;
}

} // namespace

int main() {
    RUN_SUITE("save_header_damage");
    okaytest::Sandbox sandbox("save_header_damage", OKAY_SOURCE_DIR);
    const std::string dir = sandbox.Dir().generic_string();

    // Warnings go to a list instead of the console (the fuzz below makes thousands of them).
    std::vector<std::string> warnings;
    Log::console = false;
    Log::sink = [&](Log::Level lv, const std::string& msg) { if (lv == Log::Level::Warning) warnings.push_back(msg); };

    // ---- SaveFile: the table from the finding ----
    // Saved twice: coins 120, then 50; a value with a newline and a key with '=' (both need escaping).
    const std::string p = dir + "/table.okaysave";
    Data first, second;
    {
        SaveFile s;
        s.SetInt("coins", 120);
        s.SetString("note", "line1\nline2");
        s.SetInt("a=b", 7);
        CHECK(s.Save(p));
        first = Snap(s);
        s.SetInt("coins", 50);
        CHECK(s.Save(p));
        second = Snap(s);
    }
    const std::string good1 = ReadText(p + ".bak");   // coins=120
    const std::string good2 = ReadText(p);            // coins=50
    CHECK(Contains(good1, "coins=i:120") && Contains(good2, "coins=i:50"));
    CHECK(good2.compare(0, 11, "okaysave 3\n") == 0 && Contains(good2, "\n#end\n#sum "));
    CHECK(first.at("note") == "s:line1\nline2" && first.count("a=b") == 1);
    {
        Got g = LoadFile(p, good2, good1);   // untouched: the main file, as saved
        CHECK(g.ok && !g.fromBackup && g.data == second && g.error.empty());
    }
    bool found = false;
    const std::string edited = ReplaceFirst(good2, "coins=i:50", "coins=i:99", &found);
    CHECK(found);
    auto flipFirst = [](std::string s) { s[0] = '.'; return s; };
    // The file cut right after the coins line (the note and the trailer are gone).
    const std::size_t coinsLineEnd = good2.find('\n', good2.find("coins=")) + 1;
    const std::string cutAfterCoins = good2.substr(0, coinsLineEnd);

    struct Row { const char* what; std::string text; };
    const std::vector<Row> table = {
        {"body edited, header intact",          edited},
        {"body edited + first byte o->.",        flipFirst(edited)},
        {"body edited + header okaysave 4",      ReplaceFirst(edited, "okaysave 3", "okaysave 4")},
        {"cut after coins + first byte flipped", flipFirst(cutAfterCoins)},
        {"only the first byte flipped",          flipFirst(good2)},
        // More header damage of the same kind.
        {"header okaysave , (digit damaged)",    ReplaceFirst(good2, "okaysave 3", "okaysave ,")},
        {"header okaysave 2 with the trailer",   ReplaceFirst(good2, "okaysave 3", "okaysave 2")},
        {"header okaysave 1 with the trailer",   ReplaceFirst(good2, "okaysave 3", "okaysave 1")},
        {"header okaysaveX3",                    ReplaceFirst(good2, "okaysave 3", "okaysaveX3")},
        {"cut + header okaysave x",              ReplaceFirst(cutAfterCoins, "okaysave 3", "okaysave x")},
        {"cut + header okaysave 2x",             ReplaceFirst(cutAfterCoins, "okaysave 3", "okaysave 2x")},
        {"header okay=ave 3 (an '=' in it)",     ReplaceFirst(edited, "okaysave 3", "okay=ave 3")},
        {"header newline turned into a space",   ReplaceFirst(good2, "okaysave 3\n", "okaysave 3 ")},
    };
    for (const Row& r : table) {
        const Got g = LoadFile(p, r.text, good1);
        const bool right = g.ok && g.fromBackup && g.data == first && Contains(g.error, "damaged");
        if (!right)
            std::cerr << "  row \"" << r.what << "\": ok=" << g.ok << " fromBackup=" << g.fromBackup
                      << " error=\"" << g.error << "\" data=" << ShowData(g.data) << "\n";
        CHECK(right);
    }
    // The reason is named.
    {
        const Got g = LoadFile(p, ReplaceFirst(cutAfterCoins, "okaysave 3", "okaysave x"), good1);
        CHECK(Contains(g.error, "unknown format version 'x'"));
        const Got h = LoadFile(p, flipFirst(edited), good1);
        CHECK(Contains(h.error, "checksum"));
    }
    // No good backup: refused, empty, with a reason (not loaded as an old file).
    {
        const Got g = LoadFile(p, flipFirst(edited), "");
        CHECK(!g.ok && !g.fromBackup && g.data.empty() && Contains(g.error, "checksum"));
        const Got h = LoadFile(p, flipFirst(cutAfterCoins), "");
        CHECK(!h.ok && !h.fromBackup && h.data.empty() && Contains(h.error, "isn't a save"));
    }
    // The Console line says what happened.
    {
        warnings.clear();
        LoadFile(p, flipFirst(edited), good1);
        bool said = false;
        for (const std::string& w : warnings) said = said || (Contains(w, "was damaged") && Contains(w, "backup"));
        CHECK(said);
    }
    // Saving after a damaged-header load does not let the damaged file replace the good backup.
    {
        WriteText(p, flipFirst(edited));
        WriteText(p + ".bak", good1);
        SaveFile s;
        CHECK(s.Load(p) && s.LoadedFromBackup());
        s.SetInt("coins", 60);
        CHECK(s.Save(p, nullptr, false, false));   // the facade's quick path (no check asked for)
        CHECK(ReadText(p + ".bak") == good1);
    }

    // ---- SaveFile: a newer format is refused, never parsed blind ----
    {
        const std::string newerBody = "okaysave 4\ncoins=i:1000\nnote=s:from the future\n";
        const Got g = LoadFile(p, newerBody, good1);   // no trailer
        CHECK(!g.ok && !g.fromBackup && g.data.empty());
        CHECK(Contains(g.error, "newer") && Contains(g.error, "format 4"));
        const std::string sealed = SealSaveFile(newerBody);   // with a trailer that checks out
        const Got h = LoadFile(p, sealed, good1);
        CHECK(!h.ok && !h.fromBackup && h.data.empty() && Contains(h.error, "newer"));
        const Got big = LoadFile(p, "okaysave 99999999999999999999\ncoins=i:1\n", good1);
        CHECK(!big.ok && Contains(big.error, "newer"));
        // An older build saving over it first keeps it as <file>.newer.bak (as SaveGame does for slots).
        WriteText(p, sealed);
        RemoveFile(p + ".newer.bak");
        SaveFile s;
        CHECK(!s.Load(p));
        s.SetInt("coins", 5);
        CHECK(s.Save(p, nullptr, false, false));
        CHECK(ReadText(p + ".newer.bak") == sealed);
        SaveFile again;
        CHECK(again.Load(p) && again.GetInt("coins") == 5);
    }

    // ---- SaveFile: the old layouts still load ----
    {
        const std::string q = dir + "/legacy.okaysave";
        // Version 0: no header at all, plain `key=type:payload` lines, nothing escaped.
        Got g = LoadFile(q, "coins=i:5\nname=s:Hero\npath=s:C:\\new\\table\n", "");
        CHECK(g.ok && !g.fromBackup && g.error.empty());
        CHECK(g.data == (Data{{"coins", "i:5"}, {"name", "s:Hero"}, {"path", "s:C:\\new\\table"}}));
        g = LoadFile(q, "\ncoins=i:6\n", "");                                   // an empty first line was skipped
        CHECK(g.ok && g.data == (Data{{"coins", "i:6"}}));
        g = LoadFile(q, "", "");                                                 // an empty file is an empty store
        CHECK(g.ok && g.data.empty() && g.error.empty());
        // Version 1 (the first writer, "okaysave 1", text mode: CR LF on Windows), nothing escaped.
        g = LoadFile(q, "okaysave 1\npath=s:C:\\new\\table\ncoins=i:5\nspawn=v:1.5,0,3\n", "");
        CHECK(g.ok && !g.fromBackup && g.data.at("path") == "s:C:\\new\\table" && g.data.at("coins") == "i:5");
        g = LoadFile(q, "okaysave 1\r\npath=s:C:\\new\\table\r\ncoins=i:5\r\n", "");
        CHECK(g.ok && g.data.at("path") == "s:C:\\new\\table" && g.data.at("coins") == "i:5");
        // Version 2 (#552): escaped keys and values, no end marker.
        g = LoadFile(q, "okaysave 2\nkeep=i:7\nname=s:a\\nb\nk\\=ey=s:x\n", "");
        CHECK(g.ok && !g.fromBackup && g.data == (Data{{"keep", "i:7"}, {"name", "s:a\nb"}, {"k=ey", "s:x"}}));
        // Version 3 written by hand with its trailer.
        g = LoadFile(q, SealSaveFile("okaysave 3\nkeep=i:8\n"), "");
        CHECK(g.ok && g.data == (Data{{"keep", "i:8"}}));
        // The hand-edit route in docs/save_games.md: first line `okaysave 2`, the #end and #sum lines removed.
        std::string hand = ReplaceFirst(good2, "okaysave 3", "okaysave 2");
        hand = hand.substr(0, hand.find("#end\n"));
        hand = ReplaceFirst(hand, "coins=i:50", "coins=i:999");
        g = LoadFile(q, hand, "");
        CHECK(g.ok && !g.fromBackup && g.data.at("coins") == "i:999" && g.data.at("note") == "s:line1\nline2");
        // The same file saved by an editor that puts a UTF-8 byte order mark in front.
        g = LoadFile(q, "\xEF\xBB\xBF" + hand, "");
        CHECK(g.ok && !g.fromBackup && g.data.at("coins") == "i:999");
        // A zero-filled file is still not "an empty save".
        g = LoadFile(q, std::string(80, '\0'), "");
        CHECK(!g.ok && Contains(g.error, "isn't a save"));
    }

    // ---- SaveFile fuzz: every single-byte change of a valid file ----
    // A load that says "fine", did not use the backup and gives data other than what was saved is a silent wrong.
    {
        const std::string f = dir + "/fuzz.okaysave";
        std::mt19937_64 rng(0x5EED0047ull);
        int silentWrong = 0, sameData = 0, backup = 0, refused = 0;
        int shownWrong = 0;
        for (int i = 0; i < 5000; ++i) {
            std::string m = good2;
            const std::size_t at = (std::size_t)(rng() % m.size());
            const unsigned char old = (unsigned char)m[at];
            const unsigned char now = (unsigned char)((old + 1 + rng() % 255) & 0xFF);   // never the same byte
            m[at] = (char)now;
            if (i == 0) WriteText(f + ".bak", good1);   // the backup never changes: a Load only reads it
            WriteText(f, m);
            SaveFile sf;
            Got g;
            g.ok = sf.Load(f);
            g.fromBackup = sf.LoadedFromBackup();
            g.data = Snap(sf);
            if (!g.ok) ++refused;
            else if (g.fromBackup) ++backup;
            else if (g.data == second) ++sameData;
            else {
                ++silentWrong;
                if (shownWrong++ < 5)
                    std::cerr << "  silent wrong: byte " << at << " " << (int)old << "->" << (int)now
                              << " data=" << ShowData(g.data) << "\n";
            }
        }
        std::cout << "  fuzz (5000 single-byte changes): silent wrong " << silentWrong << ", same data " << sameData
                  << ", backup used " << backup << ", refused " << refused << "\n";
        CHECK(silentWrong == 0);
        CHECK(silentWrong + sameData + backup + refused == 5000);
    }

    // ---- SaveGame: the rows from the addendum ----
    {
        Scene s("Level");
        GameObject* player = s.CreateGameObject("Player");
        player->transform->SetPosition(Vec3{1.0f, 2.0f, 3.0f});
        s.Start();
        std::string err;
        CHECK(SaveGame::Save(s, "s", "first", &err));
        player->transform->SetPosition(Vec3{4.0f, 5.0f, 6.0f});
        CHECK(SaveGame::Save(s, "s", "second", &err));
        const std::string slot = SaveGame::SlotPath("s");
        const std::string bak = ReadText(slot + ".bak");
        const std::string main = ReadText(slot);
        CHECK(main.compare(0, 11, "okaygame 2\n") == 0 && Contains(main, "--end--\nchecksum\t"));
        CHECK(Contains(main, "transform 4 5 6") && Contains(bak, "transform 1 2 3"));

        struct GameGot { bool ok = false; bool usedBak = false; std::string error; Vec3 at; };
        auto load = [&](const std::string& text) {
            WriteText(slot, text);
            WriteText(slot + ".bak", bak);
            GameGot g;
            Scene t("Other");
            g.ok = SaveGame::LoadNow(t, "s", &g.error);
            g.usedBak = g.ok && SaveGame::LastLoadUsedBackup();
            g.at = PlayerAt(t);
            return g;
        };
        {
            const GameGot g = load(main);
            CHECK(g.ok && !g.usedBak && Near(g.at, 4, 5, 6));
        }
        const std::string moved = ReplaceFirst(main, "transform 4 5 6", "transform 9 9 6", &found);
        CHECK(found);
        const std::size_t sceneAt = main.find("--scene--\n");
        const std::vector<Row> rows = {
            {"transform edited",                         moved},
            {"transform edited + okaygame ,",            ReplaceFirst(moved, "okaygame 2", "okaygame ,")},
            {"cut before the objects + okaygame x",      ReplaceFirst(main.substr(0, sceneAt), "okaygame 2", "okaygame x")},
            {"okaygamey 2",                              ReplaceFirst(main, "okaygame 2", "okaygamey 2")},
            {"okaygame>2",                               ReplaceFirst(main, "okaygame 2", "okaygame>2")},
            {"okaygame 1 with the trailer",              ReplaceFirst(moved, "okaygame 2", "okaygame 1")},
            {"okaygame 9 with the old checksum",         ReplaceFirst(moved, "okaygame 2", "okaygame 9")},
            {"okaygame (no number)",                     ReplaceFirst(main, "okaygame 2", "okaygame")},
        };
        for (const Row& r : rows) {
            const GameGot g = load(r.text);
            const bool right = g.ok && g.usedBak && Near(g.at, 1, 2, 3) && Contains(g.error, "damaged");
            if (!right)
                std::cerr << "  slot row \"" << r.what << "\": ok=" << g.ok << " usedBak=" << g.usedBak << " error=\""
                          << g.error << "\" player=" << g.at.x << "," << g.at.y << "," << g.at.z << "\n";
            CHECK(right);
        }
        // The listing agrees: a damaged header shows the backup's values.
        WriteText(slot, ReplaceFirst(moved, "okaygame 2", "okaygame ,"));
        WriteText(slot + ".bak", bak);
        const SaveSlotInfo li = SaveGame::Info("s");
        CHECK(li.fromBackup && li.label == "first");
        CHECK(SaveGame::Check("s", &err) && Contains(err, "backup"));
        // A damaged digit that reads as a newer version: the checked listing shows the backup, not "newer".
        WriteText(slot, ReplaceFirst(moved, "okaygame 2", "okaygame 9"));
        const SaveSlotInfo vi = SaveGame::Info("s", true);
        CHECK(vi.fromBackup && !vi.incompatible && vi.label == "first");

        // A genuine newer slot (its checksum checks out, or it has none) is refused, not loaded from .bak.
        const std::string newer = ResealSlot(ReplaceFirst(main, "okaygame 2", "okaygame 99"));
        GameGot g = load(newer);
        CHECK(!g.ok && Contains(g.error, "newer") && Contains(g.error, "99"));
        CHECK(SaveGame::Info("s").incompatible);
        g = load("okaygame 99\nlabel\tfuture\n--scene--\n--state--\n");
        CHECK(!g.ok && Contains(g.error, "newer"));
        // Saving over a newer slot keeps it as .newer.bak (unchanged behaviour).
        WriteText(slot, newer);
        RemoveFile(slot + ".newer.bak");
        CHECK(SaveGame::Save(s, "s", "third", &err));
        CHECK(ReadText(slot + ".newer.bak") == newer);
        CHECK(ReadText(slot + ".bak") == bak);   // the newer file did not become the .bak
        // Saving over a damaged-header slot never makes it the .bak.
        WriteText(slot, ReplaceFirst(moved, "okaygame 2", "okaygame 1"));
        WriteText(slot + ".bak", bak);
        CHECK(SaveGame::Save(s, "s", "fourth", &err));
        CHECK(ReadText(slot + ".bak") == bak);

        // Version 1 slots (#498: "okaygame 1", no end marker, no checksum) still load, CR LF too.
        Scene v1("V1");
        v1.CreateGameObject("Player")->transform->SetPosition(Vec3{7.0f, 8.0f, 9.0f});
        v1.Start();
        const std::string v1Text = "okaygame 1\nlabel\told one\nsavedAt\t42\n--scene--\n" + SceneSerializer::Serialize(v1) + "--state--\n";
        WriteText(SaveGame::SlotPath("old"), v1Text);
        RemoveFile(SaveGame::SlotPath("old") + ".bak");
        Scene t1("T1");
        CHECK(SaveGame::LoadNow(t1, "old", &err) && !SaveGame::LastLoadUsedBackup() && Near(PlayerAt(t1), 7, 8, 9));
        CHECK(SaveGame::Info("old").version == 1 && SaveGame::Info("old").label == "old one");
        std::string crlf;
        for (char ch : v1Text) { if (ch == '\n') crlf += '\r'; crlf += ch; }
        WriteText(SaveGame::SlotPath("old"), crlf);
        Scene t2("T2");
        CHECK(SaveGame::LoadNow(t2, "old", &err) && Near(PlayerAt(t2), 7, 8, 9));
    }

    Log::sink = nullptr;
    Log::console = true;
    CHECK(sandbox.Verify());
    TEST_MAIN_RESULT();
}
