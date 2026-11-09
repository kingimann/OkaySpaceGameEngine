// Saving, loading and the unsaved-changes rules of the editor (ImGui-free EditorState, so it runs
// headless): a save is whole or not at all, a failed Open changes nothing, Undo never carries one scene's
// objects into another scene's file, an edit that forgot to raise the dirty flag is still found, Play-mode
// edits are neither saved nor counted, and a new project is made all-or-nothing. Temp folders only.
#include "test_framework.hpp"
#include "../editor/EditorState.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "../shared/ProjectCreate.hpp"

#include <chrono>
#include <fstream>

namespace fs = std::filesystem;
using namespace okay;
using okay::editor::EditorState;

namespace {
struct TempDir {
    fs::path path;
    TempDir() {
        static int n = 0;
        path = fs::temp_directory_path() / ("okay_projsave_" + std::to_string(++n) + "_" +
               std::to_string((long long)std::chrono::steady_clock::now().time_since_epoch().count()));
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::create_directories(path, ec);
    }
    ~TempDir() {
        std::error_code ec;
        for (auto& e : fs::recursive_directory_iterator(path, ec)) fs::permissions(e.path(), fs::perms::owner_all, ec);
        fs::remove_all(path, ec);
    }
    std::string Sub(const std::string& s) const { return (path / s).string(); }
};
std::string Read(const std::string& p) { std::string s; okayproj::ReadText(p, s); return s; }
void Write(const std::string& p, const std::string& s) { std::ofstream(p, std::ios::binary | std::ios::trunc) << s; }
bool Has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }
std::size_t Count(const EditorState& ed) { return ed.scene().Objects().size(); }
bool HasObject(EditorState& ed, const std::string& name) { return ed.scene().Find(name) != nullptr; }
void Fresh() { Game::Reset(); Time::ResetForTesting(); }

struct Probe : public Behaviour {
    float moved = 0;
    void Update(float dt) override { moved += dt; transform->localPosition.x += dt * 10.0f; }
};
}  // namespace

