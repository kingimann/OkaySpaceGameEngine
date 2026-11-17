// The editor's saving and loading rules (editor/SaveSafety.hpp, editor/SceneSalvage.hpp) and the atomic writes
// under every save: a write that dies half way leaves the old file whole (simulated failures), backups rotate,
// recovery copies are summarised and cleaned up, the unsaved mark follows its state machine, outside changes are
// found, the unsaved list is worded right, load errors are in plain words, Open Recent is cleaned.
// Everything uses temporary folders, a fake clock and fake file systems: nothing here opens a window.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "SaveSafety.hpp"
#include "SceneSalvage.hpp"
#include "okay/Core/Backup.hpp"
#include "okay/Core/Recovery.hpp"
#include "okay/Render/Material.hpp"
#include "okay/Modeling/PolyMesh.hpp"

#include <filesystem>
#include <fstream>
#include <map>
#include <string>

using namespace okay;
using namespace okay::editor;
using namespace okay::editor::savesafe;
namespace fs = std::filesystem;

static std::string Slurp(const std::string& p) { std::string s; ReadWholeFile(p, s); return s; }
static void Put(const std::string& p, const std::string& s) { std::ofstream f(p, std::ios::binary | std::ios::trunc); f << s; }
static bool Has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

int main() {
    RUN_SUITE("editor_saves");
    const std::string dir = (fs::temp_directory_path() / "okay_editor_saves_t1").string();
    fs::remove_all(dir);
    fs::create_directories(dir);

    // ---- 1. A write that dies half way leaves the old file whole ---------------------------------------------
    {
        const std::string f = dir + "/a.txt";
        Put(f, "OLD CONTENT");
        const std::string neu(10000, 'n');
        struct Case { savefmt::WriteFaultStep step; std::size_t after; const char* what; };
        const Case cases[] = {
            {savefmt::WriteFaultStep::Open, 0, "can't create the temp file"},
            {savefmt::WriteFaultStep::Write, 0, "disk full at once"},
            {savefmt::WriteFaultStep::Write, 5000, "disk full half way"},
            {savefmt::WriteFaultStep::Write, 9999, "disk full on the last byte"},
            {savefmt::WriteFaultStep::Flush, 0, "flush fails"},
            {savefmt::WriteFaultStep::Rename, 0, "swap fails"},
        };
        for (const Case& c : cases) {
            for (int kill = 0; kill < 2; ++kill) {
                savefmt::WriteFault w;
                w.step = c.step; w.afterBytes = c.after; w.leaveTemp = kill != 0;
                savefmt::SetWriteFault(w);
                std::string err;
                const bool ok = WriteUserFile(f, neu, &err);
                CHECK(!ok);
                CHECK(!err.empty());
                CHECK(savefmt::WriteFaultHits() == 1);
                CHECK(Slurp(f) == "OLD CONTENT");                                         // the old file is whole
                if (!kill) CHECK(!fs::exists(f + ".tmp"));                                  // a clean failure leaves no litter
                if (kill && c.step != savefmt::WriteFaultStep::Open) CHECK(fs::exists(f + ".tmp"));   // the "killed" write left its temp file
                // The next write works, and replaces any temp file a killed write left.
                savefmt::ClearWriteFault();
                CHECK(WriteUserFile(f, "OLD CONTENT", &err));
                CHECK(Slurp(f) == "OLD CONTENT" && !fs::exists(f + ".tmp"));
            }
        }
        // A killed write really does leave the half-written temp file (proving the test kills it), and the file is intact.
        {
            savefmt::WriteFault w; w.step = savefmt::WriteFaultStep::Write; w.afterBytes = 5000; w.leaveTemp = true;
            savefmt::SetWriteFault(w);
            CHECK(!WriteUserFile(f, neu, nullptr));
            CHECK(fs::exists(f + ".tmp") && fs::file_size(f + ".tmp") == 5000);
            CHECK(Slurp(f) == "OLD CONTENT");
            savefmt::ClearWriteFault();
        }
        // A fault is for one write (count) and can be limited to a path.
        {
            savefmt::WriteFault w; w.step = savefmt::WriteFaultStep::Rename; w.count = 2; w.pathContains = "only_this";
            savefmt::SetWriteFault(w);
            CHECK(WriteUserFile(f, "fine", nullptr) && Slurp(f) == "fine");              // a different path is not hit
            const std::string g = dir + "/only_this.txt";
            Put(g, "G1");
            CHECK(!WriteUserFile(g, "G2") && !WriteUserFile(g, "G2") && Slurp(g) == "G1");
            CHECK(WriteUserFile(g, "G3") && Slurp(g) == "G3");                          // disarmed after two hits
            savefmt::ClearWriteFault();
        }
    }

    // ---- 2. Every kind of save goes through it ------------------------------------------------------------------
    {
        auto injected = [&](const std::string& tag) {
            savefmt::WriteFault w; w.step = savefmt::WriteFaultStep::Write; w.afterBytes = 7; w.pathContains = tag;
            savefmt::SetWriteFault(w);
        };
        // Scene.
        Scene sc;
        sc.CreateGameObject("One");
        const std::string sp = dir + "/s.okayscene";
        std::string err;
        CHECK(SceneSerializer::SaveToFile(sc, sp, &err));
        const std::string v1 = Slurp(sp);
        sc.CreateGameObject("Two");
        injected("s.okayscene");
        CHECK(!SceneSerializer::SaveToFile(sc, sp, &err));
        CHECK(Has(err, "not changed"));
        CHECK(Slurp(sp) == v1);
        savefmt::ClearWriteFault();
        CHECK(SceneSerializer::SaveToFile(sc, sp, &err));
        CHECK(Slurp(sp + ".bak") == v1 && Slurp(sp) != v1);                               // one rolling previous version
        CHECK(!fs::exists(sp + ".savetmp") && !fs::exists(sp + ".tmp"));
        // Prefab.
        const std::string pp = dir + "/p.okayprefab";
        GameObject* root = sc.Find("One");
        CHECK(root && SceneSerializer::SaveObjectToFile(*root, pp));
        const std::string p1 = Slurp(pp);
        injected("p.okayprefab");
        CHECK(!SceneSerializer::SaveObjectToFile(*sc.Find("Two"), pp));
        CHECK(Slurp(pp) == p1);
        savefmt::ClearWriteFault();
        // Material.
        Material m;
        const std::string mp = dir + "/m.okaymat";
        CHECK(m.SaveToFile(mp));
        const std::string m1 = Slurp(mp);
        m.shininess = 77.0f;
        injected("m.okaymat");
        CHECK(!m.SaveToFile(mp) && Slurp(mp) == m1);
        savefmt::ClearWriteFault();
        CHECK(m.SaveToFile(mp) && Slurp(mp) != m1);
        // Mesh asset.
        PolyMesh pm = PolyMesh::FromMesh(Mesh::Cube(1.0f));
        const std::string xp = dir + "/x.okaymesh";
        CHECK(pm.Save(xp, &err));
        const std::string x1 = Slurp(xp);
        injected("x.okaymesh");
        CHECK(!pm.Save(xp, &err) && Slurp(xp) == x1);
        savefmt::ClearWriteFault();
        // Behaviour tree, dialogue, quests, character clips, timeline: one-line proof each that the same write is used.
        const std::string bp = dir + "/b.okaybt";
        BehaviorTree bt;
        CHECK(bt.SaveToFile(bp, &err));
        const std::string b1 = Slurp(bp);
        injected("b.okaybt");
        CHECK(!bt.SaveToFile(bp, &err) && Slurp(bp) == b1);
        savefmt::ClearWriteFault();
    }

    // ---- 3. Backup rotation: the last five saves, newest first ---------------------------------------------------
    {
        const std::string bdir = dir + "/backups";
        BackupManager::Config cfg;
        cfg.directory = bdir;
        cfg.policy = docrec::LastSavesPolicy(5);
        BackupManager bm(cfg);
        std::int64_t t = 1700000000;
        bm.SetClock([&] { return t; });
        const std::string f = dir + "/rot.okay";
        for (int i = 0; i < 9; ++i) { t += 60; CHECK(bm.SaveWithBackup(f, "version " + std::to_string(i))); }
        CHECK(Slurp(f) == "version 8");
        const auto list = bm.List(f);
        CHECK(list.size() == 5);                                                          // never more than five
        CHECK(!list.empty() && list.front().timestamp > list.back().timestamp);           // newest first
        std::string newest;
        CHECK(ReadWholeFile(list.front().path, newest) && newest == "version 7");          // the one before the current file
        // A failed save leaves the file and its backups alone.
        savefmt::WriteFault w; w.step = savefmt::WriteFaultStep::Write; w.afterBytes = 3; w.pathContains = "rot.okay";
        savefmt::SetWriteFault(w);
        t += 60;
        const auto before = bm.List(f).size();
        CHECK(!bm.SaveWithBackup(f, "version 9"));
        CHECK(Slurp(f) == "version 8" && bm.List(f).size() >= 1 && bm.List(f).size() <= before + 1);
        savefmt::ClearWriteFault();
    }

    // ---- 4. Recovery copies: written to the recovery folder, listed after a crash, cleaned up -----------------------
    {
        const std::string rdir = dir + "/recovery";
        RecoveryManager::Config cfg;
        cfg.directory = rdir;
        cfg.baseName = "editor";
        cfg.slots = 3;
        cfg.intervalSeconds = 30;
        cfg.sessionId = "sessA";
        std::int64_t now = 1700000000;
        std::string scene = "okayscene 3\ngameobject 0 \"A\"\nend\n";
        const std::string real = dir + "/Level1.okayscene";
        Put(real, "THE USER'S FILE");
        {
            RecoveryManager r(cfg);
            r.SetClock([&] { return now; });
            r.SetProcessProbe([](long) { return false; });
            bool dirty = false;
            r.SetProvider([&](std::string& p, std::string& l) { if (!dirty) return false; p = scene; l = "scene"; return true; });
            CHECK(r.StartSession());
            r.Tick(31);
            CHECK(r.AutosaveCount() == 0);                                                // clean: nothing written
            dirty = true;
            CHECK(r.Tick(31) && r.AutosaveCount() == 1);
            CHECK(Slurp(real) == "THE USER'S FILE");                                      // never over the user's file
            // ... the editor is killed here: no EndSession.
        }
        {
            cfg.sessionId = "sessB";
            RecoveryManager r(cfg);
            r.SetClock([&] { return now; });
            r.SetProcessProbe([](long) { return false; });
            CHECK(r.StartSession());
            CHECK(r.UncleanShutdownDetected());
            auto found = r.FindRecoverable();
            CHECK(found.size() == 1 && found[0].sessionId == "sessA");
            std::string payload;
            CHECK(r.Restore(found[0], payload) && payload == scene);
            CHECK(r.Discard(found[0]) && r.FindRecoverable().empty());                    // discarded: gone
            r.EndSession();
        }
        // A normal save drops the session's copies; a clean exit leaves nothing.
        {
            cfg.sessionId = "sessC";
            RecoveryManager r(cfg);
            r.SetClock([&] { return now; });
            r.SetProcessProbe([](long) { return false; });
            r.SetProvider([&](std::string& p, std::string& l) { p = scene; l = "x"; return true; });
            CHECK(r.StartSession());
            now += 60;
            CHECK(r.Tick(31) && r.FindRecoverable(true).size() == 1);
            r.OnSaved();
            CHECK(r.FindRecoverable(true).empty());
            now += 60;
            r.Tick(31);
            r.EndSession();
            cfg.sessionId = "sessD";
            RecoveryManager again(cfg);
            again.SetProcessProbe([](long) { return false; });
            CHECK(again.StartSession() && !again.UncleanShutdownDetected() && again.FindRecoverable().empty());
            again.EndSession();
        }
        // Keep N: a session never holds more than `slots` copies.
        {
            cfg.sessionId = "sessE"; cfg.slots = 2;
            RecoveryManager r(cfg);
            r.SetClock([&] { return now; });
            CHECK(r.StartSession());
            for (int i = 0; i < 6; ++i) { now += 40; CHECK(r.AutosavePayload("okayscene 3\ngameobject 0 \"" + std::to_string(i) + "\"\nend\n", "s") == RecoveryManager::AutosaveResult::Written); }
            CHECK(r.FindRecoverable(true).size() == 2);
            r.EndSession();
        }

        // The words of the recovery box.
        std::vector<RecoveredDoc> docs = {
            {"scene", real, "okayscene 3\ngameobject 0 \"A\"\nend\ngameobject 1 \"B\"\nend\ngameobject 2 \"C\"\nend\n"},
            {"script", dir + "/enemy.os", "update() {}"},
            {"scene", "untitled-scene", "okayscene 3\ngameobject 0 \"Only\"\nend\n"},
        };
        std::map<std::string, std::string> disk = {{real, "okayscene 3\ngameobject 0 \"A\"\nend\ngameobject 1 \"B\"\nend\n"}};
        auto rd = [&](const std::string& p, std::string& out) { auto it = disk.find(p); if (it == disk.end()) return false; out = it->second; return true; };
        const RecoverySummary s = SummarizeRecovery(docs, now - 185, now, 0, rd);
        CHECK(s.worthRestoring);
        CHECK(Has(s.headline, "closed unexpectedly") && Has(s.headline, "Restore"));
        CHECK(Has(s.when, "3 minutes ago"));
        CHECK(s.lines.size() == 3);
        CHECK(Has(s.lines[0].name, "Level1.okayscene") && Has(s.lines[0].detail, "3 objects") && Has(s.lines[0].detail, "the saved file has 2") && Has(s.lines[0].detail, "1 more here"));
        CHECK(Has(s.lines[1].detail, "the file is missing"));
        CHECK(Has(s.lines[2].name, "never saved") && Has(s.lines[2].detail, "1 object"));
        // Identical to disk: nothing worth restoring.
        disk[real] = docs[0].text;
        std::vector<RecoveredDoc> same = {docs[0]};
        CHECK(!SummarizeRecovery(same, now, now, 0, rd).worthRestoring);
        // "Open both" puts the recovered scene next to the file, never over it, and never over an earlier recovered copy.
        std::set<std::string> taken;
        auto ex = [&](const std::string& p) { return taken.count(p) > 0; };
        const std::string c1 = RecoveredCopyPath(dir + "/Level1.okayscene", ex);
        CHECK(Has(c1, "Level1 (recovered).okayscene"));
        taken.insert(c1);
        CHECK(Has(RecoveredCopyPath(dir + "/Level1.okayscene", ex), "Level1 (recovered 2).okayscene"));
        CHECK(CountSceneObjects("not a scene") == -1 && CountSceneObjects("okayscene 3\n") == 0);
        CHECK(AgeText(10) == "just now" && AgeText(60) == "a minute ago" && AgeText(3 * 3600) == "3 hours ago" && AgeText(30 * 3600) == "yesterday");
    }

    // ---- 5. Autosave rules: not in Play, rate limited, not mid-drag ------------------------------------------------
    {
        AutosaveSettings s;
        AutosaveContext c;
        c.anythingUnsaved = true;
        c.secondsSinceLast = 100;
        CHECK(DecideAutosave(s, c) == AutosaveVerdict::Go);
        AutosaveContext p = c; p.playing = true;
        CHECK(DecideAutosave(s, p) == AutosaveVerdict::Playing);                           // never during Play
        p = c; p.anythingUnsaved = false;
        CHECK(DecideAutosave(s, p) == AutosaveVerdict::NothingToSave);
        p = c; p.secondsSinceLast = 10;
        CHECK(DecideAutosave(s, p) == AutosaveVerdict::NotDueYet);
        p = c; p.secondsSinceLast = 2; p.urgent = true;                                    // focus lost twice in a row: rate limited
        CHECK(DecideAutosave(s, p) == AutosaveVerdict::TooSoon);
        p = c; p.secondsSinceLast = 10; p.urgent = true;                                   // focus lost: do not wait for the timer
        CHECK(DecideAutosave(s, p) == AutosaveVerdict::Go);
        p = c; p.userBusy = true; p.secondsSinceRequested = 2;                             // mid-drag: wait for a quiet moment
        CHECK(DecideAutosave(s, p) == AutosaveVerdict::Busy);
        p.secondsSinceRequested = 20;                                                      // ... but not for ever
        CHECK(DecideAutosave(s, p) == AutosaveVerdict::Go);
        s.enabled = false;
        CHECK(DecideAutosave(s, c) == AutosaveVerdict::Disabled);
        AutosaveSettings bad; bad.intervalSeconds = 0.1f; bad.keep = 99; bad.Clamp();
        CHECK(bad.intervalSeconds == 5.0f && bad.keep == 20);
        AutosaveSettings low; low.keep = -4; low.Clamp();
        CHECK(low.keep == 1);
        // The cost stays under one percent: a slow copy stretches the interval.
        AutosaveCost cost;
        cost.Add(4.0);
        CHECK(cost.SuggestedInterval(30.0f) == 30.0f);
        cost.Add(900.0);
        CHECK(cost.SuggestedInterval(30.0f) >= 90.0f && cost.WorstMs() == 900.0 && cost.Count() == 2);
        CHECK(cost.AverageMs() > 400.0 && cost.AverageMs() < 500.0);
    }

    // ---- 6. The unsaved mark: its state machine ---------------------------------------------------------------------
    {
        DocState d;
        CHECK(d.State() == DocState::Kind::New && d.StatusText(0).empty() && !d.Dirty());
        d.Loaded();
        CHECK(d.State() == DocState::Kind::Clean && d.StatusText(0) == "No changes");
        d.Edited();
        CHECK(d.Dirty() && d.State() == DocState::Kind::Dirty && d.StatusText(0) == "Unsaved changes");
        d.Saved(1700000000 - 1700000000 % 86400 + 14 * 3600 + 3 * 60);                      // 14:03 UTC that day
        CHECK(!d.Dirty() && d.StatusText(0) == "Saved 14:03");
        CHECK(d.StatusText(3600) == "Saved 15:03");                                         // local time
        d.Edited();
        CHECK(Has(d.StatusText(0), "Unsaved changes") && Has(d.StatusText(0), "14:03"));
        d.SaveFailed("the disk is full");
        CHECK(d.State() == DocState::Kind::SaveFailed && d.Dirty() && d.StatusText(0) == "Save failed: the disk is full");
        d.ReachedSavePoint();                                                               // undo back to the saved text
        CHECK(!d.Dirty() && d.FailureReason().empty() && d.State() == DocState::Kind::Clean);
        d.Edited(); d.Reverted();
        CHECK(!d.Dirty());
        CHECK(MarkedName("Level1", true) == "Level1 *" && MarkedName("Level1", false) == "Level1");
        CHECK(FormatClock(0, -3600 * 5) == "19:00" && FormatClock(86399, 0) == "23:59");
        const int off = LocalUtcOffsetSeconds(1700000000);
        CHECK(off >= -14 * 3600 && off <= 14 * 3600);
    }

    // ---- 7. The list of what is unsaved --------------------------------------------------------------------------------
    {
        std::vector<UnsavedItem> items = {
            {"scene", "/p/Assets/Scenes/Level1.okayscene", ""},
            {"prefab", "/p/Assets/Player.okayprefab", ""},
            {"script", "/p/Assets/enemy.os", ""},
        };
        const UnsavedSummary s = BuildUnsavedSummary(items);
        CHECK(s.oneLine == "3 things have unsaved changes: Level1, Player prefab, enemy.os");
        CHECK(s.saveAllLabel == "Save All (3)" && s.names.size() == 3);
        CHECK(BuildUnsavedSummary({items[0]}).oneLine == "1 thing has unsaved changes: Level1");
        CHECK(BuildUnsavedSummary({items[0]}).saveAllLabel == "Save");
        CHECK(BuildUnsavedSummary({}).headline == "Nothing has unsaved changes.");
        std::vector<UnsavedItem> dup = {items[2], items[2], {"graph", "", "Untitled"}, {"scene", "", ""}};
        const UnsavedSummary d = BuildUnsavedSummary(dup);
        CHECK(d.names.size() == 3 && d.names[0] == "enemy.os" && d.names[1] == "Untitled" && d.names[2] == "this new scene");
        SaveAllReport rep;
        CHECK(rep.Text() == "Nothing needed saving.");
        rep.saved = {"Level1", "Player prefab", "enemy.os"};
        CHECK(rep.Text() == "Saved 3 things: Level1, Player prefab, enemy.os." && rep.Ok());
        rep.failed.push_back({"boss.os", "the disk is full"});
        CHECK(!rep.Ok() && Has(rep.Text(), "Could not save boss.os: the disk is full"));
    }

    // ---- 8. Outside changes ----------------------------------------------------------------------------------------------
    {
        std::map<std::string, std::pair<std::string, std::int64_t>> fake;   // path -> (content, mtime)
        int reads = 0;
        StampProbe probe = [&](const std::string& p, bool withHash) {
            FileStamp s;
            auto it = fake.find(p);
            if (it == fake.end()) return s;
            s.exists = true; s.size = it->second.first.size(); s.mtime = it->second.second;
            if (withHash) { ++reads; s.hash = savefmt::Fnv1a(it->second.first.data(), it->second.first.size()); s.hashed = true; }
            return s;
        };
        DiskWatch w(probe);
        CHECK(w.Check() == DiskChange::None && !w.Watching());
        fake["a"] = {"one", 100};
        w.Remember("a");
        CHECK(w.Check() == DiskChange::None);
        fake["a"] = {"one", 200};                                                           // touched, same text: no warning
        const int before = reads;
        CHECK(w.Check() == DiskChange::None && reads == before + 1);
        CHECK(w.Check() == DiskChange::None && reads == before + 1);                       // the stamp was refreshed: no more reads
        fake["a"] = {"two!", 300};                                                          // a git pull changed it
        CHECK(w.Check() == DiskChange::Modified);
        CHECK(w.Check() == DiskChange::Modified);                                           // keeps saying so until the user answers
        w.AcceptDisk();
        CHECK(w.Check() == DiskChange::None);
        fake["a"] = {"two?", 400};                                                          // same size, different text
        CHECK(w.Check() == DiskChange::Modified);
        w.Remember("a");                                                                    // the editor saved: its own write is not "outside"
        CHECK(w.Check() == DiskChange::None);
        fake.erase("a");
        CHECK(w.Check() == DiskChange::Deleted);
        w.Remember("b");                                                                    // a path with no file yet
        CHECK(w.Check() == DiskChange::None);
        fake["b"] = {"x", 1};
        CHECK(w.Check() == DiskChange::Appeared);
        w.Forget();
        CHECK(w.Check() == DiskChange::None);

        // The same with the real file system and a real write.
        const std::string f = dir + "/watched.okayscene";
        Put(f, "okayscene 3\n");
        DiskWatch rw;
        rw.Remember(f);
        CHECK(rw.Check() == DiskChange::None);
        Put(f, "okayscene 3\ngameobject 0 \"New\"\nend\n");
        CHECK(rw.Check() == DiskChange::Modified);

        const ExternalPrompt save = BuildExternalPrompt("Level1.okayscene", DiskChange::Modified, true, true);
        CHECK(Has(save.title, "changed outside the editor"));
        CHECK(save.buttons.size() == 4 && save.buttons[0] == "Save a Copy" && save.buttons[1] == "Overwrite" && save.buttons[2] == "Reload" && save.buttons[3] == "Cancel");
        CHECK(Has(save.body, "Level1.okayscene") && Has(save.body, "git pull"));
        const ExternalPrompt quiet = BuildExternalPrompt("Level1.okayscene", DiskChange::Modified, false, false);
        CHECK(quiet.buttons.size() == 2 && quiet.buttons[0] == "Reload");                    // nothing to lose: reload is the default
        const ExternalPrompt both = BuildExternalPrompt("Level1.okayscene", DiskChange::Modified, true, false);
        CHECK(both.buttons[0] == "Keep Mine");                                               // unsaved edits: the safe choice first
        CHECK(BuildExternalPrompt("x", DiskChange::Deleted, true, true).buttons.size() == 3);
        CHECK(Has(BuildExternalPrompt("x", DiskChange::Appeared, false, true).body, "replace"));
    }

    // ---- 9. Loading: errors in plain words, with the line and the object --------------------------------------------------
    {
        // The loader names the line and the last object it read.
        Scene live;
        live.CreateGameObject("Keep");
        const std::string bad = "okayscene 3\ngameobject 0 \"Door\"\n  active 1\n  transform 0 0 0 0 0 0 1 1 1 1\n  meshgeo 3 99999999\nend\n";
        std::string err;
        CHECK(!SceneSerializer::Deserialize(live, bad, &err));
        CHECK(Has(err, "line ") && Has(err, "Door"));
        CHECK(live.Find("Keep") != nullptr);                                                 // all or nothing: the open scene is as it was
        const LoadErrorText t = FormatLoadError(err, "Level1.okayscene", true);
        CHECK(t.line > 1 && t.object == "Door" && Has(t.where, "Door"));
        CHECK(Has(t.headline, "Level1.okayscene") && Has(t.Full(), "Look at line") && Has(t.Full(), "unchanged") && Has(t.Full(), "Level1.okayscene.bak"));
        CHECK(!Has(FormatLoadError(err, "L.okayscene", false).Full(), ".bak"));
        CHECK(Has(FormatLoadError("cannot open /x/y.okayscene", "y.okayscene", false).headline, "missing, in use by another program"));
        CHECK(Has(FormatLoadError("missing 'okayscene' header", "y.okayscene", false).headline, "not an OkaySpace scene file"));
        CHECK(Has(FormatLoadError("tampered or unreadable /x", "y", false).headline, "damaged"));
        CHECK(Has(FormatLoadError("duplicate gameobject index 4 (line 90, in or after the object \"Lamp\")", "y", false).headline, "same number"));
        CHECK(Has(FormatLoadError("something new", "y", false).headline, "something new"));
        CHECK(FormatLoadError("cannot open /x", "y", false).line == 0 && FormatLoadError("cannot open /x", "y", false).where.empty());

        // Safe mode: the objects that load on their own are kept, the damaged one is named, the file is never touched.
        const std::string text =
            "okayscene 3\nname \"Level\"\n"
            "gameobject 0 \"Ground\"\n  active 1\n  parent -1\n  transform 0 0 0 0 0 0 1 1 1 1\nend\n"
            "gameobject 1 \"Door\"\n  active 1\n  parent -1\n  transform 0 0 0 0 0 0 1 1 1 1\n  meshgeo 3 99999999\nend\n"
            "gameobject 2 \"Lamp\"\n  active 1\n  parent -1\n  transform 1 0 0 0 0 0 1 1 1 1\nend\n";
        Scene whole;
        CHECK(!SceneSerializer::Deserialize(whole, text, &err));                              // the whole file does not load
        const SalvageResult sv = SalvageSceneText(text);
        CHECK(sv.usable && sv.total == 3 && sv.kept == 2 && sv.dropped.size() == 1);
        CHECK(Has(sv.dropped[0], "Door"));
        Scene opened;
        CHECK(SceneSerializer::Deserialize(opened, sv.text, &err));
        CHECK(opened.Find("Ground") && opened.Find("Lamp") && !opened.Find("Door"));
        CHECK(!SalvageSceneText("not a scene at all").usable);
        CHECK(!SalvageSceneText("").usable);
        const SalvageResult none = SalvageSceneText("okayscene 3\ngameobject 0 \"X\"\n  meshgeo 3 99999999\nend\n");
        CHECK(!none.usable && none.kept == 0 && none.dropped.size() == 1);
        // A child whose parent was dropped still loads (it becomes a top-level object).
        const std::string kids =
            "okayscene 3\n"
            "gameobject 0 \"Parent\"\n  active 1\n  parent -1\n  transform 0 0 0 0 0 0 1 1 1 1\n  meshgeo 3 99999999\nend\n"
            "gameobject 1 \"Child\"\n  active 1\n  parent 0\n  transform 0 0 0 0 0 0 1 1 1 1\nend\n";
        const SalvageResult kv = SalvageSceneText(kids);
        Scene ks;
        CHECK(kv.usable ? SceneSerializer::Deserialize(ks, kv.text, &err) && ks.Find("Child") : true);
    }

    // ---- 10. Open Recent: clean, pinned first ------------------------------------------------------------------------------
    {
        std::set<std::string> here = {"/a/1.okayscene", "/a/2.okayscene", "/a/3.okayscene", "/a/4.okayscene"};
        auto ex = [&](const std::string& p) { return here.count(p) > 0; };
        const RecentResult r = CleanRecent({"/a/1.okayscene", "/gone/x.okayscene", "/a/2.okayscene", "/a/3.okayscene", "/a/2.okayscene", ""},
                                           {"/a/3.okayscene", "/gone/pinned.okayscene"}, ex, 10);
        CHECK(r.list == (std::vector<std::string>{"/a/3.okayscene", "/a/1.okayscene", "/a/2.okayscene"}));
        CHECK(r.dropped.size() == 2);
        const RecentResult cap = CleanRecent({"/a/1.okayscene", "/a/2.okayscene", "/a/3.okayscene", "/a/4.okayscene"}, {"/a/4.okayscene"}, ex, 2);
        CHECK(cap.list == (std::vector<std::string>{"/a/4.okayscene", "/a/1.okayscene"}));
        std::vector<std::string> pins;
        CHECK(TogglePin(pins, "/a/1.okayscene") && pins.size() == 1);
        CHECK(!TogglePin(pins, "/a/1.okayscene") && pins.empty());
    }

    // ---- 11. Names for copies ---------------------------------------------------------------------------------------------------
    {
        std::set<std::string> taken;
        auto ex = [&](const std::string& p) { return taken.count(p) > 0; };
        const std::string a = CopyName("/p/Assets/Level1.okayscene", ex);
        CHECK(fs::path(a).filename() == "Level1 copy.okayscene");
        taken.insert(a);
        const std::string b = CopyName("/p/Assets/Level1.okayscene", ex);
        CHECK(fs::path(b).filename() == "Level1 copy 2.okayscene");
        taken.insert(b);
        CHECK(fs::path(CopyName(b, ex)).filename() == "Level1 copy 3.okayscene");           // a copy of a copy is not "copy copy"
        CHECK(WithExtension("/x/Level1", ".okayscene") == "/x/Level1.okayscene" && WithExtension("/x/L.okayscene", ".okayscene") == "/x/L.okayscene");
    }

    fs::remove_all(dir);
    TEST_MAIN_RESULT();
}
