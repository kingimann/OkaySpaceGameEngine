// Upgrading a project to the version of the editor that opens it (editor/ProjectUpgrade.*, no ImGui): the backup,
// the check of every scene and prefab through the engine's loader, the rewrite of only what changes, the stamp,
// the restore, and the gate that asks before anything in the project is touched. Everything runs on temp folders;
// the failure tests make the disk "refuse" at every step in turn and require the project to come out byte for byte
// as it went in. Nothing sleeps or reads the clock.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Scene/SceneMigration.hpp"
#include "../editor/ProjectUpgrade.hpp"
#include "../editor/ProjectState.hpp"

#include <chrono>
#include <fstream>
#include <map>
#include <random>
#include <set>

namespace fs = std::filesystem;
using namespace okay;
using namespace okayup;

namespace {

struct TempDir {
    fs::path path;
    TempDir() {
        static int n = 0;
        path = fs::temp_directory_path() / ("okay_projup_" + std::to_string(++n) + "_" +
               std::to_string((long long)std::chrono::steady_clock::now().time_since_epoch().count()));
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::create_directories(path, ec);
    }
    ~TempDir() {
        std::error_code ec;
        for (auto& e : fs::recursive_directory_iterator(path, ec)) fs::permissions(e.path(), fs::perms::owner_all, fs::perm_options::add, ec);
        fs::remove_all(path, ec);
    }
};

void WriteFile(const fs::path& p, const std::string& s) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream(p, std::ios::binary | std::ios::trunc) << s;
}
std::string ReadFile(const fs::path& p) { std::string s; okayproj::ReadText(p.string(), s); return s; }
bool Has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

using Tree = std::map<std::string, std::string>;
Tree Snap(const fs::path& root) {
    Tree t;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string rel = it->path().lexically_relative(root).generic_string();
        if (it->is_symlink(ec)) t[rel] = "<link>" + fs::read_symlink(it->path(), ec).string();
        else if (it->is_directory(ec)) t[rel] = "<dir>";
        else t[rel] = ReadFile(it->path());
    }
    return t;
}
// What a backup should hold: everything but the folders it leaves out.
Tree Kept(Tree t) {
    for (auto it = t.begin(); it != t.end();) {
        const std::string& k = it->first;
        const bool skip = k == "build" || k.rfind("build/", 0) == 0 || k == ".git" || k.rfind(".git/", 0) == 0 ||
                          k == ".okay/backups" || k.rfind(".okay/backups/", 0) == 0;
        it = skip ? t.erase(it) : std::next(it);
    }
    return t;
}
// The backup folder's tree without the manifest the editor adds.
Tree WithoutManifest(Tree t) { t.erase(".okay/upgrade_backup.txt"); return t; }

std::string Current(const std::string& name, int objects) {
    Scene s(name);
    for (int i = 0; i < objects; ++i) {
        GameObject* g = s.CreateGameObject("Obj" + std::to_string(i));
        g->transform->localPosition = Vec3{(float)i, 1.0f, 0.0f};
    }
    return SceneSerializer::Serialize(s);
}
std::string WithVersion(const std::string& text, int version) { return "okayscene " + std::to_string(version) + text.substr(text.find('\n')); }
std::string PrefabText() {
    Scene s("p");
    GameObject* g = s.CreateGameObject("Thing");
    g->transform->localPosition = Vec3{1, 2, 0};
    return SceneSerializer::SerializeObject(*g);
}
std::string CharacterScene(int version) {
    Scene s("Chars");
    GameObject* p = s.CreateGameObject("Player");
    p->AddComponent<Character>()->Apply();
    return WithVersion(SceneSerializer::Serialize(s), version);
}
const char* kV1 =
    "okayscene 1\nname \"Old Level\"\ngravity 0 -9.81\n"
    "gameobject 0 \"Hero\"\n  active 1\n  parent -1\n  transform 1 2 0 0 0 0 1 1 1 1\n  sprite 64 1 0 0 1 1 1\n  rigidbody2d 1 1 1 0 0\n  boxcollider2d 1 1 0 0 0\nend\n"
    "gameobject 1 \"Cam\"\n  active 1\n  parent 0\n  transform 0 0 -10 0 0 0 1 1 1 1\n  camera 0 5 60 0.1 0.1 0.1 1 1\nend\n";

// A project with every kind of thing in it: scenes of three format versions, prefabs of two, other assets, a unicode
// name, a space in the folder's name, an empty folder, and the folders a backup leaves out.
struct Proj {
    fs::path base, root;
    explicit Proj(const fs::path& b, std::size_t bigBytes = 1200 * 1024) : base(b), root(b / "My Game \xC3\xBC") {
        WriteFile(root / "project.okayproj", "company=Me\nversion=1.0.0\nfuture_setting=on\n");
        WriteFile(root / ".okay" / "build_live.cfg", "@profile live\nproduct=My Game\ndir=build/My Game\n");
        WriteFile(root / ".okay" / "scene_build.txt", "1|Assets/Scenes/Level1.okayscene\n");
        WriteFile(root / ".okay" / "backups" / "older" / "keep.txt", "an older backup, never copied again");
        WriteFile(root / "Assets" / "Scenes" / "Level1.okayscene", Current("Level1", 4));
        WriteFile(root / "Assets" / "Scenes" / "Messy.okayscene", Current("Messy", 2) + "\n\n\n");
        WriteFile(root / "Assets" / "Scenes" / "Old1.okayscene", kV1);
        WriteFile(root / "Assets" / "Scenes" / "Chars.okayscene", CharacterScene(1));
        WriteFile(root / "Assets" / "Prefabs" / "Fresh.okayprefab", PrefabText());
        WriteFile(root / "Assets" / "Prefabs" / "Aged.okayprefab", WithVersion(PrefabText(), 2));
        std::string big;
        std::mt19937 rng(7);
        for (std::size_t i = 0; i < bigBytes; ++i) big += static_cast<char>(rng() & 0xFF);   // binary, NULs included
        WriteFile(root / "Assets" / "Textures" / "pic.png", big);
        WriteFile(root / "Assets" / "Textures" / "\xC3\xBCn\xC3\xAF c\xC3\xB8" "d\xC3\xA9 \xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E.txt", "unicode and spaces\r\n");
        WriteFile(root / "Assets" / "Scripts" / "Player.okay", "class Player { }\n");
        WriteFile(root / "items.okayitems", "sword 1\n");
        WriteFile(root / "build" / "out" / "game.bin", "build output");
        WriteFile(root / ".git" / "HEAD", "ref: refs/heads/main\n");
        std::error_code ec;
        fs::create_directories(root / "Assets" / "Empty", ec);
    }
    Options Opts(const std::string& from = "", bool migrate = true) const {
        Options o;
        o.projectDir = root.string();
        o.fromVersion = from;
        o.toVersion = "2.855.0";
        o.migrate = migrate;
        return o;
    }
};

std::set<std::string> Names(const fs::path& dir) {
    std::set<std::string> s;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) s.insert(it->path().filename().string());
    return s;
}
bool NoPartial(const fs::path& base) {
    for (const std::string& n : Names(base)) if (n.size() > 8 && n.compare(n.size() - 8, 8, ".partial") == 0) return false;
    return true;
}
std::size_t BackupCount(const Proj& p) {
    std::size_t n = 0;
    for (const std::string& s : Names(p.base)) if (s.find("(backup") != std::string::npos) ++n;
    return n;
}

bool Loads(const fs::path& p) {
    Scene s("probe");
    std::string err;
    return SceneSerializer::LoadFromFile(s, p.string(), &err);
}

}  // namespace

