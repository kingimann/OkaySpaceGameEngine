#include "okay/Input/Input.hpp"
#include "okay/Math/Mathf.hpp"
#include <cctype>
#include <cmath>

#if defined(__unix__) || defined(__APPLE__)
#  include <termios.h>
#  include <unistd.h>
#  include <fcntl.h>
#endif

namespace okay {

std::unordered_map<char, bool> Input::s_current;
std::unordered_map<char, bool> Input::s_previous;
bool Input::s_interactive = false;

Vec2     Input::s_mousePos;
Vec2     Input::s_mousePrevPos;
bool     Input::s_mouseFed = false;
unsigned Input::s_mouseCurrent = 0;
Input::PadState Input::s_pads[Input::kMaxGamepads];
Input::PadSettings Input::s_padSettings[Input::kMaxGamepads];
unsigned Input::s_mousePrevious = 0;
std::string Input::s_typedText;
float Input::s_mouseWheel = 0.0f;
std::vector<Input::Touch> Input::s_touches;
bool Input::s_uiCaptured = false;
bool Input::s_touchScreen = false;

#if defined(__unix__) || defined(__APPLE__)
namespace {
    termios g_savedTermios;
    int     g_savedFlags = 0;
    bool    g_rawActive  = false;
}
#endif

void Input::BeginSession() {
#if defined(__unix__) || defined(__APPLE__)
    if (!isatty(STDIN_FILENO)) { s_interactive = false; return; }
    if (tcgetattr(STDIN_FILENO, &g_savedTermios) != 0) { s_interactive = false; return; }
    termios raw = g_savedTermios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    g_savedFlags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, g_savedFlags | O_NONBLOCK);
    g_rawActive   = true;
    s_interactive = true;
#else
    s_interactive = false;
#endif
}

void Input::EndSession() {
#if defined(__unix__) || defined(__APPLE__)
    if (g_rawActive) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_savedTermios);
        fcntl(STDIN_FILENO, F_SETFL, g_savedFlags);
        g_rawActive = false;
    }
#endif
    s_current.clear();
    s_previous.clear();
}

