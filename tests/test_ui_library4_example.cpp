// The shipped sample scenes examples/ui_library/ui_library_4.okayscene (themes, styles, icons) and examples/ui_library/ui_starter_pack.okayscene
// (a whole game UI: main menu, settings, HUD, pause menu, game over). The test loads each file from disk and plays it like a person would:
// click a theme and see the page change, click Play and see the HUD, Pause, Resume, Give up and Try again.
//
// The files are made by this test: run it with OKAY_WRITE_UI_LIBRARY_EXAMPLE4=<folder> to write them there (after a change to the builders
// or the serializer). Without it the test only reads the committed files. Written for this engine; no other project's code or assets.
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Scene/UILibrary.hpp"
#include "okay/UI/UILayoutUpdate.hpp"
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>

using namespace okay;

static std::string WithoutGuids(const std::string& text) {
    std::string out, line;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == '\n') { if (line.rfind("  guid ", 0) != 0) { out += line; out += '\n'; } line.clear(); }
        else line += text[i];
    }
    return out;
}

// Same text, ignoring guids. On a difference it prints the first few differing lines (a loaded file against a freshly built scene), so a failure
// on another compiler or machine says which property differs instead of only "not equal".
// Same scene, ignoring guids. Words are compared one by one; a number may differ by a relative 2e-6 (a few last-place float digits), because the
// committed scene text was written on another machine and a compiler may round a computed colour one step differently (the scene text itself only
// keeps 8 digits). Names, structure and every non-number word must match exactly. On a difference the first few differing lines are printed.
static bool SameWord(const std::string& a, const std::string& b) {
    if (a == b) return true;
    char *ea = nullptr, *eb = nullptr;
    const double x = std::strtod(a.c_str(), &ea), y = std::strtod(b.c_str(), &eb);
    if (a.empty() || b.empty() || *ea || *eb) return false;
    const double m = std::max(std::fabs(x), std::fabs(y));
    return std::fabs(x - y) <= 2e-6 * std::max(1.0, m);
}
static bool SameLine(const std::string& a, const std::string& b) {
    if (a == b) return true;
    std::size_t i = 0, j = 0;
    while (true) {
        while (i < a.size() && a[i] == ' ') ++i;
        while (j < b.size() && b[j] == ' ') ++j;
        if (i >= a.size() || j >= b.size()) return i >= a.size() && j >= b.size();
        // a quoted word is taken whole, so a name with spaces is compared exactly
        auto word = [](const std::string& t, std::size_t& k) {
            const std::size_t st = k;
            if (t[k] == '"') { ++k; while (k < t.size() && t[k] != '"') ++k; if (k < t.size()) ++k; }
            else while (k < t.size() && t[k] != ' ') ++k;
            return t.substr(st, k - st);
        };
        if (!SameWord(word(a, i), word(b, j))) return false;
    }
}
static bool SameScene(const std::string& a, const std::string& b) {
    const std::string x = WithoutGuids(a), y = WithoutGuids(b);
    if (x == y) return true;
    std::size_t pa = 0, pb = 0; int shown = 0; bool same = true;
    while (pa < x.size() || pb < y.size()) {
        const std::size_t ea = x.find('\n', pa), eb = y.find('\n', pb);
        const std::string la = pa < x.size() ? x.substr(pa, ea == std::string::npos ? std::string::npos : ea - pa) : std::string("<end>");
        const std::string lb = pb < y.size() ? y.substr(pb, eb == std::string::npos ? std::string::npos : eb - pb) : std::string("<end>");
        if (!SameLine(la, lb)) { same = false; if (shown < 6) { std::printf("  scene differs: [%s] vs [%s]\n", la.c_str(), lb.c_str()); ++shown; } }
        pa = ea == std::string::npos ? x.size() : ea + 1;
        pb = eb == std::string::npos ? y.size() : eb + 1;
    }
    return same;
}

