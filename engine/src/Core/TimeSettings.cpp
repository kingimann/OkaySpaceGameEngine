#include "okay/Core/TimeSettings.hpp"
#include "okay/Core/Clock.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Scene/Scene.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ostream>
#include <sstream>

namespace okay {

namespace {
float Fix(float v, float lo, float hi, float def) {
    if (!std::isfinite(v)) return def;
    return v < lo ? lo : (v > hi ? hi : v);
}
bool ParseFloat(const std::string& s, float& out) {
    const char* b = s.c_str();
    char* e = nullptr;
    const double v = std::strtod(b, &e);
    if (e == b || !std::isfinite(v)) return false;
    out = (float)v;
    return true;
}
std::string Trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
}
}

bool TimeSettings::IsDefault() const {
    return timeScale == 1.0f && maxFrameSeconds == kDefaultMaxFrame && maxCatchUpSeconds == kDefaultCatchUp &&
           !spreadCatchUp && unfocusedFps == 0.0f && clocks.empty();
}

bool TimeSettings::operator==(const TimeSettings& o) const {
    return timeScale == o.timeScale && maxFrameSeconds == o.maxFrameSeconds && maxCatchUpSeconds == o.maxCatchUpSeconds &&
           spreadCatchUp == o.spreadCatchUp && unfocusedFps == o.unfocusedFps && clocks == o.clocks;
}

void TimeSettings::Clamp() {
    timeScale = Fix(timeScale, 0.0f, Time::kMaxTimeScale, 1.0f);
    maxFrameSeconds = Fix(maxFrameSeconds, 0.01f, 5.0f, kDefaultMaxFrame);
    maxCatchUpSeconds = Fix(maxCatchUpSeconds, 0.02f, 5.0f, kDefaultCatchUp);
    unfocusedFps = Fix(unfocusedFps, 0.0f, 1000.0f, 0.0f);
    if (unfocusedFps > 0.0f && unfocusedFps < 5.0f) unfocusedFps = 5.0f;
    std::vector<ClockSpec> keep;
    for (ClockSpec c : clocks) {
        c.name = Trim(c.name);
        c.parent = Trim(c.parent);
        if (c.name.empty() || c.name == c.parent) continue;
        if (c.name.find('|') != std::string::npos || c.parent.find('|') != std::string::npos) continue;
        c.scale = Fix(c.scale, 0.0f, Time::kMaxTimeScale, 1.0f);
        bool dup = false;
        for (const ClockSpec& k : keep) if (k.name == c.name) dup = true;
        if (!dup && (int)keep.size() < kMaxClocks) keep.push_back(std::move(c));
    }
    clocks.swap(keep);
}

bool TimeSettings::ParseLine(const std::string& key, const std::string& value) {
    if (key.compare(0, 5, "time_") != 0) return false;
    float f = 0.0f;
    if (key == "time_scale") { if (ParseFloat(value, f)) timeScale = Fix(f, 0.0f, Time::kMaxTimeScale, 1.0f); }
    else if (key == "time_max_frame") { if (ParseFloat(value, f)) maxFrameSeconds = Fix(f, 0.01f, 5.0f, kDefaultMaxFrame); }
    else if (key == "time_catch_up") { if (ParseFloat(value, f)) maxCatchUpSeconds = Fix(f, 0.02f, 5.0f, kDefaultCatchUp); }
    else if (key == "time_policy") spreadCatchUp = Trim(value) == "spread";
    else if (key == "time_unfocused_fps") {
        if (ParseFloat(value, f)) { unfocusedFps = Fix(f, 0.0f, 1000.0f, 0.0f); if (unfocusedFps > 0.0f && unfocusedFps < 5.0f) unfocusedFps = 5.0f; }
    }
    else if (key == "time_clock") {
        // name|parent|scale
        std::vector<std::string> parts;
        std::stringstream ss(value);
        std::string p;
        while (std::getline(ss, p, '|')) parts.push_back(p);
        if (parts.empty()) return true;
        ClockSpec c;
        c.name = Trim(parts[0]);
        if (parts.size() > 1) c.parent = Trim(parts[1]);
        if (parts.size() > 2) { float s; if (ParseFloat(parts[2], s)) c.scale = s; }
        clocks.push_back(std::move(c));
        Clamp();
    }
    return true;
}

void TimeSettings::WriteLines(std::ostream& out) const {
    auto num = [](float v) { std::ostringstream o; o.precision(9); o << v; return o.str(); };
    if (timeScale != 1.0f) out << "time_scale=" << num(timeScale) << "\n";
    if (maxFrameSeconds != kDefaultMaxFrame) out << "time_max_frame=" << num(maxFrameSeconds) << "\n";
    if (maxCatchUpSeconds != kDefaultCatchUp) out << "time_catch_up=" << num(maxCatchUpSeconds) << "\n";
    if (spreadCatchUp) out << "time_policy=spread\n";
    if (unfocusedFps != 0.0f) out << "time_unfocused_fps=" << num(unfocusedFps) << "\n";
    for (const ClockSpec& c : clocks) out << "time_clock=" << c.name << "|" << c.parent << "|" << num(c.scale) << "\n";
}

void TimeSettings::ApplyToLoop(GameLoop::Options& o, float stepSeconds) const {
    o.maxDelta = Fix(maxFrameSeconds, 0.01f, 5.0f, kDefaultMaxFrame);
    const float step = stepSeconds > 0.0f && std::isfinite(stepSeconds) ? stepSeconds : Time::FixedDeltaTime();
    const float catchUp = Fix(maxCatchUpSeconds, 0.02f, 5.0f, kDefaultCatchUp);
    const int steps = (int)std::ceil((double)catchUp / (double)step - 1e-4);   // 0.5 / 0.02f is 25.0000003: not 26
    o.maxFixedSteps = std::max(1, std::min(steps, GameLoop::kMaxFixedStepsPerFrame));
    o.spreadFixedOverflow = spreadCatchUp;
}

void TimeSettings::ApplyClocks(ClockSet& set) const {
    for (const ClockSpec& c : clocks) {
        Clock& k = set.Add(c.name);   // an unknown name is a new clock on real time
        k.SetScale(c.scale);
    }
    // Parents after every clock exists, so the list may name them in any order.
    for (const ClockSpec& c : clocks) {
        if (c.parent.empty()) continue;
        if (Clock* k = set.Find(c.name)) k->SetParent(c.parent);
    }
}

void TimeSettings::ApplyAtStart() const {
    Time::SetTimeScale(Fix(timeScale, 0.0f, Time::kMaxTimeScale, 1.0f));
    Scene::SetProjectPhysicsCatchUp(Fix(maxCatchUpSeconds, 0.02f, 5.0f, kDefaultCatchUp), spreadCatchUp);
}

std::vector<ClockSpec> TimeSettings::StandardClocks() {
    return {{"audio", "game", 1.0f}, {"physics", "game", 1.0f}, {"network", "", 1.0f}};
}

} // namespace okay
