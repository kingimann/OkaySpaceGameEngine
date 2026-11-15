#include "test_framework.hpp"
#include "ThemeModel.hpp"
#include <climits>
#include <sstream>
#include <string>

// The editor's two looks (editor/ThemeModel.hpp): every palette colour is neutral (no blue), text and controls read against
// every surface (WCAG AA: 4.5:1 for text, 3:1 for controls and selections), Light really is the inverse of Dark, and an old
// okay_settings.txt (theme 0 / 1 / 2, accent 0..12) maps to the two looks without a crash.
using namespace okay::edtheme;

namespace {
const Rgb* Surfaces(const Palette& p, int& n) {
    static Rgb s[6];
    s[0] = p.deep; s[1] = p.sunken; s[2] = p.base; s[3] = p.raised; s[4] = p.field; s[5] = p.control;
    n = 6;
    return s;
}
const char* const kSurfaceName[6] = {"deep", "sunken", "base", "raised", "field", "control"};

void ChromePalette(const Palette& p) {
    std::cout << "  palette " << p.name << "\n";
    const Rgb all[] = {p.deep, p.sunken, p.base, p.raised, p.field, p.control, p.text, p.textDim, p.accent, p.accentLit, p.accentOn, p.ink};
    for (const Rgb& c : all) {
        CHECK(!LeansBlue(c));        // strict chrome rule: b <= r + 0.02
        CHECK(!BlueDominant(c));     // and the owner's looser one
        CHECK(IsNeutral(c));         // every chrome colour is a pure grey
        CHECK(c.r >= 0.0f && c.r <= 1.0f && c.g >= 0.0f && c.g <= 1.0f && c.b >= 0.0f && c.b <= 1.0f);
    }
    // Status colours are colourful on purpose but never blue.
    const Rgb status[] = {p.good, p.warn, p.bad};
    for (const Rgb& c : status) CHECK(!BlueDominant(c));   // (a green may have a little more blue than red: it is still green)

    int n = 0;
    const Rgb* s = Surfaces(p, n);
    for (int i = 0; i < n; ++i) {
        const float tc = Contrast(p.text, s[i]), dc = Contrast(p.textDim, s[i]), ac = Contrast(p.accent, s[i]);
        if (tc < 4.5f || dc < 4.5f || ac < 3.0f) std::cerr << "    surface " << kSurfaceName[i] << ": text " << tc << " dim " << dc << " accent " << ac << "\n";
        CHECK(tc >= 4.5f);                              // text on every surface
        CHECK(dc >= 4.5f);                              // secondary / disabled text too
        CHECK(ac >= 3.0f);                              // the accent (selection bars, checks, sliders) on every surface
        CHECK(Contrast(p.accentLit, s[i]) >= 3.0f);
        const float g = Contrast(p.good, s[i]), w = Contrast(p.warn, s[i]), b = Contrast(p.bad, s[i]);
        if (g < 3.0f || w < 3.0f || b < 3.0f) std::cerr << "    surface " << kSurfaceName[i] << ": good " << g << " warn " << w << " bad " << b << "\n";
        CHECK(g >= 3.0f); CHECK(w >= 3.0f); CHECK(b >= 3.0f);
    }
    CHECK(Contrast(p.good, p.base) >= 4.5f);            // status text on the panel itself reads as text
    CHECK(Contrast(p.warn, p.base) >= 4.5f);
    CHECK(Contrast(p.bad, p.base) >= 4.5f);
    CHECK(Contrast(p.accentOn, p.accent) >= 4.5f);      // text on an accent fill (primary buttons, active toolbar toggles)
    // A hovered / pressed accent fill is the accent at 100 / 80 % over the panel: text on it must still read.
    CHECK(Contrast(p.accentOn, Over({p.accent.r, p.accent.g, p.accent.b, 0.80f}, p.base)) >= 4.5f);
    // The surface order is the same in both looks: deep <= sunken <= base <= raised by luminance.
    CHECK(Luminance(p.deep) <= Luminance(p.sunken));
    CHECK(Luminance(p.sunken) <= Luminance(p.base));
    CHECK(Luminance(p.base) <= Luminance(p.raised));
    CHECK(p.playTint > 0.0f && p.playTint < 0.5f);
    // A selected row (header colour: control mixed toward the accent) still reads with normal text on it.
    const Rgb sel = Mix(p.control, p.accent, 0.18f);
    CHECK(Contrast(p.text, sel) >= 4.5f);
    CHECK(Contrast(p.text, Mix(p.control, p.accent, 0.28f)) >= 4.5f);   // ... and while pressed
    // The selection wash AccentCol(0.28) over the panel is visibly different from the panel (a selection you can see)...
    const Rgb wash = Over({p.accent.r, p.accent.g, p.accent.b, 0.28f}, p.base);
    CHECK(Contrast(wash, p.base) >= 1.25f);
    // ... and text over it still reads.
    CHECK(Contrast(p.text, wash) >= 4.5f);
}

void CodePal(const CodePalette& c, const Palette& chrome) {
    std::cout << "  code palette " << c.name << "\n";
    const Rgb bg{c.background.r, c.background.g, c.background.b};
    const Rgba syntax[] = {c.text, c.keyword, c.number, c.string, c.comment, c.punct, c.preproc, c.known, c.typeName, c.function, c.member};
    for (const Rgba& s : syntax) {
        const Rgb v{s.r, s.g, s.b};
        CHECK(!BlueDominant(v));         // colourful is fine, blue is not
        CHECK(!LeansBlue(v));            // (purples are magenta-purple: blue equals red)
        CHECK(s.a == 1.0f);
        const float cr = Contrast(v, bg);
        if (cr < 4.5f) std::cerr << "    token contrast " << cr << " (" << v.r << "," << v.g << "," << v.b << ")\n";
        CHECK(cr >= 4.5f);
    }
    // Background, gutter, line numbers and the caret are neutral greys.
    const Rgba greys[] = {c.background, c.gutter, c.lineNumber, c.lineNumberCurrent, c.caret, c.scrollTrack, c.scrollThumb, c.scrollThumbHot};
    for (const Rgba& g : greys) CHECK(IsNeutral({g.r, g.g, g.b}));
    // Overlays (selection, line highlights, find / bracket / error / breakpoint marks): no blue, ever. (Warm marks are fine.)
    const Rgba overlays[] = {c.selection, c.selectionInactive, c.currentLine, c.currentLineInactive, c.currentLineEdge,
                             c.findMatch, c.bracketMatch, c.errorLine, c.errorMark, c.breakpoint};
    for (const Rgba& o : overlays) { CHECK(!BlueDominant({o.r, o.g, o.b})); CHECK(!LeansBlue({o.r, o.g, o.b})); }
    CHECK(Contrast({c.lineNumber.r, c.lineNumber.g, c.lineNumber.b}, bg) >= 4.5f);
    CHECK(Contrast({c.lineNumberCurrent.r, c.lineNumberCurrent.g, c.lineNumberCurrent.b}, bg) >= 4.5f);
    CHECK(Contrast({c.caret.r, c.caret.g, c.caret.b}, bg) >= 4.5f);
    // Text stays readable on the selection and on the current-line highlight.
    const Rgb onSel = Over(c.selection, bg), onLine = Over(c.currentLine, bg);
    const Rgb txt{c.text.r, c.text.g, c.text.b};
    if (Contrast(txt, onSel) < 4.5f) std::cerr << "    text on selection " << Contrast(txt, onSel) << "\n";
    CHECK(Contrast(txt, onSel) >= 4.5f);
    CHECK(Contrast(txt, onLine) >= 4.5f);
    // The selection is visible against the plain background.
    CHECK(Contrast(onSel, bg) >= 1.3f);
    // The code background sits next to the chrome: it is within the same family (neutral) and close in luminance.
    CHECK(std::fabs(Luminance(bg) - Luminance(chrome.base)) < 0.2f);
}
}  // namespace