static GameObject* Under(GameObject* root, const std::string& name) {
    if (!root) return nullptr;
    if (root->name == name) return root;
    for (Transform* c : root->transform->Children())
        if (c && c->gameObject) if (GameObject* f = Under(c->gameObject, name)) return f;
    return nullptr;
}
static GameObject* Named(Scene& s, const std::string& name) {
    for (const auto& o : s.Objects()) if (o->name == name) return o.get();
    return nullptr;
}

struct Play {
    Scene s{"play"};
    bool Load(const std::string& path) {
        s.physicsEnabled = false;
        std::string err; std::vector<std::string> warnings;
        const bool ok = SceneSerializer::LoadFromFile(s, path, &err, &warnings);
        if (!ok) std::printf("  could not load %s: %s\n", path.c_str(), err.c_str());
        CHECK(ok); CHECK(warnings.empty());
        for (const auto& w : warnings) std::printf("  warning: %s\n", w.c_str());
        if (!ok) return false;
        CHECK(s.mainCamera != nullptr || s.FindObjectOfType<Camera>() != nullptr);
        return true;
    }
    void Begin() {
        s.Start(); UpdateUILayouts(s); s.Update(0.016f);
        UICanvas::Set(1280.0f, 720.0f);
        UIWorld().rectOf = [](GameObject* go, Vec2& o, Vec2& sz) { return GetUIScreenRect(go, 1280.0f, 720.0f, o, sz); };
        UIWorld().screenW = 1280.0f; UIWorld().screenH = 720.0f;
    }
    void ClickAt(float x, float y) {
        Input::FeedMouse({x, y}, 0); Input::FeedMouse({x, y}, 1u << 0); s.Update(0.016f);
        Input::FeedMouse({x, y}, 0); s.Update(0.016f);
        Input::FeedMouse({0, 0}, 0); UpdateUILayouts(s); s.Update(0.016f);
    }
    void Press(GameObject* g) {
        CHECK(g && g->GetComponent<UIButton>());
        Vec2 o, sz;
        if (!g || !GetUIScreenRect(g, 1280.0f, 720.0f, o, sz)) { CHECK(false); return; }
        ClickAt(o.x + sz.x * 0.5f, o.y + sz.y * 0.5f);
    }
};

