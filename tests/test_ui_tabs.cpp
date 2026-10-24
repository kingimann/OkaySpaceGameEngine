#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Scene/UIStyles.hpp"
#include <cmath>
#include <string>

using namespace okay;

// The 8x8 bitmap font's metrics (what the renderers measure with "pixel" UI font):
// 9 font pixels per character (8 + a 1px gap) at the given pixel size.
static float Mono(const std::string& s, float px) { return (float)s.size() * 9.0f * px; }

// One frame of input: hold/release the left mouse at `p`, then tick the scene.
static void Frame(Scene& s, Vec2 p, bool down, std::vector<char> keys = {}) {
    Input::ClearTypedText();
    Input::FeedKeys(keys);
    Input::FeedMouse(p, down ? 1u : 0u);
    s.Update(0.016f);
}

int main() {
    RUN_SUITE("ui_tabs");
    UICanvas::Set(1280, 720);

    // --- A default UITabs lays out exactly like the classic segmented control ----
    {
        UITabs tb;                                   // 300x40, One/Two/Three, Segmented
        UITabsLayout L = tb.Layout(300, 40, 1.0f, Mono);
        CHECK(L.tabs.size() == 3);
        CHECK(!L.overflow);
        for (int i = 0; i < 3; ++i) {
            CHECK_NEAR(L.tabs[i].x, 100.0f * i, 1e-3f);
            CHECK_NEAR(L.tabs[i].w, 100.0f, 1e-3f);
            CHECK_NEAR(L.tabs[i].h, 40.0f, 1e-3f);
            // Label centred in its segment, vertically centred on the 8px-high font.
            const float tw = Mono(tb.tabs[i], 2.0f);
            CHECK(L.tabs[i].label == tb.tabs[i]);   // short names are never shortened
            CHECK_NEAR(L.tabs[i].textX, 100.0f * i + (100.0f - tw) * 0.5f, 1e-3f);
            CHECK_NEAR(L.tabs[i].textY, (40.0f - 16.0f) * 0.5f, 1e-3f);
        }
        // The paint list: the pill track, then the selection inset by 2px (the same
        // rects the old renderers drew), then the three labels.
        std::vector<UITabsDraw> d = tb.Paint(300, 40, 1.0f, Mono);
        CHECK(d.size() == 5);
        if (d.size() == 5) {
            CHECK(d[0].kind == UITabsDraw::Kind::Fill && d[0].shape == UIShape::Pill);
            CHECK_NEAR(d[0].w, 300.0f, 1e-3f); CHECK_NEAR(d[0].h, 40.0f, 1e-3f);
            CHECK(d[1].kind == UITabsDraw::Kind::Fill);
            CHECK_NEAR(d[1].x, 2.0f, 1e-3f); CHECK_NEAR(d[1].y, 2.0f, 1e-3f);
            CHECK_NEAR(d[1].w, 96.0f, 1e-3f); CHECK_NEAR(d[1].h, 36.0f, 1e-3f);
            CHECK(d[2].kind == UITabsDraw::Kind::Text && d[2].text == "One");
            CHECK(d[4].kind == UITabsDraw::Kind::Text && d[4].text == "Three");
        }
        // Backward-compatible helpers agree with the layout.
        float ox, oy, w, h; tb.SegmentRect(1, ox, oy, w, h);
        CHECK_NEAR(ox, 100.0f, 1e-3f); CHECK_NEAR(w, 100.0f, 1e-3f);
        CHECK(tb.SegmentAt(299.0f) == 2);
        // The far edge used to map to -1 and then get clamped to tab 0 on click.
        CHECK(UITabs::HitTest(L, 300.0f, 20.0f) == UITabs::kNoHit);
        CHECK(UITabs::HitTest(L, 150.0f, 20.0f) == 1);
    }

    // --- Fit-to-text sizing + spacing, and long labels shortened with "..." ------
    {
        UITabs tb;
        tb.tabs = {"A", "Long name"};
        tb.sizing = UITabs::Sizing::FitText; tb.spacing = 6.0f; tb.padding = 10.0f;
        UITabsLayout L = tb.Layout(400, 40, 1.0f, Mono);
        CHECK_NEAR(L.tabs[0].w, 20.0f + Mono("A", 2.0f), 1e-3f);
        CHECK_NEAR(L.tabs[1].x, L.tabs[0].w + 6.0f, 1e-3f);
        CHECK_NEAR(L.tabs[1].w, 20.0f + Mono("Long name", 2.0f), 1e-3f);
        // Scale 2 (a 2x Canvas): every length doubles, so drawing and clicks agree
        // whichever scale the host lays out at.
        UITabsLayout L2 = tb.Layout(800, 80, 2.0f, Mono);
        CHECK_NEAR(L2.tabs[1].x, L.tabs[1].x * 2.0f, 1e-3f);
        CHECK_NEAR(L2.tabs[1].w, L.tabs[1].w * 2.0f, 1e-3f);
        CHECK_NEAR(L2.tabs[1].textX, L.tabs[1].textX * 2.0f, 1e-3f);

        UITabs eq;                                  // equal tabs too narrow for a name
        eq.tabs = {"Inventory and stuff", "B"};
        UITabsLayout E = eq.Layout(200, 40, 1.0f, Mono);
        const std::string& lab = E.tabs[0].label;
        CHECK(lab.size() > 3 && lab.compare(lab.size() - 3, 3, "...") == 0);
        CHECK(E.tabs[0].textX >= E.tabs[0].x);
        CHECK(E.tabs[0].textX + E.tabs[0].textW <= E.tabs[0].x + E.tabs[0].w + 1e-3f);
    }

    // --- Styles: vertical stacks rows with left-aligned text --------------------
    {
        UITabs tb;
        tb.style = UITabs::Style::Vertical; tb.padding = 12.0f; tb.spacing = 4.0f;
        UITabsLayout L = tb.Layout(160, 300, 1.0f, Mono);
        CHECK(L.vertical);
        CHECK_NEAR(L.tabs[0].y, 0.0f, 1e-3f);
        CHECK_NEAR(L.tabs[1].y, L.tabs[0].h + 4.0f, 1e-3f);
        CHECK_NEAR(L.tabs[2].w, 160.0f, 1e-3f);
        CHECK_NEAR(L.tabs[1].textX, 12.0f, 1e-3f);      // left-aligned after padding
        CHECK(UITabs::HitTest(L, 80.0f, L.tabs[2].y + 2.0f) == 2);
        // Every style produces a paint list with the selection and three labels.
        for (int st = 0; st < 5; ++st) {
            tb.style = (UITabs::Style)st;
            std::vector<UITabsDraw> d = tb.Paint(300, 40, 1.0f, Mono);
            int texts = 0; for (const auto& e : d) if (e.kind == UITabsDraw::Kind::Text) ++texts;
            CHECK(texts == 3);
        }
        // Underline: the indicator is a line at the bottom of the selected tab.
        tb.style = UITabs::Style::Underline; tb.indicatorThickness = 3.0f; tb.value = 1; tb.spacing = 0.0f;
        bool found = false;
        for (const auto& e : tb.Paint(300, 40, 1.0f, Mono))
            if (e.kind == UITabsDraw::Kind::Fill && std::fabs(e.h - 3.0f) < 1e-3f && std::fabs(e.x - 100.0f) < 1e-3f)
                found = std::fabs(e.y - 37.0f) < 1e-3f && std::fabs(e.w - 100.0f) < 1e-3f;
        CHECK(found);
    }

    // --- Icons and badges ---------------------------------------------------------
    {
        UITabs tb;
        tb.sizing = UITabs::Sizing::FitText;
        tb.SetIcon(0, "Assets/gear.png");
        tb.SetBadge(1, -1);                         // a dot
        tb.SetBadge(2, 150);                        // a number, capped at "99+"
        UITabsLayout L = tb.Layout(600, 40, 1.0f, Mono);
        CHECK_NEAR(L.tabs[0].iconSize, 16.0f, 1e-3f);
        CHECK(L.tabs[0].textX > L.tabs[0].iconX + 16.0f);   // text after the icon
        CHECK(L.tabs[1].badgeW > 0.0f && L.tabs[1].badgeText.empty());
        CHECK(L.tabs[2].badgeText == "99+");
        CHECK(L.tabs[2].badgeX >= L.tabs[2].textX + L.tabs[2].textW);
        CHECK(L.tabs[2].badgeX + L.tabs[2].badgeW <= L.tabs[2].x + L.tabs[2].w + 1e-3f);
        int icons = 0; for (const auto& e : tb.Paint(600, 40, 1.0f, Mono)) if (e.kind == UITabsDraw::Kind::Icon) ++icons;
        CHECK(icons == 1);
        tb.SetBadge(7, 3);                          // out of range: ignored
        CHECK(tb.badges.size() == 3);
    }

    // --- Overflow: scroll clamping, arrows, hit-tests clipped to the strip -------
    {
        UITabs tb;
        tb.minTabSize = 150.0f;                     // 3 x 150 = 450 > 300
        UITabsLayout L = tb.Layout(300, 40, 1.0f, Mono);
        CHECK(L.overflow);
        CHECK_NEAR(L.prevW, 22.0f, 1e-3f);
        CHECK_NEAR(L.viewX, 22.0f, 1e-3f); CHECK_NEAR(L.viewW, 256.0f, 1e-3f);
        CHECK_NEAR(L.maxScroll, 450.0f - 256.0f, 1e-3f);
        CHECK(UITabs::HitTest(L, 5.0f, 20.0f) == UITabs::kPrevArrow);
        CHECK(UITabs::HitTest(L, 295.0f, 20.0f) == UITabs::kNextArrow);
        tb.SetScroll(10000.0f);                     // clamps to the end
        L = tb.Layout(300, 40, 1.0f, Mono);
        CHECK_NEAR(L.scroll, L.maxScroll, 1e-3f);
        CHECK_NEAR(L.tabs[2].x + L.tabs[2].w, 300.0f - 22.0f, 1e-3f);   // last tab ends at the arrow
        CHECK(!L.tabs[0].visible);
        CHECK(UITabs::HitTest(L, 23.0f, 20.0f) == 1);   // just inside the strip: tab 1 (tab 0 is scrolled away)
    }

    // --- Clicking in a scene: selection, gaps, disabled, the selected tab revealed ---
    {
        Scene s("Click"); s.physicsEnabled = false; s.uiFont = "pixel";
        GameObject* go = s.CreateGameObject("Tabs");
        auto* tb = go->AddComponent<UITabs>();
        tb->position = {100, 100}; tb->size = {300, 40};
        tb->spacing = 0.0f;
        s.Start();
        Frame(s, {350, 120}, true);   CHECK(tb->value == 2);   // third tab
        Frame(s, {350, 120}, false);
        Frame(s, {400, 120}, true);   CHECK(tb->value == 2);   // right edge: no jump to tab 0
        Frame(s, {400, 120}, false);
        tb->SetTabEnabled(0, false);
        Frame(s, {120, 120}, true);   CHECK(tb->value == 2);   // disabled tab ignores clicks
        Frame(s, {120, 120}, false);
        CHECK(tb->HoveredTab() == -1);
        Frame(s, {250, 120}, false);  CHECK(tb->HoveredTab() == 1);

        // Overflowing: the newly selected tab scrolls into view; a click selects on
        // release, a drag scrolls instead.
        tb->SetTabEnabled(0, true);
        tb->tabs = {"A", "B", "C", "D", "E", "F"};
        tb->minTabSize = 100.0f;                     // 600 px of tabs in a 300 px bar
        tb->Select(5);
        Frame(s, {0, 0}, false);
        UITabsLayout L = tb->Layout(300, 40);
        CHECK(L.tabs[5].x + L.tabs[5].w <= L.viewX + L.viewW + 1e-3f);
        CHECK(L.tabs[5].x >= L.viewX - 1e-3f);
        const float before = tb->Scroll();
        Frame(s, {250, 120}, true);                  // press on a tab...
        Frame(s, {200, 120}, true);                  // ...drag 50 px left
        CHECK(tb->value == 5);
        Frame(s, {200, 120}, false);
        CHECK(tb->value == 5);                       // a drag never selects
        CHECK(tb->Scroll() >= before - 1e-3f);       // already at the end: clamped
        Frame(s, {105, 120}, true); Frame(s, {105, 120}, false);   // the left arrow
        CHECK(tb->Scroll() < before);
        L = tb->Layout(300, 40);
        const int target = UITabs::HitTest(L, 200.0f, 20.0f);
        CHECK(target >= 0);
        Frame(s, {300, 120}, true); CHECK(tb->value == 5);   // press: not yet
        Frame(s, {300, 120}, false); CHECK(tb->value == target);  // release: selected
    }

    // --- Hit-testing a scaled, anchored, nested bar (the host's rectOf) ----------
    {
        Scene s("Scaled"); s.physicsEnabled = false; s.uiFont = "pixel";
        GameObject* cvGo = s.CreateGameObject("Canvas");
        auto* cv = cvGo->AddComponent<Canvas>();
        cv->scaleMode = Canvas::ScaleMode::ScaleWithScreenSize;
        cv->referenceResolution = {1280, 720};
        GameObject* panel = s.CreateGameObject("Panel");
        panel->transform->SetParent(cvGo->transform, false);
        auto* pn = panel->AddComponent<UIPanel>();
        pn->anchor = UIAnchor::Center; pn->position = {0, 0}; pn->size = {600, 400};
        GameObject* go = s.CreateGameObject("Tabs");
        go->transform->SetParent(panel->transform, false);
        auto* tb = go->AddComponent<UITabs>();
        tb->anchor = UIAnchor::TopCenter; tb->position = {0, 20}; tb->size = {300, 40};
        tb->sizing = UITabs::Sizing::FitText; tb->spacing = 10.0f;
        const float sw = 2560, sh = 1440;            // a 2x screen
        UICanvas::Set(sw, sh);
        UIWorld().rectOf = [&](GameObject* g, Vec2& o, Vec2& sz) { return GetUIScreenRect(g, sw, sh, o, sz); };
        s.Start();
        Vec2 o, sz; float k = 0.0f;
        CHECK(tb->ScreenFrame(o, sz, k));
        CHECK_NEAR(k, 2.0f, 1e-3f);
        CHECK_NEAR(sz.x, 600.0f, 1e-3f);
        // Click the middle of the third tab where it's drawn (layout at scale 2).
        UITabsLayout L = tb->Layout(sz.x, sz.y, k);
        const UITabsLayout::Tab& t2 = L.tabs[2];
        Frame(s, {o.x + t2.x + t2.w * 0.5f, o.y + t2.h * 0.5f}, true);
        CHECK(tb->value == 2);
        Frame(s, {o.x + t2.x + t2.w * 0.5f, o.y + t2.h * 0.5f}, false);
        // The gap between tab 0 and tab 1 selects nothing.
        const float gapX = o.x + L.tabs[0].x + L.tabs[0].w + 10.0f;   // 20 px gap at 2x
        Frame(s, {gapX, o.y + 10.0f}, true); CHECK(tb->value == 2);
        Frame(s, {gapX, o.y + 10.0f}, false);

        // Stretch anchor: width comes from the parent, height scales.
        tb->anchor = UIAnchor::StretchHorizontal; tb->position = {10, 20}; tb->size = {10, 40};
        tb->sizing = UITabs::Sizing::Equal; tb->spacing = 0.0f;
        CHECK(tb->ScreenFrame(o, sz, k));
        CHECK_NEAR(k, 2.0f, 1e-3f);
        CHECK_NEAR(sz.x, (600.0f - 20.0f) * 2.0f, 1e-3f);
        Frame(s, {o.x + sz.x * 0.5f, o.y + 10.0f}, true);   // middle of 3 equal tabs
        CHECK(tb->value == 1);
        Frame(s, {0, 0}, false);
        UIWorld().rectOf = nullptr;
        UICanvas::Set(1280, 720);
    }

    // --- Keyboard / gamepad: Q/E, shoulders, disabled skipped, typing ignored -----
    {
        Scene s("Keys"); s.physicsEnabled = false;
        auto* tb = s.CreateGameObject("Tabs")->AddComponent<UITabs>();
        tb->tabs = {"A", "B", "C", "D"};
        tb->SetTabEnabled(1, false);
        s.Start();
        Frame(s, {-50, -50}, false);
        Frame(s, {-50, -50}, false, {'e'}); CHECK(tb->value == 2);   // B is skipped
        Frame(s, {-50, -50}, false);
        Frame(s, {-50, -50}, false, {'q'}); CHECK(tb->value == 0);
        Frame(s, {-50, -50}, false);
        Frame(s, {-50, -50}, false, {'q'}); CHECK(tb->value == 0);   // stops at the start
        Frame(s, {-50, -50}, false);
        tb->wrapAround = true;
        Frame(s, {-50, -50}, false, {'q'}); CHECK(tb->value == 3);   // wraps to the end
        Frame(s, {-50, -50}, false);
        Input::FeedGamepad({0, 0}, 1u << 7);                          // R shoulder
        Frame(s, {-50, -50}, false); CHECK(tb->value == 0);
        Input::FeedGamepad({0, 0}, 0);
        // Custom keys, and no switching while a text field has focus.
        tb->nextKey = "tab";
        Frame(s, {-50, -50}, false, {'\t'}); CHECK(tb->value == 2);
        Frame(s, {-50, -50}, false);
        auto* in = s.CreateGameObject("Name")->AddComponent<UIInputField>();
        in->focused = true;
        in->position = {-1000, -1000};               // off screen so the clicks above don't move it
        tb->nextKey = "e";
        Frame(s, {-50, -50}, false, {'e'}); CHECK(tb->value == 2);
        in->focused = false;
        // A hidden bar (inactive parent) doesn't react.
        GameObject* holder = s.CreateGameObject("Holder");
        tb->gameObject->transform->SetParent(holder->transform, false);
        holder->active = false;
        Frame(s, {-50, -50}, false);
        Frame(s, {-50, -50}, false, {'e'}); CHECK(tb->value == 2);
        holder->active = true;
        Frame(s, {-50, -50}, false);
        Frame(s, {-50, -50}, false, {'e'}); CHECK(tb->value == 3);
        Frame(s, {-50, -50}, false);

        // Menu focus: Left / Right switch the focused bar's tab.
        tb->hotkeys = false;
        Frame(s, {-50, -50}, false); NavigateUI(s);
        CHECK(tb->IsFocused());
        Frame(s, {-50, -50}, false, {'a'}); NavigateUI(s);
        CHECK(tb->value == 2);
        Frame(s, {-50, -50}, false);
    }

    // --- Indicator animation (and switching it off) --------------------------------
    {
        Scene s("Anim"); s.physicsEnabled = false;
        auto* tb = s.CreateGameObject("Tabs")->AddComponent<UITabs>();
        s.Start();
        Frame(s, {-50, -50}, false);
        CHECK_NEAR(tb->IndicatorPosition(), 0.0f, 1e-4f);
        tb->Select(2);
        Frame(s, {-50, -50}, false);
        const float mid = tb->IndicatorPosition();
        CHECK(mid > 0.0f && mid < 2.0f);            // on its way
        for (int i = 0; i < 120; ++i) Frame(s, {-50, -50}, false);
        CHECK_NEAR(tb->IndicatorPosition(), 2.0f, 1e-4f);
        // Mid-slide the selection box sits between the two tabs.
        tb->animate = false;
        tb->Select(0);
        Frame(s, {-50, -50}, false);
        CHECK_NEAR(tb->IndicatorPosition(), 0.0f, 1e-4f);   // jumps
    }

    // --- Pages: shared pages stay shown, children of a hidden page are hidden -----
    {
        Scene s("Pages"); s.physicsEnabled = false;
        auto* tb = s.CreateGameObject("Tabs")->AddComponent<UITabs>();
        GameObject* pa = s.CreateGameObject("PageA");
        GameObject* pb = s.CreateGameObject("PageB");
        GameObject* btn = s.CreateGameObject("PlayButton");
        btn->AddComponent<UIButton>();
        btn->transform->SetParent(pb->transform, false);
        tb->pages = {"PageA", "PageB", "PageA"};    // tabs 0 and 2 share a page
        s.Start();
        Frame(s, {-50, -50}, false);
        CHECK(pa->active && !pb->active);
        CHECK(UIHidden(btn));                       // under the hidden page
        tb->Select(2);
        CHECK(pa->active && !pb->active);           // the shared page stays shown
        tb->Select(1);
        CHECK(!pa->active && pb->active);
        CHECK(!UIHidden(btn));
        // A removed tab leaves a valid selection (and no on_change storm).
        tb->tabs = {"Only"};
        Frame(s, {-50, -50}, false);
        CHECK(tb->value == 0);
    }

    // --- Serialization: every new field round-trips; old scenes load unchanged ----
    {
        Scene s("Ser"); s.physicsEnabled = false;
        auto* tb = s.CreateGameObject("Tabs")->AddComponent<UITabs>();
        tb->tabs = {"Video", "Audio", "Controls"};
        tb->pages = {"V", "", "C"};
        tb->style = UITabs::Style::Folder; tb->sizing = UITabs::Sizing::FitText;
        tb->spacing = 3.5f; tb->padding = 9.0f; tb->fontScale = 1.5f; tb->minTabSize = 70.0f;
        tb->indicatorThickness = 2.5f; tb->iconSize = 20.0f;
        tb->hoverColor = Color{0.1f, 0.2f, 0.3f, 0.4f}; tb->badgeColor = Color{0.9f, 0.1f, 0.1f, 1.0f};
        tb->animate = false; tb->animSpeed = 7.0f;
        tb->hotkeys = true; tb->prevKey = "1"; tb->nextKey = "tab"; tb->wrapAround = true; tb->focusable = false;
        tb->SetIcon(0, "Assets/My Icons/video.png");
        tb->SetBadge(1, -1); tb->SetBadge(2, 12);
        tb->SetTabEnabled(2, false);
        std::string text = SceneSerializer::Serialize(s);
        Scene s2("x"); std::string err;
        CHECK(SceneSerializer::Deserialize(s2, text, &err));
        auto* t2 = s2.Find("Tabs") ? s2.Find("Tabs")->GetComponent<UITabs>() : nullptr;
        CHECK(t2 != nullptr);
        if (t2) {
            CHECK(t2->style == UITabs::Style::Folder && t2->sizing == UITabs::Sizing::FitText);
            CHECK_NEAR(t2->spacing, 3.5f, 1e-4f); CHECK_NEAR(t2->padding, 9.0f, 1e-4f);
            CHECK_NEAR(t2->fontScale, 1.5f, 1e-4f); CHECK_NEAR(t2->minTabSize, 70.0f, 1e-4f);
            CHECK_NEAR(t2->indicatorThickness, 2.5f, 1e-4f); CHECK_NEAR(t2->iconSize, 20.0f, 1e-4f);
            CHECK_NEAR(t2->hoverColor.b, 0.3f, 1e-4f); CHECK_NEAR(t2->badgeColor.r, 0.9f, 1e-4f);
            CHECK(!t2->animate); CHECK_NEAR(t2->animSpeed, 7.0f, 1e-4f);
            CHECK(t2->hotkeys && t2->wrapAround && !t2->focusable);
            CHECK(t2->prevKey == "1" && t2->nextKey == "tab");
            CHECK(t2->IconOf(0) == "Assets/My Icons/video.png");
            CHECK(t2->BadgeOf(1) == -1 && t2->BadgeOf(2) == 12);
            CHECK(!t2->IsTabEnabled(2) && t2->IsTabEnabled(0));
            CHECK(t2->pages.size() == 3 && t2->pages[2] == "C");
        }
        // A line written before the tab styles existed (no trailing v3 block) still
        // loads as the classic control, with the new keys/focus left off.
        Scene so("Old"); so.physicsEnabled = false;
        auto* to = so.CreateGameObject("Tabs")->AddComponent<UITabs>();
        to->tabs = {"A", "B"}; to->pages = {"PA", "PB"}; to->value = 1;
        std::string old = SceneSerializer::Serialize(so);
        {
            const std::size_t line = old.find("  uitabs ");
            const std::size_t cut = old.find("\"PB\"", line);
            const std::size_t eol = old.find('\n', line);
            CHECK(line != std::string::npos && cut != std::string::npos && cut < eol);
            old.erase(cut + 4, eol - (cut + 4));       // drop everything after the pages
        }
        Scene s3("o");
        CHECK(SceneSerializer::Deserialize(s3, old, &err));
        auto* t3 = s3.Find("Tabs") ? s3.Find("Tabs")->GetComponent<UITabs>() : nullptr;
        CHECK(t3 != nullptr);
        if (t3) {
            CHECK(t3->Count() == 2 && t3->value == 1);
            CHECK(t3->pages.size() == 2 && t3->pages[1] == "PB");
            CHECK(t3->style == UITabs::Style::Segmented && t3->sizing == UITabs::Sizing::Equal);
            CHECK(!t3->hotkeys && !t3->focusable);
            CHECK_NEAR(t3->spacing, 0.0f, 1e-6f);
        }
    }

    // --- Look presets set a style; copy style carries it ---------------------------
    {
        UITabs a, b;
        UIStyles::ApplyTabs(&a, 6);   // Sidebar
        CHECK(a.style == UITabs::Style::Vertical);
        UIStyles::ApplyTabs(&a, 3);   // Underline
        CHECK(a.style == UITabs::Style::Underline && a.sizing == UITabs::Sizing::FitText);
        UIStyles::ApplyTabs(&a, 0);   // the classic pill segmented control
        CHECK(a.style == UITabs::Style::Segmented && a.spacing == 0.0f);
        UIStyles::ApplyTabs(&a, 5);
        UIStyles::CopyStyle(a, b);
        CHECK(b.style == UITabs::Style::Pills && b.spacing == a.spacing);
    }

    // --- Script: select, badge and disable tabs by name / on the own object -------
    {
        Scene s("Script"); s.physicsEnabled = false;
        GameObject* bar = s.CreateGameObject("Menu");
        auto* tb = bar->AddComponent<UITabs>();
        auto* ctrl = s.CreateGameObject("Ctrl")->AddComponent<ScriptComponent>("okayscript");
        ctrl->LoadSource(
            "var v = -1; var t = \"\";\n"
            "function update() {\n"
            "  if (v < 0) {\n"
            "    ui_set_tab_enabled(\"Menu\", 1, 0);\n"
            "    ui_set_tab(\"Menu\", 1);\n"          // disabled: ignored
            "    ui_set_tab(\"Menu\", 2);\n"
            "    ui_set_tab_badge(\"Menu\", 0, 5);\n"
            "    v = ui_tab_value(\"Menu\");\n"
            "    t = ui_tab_text(\"Menu\");\n"
            "  }\n"
            "}\n");
        auto* own = bar->AddComponent<ScriptComponent>("okayscript");
        own->LoadSource(
            "var changes = 0; var mine = -1;\n"
            "function on_change() { changes = changes + 1; mine = tab_value(); }\n");
        s.Start();
        s.Update(0.016f);
        s.Update(0.016f);
        CHECK(tb->value == 2);
        CHECK(!tb->IsTabEnabled(1));
        CHECK(tb->BadgeOf(0) == 5);
        CHECK_NEAR(ctrl->VM()->GetGlobal("v").AsFloat(), 2.0f, 1e-4f);
        CHECK(ctrl->VM()->GetGlobal("t").AsString() == "Three");
        CHECK_NEAR(own->VM()->GetGlobal("changes").AsFloat(), 1.0f, 1e-4f);
        CHECK_NEAR(own->VM()->GetGlobal("mine").AsFloat(), 2.0f, 1e-4f);
    }

    TEST_MAIN_RESULT();
}
