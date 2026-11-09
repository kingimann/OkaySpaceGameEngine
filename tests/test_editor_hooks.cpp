// The editor's hooks into the core systems (ImGui-free, so it runs headless): Play runs on GameLoop, so
// Time, the time scale, hit stops and the Clock channels work in Play; the editor's Pause and Step are a
// Game pause and a debug step; Play and Stop start a clean session; DuplicateSelected keeps the parent;
// and an old project file's tag lines load unchanged into the tag list. Fake frame times only: no sleeping.
#include "test_framework.hpp"
#include "EditorState.hpp"
#include "okay/Core/Clock.hpp"
#include "ProjectUpgrade.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>

using namespace okay;
using okay::editor::EditorState;

namespace {

// What a game component sees each frame of Play.
struct Probe : public Behaviour {
    int updates = 0;
    float lastDt = -1.0f;          // the delta Update got
    float lastTimeDt = -1.0f;      // Time::DeltaTime() inside Update
    bool pausedSeen = false;       // Game::Paused() inside Update
    void Update(float dt) override {
        ++updates;
        lastDt = dt;
        lastTimeDt = Time::DeltaTime();
        pausedSeen = Game::Paused();
    }
};

void Fresh() {
    Game::SetPauseOnFocusLoss(false);
    Game::NotifyFocus(true);
    Game::Reset();
    Time::ResetForTesting();
    Time::SetFixedDeltaTime(0.02f);
    ClockSet::Global().Reset();
}

Probe* AddProbe(EditorState& ed) { return ed.scene().CreateGameObject("probe")->AddComponent<Probe>(); }

std::vector<std::string> ChildNames(const GameObject* parent) {
    std::vector<std::string> out;
    for (const Transform* c : parent->transform->Children()) out.push_back(c->gameObject->name);
    return out;
}

}  // namespace

