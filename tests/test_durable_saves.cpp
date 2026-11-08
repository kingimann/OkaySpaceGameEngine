#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Core/DataPack.hpp"
#include "okay/Core/DataAsset.hpp"
#include "okay/Core/Localization.hpp"
#include "okay/Core/SaveGame.hpp"
#include <fstream>
#include <filesystem>
#include <locale>
#include <string>

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

} // namespace

int main() {
    RUN_SUITE("durable_saves");
    const fs::path dir = fs::temp_directory_path() / "okay_test_durable_saves";
    fs::remove_all(dir);
    fs::create_directories(dir);

    // ---- SaveFile: escaping, precision, atomic write ----
    {
        const std::string path = (dir / "a.okaysave").string();
        SaveFile sf;
        const std::string tricky = "line1\nline2\ttab = eq \\ back\r";
        sf.SetString("note", tricky);
        sf.SetString("a=b\nc", "v");
        sf.SetFloat("big", 1234567.5f);
        sf.SetFloat("tenth", 0.1f);
        sf.SetVec3("pos", Vec3{1234567.5f, 0.1f, -2.25f});
        CHECK(sf.Save(path));
        SaveFile in;
        CHECK(in.Load(path));
        CHECK(in.GetString("note") == tricky);
        CHECK(in.GetString("a=b\nc") == "v");
        CHECK(in.Count() == 5);
        CHECK(in.GetFloat("big") == 1234567.5f);
        CHECK(in.GetFloat("tenth") == 0.1f);
        CHECK(in.GetVec3("pos").x == 1234567.5f && in.GetVec3("pos").y == 0.1f);
        CHECK(!fs::exists(path + ".tmp"));
    }
    {
        // Unwritable path: no such folder.
        SaveFile sf; sf.SetInt("x", 1);
        CHECK(!sf.Save((dir / "no_such_dir" / "f.okaysave").string()));
        // A directory where the file should go: Save fails and the directory stays.
        const fs::path d = dir / "isdir.okaysave";
        fs::create_directories(d / "inner");
        CHECK(!sf.Save(d.string()));
        CHECK(fs::is_directory(d / "inner"));
        // The temp file can't be created: the old file survives untouched.
        const fs::path f = dir / "keep.okaysave";
        WriteText(f, "okaysave 2\nkeep=i:1\n");
        fs::create_directories(dir / "keep.okaysave.tmp");
        CHECK(!sf.Save(f.string()));
        CHECK(ReadText(f) == "okaysave 2\nkeep=i:1\n");
    }
    {
        // Files from the old, unescaped writer still load (backslashes stay literal).
        const fs::path f = dir / "old.okaysave";
        WriteText(f, "okaysave 1\npath=s:C:\\new\\table\ncoins=i:5\nspawn=v:1.5,0,3\nhealth=f:1.23457e+06\n");
        SaveFile in;
        CHECK(in.Load(f.string()));
        CHECK(in.GetString("path") == "C:\\new\\table");
        CHECK(in.GetInt("coins") == 5);
        CHECK(in.GetVec3("spawn").x == 1.5f && in.GetVec3("spawn").z == 3.0f);
        CHECK_NEAR(in.GetFloat("health"), 1234570.0f, 1.0f);
        const fs::path g = dir / "oldnohdr.okaysave";
        WriteText(g, "k=s:a\\b\n");
        SaveFile in2;
        CHECK(in2.Load(g.string()));
        CHECK(in2.GetString("k") == "a\\b");
    }
    {
        // A comma-decimal locale must not change what is written or read.
        std::locale old = std::locale::classic();
        bool set = false;
        try { old = std::locale::global(std::locale("de_DE.UTF-8")); set = true; } catch (...) {}
        SaveFile sf; sf.SetFloat("f", 2.5f);
        const std::string path = (dir / "loc.okaysave").string();
        CHECK(sf.Save(path));
        SaveFile in; in.Load(path);
        CHECK(in.GetFloat("f") == 2.5f);
        CHECK(Contains(ReadText(path), "f:2.5"));
        if (set) std::locale::global(old);
    }

    // ---- Prefs ----
    {
        const std::string path = (dir / "p.okayprefs").string();
        Prefs::Clear();
        Prefs::SetString("keep", "me");
        Prefs::SetString("multi", "a\nb");
        CHECK(Prefs::Save(path));
        CHECK(!fs::exists(path + ".tmp"));
        // A cut-off file (no end line) is refused and the live keys stay.
        const std::string whole = ReadText(path);
        WriteText(dir / "cut.okayprefs", whole.substr(0, whole.size() - 8));
        Prefs::SetString("other", "x");
        CHECK(!Prefs::Load((dir / "cut.okayprefs").string()));
        CHECK(Prefs::GetString("keep") == "me" && Prefs::GetString("other") == "x");
        // The whole file loads and replaces the store.
        CHECK(Prefs::Load(path));
        CHECK(!Prefs::Has("other") && Prefs::GetString("multi") == "a\nb");
        // An older file (version 1, no end line) still loads.
        WriteText(dir / "v1.okayprefs", "okayprefs 1\nold\t7\n");
        CHECK(Prefs::Load((dir / "v1.okayprefs").string()));
        CHECK(Prefs::GetInt("old") == 7 && !Prefs::Has("keep"));
        // Missing file: the store is empty and the result says no file was read.
        Prefs::SetString("z", "1");
        CHECK(!Prefs::Load((dir / "nope.okayprefs").string()));
        CHECK(!Prefs::Has("z"));
        CHECK(!Prefs::Save((dir / "nodir" / "p").string()));
    }

    // ---- DataAsset ----
    {
        const std::string path = (dir / "d.okaydata").string();
        CHECK(!DataAsset::Cached(path).Has("hp"));          // not there yet
        WriteText(path, "hp = 10\n");
        CHECK(DataAsset::Cached(path).GetNumber("hp") == 10.0);   // picked up, the miss wasn't cached
        WriteText(path, "hp = 20\n");
        CHECK(DataAsset::Cached(path).GetNumber("hp") == 10.0);   // cached until reloaded
        CHECK(DataAsset::Reload(path));
        CHECK(DataAsset::Cached(path).GetNumber("hp") == 20.0);
        CHECK(!DataAsset::Reload((dir / "gone.okaydata").string()));
        DataAsset::Invalidate(path);
        WriteText(path, "hp = 30\n");
        CHECK(DataAsset::Cached(path).GetNumber("hp") == 30.0);
        // A failed Load keeps the fields.
        DataAsset a; a.Set("k", "v");
        CHECK(!a.Load((dir / "gone.okaydata").string()));
        CHECK(a.Has("k"));
        // A script can Set into a missing asset and Save it.
        const std::string np = (dir / "new.okaydata").string();
        DataAsset::Cached(np).Set("x", "1");
        CHECK(DataAsset::Cached(np).Has("x"));
        // Escaping round trip.
        DataAsset b;
        b.Set("a=b", " lead\nnew\\line\ttab ");
        b.Set("#hash", "v");
        CHECK(b.Save(np));
        DataAsset c; CHECK(c.Load(np));
        CHECK(c.GetString("a=b") == " lead\nnew\\line\ttab ");
        CHECK(c.GetString("#hash") == "v");
        CHECK(!b.Save((dir / "nodir" / "x").string()));
        // Hand-written plain files are unchanged.
        WriteText(dir / "hand.okaydata", "# note\npath = C:\\x\\y\n");
        DataAsset h; CHECK(h.Load((dir / "hand.okaydata").string()));
        CHECK(h.GetString("path") == "C:\\x\\y");
    }

    // ---- SaveGame ----
    Save::SetBaseDir((dir / "base").string());
    {
        Scene s("Level");
        s.CreateGameObject("Old");
        s.Start();
        const std::string sceneText = SceneSerializer::Serialize(s);
        auto hand = [&](const std::string& slot, const std::string& head, const std::string& state) {
            WriteText(SaveGame::SlotPath(slot), head + "--scene--\n" + sceneText + "--state--\n" + state);
        };
        std::string err;

        Save::SetInt("gold", 5);
        // A hostile slot (good kv line, then a broken one) must not touch the global save.
        hand("evil", "okaygame 1\nlabel\tx\n", "kv\tjunk\ti:1\nkv\tbad\n");
        CHECK(!SaveGame::LoadNow(s, "evil", &err));
        CHECK(Save::GetInt("gold") == 5 && !Save::Has("junk"));
        hand("evil2", "okaygame 1\nlabel\tx\n", "kv\tjunk\ti:1\nkv\tbad\tnotyped\n");
        CHECK(!SaveGame::LoadNow(s, "evil2", &err));
        CHECK(Save::GetInt("gold") == 5);
        // A valid version 1 slot (no checksum) loads and replaces the keys.
        hand("v1", "okaygame 1\nlabel\told one\nsavedAt\t42\n", "kv\tjunk\ti:7\n");
        CHECK(SaveGame::LoadNow(s, "v1", &err));
        CHECK(Save::GetInt("junk") == 7 && !Save::Has("gold"));
        CHECK(SaveGame::Info("v1").version == 1 && SaveGame::Info("v1").label == "old one");
        // A newer format is refused with a message.
        hand("v99", "okaygame 99\nlabel\tfuture\n", "kv\tjunk\ti:9\n");
        CHECK(!SaveGame::LoadNow(s, "v99", &err));
        CHECK(Contains(err, "newer") && Contains(err, "99"));
        CHECK(Save::GetInt("junk") == 7);
        // Migration hook: version 1 text is rewritten before it is read.
        SaveGame::SetMigration(1, [](std::string& t, std::string*) {
            std::size_t p = t.find("Old");
            while (p != std::string::npos) { t.replace(p, 3, "New"); p = t.find("Old", p + 3); }
            return true;
        });
        Scene m("m");
        CHECK(SaveGame::LoadNow(m, "v1", &err));
        CHECK(m.Find("New") != nullptr && m.Find("Old") == nullptr);
        SaveGame::SetMigration(1, nullptr);
        SaveGame::SetMigration(1, [](std::string&, std::string* e) { if (e) *e = "no"; return false; });
        CHECK(!SaveGame::LoadNow(m, "v1", &err) && Contains(err, "upgraded"));
        SaveGame::SetMigration(1, nullptr);

        // Round trip of \r, then checksum, truncation and .bak recovery.
        Save::Clear();
        Save::SetString("tail", "ends\r");
        CHECK(SaveGame::Save(s, "r", "label\r", &err));
        CHECK(SaveGame::Info("r").label == "label\r" && SaveGame::Info("r").version == SaveGame::kFormatVersion);
        Save::SetString("tail", "changed");
        CHECK(SaveGame::LoadNow(s, "r", &err));
        CHECK(Save::GetString("tail") == "ends\r");
        CHECK(!SaveGame::LastLoadUsedBackup());
        CHECK(!fs::exists(SaveGame::SlotPath("r") + ".bak"));   // first save: nothing to back up

        const std::string good = ReadText(SaveGame::SlotPath("r"));
        std::string edited = good;
        edited.replace(edited.find("label"), 5, "LABEL");
        WriteText(SaveGame::SlotPath("r"), edited);
        CHECK(!SaveGame::LoadNow(s, "r", &err));
        CHECK(Contains(err, "checksum"));
        WriteText(SaveGame::SlotPath("r"), good.substr(0, good.size() / 2));
        CHECK(!SaveGame::LoadNow(s, "r", &err));
        CHECK(Contains(err, "cut short"));
        WriteText(SaveGame::SlotPath("r"), good);

        CHECK(SaveGame::Save(s, "r", "second", &err));       // the first good save becomes .bak
        CHECK(fs::exists(SaveGame::SlotPath("r") + ".bak"));
        CHECK(SaveGame::Info("r").label == "second");
        std::string bad = ReadText(SaveGame::SlotPath("r"));
        bad.replace(bad.find("second"), 6, "SECOND");
        WriteText(SaveGame::SlotPath("r"), bad);
        Save::SetString("tail", "changed");
        CHECK(SaveGame::Info("r").label == "SECOND");         // header only: no checksum check
        CHECK(SaveGame::LoadNow(s, "r", &err));
        CHECK(SaveGame::LastLoadUsedBackup());
        CHECK(Contains(err, "backup"));
        CHECK(Save::GetString("tail") == "ends\r");
        // A damaged file doesn't replace the good .bak when saving over it.
        CHECK(SaveGame::Save(s, "r", "third", &err));
        CHECK(Contains(ReadText(SaveGame::SlotPath("r") + ".bak"), "label\tlabel"));
        CHECK(SaveGame::Delete("r") && !fs::exists(SaveGame::SlotPath("r") + ".bak"));

        // Caps: an oversized file or one with too many lines is refused.
        CHECK(SaveGame::Save(s, "big", "b", &err));
        SaveGame::SetLimits(100, 0);
        CHECK(!SaveGame::LoadNow(s, "big", &err) && Contains(err, "too large"));
        SaveGame::SetLimits(0, 3);
        CHECK(!SaveGame::LoadNow(s, "big", &err) && Contains(err, "too many lines"));
        SaveGame::SetLimits(0, 0);
        CHECK(SaveGame::LoadNow(s, "big", &err));
        // Info reads only the head of the file.
        std::string huge = "okaygame 1\nlabel\thead\nsavedAt\t9\n--scene--\n";
        for (int i = 0; i < 20000; ++i) huge += "garbage line that is never parsed\n";
        WriteText(SaveGame::SlotPath("huge"), huge);
        CHECK(SaveGame::Info("huge").label == "head" && SaveGame::Info("huge").savedAt == 9);
    }
    Save::SetBaseDir("");

    // ---- Localization ----
    {
        StringTable t;
        std::string err;
        CHECK(!t.Parse("okayloc 1\nlanguage en \"English\" default\ntext en k \"abc\ntext en k2 x\n", &err));
        CHECK(Contains(err, "line 3"));
        CHECK(t.Parse("okayloc 1\nlanguage en \"English\" default\ntext en k \"abc\"\n", &err));
        std::string csv = "key";
        for (int i = 0; i < 600; ++i) csv += ",l" + std::to_string(i);
        CHECK(!t.ImportCsv(csv + "\nk\n", &err) && Contains(err, "too many"));
        std::string rows = "key,en\n";
        for (int i = 0; i < 200001; ++i) rows += "k" + std::to_string(i) + ",x\n";
        StringTable t2;
        CHECK(!t2.ImportCsv(rows, &err) && Contains(err, "too many"));
        StringTable t3;
        CHECK(t3.ImportCsv("key,en\na,b\n", &err));

        std::string deep, tail;
        for (int i = 0; i < 40; ++i) { deep += "{0, plural, other {"; tail += "}}"; }
        const std::string out = Localization::Format(deep + "DEEP" + tail, {2}, "en");
        CHECK(Contains(out, "plural"));            // beyond the limit it is left as text
        std::string shallow, stail;
        for (int i = 0; i < 5; ++i) { shallow += "{0, plural, other {"; stail += "}}"; }
        CHECK(Localization::Format(shallow + "DEEP" + stail, {2}, "en") == "DEEP");
    }

    // ---- DataPack ----
    {
        std::string packed = DataPack::Pack("hello world");
        bool ok = false; std::string err;
        CHECK(DataPack::Unpack(packed, &ok, &err) == "hello world" && ok && err.empty());
        packed.back() ^= 0x55;
        CHECK(DataPack::Unpack(packed, &ok, &err).empty() && !ok && !err.empty());
        CHECK(DataPack::Unpack(packed).empty());          // the old signature still works
        std::string out;
        CHECK(!DataPack::TryDecode(packed, DataPack::DefaultKey(), out, &err));
        CHECK(DataPack::TryDecode(DataPack::Pack(""), DataPack::DefaultKey(), out) && out.empty());
        CHECK(DataPack::Unpack("plain", &ok) == "plain" && ok);
    }

    fs::remove_all(dir);
    TEST_MAIN_RESULT();
}