int main() {
    RUN_SUITE("editor_theme");

    {   // Legacy saved values: 0 and 2 (Slate / Classic) -> Dark, 1 -> Light; anything else -> Dark.
        CHECK(LookFromSaved(0) == Look::Dark);
        CHECK(LookFromSaved(1) == Look::Light);
        CHECK(LookFromSaved(2) == Look::Dark);          // the removed Classic look
        CHECK(LookFromSaved(-1) == Look::Dark);
        CHECK(LookFromSaved(3) == Look::Dark);
        CHECK(LookFromSaved(INT_MAX) == Look::Dark);
        CHECK(LookFromSaved(INT_MIN) == Look::Dark);
        CHECK(LookFromSaved(LLONG_MAX) == Look::Dark);
        CHECK(LookFromSaved(LLONG_MIN) == Look::Dark);
        CHECK(NormalizeSavedTheme(0) == 0 && NormalizeSavedTheme(1) == 1 && NormalizeSavedTheme(2) == 0 && NormalizeSavedTheme(77) == 0);
        CHECK(SavedFromLook(Look::Dark) == 0 && SavedFromLook(Look::Light) == 1);
        CHECK(&PaletteFor(Look::Dark) == &kDark && &PaletteFor(Look::Light) == &kLight);
        CHECK(&CodePaletteFor(Look::Dark) == &kDarkCode && &CodePaletteFor(Look::Light) == &kLightCode);
    }
    {   // An old okay_settings.txt, read the way the editor reads it ("key value" pairs until one does not parse).
        auto load = [](const std::string& text, int& theme, int& ignoredAccentLines) {
            std::istringstream f(text);
            std::string k; long long v;
            while (f >> k >> v) {
                if (k == "accent") ++ignoredAccentLines;
                ApplySavedSetting(k, v, theme);
            }
        };
        int theme = 0, acc = 0;
        load("settingsver 6\nautoupdate 1\ntheme 0\naccent 7\nuiscalepct 100\n", theme, acc);   // old Slate + Slate accent
        CHECK(theme == 0 && acc == 1);
        theme = 5; acc = 0;
        load("settingsver 6\ntheme 1\naccent 12\nuiscalepct 100\n", theme, acc);                 // old Light
        CHECK(theme == 1 && acc == 1);
        theme = 1; acc = 0;
        load("theme 2\naccent 0\n", theme, acc);                                                 // old Classic -> Dark
        CHECK(theme == 0 && acc == 1);
        theme = 1; acc = 0;
        load("theme 999\naccent -4\n", theme, acc);                                              // damaged values -> Dark, no crash
        CHECK(theme == 0);
        theme = 1; acc = 0;
        load("theme -7\n", theme, acc);
        CHECK(theme == 0);
        theme = 1; acc = 0;
        load("theme 1\ntheme abc\naccent 3\n", theme, acc);                                      // garbage stops the reader; what came before stays
        CHECK(theme == 1);
        theme = 0; acc = 0;
        load("", theme, acc);                                                                    // empty file: unchanged
        CHECK(theme == 0 && acc == 0);
        int t = 3;
        CHECK(!ApplySavedSetting("uiscalepct", 100, t) && t == 3);                              // not our key: nothing touched
        CHECK(ApplySavedSetting("accent", 9, t) && t == 3);                                      // accent is consumed and ignored
        CHECK(ApplySavedSetting("theme", 2, t) && t == 0);
        // What is written: only `theme`, never `accent` again, and it reads back to the same look.
        CHECK(SavedThemeLine(0) == "theme 0\n");
        CHECK(SavedThemeLine(1) == "theme 1\n");
        CHECK(SavedThemeLine(2) == "theme 0\n");
        CHECK(SavedThemeLine(1).find("accent") == std::string::npos);
        int back = 9; load(SavedThemeLine(1), back, acc); CHECK(back == 1);
    }
    {   // Colour maths.
        const Rgb black{0, 0, 0}, white{1, 1, 1};
        CHECK_NEAR(Contrast(black, white), 21.0, 0.01);
        CHECK_NEAR(Contrast(white, black), 21.0, 0.01);
        CHECK_NEAR(Contrast(white, white), 1.0, 1e-6);
        CHECK_NEAR(Luminance(white), 1.0, 1e-4);
        CHECK_NEAR(Luminance(black), 0.0, 1e-6);
        CHECK_NEAR(Contrast({0.5f, 0.5f, 0.5f}, white), 3.95, 0.05);   // mid grey on white: ~3.95:1 (known value for #808080 is 3.95)
        // The owner's blue test: the old Slate navy and the old accent are blue, a neutral grey and a warm colour are not.
        CHECK(BlueDominant(0.26f, 0.59f, 0.98f));       // the old default accent
        CHECK(BlueDominant(0.082f, 0.090f, 0.114f, 0.02f));   // the old kDeep: blue by the strict rule
        CHECK(!BlueDominant(0.5f, 0.5f, 0.5f));
        CHECK(!BlueDominant(0.95f, 0.66f, 0.26f));      // amber
        CHECK(!BlueDominant(0.30f, 0.78f, 0.30f));      // green
        CHECK(LeansBlue({0.118f, 0.129f, 0.161f}));     // the old kBase
        CHECK(!LeansBlue(kDark.base) && !LeansBlue(kLight.base));
        CHECK(PackAbgr({1, 0, 0, 1}) == 0xff0000ffu);   // red, opaque, in ImU32 byte order
        CHECK(PackAbgr({0, 0, 1, 0.5f}) == 0x80ff0000u);
        CHECK(PackAbgr({2, -1, 0.5f, 1}) == 0xff8000ffu);   // out-of-range channels are clamped
        const Rgb o = Over({1, 1, 1, 0.5f}, black);
        CHECK_NEAR(o.r, 0.5, 1e-6);
    }
    std::cout << "[chrome palettes]\n";
    ChromePalette(kDark);
    ChromePalette(kLight);
    {   // Light inverts Dark sensibly.
        CHECK(Luminance(kLight.base) > Luminance(kDark.base));
        CHECK(Luminance(kLight.text) < Luminance(kDark.text));
        CHECK(Luminance(kDark.text) > Luminance(kDark.base) && Luminance(kLight.text) < Luminance(kLight.base));   // text is the high-contrast side in both
        CHECK(Luminance(kDark.accent) > Luminance(kDark.base));     // Dark: a light accent over dark surfaces ...
        CHECK(Luminance(kLight.accent) < Luminance(kLight.base));   // ... Light: a dark accent over light surfaces
        CHECK(Luminance(kDark.accentOn) < Luminance(kDark.accent) && Luminance(kLight.accentOn) > Luminance(kLight.accent));   // text on the accent flips too
        CHECK(kDark.ink.r == 1.0f && kLight.ink.r == 0.0f);          // white hairlines on dark, black on light
        // The two looks are actually different everywhere that matters.
        CHECK(std::fabs(Luminance(kDark.base) - Luminance(kLight.base)) > 0.5f);
        CHECK(std::fabs(Luminance(kDark.field) - Luminance(kLight.field)) > 0.5f);
        CHECK(Luminance(kLight.raised) > Luminance(kLight.base) && Luminance(kLight.field) >= Luminance(kLight.raised));   // light inputs are the brightest
        CHECK(Luminance(kDark.field) > Luminance(kDark.base));       // dark inputs are lighter than the panel (they read as wells)
    }
    std::cout << "[code palettes]\n";
    CodePal(kDarkCode, kDark);
    CodePal(kLightCode, kLight);
    {   // The two code palettes also invert.
        CHECK(Luminance({kLightCode.background.r, kLightCode.background.g, kLightCode.background.b}) >
              Luminance({kDarkCode.background.r, kDarkCode.background.g, kDarkCode.background.b}) + 0.5f);
        CHECK(Luminance({kLightCode.text.r, kLightCode.text.g, kLightCode.text.b}) <
              Luminance({kDarkCode.text.r, kDarkCode.text.g, kDarkCode.text.b}));
    }
    TEST_MAIN_RESULT();
}
