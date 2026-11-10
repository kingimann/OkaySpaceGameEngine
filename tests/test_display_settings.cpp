// Display settings: window modes, resolutions, vsync and the quality preset, as plain data
// that needs no window (docs/display_settings.md).
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Core/DisplaySettings.hpp"
#include "okay/Render/ShadowSetup.hpp"
#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

using namespace okay;
namespace fs = std::filesystem;

namespace {

bool Listed(const std::vector<DisplayResolution>& list, int w, int h) {
    return std::find(list.begin(), list.end(), DisplayResolution{w, h}) != list.end();
}
bool Sorted(const std::vector<DisplayResolution>& list) {
    for (std::size_t i = 1; i < list.size(); ++i) {
        const long long a = (long long)list[i - 1].width * list[i - 1].height, b = (long long)list[i].width * list[i].height;
        if (a > b || (a == b && list[i - 1].width >= list[i].width)) return false;   // strictly increasing: sorted, no repeats
    }
    return true;
}
void ResetRenderer() {
    ShadowsEnabled() = true; PerPixelLighting() = false; BloomEnabled() = false; SSAOEnabled() = false; FXAAEnabled() = true;
    ShadowMapResolution() = 2048; ShadowDistance() = 80.0f; ShadowCascades() = 3; ShadowSoftness() = 2.5f;
}

} // namespace