int main() {
    RUN_SUITE("ui_library4_example");
    const std::string dir = std::string(OKAY_SOURCE_DIR) + "/examples/ui_library";
    if (const char* out = std::getenv("OKAY_WRITE_UI_LIBRARY_EXAMPLE4")) {
        Scene a("UI Library 4"); UILibrary::BuildSlice4(a);
        Scene b("UI Starter Pack"); UILibrary::BuildStarterExample(b);
        Scene c("UI Mix Styles"); UILibrary::BuildMixStyles(c);
        std::string err;
        const bool ok3 = SceneSerializer::SaveToFile(c, std::string(out) + "/ui_mix_styles.okayscene", &err);
        const bool ok1 = SceneSerializer::SaveToFile(a, std::string(out) + "/ui_library_4.okayscene", &err);
        const bool ok2 = SceneSerializer::SaveToFile(b, std::string(out) + "/ui_starter_pack.okayscene", &err);
        std::printf("  wrote ui_library_4, ui_starter_pack and ui_mix_styles .okayscene to %s: %s\n", out, ok1 && ok2 && ok3 ? "ok" : "FAILED");
        CHECK(ok1 && ok2 && ok3);
        TEST_MAIN_RESULT();
    }
    UICanvas::Set(1280.0f, 720.0f);
    Prefs::Clear();

    {   // ---- ui_library_4: themes, styles, icons
        Play p;
        if (!p.Load(dir + "/ui_library_4.okayscene")) TEST_MAIN_RESULT();
        {   // the file is what the builder makes today
            Scene fresh("UI Library 4"); UILibrary::BuildSlice4(fresh);
            Scene reread("reread"); std::string e2;
            CHECK(SceneSerializer::Deserialize(reread, SceneSerializer::Serialize(fresh), &e2, nullptr));
            CHECK(SameScene(SceneSerializer::Serialize(reread), SceneSerializer::Serialize(p.s)));
        }
        int icons = 0, vectorIcons = 0;
        for (const auto& o : p.s.Objects()) if (auto* ib = o->GetComponent<UIButton>()) { if (!ib->interactable) { ++icons; vectorIcons += !uilib::IconNameOf(ib->icon).empty() && uilib::FindIcon(uilib::IconNameOf(ib->icon)); } }
        CHECK(vectorIcons == 40);   // forty vector icons (the page has a few other pictures)
        p.Begin();
        GameObject* back = p.s.Find("Backdrop");
        GameObject* sample = p.s.Find("Sample");
        CHECK(back && back->GetComponent<UIPanel>() && sample && sample->GetComponent<UIPanel>());
        const Color backBefore = back->GetComponent<UIPanel>()->color, sampleBefore = sample->GetComponent<UIPanel>()->color;
        auto* styleBtn = p.s.Find("Style2")->GetComponent<UIButton>();
        const Color styleBefore = styleBtn->color;
        p.Press(p.s.Find("Theme3"));                  // the Neon look: only the Sample card
        CHECK(UITheme::Find(&p.s, "") == nullptr);    // there is no scene-wide theme
        CHECK(!Prefs::Has("ui.theme"));
        const Color neon = sample->GetComponent<UIPanel>()->color;
        CHECK(neon.r != sampleBefore.r || neon.g != sampleBefore.g || neon.b != sampleBefore.b);
        CHECK(sample->GetComponent<UIStyleVariant>() && sample->GetComponent<UIStyleVariant>()->themeName == "Neon");
        p.Press(p.s.Find("Theme2"));                  // the Light look
        CHECK(sample->GetComponent<UIPanel>()->color.r > neon.r + 0.2f);
        const Color bk = back->GetComponent<UIPanel>()->color;   // nothing else on the page changed
        CHECK(bk.r == backBefore.r && bk.g == backBefore.g && bk.b == backBefore.b);
        CHECK(styleBtn->color.r == styleBefore.r && styleBtn->color.g == styleBefore.g);
        CHECK(styleBtn->shape == UIShape::Pill);       // the Neon Pill style button is as it was
        CHECK(p.s.Find("Style4")->GetComponent<UIButton>()->shape == UIShape::Rectangle);   // Pixel is square
        // the six biggest sizes differ
        CHECK(p.s.Find("Size1")->GetComponent<UIButton>()->size.y < p.s.Find("Size5")->GetComponent<UIButton>()->size.y);
        Prefs::Clear();
    }

    {   // ---- ui_starter_pack: the whole flow, with no script written by hand
        Play p;
        if (!p.Load(dir + "/ui_starter_pack.okayscene")) TEST_MAIN_RESULT();
        {
            Scene fresh("UI Starter Pack"); UILibrary::BuildStarterExample(fresh);
            Scene reread("reread"); std::string e2;
            CHECK(SceneSerializer::Deserialize(reread, SceneSerializer::Serialize(fresh), &e2, nullptr));
            CHECK(SameScene(SceneSerializer::Serialize(reread), SceneSerializer::Serialize(p.s)));
        }
        p.Begin();
        GameObject *menu = Named(p.s, "Main Menu"), *settings = Named(p.s, "Settings"), *hud = Named(p.s, "HUD Shooter"), *pause = Named(p.s, "Pause Menu"), *over = Named(p.s, "Game Over");
        CHECK(menu && settings && hud && pause && over);
        if (!(menu && settings && hud && pause && over)) TEST_MAIN_RESULT();
        CHECK(menu->active && !settings->active && !hud->active && !pause->active && !over->active);
        p.Press(Under(menu, "SettingsButton"));
        CHECK(settings->active);
        // the Audio tab: click the tab, the Audio page shows and the Video page hides
        GameObject* tabs = Under(settings, "Tabs");
        CHECK(tabs && tabs->GetComponent<UITabs>());
        UITabs* tb = tabs ? tabs->GetComponent<UITabs>() : nullptr;
        if (tb) { tb->Select(1); p.s.Update(0.016f); CHECK(Named(p.s, "Settings Audio")->active && !Named(p.s, "Settings Video")->active); }
        p.Press(Under(settings, "CloseButton"));
        CHECK(!settings->active && menu->active);
        p.Press(Under(menu, "PlayButton"));
        CHECK(!menu->active && hud->active);
        p.Press(Under(hud, "Pause"));
        CHECK(pause->active);
        p.Press(Under(pause, "ResumeButton"));
        CHECK(!pause->active && hud->active);
        p.Press(Under(hud, "Pause"));
        CHECK(pause->active);
        p.Press(Under(pause, "GiveUpButton"));
        CHECK(!pause->active && !hud->active && over->active);
        p.Press(Under(over, "RetryButton"));
        CHECK(!over->active && hud->active);
        p.Press(Under(hud, "Pause"));
        p.Press(Under(pause, "MenuButton"));
        CHECK(menu->active && !hud->active && !pause->active);
        Game::SetPaused(false);
        // the look the pack was built with is stored on each screen; no scene theme exists
        CHECK(UITheme::Find(&p.s, "") == nullptr);
        for (GameObject* g : {menu, settings, hud, pause, over}) CHECK(g->GetComponent<UIStyleVariant>() && g->GetComponent<UIStyleVariant>()->themeName == "Midnight Blue");
    }

    {   // ---- ui_mix_styles: a retro menu, a sci-fi HUD and a cartoon popup in one scene
        Play p;
        if (!p.Load(dir + "/ui_mix_styles.okayscene")) TEST_MAIN_RESULT();
        {
            Scene fresh("UI Mix Styles"); UILibrary::BuildMixStyles(fresh);
            Scene reread("reread"); std::string e2;
            CHECK(SceneSerializer::Deserialize(reread, SceneSerializer::Serialize(fresh), &e2, nullptr));
            CHECK(SameScene(SceneSerializer::Serialize(reread), SceneSerializer::Serialize(p.s)));
        }
        p.Begin();
        GameObject *menu = Named(p.s, "Main Menu"), *hud = Named(p.s, "HUD Shooter"), *pop = Named(p.s, "Confirm Popup");
        CHECK(menu && hud && pop && menu->active && hud->active && pop->active);
        if (menu && hud && pop) {
            CHECK(menu->GetComponent<UIStyleVariant>()->themeName == "Retro Pixel" && menu->GetComponent<UIStyleVariant>()->style == "Pixel");
            CHECK(hud->GetComponent<UIStyleVariant>()->themeName == "Space Station" && hud->GetComponent<UIStyleVariant>()->style == "Holo");
            CHECK(pop->GetComponent<UIStyleVariant>()->themeName == "Comic" && pop->GetComponent<UIStyleVariant>()->style == "Cartoon");
            CHECK(Under(menu, "PlayButton")->GetComponent<UIButton>()->shape == UIShape::Rectangle);       // Pixel
            CHECK(Under(pop, "Yes")->GetComponent<UIButton>()->shape == UIShape::Pill ? false : true);      // Cartoon is rounded, not Pixel
            CHECK(Under(menu, "PlayButton")->GetComponent<UIButton>()->color.r != Under(pop, "Yes")->GetComponent<UIButton>()->color.r);
            p.Press(Under(pop, "No"));                  // the popup closes; the others are untouched
            CHECK(!pop->active && menu->active && hud->active);
        }
        CHECK(UITheme::Find(&p.s, "") == nullptr);
    }

    TEST_MAIN_RESULT();
}
