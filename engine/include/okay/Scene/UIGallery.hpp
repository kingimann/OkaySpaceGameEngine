#pragma once
// The UI Gallery and the extra screens that go with it (File > Examples > UI Gallery).
//
// BuildGallery() fills a scene with one of everything the runtime UI offers, in the chosen theme:
// buttons in every role, checkbox / switch / radio buttons, slider, stepper, dropdown, input,
// health / mana / stamina bars, a radial and a rating, tabs, an inventory grid, a draggable window,
// a dialogue box with choices, floating damage numbers and a nameplate. The theme dropdown in it
// restyles the whole scene live (with ui_theme_preset from a tiny script).
//
// AddInventoryScreen / AddDialogueScreen are standalone screens for UI > Prebuilt Screens.
#include "okay/Scene/UITemplates.hpp"
#include "okay/Components/UIStyleVariant.hpp"
#include "okay/Components/UIDamageNumbers.hpp"
#include "okay/Components/UIDialogueBox.hpp"
#include "okay/Components/UIDraggable.hpp"
#include "okay/Components/UITheme.hpp"
#include "okay/Components/UIStepper.hpp"
#include "okay/Components/UIRating.hpp"
#include "okay/Components/UISlotGrid.hpp"
#include "okay/Components/UIInventoryBind.hpp"
#include "okay/Components/UISafeArea.hpp"
#include "okay/Components/UIBarBind.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include <string>

