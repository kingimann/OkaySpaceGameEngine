#include "okay/Core/DisplaySettings.hpp"
#include "okay/Core/Prefs.hpp"
#include "okay/Render/SoftwareRenderer.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace okay {
namespace {

WindowMode g_mode = WindowMode::Windowed;
bool g_vsync = true;
int  g_quality = 2;
bool g_qualityChosen = false;
DisplayResolution g_resolution;       // 0 x 0 = none chosen
DisplayResolution g_desktop;          // 0 x 0 = unknown
std::vector<DisplayResolution> g_custom;   // the host's mode list (empty = use the standard one)
int  g_generation = 0;

// The standard list a settings screen offers when the host gives none (4:3, 16:10, 16:9 and ultrawide).
const DisplayResolution kStandard[] = {
    {800, 600}, {1024, 768}, {1280, 720}, {1280, 800}, {1366, 768}, {1440, 900}, {1600, 900}, {1680, 1050},
    {1920, 1080}, {1920, 1200}, {2560, 1080}, {2560, 1440}, {2560, 1600}, {3440, 1440}, {3840, 2160},
};

// The quality table. Medium is the shipped look (the renderer's own defaults: 2048 texel cascades,
// 80 units, 3 cascades, softness 2.5); Low drops shadows and the expensive passes; High adds
// per-pixel lighting, bloom and ambient occlusion with larger, longer shadows.
const QualityPreset kPresets[DisplaySettings::kQualityLevels] = {
    {"Low",    false, false, false, false, false, 1024,  40.0f, 1, 1.0f},
    {"Medium", true,  false, false, false, true,  2048,  80.0f, 3, 2.5f},
    {"High",   true,  true,  true,  true,  true,  4096, 120.0f, 4, 3.0f},
};

int ClampQuality(int q) { return std::clamp(q, 0, DisplaySettings::kQualityLevels - 1); }

bool BySize(const DisplayResolution& a, const DisplayResolution& b) {
    const long long aa = (long long)a.width * a.height, bb = (long long)b.width * b.height;
    if (aa != bb) return aa < bb;
    return a.width < b.width;
}

void Sanitize(std::vector<DisplayResolution>& list) {
    list.erase(std::remove_if(list.begin(), list.end(), [](const DisplayResolution& r) {
        return r.width < DisplaySettings::kMinWidth || r.height < DisplaySettings::kMinHeight ||
               r.width > DisplaySettings::kMaxWidth || r.height > DisplaySettings::kMaxHeight;
    }), list.end());
    std::sort(list.begin(), list.end(), BySize);
    list.erase(std::unique(list.begin(), list.end()), list.end());
}

std::string Lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// The list Resolutions() hands out; rebuilt when its inputs change.
std::vector<DisplayResolution>& ListCache() { static std::vector<DisplayResolution> v; return v; }
bool g_listDirty = true;

void Rebuild() {
    std::vector<DisplayResolution> list;
    if (!g_custom.empty()) {
        list = g_custom;
    } else {
        for (const DisplayResolution& r : kStandard)
            if (!g_desktop.Valid() || (r.width <= g_desktop.width && r.height <= g_desktop.height)) list.push_back(r);
        if (g_desktop.Valid()) list.push_back(g_desktop);
    }
    Sanitize(list);
    ListCache() = std::move(list);
    g_listDirty = false;
}

bool ReadInt(const char* key, int& out) {
    if (!Prefs::Has(key)) return false;
    out = Prefs::GetInt(key, out);
    return true;
}
} // namespace

// ---- Window mode ------------------------------------------------------------------------
WindowMode DisplaySettings::Mode() { return g_mode; }
bool DisplaySettings::Fullscreen() { return g_mode != WindowMode::Windowed; }

