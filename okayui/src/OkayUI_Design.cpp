// OkayUI designs (okay/UI/OkayUIDesign.hpp): the built-in designs as data, Options -> Tokens -> Theme, and appearance.json.
// Written by us. The colours were chosen by hand for this engine and checked against the WCAG 2.x contrast ratio (text
// 4.5:1 on its surfaces, see test_okayui_design); the shapes follow a 4-pixel grid. Nothing here comes from another project.
#include "okay/UI/OkayUIDesign.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>

#include "okay/Core/Json.hpp"
#include "okay/Core/Paths.hpp"
#include "OkayUIInternal.hpp"

namespace OkayUI {
namespace design {
namespace {

constexpr Rgba C(int r, int g, int b, int a = 255) { return Rgba{(unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a}; }

// ---- The designs ---------------------------------------------------------------------------------------------------------------
// Graphite: the default. Neutral charcoal surfaces that step up in lightness for each layer (elevation by surface, not by
// outline), borderless controls, an iris accent, underlined tabs, generous corners and soft shadows.
Design MakeGraphite() {
    Design d; d.name = "Graphite"; d.blurb = "Neutral charcoal, borderless controls, soft depth. The default.";
    d.hasDark = d.hasLight = true;
    Palette& k = d.dark;
    k.deep = C(13, 13, 15); k.sunken = C(18, 18, 21); k.base = C(23, 23, 26); k.raised = C(30, 30, 34); k.overlay = C(36, 36, 41);
    k.field = C(32, 32, 37); k.control = C(44, 44, 50);
    k.text = C(237, 237, 241); k.textDim = C(163, 164, 173); k.textFaint = C(118, 119, 128);
    k.hairline = C(255, 255, 255, 20); k.accent = C(110, 96, 240);
    k.success = C(74, 201, 132); k.warning = C(240, 184, 72); k.danger = C(244, 104, 104); k.info = C(102, 170, 255);
    k.onAccent = C(255, 255, 255); k.ink = C(255, 255, 255); k.tooltipBg = C(8, 8, 10, 245); k.tooltipText = C(237, 237, 241);
    Palette& l = d.light; l.light = true;
    l.deep = C(226, 227, 231); l.sunken = C(238, 238, 241); l.base = C(248, 248, 250); l.raised = C(255, 255, 255); l.overlay = C(255, 255, 255);
    l.field = C(240, 240, 243); l.control = C(232, 232, 236);
    l.text = C(22, 22, 26); l.textDim = C(86, 88, 98); l.textFaint = C(130, 132, 142);
    l.hairline = C(0, 0, 0, 26); l.accent = C(92, 76, 228);
    l.success = C(22, 128, 70); l.warning = C(150, 96, 0); l.danger = C(196, 40, 48); l.info = C(24, 96, 200);
    l.onAccent = C(255, 255, 255); l.ink = C(0, 0, 0); l.tooltipBg = C(28, 28, 32, 245); l.tooltipText = C(244, 244, 247);
    Shape& s = d.shape;
    s.radius = 7; s.radiusSmall = 5; s.radiusLarge = 12; s.borderPx = 1; s.controlBorders = false; s.shadow = 2;
    s.tabs = TabShape::Underline; s.focusRingPx = 2; s.spacing = 1; s.motionMs = 120; s.boldHeaders = false;
    return d;
}
// Slate: the launcher's look as the owner knows it (the values of shared/OkayTheme.hpp SlatePalette / LightPalette): blue
// tinted surfaces, hairline outlines on every control, pill tabs, the blue accent.
Design MakeSlate() {
    Design d; d.name = "Slate"; d.blurb = "The launcher's blue-tinted look: hairlines and pill tabs.";
    d.hasDark = d.hasLight = true;
    Palette& k = d.dark;
    k.deep = C(21, 23, 29); k.sunken = C(24, 26, 33); k.base = C(30, 33, 41); k.raised = C(38, 42, 51); k.overlay = C(35, 39, 48);
    k.field = C(41, 46, 59); k.control = C(51, 56, 71);
    k.text = C(230, 235, 245); k.textDim = C(153, 163, 184); k.textFaint = C(118, 127, 148);
    k.hairline = C(255, 255, 255, 22); k.accent = C(66, 150, 250);
    k.success = C(107, 209, 143); k.warning = C(245, 199, 87); k.danger = C(250, 133, 133); k.info = C(128, 184, 255);
    k.onAccent = C(255, 255, 255); k.ink = C(255, 255, 255); k.tooltipBg = C(16, 18, 23, 245); k.tooltipText = C(230, 235, 245);
    Palette& l = d.light; l.light = true;
    l.deep = C(219, 223, 231); l.sunken = C(233, 236, 241); l.base = C(244, 245, 248); l.raised = C(255, 255, 255); l.overlay = C(255, 255, 255);
    l.field = C(237, 239, 244); l.control = C(224, 228, 234);
    l.text = C(22, 27, 37); l.textDim = C(84, 93, 110); l.textFaint = C(120, 128, 144);
    l.hairline = C(13, 20, 36, 30); l.accent = C(40, 110, 220);
    l.success = C(18, 107, 54); l.warning = C(133, 87, 0); l.danger = C(184, 33, 41); l.info = C(26, 87, 184);
    l.onAccent = C(255, 255, 255); l.ink = C(13, 20, 36); l.tooltipBg = C(22, 27, 37, 245); l.tooltipText = C(240, 243, 248);
    Shape& s = d.shape;
    s.radius = 6; s.radiusSmall = 4; s.radiusLarge = 10; s.borderPx = 1; s.controlBorders = true; s.shadow = 1;
    s.tabs = TabShape::Pill; s.focusRingPx = 2; s.spacing = 1; s.motionMs = 100; s.boldHeaders = false;
    return d;
}
// Midnight: near-black with an indigo cast, big soft corners, deep shadows, a violet accent (dark only).
Design MakeMidnight() {
    Design d; d.name = "Midnight"; d.blurb = "Near-black indigo, rounder corners, deep shadows.";
    d.hasDark = true; d.hasLight = false;
    Palette& k = d.dark;
    k.deep = C(0, 0, 2); k.sunken = C(5, 4, 9); k.base = C(9, 8, 15); k.raised = C(20, 18, 31); k.overlay = C(26, 24, 40);
    k.field = C(24, 22, 38); k.control = C(38, 35, 59);
    k.text = C(245, 244, 255); k.textDim = C(168, 166, 201); k.textFaint = C(118, 116, 150);
    k.hairline = C(220, 214, 255, 30); k.accent = C(140, 100, 255);
    k.success = C(102, 224, 153); k.warning = C(255, 204, 92); k.danger = C(255, 133, 148); k.info = C(158, 173, 255);
    k.onAccent = C(255, 255, 255); k.ink = C(219, 214, 255); k.tooltipBg = C(30, 26, 50, 250); k.tooltipText = C(245, 244, 255);
    d.light = d.dark;
    Shape& s = d.shape;
    s.radius = 10; s.radiusSmall = 6; s.radiusLarge = 16; s.borderPx = 1; s.controlBorders = false; s.shadow = 3;
    s.tabs = TabShape::Pill; s.focusRingPx = 2; s.spacing = 1; s.motionMs = 160; s.boldHeaders = false;
    return d;
}
// High Contrast: black or white surfaces, 2-pixel outlines on every control, a thick focus ring, no shadows, near-square
// corners; text reaches 7:1 (WCAG AAA) on every surface.
Design MakeHighContrast() {
    Design d; d.name = "High Contrast"; d.blurb = "Black and white, strong outlines, a thick focus ring.";
    d.hasDark = d.hasLight = true;
    Palette& k = d.dark;
    k.deep = C(0, 0, 0); k.sunken = C(0, 0, 0); k.base = C(0, 0, 0); k.raised = C(12, 12, 12); k.overlay = C(0, 0, 0);
    k.field = C(0, 0, 0); k.control = C(0, 0, 0);
    k.text = C(255, 255, 255); k.textDim = C(224, 224, 224); k.textFaint = C(196, 196, 196);
    k.hairline = C(214, 214, 214, 255); k.accent = C(255, 214, 0);
    k.success = C(96, 255, 150); k.warning = C(255, 200, 0); k.danger = C(255, 120, 120); k.info = C(120, 205, 255);
    k.onAccent = C(0, 0, 0); k.ink = C(255, 255, 255); k.tooltipBg = C(255, 255, 255); k.tooltipText = C(0, 0, 0);
    Palette& l = d.light; l.light = true;
    l.deep = C(255, 255, 255); l.sunken = C(255, 255, 255); l.base = C(255, 255, 255); l.raised = C(245, 245, 245); l.overlay = C(255, 255, 255);
    l.field = C(255, 255, 255); l.control = C(255, 255, 255);
    l.text = C(0, 0, 0); l.textDim = C(36, 36, 36); l.textFaint = C(66, 66, 66);
    l.hairline = C(36, 36, 36, 255); l.accent = C(0, 64, 192);
    l.success = C(0, 100, 30); l.warning = C(110, 70, 0); l.danger = C(170, 0, 0); l.info = C(0, 70, 170);
    l.onAccent = C(255, 255, 255); l.ink = C(0, 0, 0); l.tooltipBg = C(0, 0, 0); l.tooltipText = C(255, 255, 255);
    Shape& s = d.shape;
    s.radius = 3; s.radiusSmall = 2; s.radiusLarge = 4; s.borderPx = 2; s.controlBorders = true; s.shadow = 0;
    s.tabs = TabShape::Underline; s.focusRingPx = 3; s.spacing = 1; s.motionMs = 60; s.boldHeaders = true;
    return d;
}
// Paper: warm off-white, flat (no shadows), thin warm outlines, square-ish corners, segmented tabs, a terracotta accent.
Design MakePaper() {
    Design d; d.name = "Paper"; d.blurb = "Warm and flat: thin outlines, segmented tabs, no shadows.";
    d.hasDark = d.hasLight = true; d.lightFirst = true;
    Palette& l = d.light; l.light = true;
    l.deep = C(226, 220, 208); l.sunken = C(238, 233, 223); l.base = C(247, 244, 237); l.raised = C(252, 250, 245); l.overlay = C(255, 253, 248);
    l.field = C(255, 253, 248); l.control = C(236, 231, 220);
    l.text = C(40, 34, 28); l.textDim = C(98, 88, 76); l.textFaint = C(136, 124, 110);
    l.hairline = C(60, 40, 20, 44); l.accent = C(186, 82, 46);
    l.success = C(40, 120, 60); l.warning = C(140, 88, 8); l.danger = C(180, 40, 40); l.info = C(40, 90, 160);
    l.onAccent = C(255, 255, 255); l.ink = C(60, 40, 20); l.tooltipBg = C(52, 44, 36, 245); l.tooltipText = C(250, 246, 238);
    Palette& k = d.dark;
    k.deep = C(20, 18, 16); k.sunken = C(26, 23, 20); k.base = C(32, 29, 25); k.raised = C(40, 36, 31); k.overlay = C(46, 41, 35);
    k.field = C(38, 34, 29); k.control = C(52, 47, 40);
    k.text = C(238, 231, 220); k.textDim = C(178, 168, 152); k.textFaint = C(130, 121, 108);
    k.hairline = C(255, 240, 220, 28); k.accent = C(226, 124, 84);
    k.success = C(120, 200, 130); k.warning = C(230, 180, 90); k.danger = C(240, 120, 110); k.info = C(130, 170, 230);
    k.onAccent = C(28, 18, 12); k.ink = C(255, 240, 220); k.tooltipBg = C(12, 10, 8, 245); k.tooltipText = C(238, 231, 220);
    Shape& s = d.shape;
    s.radius = 3; s.radiusSmall = 2; s.radiusLarge = 6; s.borderPx = 1; s.controlBorders = true; s.shadow = 0;
    s.tabs = TabShape::Segmented; s.focusRingPx = 2; s.spacing = 1; s.motionMs = 90; s.boldHeaders = true;
    return d;
}

const Design* Designs() {
    static const Design kAll[] = {MakeGraphite(), MakeSlate(), MakeMidnight(), MakeHighContrast(), MakePaper()};
    return kAll;
}
constexpr int kDesignCount = 5;

bool SameName(const char* a, const char* b) {
    if (!a || !b) return false;
    for (; *a && *b; ++a, ++b) if (std::tolower((unsigned char)*a) != std::tolower((unsigned char)*b)) return false;
    return *a == *b;
}
float Clampf(float v, float lo, float hi) { return v != v ? lo : (v < lo ? lo : (v > hi ? hi : v)); }
void Put(unsigned char d[4], Rgba c) { d[0] = c.r; d[1] = c.g; d[2] = c.b; d[3] = c.a; }
float Lin(unsigned char v) { const float f = v / 255.0f; return f <= 0.03928f ? f / 12.92f : (float)std::pow((f + 0.055) / 1.055, 2.4); }
float Lum(Rgba c) { return 0.2126f * Lin(c.r) + 0.7152f * Lin(c.g) + 0.0722f * Lin(c.b); }

// The live state.
Options g_cur;
unsigned g_rev = 1;
bool g_follow = false;
std::string g_path;
long long g_stamp = -1;
double g_lastPoll = -1.0;

long long FileStamp(const std::string& p) {
    struct stat st;
    if (p.empty() || ::stat(p.c_str(), &st) != 0) return -1;
    return (long long)st.st_mtime * 1000003LL + (long long)st.st_size;
}
} // namespace

int DesignCount() { return kDesignCount; }
const Design& DesignAt(int i) { return Designs()[i < 0 || i >= kDesignCount ? 0 : i]; }
int FindDesign(const char* name) {
    for (int i = 0; i < kDesignCount; ++i) if (SameName(Designs()[i].name, name)) return i;
    return -1;
}
int DesignIndexOf(const Options& o) { const int i = FindDesign(o.design); return i < 0 ? 0 : i; }

void Sanitize(Options& o) {
    o.design[sizeof o.design - 1] = 0;
    const int i = FindDesign(o.design);
    std::snprintf(o.design, sizeof o.design, "%s", DesignAt(i < 0 ? 0 : i).name);
    if ((int)o.scheme < 0 || (int)o.scheme > 2) o.scheme = Scheme::Auto;
    if ((int)o.density < 0 || (int)o.density > 2) o.density = Density::Comfortable;
    o.roundness = Clampf(o.roundness, 0.0f, 2.0f);
    o.uiScale = Clampf(o.uiScale, 0.75f, 2.0f);
    o.textSize = Clampf(o.textSize, 0.85f, 1.3f);
    o.accent.a = 255;
}

bool SchemeIsLight(const Options& o) {
    const Design& d = DesignAt(DesignIndexOf(o));
    if (o.scheme == Scheme::Light) return d.hasLight || !d.hasDark;
    if (o.scheme == Scheme::Dark) return !d.hasDark;
    return d.lightFirst ? d.hasLight : !d.hasDark;
}

Rgba Mix(Rgba a, Rgba b, float t) {
    t = Clampf(t, 0.0f, 1.0f);
    auto m = [t](unsigned char x, unsigned char y) { return (unsigned char)std::lround(x + (y - x) * t); };
    return Rgba{m(a.r, b.r), m(a.g, b.g), m(a.b, b.b), m(a.a, b.a)};
}
Rgba WithAlpha(Rgba c, unsigned char a) { c.a = a; return c; }
float Contrast(Rgba a, Rgba b) {
    const float la = Lum(a), lb = Lum(b);
    return la > lb ? (la + 0.05f) / (lb + 0.05f) : (lb + 0.05f) / (la + 0.05f);
}

Tokens Resolve(const Options& in) {
    Options o = in; Sanitize(o);
    const Design& d = DesignAt(DesignIndexOf(o));
    Tokens t;
    t.designName = d.name;
    const bool light = SchemeIsLight(o);
    t.c = light ? d.light : d.dark;
    t.c.light = light;
    if (o.customAccent) {
        t.c.accent = WithAlpha(o.accent, 255);
        // Text on the person's accent: whichever of white and near-black reads better on it.
        const Rgba white = C(255, 255, 255), black = C(16, 16, 20);
        t.c.onAccent = Contrast(t.c.accent, white) >= Contrast(t.c.accent, black) ? white : black;
    }
    t.shape = d.shape;
    t.shape.radius *= o.roundness; t.shape.radiusSmall *= o.roundness; t.shape.radiusLarge *= o.roundness;
    if (!o.shadows) t.shape.shadow = 0;
    if (o.reduceMotion) t.shape.motionMs = 0.0f;
    const float dens = o.density == Density::Compact ? 0.75f : o.density == Density::Spacious ? 1.3f : 1.0f;
    t.spacing = d.shape.spacing * dens;
    t.uiScale = o.uiScale;
    t.textSize = o.textSize;
    const Palette& p = t.c;
    const Rgba white = C(255, 255, 255), black = C(0, 0, 0);
    t.controlHover = Mix(p.control, WithAlpha(p.ink, 255), p.light ? 0.06f : 0.08f);
    t.controlPressed = Mix(p.control, p.deep, 0.45f);
    if (p.control.r == p.deep.r && p.control.g == p.deep.g && p.control.b == p.deep.b)   // flat black / white controls (High Contrast)
        t.controlPressed = Mix(p.control, WithAlpha(p.ink, 255), 0.18f);
    t.accentHover = Mix(p.accent, p.light ? black : white, 0.12f);
    t.accentPressed = Mix(p.accent, black, 0.15f);
    t.selection = WithAlpha(p.accent, 102);
    t.focusRing = p.light ? WithAlpha(p.accent, 230) : WithAlpha(Mix(p.accent, white, 0.25f), 220);
    t.shadowCol = C(0, 0, 0, p.light ? 40 : 96);
    return t;
}

Theme ThemeFor(const Tokens& t, bool trueType) {
    Theme th;
    const Palette& p = t.c;
    Put(th.bg, p.control);
    Put(th.bgHover, t.controlHover); Put(th.bgDown, t.controlPressed);
    Put(th.border, p.hairline); Put(th.text, p.text); Put(th.panel, p.base); Put(th.track, p.sunken); Put(th.accent, p.accent);
    Put(th.textDim, p.textDim); Put(th.titleBg, p.sunken); Put(th.field, p.field);
    th.borderPx = t.shape.borderPx;
    th.textScale = 2.0f * (trueType ? t.textSize : 1.0f);
    th.rounding = t.shape.radius;
    th.windowRounding = t.shape.radiusLarge;
    th.windowPadding = (trueType ? 14.0f : 10.0f) * t.spacing;
    th.windowShadow = t.shape.shadow > 0;
    Put(th.raised, p.raised); Put(th.overlay, p.overlay); Put(th.textFaint, p.textFaint); Put(th.onAccent, p.onAccent);
    Put(th.accentHover, t.accentHover); Put(th.focusRing, t.focusRing); Put(th.selection, t.selection); Put(th.ink, p.ink);
    Put(th.tooltipBg, p.tooltipBg); Put(th.tooltipText, p.tooltipText);
    Put(th.success, p.success); Put(th.warning, p.warning); Put(th.danger, p.danger);
    th.radiusSmall = t.shape.radiusSmall;
    th.focusRingPx = t.shape.focusRingPx;
    th.tabShape = (int)t.shape.tabs;
    th.shadowDepth = t.shape.shadow;
    th.controlBorders = t.shape.controlBorders;
    th.motionMs = t.shape.motionMs;
    th.spacing = t.spacing;
    th.lightScheme = p.light;
    th.accentHeaders = t.shape.boldHeaders;
    return th;
}
Theme DefaultTheme() { return ThemeFor(Resolve(Options()), false); }

const Options& Current() { return g_cur; }
Tokens CurrentTokens() { return Resolve(g_cur); }
unsigned Revision() { return g_rev; }

void Set(const Options& in) {
    Options o = in; Sanitize(o);
    g_cur = o;
    ++g_rev;
    // OkayUI's live theme follows, keeping what is the program's and not the design's: the pushed / set colours, the caret
    // blink choice and the text scale of a host that sized its text itself.
    Theme& cur = ui_core::g_theme;
    Theme next = ThemeFor(Resolve(o), ui_core::g_look);
    next.caretBlink = cur.caretBlink;
    std::memcpy(next.colors, cur.colors, sizeof next.colors);
    next.colorSet = cur.colorSet;
    cur = next;
}

bool DefaultPath(char* out, int cap) {
    if (!out || cap <= 0) return false;
    out[0] = 0;
    if (const char* e = std::getenv("OKAY_APPEARANCE_FILE"); e && *e) { std::snprintf(out, (size_t)cap, "%s", e); return true; }
    const std::string dir = okay::Paths::ConfigDirFor(okay::Paths::CurrentPlatform(), okay::Paths::Env::Current(), "OkaySpace");
    if (dir.empty()) return false;
    const std::string p = okay::Paths::Join(dir, "appearance.json");
    if ((int)p.size() + 1 > cap) return false;
    std::snprintf(out, (size_t)cap, "%s", p.c_str());
    return true;
}

bool ParseJson(const char* text, Options* out) {
    if (!text || !out) return false;
    okay::json::Value v;
    if (!okay::json::Parse(text, v, nullptr) || !v.IsObject()) return false;
    Options o = *out;
    auto num = [&](const char* k, float& f) { if (const okay::json::Value* x = v.Get(k)) if (x->IsNumber()) f = (float)x->number; };
    auto flag = [&](const char* k, bool& b) { if (const okay::json::Value* x = v.Get(k)) if (x->IsBool()) b = x->boolean; };
    auto word = [&](const char* k) -> const char* { const okay::json::Value* x = v.Get(k); return x && x->IsString() ? x->string.c_str() : nullptr; };
    if (const char* s = word("design")) std::snprintf(o.design, sizeof o.design, "%s", s);
    if (const char* s = word("scheme")) o.scheme = SameName(s, "dark") ? Scheme::Dark : SameName(s, "light") ? Scheme::Light : Scheme::Auto;
    if (const char* s = word("density")) o.density = SameName(s, "compact") ? Density::Compact : SameName(s, "spacious") ? Density::Spacious : Density::Comfortable;
    if (const char* s = word("accent")) {
        unsigned r = 0, g = 0, b = 0;
        if (s[0] == '#' && std::strlen(s) == 7 && std::sscanf(s + 1, "%2x%2x%2x", &r, &g, &b) == 3) { o.customAccent = true; o.accent = C((int)r, (int)g, (int)b); }
        else o.customAccent = false;   // "design" (or anything else): the design's own accent
    }
    num("roundness", o.roundness); num("uiScale", o.uiScale); num("textSize", o.textSize);
    flag("shadows", o.shadows); flag("reduceMotion", o.reduceMotion);
    Sanitize(o);
    *out = o;
    return true;
}

int WriteJson(const Options& in, char* out, int cap) {
    Options o = in; Sanitize(o);
    char acc[16];
    if (o.customAccent) std::snprintf(acc, sizeof acc, "#%02x%02x%02x", o.accent.r, o.accent.g, o.accent.b);
    else std::snprintf(acc, sizeof acc, "design");
    std::string name;
    okay::json::Escape(o.design, name);
    const char* scheme = o.scheme == Scheme::Dark ? "dark" : o.scheme == Scheme::Light ? "light" : "auto";
    const char* dens = o.density == Density::Compact ? "compact" : o.density == Density::Spacious ? "spacious" : "comfortable";
    const int n = std::snprintf(out, out ? (size_t)cap : 0,
        "{\n  \"design\": %s,\n  \"scheme\": \"%s\",\n  \"accent\": \"%s\",\n  \"density\": \"%s\",\n  \"roundness\": %.2f,\n"
        "  \"uiScale\": %.2f,\n  \"textSize\": %.2f,\n  \"shadows\": %s,\n  \"reduceMotion\": %s\n}\n",
        name.c_str(), scheme, acc, dens, (double)o.roundness, (double)o.uiScale, (double)o.textSize,
        o.shadows ? "true" : "false", o.reduceMotion ? "true" : "false");
    return n < 0 || n >= cap ? -1 : n;
}

bool Load(const char* path, Options* out) {
    if (!path || !*path || !out) return false;
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::string text; char buf[1024]; std::size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0 && text.size() < 65536) text.append(buf, n);
    std::fclose(f);
    return ParseJson(text.c_str(), out);
}

bool Save(const char* path, const Options& o) {
    if (!path || !*path) return false;
    char text[1024];
    if (WriteJson(o, text, (int)sizeof text) < 0) return false;
    const std::string p(path);
    const std::size_t slash = p.find_last_of("/\\");
    if (slash != std::string::npos) okay::Paths::EnsureDir(p.substr(0, slash));
    const std::string tmp = p + ".tmp";
    std::FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fputs(text, f) >= 0;
    if (std::fclose(f) != 0 || !ok) { std::remove(tmp.c_str()); return false; }
    std::remove(p.c_str());   // Windows' rename does not replace
    if (std::rename(tmp.c_str(), p.c_str()) != 0) { std::remove(tmp.c_str()); return false; }
    return true;
}

bool FollowUserFile() {
    char p[1024];
    g_follow = true;
    g_path = DefaultPath(p, (int)sizeof p) ? p : "";
    Options o = g_cur;
    const bool read = Load(g_path.c_str(), &o);
    if (read) Set(o);
    g_stamp = FileStamp(g_path);
    return read;
}
bool FollowingUserFile() { return g_follow; }

bool SaveUserFile() {
    if (!g_follow || g_path.empty()) return false;
    const bool ok = Save(g_path.c_str(), g_cur);
    g_stamp = FileStamp(g_path);
    return ok;
}

bool Poll(double now) {
    if (!g_follow || g_path.empty()) return false;
    if (g_lastPoll >= 0.0 && now >= g_lastPoll && now - g_lastPoll < 0.5) return false;
    g_lastPoll = now;
    const long long st = FileStamp(g_path);
    if (st == g_stamp) return false;
    g_stamp = st;
    Options o = g_cur;
    if (!Load(g_path.c_str(), &o)) return false;
    char a[1024], b[1024];
    if (WriteJson(o, a, (int)sizeof a) >= 0 && WriteJson(g_cur, b, (int)sizeof b) >= 0 && std::strcmp(a, b) == 0) return false;
    Set(o);
    return true;
}

} // namespace design
} // namespace OkayUI
