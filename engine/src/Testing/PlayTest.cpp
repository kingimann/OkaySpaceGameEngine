#include "okay/Testing/PlayTest.hpp"

#include "okay/Core/Game.hpp"
#include "okay/Core/Random.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Input/InputActions.hpp"
#include "okay/Physics/Collider2D.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Physics2D.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Testing/ObjectPath.hpp"
#include "okay/Testing/TestRegistry.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <stdexcept>

namespace okay::testing {

namespace {

// A tiny spin lock (std::atomic_flag only): the log sink can be called from any
// thread, and <mutex> is not usable with MinGW's win32 thread model.
class SpinLock {
public:
    void lock() { while (m_flag.test_and_set(std::memory_order_acquire)) {} }
    void unlock() { m_flag.clear(std::memory_order_release); }
private:
    std::atomic_flag m_flag = ATOMIC_FLAG_INIT;
};
struct SpinGuard {
    explicit SpinGuard(SpinLock& l) : lock(l) { lock.lock(); }
    ~SpinGuard() { lock.unlock(); }
    SpinLock& lock;
};

struct Expectation {
    std::string substr;
    int times = 1;
};

struct TouchEvent {
    long long id = 0;
    int state = 0;
    Vec2 position;
};

void AddUnique(std::vector<char>& v, char c) {
    if (std::find(v.begin(), v.end(), c) == v.end()) v.push_back(c);
}
void Remove(std::vector<char>& v, char c) {
    v.erase(std::remove(v.begin(), v.end(), c), v.end());
}
char Lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

// Put every input device back to "nothing happening". Fed twice so the edge state
// (down / up since last frame) settles too.
void NeutralInput() {
    for (int i = 0; i < 2; ++i) {
        Input::ClearTypedText();
        Input::FeedKeys({});
        Input::FeedMouse(Vec2{0, 0}, 0u);
        Input::FeedGamepad(Vec2{0, 0}, 0u);
        Input::FeedGamepadExtra(Vec2{0, 0}, 0.0f, 0.0f, false);
    }
    Input::SetUICaptured(false);
}

} // namespace

struct PlayTest::Impl {
    PlayTestOptions opt;
    std::unique_ptr<Scene> scene;
    bool started = false;
    bool finished = false;
    std::string lastError;
    std::vector<std::string> warnings;
    int frame = 0;
    double simSeconds = 0.0;

    // injected input
    std::vector<char> held, tapped;
    Vec2 mousePos{0, 0};
    unsigned mouseHeld = 0, mouseTapped = 0;
    Vec2 padAxis{0, 0}, padRight{0, 0};
    float trigger[2] = {0.0f, 0.0f};
    bool padConnected = false;
    unsigned padHeld = 0;
    float wheel = 0.0f;
    std::string typed;
    std::vector<TouchEvent> touches;

    // log capture
    SpinLock lock;
    std::vector<LogLine> lines;
    std::vector<Expectation> expects;
    Log::Sink prevSink;
    bool prevConsole = true;

