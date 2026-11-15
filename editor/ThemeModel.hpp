#pragma once
// The editor's two looks, Dark and Light, as plain numbers with no UI library in sight, so they are unit tested
// (tests/test_editor_theme.cpp). EditorWidgets.cpp turns these into the ImGui style, OkayPanels.cpp into the OkayUI theme
// of the hosted panels, and main.cpp into the script editor palettes.
//
// Owner rule (2026-11-15): the editor has exactly two looks, Dark and Light; there are no blue tints anywhere in the
// chrome and no accent colour menu. So:
//   - every surface and text colour is a neutral grey (R = G = B); nothing has more blue than red,
//   - there is ONE fixed accent per look: a light graphite in Dark (a light fill with dark text on it) and a dark graphite
//     in Light (a dark fill with light text on it). It marks selections, active tabs, checks, sliders and primary buttons,
//   - the saved setting `theme` keeps its file format: 0 Dark, 1 Light. Old files also hold 2 (the removed "Classic" look)
//     and an `accent` line (the removed accent presets): 2 and anything unknown map to Dark, `accent` is ignored.
// Written for this repo from the WCAG 2 contrast formula (relative luminance of sRGB, ratio (L1 + 0.05) / (L2 + 0.05)).
// No <mutex> / <thread> here and no names that clash with the Windows headers.
#include <cmath>
#include <cstdint>
#include <string>

