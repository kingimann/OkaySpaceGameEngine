#include "okay/Core/DisplaySettings.hpp"
#include "okay/Core/Prefs.hpp"
#include "okay/Render/SoftwareRenderer.hpp"
#include <algorithm>

namespace okay {
namespace {
bool g_fullscreen = false;
bool g_vsync = true;
int  g_quality = 2;
int  g_generation = 0;

void ClampQuality() {
    if (g_quality < 0) g_quality = 0;
    if (g_quality > 2) g_quality = 2;
}
} // namespace

bool DisplaySettings::Fullscreen() { return g_fullscreen; }
bool DisplaySettings::VSync() { return g_vsync; }
int  DisplaySettings::Quality() { return g_quality; }
int  DisplaySettings::Generation() { return g_generation; }

void DisplaySettings::Load() {
    g_fullscreen = Prefs::GetInt("video.fullscreen", g_fullscreen ? 1 : 0) != 0;
    g_vsync = Prefs::GetInt("video.vsync", g_vsync ? 1 : 0) != 0;
    g_quality = Prefs::GetInt("video.quality", g_quality);
    ClampQuality();
}

void DisplaySettings::Save() {
    Prefs::SetInt("video.fullscreen", g_fullscreen ? 1 : 0);
    Prefs::SetInt("video.vsync", g_vsync ? 1 : 0);
    Prefs::SetInt("video.quality", g_quality);
}

void DisplaySettings::SetFullscreen(bool on) {
    if (g_fullscreen == on) return;
    g_fullscreen = on;
    ++g_generation;
    Save();
}
void DisplaySettings::SetVSync(bool on) {
    if (g_vsync == on) return;
    g_vsync = on;
    ++g_generation;
    Save();
}
void DisplaySettings::SetQuality(int q) {
    q = std::clamp(q, 0, 2);
    if (g_quality == q) return;
    g_quality = q;
    ++g_generation;
    ApplyQuality();
    Save();
}

void DisplaySettings::ApplyQuality() {
    // Low drops the expensive passes. Medium is the shipped look (shadows + FXAA).
    // High adds per-pixel lighting, bloom and ambient occlusion.
    const bool low = g_quality <= 0;
    const bool high = g_quality >= 2;
    ShadowsEnabled() = !low;
    PerPixelLighting() = high;
    BloomEnabled() = high;
    SSAOEnabled() = high;
    FXAAEnabled() = !low;
}

} // namespace okay
