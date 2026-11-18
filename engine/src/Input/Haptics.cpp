#include "okay/Input/Haptics.hpp"
#include "okay/Core/Prefs.hpp"
#include "okay/Input/Input.hpp"
#include <algorithm>
#include <cctype>

namespace okay {

namespace {
struct Running { std::vector<InputHaptics::Step> steps; std::size_t at = 0; float left = 0.0f; bool active = false; };
Running g_run[Input::kMaxGamepads];
float g_strength = -1.0f;   // -1: not read from Prefs yet

void Begin(int p) {
    Running& r = g_run[p];
    if (r.at >= r.steps.size()) { r.active = false; Input::Rumble(0, 0, 0, p); return; }
    const InputHaptics::Step& s = r.steps[r.at];
    r.left = s.seconds;
    Input::Rumble(s.low, s.high, s.seconds, p);   // the player's vibration setting is applied once, in Input::TakeRumble
}
} // namespace

std::vector<InputHaptics::Step> InputHaptics::Pattern(const std::string& nameIn) {
    std::string n;
    for (char c : nameIn) n += (char)std::tolower((unsigned char)c);
    if (n == "tap") return {{0.0f, 0.8f, 0.05f}};
    if (n == "buzz") return {{0.2f, 1.0f, 0.4f}};
    if (n == "pulse") return {{0.6f, 0.6f, 0.08f}, {0, 0, 0.08f}, {0.6f, 0.6f, 0.08f}, {0, 0, 0.08f}, {0.6f, 0.6f, 0.08f}};
    if (n == "heartbeat") return {{0.7f, 0.2f, 0.1f}, {0, 0, 0.1f}, {0.5f, 0.1f, 0.1f}, {0, 0, 0.5f}};
    std::vector<Step> out;
    if (n == "ramp") { for (int i = 1; i <= 10; ++i) out.push_back({0.1f * (float)i, 0.1f * (float)i, 0.1f}); }
    else if (n == "ramp down") { for (int i = 10; i >= 1; --i) out.push_back({0.1f * (float)i, 0.1f * (float)i, 0.1f}); }
    return out;
}

bool InputHaptics::Play(const std::string& pattern, int player) {
    const std::vector<Step> s = Pattern(pattern);
    return !s.empty() && Play(s, player);
}
bool InputHaptics::Play(const std::vector<Step>& steps, int player) {
    if (player < 0 || player >= Input::kMaxGamepads || steps.empty()) return false;
    Running& r = g_run[player];
    r.steps = steps; r.at = 0; r.active = true;
    Begin(player);
    return true;
}
void InputHaptics::Stop(int player) {
    if (player < 0 || player >= Input::kMaxGamepads) return;
    if (g_run[player].active) Input::Rumble(0, 0, 0, player);
    g_run[player] = Running{};
}
bool InputHaptics::Playing(int player) { return player >= 0 && player < Input::kMaxGamepads && g_run[player].active; }

void InputHaptics::Update(float dt) {
    for (int p = 0; p < Input::kMaxGamepads; ++p) {
        Running& r = g_run[p];
        if (!r.active) continue;
        r.left -= dt;
        while (r.active && r.left <= 0.0f) {
            ++r.at;
            const float over = -r.left;
            Begin(p);
            if (r.active) r.left -= over;   // carry the overshoot into the next step so a long frame does not stretch the pattern
        }
    }
}

void InputHaptics::SetStrength(float k) {
    g_strength = std::clamp(k, 0.0f, 1.0f);
    Prefs::SetFloat("input.haptics", g_strength);
}
float InputHaptics::Strength() {
    if (g_strength < 0.0f) g_strength = Prefs::Has("input.haptics") ? std::clamp(Prefs::GetFloat("input.haptics"), 0.0f, 1.0f) : 1.0f;
    return g_strength;
}

} // namespace okay
