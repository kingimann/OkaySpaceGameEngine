#pragma once
// ---------------------------------------------------------------------------
// PlayTest: run a scene headless, frame by frame, and assert on what happened.
//
//   PlayTest t;                                   // fixed 1/60 s frames, deterministic
//   t.LoadSceneFile("scenes/level1.okayscene");   // or LoadSceneText(...), or build t.scene() by hand
//   t.KeyDown('d');                               // hold D for the rest of the test
//   t.RunFrames(30);
//   OKAY_EXPECT(t.FindObject("Player")->transform->Position().x > 1.0f);
//   t.RunUntil([&] { return t.FindObject("Door")->active == false; }, 5.0f);
//
// A PlayTest drives the scene the way the player does each frame: it feeds the
// injected input to Input, ticks InputActions, steps Time by the fixed frame time
// (scaled by the time scale) and calls Scene::Update. Nothing depends on the wall
// clock and the shared random generator is reseeded, so a run is repeatable. The log is captured: every Log line is kept, and an
// error nobody declared with ExpectError fails the test when it finishes.
//
// Time and Input are process-wide, so keep ONE PlayTest alive at a time; the
// constructor and destructor put both back to a neutral state.
// ---------------------------------------------------------------------------
#include "okay/Core/EventBus.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace okay::testing {

struct PlayTestOptions {
    /// Simulated seconds per frame (before the time scale). 1/60 by default.
    float dt = 1.0f / 60.0f;
    /// Starting value of the global time scale (see PlayTest::SetTimeScale).
    float timeScale = 1.0f;
    /// false: one physics step per frame, sized to the frame (easiest to reason about).
    /// true: the player's setup, fixed 1/50 s physics steps with interpolation.
    bool fixedStepPhysics = false;
    /// Silence the console while the test runs (lines are still captured).
    bool muteConsole = true;
    /// Count warning lines like error lines (they fail the test unless expected).
    bool treatWarningsAsErrors = false;
    /// The shared random generator (Random::Shared) is reseeded with this when the
    /// PlayTest is created, so "random" gameplay repeats from run to run.
    std::uint64_t randomSeed = 0xC0FFEEull;
};

/// One captured log line.
struct LogLine {
    Log::Level level = Log::Level::Info;
    std::string text;
    int frame = 0;      // the frame being stepped when it was logged
};

class PlayTest {
public:
    explicit PlayTest(PlayTestOptions options = {});
    /// Restores Log, Time and Input. Reports log problems to the running test (see Finish).
    ~PlayTest();
    PlayTest(const PlayTest&) = delete;
    PlayTest& operator=(const PlayTest&) = delete;

    // ---- The scene ---------------------------------------------------------
    /// The scene under test. Starts empty; fill it by hand or load one.
    Scene& scene();
    const Scene& scene() const;
    /// Replace the scene with one loaded from a file / from serialized text. On a bad
    /// document returns false with LastError() set and the previous scene untouched.
    bool LoadSceneFile(const std::string& path);
    bool LoadSceneText(const std::string& text);
    /// Use a scene built elsewhere (it is owned from now on).
    void AdoptScene(std::unique_ptr<Scene> scene);
    /// Throw the scene away for an empty one.
    void NewScene(const std::string& name = "PlayTest");
    const std::string& LastError() const;
    /// Notes the loader made about records it skipped.
    const std::vector<std::string>& LoadWarnings() const;

    /// Run Awake/Start on everything created so far. Step() does it on first use.
    void Start();
    bool Started() const;

    // ---- Stepping ----------------------------------------------------------
    /// Advance one frame.
    void Step();
    /// Advance `frames` frames; stops early if the game asked to quit. Returns the
    /// number of frames stepped.
    int RunFrames(int frames);
    /// Advance about `seconds` of frame time (rounded to whole frames).
    int RunSeconds(float seconds);
    /// Step until `pred()` is true, checking it before the first frame and after each
    /// one, for at most `maxSeconds` of frame time (unscaled). True when it came true.
    bool RunUntil(const std::function<bool()>& pred, float maxSeconds);

    int Frame() const;                 ///< frames stepped so far
    double SimulatedSeconds() const;   ///< Frame() * FrameTime() (the unscaled frame clock)
    float FrameTime() const;           ///< the fixed dt per frame
    bool QuitRequested() const;        ///< a script or menu called Game::RequestQuit

    // ---- Time --------------------------------------------------------------
    /// Scale game time (0 pauses movement and physics, 0.5 is half speed, 4 fast-forwards).
    /// Frames stay the same length, so RunUntil's maxSeconds is not scaled.
    void SetTimeScale(float scale);
    float TimeScale() const;
    /// Freeze or unfreeze game time the way a pause menu does (Game::SetPaused).
    void SetPaused(bool paused);

    // ---- Finding things ----------------------------------------------------
    /// An object by name, or by path ("Parent/Child"), or nullptr.
    GameObject* FindObject(const std::string& nameOrPath) const;
    /// Every object with this name.
    std::vector<GameObject*> FindObjects(const std::string& name) const;
    GameObject* FindObjectWithTag(const std::string& tag) const;
    /// Like FindObject, but a missing object fails the running test and stops it
    /// (outside a test run it throws std::runtime_error).
    GameObject* Require(const std::string& nameOrPath) const;
    /// The first component of type T on the named object, or nullptr.
    template <class T>
    T* FindComponent(const std::string& objectName) const {
        GameObject* go = FindObject(objectName);
        return go ? go->GetComponent<T>() : nullptr;
    }
    /// The first component of type T anywhere in the scene, or nullptr.
    template <class T>
    T* FindComponent() const { return scene().template FindObjectOfType<T>(); }
    /// World position of the named object (Require()s it).
    Vec3 PositionOf(const std::string& objectName) const;