void Input::Poll() {
    s_previous = s_current;
    // Keys are momentary: a key counts as "down" only on the frame it arrives.
    for (auto& kv : s_current) kv.second = false;

#if defined(__unix__) || defined(__APPLE__)
    if (!s_interactive) return;
    char buf[32];
    ssize_t n;
    while ((n = read(STDIN_FILENO, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < n; ++i) {
            char c = static_cast<char>(std::tolower(static_cast<unsigned char>(buf[i])));
            s_current[c] = true;
        }
    }
#endif
}

void Input::FeedKeys(const std::vector<char>& downKeys) {
    s_previous = s_current;
    s_current.clear();
    for (char k : downKeys)
        s_current[static_cast<char>(std::tolower(static_cast<unsigned char>(k)))] = true;
    // The generic modifier reads true while either side is held.
    auto held = [](char c) { auto it = s_current.find(c); return it != s_current.end() && it->second; };
    if (held(KeyLShift) || held(KeyRShift)) s_current[KeyShift] = true;
    if (held(KeyLCtrl) || held(KeyRCtrl))   s_current[KeyCtrl] = true;
    if (held(KeyLAlt) || held(KeyRAlt))     s_current[KeyAlt] = true;
}

void Input::FeedMouse(const Vec2& position, unsigned buttonMask) {
    // The first sample is a position, not a movement (else mouse-look jumps by the
    // cursor's absolute position on frame one).
    s_mousePrevPos = s_mouseFed ? s_mousePos : position;
    s_mouseFed = true;
    s_mousePos = position;
    s_mousePrevious = s_mouseCurrent;
    s_mouseCurrent = buttonMask;
}

Vec2 Input::MousePosition() { return s_mousePos; }
Vec2 Input::MouseDelta() { return {s_mousePos.x - s_mousePrevPos.x, s_mousePos.y - s_mousePrevPos.y}; }

bool Input::GetMouseButton(int button) {
    if (button < 0 || button > 2) return false;
    return (s_mouseCurrent & (1u << button)) != 0;
}
bool Input::GetMouseButtonDown(int button) {
    if (button < 0 || button > 2) return false;
    unsigned bit = 1u << button;
    return (s_mouseCurrent & bit) && !(s_mousePrevious & bit);
}
bool Input::GetMouseButtonUp(int button) {
    if (button < 0 || button > 2) return false;
    unsigned bit = 1u << button;
    return !(s_mouseCurrent & bit) && (s_mousePrevious & bit);
}

namespace {
int PadIndex(int player) { return player >= 0 && player < Input::kMaxGamepads ? player : -1; }
float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
float Curve(float t, float curve) { return curve > 0.0f && curve != 1.0f ? std::pow(t, curve) : t; }
// Dead zone and outer zone as a 0..1 ramp for a magnitude.
float Ramp(float mag, float dead, float outer) {
    if (dead < 0.0f) dead = 0.0f;
    if (dead > 0.95f) dead = 0.95f;
    if (outer > 1.0f) outer = 1.0f;
    if (outer < dead + 0.01f) outer = dead + 0.01f;
    if (mag <= dead) return 0.0f;
    return Clamp01((mag - dead) / (outer - dead));
}
} // namespace

Vec2 Input::ProcessStick(const Vec2& raw, const StickSettings& s) {
    if (s.shape == StickSettings::Shape::Axial) {
        auto axis = [&](float v) {
            const float t = Curve(Ramp(std::fabs(v), s.deadZone, s.outerZone), s.curve);
            return v < 0.0f ? -t : t;
        };
        return {axis(raw.x), axis(raw.y)};
    }
    const float len = std::sqrt(raw.x * raw.x + raw.y * raw.y);
    const float t = Curve(Ramp(len, s.deadZone, s.outerZone), s.curve);
    if (t <= 0.0f || len <= 0.0f) return {0.0f, 0.0f};
    return {raw.x / len * t, raw.y / len * t};
}
float Input::ProcessTrigger(float raw, const TriggerSettings& s) {
    return Curve(Ramp(Clamp01(raw), s.deadZone, 1.0f), s.curve);
}
Input::PadSettings& Input::GamepadSettings(int player) {
    const int i = PadIndex(player);
    return s_padSettings[i < 0 ? 0 : i];
}

void Input::FeedGamepad(const Vec2& axis, unsigned buttonMask) { FeedGamepadAt(0, axis, buttonMask); }
void Input::FeedGamepadExtra(const Vec2& rightAxis, float lt, float rt, bool connected) {
    FeedGamepadExtraAt(0, rightAxis, lt, rt, connected);
}
void Input::FeedGamepadAt(int player, const Vec2& axis, unsigned buttonMask) {
    const int i = PadIndex(player);
    if (i < 0) return;
    PadState& p = s_pads[i];
    p.axis = axis;
    p.previous = p.current;
    p.current = buttonMask;
}
void Input::FeedGamepadExtraAt(int player, const Vec2& rightAxis, float lt, float rt, bool connected) {
    const int i = PadIndex(player);
    if (i < 0) return;
    PadState& p = s_pads[i];
    p.right = rightAxis;
    p.trigger[0] = Clamp01(lt);
    p.trigger[1] = Clamp01(rt);
    p.connected = connected;
}
void Input::FeedGamepadRaw(int player, const Vec2& rawLeft, const Vec2& rawRight, float rawLT, float rawRT,
                           unsigned buttonMask, bool connected) {
    const int i = PadIndex(player);
    if (i < 0) return;
    const PadSettings& cfg = s_padSettings[i];
    const float lt = ProcessTrigger(rawLT, cfg.triggers), rt = ProcessTrigger(rawRT, cfg.triggers);
    buttonMask &= ~((1u << PadLT) | (1u << PadRT));
    if (lt >= cfg.triggers.threshold && lt > 0.0f) buttonMask |= 1u << PadLT;
    if (rt >= cfg.triggers.threshold && rt > 0.0f) buttonMask |= 1u << PadRT;
    FeedGamepadAt(i, ProcessStick(rawLeft, cfg.left), buttonMask);
    FeedGamepadExtraAt(i, ProcessStick(rawRight, cfg.right), lt, rt, connected);
}

namespace {
struct RumbleRequest { float low = 0, high = 0, seconds = 0; bool pending = false; };
RumbleRequest& PendingRumble(int p) { static RumbleRequest r[Input::kMaxGamepads]; return r[p]; }
const char* const kPadNames[Input::PadButtonCount] = {
    "a", "b", "x", "y", "back", "start", "lb", "rb", "up", "down", "left", "right", "ls", "rs", "lt", "rt"};
const char* const kPadLabels[Input::PadButtonCount] = {
    "A", "B", "X", "Y", "Back", "Start", "LB", "RB", "D-pad Up", "D-pad Down", "D-pad Left", "D-pad Right",
    "Left Stick", "Right Stick", "LT", "RT"};
} // namespace

void Input::Rumble(float low, float high, float seconds, int player) {
    const int i = PadIndex(player);
    if (i < 0) return;
    auto clamp01 = [](float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
    PendingRumble(i) = {clamp01(low), clamp01(high), seconds > 0.0f ? seconds : 0.0f, true};
}
bool Input::TakeRumble(float& low, float& high, float& seconds, int player) {
    const int i = PadIndex(player);
    if (i < 0) return false;
    RumbleRequest& r = PendingRumble(i);
    if (!r.pending) return false;
    low = r.low; high = r.high; seconds = r.seconds; r.pending = false;
    return true;
}

int Input::GamepadButtonFromName(const std::string& name) {
    std::string n;
    for (char c : name) n += (char)std::tolower((unsigned char)c);
    for (int i = 0; i < PadButtonCount; ++i) if (n == kPadNames[i]) return i;
    return -1;
}
std::string Input::GamepadButtonKey(int button) {
    return button >= 0 && button < PadButtonCount ? kPadNames[button] : "";
}
std::string Input::GamepadButtonName(int button) {
    return button >= 0 && button < PadButtonCount ? kPadLabels[button] : "?";
}
int Input::FirstGamepadButtonDown(int player) {
    for (int i = 0; i < PadButtonCount; ++i) if (GetGamepadButtonDown(i, player)) return i;
    return -1;
}

const std::string& Input::TypedText() { return s_typedText; }
void Input::FeedText(const std::string& utf8) { s_typedText += utf8; }
void Input::ClearTypedText() {
    s_typedText.clear();
    s_mouseWheel = 0.0f;
    std::size_t n = 0;
    for (auto& t : s_touches) {
        if (t.phase == Touch::Phase::Ended) continue;
        t.phase = Touch::Phase::Stationary;
        t.delta = {0.0f, 0.0f};
        s_touches[n++] = t;
    }
    s_touches.resize(n);
}

int Input::TouchCount() { return (int)s_touches.size(); }
const Input::Touch& Input::GetTouch(int index) { return s_touches[(std::size_t)index]; }
bool Input::PointerIsTouch() { return !s_touches.empty(); }
bool Input::TouchDragging() {
    for (const auto& t : s_touches) {
        const float dx = t.position.x - t.start.x, dy = t.position.y - t.start.y;
        if (dx * dx + dy * dy > kTouchDragPixels * kTouchDragPixels) return true;
    }
    return false;
}

void Input::FeedTouch(long long id, int state, const Vec2& position) {
    s_touchScreen = true;
    Touch* t = nullptr;
    for (auto& e : s_touches) if (e.id == id) t = &e;
    if (state == 0) {
        if (!t) { s_touches.push_back({}); t = &s_touches.back(); }
        *t = Touch{id, position, position, {0.0f, 0.0f}, Touch::Phase::Began};
        return;
    }
    if (!t) return;
    t->delta = {t->delta.x + position.x - t->position.x, t->delta.y + position.y - t->position.y};
    t->position = position;
    if (state == 2) t->phase = Touch::Phase::Ended;
    else if (t->phase != Touch::Phase::Began) t->phase = Touch::Phase::Moved;
}

float Input::MouseWheel() { return s_mouseWheel; }
void Input::FeedMouseWheel(float delta) { s_mouseWheel += delta; }

bool Input::TouchScreen() { return s_touchScreen; }
void Input::SetTouchScreen(bool present) { s_touchScreen = present; }

bool Input::UICaptured() { return s_uiCaptured; }
void Input::SetUICaptured(bool captured) { s_uiCaptured = captured; }

Vec2 Input::GamepadAxis(int player) { const int i = PadIndex(player); return i < 0 ? Vec2{} : s_pads[i].axis; }
Vec2 Input::GamepadRightAxis(int player) { const int i = PadIndex(player); return i < 0 ? Vec2{} : s_pads[i].right; }
float Input::GamepadTrigger(int side, int player) {
    const int i = PadIndex(player);
    return i >= 0 && (side == 0 || side == 1) ? s_pads[i].trigger[side] : 0.0f;
}
bool Input::GamepadConnected(int player) { const int i = PadIndex(player); return i >= 0 && s_pads[i].connected; }
int Input::GamepadCount() { int n = 0; for (const PadState& p : s_pads) if (p.connected) ++n; return n; }
bool Input::GetGamepadButton(int button, int player) {
    const int i = PadIndex(player);
    if (i < 0 || button < 0 || button > 31) return false;
    return (s_pads[i].current & (1u << button)) != 0;
}
bool Input::AnyKey() {
    for (const auto& kv : s_current) if (kv.second) return true;
    if (s_mouseCurrent != 0) return true;
    for (const PadState& p : s_pads) if (p.current != 0) return true;
    return false;
}
bool Input::AnyKeyDown() {
    for (const auto& kv : s_current)        // a key held now that wasn't held last frame
        if (kv.second) { auto it = s_previous.find(kv.first); if (it == s_previous.end() || !it->second) return true; }
    if (s_mouseCurrent & ~s_mousePrevious) return true;
    for (const PadState& p : s_pads) if (p.current & ~p.previous) return true;
    return false;
}

bool Input::GetGamepadButtonUp(int button, int player) {
    const int i = PadIndex(player);
    if (i < 0 || button < 0 || button > 31) return false;
    unsigned bit = 1u << button;
    return !(s_pads[i].current & bit) && (s_pads[i].previous & bit);
}
bool Input::GetGamepadButtonDown(int button, int player) {
    const int i = PadIndex(player);
    if (i < 0 || button < 0 || button > 31) return false;
    unsigned bit = 1u << button;
    return (s_pads[i].current & bit) && !(s_pads[i].previous & bit);
}

bool Input::GetKey(char key) {
    key = static_cast<char>(std::tolower(static_cast<unsigned char>(key)));
    auto it = s_current.find(key);
    return it != s_current.end() && it->second;
}

char Input::FirstKeyDown() {
    char best = 0;
    for (const auto& kv : s_current) {
        if (!kv.second) continue;
        auto it = s_previous.find(kv.first);
        if (it != s_previous.end() && it->second) continue;
        if (best == 0 || (unsigned char)kv.first < (unsigned char)best) best = kv.first;
    }
    return best;
}

std::string Input::KeyName(char key) {
    switch (key) {
        case 0:        return "None";
        case ' ':      return "Space";
        case '\r':     case '\n': return "Enter";
        case 27:       return "Escape";
        case '\b':     return "Backspace";
        case '\t':     return "Tab";
        case KeyShift: return "Shift";
        case KeyCtrl:  return "Ctrl";
        case KeyUp:    return "Up";
        case KeyDown:  return "Down";
        case KeyLeft:  return "Left";
        case KeyRight: return "Right";
        case KeyInsert:   return "Insert";
        case KeyDelete:   return "Delete";
        case KeyHome:     return "Home";
        case KeyEnd:      return "End";
        case KeyPageUp:   return "Page Up";
        case KeyPageDown: return "Page Down";
        case KeyLShift:   return "Left Shift";
        case KeyRShift:   return "Right Shift";
        case KeyLCtrl:    return "Left Ctrl";
        case KeyRCtrl:    return "Right Ctrl";
        case KeyAlt:      return "Alt";
        case KeyLAlt:     return "Left Alt";
        case KeyRAlt:     return "Right Alt";
        default: break;
    }
    if (key >= KeyF1 && key <= KeyF12) return "F" + std::to_string(key - KeyF1 + 1);
    if (key >= 'a' && key <= 'z') return std::string(1, (char)(key - 'a' + 'A'));
    return std::string(1, key);
}

bool Input::GetKeyDown(char key) {
    key = static_cast<char>(std::tolower(static_cast<unsigned char>(key)));
    bool now  = GetKey(key);
    auto prev = s_previous.find(key);
    bool was  = prev != s_previous.end() && prev->second;
    return now && !was;
}

bool Input::GetKeyUp(char key) {
    key = static_cast<char>(std::tolower(static_cast<unsigned char>(key)));
    bool now  = GetKey(key);
    auto prev = s_previous.find(key);
    bool was  = prev != s_previous.end() && prev->second;
    return !now && was;
}

Vec2 Input::AxisWASD() {
    Vec2 axis;
    if (GetKey('d') || GetKey(KeyRight)) axis.x += 1.0f;
    if (GetKey('a') || GetKey(KeyLeft))  axis.x -= 1.0f;
    if (GetKey('w') || GetKey(KeyUp))    axis.y += 1.0f;
    if (GetKey('s') || GetKey(KeyDown))  axis.y -= 1.0f;
    if (axis.x > 1.0f) axis.x = 1.0f;
    if (axis.x < -1.0f) axis.x = -1.0f;
    if (axis.y > 1.0f) axis.y = 1.0f;
    if (axis.y < -1.0f) axis.y = -1.0f;
    return axis;
}

} // namespace okay
