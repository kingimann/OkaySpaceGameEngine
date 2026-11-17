// The shipped sample scenes examples/ui_library/ui_library_4.okayscene (themes, styles, icons) and examples/ui_library/ui_starter_pack.okayscene
// (a whole game UI: main menu, settings, HUD, pause menu, game over). The test loads each file from disk and plays it like a person would:
// click a theme and see the page change, click Play and see the HUD, Pause, Resume, Give up and Try again.
//
// The files are made by this test: run it with OKAY_WRITE_UI_LIBRARY_EXAMPLE4=<folder> to write them there (after a change to the builders
// or the serializer). Without it the test only reads the committed files. Written for this engine; no other project's code or assets.
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
        std::string err;
        const bool ok1 = SceneSerializer::SaveToFile(a, std::string(out) + "/ui_library_4.okayscene", &err);
        const bool ok2 = SceneSerializer::SaveToFile(b, std::string(out) + "/ui_starter_pack.okayscene", &err);
        std::printf("  wrote ui_library_4.okayscene and ui_starter_pack.okayscene to %s: %s\n", out, ok1 && ok2 ? "ok" : "FAILED");
        CHECK(ok1 && ok2);
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
            CHECK(WithoutGuids(SceneSerializer::Serialize(reread)) == WithoutGuids(SceneSerializer::Serialize(p.s)));
        }
        int icons = 0, vectorIcons = 0;
        for (const auto& o : p.s.Objects()) if (auto* ib = o->GetComponent<UIButton>()) { if (!ib->interactable) { ++icons; vectorIcons += !uilib::IconNameOf(ib->icon).empty() && uilib::FindIcon(uilib::IconNameOf(ib->icon)); } }
        CHECK(vectorIcons == 40);   // forty vector icons (the page has a few other pictures)
        p.Begin();
        GameObject* back = p.s.Find("Backdrop");
        CHECK(back && back->GetComponent<UIPanel>());
        const Color before = back->GetComponent<UIPanel>()->color;
        CHECK(UITheme::Find(&p.s, "") == nullptr);   // no theme yet: the page uses the launcher colours
        p.Press(p.s.Find("Theme3"));                  // Neon
        UITheme* th = UITheme::Find(&p.s, "");
        CHECK(th && th->themeName == "Neon");
        CHECK(Prefs::GetString("ui.theme") == "Neon");
        const Color neon = back->GetComponent<UIPanel>()->color;
        CHECK(neon.r != before.r || neon.g != before.g || neon.b != before.b || neon.a != before.a);
        p.Press(p.s.Find("Theme2"));                  // Light
        const Color light = back->GetComponent<UIPanel>()->color;
        CHECK(light.r > neon.r + 0.2f);               // a light theme is lighter than Neon
        CHECK(p.s.Find("Style2")->GetComponent<UIButton>()->shape == UIShape::Pill);   // the Neon Pill style survived the theme switches
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
            CHECK(WithoutGuids(SceneSerializer::Serialize(reread)) == WithoutGuids(SceneSerializer::Serialize(p.s)));
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
        // the theme the pack was built with is in the scene
        UITheme* th = UITheme::Find(&p.s, "");
        CHECK(th && th->themeName == "Midnight Blue");
    }

    TEST_MAIN_RESULT();
}