int main() {
    RUN_SUITE("project_save");
    Log::console = false;

    // ---- a failed Open changes nothing ---------------------------------------------------------------------
    {
        Fresh();
        TempDir t;
        EditorState ed;
        ed.CreateCube("Mine");
        const std::string good = t.Sub("good.okayscene");
        std::string err;
        CHECK(ed.Save(good, &err) && err.empty() && !ed.dirty && !ed.HasUnsavedChanges());
        ed.CreateCube("Unsaved edit");
        CHECK(ed.dirty);
        const int depth = ed.UndoDepth();
        const std::string path = ed.path();
        const std::size_t n = Count(ed);

        Write(t.Sub("broken.okayscene"), "this is not a scene file at all\n\x01\x02");
        CHECK(!ed.Load(t.Sub("broken.okayscene"), &err));
        CHECK(!err.empty());
        CHECK(!ed.Load(t.Sub("missing.okayscene"), &err) && Has(err, "cannot open"));
        CHECK(ed.path() == path && Count(ed) == n && HasObject(ed, "Unsaved edit"));   // the scene is still there
        CHECK(ed.dirty && ed.UndoDepth() == depth);                                    // and still counts as unsaved, with its history
        CHECK(ed.HasUnsavedChanges());
        {   // a scene cut off half way is refused too, whole
            const std::string text = Read(good);
            Write(t.Sub("cut.okayscene"), text.substr(0, text.size() / 2));
            const bool cutOk = ed.Load(t.Sub("cut.okayscene"), &err);
            if (!cutOk) CHECK(Count(ed) == n && ed.path() == path);   // refused: nothing changed
        }
    }

    // ---- Undo never puts one scene into another scene's file ----------------------------------------------
    {
        Fresh();
        TempDir t;
        EditorState ed;
        ed.CreateCube("OnlyInA");
        CHECK(ed.Save(t.Sub("a.okayscene")));
        ed.NewScene();
        ed.CreateCube("OnlyInB");
        CHECK(ed.Save(t.Sub("b.okayscene")));
        const std::string bText = Read(t.Sub("b.okayscene"));

        CHECK(ed.Load(t.Sub("a.okayscene")));
        CHECK(HasObject(ed, "OnlyInA") && !HasObject(ed, "OnlyInB"));
        CHECK(!ed.CanUndo() && !ed.CanRedo() && ed.UndoDepth() == 0);   // a loaded scene starts a fresh history
        CHECK(!ed.Undo());                                              // so Ctrl+Z cannot bring B's objects into a.okayscene
        CHECK(HasObject(ed, "OnlyInA") && !HasObject(ed, "OnlyInB"));
        CHECK(!ed.dirty && !ed.HasUnsavedChanges() && ed.path() == t.Sub("a.okayscene"));
        CHECK(Read(t.Sub("b.okayscene")) == bText);                      // b.okayscene was never touched
    }

    // ---- the saved state is what the scene holds, not what a flag says -------------------------------------
    {
        Fresh();
        TempDir t;
        EditorState ed;
        CHECK(!ed.HasUnsavedChanges());                                  // a new editor has nothing to save
        ed.NewScene3D();
        CHECK(!ed.dirty && !ed.HasUnsavedChanges());                     // a fresh template is clean
        ed.NewScene2D();
        CHECK(!ed.HasUnsavedChanges());
        ed.NewPlatformer();
        CHECK(!ed.HasUnsavedChanges());

        // An edit that does not touch ed.dirty (a widget that forgot) is still found.
        GameObject* go = ed.scene().CreateGameObject("Sneaky");
        go->transform->localPosition = {1, 2, 3};
        CHECK(!ed.dirty);                                                // the flag knows nothing
        CHECK(ed.HasUnsavedChanges());                                   // the scene text does
        CHECK(!ed.dirty);                                                // asking doesn't change anything
        CHECK(ed.SyncDirty() && ed.dirty);                               // the periodic check raises the flag so the title and autosave follow

        std::string err;
        CHECK(ed.Save(t.Sub("s.okayscene"), &err));
        CHECK(!ed.dirty && !ed.HasUnsavedChanges());
        go->name = "Renamed";                                            // a rename, no flag
        CHECK(ed.HasUnsavedChanges());
        go->name = "Sneaky";                                             // put back by hand: identical again
        CHECK(!ed.HasUnsavedChanges());
        go->transform->localPosition = {1, 2, 4};
        CHECK(ed.HasUnsavedChanges());
        go->transform->localPosition = {1, 2, 3};
        CHECK(!ed.HasUnsavedChanges());
        ed.scene().renderSettings.fog = true;                            // scene-level settings count too
        CHECK(ed.HasUnsavedChanges());
        ed.scene().renderSettings.fog = false;
        GameObject* extra = ed.scene().CreateGameObject("Extra");
        extra->AddComponent<Rigidbody2D>();
        CHECK(ed.HasUnsavedChanges());
        ed.scene().Destroy(extra);
        ed.scene().FlushDestroyed();
        CHECK(!ed.HasUnsavedChanges());                                  // deleted again: the scene is what was saved

        // Load: the loaded scene is the baseline.
        CHECK(ed.Load(t.Sub("s.okayscene")) && !ed.HasUnsavedChanges());
        // Undo / Redo raise the flag (they change the scene), and the save clears it.
        ed.CreateCube("Cube");
        CHECK(ed.dirty);
        CHECK(ed.Undo() && ed.dirty);
        CHECK(ed.Save(t.Sub("s.okayscene")) && !ed.HasUnsavedChanges());
    }

    // ---- Play: edits to the running game are thrown away and never saved -----------------------------------
    {
        Fresh();
        TempDir t;
        EditorState ed;
        GameObject* mover = ed.scene().CreateGameObject("Mover");
        mover->AddComponent<Probe>();
        CHECK(ed.Save(t.Sub("p.okayscene")));
        const std::string onDisk = Read(t.Sub("p.okayscene"));
        CHECK(!ed.HasUnsavedChanges());

        ed.Play();
        CHECK(ed.isPlaying());
        ed.Tick(0.5f);
        ed.scene().Find("Mover")->transform->localPosition.y = 42;       // the game moved it; a script could do anything
        ed.dirty = true;                                                 // and an Inspector tweak in Play raises the flag
        ed.scene().CreateGameObject("SpawnedInPlay");
        CHECK(!ed.HasUnsavedChanges());                                  // none of that counts: Stop throws it all away
        std::string err;
        CHECK(!ed.Save(t.Sub("p.okayscene"), &err));                     // Save refuses while the game runs
        CHECK(Has(err, "Stop Play"));
        CHECK(Read(t.Sub("p.okayscene")) == onDisk);                     // and the file is exactly as it was
        CHECK(!fs::exists(t.Sub("p.okayscene.savetmp")));
        ed.Stop();
        CHECK(!ed.isPlaying() && !HasObject(ed, "SpawnedInPlay"));
        CHECK(ed.scene().Find("Mover")->transform->localPosition.y == 0.0f);
        CHECK(!ed.dirty && !ed.HasUnsavedChanges());                     // Play and Stop leave a clean scene clean

        // A scene with real unsaved edits stays unsaved through Play.
        ed.CreateCube("Edit");
        ed.Play();
        CHECK(ed.HasUnsavedChanges());
        ed.Stop();
        CHECK(ed.dirty && ed.HasUnsavedChanges() && HasObject(ed, "Edit"));
    }

    // ---- hosting from the editor does not put the network object in the scene --------------------------------
    {
        Fresh();
        EditorState ed;
        ed.CreateCube("Real");
        ed.StartHost(0);                                                 // makes "__Network"; the port result doesn't matter here
        CHECK(HasObject(ed, "__Network"));
        const std::string text = SceneSerializer::Serialize(ed.scene());
        CHECK(!Has(text, "__Network") && !Has(text, "networkmanager") && Has(text, "Real"));
        ed.StopNetwork();
        CHECK(!HasObject(ed, "__Network"));
    }

    // ---- a save is whole or not at all ----------------------------------------------------------------------
    {
        Fresh();
        TempDir t;
        EditorState ed;
        ed.CreateCube("One");
        const std::string f = t.Sub("w.okayscene");
        std::string err;
        CHECK(ed.Save(f, &err));
        const std::string v1 = Read(f);
        CHECK(!fs::exists(f + ".savetmp") && !fs::exists(f + ".bak"));
        ed.CreateCube("Two");
        CHECK(ed.Save(f, &err));
        const std::string v2 = Read(f);
        CHECK(v2 != v1 && Read(f + ".bak") == v1);                       // one rolling backup of the previous good file
        CHECK(!fs::exists(f + ".savetmp"));

        // A read-only scene file is refused, with a reason, and not changed.
        std::error_code ec;
        fs::permissions(f, fs::perms::owner_read | fs::perms::group_read | fs::perms::others_read, ec);
        ed.CreateCube("Three");
        CHECK(!ed.Save(f, &err) && Has(err, "read-only"));
        CHECK(Read(f) == v2 && ed.dirty && ed.HasUnsavedChanges());     // still unsaved, still on screen
        CHECK(!fs::exists(f + ".savetmp"));
        fs::permissions(f, fs::perms::owner_read | fs::perms::owner_write, ec);
        CHECK(ed.Save(f, &err) && !ed.HasUnsavedChanges() && Has(Read(f), "Three"));

        // A missing folder, and a folder where the file should be.
        CHECK(!ed.Save(t.Sub("nofolder/x.okayscene"), &err) && Has(err, "doesn't exist") && !fs::exists(t.Sub("nofolder")));
        fs::create_directories(t.Sub("adir.okayscene"));
        CHECK(!ed.Save(t.Sub("adir.okayscene"), &err) && Has(err, "folder"));
        CHECK(fs::is_directory(t.Sub("adir.okayscene")));
        CHECK(ed.path() == f);                                           // a failed Save As keeps the open scene's path
        ed.CreateCube("Four");
        CHECK(!ed.Save(t.Sub("adir.okayscene")));
        CHECK(ed.dirty);                                                 // and the scene stays unsaved

        // SceneSerializer alone, the same rules.
        Scene s("s");
        s.CreateGameObject("x");
        CHECK(SceneSerializer::SaveToFile(s, t.Sub("plain.okayscene"), &err));
        CHECK(!SceneSerializer::SaveToFile(s, t.Sub("nofolder/p.okayscene"), &err) && !err.empty());
        CHECK(SceneSerializer::SaveToFile(s, t.Sub("noerrorarg.okayscene")));     // the old two-argument form still works
        Scene back("b");
        CHECK(SceneSerializer::LoadFromFile(back, t.Sub("plain.okayscene")) && back.Find("x"));
    }

    // ---- a new project is made all or nothing ----------------------------------------------------------------
    {
        Fresh();
        TempDir t;
        EditorState ed;
        ed.CreateCube("Precious");
        std::string old, nu;
        int calls = 0;
        EditorState::onProjectChanged = [&](const std::string& o, const std::string& n) { old = o; nu = n; ++calls; };
        CHECK(ed.Save(t.Sub("before.okayscene")));
        ed.CreateCube("Unsaved");
        const int depth = ed.UndoDepth();

        // 1. A failure while writing the project's own files: nothing is left, the open scene is untouched.
        const std::string root = t.Sub("Proj");
        auto r = ed.CreateProject(root, "Proj", &EditorState::NewScene3D,
                                  [](const std::string&, std::string& e) { e = "the disk is full"; return false; });
        CHECK(!r.ok && r.error == "the disk is full");
        CHECK(!fs::exists(root));
        CHECK(HasObject(ed, "Precious") && HasObject(ed, "Unsaved") && ed.dirty && ed.UndoDepth() == depth);
        CHECK(ed.path() == t.Sub("before.okayscene") && ed.projectDir().empty() && calls == 0);

        // 2. The first scene can't be written (a folder is in its way): the previous scene, path and history come back.
        fs::create_directories(fs::path(root) / "Assets" / "Proj.okayscene");
        r = ed.CreateProject(root, "Proj", &EditorState::NewScene3D);
        CHECK(!r.ok && Has(r.error, "folder"));
        CHECK(HasObject(ed, "Precious") && HasObject(ed, "Unsaved") && !HasObject(ed, "Ground"));
        CHECK(ed.path() == t.Sub("before.okayscene") && ed.projectDir().empty() && ed.dirty && ed.UndoDepth() == depth);
        CHECK(ed.HasUnsavedChanges());
        CHECK(fs::is_directory(fs::path(root) / "Assets" / "Proj.okayscene"));            // what was already there is kept
        CHECK(!fs::exists(fs::path(root) / "Assets" / "Scenes") && !fs::exists(fs::path(root) / ".okay"));   // what it made is gone
        fs::remove_all(root);

        // 3. Success: folders, the files from the caller, the template scene saved and open.
        bool wrote = false;
        r = ed.CreateProject(root, "Proj", &EditorState::NewScene3D,
                             [&](const std::string& rt, std::string& e) {
                                 wrote = true;
                                 return okayproj::WriteTextAtomic(okayproj::SettingsPath(rt), "company=Test\n", &e);
                             });
        CHECK(r.ok && r.error.empty() && wrote);
        CHECK(r.root == root && r.scene == (fs::path(root) / "Assets" / "Proj.okayscene").string());
        CHECK(fs::exists(r.scene) && Read(okayproj::SettingsPath(root)) == "company=Test\n");
        for (const std::string& f : okayproj::StandardFolders()) CHECK(fs::is_directory(fs::path(root) / "Assets" / f));
        CHECK(fs::is_directory(fs::path(root) / ".okay"));
        CHECK(ed.path() == r.scene && ed.projectDir() == root && !ed.dirty && !ed.HasUnsavedChanges());
        CHECK(ed.scene().Name() == "Proj");                                              // the scene is named after the project
        CHECK(HasObject(ed, "Ground") && !HasObject(ed, "Precious"));                  // the template, not the old scene
        CHECK(ed.scene().mainCamera != nullptr);                                         // and it has a camera
        CHECK(!ed.CanUndo());                                                            // a new project starts with a fresh history
        CHECK(calls >= 1 && nu == root);
        Scene again("again");
        CHECK(SceneSerializer::LoadFromFile(again, r.scene) && again.Name() == "Proj" && again.Find("Ground"));   // the file reads back
        EditorState::onProjectChanged = nullptr;

        // 4. Not while the game runs.
        ed.Play();
        r = ed.CreateProject(t.Sub("Other"), "Other", &EditorState::NewScene2D);
        CHECK(!r.ok && Has(r.error, "Stop Play") && !fs::exists(t.Sub("Other")));
        ed.Stop();
    }

    TEST_MAIN_RESULT();
}
