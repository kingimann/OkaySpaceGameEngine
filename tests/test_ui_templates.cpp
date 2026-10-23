#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Scene/UITemplates.hpp"
#include "okay/Components/UIElement.hpp"
#include <functional>
#include <string>

using namespace okay;
using namespace okay::UITemplates;

// Prebuilt screens and kit pieces: each lands on one shared Canvas, centred screens
// are actually centred, and every widget sits on screen and inside its parent's
// rect (a typo'd offset shows up as a child poking out of its card).
namespace {

constexpr float W = 1280.0f, H = 720.0f;

int CountCanvases(Scene& s) {
    int n = 0;
    for (const auto& o : s.Objects()) if (o->GetComponent<Canvas>()) ++n;
    return n;
}

// Walks a subtree; returns how many widgets were out of place (and logs them).
int Misplaced(GameObject* root, int& widgets) {
    int bad = 0;
    std::function<void(GameObject*)> walk = [&](GameObject* g) {
        Vec2 o, sz;
        if (GetUIScreenRect(g, W, H, o, sz)) {
            ++widgets;
            const float e = 1.0f;
            bool ok = o.x >= -e && o.y >= -e && o.x + sz.x <= W + e && o.y + sz.y <= H + e;
            if (GameObject* p = OwningUIParent(g)) {
                Vec2 po, ps;
                if (GetUIScreenRect(p, W, H, po, ps))
                    ok = ok && o.x >= po.x - e && o.y >= po.y - e &&
                         o.x + sz.x <= po.x + ps.x + e && o.y + sz.y <= po.y + ps.y + e;
            }
            if (!ok) {
                ++bad;
                std::cout << "  misplaced: " << g->name << " at " << o.x << "," << o.y
                          << " size " << sz.x << "x" << sz.y << "\n";
            }
        }
        for (Transform* c : g->transform->Children()) if (c && c->gameObject) walk(c->gameObject);
    };
    walk(root);
    return bad;
}

} // namespace