int main() {
    RUN_SUITE("project_upgrade");
    const std::string kChanged1 = "Assets/Scenes/Chars.okayscene", kChanged2 = "Assets/Scenes/Old1.okayscene", kChanged3 = "Assets/Prefabs/Aged.okayprefab";

    // ---- where a project is ---------------------------------------------------------------------------------------
    {
        TempDir td;
        Proj p(td.path);
        const std::string root = p.root.string();
        CHECK(ProjectRootForScene((p.root / "Assets" / "Scenes" / "Level1.okayscene").string()) == root);
        CHECK(ProjectRootForScene((p.root / "Assets" / "Scenes" / "Deeper" / "x.okayscene").string()) == root);   // the scene needn't exist: Assets does
        WriteFile(p.root / "Assets" / "Scenes" / "Deeper" / "x.okayscene", "x");
        CHECK(ProjectRootForScene((p.root / "Assets" / "Scenes" / "Deeper" / "x.okayscene").string()) == root);
        CHECK(ProjectRootForScene((p.root / "Assets" / "Main.okayscene").string()) == root);
        CHECK(ProjectRootForScene((p.root / "Main.okayscene").string()) == root);                              // the old flat layout: project.okayproj beside it
        WriteFile(td.path / "loose" / "Desktop.okayscene", "x");
        CHECK(ProjectRootForScene((td.path / "loose" / "Desktop.okayscene").string()).empty());                // a loose file is no project
        CHECK(ProjectRootForScene("").empty());
        CHECK(IsProjectRoot(p.root) && !IsProjectRoot(td.path / "loose") && !IsProjectRoot(td.path / "nowhere"));
        CHECK(RootKey(root) == RootKey(root + "/") && RootKey(root) == RootKey((p.root / "Assets" / "..").string()));
    }

    // ---- the keys project.okayproj's writer owns ---------------------------------------------------------------------
    {
        const std::string generated = oked::WriteProjectSettings(oked::ProjectSettings{}, "tag=Enemy\nstrict_tags=1\nlayer_name_3=Walls\n");
        std::size_t known = 0;
        std::istringstream in(generated);
        std::string line;
        while (std::getline(in, line)) {
            const std::size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            CHECK(IsKnownProjectKey(line.substr(0, eq)));       // every key the editor writes is its own
            ++known;
        }
        CHECK(known >= 17);
        CHECK(!IsKnownProjectKey("engine_version") && !IsKnownProjectKey("future_setting") && !IsKnownProjectKey("") && !IsKnownProjectKey("layers"));

        TempDir td;
        const std::string file = (td.path / "project.okayproj").string();
        WriteFile(file, "company=Old\nfuture_setting=on\n# a note\nengine_version=2.850.0\n");
        const std::string text = ProjectFileText(file, generated, "2.855.0");
        CHECK(text.rfind("engine_version=2.855.0\n", 0) == 0);
        CHECK(Has(text, "future_setting=on\n") && Has(text, "# a note\n") && !Has(text, "company=Old") && Has(text, "company=OkaySpace"));
        oked::ProjectSettings back;                              // an older editor loads it: unknown lines are skipped
        oked::ParseProjectSettings(back, text);
        CHECK(std::string(back.company) == "OkaySpace");
        CHECK(okayver::ReadProjectVersion(td.path.string()) == "2.850.0");                                      // the file on disk is not touched by composing the text
        CHECK(okayproj::WriteTextAtomic(file, text) && okayver::ReadProjectVersion(td.path.string()) == "2.855.0");
        CHECK(ProjectFileText(file, generated, "dev").rfind("engine_version=2.855.0\n", 0) == 0);             // a dev build keeps the stamp
        CHECK(ProjectFileText((td.path / "none.okayproj").string(), generated, "dev") == generated);          // and adds none

        std::string err;
        CHECK(StampProject(td.path.string(), "dev", &err) && okayver::ReadProjectVersion(td.path.string()) == "2.855.0");   // nothing for a dev build
        CHECK(StampProject(td.path.string(), "2.860.0", &err) && okayver::ReadProjectVersion(td.path.string()) == "2.860.0");
        CHECK(Has(ReadFile(file), "future_setting=on"));
        fs::remove(file);
        CHECK(StampProject(td.path.string(), "2.860.0", &err) && !fs::exists(file));                        // no file yet: none is made
        CHECK(StampProject("", "2.860.0", &err));
    }

    // ---- the upgrade, end to end -----------------------------------------------------------------------------------
    {
        TempDir td;
        Proj p(td.path);
        const Tree before = Snap(p.root);
        UpgradeJob job(p.Opts(""));
        CHECK(job.progress().phase == Phase::Scanning && !job.Finished());
        job.Run();
        const Result& r = job.result();
        if (!r.ok) std::cerr << "  upgrade failed: " << r.error << "\n";
        CHECK(r.ok && job.Finished() && r.error.empty() && !r.cancelled && job.progress().phase == Phase::Done);
        CHECK(r.scenes == 4 && r.prefabs == 2);                                  // every scene and prefab was loaded
        CHECK(r.rewritten == 3);                                                 // ... and only the ones that had to change were written
        CHECK(r.changed == (std::vector<std::string>{kChanged1, kChanged2, kChanged3, "project.okayproj"}) ||
              r.changed == (std::vector<std::string>{kChanged3, kChanged1, kChanged2, "project.okayproj"}));
        CHECK(!r.log.empty());
        CHECK(fs::path(r.backupDir).filename().string() == "My Game \xC3\xBC (backup before v2.855.0)");
        CHECK(fs::path(r.backupDir).parent_path() == p.base && NoPartial(p.base));

        // the backup holds everything but the folders it leaves out, byte for byte, and says what it is
        const Tree backup = Snap(r.backupDir);
        CHECK(WithoutManifest(backup) == Kept(before));
        CHECK(backup.count("Assets/Empty") && backup.at("Assets/Empty") == "<dir>");
        CHECK(!backup.count("build/out/game.bin") && !backup.count(".git/HEAD") && !backup.count(".okay/backups/older/keep.txt"));
        CHECK(Has(backup.at(".okay/upgrade_backup.txt"), "okayspace-upgrade-backup 1") && Has(backup.at(".okay/upgrade_backup.txt"), "to=2.855.0"));
        CHECK(r.files > 10 && r.bytes > 1000 * 1024);                            // the files copied (not folders)

        // the project: old scenes and prefabs now current, the rest untouched, stamped
        const Tree after = Snap(p.root);
        for (const auto& kv : before) {
            if (kv.first == kChanged1 || kv.first == kChanged2 || kv.first == kChanged3 || kv.first == "project.okayproj") continue;
            CHECK(after.count(kv.first) && after.at(kv.first) == kv.second);    // not one other byte changed (Messy keeps its blank lines)
        }
        CHECK(after.size() == before.size());                                    // nothing added, nothing lost
        for (const std::string& rel : {kChanged1, kChanged2, kChanged3}) {
            CHECK(after.at(rel) != before.at(rel) && after.at(rel).rfind("okayscene 3\n", 0) == 0);
            CHECK(Loads(p.root / rel) || rel == kChanged3);                      // a prefab is not a scene file, but it parses as one
        }
        CHECK(Has(after.at(kChanged1), "animsm"));                               // the engine's own upgrade: the Character got its Animator
        CHECK(after.at("project.okayproj") == before.at("project.okayproj") + "engine_version=2.855.0\n");     // one line added, the rest as it was
        CHECK(okayver::Decide(okayver::ReadProjectVersion(p.root.string()), "2.855.0") == okayver::Decision::Same);
        CHECK(!fs::exists(p.root / "project.okayproj.tmp") && !fs::exists(p.root / "Assets" / "Scenes" / "Old1.okayscene.tmp"));

        // it is idempotent: a second run changes nothing in the project and makes a second backup
        const Tree again = Snap(p.root);
        UpgradeJob j2(p.Opts("2.855.0"));
        j2.Run();
        CHECK(j2.result().ok && j2.result().rewritten == 0 && j2.result().changed.empty() && j2.result().scenes == 4 && j2.result().prefabs == 2);
        CHECK(Snap(p.root) == again);
        CHECK(fs::path(j2.result().backupDir).filename().string() == "My Game \xC3\xBC (backup v2.855.0)");
        CHECK(WithoutManifest(Snap(j2.result().backupDir)) == Kept(again));      // the earlier backups are siblings, not inside it
        UpgradeJob j3(p.Opts("2.855.0"));
        j3.Run();
        CHECK(j3.result().ok && fs::path(j3.result().backupDir).filename().string() == "My Game \xC3\xBC (backup v2.855.0) (2)");
        CHECK(BackupCount(p) == 3);
    }

    // ---- the backup's name and what a listing says about it -----------------------------------------------------------
    {
        CHECK(BackupFolderName("Game", "2.850.0", "2.855.0") == "Game (backup v2.850.0)");
        CHECK(BackupFolderName("Game", "v2.850", "2.855.0") == "Game (backup v2.850.0)");
        CHECK(BackupFolderName("Game", "", "2.855.0") == "Game (backup before v2.855.0)");
        CHECK(BackupFolderName("Game", "junk", "dev") == "Game (backup)");
        TempDir td;
        Proj p(td.path);
        UpgradeJob j(p.Opts("2.850.0"));
        j.Run();
        CHECK(j.result().ok && fs::path(j.result().backupDir).filename().string() == "My Game \xC3\xBC (backup v2.850.0)");
        fs::create_directories(p.base / "My Game \xC3\xBC (backup by hand)");                       // someone's own folder: listed, not restorable
        fs::create_directories(p.base / "My Game \xC3\xBC (backup v1.0.0).partial");               // unfinished: never offered
        fs::create_directories(p.base / "Other (backup v2.850.0)");                                 // another project's
        const std::vector<BackupInfo> list = ListBackups(p.root.string());
        CHECK(list.size() == 2);
        const BackupInfo* mine = nullptr;
        for (const BackupInfo& b : list) if (b.restorable) mine = &b;
        CHECK(mine && mine->fromVersion == "2.850.0" && mine->toVersion == "2.855.0" && mine->mode == "upgrade" && !mine->made.empty());
        CHECK(mine && mine->changed.size() == 4 && mine->created.empty() && mine->files > 10);
        CHECK(ListBackups((td.path / "nothing").string()).empty());
    }

    // ---- free space ---------------------------------------------------------------------------------------------------
    {
        TempDir td;
        Proj p(td.path);
        const Tree before = Snap(p.root);
        Options o = p.Opts("2.850.0");
        std::uintmax_t asked = 0;
        o.availableSpace = [&](const fs::path& where, std::error_code& ec) { ec.clear(); asked = 1; (void)where; return std::uintmax_t(1024 * 1024); };
        UpgradeJob j(o);
        j.Run();
        CHECK(asked == 1 && !j.result().ok && Has(j.result().error, "free space") && Has(j.result().error, "Nothing was changed"));
        CHECK(Snap(p.root) == before && BackupCount(p) == 0 && NoPartial(p.base));
        CHECK(j.result().backupDir.empty() && j.result().rolledBack && Has(j.result().restoreNote, "Nothing in the project was changed"));
        // exactly enough is enough; one byte short is not
        const std::uintmax_t need = [&] { std::uintmax_t n = 0; for (const auto& kv : Kept(before)) if (kv.second != "<dir>" && kv.second.rfind("<link>", 0) != 0) n += kv.second.size(); return n; }();
        o.spaceMargin = 0;
        o.availableSpace = [&](const fs::path&, std::error_code& ec) { ec.clear(); return need; };
        UpgradeJob ok(o);
        ok.Run();
        CHECK(ok.result().ok);
        fs::remove_all(ok.result().backupDir);
        o.availableSpace = [&](const fs::path&, std::error_code& ec) { ec.clear(); return need - 1; };
        UpgradeJob tight(o);
        tight.Run();
        CHECK(!tight.result().ok && Has(tight.result().error, "free space") && BackupCount(p) == 0);
    }

    // ---- the disk refuses at every step in turn -------------------------------------------------------------------------
    {
        TempDir clean;
        Proj pc(clean.path, 300 * 1024);
        std::vector<std::pair<std::string, std::string>> calls;
        Options o = pc.Opts("2.850.0");
        o.fail = [&](const char* stage, const std::string& what) { calls.emplace_back(stage, what); return false; };
        UpgradeJob cj(o);
        cj.Run();
        CHECK(cj.result().ok);
        std::set<std::string> stages;
        for (const auto& c : calls) stages.insert(c.first);
        for (const char* s : {"scan", "analyze", "space", "backup-copy", "backup-verify", "backup-manifest", "backup-rename", "apply", "stamp"}) CHECK(stages.count(s) == 1);
        CHECK(calls.size() > 30);

        int failedRuns = 0, afterRename = 0;
        for (std::size_t i = 0; i < calls.size(); ++i) {
            TempDir td;
            Proj p(td.path, 300 * 1024);
            const Tree before = Snap(p.root);
            std::size_t n = 0;
            Options fo = p.Opts("2.850.0");
            fo.fail = [&](const char* stage, const std::string&) { return std::string(stage) != "restore" && n++ == i; };
            UpgradeJob j(fo);
            j.Run();
            const Result& r = j.result();
            const bool bad = r.ok || Snap(p.root) != before || !NoPartial(p.base) || r.error.empty() || r.restoreNote.empty() || !r.rolledBack || j.progress().phase != Phase::Failed;
            if (bad) std::cerr << "  injected failure #" << i << " at " << calls[i].first << " " << calls[i].second << ": ok=" << r.ok << " error='" << r.error
                               << "' rolledBack=" << r.rolledBack << " tree-equal=" << (Snap(p.root) == before) << " partial-free=" << NoPartial(p.base) << "\n";
            CHECK(!bad);                                                          // the project is exactly as it was, and the reason is given
            ++failedRuns;
            const std::string st = calls[i].first;
            if (st == "apply" || st == "stamp") {                                // past the backup: it stays, whole, and holds the originals
                ++afterRename;
                CHECK(BackupCount(p) == 1 && !r.backupDir.empty() && WithoutManifest(Snap(r.backupDir)) == Kept(before));
            } else {
                CHECK(BackupCount(p) == 0 && r.backupDir.empty());              // no half-made backup either
            }
        }
        CHECK(failedRuns == (int)calls.size() && afterRename == 4);
    }

    // a failed rollback says which file could not be put back, and the backup still has the original
    {
        TempDir td;
        Proj p(td.path, 100 * 1024);
        const Tree before = Snap(p.root);
        int applies = 0;
        Options o = p.Opts("2.850.0");
        o.fail = [&](const char* stage, const std::string& what) {
            if (std::string(stage) == "apply" && ++applies == 3) return true;      // the third rewrite fails ...
            return std::string(stage) == "restore" && Has(what, "Chars.okayscene");  // ... and putting one of the first two back fails too
        };
        UpgradeJob j(o);
        j.Run();
        CHECK(!j.result().ok && !j.result().rolledBack && Has(j.result().restoreNote, "Couldn't put back everything") && Has(j.result().restoreNote, "Chars.okayscene"));
        CHECK(Has(j.result().restoreNote, "backup") && !j.result().backupDir.empty());
        CHECK(Snap(j.result().backupDir).at(kChanged1) == before.at(kChanged1));    // the original is safe in the backup
        const Tree now = Snap(p.root);
        int differing = 0;
        for (const auto& kv : before) if (now.at(kv.first) != kv.second) { ++differing; CHECK(kv.first == kChanged1); }
        CHECK(differing == 1);                                                      // everything else went back
    }

    // ---- scenes that are damaged, too new, or read-only stop everything before anything is made ---------------------
    {
        struct Bad { const char* what; std::string text; const char* wantInError; };
        const std::string good = Current("g", 1);
        std::string cut = good;
        cut.erase(cut.rfind("end\n"));
        const std::vector<Bad> bads = {
            {"not a scene", "this is not a scene at all\n", "can't be upgraded"},
            {"empty", "", "can't be upgraded"},
            {"cut off", cut, "can't be upgraded"},
            {"a record cut short", "okayscene 3\ngameobject 0 \"A\"\n  active 1\n  parent -1\n  transform 1 2\nend\n", "can't be"},
            {"newer than this build", WithVersion(good, 99), "newer"},
            {"binary junk", std::string("okayscene 3\n\x01\x02\x03\xFF\xFE", 16), "can't be"},
        };
        for (const Bad& b : bads) {
            TempDir td;
            Proj p(td.path);
            WriteFile(p.root / "Assets" / "Scenes" / "Zbad.okayscene", b.text);
            const Tree before = Snap(p.root);
            UpgradeJob j(p.Opts("2.850.0"));
            j.Run();
            const bool good2 = !j.result().ok && Has(j.result().error, "Zbad.okayscene") && Has(j.result().error, b.wantInError);
            if (!good2) std::cerr << "  " << b.what << ": ok=" << j.result().ok << " error='" << j.result().error << "'\n";
            CHECK(good2);
            CHECK(Snap(p.root) == before);                                           // everything as it was ...
            CHECK(BackupCount(p) == 0 && NoPartial(p.base) && j.result().backupDir.empty());   // ... and no backup was started
            CHECK(j.result().rolledBack && Has(j.result().restoreNote, "Nothing in the project was changed"));
        }
        // a damaged prefab the same way
        TempDir td;
        Proj p(td.path);
        WriteFile(p.root / "Assets" / "Prefabs" / "Zbad.okayprefab", "junk");
        const Tree before = Snap(p.root);
        UpgradeJob j(p.Opts("2.850.0"));
        j.Run();
        CHECK(!j.result().ok && Has(j.result().error, "Zbad.okayprefab") && Snap(p.root) == before && BackupCount(p) == 0);

        // read-only: a scene that has to change, and the project file that has to be stamped
        for (const char* relc : {kChanged2.c_str(), "project.okayproj"}) {
            const std::string rel = relc;
            TempDir t2;
            Proj q(t2.path);
            const Tree b2 = Snap(q.root);
            fs::permissions(q.root / fs::path(rel), fs::perms::owner_read | fs::perms::group_read | fs::perms::others_read);
            UpgradeJob ro(q.Opts("2.850.0"));
            ro.Run();
            CHECK(!ro.result().ok && Has(ro.result().error, "read-only") && Has(ro.result().error, fs::path(rel).filename().string()));
            CHECK(Snap(q.root) == b2 && BackupCount(q) == 0 && NoPartial(q.base));
        }
        // a read-only scene that does NOT have to change is no problem
        TempDir t3;
        Proj q(t3.path);
        fs::permissions(q.root / "Assets" / "Scenes" / "Level1.okayscene", fs::perms::owner_read | fs::perms::group_read | fs::perms::others_read);
        UpgradeJob ro(q.Opts("2.850.0"));
        ro.Run();
        CHECK(ro.result().ok);
    }

    // ---- a project folder or backup place that can't be used --------------------------------------------------------------
    {
        TempDir td;
        Proj p(td.path);
        const Tree before = Snap(p.root);
        Options o = p.Opts("2.850.0");
        o.projectDir = (td.path / "nowhere").string();
        UpgradeJob none(o);
        none.Run();
        CHECK(!none.result().ok && Has(none.result().error, "isn't there"));
        o = p.Opts("2.850.0");
        o.backupParent = (td.path / "no such place").string();
        UpgradeJob nowhere(o);
        nowhere.Run();
        CHECK(!nowhere.result().ok && Has(nowhere.result().error, "isn't there") && Snap(p.root) == before);
        o = p.Opts("2.850.0");
        o.toVersion = "dev";                                                         // a development build has no version to upgrade to
        UpgradeJob dev(o);
        dev.Run();
        CHECK(!dev.result().ok && Has(dev.result().error, "development build") && Snap(p.root) == before && BackupCount(p) == 0);
        o.migrate = false;                                                           // ... but it can still make a backup
        UpgradeJob bk(o);
        bk.Run();
        CHECK(bk.result().ok && BackupCount(p) == 1 && Snap(p.root) == before);
        // a backup place inside the project is not copied into itself
        TempDir t2;
        Proj q(t2.path);
        Options in = q.Opts("2.850.0");
        in.backupParent = q.root.string();
        UpgradeJob inside(in);
        inside.Run();
        CHECK(inside.result().ok && !Has(Snap(inside.result().backupDir).begin()->first, "(backup"));
        for (const auto& kv : Snap(inside.result().backupDir)) CHECK(!Has(kv.first, "(backup"));
    }

    // ---- the folders a backup leaves out, links, and a file that changes while it is copied ------------------------------
    {
        TempDir td;
        Proj p(td.path);
        WriteFile(p.root / "Export" / "Win" / "game.exe", "output");                // a Build Settings output folder inside the project
        WriteFile(p.root / "Export" / "Notes.txt", "kept");
        WriteFile(p.root / "Assets" / "Models" / ".git" / "x", "nested vcs folder");
        std::error_code ec;
        fs::create_symlink("Level1.okayscene", p.root / "Assets" / "Scenes" / "Linked.okayscene", ec);
        const bool haveLinks = !ec;
        Options o = p.Opts("2.850.0");
        o.skipDirs = {(p.root / "Export" / "Win").string()};
        UpgradeJob j(o);
        j.Run();
        CHECK(j.result().ok);
        const Tree b = Snap(j.result().backupDir);
        CHECK(!b.count("Export/Win/game.exe") && !b.count("Export/Win") && b.count("Export/Notes.txt") && !b.count("Assets/Models/.git/x"));
        if (haveLinks) {
            CHECK(b.count("Assets/Scenes/Linked.okayscene") && b.at("Assets/Scenes/Linked.okayscene") == "<link>Level1.okayscene");   // copied as a link, not followed
            CHECK(Snap(p.root).at("Assets/Scenes/Linked.okayscene") == "<link>Level1.okayscene");
        }
    }
    {   // a file that changes between the check and the copy: the upgrade stops, nothing is lost
        for (const bool sameSize : {false, true}) {
            TempDir td;
            Proj p(td.path, 50 * 1024);
            std::string touched;
            Options o = p.Opts("2.850.0");
            o.fail = [&](const char* stage, const std::string& what) {
                if (std::string(stage) == "backup-copy" && what == kChanged2) {
                    std::string t = ReadFile(p.root / kChanged2);
                    t = sameSize ? "okayscene 1" + t.substr(11, t.size() - 12) + "X" : t + "\n# someone else's edit\n";
                    WriteFile(p.root / kChanged2, t);
                    touched = t;
                }
                return false;
            };
            UpgradeJob j(o);
            j.Run();
            CHECK(!j.result().ok && Has(j.result().error, "changed while") && NoPartial(p.base) && BackupCount(p) == 0);
            CHECK(ReadFile(p.root / kChanged2) == touched);                          // the other program's edit is still there
        }
    }

    // ---- cancel, progress, and not blocking ---------------------------------------------------------------------------------
    {
        TempDir td;
        Proj p(td.path, 3 * 1024 * 1024 + 17);
        const Tree before = Snap(p.root);
        UpgradeJob j(p.Opts("2.850.0"));
        double last = -1.0;
        int steps = 0;
        bool monotone = true, countsOk = true;
        while (j.Step(std::chrono::milliseconds(0))) {
            const Progress pr = j.progress();
            if (pr.fraction + 1e-9 < last) monotone = false;
            if (pr.total && pr.done > pr.total) countsOk = false;
            last = pr.fraction;
            ++steps;
        }
        CHECK(j.result().ok && monotone && countsOk && steps > 20);                  // many small steps: the UI is never held for a big file
        CHECK(j.progress().fraction > 0.999 && j.progress().phase == Phase::Done);
        CHECK(WithoutManifest(Snap(j.result().backupDir)) == Kept(before));         // the 3 MB binary file arrived whole, in chunks
        CHECK(!j.Step(std::chrono::milliseconds(5)));                                // finished jobs stay finished

        TempDir t2;
        Proj q(t2.path, 3 * 1024 * 1024);
        const Tree b2 = Snap(q.root);
        UpgradeJob c(q.Opts("2.850.0"));
        for (int i = 0; i < 16; ++i) c.Step(std::chrono::milliseconds(0));          // into the middle of the copy
        CHECK(!c.Finished() && c.progress().phase == Phase::Backup && c.progress().done > 0 && c.progress().total > 0);
        CHECK(fs::exists(fs::path(c.result().backupDir.empty() ? (q.base / "My Game \xC3\xBC (backup v2.850.0).partial").string() : c.result().backupDir)));
        c.Cancel();
        c.Run();
        CHECK(!c.result().ok && c.result().cancelled && c.result().error == "Cancelled.");
        CHECK(Snap(q.root) == b2 && BackupCount(q) == 0 && NoPartial(q.base));       // the half-made backup is gone
        CHECK(Names(q.base) == std::set<std::string>{"My Game \xC3\xBC"});
    }

    // ---- an older format scene migrates; a current one is not touched -------------------------------------------------------
    {
        TempDir td;
        Proj p(td.path);
        const std::string level1 = ReadFile(p.root / "Assets" / "Scenes" / "Level1.okayscene");
        const auto mtimeBefore = fs::last_write_time(p.root / "Assets" / "Scenes" / "Level1.okayscene");
        UpgradeJob j(p.Opts("2.850.0"));
        j.Run();
        CHECK(j.result().ok);
        CHECK(ReadFile(p.root / "Assets" / "Scenes" / "Level1.okayscene") == level1);
        CHECK(fs::last_write_time(p.root / "Assets" / "Scenes" / "Level1.okayscene") == mtimeBefore);     // a current scene is not even rewritten
        const std::string old1 = ReadFile(p.root / kChanged2);
        CHECK(old1.rfind("okayscene 3\n", 0) == 0 && Has(old1, "gameobject 0 \"Hero\"") && Has(old1, "name \"Old Level\""));
        Scene a("a"), b("b");
        std::string e1, e2;
        CHECK(SceneSerializer::Deserialize(a, kV1, &e1) && SceneSerializer::Deserialize(b, old1, &e2));
        CHECK(a.Objects().size() == b.Objects().size() && a.Objects().size() == 2);
        // it has no "recently changed" side effects: a second pass finds the file already current
        CHECK(SceneMigration::MigrateThroughEngine(old1).changed == false);
        // the log says what happened for the file that needed the engine
        bool mentioned = false;
        for (const std::string& l : j.result().log) if (Has(l, "Chars.okayscene")) mentioned = true;
        CHECK(mentioned);
    }

    // ---- a backup made before opening a newer project changes nothing in it ---------------------------------------------------
    {
        TempDir td;
        Proj p(td.path);
        const Tree before = Snap(p.root);
        UpgradeJob j(p.Opts("2.860.0", /*migrate=*/false));
        j.Run();
        CHECK(j.result().ok && j.result().rewritten == 0 && j.result().changed.empty() && j.result().scenes == 0);   // nothing is even loaded
        CHECK(Snap(p.root) == before);
        CHECK(fs::path(j.result().backupDir).filename().string() == "My Game \xC3\xBC (backup v2.860.0)");
        CHECK(WithoutManifest(Snap(j.result().backupDir)) == Kept(before) && Has(Snap(j.result().backupDir).at(".okay/upgrade_backup.txt"), "mode=backup"));
        const std::vector<BackupInfo> list = ListBackups(p.root.string());
        CHECK(list.size() == 1 && list[0].restorable && list[0].mode == "backup" && list[0].changed.empty());
        const RestoreResult rr = RestoreBackup(list[0], p.root.string());
        CHECK(!rr.ok && Has(rr.error, "nothing to restore") && Snap(p.root) == before);
        // an unreadable scene does not matter for it: the project is only copied
        WriteFile(p.root / "Assets" / "Scenes" / "Zbad.okayscene", "junk");
        UpgradeJob j2(p.Opts("2.860.0", false));
        j2.Run();
        CHECK(j2.result().ok);
    }

    // ---- a project with no project.okayproj of its own takes the old shared one, then is stamped ---------------------------------
    {
        TempDir td;
        Proj p(td.path);
        fs::remove(p.root / "project.okayproj");
        Options o = p.Opts("");
        o.legacySettings = "company=Shared\nwidth=800\n";
        UpgradeJob j(o);
        j.Run();
        CHECK(j.result().ok);
        CHECK(ReadFile(p.root / "project.okayproj") == "company=Shared\nwidth=800\nengine_version=2.855.0\n");
        const auto list = ListBackups(p.root.string());
        CHECK(list.size() == 1 && list[0].created == std::vector<std::string>{"project.okayproj"} && list[0].changed.size() == 3);
        // and restoring takes it away again
        const RestoreResult rr = RestoreBackup(list[0], p.root.string());
        CHECK(rr.ok && rr.removed == 1 && rr.restored == 3 && !fs::exists(p.root / "project.okayproj"));
        // a project with neither: it gets a file holding just the stamp
        TempDir t2;
        Proj q(t2.path);
        fs::remove(q.root / "project.okayproj");
        UpgradeJob j2(q.Opts(""));
        j2.Run();
        CHECK(j2.result().ok && ReadFile(q.root / "project.okayproj") == "engine_version=2.855.0\n");
    }

    // ---- restoring from a backup ------------------------------------------------------------------------------------------------
    {
        TempDir td;
        Proj p(td.path);
        const Tree original = Snap(p.root);
        UpgradeJob j(p.Opts("2.850.0"));
        j.Run();
        CHECK(j.result().ok);
        // the person keeps working after the upgrade: one rewritten scene and a new one
        WriteFile(p.root / kChanged2, ReadFile(p.root / kChanged2) + "# my edit after the upgrade\n");
        WriteFile(p.root / "Assets" / "Scenes" / "New.okayscene", Current("New", 1));
        const Tree working = Snap(p.root);

        const std::vector<BackupInfo> list = ListBackups(p.root.string());
        CHECK(list.size() == 1 && list[0].restorable && list[0].changed.size() == 4);
        const RestoreResult rr = RestoreBackup(list[0], p.root.string());
        if (!rr.ok) std::cerr << "  restore failed: " << rr.error << "\n";
        CHECK(rr.ok && rr.restored == 4 && rr.removed == 0 && !rr.sideCopyDir.empty());
        const Tree after = Snap(p.root);
        for (const auto& kv : original) CHECK(after.count(kv.first) && (kv.first.rfind(".okay/backups", 0) == 0 || after.at(kv.first) == kv.second));   // the old files are back, byte for byte
        CHECK(after.count("Assets/Scenes/New.okayscene"));                           // what was made since is left alone
        CHECK(!Has(after.at("project.okayproj"), "engine_version"));                 // the old project has no stamp: it asks again next time
        // what was replaced is kept
        const fs::path side = rr.sideCopyDir;
        CHECK(fs::exists(side / kChanged2) && ReadFile(side / kChanged2) == working.at(kChanged2) && Has(ReadFile(side / kChanged2), "my edit after the upgrade"));
        CHECK(ReadFile(side / "project.okayproj") == working.at("project.okayproj"));
        // a second restore gets its own side folder; the side folders are not copied into a backup
        const RestoreResult r2 = RestoreBackup(list[0], p.root.string());
        CHECK(r2.ok && r2.sideCopyDir != rr.sideCopyDir && fs::exists(r2.sideCopyDir));
        UpgradeJob again(p.Opts("2.850.0"));
        again.Run();
        CHECK(again.result().ok);
        for (const auto& kv : Snap(again.result().backupDir)) CHECK(kv.first.rfind(".okay/backups", 0) != 0);

        // a damaged backup is refused before anything is touched
        TempDir t2;
        Proj q(t2.path);
        UpgradeJob jq(q.Opts("2.850.0"));
        jq.Run();
        const auto bl = ListBackups(q.root.string());
        const Tree beforeBad = Snap(q.root);
        WriteFile(fs::path(bl[0].dir) / kChanged2, "damaged");
        const RestoreResult bad = RestoreBackup(bl[0], q.root.string());
        CHECK(!bad.ok && Has(bad.error, "damaged") && Snap(q.root) == beforeBad);
        // a folder that is not one of ours
        BackupInfo foreign;
        foreign.dir = q.base;
        CHECK(!RestoreBackup(foreign, q.root.string()).ok && Has(RestoreBackup(foreign, q.root.string()).error, "isn't a backup"));
    }
    {   // the restore is all or nothing too: the disk refuses at each step and the project is as it was
        TempDir probe;
        Proj pp(probe.path, 50 * 1024);
        UpgradeJob jp(pp.Opts("2.850.0"));
        jp.Run();
        std::size_t steps = 0;
        const auto bl = ListBackups(pp.root.string());
        RestoreBackup(bl[0], pp.root.string(), [&](const char*, const std::string&) { ++steps; return false; });
        CHECK(steps >= 8);                                                           // 4 files kept, 4 put back
        for (std::size_t i = 0; i < steps; ++i) {
            TempDir td;
            Proj p(td.path, 50 * 1024);
            UpgradeJob j(p.Opts("2.850.0"));
            j.Run();
            WriteFile(p.root / kChanged2, ReadFile(p.root / kChanged2) + "# edit\n");
            const Tree working = Snap(p.root);
            const auto list = ListBackups(p.root.string());
            std::size_t n = 0;
            const RestoreResult rr = RestoreBackup(list[0], p.root.string(), [&](const char*, const std::string&) { return n++ == i; });
            if (rr.ok || Snap(p.root) != working) std::cerr << "  restore failure #" << i << ": ok=" << rr.ok << " error='" << rr.error << "'\n";
            CHECK(!rr.ok && !rr.error.empty() && Snap(p.root) == working);           // nothing changed, no half-restored project, no side folder left
        }
    }

    // ---- the gate: nothing is touched until the question is answered -------------------------------------------------------------
    {
        auto stamp = [](const Proj& p, const char* v) { std::string e; if (*v) CHECK(okayver::WriteProjectVersion(p.root.string(), v, &e)); };
        auto sceneOf = [](const Proj& p) { return (p.root / "Assets" / "Scenes" / "Level1.okayscene").string(); };
        GateEnv env;
        env.running = "2.855.0";

        // Same version and development builds: no question, the editor proceeds at once
        {
            TempDir td; Proj p(td.path); stamp(p, "2.855.0");
            ProjectGate g(env);
            int go = 0, no = 0;
            g.Open(sceneOf(p), [&] { ++go; }, [&] { ++no; });
            CHECK(go == 1 && no == 0 && !g.Busy() && g.IsCleared(p.root.string()));
            g.Open(sceneOf(p), [&] { ++go; }, [&] { ++no; });                        // opening it again never prompts
            CHECK(go == 2 && !g.Busy());
        }
        {
            TempDir td; Proj p(td.path); stamp(p, "2.850.0");
            const Tree before = Snap(p.root);
            GateEnv de = env; de.running = "dev";
            ProjectGate g(de);
            int go = 0;
            g.Open(sceneOf(p), [&] { ++go; });
            CHECK(go == 1 && !g.Busy() && Snap(p.root) == before);                   // a dev build never asks, and writes nothing
            TempDir t2; Proj q(t2.path);                                              // not even for a project with no stamp
            ProjectGate g2(de);
            g2.Open(sceneOf(q), [&] { ++go; });
            CHECK(go == 2 && !g2.Busy());
        }
        // a scene that is not in a project, and a project made in this session
        {
            TempDir td;
            WriteFile(td.path / "loose" / "x.okayscene", Current("x", 1));
            ProjectGate g(env);
            int go = 0;
            g.Open((td.path / "loose" / "x.okayscene").string(), [&] { ++go; });
            CHECK(go == 1 && !g.Busy());
            Proj p(td.path / "made");
            g.MarkCleared(p.root.string());                                          // CreateProject does this
            g.Open(sceneOf(p), [&] { ++go; });
            CHECK(go == 2 && !g.Busy());
        }

        // Older: the question, Cancel, then Upgrade
        {
            TempDir td; Proj p(td.path); stamp(p, "2.850.0");
            const Tree before = Snap(p.root);
            ProjectGate g(env);
            int go = 0, no = 0;
            g.Open(sceneOf(p), [&] { ++go; }, [&] { ++no; });
            CHECK(g.Busy() && g.prompt().kind == PromptKind::Older && go == 0 && no == 0);
            CHECK(g.prompt().projectVersion == "2.850.0" && g.prompt().runningVersion == "2.855.0" && g.prompt().projectName == "My Game \xC3\xBC" && g.prompt().scene == sceneOf(p));
            g.Tick();                                                                // time passing changes nothing while it is open
            CHECK(Snap(p.root) == before && BackupCount(p) == 0);                    // asked, and not one byte written
            g.Answer(Choice::Cancel);
            CHECK(!g.Busy() && no == 1 && go == 0 && Snap(p.root) == before && !g.IsCleared(p.root.string()));
            g.Answer(Choice::Upgrade);                                               // answers with no window open do nothing
            CHECK(!g.Busy());

            g.Open(sceneOf(p), [&] { ++go; }, [&] { ++no; });                        // asked again: Cancel was not a decision
            CHECK(g.prompt().kind == PromptKind::Older);
            int second = 0;
            g.Open(sceneOf(p), [&] { ++go; }, [&] { ++second; });                    // one question at a time
            CHECK(second == 1 && g.prompt().kind == PromptKind::Older && go == 0);
            g.Answer(Choice::Upgrade);
            CHECK(g.prompt().kind == PromptKind::Working && go == 0);
            g.Answer(Choice::Back);                                                  // buttons that don't belong to this window are ignored
            CHECK(g.prompt().kind == PromptKind::Working);
            int guard = 0;
            while (g.prompt().kind == PromptKind::Working && ++guard < 100000) g.Tick(std::chrono::milliseconds(0));
            CHECK(g.prompt().kind == PromptKind::Done && go == 0);
            const Result& r = g.prompt().result;
            CHECK(r.ok && r.scenes == 4 && r.prefabs == 2 && r.rewritten == 3 && fs::exists(r.backupDir) && !g.prompt().backupOnly);
            CHECK(Has(r.backupDir, "(backup v2.850.0)"));
            CHECK(okayver::ReadProjectVersion(p.root.string()) == "2.855.0");
            g.Answer(Choice::Continue);
            CHECK(!g.Busy() && go == 1 && g.IsCleared(p.root.string()));
            g.Open(sceneOf(p), [&] { ++go; });                                       // the rest of the session: no question
            CHECK(go == 2 && !g.Busy());
        }
        // Unknown: Upgrade or Cancel only (the old version can't be known, so Keep is not on offer)
        {
            TempDir td; Proj p(td.path);
            const Tree before = Snap(p.root);
            ProjectGate g(env);
            int go = 0, no = 0;
            g.Open(sceneOf(p), [&] { ++go; }, [&] { ++no; });
            CHECK(g.prompt().kind == PromptKind::Unknown && g.prompt().projectVersion.empty());
            g.Answer(Choice::Keep);
            CHECK(g.prompt().kind == PromptKind::Unknown && !g.TakeQuitRequest() && !g.TakeLaunch());
            g.Answer(Choice::OpenAnyway);
            CHECK(g.prompt().kind == PromptKind::Unknown);
            g.Answer(Choice::Cancel);
            CHECK(no == 1 && Snap(p.root) == before);
            g.Open(sceneOf(p), [&] { ++go; }, [&] { ++no; });
            g.Answer(Choice::Upgrade);
            for (int i = 0; i < 100000 && g.prompt().kind == PromptKind::Working; ++i) g.Tick(std::chrono::milliseconds(0));
            CHECK(g.prompt().kind == PromptKind::Done && Has(g.prompt().result.backupDir, "(backup before v2.855.0)"));
            g.Answer(Choice::Continue);
            CHECK(go == 1 && okayver::ReadProjectVersion(p.root.string()) == "2.855.0");
        }
        // Older, Keep: installed -> the other editor is started (after this one closes); not installed -> a message
        {
            TempDir td; Proj p(td.path); stamp(p, "2.850.0");
            const Tree before = Snap(p.root);
            const fs::path launcher = td.path / "Launcher Dir";
            GateEnv ke = env;
            ke.launcherDir = [&] { return launcher; };
            ke.forwardArgs = {"--launcher", "4242"};
            ProjectGate g(ke);
            int go = 0, no = 0;
            g.Open(sceneOf(p), [&] { ++go; }, [&] { ++no; });
            g.Answer(Choice::Keep);
            CHECK(g.prompt().kind == PromptKind::NotInstalled);
            CHECK(g.prompt().message == "v2.850.x is not installed. Open the OkaySpace launcher > Versions to install it.");
            CHECK(g.prompt().keepTarget.empty());
            CHECK(!g.TakeQuitRequest() && !g.TakeLaunch() && Snap(p.root) == before);
            g.Answer(Choice::Back);
            CHECK(g.prompt().kind == PromptKind::Older && g.prompt().message.empty());
            g.Answer(Choice::Keep);
            g.Answer(Choice::Cancel);
            CHECK(!g.Busy() && no == 1 && go == 0);

            const fs::path exe = okayver::InstalledEditorPath(launcher, "2.850.0");
            WriteFile(exe, "#!/bin/sh\n");
            WriteFile(okayver::InstalledEditorPath(launcher, "2.851.0"), "#!/bin/sh\n");      // another line: never the target
            g.Open(sceneOf(p), [&] { ++go; }, [&] { ++no; });
            CHECK(g.prompt().kind == PromptKind::Older && g.prompt().keepTarget == "2.850.0");
            g.Answer(Choice::Keep);
            CHECK(!g.Busy() && go == 0 && no == 2);                                  // this editor does not open the project
            CHECK(g.TakeQuitRequest() && !g.TakeQuitRequest());
            const auto launch = g.TakeLaunch();
            CHECK(launch && launch->exe == exe.string() && launch->workDir == exe.parent_path().string());
            CHECK(launch && launch->args == (std::vector<std::string>{"--open", sceneOf(p), "--keep-version", "--launcher", "4242"}));
            CHECK(!g.TakeLaunch());
            CHECK(okayver::ReadProjectVersion(p.root.string()) == "2.850.0");        // the project was not touched
        }
        // a different patch of the same line: no question either way round, nothing written, stamped by the next save
        {
            TempDir td; Proj p(td.path); stamp(p, "2.850.0");
            const Tree before = Snap(p.root);
            GateEnv pe = env; pe.running = "2.850.1";
            ProjectGate g(pe);
            int go = 0, no = 0;
            g.Open(sceneOf(p), [&] { ++go; }, [&] { ++no; });
            CHECK(go == 1 && no == 0 && !g.Busy() && g.IsCleared(p.root.string()) && Snap(p.root) == before);
            std::string err;
            CHECK(StampProject(p.root.string(), "2.850.1", &err) && okayver::ReadProjectVersion(p.root.string()) == "2.850.1");   // the next save
            TempDir t2; Proj q(t2.path); stamp(q, "2.850.3");
            ProjectGate g2(pe);
            std::string why;
            CHECK(g2.MayLoad(sceneOf(q), &why) && g2.IsCleared(q.root.string()));      // the reverse too: an older patch of the line opens it
            ProjectGate g3(pe);
            g3.Open(sceneOf(q), [&] { ++go; });
            CHECK(go == 2 && !g3.Busy());
            TempDir t3; Proj r(t3.path); stamp(r, "2.851.0");                          // the next build is another line, not a patch
            ProjectGate g4(pe);
            g4.Open(sceneOf(r), [&] { ++go; });
            CHECK(go == 2 && g4.prompt().kind == PromptKind::Newer);
        }
        // Keep v2.850.x starts the newest installed editor of the line
        {
            TempDir td; Proj p(td.path); stamp(p, "2.850.0");
            const fs::path launcher = td.path / "L";
            for (const char* v : {"2.850.0", "2.850.2", "2.850.1", "2.849.0"}) WriteFile(okayver::InstalledEditorPath(launcher, v), "#!/bin/sh\n");
            GateEnv ke = env;
            ke.launcherDir = [&] { return launcher; };
            ProjectGate g(ke);
            g.Open(sceneOf(p), [] {}, [] {});
            CHECK(g.prompt().kind == PromptKind::Older && g.prompt().keepTarget == "2.850.2");
            g.Answer(Choice::Keep);
            const auto launch = g.TakeLaunch();
            CHECK(launch && launch->exe == okayver::InstalledEditorPath(launcher, "2.850.2").string());
            CHECK(launch && launch->args == (std::vector<std::string>{"--open", sceneOf(p), "--keep-version"}));
            CHECK(okayver::ReadProjectVersion(p.root.string()) == "2.850.0");
        }
        // Newer: Cancel, Update, or Open anyway (a backup first, the project as it is)
        {
            TempDir td; Proj p(td.path); stamp(p, "2.860.0");
            const Tree before = Snap(p.root);
            ProjectGate g(env);
            int go = 0, no = 0;
            g.Open(sceneOf(p), [&] { ++go; }, [&] { ++no; });
            CHECK(g.prompt().kind == PromptKind::Newer && g.prompt().projectVersion == "2.860.0");
            g.Answer(Choice::Upgrade);                                               // a newer project can't be "upgraded"
            g.Answer(Choice::Keep);
            CHECK(g.prompt().kind == PromptKind::Newer);
            g.Answer(Choice::UpdateEditor);
            CHECK(!g.Busy() && no == 1 && g.TakeUpdateRequest() && !g.TakeUpdateRequest() && Snap(p.root) == before);
            g.Open(sceneOf(p), [&] { ++go; }, [&] { ++no; });
            g.Answer(Choice::Cancel);
            CHECK(no == 2 && Snap(p.root) == before);
            g.Open(sceneOf(p), [&] { ++go; }, [&] { ++no; });
            g.Answer(Choice::OpenAnyway);
            CHECK(g.prompt().kind == PromptKind::Working && g.prompt().backupOnly);
            for (int i = 0; i < 100000 && g.prompt().kind == PromptKind::Working; ++i) g.Tick(std::chrono::milliseconds(0));
            CHECK(g.prompt().kind == PromptKind::Done && g.prompt().backupOnly && go == 0);
            CHECK(Snap(p.root) == before && Has(g.prompt().result.backupDir, "(backup v2.860.0)"));    // the project is exactly as it was; the backup is made
            g.Answer(Choice::Continue);
            CHECK(go == 1 && !g.Busy());
        }
        // a failed upgrade: the reason, the restore note, the project as it was, asked again next time
        {
            TempDir td; Proj p(td.path); stamp(p, "2.850.0");
            const Tree before = Snap(p.root);
            GateEnv fe = env;
            fe.fail = [](const char* stage, const std::string&) { return std::string(stage) == "stamp"; };
            ProjectGate g(fe);
            int go = 0, no = 0;
            g.Open(sceneOf(p), [&] { ++go; }, [&] { ++no; });
            g.Answer(Choice::Upgrade);
            for (int i = 0; i < 100000 && g.prompt().kind == PromptKind::Working; ++i) g.Tick(std::chrono::milliseconds(0));
            CHECK(g.prompt().kind == PromptKind::Failed && !g.prompt().result.error.empty() && Has(g.prompt().result.restoreNote, "exactly as it was"));
            CHECK(Snap(p.root) == before && fs::exists(g.prompt().result.backupDir));
            g.Answer(Choice::Close);
            CHECK(!g.Busy() && no == 1 && go == 0 && !g.IsCleared(p.root.string()));
            g.SetEnv(env);
            g.Open(sceneOf(p), [&] { ++go; }, [&] { ++no; });
            CHECK(g.prompt().kind == PromptKind::Older);
        }
        // cancelling while it works: no result window, the project as it was
        {
            TempDir td; Proj p(td.path, 2 * 1024 * 1024); stamp(p, "2.850.0");
            const Tree before = Snap(p.root);
            ProjectGate g(env);
            int go = 0, no = 0;
            g.Open(sceneOf(p), [&] { ++go; }, [&] { ++no; });
            g.Answer(Choice::Upgrade);
            for (int i = 0; i < 25; ++i) g.Tick(std::chrono::milliseconds(0));
            CHECK(g.prompt().kind == PromptKind::Working);
            g.Answer(Choice::Cancel);
            for (int i = 0; i < 100000 && g.Busy(); ++i) g.Tick(std::chrono::milliseconds(0));
            CHECK(!g.Busy() && no == 1 && go == 0 && Snap(p.root) == before && BackupCount(p) == 0 && NoPartial(p.base));
        }
        // --keep-version: skips the question for an older or unversioned project (for this version's editor only); a newer one still asks
        {
            TempDir td; Proj p(td.path); stamp(p, "2.850.0");
            ProjectGate g(env);
            g.NoteKeepRequest(sceneOf(p), "2.850.0");                                // the flag was meant for another version's editor
            int go = 0;
            g.Open(sceneOf(p), [&] { ++go; });
            CHECK(go == 0 && g.prompt().kind == PromptKind::Older && !g.IsKept(p.root.string()));
            g.Answer(Choice::Cancel);
            g.NoteKeepRequest(sceneOf(p));                                           // a bare flag: this editor's own version
            CHECK(g.IsKept(p.root.string()));
            std::string why;
            CHECK(g.MayLoad(sceneOf(p), &why));
            g.Open(sceneOf(p), [&] { ++go; });
            CHECK(go == 1 && !g.Busy());
            g.Open(sceneOf(p), [&] { ++go; });                                       // and the rest of the session
            CHECK(go == 2 && !g.Busy());

            TempDir t2; Proj q(t2.path);                                             // no stamp
            ProjectGate g2(env);
            g2.NoteKeepRequest(sceneOf(q), "2.855.0");                               // given for this version: honored
            g2.Open(sceneOf(q), [&] { ++go; });
            CHECK(go == 3 && !g2.Busy());

            TempDir t3; Proj r(t3.path); stamp(r, "2.860.0");                        // newer: the data-loss question is always asked
            ProjectGate g3(env);
            g3.NoteKeepRequest(sceneOf(r));
            g3.Open(sceneOf(r), [&] { ++go; });
            CHECK(go == 3 && g3.prompt().kind == PromptKind::Newer);
            g3.NoteKeepRequest((t3.path / "loose.okayscene").string());              // a scene that is in no project: ignored
        }
        // File > Project Upgrade...: the person asks for it on an open project, so there is no question, only the work and the result
        {
            TempDir td; Proj p(td.path); stamp(p, "2.855.0");
            ProjectGate g(env);
            g.MarkCleared(p.root.string());                                          // it is open
            int done = 0;
            g.Upgrade(sceneOf(p), [&] { ++done; });
            CHECK(g.prompt().kind == PromptKind::Working && !g.prompt().backupOnly && g.prompt().projectVersion == "2.855.0");
            int other = 0;
            g.Upgrade(sceneOf(p), [&] { ++other; });                                 // one at a time
            g.Open(sceneOf(p), [&] { ++other; }, [&] { ++other; });
            CHECK(other == 1);                                                       // the second Open was cancelled, the second Upgrade ignored
            for (int i = 0; i < 100000 && g.prompt().kind == PromptKind::Working; ++i) g.Tick(std::chrono::milliseconds(0));
            CHECK(g.prompt().kind == PromptKind::Done && g.prompt().result.rewritten == 3 && done == 0);   // an up-to-date stamp still has old scenes to bring along
            g.Answer(Choice::Continue);
            CHECK(done == 1 && !g.Busy());
            g.Upgrade((td.path / "loose.okayscene").string(), [&] { ++done; });     // not a project: nothing happens
            CHECK(!g.Busy() && done == 1);
        }
        // the safety net under EditorState::Load
        {
            TempDir td; Proj p(td.path); stamp(p, "2.850.0");
            ProjectGate g(env);
            std::string why;
            CHECK(!g.MayLoad(sceneOf(p), &why) && Has(why, "v2.850.0") && Has(why, "upgrade"));
            CHECK(!g.IsCleared(p.root.string()));
            TempDir t2; Proj q(t2.path);
            CHECK(!g.MayLoad(sceneOf(q), &why) && Has(why, "no version recorded"));
            TempDir t3; Proj r(t3.path); stamp(r, "2.860.0");
            CHECK(!g.MayLoad(sceneOf(r), &why) && Has(why, "newer"));
            TempDir t4; Proj s(t4.path); stamp(s, "2.855.0");
            CHECK(g.MayLoad(sceneOf(s), &why) && g.IsCleared(s.root.string()));      // the same version needs no answer
            g.MarkCleared(p.root.string());
            CHECK(g.MayLoad(sceneOf(p), &why));                                      // an answered project loads
            CHECK(g.MayLoad((td.path / "loose.okayscene").string(), &why) && g.MayLoad("", nullptr));
            GateEnv de = env; de.running = "dev";
            ProjectGate dg(de);
            CHECK(dg.MayLoad(sceneOf(q), &why));
        }
        // the environment of a kept editor is what the gate hands the upgrade: build output is left out and the old shared file seeds it
        {
            TempDir td; Proj p(td.path); stamp(p, "2.850.0");
            WriteFile(p.root / "Export" / "game.exe", "out");
            fs::remove(p.root / "project.okayproj");
            GateEnv ge = env;
            ge.outputDirs = [&](const std::string& dir) { return std::vector<std::string>{(fs::path(dir) / "Export").string()}; };
            ge.legacySettings = [] { return std::string("company=Shared\n"); };
            ProjectGate g(ge);
            g.Open(sceneOf(p), [] {});
            CHECK(g.prompt().kind == PromptKind::Unknown);
            g.Answer(Choice::Upgrade);
            for (int i = 0; i < 100000 && g.prompt().kind == PromptKind::Working; ++i) g.Tick(std::chrono::milliseconds(0));
            CHECK(g.prompt().kind == PromptKind::Done);
            CHECK(!fs::exists(fs::path(g.prompt().result.backupDir) / "Export") && ReadFile(p.root / "project.okayproj") == "company=Shared\nengine_version=2.855.0\n");
        }
    }

    TEST_MAIN_RESULT();
}