int main() {
    RUN_SUITE("display_settings");
    Prefs::Clear();
    DisplaySettings::ResetToDefaults();
    ResetRenderer();

    // ---- Defaults ----
    {
        CHECK(DisplaySettings::Mode() == WindowMode::Windowed && !DisplaySettings::Fullscreen());
        CHECK(DisplaySettings::VSync() && DisplaySettings::Quality() == 2 && !DisplaySettings::QualityChosen());
        CHECK(!DisplaySettings::HasResolution() && DisplaySettings::Width() == 0 && DisplaySettings::Height() == 0);
        CHECK(DisplaySettings::ResolutionIndex() == -1 && !DisplaySettings::EffectiveResolution().Valid());
        CHECK(DisplaySettings::Summary() == "default size, windowed, vsync on, quality High");
    }

    // ---- Window modes ----
    {
        const auto modes = DisplaySettings::Modes();
        CHECK(modes.size() == 3 && modes[0] == WindowMode::Windowed && modes[1] == WindowMode::Borderless && modes[2] == WindowMode::Fullscreen);
        CHECK(std::string(DisplaySettings::ModeName(WindowMode::Windowed)) == "windowed");
        CHECK(std::string(DisplaySettings::ModeName(WindowMode::Borderless)) == "borderless");
        CHECK(std::string(DisplaySettings::ModeName(WindowMode::Fullscreen)) == "fullscreen");
        WindowMode m = WindowMode::Windowed;
        CHECK(DisplaySettings::ParseMode("Borderless", m) && m == WindowMode::Borderless);
        CHECK(DisplaySettings::ParseMode("  FULLSCREEN ", m) && m == WindowMode::Fullscreen);
        CHECK(DisplaySettings::ParseMode("window", m) && m == WindowMode::Windowed);
        CHECK(DisplaySettings::ParseMode("fullscreen_desktop", m) && m == WindowMode::Borderless);
        CHECK(DisplaySettings::ParseMode("2", m) && m == WindowMode::Fullscreen);
        m = WindowMode::Borderless;
        CHECK(!DisplaySettings::ParseMode("", m) && !DisplaySettings::ParseMode("wide", m) && !DisplaySettings::ParseMode("3", m) && m == WindowMode::Borderless);

        int gen = DisplaySettings::Generation();
        DisplaySettings::SetMode(WindowMode::Windowed);                       // no change: no bump
        CHECK(DisplaySettings::Generation() == gen);
        DisplaySettings::SetMode(WindowMode::Fullscreen);
        CHECK(DisplaySettings::Mode() == WindowMode::Fullscreen && DisplaySettings::Fullscreen() && DisplaySettings::Generation() == gen + 1);
        DisplaySettings::SetFullscreen(true);                                 // already full screen: the mode stays
        CHECK(DisplaySettings::Mode() == WindowMode::Fullscreen && DisplaySettings::Generation() == gen + 1);
        DisplaySettings::SetFullscreen(false);
        CHECK(DisplaySettings::Mode() == WindowMode::Windowed && !DisplaySettings::Fullscreen());
        DisplaySettings::SetFullscreen(true);                                 // the old on/off switch: a borderless window
        CHECK(DisplaySettings::Mode() == WindowMode::Borderless && DisplaySettings::Fullscreen());
        DisplaySettings::SetMode((WindowMode)99);                             // not a mode: windowed
        CHECK(DisplaySettings::Mode() == WindowMode::Windowed);
    }

    // ---- Resolution: clamping and any size in range ----
    {
        int gen = DisplaySettings::Generation();
        DisplaySettings::SetResolution(1234, 777);
        CHECK(DisplaySettings::HasResolution() && DisplaySettings::Width() == 1234 && DisplaySettings::Height() == 777);
        CHECK(DisplaySettings::Resolution() == DisplayResolution({1234, 777}) && DisplaySettings::Generation() == gen + 1);
        DisplaySettings::SetResolution(1234, 777);                            // the same: no bump
        CHECK(DisplaySettings::Generation() == gen + 1);
        DisplaySettings::SetResolution(100, 100);
        CHECK(DisplaySettings::Width() == DisplaySettings::kMinWidth && DisplaySettings::Height() == DisplaySettings::kMinHeight);
        DisplaySettings::SetResolution(-5, 0);
        CHECK(DisplaySettings::Width() == DisplaySettings::kMinWidth && DisplaySettings::Height() == DisplaySettings::kMinHeight);
        DisplaySettings::SetResolution(1 << 30, 1 << 30);
        CHECK(DisplaySettings::Width() == DisplaySettings::kMaxWidth && DisplaySettings::Height() == DisplaySettings::kMaxHeight);
        CHECK(DisplaySettings::ClampResolution(1920, 1080) == DisplayResolution({1920, 1080}));
        CHECK(DisplaySettings::ClampResolution(0, 5000) == DisplayResolution({DisplaySettings::kMinWidth, 5000}));
        CHECK(DisplayResolution({1920, 1080}).Label() == "1920 x 1080" && DisplayResolution({1920, 1080}).Aspect() > 1.77f);
        DisplaySettings::ClearResolution();
        CHECK(!DisplaySettings::HasResolution() && DisplaySettings::Width() == 0);
    }

    // ---- The list a settings screen offers ----
    {
        DisplaySettings::ResetToDefaults();
        const auto std1 = DisplaySettings::Resolutions();
        CHECK(std1.size() >= 10 && Sorted(std1) && Listed(std1, 1920, 1080) && Listed(std1, 1280, 720) && Listed(std1, 3840, 2160));
        for (const auto& r : std1)
            CHECK(r.width >= DisplaySettings::kMinWidth && r.height >= DisplaySettings::kMinHeight && r.width <= DisplaySettings::kMaxWidth);
        // The desktop's size cuts the list and is in it.
        DisplaySettings::SetDesktopResolution(1920, 1080);
        auto d1 = DisplaySettings::Resolutions();
        CHECK(Sorted(d1) && d1.back() == DisplayResolution({1920, 1080}) && !Listed(d1, 2560, 1440) && Listed(d1, 1280, 720));
        CHECK(DisplaySettings::DesktopResolution() == DisplayResolution({1920, 1080}));
        DisplaySettings::SetDesktopResolution(2000, 1000);                   // an odd size: kept, and nothing taller or wider
        auto d2 = DisplaySettings::Resolutions();
        CHECK(Sorted(d2) && Listed(d2, 2000, 1000) && !Listed(d2, 1920, 1080 + 120) && Listed(d2, 1280, 720));
        for (const auto& r : d2) CHECK(r.width <= 2000 && r.height <= 1000);
        DisplaySettings::SetDesktopResolution(0, 0);
        CHECK(DisplaySettings::Resolutions() == std1 && !DisplaySettings::DesktopResolution().Valid());

        // The host's own modes: out-of-range and repeated entries dropped, sorted.
        DisplaySettings::SetSupportedResolutions({{1920, 1080}, {1280, 720}, {1280, 720}, {100, 100}, {0, 0}, {800, 600}, {99999, 99999}, {-1, 600}});
        const auto host = DisplaySettings::Resolutions();
        CHECK(host.size() == 3 && host[0] == DisplayResolution({800, 600}) && host[1] == DisplayResolution({1280, 720}) && host[2] == DisplayResolution({1920, 1080}));
        CHECK(DisplaySettings::NearestResolution(1900, 1000) == DisplayResolution({1920, 1080}));
        CHECK(DisplaySettings::NearestResolution(1280, 720) == DisplayResolution({1280, 720}));
        CHECK(DisplaySettings::NearestResolution(1, 1) == DisplayResolution({800, 600}));
        CHECK(DisplaySettings::NearestResolution(8000, 8000) == DisplayResolution({1920, 1080}));
        CHECK(DisplaySettings::NearestResolution(1600, 900) == DisplayResolution({1280, 720}) || DisplaySettings::NearestResolution(1600, 900) == DisplayResolution({1920, 1080}));
        // Choosing by index.
        DisplaySettings::ClearResolution();
        DisplaySettings::SetResolutionIndex(1);
        CHECK(DisplaySettings::Resolution() == DisplayResolution({1280, 720}) && DisplaySettings::ResolutionIndex() == 1);
        DisplaySettings::SetResolutionIndex(-5);
        CHECK(DisplaySettings::ResolutionIndex() == 0);
        DisplaySettings::SetResolutionIndex(99);
        CHECK(DisplaySettings::ResolutionIndex() == 2 && DisplaySettings::Resolution() == DisplayResolution({1920, 1080}));
        DisplaySettings::SetResolution(1000, 700);                            // not in the list
        CHECK(DisplaySettings::ResolutionIndex() == -1);
        // An empty list goes back to the standard one.
        DisplaySettings::SetSupportedResolutions({});
        CHECK(DisplaySettings::Resolutions() == std1);
        DisplaySettings::SetSupportedResolutions({{1, 1}});                    // nothing usable left
        CHECK(DisplaySettings::Resolutions() == std1);
    }

    // ---- What a host applies ----
    {
        DisplaySettings::ResetToDefaults();
        CHECK(!DisplaySettings::EffectiveResolution().Valid());              // nothing chosen, desktop unknown: keep the host's size
        DisplaySettings::SetSupportedResolutions({{800, 600}, {1280, 720}, {1920, 1080}});
        DisplaySettings::SetResolution(1300, 700);
        CHECK(DisplaySettings::EffectiveResolution() == DisplayResolution({1300, 700}));        // windowed: any size
        DisplaySettings::SetMode(WindowMode::Fullscreen);
        CHECK(DisplaySettings::EffectiveResolution() == DisplayResolution({1280, 720}));        // full screen: a listed mode
        DisplaySettings::SetMode(WindowMode::Borderless);
        CHECK(DisplaySettings::EffectiveResolution() == DisplayResolution({1300, 700}));        // desktop unknown: the chosen size
        DisplaySettings::SetDesktopResolution(2560, 1440);
        CHECK(DisplaySettings::EffectiveResolution() == DisplayResolution({2560, 1440}));       // borderless covers the desktop
        DisplaySettings::ClearResolution();
        DisplaySettings::SetMode(WindowMode::Fullscreen);
        CHECK(DisplaySettings::EffectiveResolution() == DisplayResolution({2560, 1440}));       // none chosen: the desktop
        DisplaySettings::SetMode(WindowMode::Windowed);
        CHECK(!DisplaySettings::EffectiveResolution().Valid());
    }

    // ---- Quality: the preset drives shadow resolution and distance, and the switches ----
    {
        DisplaySettings::ResetToDefaults();
        ResetRenderer();
        // Nothing chosen yet: a game's own render settings (Build Settings) are left alone.
        ShadowDistance() = 55.0f; ShadowMapResolution() = 512; BloomEnabled() = true;
        DisplaySettings::SetVSync(false);                                     // another setting changed ...
        DisplaySettings::ApplyQuality();                                      // ... and the host re-applies: no overwrite
        CHECK(ShadowDistance() == 55.0f && ShadowMapResolution() == 512 && BloomEnabled());
        // Choosing High (the value it already has) still applies it.
        DisplaySettings::SetQuality(2);
        CHECK(DisplaySettings::QualityChosen());
        CHECK(ShadowMapResolution() == 4096 && ShadowDistance() == 120.0f && ShadowCascades() == 4 && ShadowSoftness() == 3.0f);
        CHECK(ShadowsEnabled() && PerPixelLighting() && BloomEnabled() && SSAOEnabled() && FXAAEnabled());
        DisplaySettings::SetQuality(1);
        CHECK(ShadowMapResolution() == 2048 && ShadowDistance() == 80.0f && ShadowCascades() == 3 && ShadowSoftness() == 2.5f);
        CHECK(ShadowsEnabled() && !PerPixelLighting() && !BloomEnabled() && !SSAOEnabled() && FXAAEnabled());
        DisplaySettings::SetQuality(0);
        CHECK(ShadowMapResolution() == 1024 && ShadowDistance() == 40.0f && ShadowCascades() == 1 && ShadowSoftness() == 1.0f);
        CHECK(!ShadowsEnabled() && !PerPixelLighting() && !BloomEnabled() && !SSAOEnabled() && !FXAAEnabled());
        // The table grows with the level, and fits the renderers' limits.
        for (int q = 1; q < DisplaySettings::kQualityLevels; ++q) {
            CHECK(DisplaySettings::Preset(q).shadowResolution >= DisplaySettings::Preset(q - 1).shadowResolution);
            CHECK(DisplaySettings::Preset(q).shadowDistance >= DisplaySettings::Preset(q - 1).shadowDistance);
            CHECK(DisplaySettings::Preset(q).shadowCascades >= DisplaySettings::Preset(q - 1).shadowCascades);
        }
        for (int q = 0; q < DisplaySettings::kQualityLevels; ++q) {
            const QualityPreset& p = DisplaySettings::Preset(q);
            CHECK(p.shadowCascades >= 1 && p.shadowCascades <= ShadowPlan::kMaxCascades);
            CHECK(p.shadowResolution >= 256 && p.shadowResolution <= 4096 && p.shadowDistance > 0.0f);
        }
        // Out of range levels clamp; names.
        DisplaySettings::SetQuality(-3);
        CHECK(DisplaySettings::Quality() == 0);
        DisplaySettings::SetQuality(99);
        CHECK(DisplaySettings::Quality() == 2);
        CHECK(std::string(DisplaySettings::QualityName(0)) == "Low" && std::string(DisplaySettings::QualityName(1)) == "Medium" && std::string(DisplaySettings::QualityName(2)) == "High");
        CHECK(&DisplaySettings::Preset(-1) == &DisplaySettings::Preset(0) && &DisplaySettings::Preset(7) == &DisplaySettings::Preset(2));
        // ApplyQuality applies the chosen level; ApplyPreset any level.
        ShadowDistance() = 1.0f;
        DisplaySettings::ApplyQuality();
        CHECK(ShadowDistance() == 120.0f);
        DisplaySettings::ApplyPreset(0);
        CHECK(ShadowDistance() == 40.0f && DisplaySettings::Quality() == 2);
    }

    // ---- Persistence through Prefs ----
    {
        Prefs::Clear();
        DisplaySettings::ResetToDefaults();
        DisplaySettings::SetMode(WindowMode::Fullscreen);
        DisplaySettings::SetResolution(2560, 1440);
        DisplaySettings::SetVSync(false);
        CHECK(Prefs::GetString("video.windowmode") == "fullscreen" && Prefs::GetInt("video.fullscreen", -1) == 1);   // older readers still see "on"
        CHECK(Prefs::GetInt("video.width") == 2560 && Prefs::GetInt("video.height") == 1440 && Prefs::GetInt("video.vsync", -1) == 0);
        CHECK(!Prefs::Has("video.quality"));                                  // a level nobody chose is not saved
        DisplaySettings::SetQuality(1);
        CHECK(Prefs::GetInt("video.quality", -1) == 1);

        // Through a real prefs file.
        const fs::path dir = fs::temp_directory_path() / "okay_test_display_settings";
        fs::remove_all(dir);
        fs::create_directories(dir);
        const std::string file = (dir / "game.okayprefs").string();
        CHECK(Prefs::Save(file));
        Prefs::Clear();
        DisplaySettings::ResetToDefaults();
        ResetRenderer();
        CHECK(Prefs::Load(file));
        int gen = DisplaySettings::Generation();
        DisplaySettings::Load();                                              // values back, generation unchanged
        CHECK(DisplaySettings::Generation() == gen);
        CHECK(DisplaySettings::Mode() == WindowMode::Fullscreen && DisplaySettings::Resolution() == DisplayResolution({2560, 1440}));
        CHECK(!DisplaySettings::VSync() && DisplaySettings::Quality() == 1 && DisplaySettings::QualityChosen());
        // Restore bumps the generation, so a host that watches it applies the saved values on its first frame.
        DisplaySettings::ResetToDefaults();
        gen = DisplaySettings::Generation();
        DisplaySettings::Restore();
        CHECK(DisplaySettings::Generation() == gen + 1 && DisplaySettings::Mode() == WindowMode::Fullscreen);
        ShadowMapResolution() = 512;
        DisplaySettings::ApplyQuality();                                      // the saved level is applied
        CHECK(ShadowMapResolution() == 2048);
        fs::remove_all(dir);

        // No saved values: Restore leaves everything, and the generation, alone.
        Prefs::Clear();
        DisplaySettings::ResetToDefaults();
        gen = DisplaySettings::Generation();
        DisplaySettings::Restore();
        CHECK(DisplaySettings::Generation() == gen && DisplaySettings::Mode() == WindowMode::Windowed && !DisplaySettings::QualityChosen());

        // Older files: only video.fullscreen (it drew a borderless full screen).
        Prefs::Clear();
        Prefs::SetInt("video.fullscreen", 1);
        Prefs::SetInt("video.quality", 0);
        DisplaySettings::ResetToDefaults();
        DisplaySettings::Load();
        CHECK(DisplaySettings::Mode() == WindowMode::Borderless && DisplaySettings::Quality() == 0 && DisplaySettings::QualityChosen());
        // Bad values are ignored or clamped, never trusted.
        Prefs::Clear();
        Prefs::SetString("video.windowmode", "sideways");
        Prefs::SetInt("video.fullscreen", 0);
        Prefs::SetInt("video.width", -5);
        Prefs::SetString("video.height", "tall");
        Prefs::SetInt("video.quality", 9);
        DisplaySettings::ResetToDefaults();
        DisplaySettings::Load();
        CHECK(DisplaySettings::Mode() == WindowMode::Windowed && !DisplaySettings::HasResolution() && DisplaySettings::Quality() == 2);
        Prefs::Clear();
        Prefs::SetString("video.windowmode", "Borderless");
        Prefs::SetInt("video.width", 100);
        Prefs::SetInt("video.height", 100000);
        DisplaySettings::ResetToDefaults();
        DisplaySettings::Load();
        CHECK(DisplaySettings::Mode() == WindowMode::Borderless);
        CHECK(DisplaySettings::Width() == DisplaySettings::kMinWidth && DisplaySettings::Height() == DisplaySettings::kMaxHeight);
        // Clearing the size also clears it from the prefs.
        DisplaySettings::ClearResolution();
        CHECK(!Prefs::Has("video.width") && !Prefs::Has("video.height"));
    }

    DisplaySettings::ResetToDefaults();
    ResetRenderer();
    Prefs::Clear();
    TEST_MAIN_RESULT();
}