    // ---- Input injection ---------------------------------------------------
    // Everything here is fed to Input at the START of the next Step, the way the
    // player feeds a frame, so GetKeyDown is true during the first frame after
    // KeyDown and GetKeyUp during the first frame after KeyUp.
    /// Hold a key (a char, or a name as in Input::KeyFromName: "space", "shift", "up"...).
    void KeyDown(char key);
    void KeyDown(const std::string& keyName);
    void KeyUp(char key);
    void KeyUp(const std::string& keyName);
    /// Hold a key for exactly one frame.
    void TapKey(char key);
    void TapKey(const std::string& keyName);
    /// Hold WASD so AxisWASD() reads `axis` (each component's sign: -1, 0 or 1).
    void SetMoveKeys(const Vec2& axis);
    /// Controller left / right stick, triggers (side 0 = left, 1 = right) and buttons
    /// (Input::PadButton). Setting any of them marks a controller as connected.
    void SetAxis(const Vec2& leftStick);
    void SetLookAxis(const Vec2& rightStick);
    void SetTrigger(int side, float amount);
    void SetGamepadButton(int button, bool down);
    /// Move the cursor (pixels, origin top left) and press/release mouse buttons
    /// (0 left, 1 right, 2 middle). Click holds the button for one frame.
    void MoveMouse(const Vec2& position);
    void MouseDown(int button);
    void MouseUp(int button);
    void Click(int button = 0);
    void Scroll(float notches);
    /// Characters typed into a text field on the next frame.
    void TypeText(const std::string& utf8);
    /// A finger: state 0 down, 1 moved, 2 up (Input::FeedTouch).
    void Touch(long long id, int state, const Vec2& position);
    /// Release every key, button and stick.
    void ReleaseAll();

    // ---- Log ---------------------------------------------------------------
    const std::vector<LogLine>& LogLines() const;
    /// Lines at `atLeast` or louder whose text contains `substr`.
    int CountLog(const std::string& substr, Log::Level atLeast = Log::Level::Trace) const;
    bool LogContains(const std::string& substr, Log::Level atLeast = Log::Level::Trace) const;
    void ClearLog();
    /// Declare that an error line containing `substr` is expected `times` times
    /// (default once; 0 = any number, not required). Declared errors do not fail the test.
    void ExpectError(const std::string& substr, int times = 1);
    /// What would fail the test now: error lines nobody declared, and declared errors
    /// that never came. Empty when the log is clean.
    std::vector<std::string> Problems() const;
    /// Report Problems() as failures on the running test. True when there were none.
    /// Called by the destructor if you have not.
    bool Finish();

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

/// Records every event of type E published on a bus, for assertions:
///   EventBus bus; EventRecorder<Scored> rec(bus);  ... OKAY_EXPECT_EQ(rec.Count(), 3);
template <class E>
class EventRecorder {
public:
    explicit EventRecorder(EventBus& bus)
        : m_sub(bus.Subscribe<E>([this](const E& e) { events.push_back(e); }, EventBus::Scoped{})) {}
    EventRecorder(const EventRecorder&) = delete;
    EventRecorder& operator=(const EventRecorder&) = delete;
    std::size_t Count() const { return events.size(); }
    bool Any() const { return !events.empty(); }
    void Clear() { events.clear(); }
    std::vector<E> events;
private:
    EventBus::Subscription m_sub;
};

/// A component that records the engine messages its object receives, so a test can
/// assert that a collision, trigger, click or lifecycle step happened. Each entry is
/// "name" or "name:Other" (the other object's name for physics messages).
///   Names: awake start enable disable destroy collision_enter collision_stay
///   collision_exit trigger_enter trigger_stay trigger_exit mouse_enter mouse_exit
///   mouse_down mouse_up mouse_click animation_event
class EventProbe : public Component {
public:
    std::vector<std::string> events;
    int updates = 0;

    /// How many recorded entries begin with `prefix` ("collision" matches all collision kinds).
    int Count(const std::string& prefix) const;
    bool Saw(const std::string& entry) const;
    void Clear() { events.clear(); updates = 0; }

    void Awake() override { events.push_back("awake"); }
    void Start() override { events.push_back("start"); }
    void Update(float) override { ++updates; }
    void OnEnable() override { events.push_back("enable"); }
    void OnDisable() override { events.push_back("disable"); }
    void OnDestroy() override { events.push_back("destroy"); }
    void OnCollisionEnter2D(const Collision2D& c) override;
    void OnCollisionStay2D(const Collision2D& c) override;
    void OnCollisionExit2D(const Collision2D& c) override;
    void OnTriggerEnter2D(Collider2D* o) override;
    void OnTriggerStay2D(Collider2D* o) override;
    void OnTriggerExit2D(Collider2D* o) override;
    void OnCollisionEnter3D(const Collision3D& c) override;
    void OnCollisionStay3D(const Collision3D& c) override;
    void OnCollisionExit3D(const Collision3D& c) override;
    void OnTriggerEnter3D(Collider3D* o) override;
    void OnTriggerStay3D(Collider3D* o) override;
    void OnTriggerExit3D(Collider3D* o) override;
    void OnMouseEnter() override { events.push_back("mouse_enter"); }
    void OnMouseExit() override { events.push_back("mouse_exit"); }
    void OnMouseDown() override { events.push_back("mouse_down"); }
    void OnMouseUp() override { events.push_back("mouse_up"); }
    void OnMouseClick() override { events.push_back("mouse_click"); }
    void OnAnimationEvent(const std::string& name) override { events.push_back("animation_event:" + name); }
};

} // namespace okay::testing
