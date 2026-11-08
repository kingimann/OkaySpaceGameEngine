// Accessibility: colour vision simulation and correction, the safe palette and contrast helpers,
// subtitle timing / queue / priority / settings, SubRip tracks, and the accessibility settings
// with their change callbacks and persistence.
#include "test_framework.hpp"
#include "okay/Audio/AudioClip.hpp"
#include "okay/Core/AccessibilitySettings.hpp"
#include "okay/Core/Localization.hpp"
#include "okay/Core/Prefs.hpp"
#include "okay/Core/Subtitles.hpp"
#include "okay/Graphics/ColorVision.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace okay;

static bool Near(const Color& c, float r, float g, float b, float eps = 0.005f) {
    return std::fabs(c.r - r) <= eps && std::fabs(c.g - g) <= eps && std::fabs(c.b - b) <= eps;
}

int main() {
    RUN_SUITE("accessibility");
    Prefs::Clear();
    Localization::Reset();
    Accessibility::Reset();

    // ---- Colour vision simulation -----------------------------------------------------------------
    {
        const ColorblindMode modes[] = {ColorblindMode::Protanopia, ColorblindMode::Deuteranopia, ColorblindMode::Tritanopia,
                                        ColorblindMode::Achromatopsia};
        for (ColorblindMode m : modes) {
            const ColorMatrix3 mat = ColorblindMatrix(m);
            for (int row = 0; row < 3; ++row)
                CHECK_NEAR(mat[(std::size_t)row * 3] + mat[(std::size_t)row * 3 + 1] + mat[(std::size_t)row * 3 + 2], 1.0, 1e-4);   // white stays white
            const Color white = SimulateColorblind(Color::White, m), gray = SimulateColorblind(Color(0.5f, 0.5f, 0.5f), m);
            CHECK(Near(white, 1, 1, 1, 0.002f) && Near(gray, 0.5f, 0.5f, 0.5f, 0.004f));
            CHECK(Near(SimulateColorblind(Color::Black, m), 0, 0, 0, 1e-4f));
            const Color half = SimulateColorblind(Color(0.9f, 0.2f, 0.4f, 0.25f), m);
            CHECK_NEAR(half.a, 0.25, 1e-6);                                   // alpha is never changed
        }
        const ColorMatrix3 id = ColorblindMatrix(ColorblindMode::None);
        CHECK(id[0] == 1 && id[4] == 1 && id[8] == 1 && id[1] == 0 && id[3] == 0);
        const Color c(0.3f, 0.6f, 0.9f);
        CHECK(Near(SimulateColorblind(c, ColorblindMode::None), 0.3f, 0.6f, 0.9f, 1e-6f));
        CHECK(Near(SimulateColorblind(c, ColorblindMode::Protanopia, 0.0f), 0.3f, 0.6f, 0.9f, 1e-6f));

        // Known colours (Machado matrices on linear light, sRGB out).
        CHECK(Near(SimulateColorblind(Color::Red, ColorblindMode::Protanopia), 0.4266f, 0.3727f, 0.0f));
        CHECK(Near(SimulateColorblind(Color::Green, ColorblindMode::Protanopia), 1.0f, 0.8994f, 0.0f));
        CHECK(Near(SimulateColorblind(Color::Red, ColorblindMode::Deuteranopia), 0.6401f, 0.5658f, 0.0f));
        CHECK(Near(SimulateColorblind(Color::Blue, ColorblindMode::Deuteranopia), 0.0f, 0.2412f, 0.9862f));
        CHECK(Near(SimulateColorblind(Color::Blue, ColorblindMode::Tritanopia), 0.0f, 0.4204f, 0.5873f));
        CHECK(Near(SimulateColorblind(Color::Red, ColorblindMode::Achromatopsia), 0.4984f, 0.4984f, 0.4984f));
        CHECK(Near(SimulateColorblind(Color::Green, ColorblindMode::Achromatopsia), 0.8625f, 0.8625f, 0.8625f));
        // Half severity: halfway between the identity and the full matrix, in linear light.
        CHECK(Near(SimulateColorblind(Color::Red, ColorblindMode::Protanopia, 0.5f), 0.7834f, 0.2654f, 0.0f));
        // Red and green, hard to tell apart with deuteranopia and protanopia, are far apart in normal vision.
        CHECK(ColorDistance(Color::Red, Color::Green) > 80.0f);
        CHECK(ColorDistance(SimulateColorblind(Color(0.8f, 0.31f, 0.16f), ColorblindMode::Deuteranopia),
                            SimulateColorblind(Color(0.39f, 0.63f, 0.16f), ColorblindMode::Deuteranopia)) <
              ColorDistance(Color(0.8f, 0.31f, 0.16f), Color(0.39f, 0.63f, 0.16f)) * 0.3f);

        // Names
        ColorblindMode m = ColorblindMode::None;
        CHECK(ColorblindFromName("Deutan", m) && m == ColorblindMode::Deuteranopia);
        CHECK(ColorblindFromName("mono", m) && m == ColorblindMode::Achromatopsia);
        CHECK(ColorblindFromName("off", m) && m == ColorblindMode::None);
        CHECK(!ColorblindFromName("purple", m) && m == ColorblindMode::None);
        CHECK(std::string(ColorblindName(ColorblindMode::Tritanopia)) == "tritanopia");

        // Images: the same maths per pixel, alpha kept, copies leave the source alone.
        Image img(2, 2);
        img.SetPixel(0, 0, Color(1, 0, 0, 1));
        img.SetPixel(1, 0, Color(0, 1, 0, 0.5f));
        img.SetPixel(0, 1, Color(0, 0, 1, 1));
        img.SetPixel(1, 1, Color(0.5f, 0.5f, 0.5f, 0));
        const Image seen = SimulatedColorblind(img, ColorblindMode::Deuteranopia);
        CHECK(img.GetPixel(0, 0).r == 1.0f && img.GetPixel(0, 0).g == 0.0f);   // untouched
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x) {
                const Color want = SimulateColorblind(img.GetPixel(x, y), ColorblindMode::Deuteranopia);
                const Color got = seen.GetPixel(x, y);
                CHECK(Near(got, want.r, want.g, want.b, 0.012f));
                CHECK_NEAR(got.a, img.GetPixel(x, y).a, 1.0 / 255.0);
            }
        Image inPlace = img;
        SimulateColorblind(inPlace, ColorblindMode::Deuteranopia);
        CHECK(inPlace.GetPixel(0, 0).r == seen.GetPixel(0, 0).r && inPlace.GetPixel(1, 0).g == seen.GetPixel(1, 0).g);
        Image none = img;
        SimulateColorblind(none, ColorblindMode::None);
        CHECK(none.Pixels() == img.Pixels());
        Image empty;
        SimulateColorblind(empty, ColorblindMode::Protanopia);   // nothing to do, no crash
        CHECK(!empty.Valid());
    }

    // ---- Correction (daltonize) -------------------------------------------------------------------------
    {
        const Color a(200 / 255.0f, 80 / 255.0f, 40 / 255.0f), b(100 / 255.0f, 160 / 255.0f, 40 / 255.0f);   // a red-brown and an olive
        for (ColorblindMode m : {ColorblindMode::Protanopia, ColorblindMode::Deuteranopia}) {
            const float before = ColorDistance(SimulateColorblind(a, m), SimulateColorblind(b, m));
            const float after = ColorDistance(SimulateColorblind(DaltonizeColor(a, m), m), SimulateColorblind(DaltonizeColor(b, m), m));
            CHECK(after > before + 10.0f);   // the pair is easier to tell apart once corrected
        }
        // Tritanopia: a blue and a teal that merge get further apart.
        const Color tb(0.1f, 0.4f, 0.9f), tt(0.1f, 0.8f, 0.8f);
        CHECK(ColorDistance(SimulateColorblind(DaltonizeColor(tb, ColorblindMode::Tritanopia), ColorblindMode::Tritanopia),
                            SimulateColorblind(DaltonizeColor(tt, ColorblindMode::Tritanopia), ColorblindMode::Tritanopia)) > 0.0f);
        const Color r = DaltonizeColor(a, ColorblindMode::Protanopia, 0.0f);
        CHECK(Near(r, a.r, a.g, a.b, 1e-6f));
        CHECK(Near(DaltonizeColor(a, ColorblindMode::None), a.r, a.g, a.b, 1e-6f));
        CHECK(Near(DaltonizeColor(a, ColorblindMode::Achromatopsia), a.r, a.g, a.b, 1e-6f));   // no hue to recover
        const Color half = DaltonizeColor(a, ColorblindMode::Deuteranopia, 0.5f), full = DaltonizeColor(a, ColorblindMode::Deuteranopia, 1.0f);
        CHECK(std::fabs(half.g - a.g) < std::fabs(full.g - a.g) + 1e-6f && std::fabs(full.g - a.g) > 0.01f);
        CHECK(DaltonizeColor(Color(1, 0, 0, 0.3f), ColorblindMode::Protanopia).a == 0.3f);
        Image img(1, 2);
        img.SetPixel(0, 0, a);
        img.SetPixel(0, 1, b);
        const Image fixed = DaltonizedImage(img, ColorblindMode::Deuteranopia);
        const Color want = DaltonizeColor(a, ColorblindMode::Deuteranopia);
        CHECK(Near(fixed.GetPixel(0, 0), want.r, want.g, want.b, 0.012f));
        CHECK(img.GetPixel(0, 0).r != fixed.GetPixel(0, 0).r || img.GetPixel(0, 0).g != fixed.GetPixel(0, 0).g);
    }

    // ---- The safe palette and contrast -------------------------------------------------------------------------
    {
        const auto& pal = SafePalette();
        CHECK(pal.size() == 12);
        for (std::size_t i = 0; i < pal.size(); ++i)
            for (std::size_t j = i + 1; j < pal.size(); ++j) CHECK(ColorDistance(pal[i], pal[j]) > 18.0f);   // distinct to normal eyes
        for (ColorblindMode m : {ColorblindMode::None, ColorblindMode::Protanopia, ColorblindMode::Deuteranopia,
                                 ColorblindMode::Tritanopia})
            CHECK(MinColorSeparation(pal, m) > 18.0f);                    // and under each dichromacy
        const std::vector<Color> first(pal.begin(), pal.begin() + 8);
        CHECK(MinColorSeparation(first, ColorblindMode::Deuteranopia) > 20.0f);   // the first n stay well apart too
        for (const Color& c : pal) CHECK(ContrastRatio(c, Color::Black) >= 3.5f);   // readable on dark backgrounds
        CHECK(SafePaletteColor(0).r == pal[0].r && SafePaletteColor(12).g == pal[0].g && SafePaletteColor(-1).b == pal[11].b);
        // The default Red / Green / Blue are not a safe set: some pair merges under a deficiency.
        CHECK(MinColorSeparation({Color::Red, Color::Green}, ColorblindMode::Deuteranopia) < 40.0f);
        CHECK(MinColorSeparation({Color::Red}, ColorblindMode::None) == 0.0f);

        CHECK_NEAR(ContrastRatio(Color::Black, Color::White), 21.0, 0.01);
        CHECK_NEAR(ContrastRatio(Color::White, Color::White), 1.0, 1e-5);
        CHECK_NEAR(RelativeLuminance(Color::White), 1.0, 1e-4);
        const Color dim(0.3f, 0.3f, 0.3f, 0.5f);
        const Color lifted = EnsureContrast(dim, Color::Black, 4.5f);
        CHECK(ContrastRatio(lifted, Color::Black) >= 4.5f && lifted.r > dim.r && lifted.a == 0.5f);
        CHECK(lifted.r < 0.7f);                                              // moved only as far as needed
        const Color ok = EnsureContrast(Color::White, Color::Black, 4.5f);
        CHECK(ok.r == 1.0f && ok.g == 1.0f);                                 // already enough: unchanged
        const Color darker = EnsureContrast(Color(0.6f, 0.6f, 0.6f), Color::White, 4.5f);
        CHECK(ContrastRatio(darker, Color::White) >= 4.5f && darker.r < 0.6f);
        const Color best = EnsureContrast(Color(0.5f, 0.5f, 0.5f), Color(0.5f, 0.5f, 0.5f), 30.0f);   // can't reach 30:1
        CHECK((best.r == 0.0f || best.r == 1.0f));
        CHECK(std::fabs(SrgbToLinear(LinearToSrgb(0.2f)) - 0.2f) < 1e-4f);
    }

    // ---- Subtitle timing and wrapping -----------------------------------------------------------------------
    {
        CHECK_NEAR(CaptionManager::DurationFor("anything", 2.0f, 0.25f), 2.25, 1e-6);    // the audio's length plus a moment
        CHECK_NEAR(CaptionManager::DurationFor("Hi"), 1.5, 1e-6);                         // at least 1.5 s
        CHECK_NEAR(CaptionManager::DurationFor(std::string(60, 'x')), 4.0, 1e-6);         // 15 cells a second
        CHECK_NEAR(CaptionManager::DurationFor(std::string(300, 'x')), 8.0, 1e-6);        // at most 8 s
        CHECK(CaptionManager::Cells("日本語") == 6 && CaptionManager::Cells("e\xCC\x81") == 1 && CaptionManager::Cells("abc") == 3);

        auto joined = [](const std::vector<std::string>& rows) {
            std::string s;
            for (const auto& r : rows) { if (!s.empty()) s += ' '; s += r; }
            return s;
        };
        const std::string quick = "the quick brown fox jumps over the lazy dog";
        const auto rows = CaptionManager::Wrap(quick, 16);
        CHECK(rows.size() == 3 && joined(rows) == quick);
        for (const auto& r : rows) CHECK(CaptionManager::Cells(r) <= 16);
        CHECK(CaptionManager::Wrap("abcdefghijklmnopqrstuvwxyz", 10) == std::vector<std::string>({"abcdefghij", "klmnopqrst", "uvwxyz"}));
        CHECK(CaptionManager::Wrap("hello world foo", 14, 6) == std::vector<std::string>({"hello", "world foo"}));   // after a 6-cell speaker label
        CHECK(CaptionManager::Wrap("a\nb", 10) == std::vector<std::string>({"a", "b"}));
        CHECK(CaptionManager::Wrap("日本語のテキスト", 8) == std::vector<std::string>({"日本語の", "テキスト"}));
        CHECK(CaptionManager::Wrap("", 10) == std::vector<std::string>({""}));
        CHECK(CaptionManager::Wrap("x", 1).size() == 1);
    }

    // ---- Showing a line: audio length, fades, settings in the draw list ---------------------------------------------
    {
        CaptionManager cm;
        const AudioClip clip = AudioClip::Sine(440.0f, 3.0f);
        const int id = cm.Say("Anna", "Hello there", clip);
        CHECK(id > 0 && cm.IsShowing(id) && cm.VisibleCount() == 1 && cm.QueuedCount() == 0);
        auto lines = cm.Lines();
        CHECK(lines.size() == 1 && lines[0].id == id && lines[0].speaker == "Anna" && lines[0].speakerLabel == "Anna: ");
        CHECK(lines[0].rows.size() == 1 && lines[0].rows[0] == "Hello there");
        CHECK_NEAR(lines[0].alpha, 0.0, 1e-6);                         // fading in from nothing
        cm.Update(0.06f);
        CHECK_NEAR(cm.Lines()[0].alpha, 0.5, 0.01);
        cm.Update(0.1f);
        lines = cm.Lines();
        CHECK_NEAR(lines[0].alpha, 1.0, 1e-6);
        CHECK_NEAR(lines[0].textColour.a, 1.0, 1e-6);
        CHECK_NEAR(lines[0].backgroundColour.a, 0.6, 1e-6);            // the default background opacity at full alpha
        cm.Update(2.9f);                                               // 3.06 s in: audio 3 s plus 0.25 s: still up
        CHECK(cm.VisibleCount() == 1 && cm.Lines()[0].alpha > 0.99f);
        cm.Update(0.3f);                                               // 3.36: past the end, fading
        CHECK(cm.VisibleCount() == 1 && cm.Lines()[0].alpha < 1.0f && cm.Lines()[0].alpha > 0.0f);
        int finished = 0;
        cm.onFinished = [&](int fid) { if (fid == id) ++finished; };
        cm.Update(0.3f);                                               // faded out and gone
        CHECK(cm.VisibleCount() == 0 && cm.Empty() && finished == 1);

        // The player's settings reach the draw list.
        cm.settings.textScale = 1.5f;
        cm.settings.backgroundOpacity = 0.4f;
        cm.settings.textOpacity = 0.8f;
        cm.settings.speakerNames = false;
        cm.settings.speakerColours = false;
        const int id2 = cm.Say("Bo", "Hi", 2.0f);
        cm.Update(0.2f);
        lines = cm.Lines();
        CHECK(id2 > 0 && lines.size() == 1 && lines[0].speakerLabel.empty() && lines[0].speaker == "Bo");
        CHECK_NEAR(lines[0].scale, 1.5, 1e-6);
        CHECK_NEAR(lines[0].backgroundColour.a, 0.4, 1e-5);
        CHECK_NEAR(lines[0].textColour.a, 0.8, 1e-5);
        CHECK(lines[0].speakerColour.r == 1.0f && lines[0].speakerColour.g == 1.0f && lines[0].speakerColour.b == 1.0f);   // colours off
        cm.settings.background = false;
        CHECK_NEAR(cm.Lines()[0].backgroundColour.a, 0.0, 1e-6);
        cm.settings.maxCharsPerLine = 16;
        cm.Clear();
        cm.Say("", "the quick brown fox jumps over the lazy dog", 0.0f);
        CHECK(cm.Lines()[0].rows.size() == 3);                         // wrapped to the player's width

        // Off means off: nothing is returned, nothing drawn.
        cm.Clear();
        cm.settings.enabled = false;
        CHECK(cm.Say("A", "text") == 0 && cm.Empty());
        cm.settings.enabled = true;
        const int shown = cm.Say("A", "text");
        cm.settings.enabled = false;
        CHECK(shown > 0 && cm.Lines().empty());                        // turned off while showing: hidden at once
        CHECK(cm.Say("A", "   ") == 0);                                // empty text is nothing to show
    }

    // ---- Queue, priority, waiting too long ------------------------------------------------------------------------------
    {
        CaptionManager cm;
        cm.settings.maxLines = 2;
        std::vector<int> dropped;
        cm.onDropped = [&](int id) { dropped.push_back(id); };
        SubtitleLine l;
        l.text = "line";
        l.duration = 2.0f;
        const int a = cm.Say(l), b = cm.Say(l);
        const int c = cm.Say("", "third", 0.0f, 0);
        l.priority = 3;
        l.text = "important";
        const int d = cm.Say(l);                                       // higher priority than everything on screen
        // d took the place of the oldest lowest-priority line (a); c waits.
        CHECK(cm.IsShowing(b) && cm.IsShowing(d) && !cm.IsShowing(a));
        CHECK(dropped.size() == 1 && dropped[0] == a);
        CHECK(cm.IsQueued(c) && cm.QueuedCount() == 1);
        const auto order = cm.Lines();
        CHECK(order.size() == 2 && order[0].id == b && order[1].id == d);   // oldest first

        // When room appears the highest priority waiting line goes first.
        cm.Clear();
        dropped.clear();
        cm.settings.maxLines = 1;
        SubtitleLine base;
        base.duration = 1.0f;
        base.priority = 5;
        base.text = "first";
        const int first = cm.Say(base);
        base.priority = 0;
        base.text = "low";
        const int low = cm.Say(base);
        base.priority = 1;
        base.text = "mid";
        const int mid = cm.Say(base);
        base.priority = 2;
        base.text = "high";
        const int high = cm.Say(base);
        CHECK(cm.IsShowing(first) && cm.QueuedCount() == 3 && dropped.empty());   // none outranks what is on screen
        cm.Update(1.3f);                                               // first is over (1 s and its fade)
        CHECK(cm.IsShowing(high) && cm.IsQueued(mid) && cm.IsQueued(low));   // high before mid before low, though low came first
        cm.Cancel(high);
        CHECK(cm.IsShowing(mid) && cm.IsQueued(low));

        // A line that waits too long is dropped.
        cm.Clear();
        dropped.clear();
        cm.settings.maxLines = 1;
        SubtitleLine slow;
        slow.text = "long one";
        slow.duration = 10.0f;
        cm.Say(slow);
        SubtitleLine stale;
        stale.text = "stale";
        stale.duration = 1.0f;
        stale.maxDelay = 1.0f;
        const int s = cm.Say(stale);
        CHECK(cm.IsQueued(s));
        cm.Update(0.9f);
        CHECK(cm.IsQueued(s) && dropped.empty());
        cm.Update(0.2f);
        CHECK(!cm.IsQueued(s) && dropped.size() == 1 && dropped[0] == s);

        // The queue has a limit; the lowest priority, latest line goes.
        cm.Clear();
        dropped.clear();
        cm.settings.maxLines = 1;
        cm.maxQueue = 2;
        slow.priority = 9;
        cm.Say(slow);
        SubtitleLine q;
        q.duration = 1.0f;
        q.text = "q1";
        const int q1 = cm.Say(q);
        q.text = "q2";
        const int q2 = cm.Say(q);
        q.text = "q3";
        q.priority = 1;
        const int q3 = cm.Say(q);                                      // outranks q2, which is pushed out
        CHECK(q3 > 0 && cm.QueuedCount() == 2 && cm.IsQueued(q1) && cm.IsQueued(q3) && !cm.IsQueued(q2));
        CHECK(dropped.size() == 1 && dropped[0] == q2);
        q.text = "q4";
        q.priority = -5;
        CHECK(cm.Say(q) == 0);                                         // lower than everything: turned away
        CHECK(cm.QueuedCount() == 2);

        // Tags
        cm.Clear();
        cm.settings.maxLines = 3;
        SubtitleLine t;
        t.text = "cutscene one";
        t.tag = "cut";
        cm.Say(t);
        t.text = "cutscene two";
        cm.Say(t);
        cm.Say("", "other");
        CHECK(cm.CancelTag("cut") == 2 && cm.VisibleCount() == 1 && cm.CancelTag("") == 0);
        CHECK(!cm.Cancel(9999));
    }

    // ---- Sound captions (SDH), speakers and their colours ----------------------------------------------------------------
    {
        CaptionManager cm;
        cm.settings.maxLines = 6;
        CHECK(cm.Sound("door slams") == 0 && cm.Empty());              // off by default
        cm.settings.soundCaptions = true;
        const int id = cm.Sound("door slams");
        CHECK(id > 0);
        auto lines = cm.Lines();
        CHECK(lines.size() == 1 && lines[0].rows[0] == "[door slams]" && lines[0].italic && lines[0].kind == SubtitleKind::Sound);
        CHECK(lines[0].speakerLabel.empty() && lines[0].priority == -1);
        cm.Update(1.0f);
        CHECK(cm.Sound("door slams") == id);                           // the same caption again restarts it
        cm.Update(0.6f);
        CHECK(cm.VisibleCount() == 1 && cm.Lines()[0].alpha > 0.99f);  // 1.5 s default: still up thanks to the restart
        CHECK(cm.Sound("[glass breaks]") > 0 && cm.Lines().back().rows[0] == "[glass breaks]");   // no double brackets
        const int m = cm.Music("tense music", 4.0f);
        CHECK(m > 0 && cm.Lines().back().kind == SubtitleKind::Music && cm.Lines().back().rows[0] == "[tense music]");
        cm.settings.soundCaptions = false;
        CHECK(cm.Lines().empty());                                     // captions off: hidden, speech unaffected
        cm.Say("Anna", "still speaking");
        CHECK(cm.Lines().size() == 1);

        // Speaker colours
        cm.Clear();
        cm.ClearSpeakerColours();
        cm.Say("Anna", "one");
        cm.Say("Bo", "two");
        cm.Say("Anna", "three");
        lines = cm.Lines();
        CHECK(lines.size() == 3);
        const Color ca = lines[0].speakerColour, cb = lines[1].speakerColour;
        CHECK(ColorDistance(Color(ca.r, ca.g, ca.b), Color(cb.r, cb.g, cb.b)) > 25.0f);              // different speakers, different colours
        CHECK(lines[2].speakerColour.r == ca.r && lines[2].speakerColour.g == ca.g);                 // the same speaker, the same colour
        CHECK(Near(Color(ca.r, ca.g, ca.b), SafePalette()[0].r, SafePalette()[0].g, SafePalette()[0].b, 1e-5f));   // from the safe palette
        cm.SetSpeakerColour("Anna", Color(0.1f, 0.2f, 0.3f));
        CHECK(Near(cm.SpeakerColour("Anna"), 0.1f, 0.2f, 0.3f, 1e-6f));
        SubtitleLine own;
        own.speaker = "Narrator";
        own.text = "special";
        own.hasColour = true;
        own.colour = Color(0.9f, 0.1f, 0.1f);
        cm.Say(own);
        CHECK(Near(cm.Lines().back().speakerColour, 0.9f, 0.1f, 0.1f, 1e-5f));

        // A line can name a string-table key; the current language's text is used when it has one.
        StringTable table;
        table.AddLanguage("en");
        table.AddLanguage("fr");
        table.defaultLanguage = "en";
        table.Set("intro", "en", "Welcome");
        table.Set("intro", "fr", "Bienvenue");
        Localization::SetTable(table);
        cm.Clear();
        SubtitleLine key;
        key.textKey = "intro";
        key.text = "fallback";
        cm.Say(key);
        CHECK(cm.Lines()[0].rows[0] == "Welcome");
        Localization::SetLanguage("fr");
        cm.Clear();
        cm.Say(key);
        CHECK(cm.Lines()[0].rows[0] == "Bienvenue");
        key.textKey = "no.such.key";
        cm.Clear();
        cm.Say(key);
        CHECK(cm.Lines()[0].rows[0] == "fallback");
        Localization::Reset();
    }

    // ---- Tracks: timing, SubRip files -----------------------------------------------------------------------------------
    {
        SubtitleTrack track;
        track.name = "intro";
        track.Add(2.0f, "Bo", "second", 1.0f);
        track.Add(0.5f, "Anna", "first", 1.0f);
        track.AddSound(1.0f, "wind howls", 1.0f);
        CHECK(track.cues.size() == 3 && track.cues[0].line.text == "first" && track.cues[1].line.text == "[wind howls]" && track.cues[2].line.text == "second");
        CHECK_NEAR(track.Length(), 3.0, 1e-6);

        CaptionManager cm;
        cm.settings.soundCaptions = true;
        const int h = cm.Play(track);
        CHECK(h > 0 && cm.Playing(h) && cm.Empty());
        cm.Update(0.4f);
        CHECK(cm.Empty());
        cm.Update(0.2f);                                               // 0.6 s: Anna's line
        CHECK(cm.VisibleCount() == 1 && cm.Lines()[0].rows[0] == "first");
        cm.Update(0.5f);                                               // 1.1 s: the wind caption joins
        CHECK(cm.VisibleCount() == 2 && cm.Lines()[1].rows[0] == "[wind howls]");
        cm.Update(1.0f);                                               // 2.1 s: Bo's line (the others are over)
        CHECK(cm.Lines().back().rows[0] == "second");
        CHECK(!cm.Playing(h));                                         // every cue has been handed over
        cm.Update(2.0f);
        CHECK(cm.Empty());

        // Starting part way in: a cue already running shows for what is left of it.
        cm.Clear();
        SubtitleTrack longTrack;
        longTrack.Add(0.0f, "Anna", "long", 3.0f);
        longTrack.Add(5.0f, "Bo", "later", 1.0f);
        const int h2 = cm.Play(longTrack, 1.0f);
        CHECK(cm.VisibleCount() == 1 && cm.Playing(h2));
        cm.Update(1.9f);                                               // 2.9 s into the track: its end is at 3.0
        CHECK(cm.VisibleCount() == 1 && cm.Lines()[0].alpha > 0.99f);
        cm.Update(0.2f);
        CHECK(cm.VisibleCount() == 1 && cm.Lines()[0].alpha < 1.0f);   // past 3.0, fading
        cm.Stop(h2);
        CHECK(!cm.Playing(h2));
        cm.Update(10.0f);
        CHECK(cm.Empty());                                             // the stopped track never shows "later"
        CHECK(cm.Play(SubtitleTrack{}) == 0);

        // SubRip
        const std::string srt =
            "\xEF\xBB\xBF" "1\r\n00:00:01,000 --> 00:00:03,500\r\nAnna: Hello <i>there</i>,\r\nfriend\r\n\r\n"
            "2\r\n00:00:04,000 --> 00:00:05,250 X1:0 X2:100\r\n[door slams]\r\n\r\n"
            "3\r\n00:01:02,5 --> 00:01:04,000\r\nNote 12: not a speaker\r\n";
        SubtitleTrack parsed;
        std::string err;
        CHECK(SubtitleTrack::ParseSrt(srt, parsed, &err));
        CHECK(parsed.cues.size() == 3);
        CHECK_NEAR(parsed.cues[0].start, 1.0, 1e-5);
        CHECK_NEAR(parsed.cues[0].line.duration, 2.5, 1e-5);
        CHECK(parsed.cues[0].line.speaker == "Anna" && parsed.cues[0].line.text == "Hello there,\nfriend");
        CHECK(parsed.cues[1].line.kind == SubtitleKind::Sound && parsed.cues[1].line.text == "[door slams]");
        CHECK_NEAR(parsed.cues[1].line.duration, 1.25, 1e-5);
        CHECK_NEAR(parsed.cues[2].start, 62.5, 1e-4);
        CHECK(parsed.cues[2].line.speaker.empty() && parsed.cues[2].line.text == "Note 12: not a speaker");

        const std::string again = parsed.ToSrt();
        CHECK(again.find("00:00:01,000 --> 00:00:03,500") != std::string::npos && again.find("Anna: Hello there,") != std::string::npos);
        SubtitleTrack round;
        CHECK(SubtitleTrack::ParseSrt(again, round));
        CHECK(round.cues.size() == 3);
        for (std::size_t i = 0; i < 3; ++i) {
            CHECK_NEAR(round.cues[i].start, parsed.cues[i].start, 1e-3);
            CHECK(round.cues[i].line.text == parsed.cues[i].line.text && round.cues[i].line.speaker == parsed.cues[i].line.speaker);
        }
        SubtitleTrack nothing;
        CHECK(!SubtitleTrack::ParseSrt("not a subtitle file", nothing, &err) && !err.empty());
    }

    // ---- Subtitle settings persist ------------------------------------------------------------------------------------------------
    {
        Prefs::Clear();
        SubtitleSettings s;
        s.enabled = false;
        s.soundCaptions = true;
        s.speakerNames = false;
        s.textScale = 1.75f;
        s.backgroundOpacity = 0.25f;
        s.maxLines = 4;
        s.top = true;
        s.Save();
        SubtitleSettings back;
        CHECK(back != s);
        back.Load();
        CHECK(back == s);
        s.textScale = 9.0f;
        s.maxLines = 99;
        s.backgroundOpacity = -1.0f;
        s.maxCharsPerLine = 3;
        s.Clamp();
        CHECK(s.textScale == 3.0f && s.maxLines == 6 && s.backgroundOpacity == 0.0f && s.maxCharsPerLine == 16);
        Prefs::Clear();
        SubtitleSettings untouched;
        untouched.Load();
        CHECK(untouched == SubtitleSettings{});                        // nothing saved: the defaults stay
    }

    // ---- Accessibility settings: callbacks, persistence, helpers ------------------------------------------------------------------------
    {
        Prefs::Clear();
        Accessibility::Reset();
        const AccessibilitySettings defaults;
        CHECK(Accessibility::Get() == defaults && defaults.aimAssist == AimAssist::Medium && !defaults.screenReaderHints);

        std::vector<std::uint32_t> heard;
        const int id = Accessibility::AddListener([&](const AccessibilitySettings&, std::uint32_t changed) { heard.push_back(changed); });
        Accessibility::SetTextScale(1.5f);
        CHECK(heard.size() == 1 && heard[0] == kA11yTextScale && Accessibility::Get().textScale == 1.5f);
        Accessibility::SetTextScale(1.5f);
        CHECK(heard.size() == 1);                                      // nothing changed: nothing said
        Accessibility::SetTextScale(10.0f);
        CHECK(Accessibility::Get().textScale == 2.0f);                 // clamped
        Accessibility::SetTextScale(0.1f);
        CHECK(Accessibility::Get().textScale == 0.75f);
        Accessibility::SetHighContrast(true);
        Accessibility::SetReduceMotion(true);
        Accessibility::SetReduceFlashing(true);
        Accessibility::SetColorblind(ColorblindMode::Deuteranopia, 0.8f);
        Accessibility::SetAimAssist(AimAssist::High);
        Accessibility::SetScreenReaderHints(true);
        CHECK(heard.size() == 9);
        CHECK(heard[3] == kA11yHighContrast && heard[4] == kA11yReduceMotion && heard[5] == kA11yReduceFlashing &&
              heard[6] == kA11yColorblind && heard[7] == kA11yAimAssist && heard[8] == kA11yScreenReader);
        AccessibilitySettings both = Accessibility::Get();
        both.toggleAllHolds = true;
        both.textScale = 1.0f;
        Accessibility::Set(both);
        CHECK(heard.back() == (kA11yHoldToggle | kA11yTextScale));     // one call, two parts
        Accessibility::SetToggleAllHolds(false);
        Accessibility::SetToggleAction("Sprint", true);
        CHECK(Accessibility::ToggleFor("Sprint") && !Accessibility::ToggleFor("Aim"));
        Accessibility::SetToggleAction("Sprint", false);
        CHECK(!Accessibility::ToggleFor("Sprint"));
        Accessibility::SetToggleAction("Aim", true);
        SubtitleSettings sub;
        sub.textScale = 2.0f;
        sub.soundCaptions = true;
        Accessibility::SetSubtitles(sub);
        CHECK(heard.back() == kA11ySubtitles);
        CHECK(CaptionManager::Main().settings.textScale == 2.0f && CaptionManager::Main().settings.soundCaptions);   // the shared manager follows

        // The helpers
        CHECK_NEAR(Accessibility::ScaleText(20.0f), 20.0 * Accessibility::Get().textScale, 1e-6);
        CHECK(Accessibility::MotionScale() == 0.0f && Accessibility::FlashScale() == 0.0f);
        CHECK_NEAR(Accessibility::TransitionSeconds(1.0f), 0.25, 1e-6);
        CHECK_NEAR(Accessibility::AimAssistStrength(), 1.0, 1e-6);
        const Color filtered = Accessibility::FilterColor(Color(0.8f, 0.31f, 0.16f));
        const Color direct = DaltonizeColor(Color(0.8f, 0.31f, 0.16f), ColorblindMode::Deuteranopia, 0.8f);
        CHECK(Near(filtered, direct.r, direct.g, direct.b, 1e-6f));
        const Color text = Accessibility::TextColor(Color(0.4f, 0.4f, 0.4f), Color::Black);       // high contrast is on
        CHECK(ContrastRatio(text, Color::Black) >= 7.0f);
        Accessibility::SetHighContrast(false);
        CHECK(Accessibility::TextColor(Color(0.4f, 0.4f, 0.4f), Color::Black).r == 0.4f);
        Accessibility::SetColorblind(ColorblindMode::None);
        CHECK(Near(Accessibility::FilterColor(Color(0.8f, 0.31f, 0.16f)), 0.8f, 0.31f, 0.16f, 1e-6f));

        // Saved and read back
        Accessibility::SetColorblind(ColorblindMode::Tritanopia, 0.6f);
        Accessibility::SetAimAssist(AimAssist::Low);
        const AccessibilitySettings saved = Accessibility::Get();
        Accessibility::Save();
        CHECK(Prefs::GetString("a11y.colorblind") == "tritanopia" && Prefs::GetString("a11y.aimAssist") == "low");
        CHECK(Prefs::GetString("a11y.toggle") == "Aim");
        Accessibility::Reset();
        CHECK(Accessibility::Get() == AccessibilitySettings{});
        int loads = 0;
        Accessibility::AddListener([&](const AccessibilitySettings&, std::uint32_t) { ++loads; });
        Accessibility::Load();
        CHECK(Accessibility::Get() == saved && loads == 1);
        CHECK(CaptionManager::Main().settings.textScale == 2.0f);      // loading updates the shared manager too
        Accessibility::RemoveListener(id);
        heard.clear();
        Accessibility::SetTextScale(1.25f);
        CHECK(heard.empty());                                          // the removed listener hears nothing

        // Screen reader hints
        Accessibility::Reset();
        std::vector<std::string> said;
        Accessibility::SetAnnouncer([&](const std::string& t) { said.push_back(t); });
        CHECK(!Accessibility::Announce("Main menu") && said.empty());   // hints are off
        Accessibility::SetScreenReaderHints(true);
        CHECK(Accessibility::Announce("Main menu, Play button") && said.size() == 1 && said[0] == "Main menu, Play button");
        CHECK(!Accessibility::Announce(""));
        Accessibility::SetAnnouncer(nullptr);
        CHECK(!Accessibility::Announce("nobody listens"));

        AimAssist level = AimAssist::Off;
        CHECK(AimAssistFromName("HIGH", level) && level == AimAssist::High && !AimAssistFromName("max", level));
        CHECK(AimAssistStrength(AimAssist::Off) == 0.0f && AimAssistStrength(AimAssist::Low) < AimAssistStrength(AimAssist::Medium));
        AccessibilitySettings wild;
        wild.colorblindStrength = 4.0f;
        wild.toggleActions = {"", "Sprint"};
        wild.Clamp();
        CHECK(wild.colorblindStrength == 1.0f && wild.toggleActions.size() == 1);
        Accessibility::Reset();
    }

    // ---- Hold or toggle ---------------------------------------------------------------------------------------------------------------
    {
        HoldToggle hold;
        CHECK(!hold.Update(false, false) && hold.Update(true, false) && hold.Update(true, false) && !hold.Update(false, false));   // follows the input
        HoldToggle tog;
        CHECK(!tog.Update(false, true));
        CHECK(tog.Update(true, true));       // a press turns it on
        CHECK(tog.Update(true, true));       // holding the key does nothing more
        CHECK(tog.Update(false, true));      // letting go keeps it on
        CHECK(!tog.Update(true, true));      // the next press turns it off
        CHECK(!tog.Update(false, true));
        CHECK(tog.Update(true, true) && tog.Active());
        tog.Cancel();                        // stamina ran out
        CHECK(!tog.Active() && !tog.Update(true, true));   // still held: no new press, so it stays off
        CHECK(!tog.Update(false, true) && tog.Update(true, true));
        tog.Reset();
        CHECK(!tog.Active());
        // switching to hold mode follows the key again
        CHECK(tog.Update(true, true));
        CHECK(!tog.Update(false, false));
    }

    // ---- Wrapping and parsing whatever arrives ----------------------------------------------------------------------------------
    {
        std::uint32_t seed = 777;
        auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
        const char* const pieces[] = {"a", "bc", "def", "ghijkl", " ", "  ", "\n", "\xE6\x97\xA5", "\xE6\x9C\xAC", "e\xCC\x81", "-", "\xF0\x9F\x8C\x9F", "longwordwithoutanybreaks"};
        for (int i = 0; i < 2000; ++i) {
            std::string text;
            for (std::size_t k = rnd() % 25; k > 0; --k) text += pieces[rnd() % (sizeof pieces / sizeof pieces[0])];
            const int width = 4 + (int)(rnd() % 30);
            const int indent = (int)(rnd() % 10);
            const auto rows = CaptionManager::Wrap(text, width, indent);
            CHECK(!rows.empty());
            std::string kept, original;
            bool tooWide = false;
            for (std::size_t r = 0; r < rows.size(); ++r) {
                if (CaptionManager::Cells(rows[r]) + (r == 0 && indent < width ? indent : 0) > width) tooWide = true;   // a label wider than the line stands alone
                for (char c : rows[r]) if (c != ' ' && c != '\n') kept += c;
            }
            for (char c : text) if (c != ' ' && c != '\n') original += c;
            CHECK(!tooWide);
            CHECK(kept == original);                           // nothing lost, nothing added
        }
        SubtitleTrack t;
        for (int i = 0; i < 1000; ++i) {
            std::string junk;
            for (std::size_t k = rnd() % 80; k > 0; --k) junk += "1 2:0,-> \n\r[]<>\xE6\x97\xA5:Ab"[rnd() % 22];
            SubtitleTrack::ParseSrt(junk, t);
            SubtitleTrack::ParseSrt("1\n00:00:01,000 --> 00:00:02,000\n" + junk, t);
        }
        // A caption manager under a storm of lines stays within its limits.
        CaptionManager cm;
        cm.settings.soundCaptions = true;
        cm.settings.maxLines = 3;
        cm.maxQueue = 5;
        for (int i = 0; i < 3000; ++i) {
            SubtitleLine l;
            l.text = "line " + std::to_string(rnd() % 40);
            l.kind = rnd() % 3 == 0 ? SubtitleKind::Sound : SubtitleKind::Speech;
            l.speaker = rnd() % 2 ? "A" : "B";
            l.priority = (int)(rnd() % 5) - 2;
            l.duration = (float)(rnd() % 30) / 10.0f;
            cm.Say(l);
            if (rnd() % 3 == 0) cm.Update((float)(rnd() % 20) / 10.0f);
            CHECK(cm.VisibleCount() <= 3 && cm.QueuedCount() <= 5);
        }
        cm.Update(100.0f);
        CHECK(cm.Empty());
    }

    Prefs::Clear();
    Accessibility::Reset();
    TEST_MAIN_RESULT();
}