void DisplaySettings::SetMode(WindowMode mode) {
    if (mode != WindowMode::Windowed && mode != WindowMode::Borderless && mode != WindowMode::Fullscreen) mode = WindowMode::Windowed;
    if (g_mode == mode) return;
    g_mode = mode;
    ++g_generation;
    Save();
}
void DisplaySettings::SetFullscreen(bool on) {
    if (!on) SetMode(WindowMode::Windowed);
    else if (g_mode == WindowMode::Windowed) SetMode(WindowMode::Borderless);
}
std::vector<WindowMode> DisplaySettings::Modes() {
    return {WindowMode::Windowed, WindowMode::Borderless, WindowMode::Fullscreen};
}
const char* DisplaySettings::ModeName(WindowMode mode) {
    switch (mode) {
        case WindowMode::Borderless: return "borderless";
        case WindowMode::Fullscreen: return "fullscreen";
        case WindowMode::Windowed: break;
    }
    return "windowed";
}
bool DisplaySettings::ParseMode(const std::string& text, WindowMode& out) {
    std::string t = Lower(text);
    t.erase(std::remove_if(t.begin(), t.end(), [](char c) { return std::isspace((unsigned char)c) != 0; }), t.end());
    if (t == "windowed" || t == "window" || t == "0") out = WindowMode::Windowed;
    else if (t == "borderless" || t == "fullscreen_desktop" || t == "fullscreen-desktop" || t == "1") out = WindowMode::Borderless;
    else if (t == "fullscreen" || t == "exclusive" || t == "2") out = WindowMode::Fullscreen;
    else return false;
    return true;
}

// ---- Resolution -------------------------------------------------------------------------
bool DisplaySettings::HasResolution() { return g_resolution.Valid(); }
int  DisplaySettings::Width() { return g_resolution.width; }
int  DisplaySettings::Height() { return g_resolution.height; }
DisplayResolution DisplaySettings::Resolution() { return g_resolution; }

DisplayResolution DisplaySettings::ClampResolution(int width, int height) {
    return {std::clamp(width, kMinWidth, kMaxWidth), std::clamp(height, kMinHeight, kMaxHeight)};
}
void DisplaySettings::SetResolution(int width, int height) {
    const DisplayResolution r = ClampResolution(width, height);
    if (g_resolution == r) return;
    g_resolution = r;
    ++g_generation;
    Save();
}
void DisplaySettings::SetResolutionIndex(int index) {
    const auto& list = Resolutions();
    if (list.empty()) return;
    const DisplayResolution r = list[(std::size_t)std::clamp(index, 0, (int)list.size() - 1)];
    SetResolution(r.width, r.height);
}
void DisplaySettings::ClearResolution() {
    if (!g_resolution.Valid()) return;
    g_resolution = DisplayResolution{};
    ++g_generation;
    Save();
}

const std::vector<DisplayResolution>& DisplaySettings::Resolutions() {
    if (g_listDirty) Rebuild();
    return ListCache();
}
void DisplaySettings::SetSupportedResolutions(const std::vector<DisplayResolution>& modes) {
    g_custom = modes;
    Sanitize(g_custom);
    g_listDirty = true;
}
void DisplaySettings::SetDesktopResolution(int width, int height) {
    g_desktop = (width > 0 && height > 0) ? ClampResolution(width, height) : DisplayResolution{};
    g_listDirty = true;
}
DisplayResolution DisplaySettings::DesktopResolution() { return g_desktop; }

DisplayResolution DisplaySettings::NearestResolution(int width, int height) {
    const auto& list = Resolutions();
    DisplayResolution best;
    double bestDist = 0.0;
    for (const DisplayResolution& r : list) {
        if (r.width == width && r.height == height) return r;
        const double dw = (double)r.width - width, dh = (double)r.height - height;
        const double d = dw * dw + dh * dh;
        if (!best.Valid() || d < bestDist || (d == bestDist && BySize(r, best))) { best = r; bestDist = d; }
    }
    return best;
}
int DisplaySettings::ResolutionIndex() {
    if (!g_resolution.Valid()) return -1;
    const auto& list = Resolutions();
    for (std::size_t i = 0; i < list.size(); ++i) if (list[i] == g_resolution) return (int)i;
    return -1;
}
DisplayResolution DisplaySettings::EffectiveResolution() {
    switch (g_mode) {
        case WindowMode::Borderless:
            if (g_desktop.Valid()) return g_desktop;
            return g_resolution;
        case WindowMode::Fullscreen:
            if (!g_resolution.Valid()) return g_desktop;
            return NearestResolution(g_resolution.width, g_resolution.height);
        case WindowMode::Windowed: break;
    }
    return g_resolution;
}

// ---- VSync and quality ------------------------------------------------------------------
bool DisplaySettings::VSync() { return g_vsync; }
void DisplaySettings::SetVSync(bool on) {
    if (g_vsync == on) return;
    g_vsync = on;
    ++g_generation;
    Save();
}