namespace okay { namespace UIGallery {

using namespace okay::UITemplates;
using namespace okay::UITemplates::detail;

/// The theme that styles `scene` (the Canvas's, made when missing). Returns it with `preset` baked in.
inline UITheme* EnsureTheme(Scene& scene, int preset) {
    GameObject* canvas = EnsureCanvas(scene);
    UITheme* th = canvas->GetComponent<UITheme>();
    if (!th) th = canvas->AddComponent<UITheme>();
    th->global = true;
    th->ApplyPreset(preset);
    return th;
}

/// The gallery's own pieces after any theme apply (also from `ui_theme_preset` in Play): slot grids
/// restyle, inset boxes take a shade, the Glass blobs show, the page backdrop follows the look.
/// It only touches slot grids and objects whose names start with Swatch, so other scenes are unaffected.
inline void StyleGalleryExtras(Scene& scene, UITheme* th) {
    if (!th) return;
    for (const auto& o : scene.Objects())            // slot buttons take their colours from the grid
        if (auto* sg = o->GetComponent<UISlotGrid>()) sg->Rebuild();
    // Panels that sit ON a card (inset boxes, title bars) keep a different shade from it: the theme
    // gives every panel the same colour, so these take their own from the theme's.
    auto mix = [](Color a, Color b, float t) { return Color(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a); };
    for (const auto& o : scene.Objects()) {
        auto* ip = o->GetComponent<UIPanel>();
        if (!ip) continue;
        GameObject* uiParent = OwningUIParent(o.get());
        const bool dialogueInCard = o->name == "Box" && uiParent && uiParent->GetComponent<UIPanel>() && o->transform->Parent() &&
                                    o->transform->Parent()->gameObject->GetComponent<UIDialogueBox>();
        const bool inset = o->name.rfind("SwatchInset", 0) == 0 || dialogueInCard, title = o->name.rfind("SwatchTitle", 0) == 0;
        if (!inset && !title) continue;
        Color c = mix(th->panel, title ? th->accent : th->text, title ? 0.30f : 0.09f);
        c.a = th->panel.a < 0.5f ? 0.35f : 1.0f;                      // a glass look stays see-through
        ip->color = c; ip->useGradient = false; ip->shadow = false; ip->topHighlight = false;
        ip->borderColor = th->border;
    }
    for (const auto& o : scene.Objects())              // colour behind the cards, so a see-through look has something to show
        if (o->name.rfind("SwatchBlob", 0) == 0) o->active = th->preset == (int)UITheme::Preset::Glass;
    if (GameObject* bg = scene.Find("SwatchBackdrop"))
        if (auto* p = bg->GetComponent<UIPanel>()) {
            const bool lightLook = (th->panel.r + th->panel.g + th->panel.b) > 2.1f;
            Color c = lightLook ? Color(th->panel.r * 0.86f, th->panel.g * 0.86f, th->panel.b * 0.86f, 1.0f)
                                : Color(th->panel.r * 0.45f, th->panel.g * 0.45f, th->panel.b * 0.45f, 1.0f);
            if (th->preset == (int)UITheme::Preset::Glass) c = Color::FromBytes(22, 34, 78);   // glass needs something to see through (see the blobs)
            if (th->preset == (int)UITheme::Preset::Neon)  c = Color::FromBytes(6, 2, 16);
            c.a = 1.0f;
            p->color = c; p->colorBottom = c; p->useGradient = false;
        }
}

/// Restyle everything in the scene with a built-in look. The page background follows the look.
inline void ApplyThemePreset(Scene& scene, int preset) {
    UITheme* th = EnsureTheme(scene, preset);
    th->Apply();
    StyleGalleryExtras(scene, th);
}

namespace gx {
inline GameObject* Card(Scene& s, GameObject* parent, const char* name, const char* title, Vec2 pos, Vec2 size) {
    GameObject* c = Panel(s, parent, name, UIAnchor::TopLeft, pos, size, slate::Panel(), 14.0f, true);
    c->GetComponent<UIPanel>()->blocksPointer = true;
    Caption(s, c, "Caption", title, UIAnchor::TopLeft, {20, 14}, size.x - 40.0f, slate::Accent());
    return c;
}
inline GameObject* Btn(Scene& s, GameObject* parent, const char* name, const char* label, Vec2 pos, Vec2 size, int variant) {
    GameObject* g = Button(s, parent, name, label, UIAnchor::TopLeft, pos, size, ButtonKind::Primary);
    auto* v = g->AddComponent<UIStyleVariant>();
    v->variant = variant;
    return g;
}
inline GameObject* Label(Scene& s, GameObject* parent, const char* name, const std::string& t, Vec2 pos, float w, Color col = slate::Dim(), float px = 13.0f) {
    return Text(s, parent, name, t, UIAnchor::TopLeft, pos, {w, 20.0f}, px, col);
}
inline GameObject* Bar(Scene& s, GameObject* parent, const char* name, Vec2 pos, Vec2 size, float v, Color fill, int role = 0) {
    GameObject* g = Child(s, parent, name);
    if (role > 0) g->AddComponent<UIStyleVariant>()->variant = role;
    auto* pb = g->AddComponent<UIProgressBar>();
    pb->anchor = UIAnchor::TopLeft; pb->position = pos; pb->size = size; pb->value = v;
    pb->shape = UIShape::Pill; pb->fill = fill; pb->background = Color::FromBytes(255, 255, 255, 28);
    return g;
}
}  // namespace gx

/// A bag screen: a title, a grid of item slots you can click, an info line and Close.
inline GameObject* AddInventoryScreen(Scene& scene) {
    GameObject* canvas = EnsureCanvas(scene);
    GameObject* root = Panel(scene, canvas, "Inventory", UIAnchor::Center, {0, 0}, {560, 460}, slate::Panel(), 14.0f, true);
    root->GetComponent<UIPanel>()->blocksPointer = true;
    Caption(scene, root, "Caption", "BACKPACK", UIAnchor::TopLeft, {28, 22}, 300, slate::Accent());
    Text(scene, root, "Title", "Inventory", UIAnchor::TopLeft, {28, 42}, {300, 34}, 26.0f, slate::Text(), 0, true);
    GameObject* grid = Child(scene, root, "Slots");
    auto* g = grid->AddComponent<UISlotGrid>();
    g->anchor = UIAnchor::TopLeft; g->position = {28, 96}; g->columns = 6; g->rows = 4;
    g->cell = {72, 72}; g->gap = 8; g->value = 0;
    g->color = slate::Field(); g->hoverColor = slate::FieldHover(); g->selectedColor = slate::Accent();
    const char* names[] = {"Sword", "Shield", "Potion", "Bow", "Key", "Map", "Gem", "Rope"};
    for (int i = 0; i < 8; ++i) g->SetLabel(i, names[i]);
    g->Rebuild();
    grid->AddComponent<UIInventoryBind>();   // shows the Player's Inventory (the names above are samples until there is one)
    Text(scene, root, "Info", "Click a slot to select it", UIAnchor::BottomLeft, {28, -24}, {320, 24}, 15.0f, slate::Dim());
    Button(scene, root, "Close", "Close", UIAnchor::BottomRight, {-24, -20}, {110, 40}, ButtonKind::Secondary);
    IntroAnim(root, UIAnimation::Intro::Pop);
    return root;
}

/// A dialogue screen driven by UIDialogueBox: speaker, typewriter text, up to three choices.
/// Script it with ui_dialogue_show("Mara", "Hello!", "Hi", "Go away") and on_dialogue_choice().
inline GameObject* AddDialogueScreen(Scene& scene) {
    GameObject* canvas = EnsureCanvas(scene);
    GameObject* root = Child(scene, canvas, "Dialogue");
    auto* db = root->AddComponent<UIDialogueBox>();
    db->startHidden = true;
    GameObject* box = Panel(scene, root, "Box", UIAnchor::BottomCenter, {0, -28}, {860, 210}, slate::Panel(), 14.0f, true);
    box->GetComponent<UIPanel>()->blocksPointer = true;
    Avatar(scene, box, "Portrait", "Mara", UIAnchor::TopLeft, {24, 24}, 64.0f);
    Text(scene, box, "Speaker", "Speaker", UIAnchor::TopLeft, {108, 22}, {500, 26}, 19.0f, slate::Accent(), 0, true);
    auto* body = Text(scene, box, "Body", "Line of dialogue...", UIAnchor::TopLeft, {108, 52}, {730, 70}, 17.0f)->GetComponent<TextRenderer>();
    body->wrap = true; body->vcenter = false; body->richText = true;
    for (int i = 0; i < 3; ++i) {
        const std::string nm = "Choice" + std::to_string(i + 1);
        GameObject* c = Button(scene, box, nm.c_str(), nm, UIAnchor::TopLeft, {108.0f + i * 250.0f, 140}, {238, 44}, ButtonKind::Secondary);
        c->active = false;
    }
    GameObject* next = Text(scene, box, "Next", "Press Space to continue", UIAnchor::BottomRight, {-24, -16}, {260, 20}, 13.0f, slate::Dim(), 2);
    next->active = false;
    IntroAnim(box, UIAnimation::Intro::SlideBottom);
    return root;
}

/// A window with a title bar you can drag, a close button and room for content.
inline GameObject* AddWindow(Scene& scene, GameObject* parent, const char* name, const char* title, Vec2 pos, Vec2 size) {
    GameObject* win = Panel(scene, parent, name, UIAnchor::TopLeft, pos, size, slate::Panel(), 12.0f, true);
    win->GetComponent<UIPanel>()->blocksPointer = true;
    {   // A window is its own Canvas layer: it, and everything on it, draws above the page and clicks reach it first.
        Canvas* page = OwningCanvas(parent);
        auto* wc = win->AddComponent<Canvas>();
        if (page) {
            wc->scaleMode = page->scaleMode; wc->referenceResolution = page->referenceResolution;
            wc->screenMatch = page->screenMatch; wc->matchWidthOrHeight = page->matchWidthOrHeight;
            wc->scaleFactor = page->scaleFactor;
        }
        wc->sortOrder = (page ? page->sortOrder : 0) + 1;
    }
    GameObject* bar = Panel(scene, win, "SwatchTitleBar", UIAnchor::TopLeft, {0, 0}, {size.x, 38}, slate::Card(), 12.0f);
    auto* bp = bar->GetComponent<UIPanel>();
    bp->cornerMask = UICornerTL | UICornerTR;
    auto* d = bar->AddComponent<UIDraggable>();
    d->dragParent = true; d->keepOnScreen = true; d->bringToFront = true;
    Text(scene, bar, "Title", title, UIAnchor::TopLeft, {14, 0}, {size.x - 60, 38}, 15.0f, slate::Text(), 0, true);
    GameObject* x = Button(scene, bar, "Close", "x", UIAnchor::TopRight, {-6, 5}, {28, 28}, ButtonKind::Ghost);
    auto* xb = x->GetComponent<UIButton>();
    xb->clickTarget = name; xb->clickFunction = "HideObject";
    return win;
}

/// Make a nameplate's health bar live: it fills from the `healthFrac` value that a Health Stat
/// publishes (the player's health), so it moves as the character takes damage or heals.
inline void BindNameplateHealth(GameObject* bar) {
    if (!bar) return;
    auto* b = bar->AddComponent<UIBarBind>();
    b->var = "healthFrac"; b->min = 0.0f; b->max = 1.0f;
}

/// A nameplate: a name, a level badge and a thin health bar that follows the Health Stat. Add it as a
/// world-space UI object and make it a child of the character: it follows and faces the camera.
inline GameObject* AddNameplate(Scene& scene, bool inWorld) {
    GameObject* root = inWorld ? scene.CreateGameObject("Nameplate") : Child(scene, EnsureCanvas(scene), "Nameplate");
    if (inWorld) root->AddComponent<WorldSpaceUI>();
    GameObject* card = Panel(scene, root, "SwatchPlate", UIAnchor::TopLeft, {0, 0}, {180, 46}, Color::FromBytes(16, 18, 24, 190), 10.0f);
    Text(scene, card, "Name", "Goblin", UIAnchor::TopLeft, {10, 4}, {110, 22}, 15.0f, slate::Text(), 0, true);
    Badge(scene, card, "SwatchLevel", "Lv 7", UIAnchor::TopRight, {-8, 4}, slate::Warning());
    BindNameplateHealth(gx::Bar(scene, card, "Health", {10, 30}, {160, 8}, 0.72f, slate::Danger(), (int)UITheme::Variant::Danger));
    return root;
}

/// Pick the widgets of one preset: a dropdown in the header that restyles the scene.
/// `portrait` lays the three cards out one under the other in a scroll view, on a 440 x 960 canvas, for phones.
inline GameObject* BuildGallery(Scene& scene, int preset = (int)UITheme::Preset::ModernFlat, bool portrait = false) {
    GameObject* canvas = EnsureCanvas(scene);
    if (portrait)
        if (auto* cv = canvas->GetComponent<Canvas>()) cv->referenceResolution = {440.0f, 960.0f};
    GameObject* root = Group(scene, canvas, "UI Gallery");
    // Page background: kept as-is by the theme (the name starts with Swatch).
    Panel(scene, root, "SwatchBackdrop", UIAnchor::StretchFull, {0, 0}, {0, 0}, slate::Backdrop(), 0.0f);
    {
        auto* bp = scene.Find("SwatchBackdrop")->GetComponent<UIPanel>();
        bp->borderWidth = 0.0f;
    }
    // On a phone everything but the backdrop sits in a Safe Area, clear of the notch and the home bar.
    GameObject* content = root;
    if (portrait) { content = Child(scene, root, "SafeArea"); content->AddComponent<UISafeArea>(); }
    {   // bright shapes behind the cards: only shown in the Glass look
        struct Blob { const char* name; Color col; Vec2 pos, size; };
        const Blob blobs[] = {
            {"SwatchBlobA", Color::FromBytes(255, 70, 150), portrait ? Vec2{20, 160} : Vec2{150, 150}, {320, 320}},
            {"SwatchBlobB", Color::FromBytes(60, 190, 255), portrait ? Vec2{150, 520} : Vec2{560, 330}, {360, 360}},
            {"SwatchBlobC", Color::FromBytes(255, 190, 60), portrait ? Vec2{40, 800} : Vec2{960, 120}, {300, 300}}};
        for (const Blob& b : blobs) {
            GameObject* g = Panel(scene, root, b.name, UIAnchor::TopLeft, b.pos, b.size, b.col, 0.0f);
            auto* bp = g->GetComponent<UIPanel>();
            bp->shape = UIShape::Circle; bp->borderWidth = 0.0f;
            g->active = false;
        }
    }
    const float titleX = portrait ? 24.0f : 32.0f;
    Text(scene, content, "Title", "UI Gallery", UIAnchor::TopLeft, {titleX, 14}, {360, 40}, 30.0f, slate::Text(), 0, true);
    Text(scene, content, "Subtitle", portrait ? "Scroll down. Pick a look below." : "Every widget, in any theme. Pick a look on the right.",
         UIAnchor::TopLeft, {titleX + 2, 50}, {portrait ? 400.0f : 560.0f, 20}, 14.0f, slate::Dim());
    if (!portrait) gx::Label(scene, content, "ThemeLabel", "Theme", {880, 24}, 80, slate::Dim(), 14.0f);
    {   // theme picker
        GameObject* pick = Child(scene, content, "ThemePick");
        {   // its own Canvas layer above the window's, so the open list draws over every card and the window
            Canvas* page = OwningCanvas(content);
            auto* pc = pick->AddComponent<Canvas>();
            if (page) {
                pc->scaleMode = page->scaleMode; pc->referenceResolution = page->referenceResolution;
                pc->screenMatch = page->screenMatch; pc->matchWidthOrHeight = page->matchWidthOrHeight;
                pc->scaleFactor = page->scaleFactor;
            }
            pc->sortOrder = (page ? page->sortOrder : 0) + 2;
        }
        auto* dd = pick->AddComponent<UIDropdown>();
        dd->anchor = UIAnchor::TopLeft; dd->position = portrait ? Vec2{24, 76} : Vec2{944, 14}; dd->size = portrait ? Vec2{392, 38} : Vec2{304, 38};
        dd->options.clear();
        for (int i = 1; i < UITheme::kPresetCount; ++i) dd->options.push_back(UITheme::PresetName(i));
        // every look is listed (maxVisible would hide the last ones: the list does not scroll)
        dd->value = preset >= 1 && preset < UITheme::kPresetCount ? preset - 1 : 0;
        pick->AddComponent<ScriptComponent>()->LoadSource(
            "function on_change() { ui_theme_preset(ui_dropdown_value(\"ThemePick\") + 1); }\n");
    }
    GameObject* cardParent = content;
    if (portrait) {   // the cards scroll: one column, one card after the other
        GameObject* sv = Child(scene, content, "SwatchScroll");
        auto* sc = sv->AddComponent<UIScrollView>();
        sc->anchor = UIAnchor::TopLeft; sc->position = {0, 124}; sc->size = {440, 780};
        sc->background = Color(0, 0, 0, 0); sc->autoContent = true; sc->contentHeight = 3 * 640.0f + 8.0f;
        cardParent = sv;
    }
    auto cardAt = [&](int i) { return portrait ? Vec2{24.0f, 8.0f + 640.0f * i} : Vec2{32.0f + 416.0f * i, 86.0f}; };

    // ---- Column 1: buttons and inputs ----
    GameObject* c1 = gx::Card(scene, cardParent, "Buttons", "Buttons and inputs", cardAt(0), {392, 618});
    gx::Btn(scene, c1, "Primary", "Primary", {20, 40}, {170, 40}, (int)UITheme::Variant::Primary);
    gx::Btn(scene, c1, "Secondary", "Secondary", {202, 40}, {170, 40}, (int)UITheme::Variant::Secondary);
    gx::Btn(scene, c1, "Danger", "Danger", {20, 90}, {170, 40}, (int)UITheme::Variant::Danger);
    gx::Btn(scene, c1, "Ghost", "Ghost", {202, 90}, {170, 40}, (int)UITheme::Variant::Ghost);
    gx::Btn(scene, c1, "Outline", "Outline", {20, 140}, {170, 40}, (int)UITheme::Variant::Outline);
    gx::Btn(scene, c1, "Pill", "Pill", {202, 140}, {170, 40}, (int)UITheme::Variant::Pill);
    {
        GameObject* g = Child(scene, c1, "Music"); auto* t = g->AddComponent<UIToggle>();
        t->anchor = UIAnchor::TopLeft; t->position = {20, 202}; t->size = {26, 26}; t->label = "Music"; t->on = true;
        t->cornerRadius = 6.0f;
        GameObject* g2 = Child(scene, c1, "Fullscreen"); auto* t2 = g2->AddComponent<UIToggle>();
        t2->anchor = UIAnchor::TopLeft; t2->position = {170, 202}; t2->size = {48, 26}; t2->label = "Fullscreen";
        t2->style = UIToggle::Style::Switch;
        const char* q[] = {"Low", "Medium", "High"};
        for (int i = 0; i < 3; ++i) {
            GameObject* r = Child(scene, c1, q[i]); auto* tr = r->AddComponent<UIToggle>();
            tr->anchor = UIAnchor::TopLeft; tr->position = {20.0f + i * 124.0f, 244}; tr->size = {24, 24};
            tr->style = UIToggle::Style::Radio; tr->radioGroup = "Quality"; tr->label = q[i]; tr->on = (i == 1);
        }
    }
    gx::Label(scene, c1, "VolumeLabel", "Volume", {20, 286}, 120);
    {
        GameObject* g = Child(scene, c1, "Volume"); auto* sl = g->AddComponent<UISlider>();
        sl->anchor = UIAnchor::TopLeft; sl->position = {20, 308}; sl->size = {352, 24}; sl->value = 0.7f;
        sl->trackShape = UIShape::Pill; sl->showValue = false;
    }
    gx::Label(scene, c1, "PlayersLabel", "Players", {20, 346}, 120);
    {
        GameObject* g = Child(scene, c1, "Players"); auto* st = g->AddComponent<UIStepper>();
        st->anchor = UIAnchor::TopLeft; st->position = {20, 368}; st->size = {170, 38}; st->value = 4; st->minValue = 1; st->maxValue = 8;
        GameObject* g2 = Child(scene, c1, "Rating"); auto* rt = g2->AddComponent<UIRating>();
        rt->anchor = UIAnchor::TopLeft; rt->position = {212, 368}; rt->size = {160, 38}; rt->value = 4;
    }
    gx::Label(scene, c1, "ServerLabel", "Region", {20, 418}, 120);
    {
        GameObject* g = Child(scene, c1, "Region"); auto* dd = g->AddComponent<UIDropdown>();
        dd->anchor = UIAnchor::TopLeft; dd->position = {20, 440}; dd->size = {352, 38};
        dd->options = {"Europe", "North America", "Asia", "Oceania"}; dd->value = 0;
        GameObject* g2 = Child(scene, c1, "Name"); auto* in = g2->AddComponent<UIInputField>();
        in->anchor = UIAnchor::TopLeft; in->position = {20, 492}; in->size = {352, 40}; in->placeholder = "Your name"; in->text = "Alex";
    }
    Button(scene, c1, "Play", "Play", UIAnchor::TopLeft, {20, 556}, {352, 44}, ButtonKind::Primary)->AddComponent<UIStyleVariant>()->variant = (int)UITheme::Variant::Pill;

    // ---- Column 2: bars and feedback ----
    GameObject* c2 = gx::Card(scene, cardParent, "Bars", "Bars and feedback", cardAt(1), {392, 618});
    gx::Label(scene, c2, "HealthLabel", "Health", {20, 40}, 100, slate::Text(), 14.0f);
    gx::Label(scene, c2, "ManaLabel", "Mana", {20, 86}, 100, slate::Text(), 14.0f);
    gx::Label(scene, c2, "StaminaLabel", "Stamina", {20, 132}, 100, slate::Text(), 14.0f);
    {
        GameObject* hp = gx::Bar(scene, c2, "HealthBar", {20, 62}, {352, 16}, 0.74f, slate::Danger(), (int)UITheme::Variant::Danger);
        auto* pb = hp->GetComponent<UIProgressBar>(); pb->trailDelay = 0.6f; pb->showPercent = true;
        pb->textFormat = "{n} / 100";
        GameObject* mp = gx::Bar(scene, c2, "ManaBar", {20, 108}, {352, 16}, 0.55f, slate::Accent(), (int)UITheme::Variant::Primary);
        mp->GetComponent<UIProgressBar>()->segments = 10; mp->GetComponent<UIProgressBar>()->shape = UIShape::Rounded;
        GameObject* sp = gx::Bar(scene, c2, "StaminaBar", {20, 154}, {352, 16}, 0.88f, slate::Success());
        auto* sb = sp->GetComponent<UIProgressBar>(); sb->gradientFill = true; sb->fillEnd = slate::Warning(); sb->colorByValue = false;
    }
    gx::Label(scene, c2, "RadialLabel", "Progress ring and spinner", {20, 196}, 300);
    {
        GameObject* g = Child(scene, c2, "Ring"); auto* r = g->AddComponent<UIRadialProgress>();
        r->anchor = UIAnchor::TopLeft; r->position = {24, 224}; r->size = {96, 96}; r->value = 0.65f; r->thickness = 10.0f; r->showPercent = true;
        GameObject* g2 = Child(scene, c2, "Pie"); auto* r2 = g2->AddComponent<UIRadialProgress>();
        r2->anchor = UIAnchor::TopLeft; r2->position = {146, 224}; r2->size = {96, 96}; r2->value = 0.4f; r2->thickness = 0.0f;
        GameObject* g3 = Child(scene, c2, "Spinner"); auto* r3 = g3->AddComponent<UIRadialProgress>();
        r3->anchor = UIAnchor::TopLeft; r3->position = {282, 236}; r3->size = {72, 72}; r3->value = 0.25f; r3->thickness = 7.0f; r3->spin = true;
    }
    gx::Label(scene, c2, "TabsLabel", "Tabs", {20, 338}, 100);
    {
        GameObject* g = Child(scene, c2, "Tabs"); auto* tb = g->AddComponent<UITabs>();
        tb->anchor = UIAnchor::TopLeft; tb->position = {20, 360}; tb->size = {352, 40};
        tb->tabs = {"Stats", "Skills", "Map", "Log"};
    }
    gx::Label(scene, c2, "BadgeLabel", "Badges and toasts", {20, 420}, 300);
    Badge(scene, c2, "SwatchBadgeNew", "New", UIAnchor::TopLeft, {20, 446}, slate::Accent());
    Badge(scene, c2, "SwatchBadgeOnline", "Online", UIAnchor::TopLeft, {82, 446}, slate::Success());
    Badge(scene, c2, "SwatchBadgeHot", "Hot", UIAnchor::TopLeft, {164, 446}, slate::Danger());
    Badge(scene, c2, "SwatchBadgeBeta", "Beta", UIAnchor::TopLeft, {216, 446}, slate::Warning());
    {
        GameObject* toast = Panel(scene, c2, "Toast", UIAnchor::TopLeft, {20, 484}, {352, 64}, Color::FromBytes(28, 30, 38, 240), 12.0f, true);
        GameObject* icon = Panel(scene, toast, "Swatch Icon", UIAnchor::TopLeft, {14, 14}, {36, 36}, slate::Success(), 0.0f);
        icon->GetComponent<UIPanel>()->shape = UIShape::Circle;
        Text(scene, icon, "Glyph", "+", UIAnchor::TopLeft, {0, 0}, {36, 36}, 20.0f, Color::White, 1, true);
        Text(scene, toast, "Title", "Game saved", UIAnchor::TopLeft, {64, 10}, {270, 22}, 16.0f, slate::Text(), 0, true);
        Text(scene, toast, "Body", "Slot 1 - just now", UIAnchor::TopLeft, {64, 34}, {270, 18}, 13.0f, slate::Dim());
    }
    gx::Label(scene, c2, "CrossLabel", "Crosshair", {20, 562}, 120);
    {
        GameObject* g = Child(scene, c2, "Crosshair"); auto* c = g->AddComponent<Crosshair>();
        c->anchor = UIAnchor::TopLeft; c->position = {300, 540}; c->size = {70, 70}; c->dot = true; c->circle = true; c->circleRadius = 22.0f;
    }

    // ---- Column 3: game pieces ----
    GameObject* c3 = gx::Card(scene, cardParent, "Game", "Game pieces", cardAt(2), {392, 618});
    gx::Label(scene, c3, "SlotLabel", "Inventory", {20, 40}, 200);
    {
        GameObject* g = Child(scene, c3, "Slots"); auto* sg = g->AddComponent<UISlotGrid>();
        sg->anchor = UIAnchor::TopLeft; sg->position = {20, 64}; sg->columns = 6; sg->rows = 2; sg->cell = {50, 50}; sg->gap = 8; sg->value = 1;
        sg->color = slate::Field(); sg->hoverColor = slate::FieldHover(); sg->selectedColor = slate::Accent();
        // A real bag (the engine's Inventory) that the grid shows while the game runs.
        GameObject* bag = Child(scene, root, "Gallery Bag");
        auto* inv = bag->AddComponent<Inventory>();
        inv->capacity = 12;
        const struct { const char* item; int n; } stock[] = {{"Axe", 1}, {"Potion", 3}, {"Key", 1}, {"Bow", 1}, {"Map", 1},
                                                             {"Gem", 5}, {"Rope", 2}, {"Torch", 12}, {"Ring", 1}};
        for (const auto& st : stock) inv->slots.push_back({st.item, st.n, {}});
        auto* ib = g->AddComponent<UIInventoryBind>();
        ib->inventoryName = "Gallery Bag"; ib->slotFontScale = 1.0f;
        sg->fontScale = 1.0f; sg->numberEmptySlots = false;
        for (int i = 0; i < 12; ++i)                       // the same labels in the editor, before Play
            sg->SetLabel(i, i < (int)inv->slots.size() ? UIInventoryBind::SlotText(inv->slots[(std::size_t)i], 5) : std::string());
        sg->Rebuild();
    }
    gx::Label(scene, c3, "DialogueLabel", "Dialogue box", {20, 190}, 200);
    {
        GameObject* dlg = Child(scene, c3, "DialogueBox");
        auto* db = dlg->AddComponent<UIDialogueBox>();
        db->startHidden = false; db->charsPerSecond = 40.0f;
        GameObject* box = Panel(scene, dlg, "Box", UIAnchor::TopLeft, {20, 214}, {352, 150}, Color::FromBytes(255, 255, 255, 16), 12.0f);
        Avatar(scene, box, "Portrait", "Mara", UIAnchor::TopLeft, {14, 14}, 40.0f);
        Text(scene, box, "Speaker", "Mara", UIAnchor::TopLeft, {64, 12}, {200, 20}, 15.0f, slate::Accent(), 0, true);
        auto* body = Text(scene, box, "Body", "You found the [color=#ffcc33]old road[/color]? Then you are [b]braver[/b] than you look.",
                          UIAnchor::TopLeft, {64, 34}, {278, 44}, 14.0f)->GetComponent<TextRenderer>();
        body->wrap = true; body->vcenter = false; body->richText = true;
        Button(scene, box, "Choice1", "I had help", UIAnchor::TopLeft, {14, 96}, {160, 36}, ButtonKind::Secondary);
        Button(scene, box, "Choice2", "Mind your business", UIAnchor::TopLeft, {182, 96}, {158, 36}, ButtonKind::Ghost);
        db->Prime("Mara", body->text, {"I had help", "Mind your business"});
        // Talk plays the line again from its first letter; the choices come back when it has finished typing.
        GameObject* talk = gx::Btn(scene, c3, "Talk", "Talk", {292, 186}, {80, 26}, (int)UITheme::Variant::Outline);
        auto* tb = talk->GetComponent<UIButton>();
        tb->clickTarget = "DialogueBox"; tb->clickFunction = "Restart";
    }
    gx::Label(scene, c3, "WindowLabel", "Window (drag the title bar)", {20, 380}, 300);
    {
        GameObject* win = AddWindow(scene, c3, "Window", "Quest log", {20, 404}, {212, 130});
        Text(scene, win, "Body", "Find the old road.", UIAnchor::TopLeft, {14, 48}, {184, 40}, 14.0f, slate::Dim());
        gx::Bar(scene, win, "Progress", {14, 98}, {184, 8}, 0.4f, slate::Accent(), (int)UITheme::Variant::Primary);
    }
    gx::Label(scene, c3, "PlateLabel", "Nameplate", {248, 380}, 120);
    {
        GameObject* plate = Panel(scene, c3, "SwatchInsetNameplate", UIAnchor::TopLeft, {244, 410}, {128, 46}, Color::FromBytes(16, 18, 24, 190), 10.0f);
        Text(scene, plate, "Name", "Goblin", UIAnchor::TopLeft, {10, 4}, {70, 22}, 14.0f, slate::Text(), 0, true);
        Badge(scene, plate, "SwatchLevel", "Lv 7", UIAnchor::TopRight, {-6, 5}, slate::Warning());
        BindNameplateHealth(gx::Bar(scene, plate, "Health", {10, 32}, {108, 7}, 0.72f, slate::Danger(), (int)UITheme::Variant::Danger));
        // The goblin the plate shows: a Health Stat (full at Play start) that heals back slowly. The two buttons hurt and heal it.
        GameObject* gob = Child(scene, root, "Goblin");
        auto* hs = gob->AddComponent<HealthStat>();
        hs->maxHealth = 100.0f; hs->regenPerSecond = 4.0f; hs->regenDelay = 1.5f;
        hs->respawn = true; hs->respawnDelay = 1.0f; hs->playDeathAnim = false;
        hs->publishPrefs = false; hs->sendMessages = false; hs->publishBar = false;
        const char* pn[] = {"PlateHit", "PlateHeal"};
        const char* pl[] = {"Hit", "Heal"};
        const char* pf[] = {"Damage", "Heal"};
        const int pv[] = {(int)UITheme::Variant::Danger, (int)UITheme::Variant::Outline};
        for (int i = 0; i < 2; ++i) {
            GameObject* b = gx::Btn(scene, c3, pn[i], pl[i], {244.0f + i * 68.0f, 462}, {60, 28}, pv[i]);
            auto* ub = b->GetComponent<UIButton>();
            ub->clickTarget = "Goblin"; ub->clickFunction = pf[i]; ub->clickArg = 15.0f;
        }
    }
    gx::Label(scene, c3, "FloatLabel", "Floating numbers (click)", {20, 548}, 300);
    {
        GameObject* dn = Child(scene, c3, "Numbers");
        auto* d = dn->AddComponent<UIDamageNumbers>();
        d->spawnAt = {1060, 520}; d->textSize = 3.0f;
        const char* fn[] = {"Hit", "Crit", "Heal"};
        const float amt[] = {25.0f, 90.0f, 40.0f};
        const int var[] = {(int)UITheme::Variant::Danger, (int)UITheme::Variant::Primary, (int)UITheme::Variant::Outline};
        for (int i = 0; i < 3; ++i) {
            GameObject* b = gx::Btn(scene, c3, fn[i], fn[i], {20.0f + i * 120.0f, 572}, {112, 34}, var[i]);
            auto* ub = b->GetComponent<UIButton>();
            ub->clickTarget = "Numbers"; ub->clickFunction = fn[i]; ub->clickArg = amt[i];
        }
    }
    ApplyThemePreset(scene, preset);
    return root;
}

}}  // namespace okay::UIGallery
