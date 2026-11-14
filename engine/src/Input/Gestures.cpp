#include "okay/Input/Gestures.hpp"
#include "okay/Input/GestureRecognizer.hpp"
#include "okay/Input/Input.hpp"
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
    if (k == "doubletap") return "doubletap";
    if (k == "longpress") return "longpress";
    if (k == "twist" || k == "twofingerrotate" || k == "rotate") return "rotate";
    return k;
}

bool Matches(const GestureEvent& e, const std::string& name) {
    switch (e.kind) {
        case GestureKind::Tap:       return name == "tap";
        case GestureKind::DoubleTap: return name == "doubletap";
        case GestureKind::LongPress: return name == "longpress";
        case GestureKind::Pinch:     return name == "pinch";
        case GestureKind::Rotate:    return name == "rotate";
        case GestureKind::Swipe:
            if (name == "swipe") return true;
            if (name == "swipeup") return e.directionName == "up";
            if (name == "swipedown") return e.directionName == "down";
            if (name == "swipeleft") return e.directionName == "left";
            if (name == "swiperight") return e.directionName == "right";
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
}

void Gestures::Tick(double timeSeconds) {
    Frame& f = State();
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

bool Gestures::Happened(const std::string& name) {
    const std::string key = Fold(name);
    for (const GestureEvent& e : State().events) if (Matches(e, key)) return true;
    return false;
}
std::string Gestures::SwipeDirection() { return State().swipe; }
Vec2 Gestures::SwipeVector() { return State().swipeVector; }
float Gestures::PinchScale() { return State().recognizer.PinchScale(); }
float Gestures::PinchDelta() { return State().pinchDelta; }
float Gestures::TwistDegrees() { return State().recognizer.TwistRadians() * (180.0f / 3.14159265358979323846f); }
float Gestures::TwistDeltaDegrees() { return State().twistDelta * (180.0f / 3.14159265358979323846f); }

} // namespace okay
