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
    // Shadow resolution and how far cascades reach. High matches the historical
    // defaults (2048, 80 m, 3 cascades) so an untouched install looks the same.
    if (low) {
        ShadowMapResolution() = 512;
        ShadowDistance() = 40.0f;
        ShadowCascades() = 1;
    } else if (!high) {
        ShadowMapResolution() = 1024;
        ShadowDistance() = 60.0f;
        ShadowCascades() = 2;
    } else {
        ShadowMapResolution() = 2048;
        ShadowDistance() = 80.0f;
        ShadowCascades() = 3;
    }
}

} // namespace okay