namespace okay::edtheme {

struct Rgb  { float r, g, b; };
struct Rgba { float r, g, b, a; };

enum class Look : int { Dark = 0, Light = 1 };

// ---- the chrome palette -----------------------------------------------------------------------------------------------
// Surfaces, darkest to lightest by luminance in BOTH looks (deep <= sunken <= base <= raised); `field` is the input fill and
// `control` the button / header fill. `ink` is the hairline and hover-lift colour (white on Dark, black on Light).
struct Palette {
    const char* name;
    Rgb deep;      // title bars, empty dock space
    Rgb sunken;    // side rails, menu / tool bars, tab strips
    Rgb base;      // panels / window background
    Rgb raised;    // cards, popups
    Rgb field;     // text inputs, drag fields
    Rgb control;   // buttons, foldout headers
    Rgb text;
    Rgb textDim;   // secondary and disabled text
    Rgb accent;    // the one accent: selections, active tab line, checks, sliders, primary buttons
    Rgb accentLit; // the accent pushed further from the surface (links, hovered / active accent text)
    Rgb accentOn;  // text drawn ON an accent fill
    Rgb ink;       // hairlines, hover lifts, scrollbar grabs
    Rgb good, warn, bad;   // status colours: ok / warning / error text (never blue)
    float playTint;        // while the game runs the chrome is mixed this far toward black (edit vs play at a glance)
};

inline constexpr Palette kDark = {
    "Dark",
    {0.075f, 0.075f, 0.075f},   // deep
    {0.095f, 0.095f, 0.095f},   // sunken
    {0.125f, 0.125f, 0.125f},   // base
    {0.155f, 0.155f, 0.155f},   // raised
    {0.175f, 0.175f, 0.175f},   // field
    {0.225f, 0.225f, 0.225f},   // control
    {0.920f, 0.920f, 0.920f},   // text
    {0.640f, 0.640f, 0.640f},   // textDim
    {0.800f, 0.800f, 0.800f},   // accent: light graphite
    {0.960f, 0.960f, 0.960f},   // accentLit
    {0.070f, 0.070f, 0.070f},   // accentOn: dark text on the light accent fill
    {1.000f, 1.000f, 1.000f},   // ink: white hairlines
    {0.450f, 0.800f, 0.520f},   // good
    {0.950f, 0.800f, 0.350f},   // warn
    {0.960f, 0.450f, 0.420f},   // bad
    0.22f,
};

inline constexpr Palette kLight = {
    "Light",
    {0.850f, 0.850f, 0.850f},   // deep
    {0.890f, 0.890f, 0.890f},   // sunken
    {0.930f, 0.930f, 0.930f},   // base
    {0.975f, 0.975f, 0.975f},   // raised
    {1.000f, 1.000f, 1.000f},   // field
    {0.850f, 0.850f, 0.850f},   // control
    {0.100f, 0.100f, 0.100f},   // text
    {0.340f, 0.340f, 0.340f},   // textDim
    {0.180f, 0.180f, 0.180f},   // accent: dark graphite
    {0.060f, 0.060f, 0.060f},   // accentLit
    {0.980f, 0.980f, 0.980f},   // accentOn: light text on the dark accent fill
    {0.000f, 0.000f, 0.000f},   // ink: black hairlines
    {0.100f, 0.460f, 0.200f},   // good
    {0.560f, 0.350f, 0.000f},   // warn
    {0.740f, 0.140f, 0.140f},   // bad
    0.07f,
};

inline constexpr const Palette& PaletteFor(Look l) { return l == Look::Light ? kLight : kDark; }

// ---- the saved setting ------------------------------------------------------------------------------------------------
// okay_settings.txt keeps `theme <int>`: 0 Dark, 1 Light. Older builds also wrote 2 (Classic) and offered 13 accent presets
// (`accent <int>`); every value that is not 1 is Dark, so an old or hand-edited file can never crash or pick a removed look.
inline constexpr Look LookFromSaved(long long saved) { return saved == 1 ? Look::Light : Look::Dark; }
inline constexpr int SavedFromLook(Look l) { return l == Look::Light ? 1 : 0; }
// The same, in the int the editor stores (g_theme): always 0 or 1.
inline constexpr int NormalizeSavedTheme(long long saved) { return SavedFromLook(LookFromSaved(saved)); }

// One `key value` line of okay_settings.txt. True when the key belongs to the theme: `theme` sets `theme` (mapped to 0 / 1),
// `accent` (the removed accent presets) is read and ignored. Any other key is not ours (false) and nothing is changed.
inline bool ApplySavedSetting(const std::string& key, long long value, int& theme) {
    if (key == "theme") { theme = NormalizeSavedTheme(value); return true; }
    if (key == "accent") return true;
    return false;
}
// The settings-file line for the current look (the `accent` line is no longer written).
inline std::string SavedThemeLine(int theme) { return "theme " + std::to_string(NormalizeSavedTheme(theme)) + "\n"; }

// ---- colour maths -----------------------------------------------------------------------------------------------------
inline float Lin(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
inline float Luminance(const Rgb& c) { return 0.2126f * Lin(c.r) + 0.7152f * Lin(c.g) + 0.0722f * Lin(c.b); }
// WCAG 2 contrast ratio, 1 (same colour) to 21 (black on white).
inline float Contrast(const Rgb& a, const Rgb& b) {
    float la = Luminance(a), lb = Luminance(b);
    if (la < lb) { const float t = la; la = lb; lb = t; }
    return (la + 0.05f) / (lb + 0.05f);
}
inline Rgb Mix(const Rgb& a, const Rgb& b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }
// The colour of `fg` (with alpha) drawn over an opaque `bg`.
inline Rgb Over(const Rgba& fg, const Rgb& bg) { return {bg.r + (fg.r - bg.r) * fg.a, bg.g + (fg.g - bg.g) * fg.a, bg.b + (fg.b - bg.b) * fg.a}; }
// The owner's "blue" test (scripts/find_blue_ui.py uses the same one): the blue channel leads red by more than `thr` and
// is larger than green. Chrome colours use a much stricter 0.02 (nothing may lean blue at all).
inline constexpr bool BlueDominant(float r, float g, float b, float thr = 0.08f) { return b > r + thr && b > g; }
inline constexpr bool BlueDominant(const Rgb& c, float thr = 0.08f) { return BlueDominant(c.r, c.g, c.b, thr); }
inline constexpr bool LeansBlue(const Rgb& c) { return c.b > c.r + 0.02f; }   // the strict chrome rule: b <= r + 0.02
inline constexpr bool IsNeutral(const Rgb& c, float eps = 0.004f) {
    return c.r - c.g <= eps && c.g - c.r <= eps && c.g - c.b <= eps && c.b - c.g <= eps;
}

// Packed 0xAABBGGRR (ImU32 / OkayUI Color32 byte order) from 0..1 floats.
inline std::uint32_t PackAbgr(const Rgba& c) {
    auto q = [](float v) { return (std::uint32_t)(std::fmin(1.0f, std::fmax(0.0f, v)) * 255.0f + 0.5f); };
    return (q(c.a) << 24) | (q(c.b) << 16) | (q(c.g) << 8) | q(c.r);
}

// ---- the script editor palette ------------------------------------------------------------------------------------------
// One per look. Neutral background, gutter, caret, selection and line highlights; the syntax colours stay colourful (that is
// what they are for) but none leans blue: types are leaf green, numbers amber, builtins magenta-purple (blue equals red).
struct CodePalette {
    const char* name;
    Rgba text, keyword, number, string, comment, punct, preproc, known, typeName, function, member;
    Rgba background, gutter, lineNumber, lineNumberCurrent;
    Rgba caret, selection, selectionInactive, currentLine, currentLineInactive, currentLineEdge;
    Rgba findMatch, bracketMatch, errorLine, errorMark, breakpoint;
    Rgba scrollTrack, scrollThumb, scrollThumbHot;
};

inline constexpr CodePalette kDarkCode = {
    "Dark",
    {0.745f, 0.745f, 0.745f, 1}, {0.839f, 0.525f, 0.275f, 1}, {0.839f, 0.698f, 0.392f, 1}, {0.471f, 0.659f, 0.408f, 1},
    {0.510f, 0.525f, 0.510f, 1}, {0.784f, 0.784f, 0.784f, 1}, {0.549f, 0.549f, 0.549f, 1}, {0.784f, 0.549f, 0.784f, 1},
    {0.659f, 0.816f, 0.431f, 1}, {1.000f, 0.776f, 0.427f, 1}, {0.808f, 0.690f, 0.808f, 1},
    {0.118f, 0.118f, 0.118f, 1}, {0.141f, 0.141f, 0.141f, 1}, {0.541f, 0.541f, 0.541f, 1}, {0.769f, 0.769f, 0.769f, 1},
    {0.910f, 0.910f, 0.910f, 1}, {1.000f, 1.000f, 1.000f, 0.190f}, {1.000f, 1.000f, 1.000f, 0.100f},
    {1.000f, 1.000f, 1.000f, 0.060f}, {1.000f, 1.000f, 1.000f, 0.030f}, {1.000f, 1.000f, 1.000f, 0.200f},
    {0.902f, 0.706f, 0.235f, 0.390f}, {1.000f, 1.000f, 1.000f, 0.240f}, {0.784f, 0.235f, 0.235f, 0.200f},
    {0.940f, 0.314f, 0.314f, 1}, {0.902f, 0.706f, 0.235f, 1},
    {0.000f, 0.000f, 0.000f, 0.160f}, {0.600f, 0.600f, 0.600f, 0.430f}, {0.700f, 0.700f, 0.700f, 0.660f},
};

inline constexpr CodePalette kLightCode = {
    "Light",
    {0.157f, 0.157f, 0.157f, 1}, {0.667f, 0.275f, 0.000f, 1}, {0.588f, 0.157f, 0.431f, 1}, {0.157f, 0.471f, 0.157f, 1},
    {0.392f, 0.424f, 0.392f, 1}, {0.275f, 0.275f, 0.275f, 1}, {0.392f, 0.392f, 0.392f, 1}, {0.549f, 0.235f, 0.549f, 1},
    {0.235f, 0.431f, 0.078f, 1}, {0.431f, 0.373f, 0.000f, 1}, {0.510f, 0.275f, 0.510f, 1},
    {0.988f, 0.988f, 0.988f, 1}, {0.945f, 0.945f, 0.945f, 1}, {0.431f, 0.431f, 0.431f, 1}, {0.157f, 0.157f, 0.157f, 1},
    {0.100f, 0.100f, 0.100f, 1}, {0.000f, 0.000f, 0.000f, 0.170f}, {0.000f, 0.000f, 0.000f, 0.090f},
    {0.000f, 0.000f, 0.000f, 0.050f}, {0.000f, 0.000f, 0.000f, 0.025f}, {0.000f, 0.000f, 0.000f, 0.180f},
    {0.800f, 0.560f, 0.000f, 0.330f}, {0.000f, 0.000f, 0.000f, 0.200f}, {0.780f, 0.157f, 0.157f, 0.140f},
    {0.780f, 0.157f, 0.157f, 1}, {0.700f, 0.470f, 0.000f, 1},
    {0.000f, 0.000f, 0.000f, 0.050f}, {0.350f, 0.350f, 0.350f, 0.400f}, {0.250f, 0.250f, 0.250f, 0.650f},
};

inline constexpr const CodePalette& CodePaletteFor(Look l) { return l == Look::Light ? kLightCode : kDarkCode; }

}  // namespace okay::edtheme
