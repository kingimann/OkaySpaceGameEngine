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
    explicit Proj(const fs::path& b, std::size_t bigBytes = 1200 * 1024, bool bomScene = false) : base(b), root(b / "My Game \xC3\xBC") {
        if (bomScene) WriteFile(root / "Assets" / "Scenes" / "Bom.okayscene", "\xEF\xBB\xBF" + Current("Bom", 1));   // rewritten too: the mark goes
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

// The machine's rules, but the search for a project stops at `top` (a test's temp folder): what is above it (the system's
// temp folder, a home folder that holds a project) can't change a "this is a loose file" answer.
RootRules Fenced(const fs::path& top) { RootRules r = DefaultRootRules(); r.ceiling = top; return r; }
GateEnv FencedEnv(const fs::path& top, const char* running = "2.855.0") { GateEnv e; e.running = running; e.rootRules = Fenced(top); return e; }

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
        CHECK(ProjectRootForScene((td.path / "loose" / "Desktop.okayscene").string(), Fenced(td.path)).empty());   // a loose file is no project
        CHECK(ProjectRootForScene("").empty());
        CHECK(IsProjectRoot(p.root) && !IsProjectRoot(td.path / "loose") && !IsProjectRoot(td.path / "nowhere"));
        CHECK(RootKey(root) == RootKey(root + "/") && RootKey(root) == RootKey((p.root / "Assets" / "..").string()));
    }

    // ---- a scene outside Assets/ still belongs to its project (issue #888, bug 1) -------------------------------------
    // The project is the nearest folder above the scene that holds project.okayproj, so the version question is asked and
    // File > Project Upgrade works on the whole project, wherever the scene sits in it.
    {
        TempDir td;
        const fs::path root = td.path / "Game";
        WriteFile(root / "project.okayproj", "engine_version=2.850.0\n");
        WriteFile(root / "Levels" / "World1" / "a.okayscene", Current("A", 1));
        WriteFile(root / "Levels" / "b.okayscene", kV1);
        const std::string deep = (root / "Levels" / "World1" / "a.okayscene").string();
        std::cerr << "[bug1_scene_outside_assets] root='" << ProjectRootForScene(deep) << "'\n";
        CHECK(ProjectRootForScene(deep) == root.string());                                       // two folders down, no Assets anywhere
        CHECK(ProjectRootForScene((root / "Levels" / "b.okayscene").string()) == root.string());
        WriteFile(root / "Tools" / "project.okayproj", "company=Inner\n");                       // an unstamped file in a folder above the scene is
        CHECK(ProjectRootForScene((root / "Tools" / "Maps" / "c.okayscene").string()) == root.string());   // a copy of the shared one, not a project
        WriteFile(root / "Tools" / "project.okayproj", "engine_version=2.840.0\ncompany=Inner\n");   // a project inside a project: the nearest wins
        CHECK(ProjectRootForScene((root / "Tools" / "Maps" / "c.okayscene").string()) == (root / "Tools").string());
        WriteFile(td.path / "Loose" / "Deep" / "d.okayscene", Current("D", 1));                  // no project.okayproj above it: still a loose file
        CHECK(ProjectRootForScene((td.path / "Loose" / "Deep" / "d.okayscene").string(), Fenced(td.path)).empty());

        // The editor's working folder keeps the old shared project.okayproj (written while no project is open); it does not make
        // every scene below the working folder part of a "project" (launch.sh / launch.bat start the editor in the repository).
        {
            const fs::path work = td.path / "Work";
            WriteFile(work / "project.okayproj", "company=Shared\n");
            WriteFile(work / "examples" / "vehicle" / "v.okayscene", Current("V", 1));
            std::error_code ec;
            const fs::path before = fs::current_path(ec);
            fs::current_path(work, ec);
            CHECK(!ec);
            const std::string found = ProjectRootForScene((work / "examples" / "vehicle" / "v.okayscene").string());
            fs::current_path(before, ec);
            CHECK(found.empty());
            CHECK(ProjectRootForScene((work / "examples" / "vehicle" / "v.okayscene").string(), Fenced(td.path)).empty());   // from elsewhere too: no stamp, no project
        }

        // the gate asks, and its safety net refuses, for such a scene of an older project
        GateEnv env;
        env.running = "2.855.0";
        {
            ProjectGate g(env);
            int go = 0;
            g.Open(deep, [&] { ++go; });
            CHECK(go == 0 && g.prompt().kind == PromptKind::Older && g.prompt().root == root.string() && g.prompt().projectName == "Game");
            std::string why;
            CHECK(!ProjectGate(env).MayLoad(deep, &why) && Has(why, "v2.850.0"));
        }
        // File > Project Upgrade... on that scene upgrades the whole project: the old scene in another folder, the stamp at the root
        {
            ProjectGate g(env);
            g.MarkCleared(root.string());
            int done = 0;
            g.Upgrade(deep, [&] { ++done; });
            CHECK(g.prompt().kind == PromptKind::Working);
            for (int i = 0; i < 100000 && g.prompt().kind == PromptKind::Working; ++i) g.Tick(std::chrono::milliseconds(0));
            CHECK(g.prompt().kind == PromptKind::Done && g.prompt().result.ok && g.prompt().result.scenes == 2 && g.prompt().result.rewritten == 1);
            CHECK(okayver::ReadProjectVersion(root.string()) == "2.855.0");
            CHECK(Has(ReadFile(root / "Levels" / "b.okayscene"), "okayscene 3"));
            CHECK(fs::path(g.prompt().result.backupDir).filename() == "Game (backup v2.850.0)" && fs::path(g.prompt().result.backupDir).parent_path() == td.path);
            g.Answer(Choice::Continue);
            CHECK(done == 1 && !g.Busy());
        }
        // Folder names ignore case on Windows: there (assetsAnyCase) a lower-case "assets" the scene is inside counts as the Assets
        // folder; elsewhere it does not. The rule is a parameter, so both answers are checked on every system.
        WriteFile(td.path / "Win" / "assets" / "Scenes" / "w.okayscene", Current("W", 1));
        const std::string w = (td.path / "Win" / "assets" / "Scenes" / "w.okayscene").string();
        RootRules anyCase = Fenced(td.path), exact = Fenced(td.path);
        anyCase.assetsAnyCase = true; exact.assetsAnyCase = false;
        CHECK(ProjectRootForScene(w, anyCase) == (td.path / "Win").string());
        CHECK(ProjectRootForScene(w, exact).empty());
        WriteFile(td.path / "Ex" / "Assets" / "x.okayscene", Current("X", 1));
        CHECK(ProjectRootForScene((td.path / "Ex" / "Assets" / "x.okayscene").string(), exact) == (td.path / "Ex").string());   // "Assets": either rule
        CHECK(ProjectRootForScene((td.path / "Ex" / "Assets" / "x.okayscene").string(), anyCase) == (td.path / "Ex").string());
#if defined(_WIN32)
        CHECK(DefaultRootRules().assetsAnyCase);
#else
        CHECK(!DefaultRootRules().assetsAnyCase);
#endif
    }

    // ---- a bare ".okay" folder does not make a project (issue #888, bug 2) ------------------------------------------------
    // The editor leaves a .okay folder (autosave, build list) next to any scene it opened. That alone must not turn a loose scene
    // folder such as examples/vehicle into a "project": no "Unknown" version question, and no upgrade that copies the whole folder.
    {
        TempDir td;
        const fs::path dir = td.path / "examples" / "vehicle";
        WriteFile(dir / "vehicle_test.okayscene", Current("V", 1));
        WriteFile(dir / ".okay" / "autosave.txt", "x");
        WriteFile(dir / ".okay" / "scene_build.txt", "1|vehicle_test.okayscene\n");
        const std::string scene = (dir / "vehicle_test.okayscene").string();
        std::cerr << "[bug2_bare_okay_folder] IsProjectRoot=" << IsProjectRoot(dir) << " root='" << ProjectRootForScene(scene, Fenced(td.path)) << "'\n";
        CHECK(!IsProjectRoot(dir));
        CHECK(ProjectRootForScene(scene, Fenced(td.path)).empty());
        GateEnv env = FencedEnv(td.path);
        ProjectGate g(env);
        int go = 0;
        g.Open(scene, [&] { ++go; });
        CHECK(go == 1 && !g.Busy());                                                             // opened with no question
        std::string why;
        CHECK(ProjectGate(env).MayLoad(scene, &why));
        int done = 0;
        g.Upgrade(scene, [&] { ++done; });                                                       // File > Project Upgrade: nothing to upgrade
        CHECK(!g.Busy() && done == 0);
        for (int i = 0; i < 100000 && g.prompt().kind == PromptKind::Working; ++i) g.Tick(std::chrono::milliseconds(0));   // (before the fix: the copy)
        CHECK(Names(td.path / "examples") == std::set<std::string>{"vehicle"});                  // no backup copy of the folder was made
        // project.okayproj or an Assets folder still make it a project
        WriteFile(dir / "project.okayproj", "engine_version=2.850.0\n");
        CHECK(IsProjectRoot(dir) && ProjectRootForScene(scene) == dir.string());
        fs::remove(dir / "project.okayproj");
        std::error_code ec;
        fs::create_directories(dir / "Assets", ec);
        CHECK(IsProjectRoot(dir) && ProjectRootForScene(scene) == dir.string());
        fs::remove(dir / "Assets", ec);
        fs::create_directories(dir / "project.okayproj", ec);                                   // a folder of that name is not the settings file
        CHECK(!IsProjectRoot(dir) && ProjectRootForScene(scene, Fenced(td.path)).empty());
    }

    // ---- scene files that start with a UTF-8 byte-order mark (issue #888, bug 3) ---------------------------------------
    // Some text editors put EF BB BF at the start of a file they save. The upgrade reads past it, and writes such a scene or
    // prefab back without it (the engine's loader refuses the mark: see test_scene_bom_bug.cpp); the backup keeps the original.
    {
        TempDir td;
        const fs::path root = td.path / "Game";
        const std::string bom = "\xEF\xBB\xBF";
        WriteFile(root / "project.okayproj", "engine_version=2.850.0\n");
        WriteFile(root / "Assets" / "Plain.okayscene", Current("Plain", 1));
        WriteFile(root / "Assets" / "Bom.okayscene", bom + Current("Bom", 2));
        WriteFile(root / "Assets" / "BomOld.okayscene", bom + kV1);
        WriteFile(root / "Assets" / "Bom.okayprefab", bom + PrefabText());
        WriteFile(root / "Assets" / "BomCrlf.okayscene", bom + [] { std::string o; for (char c : Current("C", 1)) { if (c == '\n') o += '\r'; o += c; } return o; }());
        const Tree before = Snap(root);
        Options o;
        o.projectDir = root.string(); o.fromVersion = "2.850.0"; o.toVersion = "2.855.0";
        UpgradeJob j(o);
        j.Run();
        const Result& r = j.result();
        std::cerr << "[bug3_bom_scene] ok=" << r.ok << " error='" << r.error << "' rewritten=" << r.rewritten << "\n";
        CHECK(r.ok && r.error.empty());
        CHECK(r.scenes == 4 && r.prefabs == 1);
        CHECK(r.rewritten == 4);                                                                  // the four files with a mark; Plain is untouched
        CHECK(ReadFile(root / "Assets" / "Plain.okayscene") == before.at("Assets/Plain.okayscene"));
        for (const char* f : {"Bom.okayscene", "BomOld.okayscene", "Bom.okayprefab", "BomCrlf.okayscene"}) {
            const std::string now = ReadFile(root / "Assets" / f);
            CHECK(now.compare(0, 10, "okayscene ") == 0);                                         // the mark is gone, the header is first
            CHECK(ReadFile(fs::path(r.backupDir) / "Assets" / f) == before.at(std::string("Assets/") + f));   // the backup has the original bytes
            bool logged = false;
            for (const std::string& l : r.log) if (Has(l, f) && Has(l, "byte-order mark")) logged = true;
            CHECK(logged);
        }
        CHECK(Loads(root / "Assets" / "Bom.okayscene") && Loads(root / "Assets" / "BomOld.okayscene") && Loads(root / "Assets" / "BomCrlf.okayscene"));
        CHECK(Has(ReadFile(root / "Assets" / "BomOld.okayscene"), "okayscene 3"));               // the old one was upgraded as well
        CHECK(okayver::ReadProjectVersion(root.string()) == "2.855.0");
        // Restore puts the files back exactly as they were, mark included
        const auto list = ListBackups(root.string());
        CHECK(list.size() == 1);
        if (list.size() == 1) {
            const RestoreResult rr = RestoreBackup(list[0], root.string());
            CHECK(rr.ok);
            for (const char* f : {"Bom.okayscene", "BomOld.okayscene", "Bom.okayprefab", "BomCrlf.okayscene"})
                CHECK(ReadFile(root / "Assets" / f) == before.at(std::string("Assets/") + f));
        }
        // A mark followed by something that is not a scene is still a damaged file: the upgrade stops before anything is made
        WriteFile(root / "Assets" / "Junk.okayscene", bom + "hello\n");
        const Tree mid = Snap(root);
        UpgradeJob k(o);
        k.Run();
        CHECK(!k.result().ok && Has(k.result().error, "Junk.okayscene") && Snap(root) == mid);
    }
    // A UTF-16 file with its byte-order mark (Windows PowerShell's `>` writes those) is converted to UTF-8 the same way.
    {
        TempDir td;
        const fs::path root = td.path / "Game";
        WriteFile(root / "project.okayproj", "engine_version=2.850.0\n");
        auto utf16 = [](const std::string& ascii, bool bigEndian) {
            std::string o = bigEndian ? std::string("\xFE\xFF", 2) : std::string("\xFF\xFE", 2);
            for (char c : ascii) { if (bigEndian) { o += '\0'; o += c; } else { o += c; o += '\0'; } }
            return o;
        };
        WriteFile(root / "Assets" / "Ps.okayscene", utf16(Current("Ps", 2), false));
        WriteFile(root / "Assets" / "PsOld.okayscene", utf16(kV1, false));
        WriteFile(root / "Assets" / "Be.okayscene", utf16(Current("Be", 1), true));
        const Tree before = Snap(root);
        Options o;
        o.projectDir = root.string(); o.fromVersion = "2.850.0"; o.toVersion = "2.855.0";
        UpgradeJob j(o);
        j.Run();
        std::cerr << "[review7_utf16_scene] ok=" << j.result().ok << " error='" << j.result().error << "'\n";
        CHECK(j.result().ok && j.result().scenes == 3 && j.result().rewritten == 3);
        for (const char* f : {"Ps.okayscene", "PsOld.okayscene", "Be.okayscene"}) {
            CHECK(ReadFile(root / "Assets" / f).compare(0, 12, "okayscene 3\n") == 0);
            CHECK(Loads(root / "Assets" / f));
            if (!j.result().backupDir.empty()) CHECK(ReadFile(fs::path(j.result().backupDir) / "Assets" / f) == before.at(std::string("Assets/") + f));
            bool logged = false;
            for (const std::string& l : j.result().log) if (Has(l, f) && Has(l, "UTF-16")) logged = true;
            CHECK(logged);
        }
        // broken UTF-16 (an odd number of bytes) is a damaged file: it stops the upgrade, named, before anything is made
        WriteFile(root / "Assets" / "Odd.okayscene", utf16(Current("O", 1), false) + "x");
        const Tree mid = Snap(root);
        UpgradeJob k(o);
        k.Run();
        CHECK(!k.result().ok && Has(k.result().error, "Odd.okayscene") && Snap(root) == mid);
    }

    // ---- review of the upgrade fixes (issue #888, da5423e): which folder is the project ------------------------------------
    // 1. A project.okayproj above the scene with no stamp is the old shared file (the editor copied it into scene folders),
    //    not a project: Documents, Desktop or a repository root must not become a "project" that is asked about and copied.
    //    An Assets folder beside a folder above the scene is no evidence either: the release folder holds Assets/ (starter
    //    textures) next to examples/.
    {
        TempDir td;
        const fs::path docs = td.path / "Documents";
        WriteFile(docs / "project.okayproj", "company=Shared\n");
        WriteFile(docs / "Downloads" / "x.okayscene", Current("X", 1));
        const std::string scene = (docs / "Downloads" / "x.okayscene").string();
        std::cerr << "[review1_unstamped_ancestor] root='" << ProjectRootForScene(scene, Fenced(td.path)) << "'\n";
        CHECK(ProjectRootForScene(scene, Fenced(td.path)).empty());
        GateEnv env = FencedEnv(td.path);
        ProjectGate g(env);
        int go = 0;
        g.Open(scene, [&] { ++go; });
        CHECK(go == 1 && !g.Busy());                                                             // no "Unknown" question
        std::string why;
        CHECK(ProjectGate(env).MayLoad(scene, &why));
        // the same folder with a stamp is a project
        WriteFile(docs / "project.okayproj", "engine_version=2.850.0\ncompany=Shared\n");
        CHECK(ProjectRootForScene(scene, Fenced(td.path)) == docs.string());
        // an Assets folder beside it is not enough
        WriteFile(docs / "project.okayproj", "company=Shared\n");
        std::error_code ec;
        fs::create_directories(docs / "Assets", ec);
        CHECK(ProjectRootForScene(scene, Fenced(td.path)).empty());
        // the release folder: Assets/Textures and examples/vehicle side by side, with the shared file
        const fs::path inst = td.path / "OkaySpace";
        WriteFile(inst / "Assets" / "Textures" / "grid.png", "png");
        WriteFile(inst / "project.okayproj", "company=Shared\n");
        WriteFile(inst / "examples" / "vehicle" / "vehicle_test.okayscene", Current("V", 1));
        CHECK(ProjectRootForScene((inst / "examples" / "vehicle" / "vehicle_test.okayscene").string(), Fenced(td.path)).empty());
        // 3. a stray copy of the shared file (no stamp) in a project's scene folder does not beat the project above it
        const fs::path game = td.path / "Game";
        WriteFile(game / "project.okayproj", "engine_version=2.855.0\n");
        WriteFile(game / "Levels" / "project.okayproj", "company=Shared\n");
        WriteFile(game / "Levels" / "a.okayscene", Current("A", 1));
        const std::string levels = ProjectRootForScene((game / "Levels" / "a.okayscene").string(), Fenced(td.path));
        std::cerr << "[review3_stray_copy_in_levels] root='" << levels << "'\n";
        CHECK(levels == game.string());
        // ... and a project's settings only ever go into a project folder (never a scene's folder: no new stray copies)
        CHECK(SettingsFolderFor(game.string()) == game.string());
        CHECK(SettingsFolderFor((td.path / "Loose").string()).empty());
        WriteFile(td.path / "Loose" / "a.okayscene", "x");
        CHECK(SettingsFolderFor((td.path / "Loose").string()).empty());
        fs::create_directories(td.path / "OnlyAssets" / "Assets", ec);
        CHECK(SettingsFolderFor((td.path / "OnlyAssets").string()) == (td.path / "OnlyAssets").string());   // an old project: it gets its copy
        CHECK(SettingsFolderFor("").empty() && SettingsFolderFor("/").empty());
    }
    // 1. A folder too big to copy without asking: the scan stops early, nothing is made, and the gate asks.
    {
        TempDir td;
        Proj p(td.path, 10 * 1024);
        std::string e;
        CHECK(okayver::WriteProjectVersion(p.root.string(), "2.850.0", &e));
        for (int i = 0; i < 40; ++i) WriteFile(p.root / "Assets" / "Many" / ("f" + std::to_string(i) + ".txt"), "x");
        const Tree before = Snap(p.root);
        Options o = p.Opts("2.850.0");
        o.largeFiles = 20;
        UpgradeJob j(o);
        j.Run();
        std::cerr << "[review1_too_large] ok=" << j.result().ok << " tooLarge=" << j.result().tooLarge << " error='" << j.result().error << "'\n";
        CHECK(!j.result().ok && j.result().tooLarge && Has(j.result().error, "more than 20 files") && Has(j.result().error, "Nothing was changed"));
        CHECK(j.result().files <= 20 + 256 && Snap(p.root) == before && BackupCount(p) == 0 && NoPartial(p.base));
        o.largeFiles = 50000; o.largeBytes = 1024;                                                // the size limit the same way
        UpgradeJob jb(o);
        jb.Run();
        CHECK(!jb.result().ok && jb.result().tooLarge && Has(jb.result().error, "MB") && BackupCount(p) == 0);
        o.largeBytes = 1024; o.allowLarge = true;                                                // "copy it anyway"
        UpgradeJob ja(o);
        ja.Run();
        CHECK(ja.result().ok && !ja.result().tooLarge && BackupCount(p) == 1);
        fs::remove_all(ja.result().backupDir);
        // the gate: the "very big folder" window, Cancel changes nothing, "copy it anyway" upgrades
        std::string root;
        WriteFile(p.root / "project.okayproj", "engine_version=2.850.0\n");
        const Tree before2 = Snap(p.root);
        GateEnv env = FencedEnv(td.path);
        env.largeFiles = 20;
        ProjectGate g(env);
        int go = 0, no = 0;
        const std::string scene = (p.root / "Assets" / "Scenes" / "Level1.okayscene").string();
        g.Open(scene, [&] { ++go; }, [&] { ++no; });
        g.Answer(Choice::Upgrade);
        for (int i = 0; i < 100000 && g.prompt().kind == PromptKind::Working; ++i) g.Tick(std::chrono::milliseconds(0));
        CHECK(g.prompt().kind == PromptKind::Large && g.prompt().result.tooLarge && g.prompt().root == p.root.string());
        g.Answer(Choice::Cancel);
        CHECK(!g.Busy() && no == 1 && go == 0 && Snap(p.root) == before2 && BackupCount(p) == 0);
        g.Open(scene, [&] { ++go; }, [&] { ++no; });
        g.Answer(Choice::Upgrade);
        for (int i = 0; i < 100000 && g.prompt().kind == PromptKind::Working; ++i) g.Tick(std::chrono::milliseconds(0));
        CHECK(g.prompt().kind == PromptKind::Large);
        g.Answer(Choice::Upgrade);                                                                // copy it anyway
        CHECK(g.prompt().kind == PromptKind::Working && !g.prompt().backupOnly);
        for (int i = 0; i < 100000 && g.prompt().kind == PromptKind::Working; ++i) g.Tick(std::chrono::milliseconds(0));
        CHECK(g.prompt().kind == PromptKind::Done && g.prompt().result.ok && BackupCount(p) == 1);
        g.Answer(Choice::Continue);
        CHECK(go == 1 && okayver::ReadProjectVersion(p.root.string()) == "2.855.0");
        // a newer project opened anyway: the big-folder window keeps it a backup only
        TempDir t2;
        Proj q(t2.path, 10 * 1024);
        CHECK(okayver::WriteProjectVersion(q.root.string(), "2.860.0", &e));
        const Tree qb = Snap(q.root);
        GateEnv e2 = FencedEnv(t2.path);
        e2.largeFiles = 3;
        ProjectGate g2(e2);
        g2.Open((q.root / "Assets" / "Scenes" / "Level1.okayscene").string(), [&] { ++go; });
        g2.Answer(Choice::OpenAnyway);
        for (int i = 0; i < 100000 && g2.prompt().kind == PromptKind::Working; ++i) g2.Tick(std::chrono::milliseconds(0));
        CHECK(g2.prompt().kind == PromptKind::Large && g2.prompt().backupOnly);
        g2.Answer(Choice::OpenAnyway);
        for (int i = 0; i < 100000 && g2.prompt().kind == PromptKind::Working; ++i) g2.Tick(std::chrono::milliseconds(0));
        CHECK(g2.prompt().kind == PromptKind::Done && g2.prompt().backupOnly && Snap(q.root) == qb);
    }
    // 2. The working folder: the same folder however it is spelled (a link); a scene right in it is not a project because of the
    //    shared file; a real project there still is one through its Assets folder. (A scene outside Assets/ of a project that
    //    is the working folder is not recognised: that folder can't be told apart from the release folder, which holds
    //    Assets/ and the shared file, stamped by a release editor. See docs/project_upgrade.md.)
    {
        TempDir td;
        std::error_code ec;
        const fs::path before = fs::current_path(ec);
        const fs::path real = td.path / "Real";
        WriteFile(real / "project.okayproj", "engine_version=2.850.0\n");
        WriteFile(real / "Assets" / "Scenes" / "Main.okayscene", Current("M", 1));
        WriteFile(real / "Levels" / "a.okayscene", Current("A", 1));
        fs::current_path(real, ec);
        const std::string inAssets = ProjectRootForScene((real / "Assets" / "Scenes" / "Main.okayscene").string(), Fenced(td.path));
        const std::string inLevels = ProjectRootForScene((real / "Levels" / "a.okayscene").string(), Fenced(td.path));
        fs::current_path(before, ec);
        std::cerr << "[review2_project_is_cwd] assets='" << inAssets << "' levels='" << inLevels << "'\n";
        CHECK(inAssets == real.string());
        CHECK(inLevels.empty());                                                                  // the accepted limit (see above)
        const fs::path work = td.path / "Work";
        WriteFile(work / "project.okayproj", "engine_version=2.855.0\ncompany=Shared\n");   // a release build stamps the shared file too
        WriteFile(work / "s.okayscene", Current("S", 1));
        WriteFile(work / "examples" / "v.okayscene", Current("V", 1));
        fs::create_directory_symlink(work, td.path / "WorkLink", ec);
        const bool links = !ec;
        fs::current_path(work, ec);
        const std::string direct = ProjectRootForScene((work / "s.okayscene").string(), Fenced(td.path));
        const std::string viaLink = links ? ProjectRootForScene((td.path / "WorkLink" / "examples" / "v.okayscene").string(), Fenced(td.path)) : std::string();
        const std::string viaDots = ProjectRootForScene((work / "examples" / ".." / "examples" / "v.okayscene").string(), Fenced(td.path));
        fs::current_path(before, ec);
        std::cerr << "[review2_scene_in_cwd] direct='" << direct << "' viaLink='" << viaLink << "'\n";
        CHECK(direct.empty());
        CHECK(viaLink.empty());
        CHECK(viaDots.empty());
        // the same rule with the working folder given (what the tests of other machines' spellings use)
        RootRules r = Fenced(td.path);
        r.workingFolder = links ? td.path / "WorkLink" : work;
        CHECK(ProjectRootForScene((work / "examples" / "v.okayscene").string(), r).empty());
        CHECK(SameFolder(work, work / "") && SameFolder(work, td.path / "Work" / "examples" / ".."));
        CHECK(!links || SameFolder(work, td.path / "WorkLink"));
        CHECK(!SameFolder(work, real) && !SameFolder(work, td.path / "missing") && !SameFolder(fs::path(), work));
        CHECK(IsDriveRoot(fs::current_path(ec).root_path()));
        CHECK(!IsDriveRoot(td.path) && !IsDriveRoot(fs::path()));
    }
    // 5. The nearest real project wins: a stamped project inside another project's Assets folder is its own project; an Assets
    //    folder in the home folder does not make the home folder a project.
    {
        TempDir td;
        const fs::path game = td.path / "Game";
        WriteFile(game / "project.okayproj", "engine_version=2.850.0\n");
        WriteFile(game / "Assets" / "Sub" / "project.okayproj", "engine_version=2.840.0\n");
        WriteFile(game / "Assets" / "Sub" / "Levels" / "x.okayscene", Current("X", 1));
        const std::string nested = ProjectRootForScene((game / "Assets" / "Sub" / "Levels" / "x.okayscene").string(), Fenced(td.path));
        std::cerr << "[review5_nearest_wins] root='" << nested << "'\n";
        CHECK(nested == (game / "Assets" / "Sub").string());
        WriteFile(game / "Assets" / "Other" / "y.okayscene", Current("Y", 1));                  // elsewhere in Assets: the outer project
        CHECK(ProjectRootForScene((game / "Assets" / "Other" / "y.okayscene").string(), Fenced(td.path)) == game.string());
        const fs::path home = td.path / "home" / "me";
        WriteFile(home / "Assets" / "Downloads" / "t.okayscene", Current("T", 1));
        const char* oldHome = std::getenv("HOME");
        const std::string savedHome = oldHome ? oldHome : "";
        setenv("HOME", home.string().c_str(), 1);
        const std::string inHome = ProjectRootForScene((home / "Assets" / "Downloads" / "t.okayscene").string(), Fenced(td.path));
        const std::string inHomeDefault = ProjectRootForScene((home / "Assets" / "Downloads" / "t.okayscene").string());
        if (oldHome) setenv("HOME", savedHome.c_str(), 1); else unsetenv("HOME");
        std::cerr << "[review5_home_assets] root='" << inHome << "'\n";
        CHECK(inHome.empty() && inHomeDefault.empty());
        RootRules hr = Fenced(td.path);
        hr.home = home;
        WriteFile(home / "project.okayproj", "engine_version=2.850.0\n");                       // not even with a stamp
        CHECK(ProjectRootForScene((home / "Docs" / "t.okayscene").string(), hr).empty());
        CHECK(ProjectRootForScene((home / "t.okayscene").string(), hr).empty());
    }
    // 4. File > Project Upgrade shows, upgrades and restores one folder: UpgradeRootFor(scene, projectDir).
    {
        TempDir td;
        const fs::path game = td.path / "Game";
        WriteFile(game / "project.okayproj", "engine_version=2.850.0\n");
        WriteFile(game / "Levels" / "a.okayscene", kV1);
        const RootRules r = Fenced(td.path);
        CHECK(UpgradeRootFor((game / "Levels" / "a.okayscene").string(), "", r) == game.string());
        CHECK(UpgradeRootFor((game / "Levels" / "a.okayscene").string(), (game / "Levels").string(), r) == game.string());   // the scene's project wins
        std::error_code ec;
        fs::create_directories(td.path / "New" / "Assets", ec);
        CHECK(UpgradeRootFor("", (td.path / "New").string(), r) == (td.path / "New").string());     // a project with no scene saved yet
        CHECK(UpgradeRootFor("", (td.path / "Levels").string(), r).empty());                         // not a project folder
        WriteFile(td.path / "Loose" / "l.okayscene", Current("L", 1));
        CHECK(UpgradeRootFor((td.path / "Loose" / "l.okayscene").string(), "", r).empty());
        GateEnv env = FencedEnv(td.path);
        ProjectGate g(env);
        CHECK(g.UpgradeRootOf((game / "Levels" / "a.okayscene").string(), (game / "Levels").string()) == game.string());
        g.Upgrade((game / "Levels" / "a.okayscene").string(), {}, (game / "Levels").string());
        CHECK(g.prompt().kind == PromptKind::Working && g.prompt().root == game.string());
        for (int i = 0; i < 100000 && g.prompt().kind == PromptKind::Working; ++i) g.Tick(std::chrono::milliseconds(0));
        CHECK(g.prompt().kind == PromptKind::Done && okayver::ReadProjectVersion(game.string()) == "2.855.0");
        g.Answer(Choice::Continue);
        g.Upgrade("", {}, (td.path / "Levels").string());                                            // nothing to upgrade
        CHECK(!g.Busy());
    }
    // item 7a: a second open while a question is on screen (the launcher's hand-off) is not dropped: it runs once the
    // question is answered. A newer one replaces it; an editor that is closing (Keep) opens nothing more.
    {
        TempDir td;
        GateEnv env = FencedEnv(td.path);
        const fs::path a = td.path / "A", b = td.path / "B", c = td.path / "C";
        WriteFile(a / "project.okayproj", "engine_version=2.850.0\n");
        WriteFile(a / "Assets" / "a.okayscene", Current("A", 1));
        WriteFile(b / "project.okayproj", "engine_version=2.855.0\n");
        WriteFile(b / "Assets" / "b.okayscene", Current("B", 1));
        WriteFile(c / "project.okayproj", "engine_version=2.850.0\n");
        WriteFile(c / "Assets" / "c.okayscene", Current("C", 1));
        const std::string sa = (a / "Assets" / "a.okayscene").string(), sb = (b / "Assets" / "b.okayscene").string(), sc = (c / "Assets" / "c.okayscene").string();
        {
            ProjectGate g(env);
            int goA = 0, noA = 0, goB = 0, noB = 0;
            g.Open(sa, [&] { ++goA; }, [&] { ++noA; });
            CHECK(g.prompt().kind == PromptKind::Older);
            g.Open(sb, [&] { ++goB; }, [&] { ++noB; });
            std::cerr << "[review7a_second_open] goB=" << goB << " noB=" << noB << "\n";
            CHECK(goB == 0 && noB == 0 && g.prompt().kind == PromptKind::Older && g.prompt().root == a.string() && g.HasHeldOpen());   // held, not refused
            g.Answer(Choice::Cancel);
            CHECK(noA == 1 && goA == 0 && goB == 1 && noB == 0 && !g.Busy() && !g.HasHeldOpen());   // A was cancelled, then B opened
        }
        {   // the held one asks its own question when it is its turn; a newer request replaces an older held one
            ProjectGate g(env);
            int goA = 0, goB = 0, noB = 0, goC = 0, noC = 0;
            g.Open(sa, [&] { ++goA; });
            g.Open(sb, [&] { ++goB; }, [&] { ++noB; });
            g.Open(sc, [&] { ++goC; }, [&] { ++noC; });
            CHECK(noB == 1 && goB == 0 && goC == 0 && noC == 0);
            g.Answer(Choice::Cancel);
            CHECK(g.prompt().kind == PromptKind::Older && g.prompt().root == c.string() && goC == 0);
            g.Answer(Choice::Cancel);
            CHECK(noC == 1 && goC == 0 && !g.Busy() && goA == 0);
        }
        {   // Keep starts another editor and this one closes: the held request is cancelled, not opened
            const fs::path launcher = td.path / "L";
            WriteFile(okayver::InstalledEditorPath(launcher, "2.850.0"), "#!/bin/sh\n");
            GateEnv ke = env;
            ke.launcherDir = [&] { return launcher; };
            ProjectGate g(ke);
            int goB = 0, noB = 0;
            g.Open(sa, [] {}, [] {});
            g.Open(sb, [&] { ++goB; }, [&] { ++noB; });
            g.Answer(Choice::Keep);
            CHECK(g.TakeQuitRequest() && goB == 0 && noB == 1 && !g.Busy());
        }
        {   // held behind an upgrade: it runs after the result window's Continue
            ProjectGate g(env);
            int goA = 0, goB = 0;
            g.Open(sa, [&] { ++goA; });
            g.Answer(Choice::Upgrade);
            g.Open(sb, [&] { ++goB; });
            for (int i = 0; i < 100000 && g.prompt().kind == PromptKind::Working; ++i) g.Tick(std::chrono::milliseconds(0));
            CHECK(g.prompt().kind == PromptKind::Done && goB == 0);
            g.Answer(Choice::Continue);
            CHECK(goA == 1 && goB == 1 && !g.Busy());
        }
    }
    // carried over from the known-bug file (they pass): records this version does not know are kept, and scenes with Windows
    // line endings upgrade like any other
    {
        TempDir td;
        const fs::path root = td.path / "Game";
        WriteFile(root / "Assets" / "Old.okayscene", std::string(kV1) + "futurerecord 1 2 3\n");
        WriteFile(root / "Assets" / "Crlf.okayscene", [] { std::string o; for (char ch : Current("C", 2)) { if (ch == '\n') o += '\r'; o += ch; } return o; }());
        WriteFile(root / "Assets" / "CrlfOld.okayscene", [] { std::string o; for (const char* ch = kV1; *ch; ++ch) { if (*ch == '\n') o += '\r'; o += *ch; } return o; }());
        Options o;
        o.projectDir = root.string(); o.fromVersion = "2.850.0"; o.toVersion = "2.855.0";
        UpgradeJob j(o);
        j.Run();
        CHECK(j.result().ok && j.result().scenes == 3);
        CHECK(Has(ReadFile(root / "Assets" / "Old.okayscene"), "futurerecord 1 2 3"));
        CHECK(Loads(root / "Assets" / "Crlf.okayscene") && Has(ReadFile(root / "Assets" / "CrlfOld.okayscene"), "okayscene 3"));
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
        Proj pc(clean.path, 300 * 1024, true);                                  // with a byte-order-mark scene: one more file rewritten
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
            Proj p(td.path, 300 * 1024, true);
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
        CHECK(failedRuns == (int)calls.size() && afterRename == 5);              // apply x4 (three old files and the marked one), stamp
    }

    // a failed rollback says which file could not be put back, and the backup still has the original
    {
        TempDir td;
        Proj p(td.path, 100 * 1024, true);
        const Tree before = Snap(p.root);
        int applies = 0;
        Options o = p.Opts("2.850.0");
        o.fail = [&](const char* stage, const std::string& what) {
            if (std::string(stage) == "apply" && ++applies == 4) return true;      // the fourth rewrite (Old1) fails ...
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
        CHECK(now.at("Assets/Scenes/Bom.okayscene") == before.at("Assets/Scenes/Bom.okayscene"));   // the marked scene too, mark and all
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
            ProjectGate g(FencedEnv(td.path));
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
            g.Open(sceneOf(p), [&] { ++second; }, [&] { ++second; });                // one question at a time: the second is held
            CHECK(second == 0 && g.prompt().kind == PromptKind::Older && go == 0 && g.HasHeldOpen());
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
            CHECK(second == 1 && !g.HasHeldOpen());                                  // ... and opened once the project was answered for
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
            ProjectGate g(FencedEnv(td.path));
            g.MarkCleared(p.root.string());                                          // it is open
            int done = 0;
            g.Upgrade(sceneOf(p), [&] { ++done; });
            CHECK(g.prompt().kind == PromptKind::Working && !g.prompt().backupOnly && g.prompt().projectVersion == "2.855.0");
            int other = 0;
            g.Upgrade(sceneOf(p), [&] { ++other; });                                 // one at a time
            g.Open(sceneOf(p), [&] { ++other; }, [&] { ++other; });
            CHECK(other == 0 && g.HasHeldOpen());                                    // the second Upgrade ignored, the Open held
            for (int i = 0; i < 100000 && g.prompt().kind == PromptKind::Working; ++i) g.Tick(std::chrono::milliseconds(0));
            CHECK(g.prompt().kind == PromptKind::Done && g.prompt().result.rewritten == 3 && done == 0);   // an up-to-date stamp still has old scenes to bring along
            g.Answer(Choice::Continue);
            CHECK(done == 1 && !g.Busy() && other == 1);                             // the held Open ran after the window closed
            g.Upgrade((td.path / "loose.okayscene").string(), [&] { ++done; });     // not a project: nothing happens
            CHECK(!g.Busy() && done == 1);
        }
        // the safety net under EditorState::Load
        {
            TempDir td; Proj p(td.path); stamp(p, "2.850.0");
            ProjectGate g(FencedEnv(td.path));
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

    // ---- the launcher already asked (--project-answer): the gate does what was answered and does not ask a second time -------------------
    // docs/project_upgrade.md, "The launcher's side". The answer is applied once, to the project the launcher named, and only while the
    // project is still what the launcher saw: "upgrade" never to a project from a newer version, "open anyway" never to an older one.
    {
        using okayver::PreAnswer;
        auto sceneOf = [](const Proj& p) { return (p.root / "Assets" / "Scenes" / "Level1.okayscene").string(); };
        auto stamp = [](const Proj& p, const char* v) { std::string e; if (*v) CHECK(okayver::WriteProjectVersion(p.root.string(), v, &e)); };
        auto finish = [](ProjectGate& g) { for (int i = 0; i < 100000 && g.prompt().kind == PromptKind::Working; ++i) g.Tick(std::chrono::milliseconds(0)); };
        GateEnv env;
        env.running = "2.855.0";

        {   // Older + "upgrade": no question, the backup and upgrade start at once, the result window as usual, then the project opens
            TempDir td; Proj p(td.path, 20 * 1024); stamp(p, "2.850.0");
            ProjectGate g(env);
            int go = 0, no = 0;
            g.NotePreAnswer(sceneOf(p), PreAnswer::Upgrade);
            CHECK(g.HasPreAnswer(p.root.string()));
            g.Open(sceneOf(p), [&] { ++go; }, [&] { ++no; });
            CHECK(g.prompt().kind == PromptKind::Working && !g.prompt().backupOnly && go == 0 && no == 0);   // straight to the progress window
            CHECK(g.prompt().projectVersion == "2.850.0" && g.prompt().runningVersion == "2.855.0" && g.prompt().root == p.root.string());
            CHECK(!g.HasPreAnswer(p.root.string()));                                          // used up
            finish(g);
            CHECK(g.prompt().kind == PromptKind::Done && g.prompt().result.ok && go == 0);    // the result window, as today
            CHECK(okayver::ReadProjectVersion(p.root.string()) == "2.855.0" && BackupCount(p) == 1);
            g.Answer(Choice::Continue);
            CHECK(go == 1 && no == 0 && !g.Busy() && g.IsCleared(p.root.string()));
            g.Open(sceneOf(p), [&] { ++go; });                                                // and it is not asked about again
            CHECK(go == 2 && !g.Busy());
        }
        {   // no version recorded + "upgrade": the same
            TempDir td; Proj p(td.path, 20 * 1024);                                            // (the Proj has a project.okayproj without a stamp)
            ProjectGate g(env);
            int go = 0;
            g.NotePreAnswer(sceneOf(p), PreAnswer::Upgrade);
            g.Open(sceneOf(p), [&] { ++go; });
            CHECK(g.prompt().kind == PromptKind::Working && !g.prompt().backupOnly && g.prompt().projectVersion.empty());
            finish(g);
            CHECK(g.prompt().kind == PromptKind::Done && okayver::ReadProjectVersion(p.root.string()) == "2.855.0");
            g.Answer(Choice::Continue);
            CHECK(go == 1 && !g.Busy());
        }
        {   // Newer + "open anyway": the backup first (the question's own promise), nothing in the project changed, then it opens
            TempDir td; Proj p(td.path, 20 * 1024); stamp(p, "2.860.0");
            ProjectGate g(env);
            int go = 0, no = 0;
            g.NotePreAnswer(sceneOf(p), PreAnswer::OpenAnyway);
            g.Open(sceneOf(p), [&] { ++go; }, [&] { ++no; });
            CHECK(g.prompt().kind == PromptKind::Working && g.prompt().backupOnly && go == 0 && no == 0);
            finish(g);
            CHECK(g.prompt().kind == PromptKind::Done && g.prompt().backupOnly && g.prompt().result.ok);
            CHECK(okayver::ReadProjectVersion(p.root.string()) == "2.860.0" && BackupCount(p) == 1);   // the stamp is the newer editor's still
            g.Answer(Choice::Continue);
            CHECK(go == 1 && no == 0 && !g.Busy());
        }
        {   // an answer that does not fit the project is dropped and the editor asks its own question
            TempDir td; Proj p(td.path, 20 * 1024); stamp(p, "2.860.0");                       // newer than the editor
            const Tree before = Snap(p.root);
            ProjectGate g(env);
            int go = 0, no = 0;
            g.NotePreAnswer(sceneOf(p), PreAnswer::Upgrade);                                   // never "upgrade" a project from a newer version
            g.Open(sceneOf(p), [&] { ++go; }, [&] { ++no; });
            CHECK(g.prompt().kind == PromptKind::Newer && go == 0 && no == 0 && Snap(p.root) == before && BackupCount(p) == 0);
            CHECK(!g.HasPreAnswer(p.root.string()));
            g.Answer(Choice::Cancel);
            g.Open(sceneOf(p), [&] { ++go; });                                                 // and it was a single use: the next open asks as well
            CHECK(g.prompt().kind == PromptKind::Newer);
            g.Answer(Choice::Cancel);
        }
        {
            TempDir td; Proj p(td.path, 20 * 1024); stamp(p, "2.850.0");                       // older than the editor
            const Tree before = Snap(p.root);
            ProjectGate g(env);
            g.NotePreAnswer(sceneOf(p), PreAnswer::OpenAnyway);                                // "open anyway" is for newer projects
            g.Open(sceneOf(p), [] {});
            CHECK(g.prompt().kind == PromptKind::Older && Snap(p.root) == before);
            g.Answer(Choice::Cancel);
        }
        {   // the project is not what the launcher saw (someone upgraded it meanwhile): it just opens, and the answer is used up, not kept
            TempDir td; Proj p(td.path, 20 * 1024); stamp(p, "2.855.0");
            const Tree before = Snap(p.root);
            ProjectGate g(env);
            int go = 0;
            g.NotePreAnswer(sceneOf(p), PreAnswer::Upgrade);
            g.Open(sceneOf(p), [&] { ++go; });
            CHECK(go == 1 && !g.Busy() && Snap(p.root) == before && !g.HasPreAnswer(p.root.string()));
            // ... so a stale answer can't upgrade the project later, in another session of opening it
            TempDir td2; Proj q(td2.path, 20 * 1024); stamp(q, "2.855.0");
            ProjectGate g2(env);
            g2.NotePreAnswer(sceneOf(q), PreAnswer::Upgrade);
            g2.Open(sceneOf(q), [] {});
            stamp(q, "2.850.0");                                                               // the project is older again
            ProjectGate g3(env);                                                               // (a cleared project is never asked again in a session, so use a new gate to look)
            g3.Open(sceneOf(q), [] {});
            CHECK(g3.prompt().kind == PromptKind::Older);
            g3.Answer(Choice::Cancel);
        }
        {   // only the project the answer was for: another older project asks its own question
            TempDir td; Proj a(td.path / "A", 20 * 1024), b(td.path / "B", 20 * 1024); stamp(a, "2.850.0"); stamp(b, "2.850.0");
            ProjectGate g(env);
            g.NotePreAnswer(sceneOf(a), PreAnswer::Upgrade);
            g.Open(sceneOf(b), [] {});
            CHECK(g.prompt().kind == PromptKind::Older && g.prompt().root == b.root.string() && g.HasPreAnswer(a.root.string()));
            g.Answer(Choice::Cancel);
            g.Open(sceneOf(a), [] {});                                                         // A still has its answer
            CHECK(g.prompt().kind == PromptKind::Working && g.prompt().root == a.root.string());
            finish(g);
            g.Answer(Choice::Continue);
        }
        {   // a scene of the same project (another folder in it) is the same project
            TempDir td;
            const fs::path game = td.path / "Game";
            WriteFile(game / "project.okayproj", "engine_version=2.850.0\n");
            WriteFile(game / "Assets" / "main.okayscene", Current("Main", 1));
            WriteFile(game / "Levels" / "b.okayscene", Current("B", 1));
            GateEnv fe = FencedEnv(td.path);
            ProjectGate g(fe);
            g.NotePreAnswer((game / "Assets" / "main.okayscene").string(), PreAnswer::Upgrade);
            g.Open((game / "Levels" / "b.okayscene").string(), [] {});
            CHECK(g.prompt().kind == PromptKind::Working && g.prompt().root == game.string());
            finish(g);
            CHECK(g.prompt().kind == PromptKind::Done);
            g.Answer(Choice::Continue);
        }
        {   // a scene that is not in a project: nothing to answer for, nothing kept
            TempDir td;
            WriteFile(td.path / "loose" / "x.okayscene", Current("x", 1));
            ProjectGate g(FencedEnv(td.path));
            int go = 0;
            g.NotePreAnswer((td.path / "loose" / "x.okayscene").string(), PreAnswer::Upgrade);
            g.NotePreAnswer("", PreAnswer::Upgrade);
            g.NotePreAnswer((td.path / "nowhere" / "y.okayscene").string(), PreAnswer::OpenAnyway);
            g.Open((td.path / "loose" / "x.okayscene").string(), [&] { ++go; });
            CHECK(go == 1 && !g.Busy() && fs::exists(td.path / "loose") && !fs::exists(td.path / "loose" / "project.okayproj"));
            g.NotePreAnswer((td.path / "loose" / "x.okayscene").string(), PreAnswer::None);   // "no answer" is not an answer
            CHECK(!g.HasPreAnswer((td.path / "loose").string()));
        }
        {   // while another window is on screen the open is held, and its answer is kept for it until it runs
            TempDir td; Proj a(td.path / "A", 20 * 1024), b(td.path / "B", 20 * 1024), c(td.path / "C", 20 * 1024);
            stamp(a, "2.850.0"); stamp(b, "2.850.0"); stamp(c, "2.850.0");
            {
                ProjectGate g(env);
                int goB = 0, noB = 0;
                g.Open(sceneOf(a), [] {}, [] {});                                              // A's question is on screen
                g.NotePreAnswer(sceneOf(b), PreAnswer::Upgrade);
                g.Open(sceneOf(b), [&] { ++goB; }, [&] { ++noB; });                            // the launcher hands B over: held
                CHECK(g.HasHeldOpen() && g.prompt().root == a.root.string() && g.HasPreAnswer(b.root.string()));
                g.Answer(Choice::Cancel);                                                      // A is cancelled: B opens, and its answer is used
                CHECK(g.prompt().kind == PromptKind::Working && g.prompt().root == b.root.string() && !g.HasHeldOpen());
                finish(g);
                g.Answer(Choice::Continue);
                CHECK(goB == 1 && noB == 0 && !g.Busy());
            }
            {   // a held open that is replaced by a newer one takes its answer with it
                stamp(b, "2.850.0");                                                           // (the case above upgraded B: make it older again)
                ProjectGate g(env);
                int noB = 0;
                g.Open(sceneOf(a), [] {}, [] {});
                g.NotePreAnswer(sceneOf(b), PreAnswer::Upgrade);
                g.Open(sceneOf(b), [] {}, [&] { ++noB; });
                g.Open(sceneOf(c), [] {}, [] {});                                              // replaces B
                CHECK(noB == 1 && !g.HasPreAnswer(b.root.string()));
                g.Answer(Choice::Cancel);                                                      // A cancelled; C asks its own question
                CHECK(g.prompt().kind == PromptKind::Older && g.prompt().root == c.root.string());
                g.Answer(Choice::Cancel);
                g.Open(sceneOf(b), [] {});                                                     // B asks, it was not upgraded behind anyone's back
                CHECK(g.prompt().kind == PromptKind::Older);
                g.Answer(Choice::Cancel);
            }
        }
        {   // the safety net under EditorState::Load is not loosened by an answer that has not been used yet
            TempDir td; Proj p(td.path, 20 * 1024); stamp(p, "2.850.0");
            ProjectGate g(env);
            g.NotePreAnswer(sceneOf(p), PreAnswer::Upgrade);
            std::string why;
            CHECK(!g.MayLoad(sceneOf(p), &why) && !why.empty());
            CHECK(g.HasPreAnswer(p.root.string()));
        }
        {   // --keep-version (chosen on purpose) still wins for an older project: it opens in this editor as it always did
            TempDir td; Proj p(td.path, 20 * 1024); stamp(p, "2.850.0");
            const Tree before = Snap(p.root);
            ProjectGate g(env);
            int go = 0;
            g.NoteKeepRequest(sceneOf(p));
            g.NotePreAnswer(sceneOf(p), PreAnswer::Upgrade);
            g.Open(sceneOf(p), [&] { ++go; });
            CHECK(go == 1 && !g.Busy() && Snap(p.root) == before);
        }
        {   // a development build never asks and never upgrades, answer or not
            TempDir td; Proj p(td.path, 20 * 1024); stamp(p, "2.850.0");
            const Tree before = Snap(p.root);
            GateEnv de = env; de.running = "dev";
            ProjectGate g(de);
            int go = 0;
            g.NotePreAnswer(sceneOf(p), PreAnswer::Upgrade);
            g.Open(sceneOf(p), [&] { ++go; });
            CHECK(go == 1 && !g.Busy() && Snap(p.root) == before);
        }
        {   // the very big folder guard is still the editor's own question, answer or not
            TempDir td; Proj p(td.path, 20 * 1024); stamp(p, "2.850.0");
            GateEnv ge = env; ge.largeFiles = 3;
            ProjectGate g(ge);
            g.NotePreAnswer(sceneOf(p), PreAnswer::Upgrade);
            g.Open(sceneOf(p), [] {});
            finish(g);
            CHECK(g.prompt().kind == PromptKind::Large && BackupCount(p) == 0);
            g.Answer(Choice::Cancel);
            CHECK(!g.Busy() && okayver::ReadProjectVersion(p.root.string()) == "2.850.0");
        }
    }

    TEST_MAIN_RESULT();
}