int  DisplaySettings::Quality() { return g_quality; }
bool DisplaySettings::QualityChosen() { return g_qualityChosen; }
const QualityPreset& DisplaySettings::Preset(int quality) { return kPresets[ClampQuality(quality)]; }
const char* DisplaySettings::QualityName(int quality) { return Preset(quality).name; }

void DisplaySettings::SetQuality(int q) {
    q = ClampQuality(q);
    const bool first = !g_qualityChosen;
    g_qualityChosen = true;
    if (g_quality == q && !first) return;
    g_quality = q;
    ++g_generation;
    ApplyPreset(q);
    Save();
}

void DisplaySettings::ApplyPreset(int quality) {
    const QualityPreset& p = Preset(quality);
    ShadowsEnabled() = p.shadows;
    PerPixelLighting() = p.perPixelLighting;
    BloomEnabled() = p.bloom;
    SSAOEnabled() = p.ssao;
    FXAAEnabled() = p.fxaa;
    ShadowMapResolution() = p.shadowResolution;
    ShadowDistance() = p.shadowDistance;
    ShadowCascades() = p.shadowCascades;
    ShadowSoftness() = p.shadowSoftness;
}

void DisplaySettings::ApplyQuality() {
    if (g_qualityChosen) ApplyPreset(g_quality);
}

// ---- Persistence ------------------------------------------------------------------------
int DisplaySettings::Generation() { return g_generation; }

void DisplaySettings::Load() {
    // The window mode: the new key, else the older on/off one (which drew a borderless full screen).
    WindowMode mode = g_mode;
    bool haveMode = false;
    if (Prefs::Has("video.windowmode")) haveMode = ParseMode(Prefs::GetString("video.windowmode"), mode);
    if (!haveMode && Prefs::Has("video.fullscreen")) {
        mode = Prefs::GetInt("video.fullscreen", g_mode != WindowMode::Windowed ? 1 : 0) != 0 ? WindowMode::Borderless : WindowMode::Windowed;
        haveMode = true;
    }
    if (haveMode) g_mode = mode;
    g_vsync = Prefs::GetInt("video.vsync", g_vsync ? 1 : 0) != 0;
    if (Prefs::Has("video.quality")) {
        g_quality = ClampQuality(Prefs::GetInt("video.quality", g_quality));
        g_qualityChosen = true;
    }
    int w = 0, h = 0;
    if (ReadInt("video.width", w) && ReadInt("video.height", h) && w > 0 && h > 0) g_resolution = ClampResolution(w, h);
}

void DisplaySettings::Save() {
    Prefs::SetInt("video.fullscreen", g_mode != WindowMode::Windowed ? 1 : 0);   // older readers
    Prefs::SetString("video.windowmode", ModeName(g_mode));
    Prefs::SetInt("video.vsync", g_vsync ? 1 : 0);
    if (g_qualityChosen) Prefs::SetInt("video.quality", g_quality);
    if (g_resolution.Valid()) {
        Prefs::SetInt("video.width", g_resolution.width);
        Prefs::SetInt("video.height", g_resolution.height);
    } else {
        Prefs::Delete("video.width");
        Prefs::Delete("video.height");
    }
}

void DisplaySettings::Restore() {
    const bool any = Prefs::Has("video.windowmode") || Prefs::Has("video.fullscreen") || Prefs::Has("video.vsync") ||
                     Prefs::Has("video.quality") || Prefs::Has("video.width");
    Load();
    if (any) ++g_generation;
}

std::string DisplaySettings::Summary() {
    std::string s = g_resolution.Valid() ? g_resolution.Label() : std::string("default size");
    s += ", ";
    s += ModeName(g_mode);
    s += g_vsync ? ", vsync on" : ", vsync off";
    s += ", quality ";
    s += QualityName(g_quality);
    return s;
}

void DisplaySettings::ResetToDefaults() {
    g_mode = WindowMode::Windowed;
    g_vsync = true;
    g_quality = 2;
    g_qualityChosen = false;
    g_resolution = DisplayResolution{};
    g_desktop = DisplayResolution{};
    g_custom.clear();
    g_listDirty = true;
    ++g_generation;
}

} // namespace okay
