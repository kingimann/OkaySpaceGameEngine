// Save framework: typed blocks, slot files (header metadata, backups, listing,
// copy/rename/delete), the migration chain, forward compatibility, Verify, sync
// backends and the menu model. Scene capture and checkpoints are in test_save_scene.cpp.
#include "test_framework.hpp"
#include "okay/Core/SaveFramework.hpp"
#include <filesystem>
#include <fstream>
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

long long g_now = 1700000000000LL;
long long Clock() { return g_now; }

SaveDocument Doc(int gold, const std::string& name = "Ann") {
    SaveDocument d;
    SaveWriter w = d.Writer("game");
    w.Int("gold", gold);
    w.String("name", name);
    return d;
}

SaveSlotInfo Meta(const std::string& label, const std::string& kind = SlotKind::Manual) {
    SaveSlotInfo m;
    m.label = label;
    m.kind = kind;
    return m;
}

} // namespace

int main() {
    RUN_SUITE("save_framework");
    const fs::path root = fs::temp_directory_path() / "okay_test_save_framework";
    fs::remove_all(root);
    fs::create_directories(root);
    std::string err;

    // ---- typed fields ----
    {
        SaveBlock b;
        SaveWriter w(b);
        w.Int("i", -42);
        w.Int64("big", 9007199254740993LL);
        w.Float("f", 0.1f);
        w.Bool("yes", true);
        w.String("s", "tab\there\nnewline = equals \\ back\r");
        w.Vector3("v", Vec3{1234567.5f, 0.1f, -2.25f});
        w.Rotation("q", Quat{0.1f, 0.2f, 0.3f, 0.9f});
        w.Strings("list", {"a", "b,c", "", "d\ne"});
        w.Scope("inv").Int("count", 7);
        w.Scope("inv").Scope("deep").String("x", "y");

        SaveReader r(b);
        CHECK(r.Int("i") == -42);
        CHECK(r.Int64("big") == 9007199254740993LL);
        CHECK(r.Float("f") == 0.1f);
        CHECK(r.Bool("yes") && !r.Bool("nope"));
        CHECK(r.String("s") == "tab\there\nnewline = equals \\ back\r");
        CHECK(r.Vector3("v").x == 1234567.5f && r.Vector3("v").y == 0.1f && r.Vector3("v").z == -2.25f);
        CHECK(r.Rotation("q").w == 0.9f && r.Rotation("q").x == 0.1f);
        const auto list = r.Strings("list");
        CHECK(list.size() == 4 && list[1] == "b,c" && list[2].empty() && list[3] == "d\ne");
        CHECK(r.Scope("inv").Int("count") == 7 && r.Scope("inv").Scope("deep").String("x") == "y");
        CHECK(r.Scope("inv").HasAny() && !r.Scope("nothing").HasAny());
        // A missing field gives the default, which is how OnLoad keeps its current value.
        CHECK(r.Float("missing", 3.5f) == 3.5f && r.Int("missing", 9) == 9);
        CHECK(r.Vector3("missing", Vec3{1, 2, 3}).y == 2.0f);
        // A field of another kind gives the default; numbers convert.
        CHECK(r.Int("s", 5) == 5 && r.Vector3("i", Vec3{7, 7, 7}).x == 7.0f);
        CHECK(r.Float("i") == -42.0f && r.Int("f") == 0 && r.Bool("i") );
        CHECK(b.HasPrefix("inv.") && !b.HasPrefix("zzz"));
        CHECK(b.Erase("i") && !b.Has("i"));
    }

    // ---- document text ----
    {
        SaveDocument d = Doc(5, "x=y\nz");
        d.Writer("other").Float("t", 2.5f);
        d.Block("empty");
        const std::string text = d.Serialize();
        CHECK(text == d.Serialize());                 // deterministic
        SaveDocument back;
        CHECK(SaveDocument::Parse(text, back, &err) && back == d);
        CHECK(back.Has("empty") && back.Find("empty")->Empty());
        CHECK(d.RoundTrips(&err));
        CHECK(back.Names().size() == 3);
        CHECK(!SaveDocument::Parse(text.substr(0, text.rfind("t=f")), back, &err) && Contains(err, "cut short"));
        CHECK(!SaveDocument::Parse("garbage\n", back, &err));
        CHECK(!SaveDocument::Parse("@block\ta\t0\n@block\ta\t0\n", back, &err) && Contains(err, "twice"));
        CHECK(!SaveDocument::Parse("@block\ta\tx\n", back, &err));
        CHECK(!SaveDocument::Parse("@block\ta\t1\nnoequals\n", back, &err));
        SaveDocument merged = Doc(1);
        merged.Merge(d);
        CHECK(merged.Find("game")->GetInt("gold") == 5 && merged.Has("other"));
    }

    // ---- a slot's header is readable without the payload ----
    {
        const fs::path dir = root / "header";
        SaveStore st(dir.string());
        st.SetClock(Clock);
        st.SetGameVersion("1.4.2");
        SaveDocument big = Doc(1);
        SaveWriter bw = big.Writer("world");
        for (int i = 0; i < 20000; ++i) bw.Int("tile" + std::to_string(i), i);
        SaveSlotInfo m = Meta("Chapter 2: The Mine");
        m.scene = "Mine";
        m.playTime = 3725.5f;
        m.thumbnail = "s1.png";
        m.fields["difficulty"] = "hard";
        m.fields["note"] = "line\nbreak\ttab = eq";
        g_now = 1700000000000LL;
        CHECK(st.Write("s1", big, m, &err));
        CHECK(!fs::exists(st.SlotPath("s1") + ".tmp"));

        SaveSlotInfo i = st.Info("s1");
        CHECK(i.exists && !i.corrupt && !i.incompatible && i.slot == "s1");
        CHECK(i.label == "Chapter 2: The Mine" && i.scene == "Mine" && i.playTime == 3725.5f);
        CHECK(i.thumbnail == "s1.png" && i.gameVersion == "1.4.2" && i.kind == "manual");
        CHECK(i.fields.at("difficulty") == "hard" && i.fields.at("note") == "line\nbreak\ttab = eq");
        CHECK(i.savedAtMs == 1700000000000LL && i.savedAt == 1700000000LL && i.version == 1);
        CHECK(i.sizeBytes > 100000);

        SaveDocument back; SaveSlotInfo ri;
        CHECK(st.Read("s1", back, &ri, &err) && back == big && ri.label == i.label);

        // Flip one payload byte: the header-only info still lists it; a deep check and Read refuse.
        std::string bytes = ReadText(st.SlotPath("s1"));
        std::string flipped = bytes;
        flipped[flipped.size() - 20] ^= 0x01;
        WriteText(st.SlotPath("s1"), flipped);
        CHECK(!st.Info("s1").corrupt && st.Info("s1").label == "Chapter 2: The Mine");
        CHECK(st.Info("s1", true).corrupt);
        CHECK(!st.Read("s1", back, nullptr, &err) && Contains(err, "checksum"));
        // Cut short: caught from the header and the file size alone.
        WriteText(st.SlotPath("s1"), bytes.substr(0, bytes.size() - 100));
        CHECK(st.Info("s1").corrupt && Contains(st.Info("s1").error, "cut short"));
        // An edited header fails its own checksum.
        std::string edited = bytes;
        edited.replace(edited.find("Chapter"), 7, "CHAPTER");
        WriteText(st.SlotPath("s1"), edited);
        CHECK(st.Info("s1").corrupt && Contains(st.Info("s1").error, "header"));
        // Trailing junk and non-slot files.
        WriteText(st.SlotPath("s1"), bytes + "extra");
        CHECK(st.Info("s1").corrupt && Contains(st.Info("s1").error, "extra"));
        WriteText(st.SlotPath("s1"), "hello world\n");
        CHECK(st.Info("s1").corrupt && Contains(st.Info("s1").error, "isn't a save slot"));
        // An invalid slot name is refused outright.
        CHECK(!st.Write("bad name", big, Meta("x"), &err) && Contains(err, "valid slot name"));
        CHECK(!st.Write("../escape", big, Meta("x"), &err));
        CHECK(!st.Info("nope").exists && !st.Read("nope", back, nullptr, &err) && Contains(err, "No save"));
    }

    // ---- limits, unwritable folders, the default folder ----
    {
        SaveStore st((root / "limits").string());
        st.SetClock(Clock);
        SaveDocument big = Doc(1);
        SaveWriter bw = big.Writer("pad");
        for (int i = 0; i < 2000; ++i) bw.Int("k" + std::to_string(i), i);
        CHECK(st.Write("ok", big, Meta("ok"), &err));
        st.SetMaxBytes(1000);
        CHECK(!st.Write("big", big, Meta("big"), &err) && Contains(err, "too large") && !st.Exists("big"));
        SaveDocument d;
        CHECK(!st.Read("ok", d, nullptr, &err) && Contains(err, "too large"));
        st.SetMaxBytes(0);                                              // back to the default
        CHECK(st.Read("ok", d, nullptr, &err) && d == big);
        // The save folder can't be made (a file is in the way): the write fails and says so.
        WriteText(root / "blocker", "a file");
        SaveStore blocked((root / "blocker").string());
        CHECK(!blocked.Write("x", Doc(1), Meta("x"), &err) && Contains(err, "Couldn't write") && blocked.ListSlots().empty());
        // The default store follows Save::SetBaseDir, like SaveGame does.
        Save::SetBaseDir((root / "base").string());
        SaveStore def;
        CHECK(def.Directory() == Save::Resolve("saves") && Contains(def.SlotPath("a"), "saves/a.okayslot"));
        def.SetClock(Clock);
        CHECK(def.Write("a", Doc(1), Meta("a"), &err) && fs::exists(root / "base" / "saves" / "a.okayslot"));
        CHECK(&SaveStore::Default() == &SaveStore::Default() && SaveStore::Default().Exists("a"));
        Save::SetBaseDir("");
    }

    // ---- backups ----
    {
        const fs::path dir = root / "backup";
        SaveStore st(dir.string());
        st.SetClock(Clock);
        g_now += 1000;
        CHECK(st.Write("a", Doc(1), Meta("first"), &err));
        CHECK(!fs::exists(st.SlotPath("a") + ".bak"));       // nothing to back up yet
        g_now += 1000;
        CHECK(st.Write("a", Doc(2), Meta("second"), &err));
        CHECK(fs::exists(st.SlotPath("a") + ".bak"));
        SaveDocument d;
        CHECK(st.Read("a", d, nullptr, &err) && d.Find("game")->GetInt("gold") == 2);

        // The main file is damaged: the backup of the previous save is read instead.
        const std::string good = ReadText(st.SlotPath("a"));
        WriteText(st.SlotPath("a"), good.substr(0, good.size() / 2));
        SaveSlotInfo info; SaveLoadReport rep;
        CHECK(st.Read("a", d, &info, &err, &rep));
        CHECK(rep.usedBackup && Contains(err, "backup") && info.fromBackup && d.Find("game")->GetInt("gold") == 1);
        const SaveSlotInfo li = st.Info("a");
        CHECK(li.fromBackup && !li.corrupt && li.label == "first");
        // Saving over the damaged file doesn't replace the good backup with it.
        g_now += 1000;
        CHECK(st.Write("a", Doc(3), Meta("third"), &err));
        SaveDocument bak;
        std::string bakText = ReadText(st.SlotPath("a") + ".bak");
        SaveSlotInfo bi;
        CHECK(SaveStore::ParseHeader(bakText, bi, &err) && bi.label == "first");
        // Delete removes the file and its backups.
        CHECK(st.Delete("a") && !fs::exists(st.SlotPath("a")) && !fs::exists(st.SlotPath("a") + ".bak"));
        CHECK(!st.Delete("a"));
    }

    // ---- listing: sorted, corrupt and incompatible flagged ----
    {
        const fs::path dir = root / "list";
        SaveStore st(dir.string());
        st.SetClock(Clock);
        SaveStore future(dir.string());
        future.SetClock(Clock);
        future.SetDataVersion(5);
        g_now = 1000000; CHECK(st.Write("a", Doc(1), Meta("A"), &err));
        g_now = 3000000; { SaveSlotInfo m = Meta("B"); m.playTime = 10.0f; CHECK(st.Write("b", Doc(1), m, &err)); }
        g_now = 2000000; { SaveSlotInfo m = Meta("C"); m.playTime = 300.0f; CHECK(st.Write("c", Doc(1), m, &err)); }
        g_now = 4000000; CHECK(future.Write("future", Doc(1), Meta("From the future"), &err));
        WriteText(dir / "bad.okayslot", "not a save");
        WriteText(dir / "bad name.okayslot", "ignored: not a valid slot name");
        WriteText(dir / "other.txt", "ignored");
        WriteText(dir / "a.okayslot.bak", "ignored");

        auto names = [](const std::vector<SaveSlotInfo>& v) { std::string s; for (auto& i : v) s += i.slot + ","; return s; };
        const auto newest = st.ListSlots();
        CHECK(names(newest) == "future,b,c,a,bad,");
        CHECK(newest[0].incompatible && Contains(newest[0].error, "newer") && !newest[0].corrupt);
        CHECK(newest[4].corrupt && !newest[4].error.empty() && newest[4].exists);
        CHECK(!newest[1].corrupt && !newest[1].incompatible);
        CHECK(names(st.ListSlots(SlotSort::Oldest)) == "a,c,b,future,bad,");
        CHECK(names(st.ListSlots(SlotSort::Name)) == "a,b,bad,c,future,");
        CHECK(names(st.ListSlots(SlotSort::PlayTime)) == "c,b,a,future,bad,");
        // Continue target: the newest slot that is neither corrupt nor from a newer build.
        CHECK(st.NewestValidSlot() == "b");
        std::string bt = ReadText(st.SlotPath("b"));
        bt[bt.size() - 10] ^= 0x04;                       // payload damage: only a deep check sees it
        WriteText(st.SlotPath("b"), bt);
        const auto deepList = st.ListSlots(SlotSort::Newest, true);
        CHECK(names(deepList) == "future,b,c,a,bad," && deepList[1].corrupt && !deepList[2].corrupt);
        CHECK(st.Info("b", true).corrupt);
        CHECK(st.NewestValidSlot() == "c");
        // The newer build reads it all.
        SaveDocument d;
        CHECK(future.Read("future", d, nullptr, &err));
        // An empty folder lists nothing.
        SaveStore none((root / "does_not_exist").string());
        CHECK(none.ListSlots().empty() && none.NewestValidSlot().empty());
    }

    // ---- copy, rename, delete, label, thumbnails ----
    {
        const fs::path dir = root / "manage";
        const fs::path elsewhere = root / "elsewhere";
        SaveStore st(dir.string());
        st.SetClock(Clock);
        WriteText(dir / "s1.png", "PNGDATA");
        WriteText(elsewhere / "pic.png", "KEEP");
        g_now = 5000000;
        SaveSlotInfo m = Meta("One"); m.thumbnail = "s1.png"; m.fields["k"] = "v";
        CHECK(st.Write("s1", Doc(11), m, &err));
        SaveSlotInfo ext = Meta("Ext"); ext.thumbnail = (elsewhere / "pic.png").string();
        CHECK(st.Write("ext", Doc(12), ext, &err));

        CHECK(st.Copy("s1", "s2", false, &err));
        SaveDocument d;
        CHECK(st.Read("s2", d, nullptr, &err) && d.Find("game")->GetInt("gold") == 11);
        CHECK(st.Info("s2").thumbnail == "s2.png" && ReadText(dir / "s2.png") == "PNGDATA" && fs::exists(dir / "s1.png"));
        CHECK(st.Info("s2").fields.at("k") == "v" && st.Info("s2").savedAtMs == st.Info("s1").savedAtMs);
        CHECK(!st.Copy("s1", "s2", false, &err) && Contains(err, "already exists"));
        CHECK(!st.Copy("s1", "s1", true, &err));
        CHECK(!st.Copy("missing", "x", false, &err));
        CHECK(!st.Copy("s1", "bad name", false, &err));
        g_now += 1000;
        CHECK(st.Write("s1", Doc(99), m, &err));
        CHECK(st.Copy("s1", "s2", true, &err));            // overwrite: the old s2 becomes its .bak
        CHECK(st.Read("s2", d, nullptr, &err) && d.Find("game")->GetInt("gold") == 99);
        CHECK(fs::exists(st.SlotPath("s2") + ".bak"));
        // A thumbnail path that isn't a bare file beside the slots is the game's own: copied by name only.
        CHECK(st.Copy("ext", "ext2", false, &err) && st.Info("ext2").thumbnail == ext.thumbnail);

        CHECK(st.Rename("s2", "s3", false, &err));
        CHECK(!st.Exists("s2") && st.Exists("s3") && !fs::exists(st.SlotPath("s2") + ".bak") && fs::exists(st.SlotPath("s3") + ".bak"));
        CHECK(st.Info("s3").thumbnail == "s3.png" && fs::exists(dir / "s3.png") && !fs::exists(dir / "s2.png"));
        CHECK(st.Read("s3", d, nullptr, &err) && d.Find("game")->GetInt("gold") == 99);
        CHECK(!st.Rename("s3", "s1", false, &err) && Contains(err, "already exists"));
        CHECK(st.Rename("s3", "s1", true, &err));
        CHECK(st.Read("s1", d, nullptr, &err) && st.Info("s1").thumbnail == "s1.png");
        CHECK(!st.Rename("nope", "x", false, &err));

        // The label changes without touching the payload.
        const std::string before = ReadText(st.SlotPath("s1"));
        CHECK(st.SetLabel("s1", "Renamed", &err));
        CHECK(st.Info("s1").label == "Renamed" && st.Read("s1", d, nullptr, &err) && d.Find("game")->GetInt("gold") == 99);
        CHECK(ReadText(st.SlotPath("s1")) != before && !st.Info("s1", true).corrupt);
        CHECK(st.UpdateInfo("s1", [](SaveSlotInfo& i) { i.fields["k"] = "changed"; i.fields["new"] = "1"; i.savedAtMs = 5; i.version = 9; }, &err));
        CHECK(st.Info("s1").fields.at("k") == "changed" && st.Info("s1").savedAtMs != 5 && st.Info("s1").version == 1);
        CHECK(!st.SetLabel("nope", "x", &err));

        // Delete takes the thumbnail it owns, never a file elsewhere.
        CHECK(st.Delete("s1") && !fs::exists(dir / "s1.png"));
        CHECK(st.Delete("ext") && fs::exists(elsewhere / "pic.png"));
        CHECK(fs::exists(dir / "ext2.okayslot"));
    }

    // ---- migration chain ----
    {
        const fs::path dir = root / "migrate";
        SaveStore v1(dir.string());
        v1.SetClock(Clock);
        g_now = 6000000;
        SaveDocument old;
        old.Writer("game").String("name", "Ann");
        old.Writer("game").Int("hp", 10);
        CHECK(v1.Write("old", old, Meta("old"), &err));
        CHECK(v1.Info("old").version == 1);

        SaveStore v4(dir.string());
        v4.SetClock(Clock);
        v4.SetDataVersion(4);
        std::string order;
        v4.RegisterMigration(1, [&](SaveDocument& d, std::string*) {            // 1 -> 2: a key moves
            order += "1";
            SaveBlock& b = d.Block("game");
            b.SetString("player.name", b.GetString("name"));
            b.Erase("name");
            return true;
        });
        v4.RegisterMigration(2, [&](SaveDocument& d, std::string*) {            // 2 -> 3: a value changes type and scale
            order += "2";
            SaveBlock& b = d.Block("game");
            b.SetFloat("health", (float)b.GetInt("hp") * 10.0f);
            b.Erase("hp");
            return true;
        });
        v4.RegisterMigration(3, [&](SaveDocument& d, std::string*) {            // 3 -> 4: a block is added from the others
            order += "3";
            d.Writer("meta").String("note", "hello " + d.Find("game")->GetString("player.name"));
            return true;
        });
        CHECK(v4.MigrationPath(1).size() == 3 && v4.MigrationPath(4).empty());
        SaveDocument d; SaveLoadReport rep; SaveSlotInfo info;
        CHECK(v4.Read("old", d, &info, &err, &rep));
        CHECK(order == "123");
        CHECK(rep.migrated && rep.fromVersion == 1 && rep.toVersion == 4 && rep.steps == (std::vector<int>{1, 2, 3}));
        CHECK(d.Find("game")->GetString("player.name") == "Ann" && !d.Find("game")->Has("name"));
        CHECK(d.Find("game")->GetFloat("health") == 100.0f && !d.Find("game")->Has("hp"));
        CHECK(d.Find("meta")->GetString("note") == "hello Ann");
        CHECK(info.version == 1);                                   // the file itself is untouched
        CHECK(v4.Info("old").version == 1 && !v4.Info("old").incompatible);

        // A slot already at version 2 only runs the later steps.
        SaveStore v2(dir.string()); v2.SetClock(Clock); v2.SetDataVersion(2);
        SaveDocument mid;
        mid.Writer("game").String("player.name", "Bo");
        mid.Writer("game").Int("hp", 3);
        CHECK(v2.Write("mid", mid, Meta("mid"), &err));
        order.clear();
        CHECK(v4.Read("mid", d, nullptr, &err, &rep) && order == "23" && rep.steps == (std::vector<int>{2, 3}));
        CHECK(d.Find("game")->GetFloat("health") == 30.0f && d.Find("meta")->GetString("note") == "hello Bo");
        // Saved by the new build, it reads with no steps.
        CHECK(v4.Write("old", d, Meta("new"), &err) && v4.Info("old").version == 4);
        order.clear();
        CHECK(v4.Read("old", d, nullptr, &err, &rep) && order.empty() && !rep.migrated);
        // The previous version of the file is kept as the backup.
        SaveSlotInfo bk;
        CHECK(SaveStore::ParseHeader(ReadText(v4.SlotPath("old") + ".bak"), bk, &err) && bk.version == 1);

        // A version with no registered step keeps its layout.
        SaveStore v6(dir.string()); v6.SetClock(Clock); v6.SetDataVersion(6);
        v6.RegisterMigration(1, [](SaveDocument& dd, std::string*) { dd.Writer("steps").Bool("one", true); return true; });
        v6.RegisterMigration(5, [](SaveDocument& dd, std::string*) { dd.Writer("steps").Bool("five", true); return true; });
        // "mid" is version 2: it passes through 2, 3, 4 and 5, and only 5 has a step.
        CHECK(v6.Read("mid", d, nullptr, &err, &rep) && rep.steps == (std::vector<int>{5}) && d.Find("steps")->Has("five") && !d.Find("steps")->Has("one"));

        // A step that fails: a clear error, nothing loaded.
        SaveStore bad(dir.string()); bad.SetClock(Clock); bad.SetDataVersion(3);
        bad.RegisterMigration(2, [](SaveDocument&, std::string* e) { if (e) *e = "boom"; return false; });
        SaveDocument untouched = Doc(1);
        CHECK(!bad.Read("mid", untouched, nullptr, &err) && Contains(err, "upgraded from data version 2 to 3") && Contains(err, "boom"));
        CHECK(untouched == Doc(1));
        bad.RegisterMigration(2, nullptr);                          // removing a step
        CHECK(bad.Read("mid", d, nullptr, &err));
    }

    // ---- forward compatibility ----
    {
        const fs::path dir = root / "forward";
        SaveStore v5(dir.string()); v5.SetClock(Clock); v5.SetDataVersion(5);
        SaveStore v3(dir.string()); v3.SetClock(Clock); v3.SetDataVersion(3);
        g_now = 7000000;
        CHECK(v5.Write("fut", Doc(500), Meta("progress made on the new build"), &err));
        const std::string original = ReadText(v5.SlotPath("fut"));

        SaveDocument d;
        CHECK(!v3.Read("fut", d, nullptr, &err));
        CHECK(Contains(err, "newer version") && Contains(err, "5") && Contains(err, "3") && Contains(err, "untouched"));
        CHECK(ReadText(v3.SlotPath("fut")) == original);            // refused, never touched
        const SaveSlotInfo fi = v3.Info("fut");
        CHECK(fi.incompatible && fi.exists && !fi.corrupt && fi.label == "progress made on the new build");
        CHECK(!v3.Verify("fut", &err) && Contains(err, "newer"));
        CHECK(!v3.SetLabel("fut", "nope", &err) && Contains(err, "newer"));
        CHECK(v3.NewestValidSlot().empty());
        // A newer save container format is refused the same way.
        WriteText(dir / "container.okayslot", "okayslot 99\nwhatever\n");
        CHECK(v3.Info("container").incompatible && !v3.Info("container").corrupt);
        CHECK(!v3.Read("container", d, nullptr, &err) && Contains(err, "newer"));

        // Saving over it keeps it: the old build never destroys the newer progress.
        g_now += 1000;
        CHECK(v3.Write("fut", Doc(1), Meta("old build"), &err));
        CHECK(ReadText(v3.SlotPath("fut") + ".newer.bak") == original);
        CHECK(v3.Read("fut", d, nullptr, &err) && d.Find("game")->GetInt("gold") == 1);
        // ... and the newer build can read the kept copy.
        CHECK(v5.ImportRaw("recovered", ReadText(v3.SlotPath("fut") + ".newer.bak"), false, &err));
        CHECK(v5.Read("recovered", d, nullptr, &err) && d.Find("game")->GetInt("gold") == 500);
        // The regular backup isn't used for a newer file: a refused read isn't "damaged".
        CHECK(!fs::exists(v3.SlotPath("fut") + ".bak"));
    }

    // ---- Verify ----
    {
        const fs::path dir = root / "verify";
        SaveStore st(dir.string()); st.SetClock(Clock);
        SaveDocument d = Doc(8, "tricky\t=\n\\value");
        d.Writer("x").Float("f", 0.3f);
        CHECK(st.Write("v", d, Meta("v"), &err));
        std::string report;
        CHECK(st.Verify("v", &report) && Contains(report, "ok") && Contains(report, "block"));
        CHECK(!st.Verify("none", &report) && Contains(report, "does not exist"));
        std::string bytes = ReadText(st.SlotPath("v"));
        bytes[bytes.size() - 5] ^= 0x10;
        WriteText(st.SlotPath("v"), bytes);
        CHECK(!st.Verify("v", &report) && Contains(report, "checksum"));
        // A migration that breaks the round trip shows up too.
        SaveStore up(dir.string()); up.SetClock(Clock); up.SetDataVersion(2);
        g_now += 10;
        SaveStore one(dir.string()); one.SetClock(Clock);
        CHECK(one.Write("w", Doc(1), Meta("w"), &err));
        up.RegisterMigration(1, [](SaveDocument&, std::string* e) { if (e) *e = "cannot"; return false; });
        CHECK(!up.Verify("w", &report) && Contains(report, "migration failed"));
        up.RegisterMigration(1, [](SaveDocument& dd, std::string*) { dd.Writer("n").Int("x", 1); return true; });
        CHECK(up.Verify("w", &report) && Contains(report, "migrated"));
        // The debug read-back after every write.
        up.SetVerifyAfterWrite(true);
        CHECK(up.Write("w2", Doc(2), Meta("w2"), &err));
    }

    // ---- sync ----
    {
        const fs::path dirA = root / "syncA", dirB = root / "syncB";
        SaveStore a(dirA.string()), b(dirB.string());
        a.SetClock(Clock); b.SetClock(Clock);
        MemorySaveSync cloud;
        SaveSyncer sa(a, cloud), sb(b, cloud);

        g_now = 10000; CHECK(a.Write("one", Doc(1), Meta("one"), &err));
        g_now = 20000; CHECK(a.Write("two", Doc(2), Meta("two"), &err));
        std::vector<SaveSyncer::Item> plan;
        CHECK(sa.Plan(plan, &err) && plan.size() == 2 && plan[0].state == SyncState::LocalOnly && plan[1].state == SyncState::LocalOnly);
        auto rep = sa.Sync();
        CHECK(rep.ok && rep.uploaded == 2 && rep.downloaded == 0 && rep.conflicts == 0 && cloud.Count() == 2);
        std::vector<SaveSyncEntry> entries;
        CHECK(cloud.List(entries) && entries.size() == 2 && entries[0].modifiedMs == 10000 && entries[1].modifiedMs == 20000);
        CHECK(entries[0].checksum.size() == 16 && entries[0].checksum != entries[1].checksum);
        CHECK(fs::exists(dirA / "sync_state.txt"));
        CHECK(a.ListSlots().size() == 2);                              // the state file isn't a slot
        rep = sa.Sync();
        CHECK(rep.ok && rep.uploaded == 0 && rep.downloaded == 0 && cloud.uploads == 2);

        // Another device downloads both.
        rep = sb.Sync();
        CHECK(rep.ok && rep.downloaded == 2 && rep.uploaded == 0);
        SaveDocument d;
        CHECK(b.Read("two", d, nullptr, &err) && d.Find("game")->GetInt("gold") == 2);

        // A change on one side travels to the other.
        g_now = 30000; CHECK(a.Write("one", Doc(11), Meta("one"), &err));
        CHECK(sa.Sync().uploaded == 1);
        rep = sb.Sync();
        CHECK(rep.downloaded == 1 && rep.conflicts == 0);
        CHECK(b.Read("one", d, nullptr, &err) && d.Find("game")->GetInt("gold") == 11);
        CHECK(fs::exists(b.SlotPath("one") + ".bak"));                 // what was replaced is kept

        // Both sides changed "two": a conflict, settled by the newer save time.
        g_now = 40000; CHECK(a.Write("two", Doc(21), Meta("two A"), &err));
        g_now = 50000; CHECK(b.Write("two", Doc(22), Meta("two B"), &err));
        CHECK(sa.Sync().uploaded == 1);                                // A goes first: the cloud still matched its base
        plan.clear();
        CHECK(sb.Plan(plan, &err));
        bool sawConflict = false;
        for (auto& it : plan) if (it.slot == "two") sawConflict = it.state == SyncState::Conflict && it.local.modifiedMs == 50000 && it.remote.modifiedMs == 40000;
        CHECK(sawConflict);
        rep = sb.Sync();                                               // B's save is newer: it wins and goes up
        CHECK(rep.conflicts == 1 && rep.uploaded == 1 && rep.unresolved == 0 && rep.conflictSlots.size() == 1 && rep.conflictSlots[0] == "two");
        std::string bytes;
        CHECK(cloud.Download("two", bytes) && SaveStore::Checksum(bytes) == SaveStore::Checksum(ReadText(b.SlotPath("two"))));
        rep = sa.Sync();                                               // A now sees a remote-only change
        CHECK(rep.downloaded == 1 && rep.conflicts == 0);
        CHECK(a.Read("two", d, nullptr, &err) && d.Find("game")->GetInt("gold") == 22);

        // Both changed "one"; the local save is newer, so it wins and goes up.
        g_now = 60000; CHECK(b.Write("one", Doc(31), Meta("one B"), &err));
        g_now = 70000; CHECK(a.Write("one", Doc(32), Meta("one A"), &err));
        CHECK(sb.Sync().uploaded == 1);                                // B first: the cloud still matched its base
        rep = sa.Sync();                                               // A: both changed; local 70000 > remote 60000
        CHECK(rep.conflicts == 1 && rep.uploaded == 1 && rep.downloaded == 0);
        rep = sb.Sync();                                               // B: only the remote changed now
        CHECK(rep.downloaded == 1 && rep.conflicts == 0);
        CHECK(b.Read("one", d, nullptr, &err) && d.Find("game")->GetInt("gold") == 32);
        // Both changed "one" again; this time the cloud copy is newer, so the local one loses (kept as .bak).
        g_now = 80000; CHECK(b.Write("one", Doc(41), Meta("one B2"), &err));
        g_now = 75000; CHECK(a.Write("one", Doc(42), Meta("one A2"), &err));
        CHECK(sb.Sync().uploaded == 1);
        rep = sa.Sync();
        CHECK(rep.conflicts == 1 && rep.downloaded == 1 && rep.uploaded == 0);
        CHECK(a.Read("one", d, nullptr, &err) && d.Find("game")->GetInt("gold") == 41);
        CHECK(fs::exists(a.SlotPath("one") + ".bak"));

        // Same time, different bytes: left alone unless someone decides.
        g_now = 90000; CHECK(a.Write("tie", Doc(1), Meta("tie A"), &err));
        CHECK(b.Write("tie", Doc(2), Meta("tie B"), &err));
        CHECK(sa.Sync().uploaded == 1);
        rep = sb.Sync();
        CHECK(rep.conflicts == 1 && rep.unresolved == 1 && rep.uploaded == 0 && rep.downloaded == 0 && !rep.messages.empty());
        CHECK(b.Read("tie", d, nullptr, &err) && d.Find("game")->GetInt("gold") == 2);
        SaveSyncer::Options ask;
        ask.policy = ConflictPolicy::Ask;
        int asked = 0;
        SaveSyncConflict seen;
        ask.resolve = [&](const SaveSyncConflict& c) { ++asked; seen = c; return SyncChoice::UseRemote; };
        rep = sb.Sync(ask);
        CHECK(asked == 1 && seen.slot == "tie" && seen.local.modifiedMs == 90000 && seen.remote.modifiedMs == 90000 && seen.local.checksum != seen.remote.checksum);
        CHECK(rep.downloaded == 1 && b.Read("tie", d, nullptr, &err) && d.Find("game")->GetInt("gold") == 1);
        CHECK(fs::exists(b.SlotPath("tie") + ".bak"));
        // Ask without a callback leaves it; Skip and the Prefer policies.
        g_now = 95000; CHECK(a.Write("pol", Doc(1), Meta("pol A"), &err));
        CHECK(b.Write("pol", Doc(2), Meta("pol B"), &err));
        CHECK(sa.Sync().uploaded == 1);
        SaveSyncer::Options o2; o2.policy = ConflictPolicy::Ask;
        CHECK(sb.Sync(o2).unresolved == 1);
        o2.policy = ConflictPolicy::Skip;
        CHECK(sb.Sync(o2).unresolved == 1);
        o2.policy = ConflictPolicy::PreferLocal;
        rep = sb.Sync(o2);
        CHECK(rep.uploaded == 1 && rep.unresolved == 0);
        CHECK(sa.Sync().downloaded == 1 && a.Read("pol", d, nullptr, &err) && d.Find("game")->GetInt("gold") == 2);
        // KeepBoth: the cloud copy comes down under a new name, then ours goes up.
        g_now = 99000; CHECK(a.Write("pol", Doc(5), Meta("pol A2"), &err));
        CHECK(b.Write("pol", Doc(6), Meta("pol B2"), &err));
        CHECK(sa.Sync().uploaded == 1);
        SaveSyncer::Options both; both.policy = ConflictPolicy::Ask;
        both.resolve = [](const SaveSyncConflict&) { return SyncChoice::KeepBoth; };
        rep = sb.Sync(both);
        CHECK(rep.keptBoth == 1 && b.Exists("pol_remote"));
        CHECK(b.Read("pol_remote", d, nullptr, &err) && d.Find("game")->GetInt("gold") == 5);
        CHECK(b.Read("pol", d, nullptr, &err) && d.Find("game")->GetInt("gold") == 6);
        CHECK(sb.Sync().conflicts == 0);

        // Never synced and different: a conflict, not a silent overwrite.
        SaveStore c((root / "syncC").string());
        c.SetClock(Clock);
        g_now = 120000; CHECK(c.Write("one", Doc(77), Meta("one C"), &err));
        SaveSyncer sc(c, cloud);
        rep = sc.Sync();
        CHECK(rep.conflicts == 1 && rep.uploaded == 1 && rep.downloaded >= 1);   // "one" conflicts (C's is newer); the rest come down

        // A damaged local slot is never pushed; an offline backend fails cleanly.
        WriteText(a.SlotPath("junk"), "not a save");
        const std::size_t before = cloud.Count();
        CHECK(sa.Sync().ok && cloud.Count() == before && !cloud.Has("junk"));
        cloud.SetOffline(true);
        g_now = 130000; CHECK(a.Write("late", Doc(1), Meta("late"), &err));
        rep = sa.Sync();
        CHECK(!rep.ok && !rep.messages.empty() && Contains(rep.messages[0], "offline") && a.Exists("late"));
        CHECK(!sa.Plan(plan, &err));
        cloud.SetOffline(false);
        CHECK(sa.Sync().uploaded >= 1 && cloud.Has("late"));
    }
    {
        // A folder-backed backend: the same flow over files.
        SaveStore a((root / "fsA").string()), b((root / "fsB").string());
        a.SetClock(Clock); b.SetClock(Clock);
        FolderSaveSync folder((root / "fsCloud").string());
        SaveSyncer sa(a, folder), sb(b, folder);
        std::string err2;
        std::vector<SaveSyncEntry> list;
        CHECK(folder.List(list, &err2) && list.empty());               // nothing there yet, not an error
        g_now = 200000; CHECK(a.Write("x", Doc(1), Meta("x"), &err2));
        CHECK(sa.Sync().uploaded == 1 && fs::exists(root / "fsCloud" / "x.okayslot"));
        CHECK(folder.List(list, &err2) && list.size() == 1 && list[0].modifiedMs == 200000 && list[0].slot == "x");
        auto rep = sb.Sync();
        CHECK(rep.downloaded == 1);
        SaveDocument d;
        CHECK(b.Read("x", d, nullptr, &err2) && d.Find("game")->GetInt("gold") == 1);
        CHECK(sb.Sync().downloaded == 0 && sb.Sync().uploaded == 0);
        std::string bytes;
        SaveSyncEntry entry;
        CHECK(folder.Download("x", bytes, &entry, &err2) && entry.modifiedMs == 200000 && !bytes.empty());
        CHECK(!folder.Download("nope", bytes, nullptr, &err2));
        CHECK(folder.Remove("x", &err2) && !fs::exists(root / "fsCloud" / "x.okayslot"));
        CHECK(!folder.Upload("bad name", bytes, 0, &err2));
        // A remote file that isn't a slot is refused on download, not installed.
        WriteText(root / "fsCloud" / "junk.okayslot", "garbage");
        rep = sb.Sync();
        CHECK(!rep.ok && rep.failed == 1 && !b.Exists("junk"));
    }

    // ---- menu model ----
    {
        const fs::path dir = root / "menu";
        SaveStore st(dir.string()); st.SetClock(Clock);
        st.SetGameVersion("2.0");
        g_now = 1700000000000LL; { SaveSlotInfo m = Meta("Start of the game"); m.scene = "Intro"; m.playTime = 65.0f; m.thumbnail = "slot_1.png"; m.fields["difficulty"] = "easy"; CHECK(st.Write("slot_1", Doc(1), m, &err)); }
        g_now += 3600LL * 1000 * 5;  { SaveSlotInfo m = Meta("Deep in the mine"); m.scene = "Mine"; m.playTime = 3725.0f; CHECK(st.Write("slot_3", Doc(1), m, &err)); }
        g_now += 60LL * 1000;        CHECK(st.Write("quicksave", Doc(1), Meta("Quick save", SlotKind::Quick), &err));
        g_now += 60LL * 1000;        CHECK(st.Write("autosave_1", Doc(1), Meta("Autosave", SlotKind::Auto), &err));
        g_now += 60LL * 1000;        CHECK(st.Write("checkpoint_gate", Doc(1), Meta("gate", SlotKind::Checkpoint), &err));
        g_now += 60LL * 1000;        CHECK(st.Write("slot_9", Doc(1), Meta("Last one"), &err));
        WriteText(dir / "broken.okayslot", "junk");
        std::string text = ReadText(st.SlotPath("slot_9"));
        text[text.size() - 3] ^= 0x01;                                  // a payload flip: the newest, but not loadable
        WriteText(st.SlotPath("slot_9"), text);
        SaveStore future(dir.string()); future.SetClock(Clock); future.SetDataVersion(8);
        g_now += 1000; CHECK(future.Write("slot_2", Doc(1), Meta("from a newer build"), &err));

        SaveMenuModel::Options opt;
        opt.manualSlots = 4;
        SaveMenuModel model(st, opt);
        model.Refresh(g_now + 90LL * 1000);
        const auto& rows = model.Rows();
        // Fixed slots first, in order; then the rest, newest first.
        CHECK(rows.size() >= 7);
        CHECK(rows[0].slot == "slot_1" && rows[1].slot == "slot_2" && rows[2].slot == "slot_3" && rows[3].slot == "slot_4");
        CHECK(!rows[0].empty && rows[0].canLoad && rows[0].canSave && rows[0].canDelete && rows[0].title == "Start of the game");
        CHECK(rows[0].scene == "Intro" && rows[0].thumbnail == "slot_1.png" && rows[0].gameVersion == "2.0" && rows[0].fields.at("difficulty") == "easy");
        CHECK(rows[0].savedText == "2023-11-14 22:13" && rows[0].playTimeText == "1m 05s" && rows[0].savedAt == 1700000000LL);
        CHECK(rows[0].agoText == "5 hours ago");
        CHECK(rows[1].incompatible && !rows[1].canLoad && rows[1].statusText == "Saved by a newer version");
        CHECK(rows[2].playTimeText == "1h 02m");
        CHECK(rows[3].empty && rows[3].title == "Empty slot" && !rows[3].canLoad && rows[3].canSave && !rows[3].canDelete);
        std::string rest;
        for (std::size_t i = 4; i < rows.size(); ++i) rest += rows[i].slot + ",";
        CHECK(rest == "slot_9,autosave_1,quicksave,broken,");          // checkpoints hidden by default
        const SaveMenuRow* quick = model.Find("quicksave");
        CHECK(quick && quick->isQuick && !quick->canSave && quick->canLoad && model.Find("autosave_1")->isAuto);
        CHECK(model.Find("broken")->corrupt && model.Find("broken")->statusText == "Damaged" && !model.Find("broken")->canLoad);
        CHECK(model.Find("checkpoint_gate") == nullptr && model.Find("nope") == nullptr);
        // Continue is the newest slot that can really load: the damaged slot_9 and the
        // newer-build slot_2 are skipped, and the checkpoint (hidden from the rows) counts.
        CHECK(model.CanContinue() && model.ContinueSlot() == "checkpoint_gate" && st.NewestValidSlot() == "checkpoint_gate");
        CHECK(model.ContinueRow() == nullptr);

        // Options: show checkpoints, hide damaged, the local-time offset, other sorts.
        model.GetOptions().includeCheckpoints = true;
        model.GetOptions().showCorrupt = false;
        model.GetOptions().utcOffsetMinutes = 120;
        model.GetOptions().sort = SlotSort::Oldest;
        model.Refresh(g_now);
        CHECK(model.Find("checkpoint_gate") && model.Find("checkpoint_gate")->isCheckpoint && model.Find("checkpoint_gate")->isNewest);
        CHECK(model.ContinueRow() && model.ContinueRow()->slot == "checkpoint_gate" && !model.Find("slot_9")->isNewest);
        CHECK(model.Find("broken") == nullptr && model.Find("slot_1")->savedText == "2023-11-15 00:13");
        model.GetOptions().includeAuto = false;
        model.GetOptions().includeQuick = false;
        model.Refresh(g_now);
        CHECK(model.Find("autosave_1") == nullptr && model.Find("quicksave") == nullptr);

        // Selection.
        model.GetOptions().manualSlots = 0;
        model.GetOptions().sort = SlotSort::Newest;
        model.Refresh(g_now);
        CHECK(model.Selected() == -1 && model.SelectedRow() == nullptr);
        model.SelectNext();
        CHECK(model.Selected() == 0);
        model.SelectPrevious();
        CHECK(model.Selected() == 0);
        CHECK(model.SelectSlot("slot_3") && model.SelectedRow()->slot == "slot_3");
        CHECK(!model.SelectSlot("zzz") && !model.Select(-1) && !model.Select(99));
        model.Refresh(g_now);                                           // the selection survives a refresh
        CHECK(model.SelectedRow() && model.SelectedRow()->slot == "slot_3");
        const std::size_t count = model.Rows().size();
        CHECK(model.DeleteSelected(&err) && !st.Exists("slot_3") && model.Rows().size() == count - 1);
        CHECK(model.SelectedRow() != nullptr);
        for (int i = 0; i < 20; ++i) model.SelectNext();
        CHECK(model.Selected() == (int)model.Rows().size() - 1);
        model.GetOptions().manualSlots = 2;
        model.Refresh(g_now);
        CHECK(model.SelectSlot("slot_2") == true);
        model.GetOptions().manualSlots = 3;
        model.Refresh(g_now);
        CHECK(model.SelectSlot("slot_3") && model.SelectedRow()->empty);
        CHECK(!model.DeleteSelected(&err));                             // an empty row has nothing to delete

        // Text helpers.
        CHECK(SaveMenuModel::FormatPlayTime(0) == "0s" && SaveMenuModel::FormatPlayTime(-5) == "0s" && SaveMenuModel::FormatPlayTime(59.6f) == "1m 00s");
        CHECK(SaveMenuModel::FormatPlayTime(7384) == "2h 03m" && SaveMenuModel::FormatPlayTime(12) == "12s");
        CHECK(SaveMenuModel::FormatDateTime(0) == "1970-01-01 00:00");
        CHECK(SaveMenuModel::FormatDateTime(951782400 + 3600 * 13 + 60 * 5) == "2000-02-29 13:05");   // a leap day
        CHECK(SaveMenuModel::FormatDateTime(-86400) == "1969-12-31 00:00");
        CHECK(SaveMenuModel::FormatDateTime(0, -90) == "1969-12-31 22:30");
        CHECK(SaveMenuModel::FormatAgo(-5) == "just now" && SaveMenuModel::FormatAgo(59) == "just now");
        CHECK(SaveMenuModel::FormatAgo(60) == "1 minute ago" && SaveMenuModel::FormatAgo(120) == "2 minutes ago");
        CHECK(SaveMenuModel::FormatAgo(3600) == "1 hour ago" && SaveMenuModel::FormatAgo(86400 * 3) == "3 days ago");
        CHECK(SaveMenuModel::FormatAgo(86400 * 60) == "2 months ago" && SaveMenuModel::FormatAgo(86400LL * 800) == "2 years ago");
    }

    fs::remove_all(root);
    TEST_MAIN_RESULT();
}
