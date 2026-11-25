#include "okay/Input/Gestures.hpp"
#include "okay/Input/GestureRecognizer.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Input/ActionSet.hpp"
#include "okay/Input/InputRuntime.hpp"
#include <cmath>
#include <unordered_set>
#include <vector>

namespace okay {
namespace {

std::unordered_set<long long>& Claims() {
    static std::unordered_set<long long> ids;
    return ids;
}

struct Frame {
    GestureRecognizer recognizer;
    std::vector<GestureEvent> events;
    float pinchDelta = 0.0f;
    float twistDelta = 0.0f;   // radians
    std::string swipe;
    Vec2 swipeVector;
    std::vector<std::pair<int, std::string>> binds;   // gesture index (kNames), action; one gesture may drive several actions
    bool applied = true;                              // this tick's gestures already reached the action table (ApplyBound)
};
Frame& State() {
    static Frame f;
    return f;
}

std::string Fold(const std::string& name) {
    std::string k;
    for (char c : name) {
        if (c == ' ' || c == '_' || c == '-') continue;
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        k += c;
    }
    if (k == "twist" || k == "twofingerrotate") return "rotate";
    return k;
}

// Every gesture name, in the order Gestures::NameIndex numbers them (a "gesture:" binding keeps the index while it runs
// and is saved by its key, so the order may grow but not change within a run).
enum GestureName { GTap, GDoubleTap, GLongPress, GSwipe, GSwipeUp, GSwipeDown, GSwipeLeft, GSwipeRight, GPinch, GRotate, GNameCount };
struct NameEntry { const char* key; const char* label; };
const NameEntry kNames[GNameCount] = {
    {"tap", "Tap"}, {"doubletap", "Double Tap"}, {"longpress", "Long Press"}, {"swipe", "Swipe"},
    {"swipeup", "Swipe Up"}, {"swipedown", "Swipe Down"}, {"swipeleft", "Swipe Left"}, {"swiperight", "Swipe Right"},
    {"pinch", "Pinch"}, {"rotate", "Rotate"}};

bool Matches(const GestureEvent& e, int name) {
    switch (e.kind) {
        case GestureKind::Tap:       return name == GTap;
        case GestureKind::DoubleTap: return name == GDoubleTap;
        case GestureKind::LongPress: return name == GLongPress;
        case GestureKind::Pinch:     return name == GPinch;
        case GestureKind::Rotate:    return name == GRotate;
        case GestureKind::Swipe:
            if (name == GSwipe) return true;
            if (name == GSwipeUp) return e.directionName == "up";
            if (name == GSwipeDown) return e.directionName == "down";
            if (name == GSwipeLeft) return e.directionName == "left";
            if (name == GSwipeRight) return e.directionName == "right";
            return false;
    }
    return false;
}

} // namespace

void TouchClaims::Clear() { Claims().clear(); }
void TouchClaims::Claim(long long fingerId) { Claims().insert(fingerId); }
bool TouchClaims::Claimed(long long fingerId) { return Claims().count(fingerId) != 0; }

void Gestures::Reset() {
    Frame& f = State();
    f.recognizer.Reset();
    f.events.clear();
    f.pinchDelta = 0.0f;
    f.twistDelta = 0.0f;
    f.swipe.clear();
    f.swipeVector = {};
    f.applied = true;
}

void Gestures::Tick(double timeSeconds) {
    Frame& f = State();
    f.applied = false;   // this tick's gestures have not driven the bound actions yet
    f.events.clear();
    f.pinchDelta = 0.0f;
    f.twistDelta = 0.0f;
    f.swipe.clear();
    f.swipeVector = {};

    std::vector<long long> live;
    live.reserve(static_cast<std::size_t>(Input::TouchCount()));
    for (int i = 0; i < Input::TouchCount(); ++i) {
        const Input::Touch& t = Input::GetTouch(i);
        live.push_back(t.id);
        if (TouchClaims::Claimed(t.id)) {
            if (f.recognizer.Tracking(t.id)) f.recognizer.Cancel(t.id);
            continue;
        }
        int phase = -1;
        if (t.phase == Input::Touch::Phase::Began) phase = 0;
        else if (t.phase == Input::Touch::Phase::Moved) phase = 1;
        else if (t.phase == Input::Touch::Phase::Ended) phase = 2;
        if (phase < 0) continue;                 // still, with no new sample
        if (phase == 0 && f.recognizer.Tracking(t.id)) continue;
        TouchSample s;
        s.id = t.id;
        s.phase = phase;
        s.x = t.position.x;
        s.y = t.position.y;
        s.time = timeSeconds;
        f.recognizer.Feed(s, f.events);
    }
    f.recognizer.DropExcept(live);
    f.recognizer.Advance(timeSeconds, f.events);
    for (const GestureEvent& e : f.events) {
        if (e.kind == GestureKind::Pinch) f.pinchDelta += e.delta;
        if (e.kind == GestureKind::Rotate) f.twistDelta += e.delta;
        if (e.kind == GestureKind::Swipe) { f.swipe = e.directionName; f.swipeVector = e.direction; }
    }
}

int Gestures::NameIndex(const std::string& name) {
    const std::string key = Fold(name);
    for (int i = 0; i < GNameCount; ++i) if (key == kNames[i].key) return i;
    return -1;
}
const char* Gestures::NameKey(int index) { return index >= 0 && index < GNameCount ? kNames[index].key : ""; }
const char* Gestures::NameLabel(int index) { return index >= 0 && index < GNameCount ? kNames[index].label : "None"; }

bool Gestures::Happened(int index) {
    if (index < 0 || index >= GNameCount) return false;
    for (const GestureEvent& e : State().events) if (Matches(e, index)) return true;
    return false;
}
bool Gestures::Happened(const std::string& name) { return Happened(NameIndex(name)); }
std::string Gestures::SwipeDirection() { return State().swipe; }
Vec2 Gestures::SwipeVector() { return State().swipeVector; }
float Gestures::PinchScale() { return State().recognizer.PinchScale(); }
float Gestures::PinchDelta() { return State().pinchDelta; }
float Gestures::TwistDegrees() { return State().recognizer.TwistRadians() * (180.0f / 3.14159265358979323846f); }
float Gestures::TwistDeltaDegrees() { return State().twistDelta * (180.0f / 3.14159265358979323846f); }

bool Gestures::Bind(const std::string& gesture, const std::string& action) {
    const int g = NameIndex(gesture);
    if (g < 0 || action.empty()) return false;
    for (const auto& b : State().binds)
        if (b.first == g && b.second == action) return true;   // already bound: nothing to add
    State().binds.emplace_back(g, action);
    return true;
}
void Gestures::Unbind(const std::string& gesture, const std::string& action) {
    const int g = NameIndex(gesture);
    auto& binds = State().binds;
    for (std::size_t i = binds.size(); i-- > 0;)
        if (binds[i].first == g && (action.empty() || binds[i].second == action)) binds.erase(binds.begin() + (std::ptrdiff_t)i);
}
void Gestures::ClearBinds() { State().binds.clear(); }
void Gestures::Apply(ActionSet& set) {
    for (const auto& b : State().binds)
        if (Happened(b.first)) set.Contribute(b.second, 1.0f);
}
void Gestures::ApplyBound() {
    Frame& f = State();
    if (f.applied) return;
    f.applied = true;
    if (f.binds.empty() || f.events.empty()) return;
    InputRuntime::Actions();   // the table exists before ForEachSet walks it
    InputRuntime::ForEachSet([](ActionSet& set, const void*) { Gestures::Apply(set); }, nullptr);
}

} // namespace okay
