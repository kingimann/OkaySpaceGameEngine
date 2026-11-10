#include "okay/Core/DebugOverlay.hpp"
#include "okay/Core/BuildInfo.hpp"
#include "okay/Core/Cvar.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Scene/Scene.hpp"
#include <cstdio>

namespace okay {

namespace {
struct Provider { int id; std::function<std::string()> fn; };
std::vector<Provider>& Providers() { static std::vector<Provider> p; return p; }
int g_next = 1;
Cvar& EnabledVar() { return Cvars::Bool("debug.overlay", false, "show the debug overlay (FPS, frame time, objects)"); }
std::string Fmt(const char* f, double a, double b = 0.0) {
    char buf[96]; std::snprintf(buf, sizeof(buf), f, a, b); return buf;
}
}

DebugOverlay::Stats DebugOverlay::Capture(const Scene* scene) {
    Stats s;
    s.fps = Time::SmoothedFPS();
    s.frameMs = Time::RawDeltaTime() * 1000.0f;
    s.frame = Time::FrameCount();
    s.timeScale = Time::TimeScale();
    s.paused = s.timeScale == 0.0f;
    if (scene) s.objectCount = scene->Objects().size();
    return s;
}

std::vector<std::string> DebugOverlay::BuildLines(const Stats& s, bool detailed, bool ignoreEnabled) {
    std::vector<std::string> lines;
    if (!ignoreEnabled && !Enabled()) return lines;
    lines.push_back(Fmt("FPS: %.1f (%.1f ms)", s.fps, s.frameMs));
    lines.push_back("Objects: " + std::to_string(s.objectCount));
    if (detailed) {
        lines.push_back("Frame: " + std::to_string((unsigned long long)s.frame));
        lines.push_back(s.paused ? std::string("Paused") : Fmt("Time scale: %.2f", s.timeScale));
        lines.push_back(BuildInfo::Summary());
    }
    // Work on a copy: a provider may add or remove lines (a one-shot line removes itself) while it runs, which
    // would invalidate the list being walked. Lines added meanwhile first show in the next call.
    const std::vector<Provider> providers = Providers();
    for (const Provider& p : providers) { std::string t = p.fn(); if (!t.empty()) lines.push_back(std::move(t)); }
    return lines;
}

bool DebugOverlay::Enabled() { return EnabledVar().AsBool(); }
void DebugOverlay::SetEnabled(bool on) { EnabledVar().SetBool(on); }

int DebugOverlay::AddLine(std::function<std::string()> provider) {
    if (!provider) return 0;
    Providers().push_back({g_next, std::move(provider)});
    return g_next++;
}
void DebugOverlay::RemoveLine(int id) {
    auto& p = Providers();
    for (auto it = p.begin(); it != p.end(); ++it) if (it->id == id) { p.erase(it); return; }
}
void DebugOverlay::ClearLines() { Providers().clear(); }

} // namespace okay