    void ApplySceneOptions() {
        if (opt.fixedStepPhysics) scene->UseFixedStepPhysics(true);
    }
    void Record(Log::Level level, const std::string& text) {
        SpinGuard g(lock);
        lines.push_back({level, text, frame});
    }
};

PlayTest::PlayTest(PlayTestOptions options) : m(new Impl) {
    m->opt = options;
    if (!(m->opt.dt > 0.0f)) m->opt.dt = 1.0f / 60.0f;
    Game::Reset();
    Time::ResetForTesting();
    Time::SetTimeScale(m->opt.timeScale);
    Random::Shared().Seed(m->opt.randomSeed);
    NeutralInput();

    m->prevSink = Log::sink;
    m->prevConsole = Log::console;
    if (m->opt.muteConsole) Log::console = false;
    Impl* impl = m.get();
    Log::sink = [impl](Log::Level level, const std::string& text) {
        impl->Record(level, text);
        if (impl->prevSink) impl->prevSink(level, text);
    };

    m->scene.reset(new Scene("PlayTest"));
    m->ApplySceneOptions();
}

PlayTest::~PlayTest() {
    m->scene.reset();       // tearing the scene down can log too, and that counts
    if (!m->finished) Finish();
    Log::sink = m->prevSink;
    Log::console = m->prevConsole;
    Game::Reset();
    Time::ResetForTesting();
    NeutralInput();
}

// ---- Scene -----------------------------------------------------------------

Scene& PlayTest::scene() { return *m->scene; }
const Scene& PlayTest::scene() const { return *m->scene; }

bool PlayTest::LoadSceneFile(const std::string& path) {
    m->lastError.clear();
    m->warnings.clear();
    if (!SceneSerializer::LoadFromFile(*m->scene, path, &m->lastError, &m->warnings)) return false;
    m->started = false;
    m->ApplySceneOptions();
    return true;
}

bool PlayTest::LoadSceneText(const std::string& text) {
    m->lastError.clear();
    m->warnings.clear();
    if (!SceneSerializer::Deserialize(*m->scene, text, &m->lastError, &m->warnings)) return false;
    m->started = false;
    m->ApplySceneOptions();
    return true;
}

void PlayTest::AdoptScene(std::unique_ptr<Scene> scene) {
    if (!scene) return;
    m->scene = std::move(scene);
    m->started = false;
    m->ApplySceneOptions();
}

void PlayTest::NewScene(const std::string& name) {
    m->scene.reset(new Scene(name));
    m->started = false;
    m->ApplySceneOptions();
}

const std::string& PlayTest::LastError() const { return m->lastError; }
const std::vector<std::string>& PlayTest::LoadWarnings() const { return m->warnings; }

void PlayTest::Start() {
    if (m->started) return;
    m->started = true;
    m->scene->Start();
}

bool PlayTest::Started() const { return m->started; }

// ---- Stepping --------------------------------------------------------------

void PlayTest::Step() {
    Start();
    Impl& s = *m;

    // The same order the player uses: clear last frame's text, feed every device,
    // tick the action map, then advance time and the scene.
    Input::ClearTypedText();
    std::vector<char> down = s.held;
    for (char k : s.tapped) AddUnique(down, k);
    Input::FeedKeys(down);
    Input::FeedMouse(s.mousePos, s.mouseHeld | s.mouseTapped);
    Input::FeedGamepad(s.padAxis, s.padHeld);
    Input::FeedGamepadExtra(s.padRight, s.trigger[0], s.trigger[1], s.padConnected);
    if (s.wheel != 0.0f) Input::FeedMouseWheel(s.wheel);
    if (!s.typed.empty()) Input::FeedText(s.typed);
    for (const TouchEvent& t : s.touches) Input::FeedTouch(t.id, t.state, t.position);
    InputActions::Tick();
    s.tapped.clear();
    s.mouseTapped = 0;
    s.wheel = 0.0f;
    s.typed.clear();
    s.touches.clear();

    Time::Step(s.opt.dt);
    s.scene->Update(Time::DeltaTime());
    ++s.frame;
    s.simSeconds += (double)s.opt.dt;
}

int PlayTest::RunFrames(int frames) {
    int n = 0;
    for (; n < frames; ++n) {
        if (Game::QuitRequested()) break;
        Step();
    }
    return n;
}

int PlayTest::RunSeconds(float seconds) {
    if (!(seconds > 0.0f)) return 0;
    const double frames = std::round((double)seconds / (double)m->opt.dt);
    return RunFrames(frames > 1.0e8 ? 100000000 : (int)frames);
}

bool PlayTest::RunUntil(const std::function<bool()>& pred, float maxSeconds) {
    if (pred()) return true;
    double want = (double)maxSeconds / (double)m->opt.dt - 1e-6;
    if (std::isnan(want)) want = 0.0;
    if (want > 1.0e8) want = 1.0e8;                     // an absurd limit still ends
    const int limit = want > 0.0 ? (int)std::ceil(want) : 0;
    for (int i = 0; i < limit; ++i) {
        if (Game::QuitRequested()) break;
        Step();
        if (pred()) return true;
    }
    return false;
}

int PlayTest::Frame() const { return m->frame; }
double PlayTest::SimulatedSeconds() const { return m->simSeconds; }
float PlayTest::FrameTime() const { return m->opt.dt; }
bool PlayTest::QuitRequested() const { return Game::QuitRequested(); }

// ---- Time ------------------------------------------------------------------

void PlayTest::SetTimeScale(float scale) { Time::SetTimeScale(scale); }
float PlayTest::TimeScale() const { return Time::TimeScale(); }
void PlayTest::SetPaused(bool paused) { Game::SetPaused(paused); }

// ---- Finding things --------------------------------------------------------

GameObject* PlayTest::FindObject(const std::string& nameOrPath) const {
    if (nameOrPath.find('/') == std::string::npos) return m->scene->Find(nameOrPath);
    for (const auto& go : m->scene->Objects()) {
        if (go->IsDestroyed()) continue;
        const std::string path = ObjectPath(go.get());
        if (path == nameOrPath) return go.get();
        // A path that starts below the roots ("Arm/Hand") also matches.
        if (path.size() > nameOrPath.size() &&
            path.compare(path.size() - nameOrPath.size(), nameOrPath.size(), nameOrPath) == 0 &&
            path[path.size() - nameOrPath.size() - 1] == '/')
            return go.get();
    }
    return nullptr;
}

std::vector<GameObject*> PlayTest::FindObjects(const std::string& name) const {
    std::vector<GameObject*> out;
    for (const auto& go : m->scene->Objects())
        if (go->name == name && !go->IsDestroyed()) out.push_back(go.get());
    return out;
}

GameObject* PlayTest::FindObjectWithTag(const std::string& tag) const { return m->scene->FindWithTag(tag); }

GameObject* PlayTest::Require(const std::string& nameOrPath) const {
    GameObject* go = FindObject(nameOrPath);
    if (go) return go;
    const std::string what = "no object named '" + nameOrPath + "' in the scene";
    if (TestContext* ctx = TestContext::Current()) {
        ctx->Fail(what);
        throw AbortTest{};
    }
    throw std::runtime_error(what);
}

Vec3 PlayTest::PositionOf(const std::string& objectName) const {
    return Require(objectName)->transform->Position();
}

// ---- Input -----------------------------------------------------------------

void PlayTest::KeyDown(char key) { AddUnique(m->held, Lower(key)); }
void PlayTest::KeyDown(const std::string& keyName) { KeyDown(Input::KeyFromName(keyName)); }
void PlayTest::KeyUp(char key) { Remove(m->held, Lower(key)); }
void PlayTest::KeyUp(const std::string& keyName) { KeyUp(Input::KeyFromName(keyName)); }
void PlayTest::TapKey(char key) { AddUnique(m->tapped, Lower(key)); }
void PlayTest::TapKey(const std::string& keyName) { TapKey(Input::KeyFromName(keyName)); }

void PlayTest::SetMoveKeys(const Vec2& axis) {
    auto set = [&](char key, bool on) { if (on) KeyDown(key); else KeyUp(key); };
    set('d', axis.x > 0.0f);
    set('a', axis.x < 0.0f);
    set('w', axis.y > 0.0f);
    set('s', axis.y < 0.0f);
}

void PlayTest::SetAxis(const Vec2& leftStick) { m->padAxis = leftStick; m->padConnected = true; }
void PlayTest::SetLookAxis(const Vec2& rightStick) { m->padRight = rightStick; m->padConnected = true; }
void PlayTest::SetTrigger(int side, float amount) {
    if (side != 0 && side != 1) return;
    m->trigger[side] = amount;
    m->padConnected = true;
}
void PlayTest::SetGamepadButton(int button, bool down) {
    if (button < 0 || button > 31) return;
    if (down) m->padHeld |= (1u << button); else m->padHeld &= ~(1u << button);
    m->padConnected = true;
}

void PlayTest::MoveMouse(const Vec2& position) { m->mousePos = position; }
void PlayTest::MouseDown(int button) { if (button >= 0 && button <= 2) m->mouseHeld |= (1u << button); }
void PlayTest::MouseUp(int button) { if (button >= 0 && button <= 2) m->mouseHeld &= ~(1u << button); }
void PlayTest::Click(int button) { if (button >= 0 && button <= 2) m->mouseTapped |= (1u << button); }
void PlayTest::Scroll(float notches) { m->wheel += notches; }
void PlayTest::TypeText(const std::string& utf8) { m->typed += utf8; }
void PlayTest::Touch(long long id, int state, const Vec2& position) { m->touches.push_back({id, state, position}); }

void PlayTest::ReleaseAll() {
    m->held.clear();
    m->tapped.clear();
    m->mouseHeld = m->mouseTapped = 0;
    m->padAxis = m->padRight = Vec2{0, 0};
    m->trigger[0] = m->trigger[1] = 0.0f;
    m->padHeld = 0;
}

// ---- Log -------------------------------------------------------------------

const std::vector<LogLine>& PlayTest::LogLines() const { return m->lines; }

int PlayTest::CountLog(const std::string& substr, Log::Level atLeast) const {
    SpinGuard g(m->lock);
    int n = 0;
    for (const LogLine& l : m->lines)
        if (l.level >= atLeast && l.text.find(substr) != std::string::npos) ++n;
    return n;
}

bool PlayTest::LogContains(const std::string& substr, Log::Level atLeast) const {
    return CountLog(substr, atLeast) > 0;
}

void PlayTest::ClearLog() {
    SpinGuard g(m->lock);
    m->lines.clear();
}

void PlayTest::ExpectError(const std::string& substr, int times) {
    SpinGuard g(m->lock);
    m->expects.push_back({substr, times < 0 ? 0 : times});
}

std::vector<std::string> PlayTest::Problems() const {
    std::vector<std::string> out;
    SpinGuard g(m->lock);
    std::vector<int> used(m->expects.size(), 0);
    const Log::Level bar = m->opt.treatWarningsAsErrors ? Log::Level::Warning : Log::Level::Error;
    for (const LogLine& l : m->lines) {
        if (l.level < bar) continue;
        bool claimed = false;
        for (std::size_t i = 0; i < m->expects.size() && !claimed; ++i) {
            const Expectation& e = m->expects[i];
            if (l.text.find(e.substr) == std::string::npos) continue;
            if (e.times == 0 || used[i] < e.times) { ++used[i]; claimed = true; }
        }
        if (!claimed)
            out.push_back(std::string("unexpected ") + (l.level >= Log::Level::Error ? "error" : "warning") +
                          " logged on frame " + std::to_string(l.frame) + ": " + l.text);
    }
    for (std::size_t i = 0; i < m->expects.size(); ++i)
        if (used[i] < m->expects[i].times)
            out.push_back("expected error \"" + m->expects[i].substr + "\" x" + std::to_string(m->expects[i].times) +
                          " but it was logged " + std::to_string(used[i]) + " time" + (used[i] == 1 ? "" : "s"));
    return out;
}

bool PlayTest::Finish() {
    m->finished = true;
    const std::vector<std::string> problems = Problems();
    if (TestContext* ctx = TestContext::Current())
        for (const std::string& p : problems) ctx->Fail(p);
    return problems.empty();
}

// ---- EventProbe ------------------------------------------------------------

namespace {
std::string Entry(const char* name, const GameObject* other) {
    std::string s = name;
    if (other) { s += ':'; s += other->name; }
    return s;
}
const GameObject* OwnerOf(const Component* c) { return c ? c->gameObject : nullptr; }
} // namespace

int EventProbe::Count(const std::string& prefix) const {
    int n = 0;
    for (const auto& e : events) if (e.compare(0, prefix.size(), prefix) == 0) ++n;
    return n;
}
bool EventProbe::Saw(const std::string& entry) const {
    return std::find(events.begin(), events.end(), entry) != events.end();
}

void EventProbe::OnCollisionEnter2D(const Collision2D& c) { events.push_back(Entry("collision_enter", c.gameObject)); }
void EventProbe::OnCollisionStay2D(const Collision2D& c) { events.push_back(Entry("collision_stay", c.gameObject)); }
void EventProbe::OnCollisionExit2D(const Collision2D& c) { events.push_back(Entry("collision_exit", c.gameObject)); }
void EventProbe::OnTriggerEnter2D(Collider2D* o) { events.push_back(Entry("trigger_enter", OwnerOf(o))); }
void EventProbe::OnTriggerStay2D(Collider2D* o) { events.push_back(Entry("trigger_stay", OwnerOf(o))); }
void EventProbe::OnTriggerExit2D(Collider2D* o) { events.push_back(Entry("trigger_exit", OwnerOf(o))); }
void EventProbe::OnCollisionEnter3D(const Collision3D& c) { events.push_back(Entry("collision_enter", c.gameObject)); }
void EventProbe::OnCollisionStay3D(const Collision3D& c) { events.push_back(Entry("collision_stay", c.gameObject)); }
void EventProbe::OnCollisionExit3D(const Collision3D& c) { events.push_back(Entry("collision_exit", c.gameObject)); }
void EventProbe::OnTriggerEnter3D(Collider3D* o) { events.push_back(Entry("trigger_enter", OwnerOf(o))); }
void EventProbe::OnTriggerStay3D(Collider3D* o) { events.push_back(Entry("trigger_stay", OwnerOf(o))); }
void EventProbe::OnTriggerExit3D(Collider3D* o) { events.push_back(Entry("trigger_exit", OwnerOf(o))); }

} // namespace okay::testing
