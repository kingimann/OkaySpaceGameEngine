// OkayUI designs (okay/UI/OkayUIDesign.hpp): every built-in design defines every token and is readable (WCAG 2.x contrast), the
// designs really differ, Options are clamped, appearance.json round-trips and is followed when it changes, Set restyles OkayUI
// live and keeps pushed colours, motion needs a clock (none in tests), and a gallery of every widget per design (goldens).
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "okay/UI/OkayUIDesign.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

using namespace okayuitest;
namespace UI = OkayUI;
namespace D = OkayUI::design;

namespace {

float Dist(D::Rgba a, D::Rgba b) { return (std::abs(a.r - b.r) + std::abs(a.g - b.g) + std::abs(a.b - b.b)) / 255.0f; }
D::Options Named(const char* name, D::Scheme sc = D::Scheme::Auto) { D::Options o; std::snprintf(o.design, sizeof o.design, "%s", name); o.scheme = sc; return o; }

void TestPalettes() {
    RUN_SUITE("design: every design is complete and readable");
    CHECK(D::DesignCount() >= 4);
    for (int i = 0; i < D::DesignCount(); ++i) {
        const D::Design& d = D::DesignAt(i);
        CHECK(d.name && d.name[0] && d.blurb && d.blurb[0]);
        CHECK(D::FindDesign(d.name) == i);
        for (int sc = 0; sc < 2; ++sc) {
            if (sc == 0 ? !d.hasDark : !d.hasLight) continue;
            const D::Palette& p = sc == 0 ? d.dark : d.light;
            CHECK(p.light == (sc == 1));
            const D::Rgba surf[] = {p.deep, p.sunken, p.base, p.raised, p.overlay, p.field, p.control};
            for (const D::Rgba& s : surf) {
                CHECK(D::Contrast(p.text, s) >= 7.0f);       // body text: well past 4.5:1
                CHECK(D::Contrast(p.textDim, s) >= 4.5f);    // secondary text too
                for (const D::Rgba& c : {p.success, p.warning, p.danger, p.info}) CHECK(D::Contrast(c, s) >= 4.5f);
                if (D::Contrast(p.text, s) < 7.0f || D::Contrast(p.textDim, s) < 4.5f)
                    std::fprintf(stderr, "  %s %s: text %.2f dim %.2f on (%d,%d,%d)\n", d.name, sc ? "light" : "dark",
                                 (double)D::Contrast(p.text, s), (double)D::Contrast(p.textDim, s), s.r, s.g, s.b);
                for (const D::Rgba& c : {p.success, p.warning, p.danger, p.info})
                    if (D::Contrast(c, s) < 4.5f) std::fprintf(stderr, "  %s %s: status (%d,%d,%d) %.2f on (%d,%d,%d)\n", d.name, sc ? "light" : "dark", c.r, c.g, c.b, (double)D::Contrast(c, s), s.r, s.g, s.b);
            }
            CHECK(D::Contrast(p.onAccent, p.accent) >= 3.0f);   // a label on a filled accent (large text, 3:1)
            CHECK(D::Contrast(p.tooltipText, D::WithAlpha(p.tooltipBg, 255)) >= 7.0f);
            CHECK(D::Contrast(p.textFaint, p.base) >= 3.0f);    // placeholders still readable
        }
    }
    // The designs look clearly different: page colour, shape or both.
    for (int i = 0; i < D::DesignCount(); ++i)
        for (int j = i + 1; j < D::DesignCount(); ++j) {
            const D::Tokens a = D::Resolve(Named(D::DesignAt(i).name)), b = D::Resolve(Named(D::DesignAt(j).name));
            const bool colour = Dist(a.c.base, b.c.base) > 0.08f || Dist(a.c.accent, b.c.accent) > 0.3f;
            const bool shape = a.shape.tabs != b.shape.tabs || std::fabs(a.shape.radius - b.shape.radius) >= 2.0f || a.shape.controlBorders != b.shape.controlBorders;
            CHECK(colour && shape);
        }
    // Slate is the launcher's look as it was (shared/OkayTheme.hpp kept these values).
    const D::Tokens s = D::Resolve(Named("Slate", D::Scheme::Dark));
    CHECK(s.c.base.r == 30 && s.c.base.g == 33 && s.c.base.b == 41 && s.c.accent.r == 66 && s.c.accent.g == 150 && s.c.accent.b == 250);
    CHECK(s.shape.tabs == D::TabShape::Pill && s.shape.controlBorders);
}

void TestResolve() {
    RUN_SUITE("design: options on top of a design");
    D::Options o;   // the defaults: Graphite, dark
    CHECK(std::strcmp(o.design, "Graphite") == 0);
    D::Tokens t = D::Resolve(o);
    CHECK(!t.c.light && std::strcmp(t.designName, "Graphite") == 0);
    o.scheme = D::Scheme::Light; CHECK(D::Resolve(o).c.light);
    D::Options m = Named("Midnight", D::Scheme::Light); CHECK(!D::Resolve(m).c.light);   // no light scheme: stays dark
    D::Options p = Named("Paper"); CHECK(D::Resolve(p).c.light);                         // light first
    o = D::Options(); o.customAccent = true; o.accent = D::Rgba{250, 220, 30, 255};
    t = D::Resolve(o);
    CHECK(t.c.accent.r == 250 && t.c.accent.g == 220);
    CHECK(D::Contrast(t.c.onAccent, t.c.accent) >= 4.5f);   // a yellow accent gets dark text on it
    o = D::Options(); o.roundness = 0.0f; o.shadows = false; o.reduceMotion = true; o.density = D::Density::Compact;
    t = D::Resolve(o);
    CHECK(t.shape.radius == 0.0f && t.shape.shadow == 0 && t.shape.motionMs == 0.0f && t.spacing < 1.0f);
    o = D::Options(); o.roundness = 99.0f; o.uiScale = -3.0f; o.textSize = 1e9f; std::snprintf(o.design, sizeof o.design, "nope");
    D::Sanitize(o);
    CHECK(o.roundness == 2.0f && o.uiScale == 0.75f && o.textSize == 1.3f && std::strcmp(o.design, "Graphite") == 0);
    o.roundness = std::nanf(""); D::Sanitize(o); CHECK(o.roundness == 0.0f);
    CHECK(D::FindDesign("high contrast") >= 0 && D::FindDesign("HIGH CONTRAST") == D::FindDesign("High Contrast"));
}

void TestJson() {
    RUN_SUITE("design: appearance.json");
    D::Options o = Named("Paper", D::Scheme::Dark);
    o.customAccent = true; o.accent = D::Rgba{12, 200, 99, 255}; o.density = D::Density::Spacious; o.roundness = 1.5f;
    o.uiScale = 1.25f; o.textSize = 1.1f; o.shadows = false; o.reduceMotion = true;
    char buf[1024];
    CHECK(D::WriteJson(o, buf, (int)sizeof buf) > 0);
    D::Options r;
    CHECK(D::ParseJson(buf, &r));
    CHECK(std::strcmp(r.design, "Paper") == 0 && r.scheme == D::Scheme::Dark && r.customAccent && r.accent.r == 12 && r.accent.g == 200 && r.accent.b == 99);
    CHECK(r.density == D::Density::Spacious && std::fabs(r.roundness - 1.5f) < 0.01f && std::fabs(r.uiScale - 1.25f) < 0.01f);
    CHECK(std::fabs(r.textSize - 1.1f) < 0.01f && !r.shadows && r.reduceMotion);
    CHECK(D::WriteJson(o, buf, 8) == -1);                     // too small: refused, not cut
    D::Options keep = Named("Midnight");
    CHECK(!D::ParseJson("not json", &keep) && std::strcmp(keep.design, "Midnight") == 0);
    CHECK(!D::ParseJson("[1,2]", &keep));
    CHECK(D::ParseJson("{\"design\": \"slate\", \"extra\": 5, \"roundness\": \"big\"}", &keep));   // unknown keys and wrong types are ignored
    CHECK(std::strcmp(keep.design, "Slate") == 0 && keep.roundness == 1.0f);
    CHECK(D::ParseJson("{\"accent\": \"design\"}", &keep) && !keep.customAccent);
    CHECK(D::ParseJson("{\"accent\": \"#zzzzzz\"}", &keep) && !keep.customAccent);
}

void TestFollowFile() {
    RUN_SUITE("design: following the per-user file");
    std::error_code ec;
    const std::filesystem::path dir = std::filesystem::temp_directory_path(ec) / "okayui_design_test";
    std::filesystem::remove_all(dir, ec);
    const std::string path = (dir / "sub" / "appearance.json").string();
#if defined(_WIN32)
    _putenv_s("OKAY_APPEARANCE_FILE", path.c_str());
#else
    setenv("OKAY_APPEARANCE_FILE", path.c_str(), 1);
#endif
    char p[1024]; CHECK(D::DefaultPath(p, (int)sizeof p) && path == p);
    Reset();
    D::Set(D::Options());
    CHECK(!D::FollowUserFile());                     // a first start: no file
    CHECK(D::FollowingUserFile());
    D::Options o = Named("High Contrast");
    D::Set(o);
    CHECK(D::SaveUserFile());                        // makes the folder
    D::Options back; CHECK(D::Load(path.c_str(), &back) && std::strcmp(back.design, "High Contrast") == 0);
    CHECK(!D::Poll(10.0));                           // our own save: nothing new
    {   // another program writes a new choice; Poll (at most twice a second) picks it up and restyles OkayUI
        D::Options other = Named("Paper"); CHECK(D::Save(path.c_str(), other));
        std::ofstream(path, std::ios::app) << " ";   // a different size, so the stamp moves even within one second
    }
    const unsigned rev = D::Revision();
    CHECK(!D::Poll(10.1));                           // too soon after the last look
    CHECK(D::Poll(11.0));
    CHECK(std::strcmp(D::Current().design, "Paper") == 0 && D::Revision() != rev);
    CHECK(UI::Style().lightScheme && UI::Style().tabShape == (int)D::TabShape::Segmented);
    std::filesystem::remove_all(dir, ec);
#if defined(_WIN32)
    _putenv_s("OKAY_APPEARANCE_FILE", "");
#else
    unsetenv("OKAY_APPEARANCE_FILE");
#endif
    D::Set(D::Options());
}

void TestSetKeepsProgramState() {
    RUN_SUITE("design: Set restyles live, keeps pushed colours");
    Reset();
    UI::UseLauncherLook(true);
    UI::Style().caretBlink = false;
    UI::SetStyleColor(UI::Col_PopupBg, 1, 2, 3, 255);
    D::Set(Named("Midnight"));
    CHECK(UI::Style().caretBlink == false);
    CHECK(UI::Style().colors[UI::Col_PopupBg][0] == 1 && ((UI::Style().colorSet >> UI::Col_PopupBg) & 1ull));
    const D::Tokens t = D::CurrentTokens();
    CHECK(UI::Style().panel[0] == t.c.base.r && UI::Style().accent[2] == t.c.accent.b && UI::Style().textScale == 2.0f);
    D::Options big = Named("Midnight"); big.textSize = 1.25f; D::Set(big);
    CHECK(std::fabs(UI::Style().textScale - 2.5f) < 1e-4f);   // TrueType text follows the text size
    D::Set(D::Options());
    Reset();
    CHECK(UI::Style().panel[0] == D::DefaultTheme().panel[0]);   // ResetState: the default design
    CHECK(UI::Theme().panel[0] == D::DefaultTheme().panel[0] && UI::Theme().accent[0] == D::DefaultTheme().accent[0] && UI::Theme().bgHover[0] == D::DefaultTheme().bgHover[0]);
}

void TestMotion() {
    RUN_SUITE("design: motion needs a clock");
    Reset();
    // No clock (Input::time 0, as every other test): a hovered button is its hover colour at once.
    Sim sim(200, 80);
    const auto ui = [] { UI::Button(1, 10, 10, 120, 40, "Hover"); };
    sim.Move(60, 30); sim.Frame(ui);
    CHECK(PixelAt(sim.Draw(), 14, 30) == ThemeRgba(UI::Style().bgHover));
    // With a clock the colour eases in over Theme::motionMs and ends exactly on the hover colour.
    Reset();
    double t = 1.0;
    auto frame = [&](float mx) {
        UI::Input in; in.mouseX = mx; in.mouseY = 30; in.displayW = 200; in.displayH = 80; in.time = t; t += 1.0 / 60.0;
        UI::BeginFrame(in); ui(); UI::EndFrameData();
    };
    auto px = [] { okayuitest::Bitmap b(200, 80); b.Clear(0, 0, 0); UI::soft::Render(UI::GetDrawData(), b); return PixelAt(b, 14, 30); };
    frame(500); frame(500);
    frame(60);
    const unsigned mid = px();
    CHECK(mid != ThemeRgba(UI::Style().bgHover) && mid != ThemeRgba(UI::Style().bg));
    for (int i = 0; i < 20; ++i) frame(60);
    CHECK(px() == ThemeRgba(UI::Style().bgHover));
    // Reduce motion: at once again.
    Reset();
    D::Options rm; rm.reduceMotion = true; D::Set(rm);
    t = 1.0; frame(500); frame(500); frame(60);
    CHECK(px() == ThemeRgba(UI::Style().bgHover));
    D::Set(D::Options());
}

void TestFocusRing() {
    RUN_SUITE("design: the field with the keyboard has a focus ring");
    Reset();
    Sim sim(300, 100);
    char buf[32] = "";
    const auto ui = [&] { UI::TextField(1, 40, 30, 200, 34, buf, (int)sizeof buf); };
    sim.Frame(ui);
    const unsigned before = PixelAt(sim.Draw(), 36, 47);
    sim.Click(100, 47, ui);
    const unsigned after = PixelAt(sim.Draw(), 36, 47);   // 4 pixels left of the field: the ring
    CHECK(before == Rgba(0, 0, 0) && after != before);
}

// Every core widget on one page, drawn in a design (TrueType text): what the goldens design_<name>.png hold.
void Gallery(Sim& sim) {
    static bool cb = true, cb2 = false, sw = true, sw2 = false; static int radio = 1, tab = 0, combo = 1; static float sl = 0.6f;
    static char txt[32] = "Search", empty[16] = "";
    static const char* items[] = {"Linear", "Smooth", "Bounce"};
    sim.Frame([&] {
        if (UI::Begin("Inspector", 16, 16, 380, 430, nullptr)) {
            if (UI::BeginTabBar("tabs")) {
                if (UI::BeginTabItem("General", nullptr, 0)) UI::EndTabItem();
                if (UI::BeginTabItem("Physics", nullptr, 0)) UI::EndTabItem();
                if (UI::BeginTabItem("Audio", nullptr, 0)) UI::EndTabItem();
                UI::EndTabBar();
            }
            if (UI::CollapsingHeader("Transform", true)) {
                UI::Checkbox("Visible", &cb); UI::SameLine(); UI::Checkbox("Static", &cb2);
                UI::ToggleSwitch("Cast shadows", &sw); UI::SameLine(); UI::ToggleSwitch("Mirror", &sw2);
                UI::RadioButton("Local", &radio, 0); UI::SameLine(); UI::RadioButton("World", &radio, 1);
                UI::SliderFloat("Opacity", &sl, 0.0f, 1.0f);
                UI::Combo("Easing", items, 3, &combo);
                UI::InputText("Name", txt, (int)sizeof txt);
                UI::InputTextWithHint("Tag", "Type a tag...", empty, (int)sizeof empty);
                UI::ProgressBar(0.45f, UI::Vec2{-1.0f, 0.0f}, nullptr);
            }
            if (UI::TreeNode("Children")) {
                UI::TreeNodeEx("Camera", UI::TreeNode_Leaf); UI::TreePop();
                if (UI::TreeNode("Lights")) { UI::TreeNodeEx("Sun", UI::TreeNode_Leaf); UI::TreePop(); UI::TreePop(); }
                UI::TreePop();
            }
            UI::Button("Cancel"); UI::SameLine(); UI::PrimaryButton("Apply");
        }
        UI::End();
        if (UI::Begin("Menu", 410, 16, 220, 170, nullptr)) {
            UI::SeparatorText("Section");
            UI::Selectable("Selected row", true);
            UI::Selectable("Another row", false);
            UI::Selectable("Hovered row", false);
            UI::TextDisabled("Secondary text");
            UI::Badge("NEW", 0, 0, 0);
        }
        UI::End();
        UI::SetTooltip("A tooltip card");
    });
}

void TestGalleries() {
    RUN_SUITE("design: a gallery per design (goldens)");
    for (int i = 0; i < D::DesignCount(); ++i) {
        Reset();
        D::Set(Named(D::DesignAt(i).name));
        UI::UseLauncherLook(true); UI::Style().caretBlink = false;
        Sim sim(650, 470);
        sim.Move(470, 128);   // over "Hovered row"; the tooltip follows the pointer
        Gallery(sim); Gallery(sim); Gallery(sim);
        const D::Tokens t = D::CurrentTokens();
        const Bitmap& b = sim.Draw(t.c.deep.r, t.c.deep.g, t.c.deep.b);
        std::string name = std::string("design_") + D::DesignAt(i).name;
        for (char& c : name) c = c == ' ' ? '_' : (char)std::tolower((unsigned char)c);
        std::string why;
        const bool ok = MatchesGolden(name, b, &why);
        if (!ok) std::cerr << "  golden " << name << ": " << why << "\n";
        CHECK(ok);
        if (const char* dir = std::getenv("OKAYUI_SHOT_DIR")) WritePng(std::string(dir) + "/" + name + ".png", b);
        UI::UseLauncherLook(false);
    }
    D::Set(D::Options());
}

} // namespace

int main() {
    TestPalettes();
    TestResolve();
    TestJson();
    TestFollowFile();
    TestSetKeepsProgramState();
    TestMotion();
    TestFocusRing();
    TestGalleries();
    TEST_MAIN_RESULT();
}