int main() {
    RUN_SUITE("editor_hooks");
    Log::console = false;

    // ---- Editing never steps Time ------------------------------------------------------------------------
    {
        Fresh();
        EditorState ed;
        CHECK(!ed.PlayFrame(0.016));
        ed.Tick(0.016f);
        CHECK(Time::FrameCount() == 0 && Time::ElapsedTime() == 0.0f && Time::DeltaTime() == 0.0f);
        CHECK(!ed.StepFrame());                       // nothing to step outside Play
        ed.SetPaused(true);                           // and nothing to pause
        CHECK(!ed.paused() && !Game::Paused());
    }

    // ---- Play steps Time: DeltaTime, ElapsedTime, FrameCount, and the scene's update delta ------------------
    {
        Fresh();
        EditorState ed;
        Probe* p = AddProbe(ed);
        Time::SetTimeScale(0.25f);                    // a script left it from an earlier session
        ed.Play();
        CHECK(Time::BaseTimeScale() == 1.0f);         // Play starts a session: scale back to 1
        const auto frame0 = Time::FrameCount();
        for (int i = 0; i < 3; ++i) CHECK(ed.PlayFrame(0.016, false));
        CHECK(p->updates == 3);
        CHECK_NEAR(p->lastDt, 0.016, 1e-7);
        CHECK_NEAR(p->lastTimeDt, 0.016, 1e-7);       // Time::DeltaTime() is no longer stuck at 0
        CHECK(Time::FrameCount() == frame0 + 3);
        CHECK_NEAR(Time::ElapsedTime(), 0.048, 1e-6);

        // The player's clamp: one 0.5 s hitch is a 0.1 s step.
        ed.PlayFrame(0.5, false);
        CHECK_NEAR(p->lastDt, 0.1, 1e-7);
        // NaN and negative frame times are 0 s frames, not poison.
        ed.PlayFrame(std::nan(""), false);
        CHECK(p->lastDt == 0.0f && std::isfinite(Time::ElapsedTime()));

        // A script's Time.timeScale reaches the scene (it did nothing in the editor before).
        Time::SetTimeScale(0.5f);
        ed.PlayFrame(0.02, false);
        CHECK_NEAR(p->lastDt, 0.01, 1e-7);
        Time::SetTimeScale(1.0f);

        // A hit stop freezes the scene, then lets go by itself.
        Time::HitStop(0.05f);
        ed.PlayFrame(0.02, false);
        CHECK(p->lastDt == 0.0f);
        int back = -1;
        for (int i = 0; i < 6 && back < 0; ++i) { ed.PlayFrame(0.02, false); if (p->lastDt > 0.0f) back = i; }
        CHECK(back >= 0);

        // The clock channels tick: "game" follows the scale, "ui" real time.
        ed.Stop();
        ed.Play();
        CHECK(Clock::Get("ui").Elapsed() == 0.0 && Clock::Get("game").Elapsed() == 0.0);   // zeroed by the new session
        Probe* q = AddProbe(ed);
        (void)q;
        Time::SetTimeScale(0.5f);
        for (int i = 0; i < 5; ++i) ed.PlayFrame(0.02, false);
        CHECK_NEAR(Clock::Get("ui").Elapsed(), 0.1, 1e-6);
        CHECK_NEAR(Clock::Get("game").Elapsed(), 0.05, 1e-6);
        ed.Stop();
    }

    // ---- Smoothing is the old editor's: SmoothDelta of the clamped time -------------------------------------
    {
        Fresh();
        EditorState ed;
        Probe* p = AddProbe(ed);
        ed.Play();
        const float seq[] = {0.016f, 0.016f, 0.016f, 0.016f, 0.040f};
        for (float f : seq) ed.PlayFrame(f, true, 0.0f);
        CHECK_NEAR(p->lastDt, (0.016f * 3 + 0.040f) / 4.0f, 1e-6);   // average of the last four
        CHECK(ed.playLoop().options().smoothDelta && ed.playLoop().options().maxDelta == 0.1f);
        ed.PlayFrame(0.040, false);                                   // switching it off steps the raw time
        CHECK_NEAR(p->lastDt, 0.040, 1e-7);
        ed.Stop();
    }

    // ---- The editor's Pause is a Game pause: nothing updates, Time stands still ------------------------------
    {
        Fresh();
        EditorState ed;
        Probe* p = AddProbe(ed);
        ed.Play();
        ed.PlayFrame(0.016, false);
        CHECK(p->updates == 1);
        ed.SetPaused(true);
        CHECK(ed.paused() && Game::Paused() && Game::IsPausedBy("editor"));
        ed.PlayFrame(0.016, false);
        ed.PlayFrame(0.016, false);
        CHECK(p->updates == 1);                       // the scene is frozen, as before
        CHECK(Time::DeltaTime() == 0.0f);             // and Time says so
        const double elapsed = Time::ElapsedTimeD();
        ed.PlayFrame(0.016, false);
        CHECK(Time::ElapsedTimeD() == elapsed);
        ed.SetPaused(false);
        CHECK(!ed.paused() && !Game::Paused());
        ed.PlayFrame(0.016, false);
        CHECK(p->updates == 2 && p->lastDt > 0.0f);

        // A game pause (a pause menu) still ticks the scene with 0, so its UI keeps working.
        Game::SetPaused(true);
        ed.PlayFrame(0.016, false);
        CHECK(p->updates == 3 && p->lastDt == 0.0f && p->pausedSeen);
        Game::SetPaused(false);
        // Both at once: the editor's pause wins until it is lifted, then the menu's does.
        ed.SetPaused(true);
        Game::SetPaused(true);
        ed.SetPaused(false);
        CHECK(Game::Paused());
        Game::SetPaused(false);
        ed.Stop();
    }

    // ---- Step: one frame of 1/60 s, a running game for that frame, then paused again ---------------------------
    {
        Fresh();
        EditorState ed;
        Probe* p = AddProbe(ed);
        int ticks = 0; float tickDt = -1.0f;
        ed.playTick = [&](float dt) { ++ticks; tickDt = dt; ed.Tick(dt); };   // what main.cpp's debugger fiber wrapper is
        ed.Play();
        ed.PlayFrame(0.016, false);
        CHECK(ticks == 1 && p->updates == 1);

        CHECK(ed.StepFrame());                        // pauses first
        CHECK(ed.paused() && Game::Paused());
        ed.PlayFrame(5.0, false);                     // the real time is ignored: the step is exact
        CHECK(ticks == 2 && p->updates == 2);
        CHECK_NEAR(tickDt, 1.0 / 60.0, 1e-7);
        CHECK_NEAR(p->lastDt, 1.0 / 60.0, 1e-7);
        CHECK(!p->pausedSeen);                        // the scene saw a running game (controllers skip a paused one)
        CHECK(Game::Paused() && ed.paused() && Time::TimeScale() == 0.0f);   // and it is paused again after
        ed.PlayFrame(0.016, false);
        CHECK(ticks == 2);                            // no more frames until the next step

        CHECK(ed.StepFrame());
        CHECK(ed.StepFrame());                        // two clicks: two steps
        ed.PlayFrame(0.016, false);
        ed.PlayFrame(0.016, false);
        ed.PlayFrame(0.016, false);
        CHECK(ticks == 4 && p->updates == 4);

        // The time scale applies to a step, as it does to any frame.
        Time::SetTimeScale(0.5f);
        CHECK(ed.StepFrame());
        ed.PlayFrame(0.016, false);
        CHECK_NEAR(p->lastDt, 1.0 / 120.0, 1e-7);
        Time::SetTimeScale(1.0f);

        // A step still pending when the game resumes is dropped, not run on top of the running frame.
        CHECK(ed.StepFrame());
        ed.SetPaused(false);
        const int before = p->updates;
        ed.PlayFrame(0.016, false);
        CHECK(p->updates == before + 1 && p->lastDt > 0.016f * 0.99f);
        ed.PlayFrame(0.016, false);
        CHECK(p->updates == before + 2);
        ed.Stop();
    }

    // ---- Play and Stop start clean sessions: no stale pause, quit or time scale -----------------------------------
    {
        Fresh();
        EditorState ed;
        ed.Play();
        Time::SetTimeScale(0.0f);                      // a script froze the game ...
        ed.SetPaused(true);
        Game::RequestQuit("script");
        ed.Stop();                                     // ... and the editor stopped: everything is back
        CHECK(!Game::Paused() && !Game::QuitRequested() && Time::BaseTimeScale() == 1.0f && !ed.paused());
        Probe* p = AddProbe(ed);
        Time::SetTimeScale(0.0f);
        ed.Play();                                     // a scale a script left would freeze this Play
        ed.PlayFrame(0.016, false);
        CHECK(p->updates == 1 && p->lastDt > 0.0f);
        // A quit during Play is reported by the frame.
        Game::RequestQuit("menu");
        CHECK(!ed.PlayFrame(0.016, false));
        ed.Stop();
    }

    // ---- DuplicateSelected keeps the parent, names the copy, and is one undo step --------------------------------
    {
        Fresh();
        EditorState ed;
        Scene& sc = ed.scene();
        GameObject* parent = sc.CreateGameObject("Parent");
        GameObject* a = sc.CreateGameObject("Cube");
        GameObject* b = sc.CreateGameObject("Other");
        a->transform->SetParent(parent->transform, false);
        b->transform->SetParent(parent->transform, false);
        a->transform->localPosition = {1.0f, 2.0f, 3.0f};
        GameObject* kid = sc.CreateGameObject("Kid");
        kid->transform->SetParent(a->transform, false);
        const int objects = (int)sc.Objects().size();

        ed.Select(a);
        const int depth = ed.UndoDepth();
        GameObject* d = ed.DuplicateSelected();
        CHECK(d && d != a);
        CHECK(d->transform->Parent() == parent->transform);        // a child stays a child (it became a root before)
        CHECK(d->name == "Cube (1)" && a->name == "Cube");
        CHECK((ChildNames(parent) == std::vector<std::string>{"Cube", "Cube (1)", "Other"}));   // right behind the original
        CHECK(d->transform->Children().size() == 1);               // its children came along
        CHECK((int)sc.Objects().size() == objects + 2);
        CHECK_NEAR(d->transform->localPosition.x, 1.5, 1e-5);      // nudged half a unit (a unit parent: local == world)
        CHECK_NEAR(d->transform->localPosition.y, 2.5, 1e-5);
        CHECK_NEAR(d->transform->localPosition.z, 3.0, 1e-5);
        CHECK(ed.selected() == d && ed.MultiSelection().size() == 1 && ed.IsSelected(d));
        CHECK(ed.UndoDepth() == depth + 1 && ed.dirty);

        // Undo puts the scene back, redo does it again.
        CHECK(ed.Undo());
        CHECK((int)sc.Objects().size() == objects);
        CHECK(ed.selected() == nullptr || ed.selected()->name != "Cube (1)");
        GameObject* parentAfter = sc.Find("Parent");
        CHECK(parentAfter && (ChildNames(parentAfter) == std::vector<std::string>{"Cube", "Other"}));
        CHECK(ed.Redo());
        parentAfter = sc.Find("Parent");
        CHECK(parentAfter && (ChildNames(parentAfter) == std::vector<std::string>{"Cube", "Cube (1)", "Other"}));

        // The nudge is in world space: under a parent scaled 4x the copy is still half a unit away, not two.
        Fresh();
        EditorState ed2;
        Scene& s2 = ed2.scene();
        GameObject* big = s2.CreateGameObject("Big");
        big->transform->localScale = {4.0f, 4.0f, 4.0f};
        GameObject* c = s2.CreateGameObject("C");
        c->transform->SetParent(big->transform, false);
        ed2.Select(c);
        GameObject* c2 = ed2.DuplicateSelected();
        CHECK(c2 && c2->transform->Parent() == big->transform);
        const Vec3 off = c2->transform->Position() - c->transform->Position();
        CHECK_NEAR(off.x, 0.5, 1e-4);
        CHECK_NEAR(off.y, 0.5, 1e-4);
        CHECK_NEAR(off.z, 0.0, 1e-4);

        // A root stays a root; the numbers count up; nothing selected does nothing.
        Fresh();
        EditorState ed3;
        GameObject* r = ed3.CreateCube("Cube");
        ed3.Select(r);
        GameObject* r1 = ed3.DuplicateSelected();
        CHECK(r1 && r1->name == "Cube (1)" && r1->transform->Parent() == nullptr);
        ed3.Select(r);
        GameObject* r2 = ed3.DuplicateSelected();
        CHECK(r2 && r2->name == "Cube (2)");
        ed3.Select(nullptr);
        const int d3 = ed3.UndoDepth();
        CHECK(ed3.DuplicateSelected() == nullptr && ed3.UndoDepth() == d3);

        // Several at once: each copy under its own parent, one undo step; a selected child of a selected
        // parent is not copied twice.
        Fresh();
        EditorState ed4;
        Scene& s4 = ed4.scene();
        GameObject* pa = s4.CreateGameObject("PA");
        GameObject* pb = s4.CreateGameObject("PB");
        GameObject* ca = s4.CreateGameObject("Child");
        GameObject* cb = s4.CreateGameObject("Child");
        ca->transform->SetParent(pa->transform, false);
        cb->transform->SetParent(pb->transform, false);
        ed4.Select(ca);
        ed4.ToggleSelect(cb);
        const int depth4 = ed4.UndoDepth();
        const int n4 = (int)s4.Objects().size();
        GameObject* last = ed4.DuplicateSelected();
        CHECK(last && (int)s4.Objects().size() == n4 + 2 && ed4.MultiSelection().size() == 2);
        CHECK((ChildNames(pa) == std::vector<std::string>{"Child", "Child (1)"}));
        CHECK((ChildNames(pb) == std::vector<std::string>{"Child", "Child (1)"}));
        CHECK(ed4.UndoDepth() == depth4 + 1);
        ed4.Select(pa);
        ed4.ToggleSelect(pa->transform->Children()[0]->gameObject);   // the parent and one of its children
        const int n5 = (int)s4.Objects().size();
        CHECK(ed4.DuplicateSelected() != nullptr);
        CHECK((int)s4.Objects().size() == n5 + 3);                     // PA and its two children, once
    }

    // ---- An old project file's tag lines load unchanged into the project's tag list ----------------------------
    {
        ProjectLists::Reset();
        const std::string oldFile =
            "company=OkaySpace\nversion=1.0.0\nwidth=1280\nheight=720\ngravity2d=0 -9.81\nambient=0.3\nskybox=1\n"
            "splash_logo=\nsplash_bar=1\ntag=Boss\ntag=Pickup\ntag=Boss\ntag= bad\ntag=Door\n";
        int taken = 0;
        std::size_t at = 0;
        while (at < oldFile.size()) {                  // the loop LoadProjectSettings runs, line by line
            const std::size_t end = oldFile.find('\n', at);
            const std::string line = oldFile.substr(at, end - at);
            at = end == std::string::npos ? oldFile.size() : end + 1;
            const std::size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            if (ProjectLists::ApplyLine(line.substr(0, eq), line.substr(eq + 1))) ++taken;
        }
        CHECK((Tags::Custom() == std::vector<std::string>{"Boss", "Pickup", "Door"}));   // a duplicate and a bad name skipped
        CHECK(Tags::Contains("Player") && Tags::Contains("Door") && !Tags::Contains("Untagged"));
        CHECK(taken == 4);                             // Boss, Pickup, the repeated Boss (already listed) and Door
        CHECK(ProjectLists::Write() == "tag=Boss\ntag=Pickup\ntag=Door\n");              // saved the way it was written
        // The tag menu's rename: the list and every object that carries it.
        Scene s("tags");
        GameObject* g = s.CreateGameObject("g"); g->SetTag("Boss");
        GameObject* h = s.CreateGameObject("h"); h->SetTag("Boss");
        GameObject* i = s.CreateGameObject("i"); i->SetTag("Door");
        CHECK(Tags::Rename("Boss", "Elite") && s.ReplaceTag("Boss", "Elite") == 2);
        CHECK(g->tag == "Elite" && h->tag == "Elite" && i->tag == "Door");
        CHECK((Tags::Custom() == std::vector<std::string>{"Elite", "Pickup", "Door"}));  // it kept its place
        // The layer name box: Layers::SetName / ClearName, the default as the hint.
        CHECK(Layers::SetName(8, "Enemy") && Layers::Name(8) == "Enemy");
        CHECK(!Layers::SetName(9, "enemy"));           // another layer has it
        CHECK(ProjectLists::Write().find("layer_name_8=Enemy\n") != std::string::npos);
        CHECK(Layers::ClearName(8) && Layers::Name(8) == "Layer 8" && std::string(Layers::DefaultName(8)).empty());
        ProjectLists::Reset();
    }

    // ---- The project version gate under Load: a scene of a project nobody has answered for does not open ----------
    {
        namespace fs = std::filesystem;
        const fs::path base = fs::temp_directory_path() / ("okay_hooks_gate_" + std::to_string((long long)std::chrono::steady_clock::now().time_since_epoch().count()));
        std::error_code ec;
        fs::create_directories(base / "Game" / "Assets", ec);
        {
            Scene made("Level");
            made.CreateGameObject("Hero");
            std::ofstream(base / "Game" / "Assets" / "Level.okayscene", std::ios::binary) << SceneSerializer::Serialize(made);
            std::ofstream(base / "Game" / "project.okayproj", std::ios::binary) << "company=Me\nengine_version=2.850.0\n";
        }
        const std::string scenePath = (base / "Game" / "Assets" / "Level.okayscene").string();
        auto projectText = [&] { std::ifstream f(base / "Game" / "project.okayproj", std::ios::binary); return std::string(std::istreambuf_iterator<char>(f), {}); };
        auto has = [](EditorState& e, const char* name) { for (const auto& o : e.scene().Objects()) if (o->name == name) return true; return false; };

        okayup::GateEnv env;
        env.running = "2.855.0";
        okayup::ProjectGate gate(env);
        EditorState::beforeLoad = [&gate](const std::string& p, std::string* why) { return gate.MayLoad(p, why); };
        EditorState ed;
        ed.scene().CreateGameObject("Mine");
        std::string err;
        CHECK(!ed.Load(scenePath, &err) && err.find("v2.850.0") != std::string::npos && err.find("upgrade") != std::string::npos);
        CHECK(has(ed, "Mine") && !has(ed, "Hero") && ed.projectDir().empty() && ed.path().empty());   // refused before anything changed
        CHECK(projectText() == "company=Me\nengine_version=2.850.0\n");                              // and nothing written

        // asking first: Cancel keeps the refusal, Upgrade then Continue opens the project (the editor's own code path)
        int opened = 0, cancelled = 0;
        auto open = [&] { std::string e; if (ed.Load(scenePath, &e)) ++opened; };
        gate.Open(scenePath, open, [&] { ++cancelled; });
        CHECK(gate.prompt().kind == okayup::PromptKind::Older && opened == 0 && projectText() == "company=Me\nengine_version=2.850.0\n");
        gate.Answer(okayup::Choice::Cancel);
        CHECK(cancelled == 1 && opened == 0 && !ed.Load(scenePath, &err) && has(ed, "Mine"));
        gate.Open(scenePath, open, [&] { ++cancelled; });
        gate.Answer(okayup::Choice::Upgrade);
        for (int i = 0; i < 100000 && gate.prompt().kind == okayup::PromptKind::Working; ++i) gate.Tick(std::chrono::milliseconds(0));
        CHECK(gate.prompt().kind == okayup::PromptKind::Done && opened == 0);
        gate.Answer(okayup::Choice::Continue);
        CHECK(opened == 1 && has(ed, "Hero") && !has(ed, "Mine") && ed.path() == scenePath && ed.projectDir() == (base / "Game").string());
        CHECK(projectText().find("engine_version=2.855.0") != std::string::npos);                    // stamped by the upgrade
        CHECK(ed.Load(scenePath, &err));                                                             // answered: opens like any scene
        // a scene outside any project is nobody's business, and a same-version project needs no answer
        fs::create_directories(base / "loose", ec);
        std::ofstream(base / "loose" / "x.okayscene", std::ios::binary) << SceneSerializer::Serialize(Scene("x"));
        CHECK(ed.Load((base / "loose" / "x.okayscene").string(), &err));
        EditorState::beforeLoad = nullptr;
        fs::remove_all(base, ec);
    }

    TEST_MAIN_RESULT();
}