int main() {
    RUN_SUITE("ui_templates");

    struct Screen { const char* name; GameObject* (*fn)(Scene&); };
    const Screen screens[] = {
        {"Main Menu", &AddMainMenu}, {"Pause Menu", &AddPauseMenu}, {"Settings", &AddSettings},
        {"HUD", &AddHUD}, {"Dialog Box", &AddDialogBox}, {"Health Bar", &AddHealthBar},
        {"Lobby", &AddLobby}, {"Scoreboard", &AddScoreboard}, {"Game Over", &AddGameOver},
        {"Loading Screen", &AddLoadingScreen}, {"Shop", &AddShop}, {"Confirm Dialog", &AddConfirmDialog},
        {"Notification", &AddNotificationScreen}, {"Matchmaking", &AddMatchmaking}, {"Leaderboard", &AddLeaderboard},
    };

    {   // Every screen in one scene: one Canvas, one EventSystem, all widgets placed.
        Scene s;
        for (const Screen& sc : screens) {
            GameObject* root = sc.fn(s);
            CHECK(root != nullptr);
            if (!root) continue;
            CHECK(root->name == sc.name);
            int widgets = 0;
            const int bad = Misplaced(root, widgets);
            if (bad) std::cout << "  in " << sc.name << "\n";
            CHECK(bad == 0);
            CHECK(widgets >= 3);
        }
        CHECK(CountCanvases(s) == 1);
        int events = 0;
        for (const auto& o : s.Objects()) if (o->GetComponent<EventSystem>()) ++events;
        CHECK(events == 1);
    }

    {   // A centred screen is centred (Center anchor + {0,0}, not a negative half-size).
        Scene s;
        GameObject* menu = AddMainMenu(s);
        Vec2 o, sz;
        CHECK(GetUIScreenRect(menu, W, H, o, sz));
        CHECK_NEAR(o.x + sz.x * 0.5f, W * 0.5f, 0.5f);
        CHECK_NEAR(o.y + sz.y * 0.5f, H * 0.5f, 0.5f);
        // The Play button is the accent primary; Quit is a ghost (transparent).
        GameObject* play = nullptr; GameObject* quit = nullptr;
        for (Transform* c : menu->transform->Children()) {
            if (c->gameObject->name == "PlayButton") play = c->gameObject;
            if (c->gameObject->name == "QuitButton") quit = c->gameObject;
        }
        CHECK(play && quit);
        if (play && quit) {
            CHECK(play->GetComponent<UIButton>()->color.b > 0.9f);
            CHECK(quit->GetComponent<UIButton>()->color.a < 0.01f);
        }
    }

    {   // Modal dimmers stretch over any screen size.
        Scene s;
        GameObject* pause = AddPauseMenu(s);
        GameObject* dim = pause->transform->Children()[0]->gameObject;
        CHECK(dim->name == "Dim");
        Vec2 o, sz;
        CHECK(GetUIScreenRect(dim, 1920.0f, 1080.0f, o, sz));
        CHECK_NEAR(sz.x, 1920.0f, 0.5f);
        CHECK_NEAR(sz.y, 1080.0f, 0.5f);
    }

    {   // Kit pieces go inside the given parent (a card) and stay inside it.
        Scene s;
        GameObject* canvas = EnsureCanvas(s);
        GameObject* card = AddCard(s, canvas);
        CHECK(card->transform->Parent() == canvas->transform);
        using KitFn = GameObject* (*)(Scene&, GameObject*);
        const KitFn kit[] = {&AddBadge, &AddAvatar, &AddDivider, &AddSpinner, &AddSwitch};
        for (KitFn fn : kit) {
            GameObject* g = fn(s, card);
            CHECK(g && g->transform->Parent() == card->transform);
        }
        int widgets = 0;
        CHECK(Misplaced(card, widgets) == 0);
        CHECK(widgets >= 8);
        // List rows, headings and notifications live on the canvas fine too.
        GameObject* row = AddListRow(s, canvas);
        GameObject* head = AddHeading(s, canvas);
        GameObject* toast = AddNotification(s, canvas);
        widgets = 0;
        CHECK(Misplaced(row, widgets) == 0 && Misplaced(head, widgets) == 0 && Misplaced(toast, widgets) == 0);
        for (int k = 0; k < 4; ++k) {
            GameObject* b = AddButtonOfKind(s, canvas, (ButtonKind)k);
            CHECK(b && b->GetComponent<UIButton>());
        }
        CHECK(CountCanvases(s) == 1);
    }

    {   // Avatars get a stable colour per name and show the initial.
        Scene s;
        GameObject* canvas = EnsureCanvas(s);
        GameObject* a1 = okay::UITemplates::detail::Avatar(s, canvas, "A", "maya", UIAnchor::TopLeft, {0, 0});
        GameObject* a2 = okay::UITemplates::detail::Avatar(s, canvas, "B", "maya", UIAnchor::TopLeft, {0, 0});
        Color c1 = a1->GetComponent<UIPanel>()->color, c2 = a2->GetComponent<UIPanel>()->color;
        CHECK(c1.r == c2.r && c1.g == c2.g && c1.b == c2.b);
        auto* t = a1->transform->Children()[0]->gameObject->GetComponent<TextRenderer>();
        CHECK(t && t->text == "M");
        CHECK(a1->GetComponent<UIPanel>()->shape == UIShape::Circle);
    }

    {   // At another window size the canvas scales (640x360 = 0.5) and widgets inside
        // panels are clickable exactly where they're drawn, via the host's rectOf.
        const float sw = 640.0f, sh = 360.0f;
        UICanvas::Set(sw, sh);
        UIWorld().rectOf = [&](GameObject* go, Vec2& o, Vec2& sz) { return GetUIScreenRect(go, sw, sh, o, sz); };
        Scene s("Scaled");
        GameObject* pause = AddPauseMenu(s);
        GameObject* card = pause->transform->Children()[1]->gameObject;
        GameObject* resume = nullptr;
        for (Transform* c : card->transform->Children()) if (c->gameObject->name == "ResumeButton") resume = c->gameObject;
        CHECK(resume != nullptr);
        GameObject* settings = AddSettings(s);
        GameObject* music = nullptr;
        for (Transform* c : settings->transform->Children()) if (c->gameObject->name == "MusicSlider") music = c->gameObject;
        CHECK(music != nullptr);
        settings->active = false;   // under the pause card: click one screen at a time
        s.Start();
        if (resume) {
            Vec2 o, sz;
            CHECK(GetUIScreenRect(resume, sw, sh, o, sz));
            CHECK_NEAR(sz.x, 148.0f, 0.5f);   // 296 design px at half scale
            auto* b = resume->GetComponent<UIButton>();
            const Vec2 c{o.x + sz.x * 0.5f, o.y + sz.y * 0.5f};
            CHECK(b->Contains(c));
            CHECK(!b->Contains(Vec2{o.x - 4.0f, c.y}));
            // Its unscaled, parent-less spot (the old hit-test) no longer clicks it.
            CHECK(!b->Contains(Vec2{32.0f + 10.0f, 110.0f + 10.0f}));
            Input::FeedMouse(c, 0);
            Input::FeedMouse(c, 1u << 0);
            s.Update(0.016f);
            CHECK(b->WasClicked());
            Input::FeedMouse(c, 0);
            s.Update(0.016f);
        }
        if (music) {   // dragging at 3/4 of the drawn track sets 0.75
            pause->active = false; settings->active = true;
            Vec2 o, sz;
            CHECK(GetUIScreenRect(music, sw, sh, o, sz));
            const Vec2 at{o.x + sz.x * 0.75f, o.y + sz.y * 0.5f};
            Input::FeedMouse(at, 0);
            Input::FeedMouse(at, 1u << 0);
            s.Update(0.016f);
            CHECK_NEAR(music->GetComponent<UISlider>()->value, 0.75f, 0.02f);
            Input::FeedMouse(at, 0);
        }
        UIWorld().rectOf = nullptr;
        UICanvas::Set(1280.0f, 720.0f);
    }

    {   // Survives a save / load round trip (the editor saves scenes as text).
        Scene s;
        AddLobby(s);
        AddShop(s);
        const std::string text = SceneSerializer::Serialize(s);
        Scene t;
        CHECK(SceneSerializer::Deserialize(t, text));
        CHECK(t.Objects().size() == s.Objects().size());
        int panels = 0;
        for (const auto& o : t.Objects()) if (auto* p = o->GetComponent<UIPanel>()) { ++panels; (void)p; }
        CHECK(panels > 10);
    }

    TEST_MAIN_RESULT();
}
