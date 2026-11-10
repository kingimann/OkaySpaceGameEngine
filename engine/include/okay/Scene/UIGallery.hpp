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

/// Restyle everything in the scene with a built-in look. The page background follows the look.
inline void ApplyThemePreset(Scene& scene, int preset) {
    UITheme* th = EnsureTheme(scene, preset);
    th->Apply();
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
    if (GameObject* bg = scene.Find("SwatchBackdrop"))
        if (auto* p = bg->GetComponent<UIPanel>()) {
            const bool lightLook = (th->panel.r + th->panel.g + th->panel.b) > 2.1f;
            Color c = lightLook ? Color(th->panel.r * 0.86f, th->panel.g * 0.86f, th->panel.b * 0.86f, 1.0f)
                                : Color(th->panel.r * 0.45f, th->panel.g * 0.45f, th->panel.b * 0.45f, 1.0f);
            if (th->preset == (int)UITheme::Preset::Glass) c = Color::FromBytes(58, 82, 140);   // glass needs something to see through
            if (th->preset == (int)UITheme::Preset::Neon)  c = Color::FromBytes(6, 2, 16);
            c.a = 1.0f;
            p->color = c; p->colorBottom = c; p->useGradient = false;
        }
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
    body->wrap = true; body->vcenter = false;
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

/// A nameplate: a name, a level badge and a thin health bar. Put it under a character as a
/// world-space UI object (it follows the character and faces the camera).
inline GameObject* AddNameplate(Scene& scene, bool inWorld) {
    GameObject* root = inWorld ? scene.CreateGameObject("Nameplate") : Child(scene, EnsureCanvas(scene), "Nameplate");
    if (inWorld) root->AddComponent<WorldSpaceUI>();
    GameObject* card = Panel(scene, root, "SwatchPlate", UIAnchor::TopLeft, {0, 0}, {180, 46}, Color::FromBytes(16, 18, 24, 190), 10.0f);
    Text(scene, card, "Name", "Goblin", UIAnchor::TopLeft, {10, 4}, {110, 22}, 15.0f, slate::Text(), 0, true);
    Badge(scene, card, "SwatchLevel", "Lv 7", UIAnchor::TopRight, {-8, 4}, slate::Warning());
    gx::Bar(scene, card, "Health", {10, 30}, {160, 8}, 0.72f, slate::Danger(), (int)UITheme::Variant::Danger);
    return root;
}

/// Pick the widgets of one preset: a dropdown in the header that restyles the scene.
inline GameObject* BuildGallery(Scene& scene, int preset = (int)UITheme::Preset::ModernFlat) {
    GameObject* canvas = EnsureCanvas(scene);
    GameObject* root = Group(scene, canvas, "UI Gallery");
    // Page background: kept as-is by the theme (the name starts with Swatch).
    Panel(scene, root, "SwatchBackdrop", UIAnchor::StretchFull, {0, 0}, {0, 0}, slate::Backdrop(), 0.0f);
    {
        auto* bp = scene.Find("SwatchBackdrop")->GetComponent<UIPanel>();
        bp->borderWidth = 0.0f;
    }
    Text(scene, root, "Title", "UI Gallery", UIAnchor::TopLeft, {32, 16}, {360, 40}, 30.0f, slate::Text(), 0, true);
    Text(scene, root, "Subtitle", "Every widget, in any theme. Pick a look on the right.", UIAnchor::TopLeft, {34, 52}, {560, 20}, 14.0f, slate::Dim());
    gx::Label(scene, root, "ThemeLabel", "Theme", {880, 24}, 80, slate::Dim(), 14.0f);
    {   // theme picker
        GameObject* pick = Child(scene, root, "ThemePick");
        auto* dd = pick->AddComponent<UIDropdown>();
        dd->anchor = UIAnchor::TopLeft; dd->position = {944, 14}; dd->size = {304, 38};
        dd->options.clear();
        for (int i = 1; i < UITheme::kPresetCount; ++i) dd->options.push_back(UITheme::PresetName(i));
        dd->maxVisible = 7;
        dd->value = preset >= 1 && preset < UITheme::kPresetCount ? preset - 1 : 0;
        pick->AddComponent<ScriptComponent>()->LoadSource(
            "function on_change() { ui_theme_preset(ui_dropdown_value(\"ThemePick\") + 1); }\n");
    }

    // ---- Column 1: buttons and inputs ----
    GameObject* c1 = gx::Card(scene, root, "Buttons", "Buttons and inputs", {32, 86}, {392, 618});
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
    GameObject* c2 = gx::Card(scene, root, "Bars", "Bars and feedback", {448, 86}, {392, 618});
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
    GameObject* c3 = gx::Card(scene, root, "Game", "Game pieces", {864, 86}, {392, 618});
    gx::Label(scene, c3, "SlotLabel", "Inventory", {20, 40}, 200);
    {
        GameObject* g = Child(scene, c3, "Slots"); auto* sg = g->AddComponent<UISlotGrid>();
        sg->anchor = UIAnchor::TopLeft; sg->position = {20, 64}; sg->columns = 6; sg->rows = 2; sg->cell = {50, 50}; sg->gap = 8; sg->value = 1;
        sg->color = slate::Field(); sg->hoverColor = slate::FieldHover(); sg->selectedColor = slate::Accent();
        sg->SetLabel(0, "Axe"); sg->SetLabel(1, "Pot"); sg->SetLabel(2, "Key"); sg->SetLabel(7, "Gem");
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
        auto* body = Text(scene, box, "Body", "You found the old road? Then you are braver than you look.",
                          UIAnchor::TopLeft, {64, 34}, {278, 44}, 14.0f)->GetComponent<TextRenderer>();
        body->wrap = true; body->vcenter = false;
        Button(scene, box, "Choice1", "I had help", UIAnchor::TopLeft, {14, 96}, {160, 36}, ButtonKind::Secondary);
        Button(scene, box, "Choice2", "Mind your business", UIAnchor::TopLeft, {182, 96}, {158, 36}, ButtonKind::Ghost);
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
        gx::Bar(scene, plate, "Health", {10, 32}, {108, 7}, 0.72f, slate::Danger(), (int)UITheme::Variant::Danger);
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
