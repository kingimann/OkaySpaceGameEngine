// The editor's hooks into the core systems (ImGui-free, so it runs headless): Play runs on GameLoop, so
// Time, the time scale, hit stops and the Clock channels work in Play; the editor's Pause and Step are a
// Game pause and a debug step; Play and Stop start a clean session; DuplicateSelected keeps the parent;
// and an old project file's tag lines load unchanged into the tag list. Fake frame times only: no sleeping.
#include "test_framework.hpp"
#include "EditorState.hpp"
#include "okay/Core/Clock.hpp"
#include "okay/Core/FrameOverlay.hpp"

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

    // ---- Slow motion: the toolbar's Slower / Faster / 1x buttons ---------------------------------------------
    {
        Fresh();
        EditorState ed;
        Probe* p = AddProbe(ed);
        CHECK(ed.debugTimeScale() == 1.0f);
        CHECK(ed.StepDebugTimeScale(-1) == 1.0f && ed.debugTimeScale() == 1.0f);   // greyed out in edit mode: nothing happens
        ed.Play();
        CHECK(ed.StepDebugTimeScale(-1) == 0.5f);
        CHECK(ed.StepDebugTimeScale(-1) == 0.25f);
        ed.PlayFrame(0.016, false);
        CHECK_NEAR(p->lastDt, 0.016 * 0.25, 1e-7);                                  // the whole game runs at a quarter
        CHECK(ed.StepDebugTimeScale(+1) == 0.5f && ed.StepDebugTimeScale(+1) == 1.0f && ed.StepDebugTimeScale(+1) == 2.0f);
        ed.PlayFrame(0.016, false);
        CHECK_NEAR(p->lastDt, 0.032, 1e-7);
        for (int i = 0; i < 20; ++i) ed.StepDebugTimeScale(+1);
        CHECK(ed.debugTimeScale() == 32.0f);                                        // the last preset is the end
        for (int i = 0; i < 20; ++i) ed.StepDebugTimeScale(-1);
        CHECK(ed.debugTimeScale() == 0.05f);
        ed.ResetDebugTimeScale();
        CHECK(ed.debugTimeScale() == 1.0f);
        ed.PlayFrame(0.016, false);
        CHECK_NEAR(p->lastDt, 0.016, 1e-7);
        // Time.timeScale (the game's own) is a separate thing and is left alone.
        ed.StepDebugTimeScale(-1);
        CHECK(Time::BaseTimeScale() == 1.0f);
        // Stop and Play both put the scale back: a new Play never starts in slow motion.
        ed.Stop();
        CHECK(ed.debugTimeScale() == 1.0f);
        ed.Play();
        ed.StepDebugTimeScale(+1);
        CHECK(ed.debugTimeScale() == 2.0f);
        ed.Stop();
        ed.Play();
        CHECK(ed.debugTimeScale() == 1.0f);
        ed.Stop();
    }

    // ---- Run N frames, then pause ---------------------------------------------------------------------------
    {
        Fresh();
        EditorState ed;
        Probe* p = AddProbe(ed);
        int done = 0;
        bool toolbarPaused = false;                    // main.cpp's g_paused
        ed.onRunFramesDone = [&] { ++done; toolbarPaused = true; };
        CHECK(!ed.RunFrames(5));                       // not playing
        ed.Play();
        CHECK(!ed.RunFrames(0) && !ed.RunFrames(-3));
        ed.PlayFrame(0.016, false);
        CHECK(p->updates == 1 && ed.PendingFrames() == 0);
        CHECK(ed.RunFrames(3, 1.0f / 30.0f));
        CHECK(ed.PendingFrames() == 3);                // the Pause button shows "3"
        ed.PlayFrame(0.5, false);                      // the real time is ignored: each run frame is exactly 1/30 s
        CHECK(p->updates == 2 && ed.PendingFrames() == 2);
        CHECK_NEAR(p->lastDt, 1.0 / 30.0, 1e-7);
        ed.PlayFrame(0.001, false);
        CHECK(done == 0 && !ed.paused());
        ed.PlayFrame(0.001, false);                    // the third and last frame of the run
        CHECK(p->updates == 4);                        // frames 2, 3 and 4: three of them
        CHECK(done == 1 && toolbarPaused);             // the toolbar flag follows
        CHECK(ed.paused() && Game::IsPausedBy(EditorState::kPauseReason));   // the editor's own pause, not "manual"
        CHECK(ed.PendingFrames() == 0);
        ed.PlayFrame(0.016, false);
        CHECK(p->updates == 4);                        // frozen
        // The Resume button (SetPaused(false)) releases it: the reason is the editor's.
        ed.SetPaused(false);
        CHECK(!ed.paused() && !Game::Paused());
        ed.PlayFrame(0.016, false);
        CHECK(p->updates == 5);
        CHECK(done == 1);

        // Already paused: the same call steps the frames and stays paused (the editor lifts its pause around them).
        ed.SetPaused(true);
        CHECK(ed.RunFrames(2, 1.0f / 60.0f));
        CHECK(ed.PendingFrames() == 2);
        ed.PlayFrame(0.016, false);
        ed.PlayFrame(0.016, false);
        ed.PlayFrame(0.016, false);
        CHECK(p->updates == 7 && !p->pausedSeen);
        CHECK(ed.paused() && ed.PendingFrames() == 0);
        // Stop drops a run that is still going.
        ed.SetPaused(false);
        CHECK(ed.RunFrames(100));
        CHECK(ed.PendingFrames() == 100);
        ed.PlayFrame(0.016, false);
        ed.Stop();
        CHECK(ed.PendingFrames() == 0 && !ed.paused());
        ed.Play();
        ed.PlayFrame(0.016, false);
        CHECK(ed.PendingFrames() == 0);
        ed.Stop();
    }

    // ---- Hierarchy "Delete Children": empties a container, keeps the object, one undo step -------------------------
    {
        Fresh();
        EditorState ed;
        GameObject* box = ed.CreateEmpty("Box");
        GameObject* a = ed.CreateEmpty("a");
        GameObject* b = ed.CreateEmpty("b");
        GameObject* deep = ed.CreateEmpty("deep");
        a->transform->SetParent(box->transform, false);
        b->transform->SetParent(box->transform, false);
        deep->transform->SetParent(a->transform, false);
        GameObject* other = ed.CreateEmpty("other");
        const int before = (int)ed.scene().Objects().size();
        ed.Select(other);
        const int undoBefore = ed.UndoDepth();
        CHECK(ed.DeleteChildrenOfSelected() == 0 && ed.UndoDepth() == undoBefore);   // a leaf: nothing to do, no undo step
        ed.Select(box);
        ed.ToggleSelect(a);                           // a child is selected as well: it must not dangle afterwards
        CHECK(ed.MultiSelection().size() == 2);
        CHECK(ed.DeleteChildrenOfSelected() == 3);    // a and b below Box (2), then deep below a (1): a was already queued, deep is not counted twice
        CHECK(ed.scene().Objects().size() == (std::size_t)before - 3);
        CHECK(ed.scene().Find("Box") == box && box->transform->ChildCount() == 0);
        CHECK(ed.scene().Find("a") == nullptr && ed.scene().Find("b") == nullptr && ed.scene().Find("deep") == nullptr);
        CHECK(ed.selected() == box && ed.MultiSelection().size() == 1);   // the object stays selected, its child left the selection
        CHECK(ed.dirty && ed.UndoDepth() == undoBefore + 1);
        CHECK(ed.Undo());                             // one step brings the whole subtree back
        CHECK(ed.scene().Find("Box") != nullptr && ed.scene().Find("a") != nullptr && ed.scene().Find("deep") != nullptr);
        GameObject* box2 = ed.scene().Find("Box");
        CHECK(box2 && box2->transform->ChildCount() == 2);
    }

    // ---- The Game view's Stats overlay text: the loop's lines (while playing), then the scene's counts --------
    {
        Fresh();
        EditorState ed;
        ed.scene().CreateGameObject("a"); ed.scene().CreateGameObject("b");
        auto edit = ed.StatsLines();
        CHECK(!edit.empty() && edit[0].rfind("Objects: 2", 0) == 0);              // edit mode: no frame lines, only counts
        for (const std::string& l : edit) CHECK(l.find(" fps") == std::string::npos);
        ed.Play();
        for (int i = 0; i < 10; ++i) ed.PlayFrame(0.016, false);
        const auto play = ed.StatsLines();
        CHECK(play.size() > edit.size());
        CHECK(play[0].find(" fps") != std::string::npos);                         // FrameOverlay's first line
        bool hasObjects = false, hasTime = false;
        for (const std::string& l : play) { hasObjects |= l.rfind("Objects: ", 0) == 0; hasTime |= l.rfind("time x", 0) == 0; }
        CHECK(hasObjects && hasTime);
        CHECK(FrameOverlay::Lines(ed.playLoop()) == std::vector<std::string>(play.begin(), play.begin() + (long)FrameOverlay::Lines(ed.playLoop()).size()));
        // The graph: a fixed number of points, newest last, 0 padded in front.
        const auto g = ed.RecentFps(90);
        CHECK(g.size() == 90 && g.front() == 0.0f && g.back() > 50.0f && g.back() < 70.0f);   // 16 ms frames = 62.5 fps
        ed.PlayFrame(0.033, false);
        CHECK_NEAR(ed.RecentFps(90).back(), 1.0 / 0.033, 0.05);
        // The slow-motion factor and a pause show in the text (the loop's time line).
        ed.StepDebugTimeScale(-1);
        ed.SetPaused(true);
        ed.PlayFrame(0.016, false);
        bool slow = false, paused = false;
        for (const std::string& l : ed.StatsLines()) { slow |= l.find("debug x0.5") != std::string::npos; paused |= l.rfind("paused by", 0) == 0; }
        CHECK(slow && paused);
        ed.Stop();
    }

    // ---- The Timers overlay text: the scene's scheduler and tweens, shortened to fit ---------------------------
    {
        Fresh();
        EditorState ed;
        auto lines = ed.TimerLines();
        CHECK((lines == std::vector<std::string>{"Scheduler: 0 tasks", "Tweens: 0"}));
        int fired = 0;
        ed.scene().scheduler().Invoke(5.0f, [&] { ++fired; });
        ed.scene().scheduler().InvokeRepeating(0.5f, [&] { ++fired; });
        lines = ed.TimerLines();
        CHECK(lines.size() >= 3 && lines[0].rfind("Scheduler: 2 tasks", 0) == 0);
        CHECK(lines[1].find("next due in 0.5s") != std::string::npos);
        for (const std::string& l : lines) CHECK(l.size() <= 78);
        const auto cut = ed.TimerLines(20);
        for (const std::string& l : cut) CHECK(l.size() <= 20);
        CHECK(cut[0].size() == 20 && cut[0].compare(17, 3, "...") == 0);
        // The process-wide scheduler is listed too, once it holds a timer (and not before).
        for (const std::string& l : lines) CHECK(l.rfind("Global", 0) != 0);
        const auto h = Scheduler::Global().Invoke(100.0f, [] {});
        lines = ed.TimerLines();
        bool global = false;
        for (const std::string& l : lines) global |= l.rfind("Global Scheduler: 1 task", 0) == 0;
        CHECK(global);
        Scheduler::Global().Cancel(h);
        CHECK(ed.TimerLines().size() == 4);                                        // back to the scene's lines only
        for (const std::string& l : ed.TimerLines()) CHECK(l.rfind("Global", 0) != 0);
        ed.scene().scheduler().Clear();
        CHECK((ed.TimerLines() == std::vector<std::string>{"Scheduler: 0 tasks", "Tweens: 0"}));
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

    TEST_MAIN_RESULT();
}
