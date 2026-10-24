#pragma once
// Ready-made, fully-editable UI screens and building blocks you can drop into an
// existing scene (the editor's "Create > UI" menu and the UI view's "+ Add" menu).
// Each screen builder finds or creates the scene's Canvas + EventSystem, then adds a
// grouped subtree (returned) so you can move, edit, or save it as a reusable
// prefab/template as one unit. Nothing here clears the scene.
//
// Everything is styled like the OkaySpace launcher ("Slate"): blue-tinted dark
// cards with hairline borders and soft shadows, the accent colour for the main
// action, dim secondary text, pill badges and round avatars.
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/Canvas.hpp"
#include "okay/Components/EventSystem.hpp"
#include "okay/Components/UIPanel.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Components/UISlider.hpp"
#include "okay/Components/UIToggle.hpp"
#include "okay/Components/UIDropdown.hpp"
#include "okay/Components/UIProgressBar.hpp"
#include "okay/Components/UIRadialProgress.hpp"
#include "okay/Components/UIAnimation.hpp"
#include "okay/Components/UIBarBind.hpp"    // health bar follows a variable
#include "okay/Components/UITextBind.hpp"   // score/text follows a variable
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Scene/UILayoutTools.hpp"   // AvoidOverlap: HUD pieces don't land on each other
#include <cctype>
#include <string>

namespace okay { namespace UITemplates {

// The launcher's palette, so screens and kit pieces match it (and each other).
namespace slate {
    inline Color Backdrop()   { return Color::FromBytes(18, 20, 26, 250); }
    inline Color Panel()      { return Color::FromBytes(30, 33, 41, 252); }
    inline Color Card()       { return Color::FromBytes(38, 42, 53, 255); }
    inline Color Field()      { return Color::FromBytes(46, 51, 64, 255); }
    inline Color FieldHover() { return Color::FromBytes(58, 64, 80, 255); }
    inline Color Border()     { return Color::FromBytes(255, 255, 255, 22); }
    inline Color Accent()     { return Color::FromBytes(66, 150, 250); }
    inline Color Text()       { return Color::FromBytes(232, 235, 240); }
    inline Color Dim()        { return Color::FromBytes(150, 157, 172); }
    inline Color Success()    { return Color::FromBytes(72, 196, 128); }
    inline Color Danger()     { return Color::FromBytes(226, 82, 82); }
    inline Color Warning()    { return Color::FromBytes(240, 180, 70); }
    inline Color Dim(float a) { return Color(0.0f, 0.0f, 0.0f, a); }   // full-screen dimmer
}

// Find or create the scene's UI Canvas (+ an EventSystem), so a prebuilt screen drops
// into an existing scene without clobbering anything already there.
inline GameObject* EnsureCanvas(Scene& scene) {
    GameObject* canvas = nullptr;
    if (Canvas* existing = scene.FindObjectOfType<Canvas>()) canvas = existing->gameObject;
    else {
        canvas = scene.CreateGameObject("Canvas");
        canvas->AddComponent<Canvas>()->scaleMode = Canvas::ScaleMode::ScaleWithScreenSize;
    }
    if (!scene.FindObjectOfType<EventSystem>())
        scene.CreateGameObject("EventSystem")->AddComponent<EventSystem>();
    return canvas;
}

enum class ButtonKind { Primary, Secondary, Danger, Ghost };

namespace detail {
    inline GameObject* Child(Scene& s, GameObject* parent, const char* name) {
        GameObject* g = s.CreateGameObject(name);
        if (parent) g->transform->SetParent(parent->transform, false);
        return g;
    }
    inline GameObject* Group(Scene& s, GameObject* parent, const char* name) {
        return Child(s, parent, name);   // no UI rect: children anchor to the canvas
    }
    // A flat Slate card. `lifted` adds the soft drop shadow dialogs and menus use.
    inline GameObject* Panel(Scene& s, GameObject* parent, const char* name, UIAnchor anchor,
                             Vec2 pos, Vec2 size, Color col = slate::Panel(), float corner = 12.0f,
                             bool lifted = false) {
        GameObject* g = Child(s, parent, name);
        auto* p = g->AddComponent<UIPanel>();
        p->anchor = anchor; p->position = pos; p->size = size; p->color = col;
        p->shape = corner > 0.0f ? UIShape::Rounded : UIShape::Rectangle;
        p->cornerRadius = corner;
        p->borderWidth = corner > 0.0f ? 1.0f : 0.0f; p->borderColor = slate::Border();
        if (lifted) {
            p->shadow = true; p->shadowColor = Color::FromBytes(0, 0, 0, 90);
            p->shadowOffset = {0.0f, 6.0f}; p->shadowSoftness = 16.0f;
        }
        return g;
    }
    // A full-screen translucent layer behind a modal (stretches with the screen).
    inline GameObject* Dimmer(Scene& s, GameObject* parent, float alpha = 0.6f) {
        GameObject* g = Child(s, parent, "Dim");
        auto* p = g->AddComponent<UIPanel>();
        p->anchor = UIAnchor::StretchFull; p->position = {0, 0}; p->size = {0, 0};   // margins
        p->shape = UIShape::Rectangle; p->cornerRadius = 0.0f; p->color = slate::Dim(alpha);
        return g;
    }
    // Text laid out in a `size` box (align: 0 left, 1 centre, 2 right; vertically centred).
    // `px` is the text height in pixels (16 = body, 13 = caption, 36 = title).
    inline GameObject* Text(Scene& s, GameObject* parent, const char* name, const std::string& t,
                            UIAnchor anchor, Vec2 pos, Vec2 size, float px,
                            Color col = slate::Text(), int align = 0, bool bold = false) {
        GameObject* g = Child(s, parent, name);
        auto* tr = g->AddComponent<TextRenderer>();
        tr->text = t; tr->screenSpace = true; tr->anchor = anchor; tr->screenPos = pos; tr->size = size;
        tr->pixelSize = px / 8.0f; tr->color = col; tr->align = align; tr->bold = bold;
        return g;
    }
    // A small upper-case label in a colour (section headers: "NOW PLAYING").
    inline GameObject* Caption(Scene& s, GameObject* parent, const char* name, const std::string& t,
                               UIAnchor anchor, Vec2 pos, float width, Color col = slate::Accent()) {
        GameObject* g = Text(s, parent, name, t, anchor, pos, {width, 18.0f}, 12.0f, col);
        auto* tr = g->GetComponent<TextRenderer>();
        tr->uppercase = true; tr->letterSpacing = 1.0f;
        return g;
    }
    inline void StyleButton(UIButton* b, ButtonKind kind) {
        switch (kind) {
            case ButtonKind::Primary:
                b->color = slate::Accent(); b->hoverColor = Color::FromBytes(92, 166, 252);
                b->pressedColor = Color::FromBytes(52, 122, 214); b->textColor = Color::White; break;
            case ButtonKind::Secondary:
                b->color = slate::Field(); b->hoverColor = slate::FieldHover();
                b->pressedColor = slate::Card(); b->textColor = slate::Text();
                b->borderWidth = 1.0f; b->borderColor = slate::Border(); break;
            case ButtonKind::Danger:
                b->color = slate::Danger(); b->hoverColor = Color::FromBytes(236, 104, 104);
                b->pressedColor = Color::FromBytes(196, 64, 64); b->textColor = Color::White; break;
            case ButtonKind::Ghost:
                b->color = Color::FromBytes(255, 255, 255, 0); b->hoverColor = Color::FromBytes(255, 255, 255, 18);
                b->pressedColor = Color::FromBytes(255, 255, 255, 10); b->textColor = slate::Dim();
                b->hoverTextColor = slate::Text(); break;
        }
        if (kind != ButtonKind::Ghost) b->hoverTextColor = b->textColor;
    }
    inline GameObject* Button(Scene& s, GameObject* parent, const char* name, const std::string& label,
                              UIAnchor anchor, Vec2 pos, Vec2 size, ButtonKind kind = ButtonKind::Primary) {
        GameObject* g = Child(s, parent, name);
        auto* b = g->AddComponent<UIButton>();
        b->label = label; b->anchor = anchor; b->position = pos; b->size = size;
        b->cornerRadius = 8.0f; b->fontScale = 2.0f;
        StyleButton(b, kind);
        return g;
    }
    // A pill tag sized to its text ("Ready", "3 / 4", "Ranked").
    inline GameObject* Badge(Scene& s, GameObject* parent, const char* name, const std::string& t,
                             UIAnchor anchor, Vec2 pos, Color col = slate::Accent()) {
        const float w = 7.0f * (float)t.size() + 22.0f, h = 22.0f;
        GameObject* g = Child(s, parent, name);
        auto* p = g->AddComponent<UIPanel>();
        p->anchor = anchor; p->position = pos; p->size = {w, h}; p->shape = UIShape::Pill;
        p->color = Color(col.r, col.g, col.b, 0.22f);
        p->borderWidth = 1.0f; p->borderColor = Color(col.r, col.g, col.b, 0.55f);
        Text(s, g, "Label", t, UIAnchor::TopLeft, {0, 0}, {w, h}, 12.0f,
             Color(col.r * 0.4f + 0.6f, col.g * 0.4f + 0.6f, col.b * 0.4f + 0.6f, 1.0f), 1);
        return g;
    }
    // A coloured circle with an initial (players, friends, speakers).
    inline GameObject* Avatar(Scene& s, GameObject* parent, const char* name, const std::string& who,
                              UIAnchor anchor, Vec2 pos, float d = 36.0f) {
        static const unsigned char pal[6][3] = {{66, 150, 250}, {72, 196, 128}, {240, 140, 70},
                                                {170, 110, 240}, {226, 82, 120}, {50, 180, 200}};
        unsigned h = 0; for (char c : who) h = h * 31u + (unsigned char)c;
        const unsigned char* c = pal[h % 6];
        GameObject* g = Child(s, parent, name);
        auto* p = g->AddComponent<UIPanel>();
        p->anchor = anchor; p->position = pos; p->size = {d, d}; p->shape = UIShape::Circle;
        p->color = Color::FromBytes(c[0], c[1], c[2]);
        std::string initial = who.empty() ? std::string("?") : std::string(1, (char)std::toupper((unsigned char)who[0]));
        Text(s, g, "Initial", initial, UIAnchor::TopLeft, {0, 0}, {d, d}, d * 0.45f, Color::White, 1, true);
        return g;
    }
    // A 1 px hairline across `width`.
    inline GameObject* Divider(Scene& s, GameObject* parent, UIAnchor anchor, Vec2 pos, float width) {
        GameObject* g = Child(s, parent, "Divider");
        auto* p = g->AddComponent<UIPanel>();
        p->anchor = anchor; p->position = pos; p->size = {width, 1.0f};
        p->shape = UIShape::Rectangle; p->cornerRadius = 0.0f; p->color = Color::FromBytes(255, 255, 255, 26);
        return g;
    }
    // Dim label on the left, value on the right ("Ping ........ 23 ms").
    inline GameObject* KeyValue(Scene& s, GameObject* parent, const char* name, const std::string& key,
                                const std::string& value, Vec2 pos, float width) {
        GameObject* g = Group(s, parent, name);
        Text(s, g, "Key", key, UIAnchor::TopLeft, pos, {width * 0.5f, 24.0f}, 15.0f, slate::Dim());
        Text(s, g, "Value", value, UIAnchor::TopLeft, {pos.x + width * 0.5f, pos.y}, {width * 0.5f, 24.0f},
             15.0f, slate::Text(), 2);
        return g;
    }
    // Avatar + name + dim subtitle in a row card; returns the row so callers can add a
    // badge or numbers on the right.
    inline GameObject* ListRow(Scene& s, GameObject* parent, const char* name, const std::string& title,
                               const std::string& subtitle, Vec2 pos, float width, bool highlight = false) {
        GameObject* row = Panel(s, parent, name, UIAnchor::TopLeft, pos, {width, 52.0f},
                                highlight ? Color::FromBytes(44, 58, 84) : slate::Card(), 8.0f);
        if (highlight) { auto* p = row->GetComponent<UIPanel>(); p->borderColor = Color::FromBytes(66, 150, 250, 140); }
        Avatar(s, row, "Avatar", title, UIAnchor::TopLeft, {10, 9}, 34.0f);
        if (subtitle.empty()) {
            Text(s, row, "Title", title, UIAnchor::TopLeft, {54, 0}, {width - 64.0f, 52.0f}, 16.0f);
        } else {
            Text(s, row, "Title", title, UIAnchor::TopLeft, {54, 7}, {width - 64.0f, 20.0f}, 16.0f);
            Text(s, row, "Subtitle", subtitle, UIAnchor::TopLeft, {54, 27}, {width - 64.0f, 18.0f}, 13.0f, slate::Dim());
        }
        return row;
    }
    inline void IntroAnim(GameObject* g, UIAnimation::Intro intro, float delay = 0.0f) {
        auto* a = g->AddComponent<UIAnimation>();
        a->intro = intro; a->delay = delay; a->duration = 0.3f; a->distance = 60.0f;
    }
}

// ---------------------------------------------------------------------------
// Kit: single building blocks. `parent` is a Canvas or another widget (a card,
// a panel); pieces are placed at its top-left so they're easy to find and drag.
// ---------------------------------------------------------------------------

inline GameObject* AddCard(Scene& scene, GameObject* parent) {
    using namespace detail;
    GameObject* card = Panel(scene, parent, "Card", UIAnchor::Center, {0, 0}, {320, 200}, slate::Panel(), 12.0f, true);
    Caption(scene, card, "Caption", "SECTION", UIAnchor::TopLeft, {20, 18}, 280);
    Text(scene, card, "Title", "Card title", UIAnchor::TopLeft, {20, 38}, {280, 30}, 22.0f, slate::Text(), 0, true);
    auto* body = Text(scene, card, "Body", "Put a short description here. Cards hold related content together.",
                      UIAnchor::TopLeft, {20, 74}, {280, 60}, 14.0f, slate::Dim())->GetComponent<TextRenderer>();
    body->wrap = true; body->vcenter = false;
    Button(scene, card, "ActionButton", "Action", UIAnchor::BottomRight, {-20, -18}, {110, 38});
    return card;
}

inline GameObject* AddBadge(Scene& scene, GameObject* parent) {
    return detail::Badge(scene, parent, "Badge", "New", UIAnchor::TopLeft, {24, 24});
}

inline GameObject* AddAvatar(Scene& scene, GameObject* parent) {
    return detail::Avatar(scene, parent, "Avatar", "Player", UIAnchor::TopLeft, {24, 24}, 48.0f);
}

namespace detail {
    // A kit piece's width: `want`, narrowed to fit a fixed-size parent with 24 px margins.
    inline float FitWidth(GameObject* parent, float want) {
        if (!parent) return want;
        if (auto* p = parent->GetComponent<UIPanel>())
            if (!AnchorIsStretch(p->anchor) && p->size.x - 48.0f < want) return p->size.x - 48.0f > 40.0f ? p->size.x - 48.0f : 40.0f;
        return want;
    }
}

inline GameObject* AddListRow(Scene& scene, GameObject* parent) {
    GameObject* row = detail::ListRow(scene, parent, "List Row", "Player name", "Online", {24, 24},
                                      detail::FitWidth(parent, 320.0f));
    detail::Badge(scene, row, "Badge", "Ready", UIAnchor::MiddleRight, {-12, 0}, slate::Success());
    return row;
}

inline GameObject* AddDivider(Scene& scene, GameObject* parent) {
    // Stretches across its parent (or the screen) with 24 px margins, so it follows resizes.
    GameObject* g = detail::Divider(scene, parent, UIAnchor::StretchHorizontal, {24, 24}, 24.0f);
    g->GetComponent<UIPanel>()->size = {24.0f, 1.0f};   // StretchHorizontal: size.x = right margin
    return g;
}

inline GameObject* AddHeading(Scene& scene, GameObject* parent) {
    using namespace detail;
    GameObject* g = Group(scene, parent, "Heading");
    Caption(scene, g, "Caption", "NOW PLAYING", UIAnchor::TopLeft, {24, 24}, 360);
    Text(scene, g, "Title", "Heading", UIAnchor::TopLeft, {24, 44}, {360, 40}, 30.0f, slate::Text(), 0, true);
    auto* line = Panel(scene, g, "Underline", UIAnchor::TopLeft, {24, 88}, {48, 3}, slate::Accent(), 0.0f);
    line->GetComponent<UIPanel>()->shape = UIShape::Pill;
    return g;
}

inline GameObject* AddButtonOfKind(Scene& scene, GameObject* parent, ButtonKind kind) {
    const char* names[] = {"Primary Button", "Secondary Button", "Danger Button", "Ghost Button"};
    const char* labels[] = {"Play", "Options", "Delete", "Cancel"};
    return detail::Button(scene, parent, names[(int)kind], labels[(int)kind], UIAnchor::Center, {0, 0}, {200, 46}, kind);
}

inline GameObject* AddSpinner(Scene& scene, GameObject* parent) {
    GameObject* g = detail::Child(scene, parent, "Spinner");
    auto* r = g->AddComponent<UIRadialProgress>();
    r->anchor = UIAnchor::Center; r->position = {0, 0}; r->size = {40, 40};
    r->value = 0.25f; r->thickness = 4.0f; r->spin = true;
    r->background = Color::FromBytes(255, 255, 255, 20); r->fill = slate::Accent();
    return g;
}

inline GameObject* AddSwitch(Scene& scene, GameObject* parent) {
    GameObject* g = detail::Child(scene, parent, "Switch");
    auto* t = g->AddComponent<UIToggle>();
    t->anchor = UIAnchor::TopLeft; t->position = {24, 24}; t->size = {44, 24};
    t->style = UIToggle::Style::Switch; t->label = "Enabled"; t->on = true;
    return g;
}

// A toast that slides in at the top-right (achievements, invites, "Saved").
inline GameObject* AddNotification(Scene& scene, GameObject* parent) {
    using namespace detail;
    GameObject* n = Panel(scene, parent, "Notification", UIAnchor::TopRight, {-24, 24}, {340, 76},
                          slate::Panel(), 12.0f, true);
    GameObject* icon = Panel(scene, n, "Icon", UIAnchor::TopLeft, {16, 18}, {40, 40}, slate::Accent(), 0.0f);
    icon->GetComponent<UIPanel>()->shape = UIShape::Circle;
    Text(scene, icon, "Glyph", "!", UIAnchor::TopLeft, {0, 0}, {40, 40}, 20.0f, Color::White, 1, true);
    Text(scene, n, "Title", "Achievement unlocked", UIAnchor::TopLeft, {70, 16}, {256, 22}, 16.0f, slate::Text(), 0, true);
    Text(scene, n, "Body", "First Blood - win your first match", UIAnchor::TopLeft, {70, 40}, {256, 20}, 13.0f, slate::Dim());
    IntroAnim(n, UIAnimation::Intro::SlideRight);
    return n;
}

// ---------------------------------------------------------------------------
// Screens
// ---------------------------------------------------------------------------

// Centered main menu: game title + Play / Options / Quit.
inline GameObject* AddMainMenu(Scene& scene) {
    using namespace detail;
    GameObject* canvas = EnsureCanvas(scene);
    GameObject* root = Panel(scene, canvas, "Main Menu", UIAnchor::Center, {0, 0}, {400, 440},
                             slate::Panel(), 16.0f, true);
    Caption(scene, root, "Caption", "WELCOME TO", UIAnchor::TopLeft, {36, 34}, 328);
    Text(scene, root, "Title", "MY GAME", UIAnchor::TopLeft, {36, 54}, {328, 48}, 40.0f, slate::Text(), 0, true);
    Divider(scene, root, UIAnchor::TopLeft, {36, 118}, 328);
    Button(scene, root, "PlayButton",    "Play",    UIAnchor::TopLeft, {36, 146}, {328, 52});
    Button(scene, root, "OptionsButton", "Options", UIAnchor::TopLeft, {36, 210}, {328, 48}, ButtonKind::Secondary);
    Button(scene, root, "CreditsButton", "Credits", UIAnchor::TopLeft, {36, 270}, {328, 48}, ButtonKind::Secondary);
    Button(scene, root, "QuitButton",    "Quit",    UIAnchor::TopLeft, {36, 330}, {328, 48}, ButtonKind::Ghost);
    Text(scene, root, "Version", "v1.0", UIAnchor::BottomLeft, {36, -14}, {120, 18}, 12.0f, slate::Dim());
    IntroAnim(root, UIAnimation::Intro::Pop);
    return root;
}

// Full-screen dim + centered card: PAUSED + Resume / Settings / Quit.
inline GameObject* AddPauseMenu(Scene& scene) {
    using namespace detail;
    GameObject* canvas = EnsureCanvas(scene);
    GameObject* root = Group(scene, canvas, "Pause Menu");
    Dimmer(scene, root, 0.55f);
    GameObject* card = Panel(scene, root, "Card", UIAnchor::Center, {0, 0}, {360, 336}, slate::Panel(), 16.0f, true);
    Caption(scene, card, "Caption", "GAME", UIAnchor::TopLeft, {32, 28}, 296);
    Text(scene, card, "Title", "Paused", UIAnchor::TopLeft, {32, 46}, {296, 40}, 32.0f, slate::Text(), 0, true);
    Button(scene, card, "ResumeButton",   "Resume",       UIAnchor::TopLeft, {32, 110}, {296, 50});
    Button(scene, card, "SettingsButton", "Settings",     UIAnchor::TopLeft, {32, 172}, {296, 46}, ButtonKind::Secondary);
    Button(scene, card, "QuitButton",     "Quit to menu", UIAnchor::TopLeft, {32, 230}, {296, 46}, ButtonKind::Ghost);
    IntroAnim(card, UIAnimation::Intro::Pop);
    return root;
}

// Settings panel: Music / SFX sliders, Fullscreen / VSync switches, Quality dropdown, Back.
inline GameObject* AddSettings(Scene& scene) {
    using namespace detail;
    GameObject* canvas = EnsureCanvas(scene);
    GameObject* root = Panel(scene, canvas, "Settings", UIAnchor::Center, {0, 0}, {440, 480},
                             slate::Panel(), 16.0f, true);
    Text(scene, root, "Title", "Settings", UIAnchor::TopLeft, {32, 24}, {376, 40}, 30.0f, slate::Text(), 0, true);
    Caption(scene, root, "AudioCaption", "AUDIO", UIAnchor::TopLeft, {32, 82}, 376);
    auto slider = [&](const char* name, const char* label, float y, float v) {
        Text(scene, root, (std::string(name) + "Label").c_str(), label, UIAnchor::TopLeft, {32, y}, {110, 24}, 16.0f);
        GameObject* g = Child(scene, root, (std::string(name) + "Slider").c_str());
        auto* sl = g->AddComponent<UISlider>();
        sl->anchor = UIAnchor::TopLeft; sl->position = {150, y + 6}; sl->size = {258, 12}; sl->value = v;
    };
    slider("Music", "Music", 106, 0.8f);
    slider("Sfx", "Effects", 144, 0.7f);
    Divider(scene, root, UIAnchor::TopLeft, {32, 190}, 376);
    Caption(scene, root, "VideoCaption", "VIDEO", UIAnchor::TopLeft, {32, 206}, 376);
    auto toggle = [&](const char* name, const char* label, float y, bool on) {
        Text(scene, root, (std::string(name) + "Label").c_str(), label, UIAnchor::TopLeft, {32, y}, {200, 24}, 16.0f);
        GameObject* g = Child(scene, root, (std::string(name) + "Toggle").c_str());
        auto* tg = g->AddComponent<UIToggle>();
        tg->anchor = UIAnchor::TopLeft; tg->position = {364, y}; tg->size = {44, 24};
        tg->style = UIToggle::Style::Switch; tg->label = ""; tg->on = on;
    };
    toggle("Fullscreen", "Fullscreen", 230, false);
    toggle("Vsync", "VSync", 266, true);
    Text(scene, root, "QualityLabel", "Quality", UIAnchor::TopLeft, {32, 306}, {110, 32}, 16.0f);
    { GameObject* g = Child(scene, root, "QualityDropdown"); auto* dd = g->AddComponent<UIDropdown>();
      dd->anchor = UIAnchor::TopLeft; dd->position = {208, 306}; dd->size = {200, 32};
      dd->options = {"Low", "Medium", "High"}; dd->value = 2; }
    Button(scene, root, "BackButton", "Done", UIAnchor::BottomLeft, {32, -28}, {376, 48});
    return root;
}

// HUD: player card with a health bar (top-left), score chip (top-right), ammo (bottom-right).
inline GameObject* AddHUD(Scene& scene) {
    using namespace detail;
    GameObject* canvas = EnsureCanvas(scene);
    GameObject* root = Group(scene, canvas, "HUD");
    GameObject* card = Panel(scene, root, "PlayerCard", UIAnchor::TopLeft, {20, 20}, {300, 64},
                             Color::FromBytes(30, 33, 41, 215), 12.0f);
    Avatar(scene, card, "Avatar", "Player", UIAnchor::TopLeft, {12, 12}, 40.0f);
    Text(scene, card, "Name", "Player", UIAnchor::TopLeft, {64, 10}, {160, 20}, 15.0f, slate::Text(), 0, true);
    Text(scene, card, "HealthText", "100", UIAnchor::TopLeft, {224, 10}, {62, 20}, 13.0f, slate::Dim(), 2)
        ->AddComponent<UITextBind>()->format = "{hp|100}";
    { GameObject* g = Child(scene, card, "HealthBar"); auto* pb = g->AddComponent<UIProgressBar>();
      pb->anchor = UIAnchor::TopLeft; pb->position = {64, 38}; pb->size = {222, 10}; pb->value = 1.0f;
      pb->shape = UIShape::Pill; pb->fill = slate::Success(); pb->background = Color::FromBytes(255, 255, 255, 24);
      auto* bb = g->AddComponent<UIBarBind>(); bb->var = "hp"; bb->min = 0.0f; bb->max = 100.0f; }   // fills from `hp`
    GameObject* score = Panel(scene, root, "ScoreChip", UIAnchor::TopRight, {-20, 20}, {170, 44},
                              Color::FromBytes(30, 33, 41, 215), 22.0f);
    score->GetComponent<UIPanel>()->shape = UIShape::Pill;
    Caption(scene, score, "Caption", "SCORE", UIAnchor::TopLeft, {18, 13}, 60, slate::Dim());
    Text(scene, score, "Score", "0", UIAnchor::TopLeft, {70, 0}, {84, 44}, 20.0f, slate::Text(), 2, true)
        ->AddComponent<UITextBind>()->format = "{score|0}";   // shows the live `score` variable
    GameObject* ammo = Panel(scene, root, "Ammo", UIAnchor::BottomRight, {-20, -20}, {150, 56},
                             Color::FromBytes(30, 33, 41, 215), 12.0f);
    Text(scene, ammo, "Count", "30 / 90", UIAnchor::TopLeft, {0, 0}, {150, 56}, 24.0f, slate::Text(), 1, true);
    for (GameObject* piece : {card, score, ammo}) UILayout::AvoidOverlap(scene, piece);
    return root;
}

// Bottom dialogue box: speaker badge + body text + Next.
inline GameObject* AddDialogBox(Scene& scene) {
    using namespace detail;
    GameObject* canvas = EnsureCanvas(scene);
    GameObject* root = Panel(scene, canvas, "Dialog Box", UIAnchor::BottomCenter, {0, -24}, {840, 168},
                             Color::FromBytes(24, 27, 34, 240), 14.0f, true);
    Avatar(scene, root, "Portrait", "Name", UIAnchor::TopLeft, {24, 22}, 56.0f);
    Text(scene, root, "Speaker", "Name", UIAnchor::TopLeft, {96, 22}, {400, 24}, 17.0f, slate::Accent(), 0, true);
    auto* body = Text(scene, root, "Body", "Type your dialogue here...", UIAnchor::TopLeft, {96, 52},
                      {600, 64}, 17.0f)->GetComponent<TextRenderer>();
    body->wrap = true; body->vcenter = false; body->typeSpeed = 40.0f; body->visibleChars = 0;
    Button(scene, root, "NextButton", "Next", UIAnchor::BottomRight, {-20, -18}, {120, 40});
    IntroAnim(root, UIAnimation::Intro::SlideBottom);
    UILayout::AvoidOverlap(scene, root);
    return root;
}

// A standalone labeled health bar (top-left), for quick HUDs.
inline GameObject* AddHealthBar(Scene& scene) {
    using namespace detail;
    GameObject* canvas = EnsureCanvas(scene);
    GameObject* root = Panel(scene, canvas, "Health Bar", UIAnchor::TopLeft, {20, 20}, {300, 58},
                             Color::FromBytes(30, 33, 41, 215), 12.0f);
    Caption(scene, root, "Label", "HEALTH", UIAnchor::TopLeft, {14, 10}, 140, slate::Dim());
    Text(scene, root, "Value", "100", UIAnchor::TopLeft, {186, 6}, {100, 22}, 15.0f, slate::Text(), 2, true)
        ->AddComponent<UITextBind>()->format = "{hp|100}";
    { GameObject* g = Child(scene, root, "Bar"); auto* pb = g->AddComponent<UIProgressBar>();
      pb->anchor = UIAnchor::TopLeft; pb->position = {14, 34}; pb->size = {272, 12}; pb->value = 1.0f;
      pb->shape = UIShape::Pill; pb->fill = slate::Success(); pb->background = Color::FromBytes(255, 255, 255, 24);
      auto* bb = g->AddComponent<UIBarBind>(); bb->var = "hp"; bb->min = 0.0f; bb->max = 100.0f; }   // fills from `hp`
    UILayout::AvoidOverlap(scene, root);
    return root;
}

// Multiplayer lobby: players (avatar + ready badge) on the left, match info + Ready on the right.
inline GameObject* AddLobby(Scene& scene) {
    using namespace detail;
    GameObject* canvas = EnsureCanvas(scene);
    GameObject* root = Panel(scene, canvas, "Lobby", UIAnchor::Center, {0, 0}, {760, 460}, slate::Panel(), 16.0f, true);
    Caption(scene, root, "Caption", "LOBBY", UIAnchor::TopLeft, {32, 28}, 400);
    Text(scene, root, "Title", "Star Arena", UIAnchor::TopLeft, {32, 46}, {400, 40}, 30.0f, slate::Text(), 0, true);
    Badge(scene, root, "Count", "3 / 4", UIAnchor::TopRight, {-32, 36});
    GameObject* list = Group(scene, root, "Players");   // no rect: rows still lay out in the lobby card
    const char* names[] = {"Maya", "Leo", "Sam"};
    const bool ready[] = {true, true, false};
    for (int i = 0; i < 3; ++i) {
        GameObject* row = ListRow(scene, root, (std::string("Player") + std::to_string(i + 1)).c_str(),
                                  names[i], i == 0 ? "Host" : "", {32, 110.0f + i * 62.0f}, 420.0f, i == 0);
        row->transform->SetParent(list->transform, false);
        Badge(scene, row, "Status", ready[i] ? "Ready" : "Not ready", UIAnchor::MiddleRight, {-12, 0},
              ready[i] ? slate::Success() : slate::Dim());
    }
    { GameObject* empty = Panel(scene, root, "OpenSlot", UIAnchor::TopLeft, {32, 296}, {420, 52},
                                Color::FromBytes(255, 255, 255, 8), 8.0f);
      empty->transform->SetParent(list->transform, false);
      empty->GetComponent<UIPanel>()->borderWidth = 0.0f;   // an empty slot: just a faint well
      Text(scene, empty, "Label", "Waiting for a player...", UIAnchor::TopLeft, {0, 0}, {420, 52}, 14.0f, slate::Dim(), 1); }
    GameObject* side = Panel(scene, root, "Match", UIAnchor::TopRight, {-32, 110}, {244, 238}, slate::Card(), 10.0f);
    Caption(scene, side, "Caption", "MATCH", UIAnchor::TopLeft, {18, 16}, 200, slate::Dim());
    KeyValue(scene, side, "Mode", "Mode", "Ranked", {18, 42}, 208);
    KeyValue(scene, side, "Map", "Map", "Dunes", {18, 72}, 208);
    KeyValue(scene, side, "Region", "Region", "EU West", {18, 102}, 208);
    KeyValue(scene, side, "Ping", "Ping", "23 ms", {18, 132}, 208);
    Button(scene, root, "ReadyButton", "Ready", UIAnchor::BottomRight, {-32, -28}, {244, 50});
    Button(scene, root, "LeaveButton", "Leave", UIAnchor::BottomLeft, {32, -28}, {140, 50}, ButtonKind::Secondary);
    return root;
}

// TAB-style scoreboard: rank, player, K / D / score columns; your row highlighted.
inline GameObject* AddScoreboard(Scene& scene) {
    using namespace detail;
    GameObject* canvas = EnsureCanvas(scene);
    GameObject* root = Panel(scene, canvas, "Scoreboard", UIAnchor::Center, {0, 0}, {680, 432},
                             Color::FromBytes(24, 27, 34, 235), 16.0f, true);
    Text(scene, root, "Title", "Scoreboard", UIAnchor::TopLeft, {28, 22}, {300, 36}, 26.0f, slate::Text(), 0, true);
    Badge(scene, root, "Mode", "Team Deathmatch", UIAnchor::TopRight, {-28, 30});
    const float colX[] = {28, 80, 420, 490, 560};
    const float colW[] = {40, 300, 60, 60, 92};
    const char* heads[] = {"#", "PLAYER", "K", "D", "SCORE"};
    for (int c = 0; c < 5; ++c)
        Caption(scene, root, "Header", heads[c], UIAnchor::TopLeft, {colX[c], 76}, colW[c], slate::Dim())
            ->GetComponent<TextRenderer>()->align = c == 4 ? 2 : 0;
    const char* names[] = {"Maya", "You", "Leo", "Sam", "Ava"};
    const int k[] = {21, 17, 12, 9, 4}, d[] = {6, 9, 11, 12, 15}, sc[] = {2450, 1980, 1460, 1120, 610};
    for (int i = 0; i < 5; ++i) {
        const bool me = i == 1;
        GameObject* row = Panel(scene, root, (std::string("Row") + std::to_string(i + 1)).c_str(), UIAnchor::TopLeft,
                                {16, 100.0f + i * 58.0f}, {648, 50},
                                me ? Color::FromBytes(44, 58, 84) : (i % 2 ? Color::FromBytes(255, 255, 255, 6) : slate::Card()), 8.0f);
        if (me) row->GetComponent<UIPanel>()->borderColor = Color::FromBytes(66, 150, 250, 140);
        Text(scene, row, "Rank", std::to_string(i + 1), UIAnchor::TopLeft, {12, 0}, {40, 50}, 16.0f, slate::Dim());
        Avatar(scene, row, "Avatar", names[i], UIAnchor::TopLeft, {64, 9}, 32.0f);
        Text(scene, row, "Name", names[i], UIAnchor::TopLeft, {106, 0}, {280, 50}, 16.0f, slate::Text(), 0, me);
        Text(scene, row, "Kills", std::to_string(k[i]), UIAnchor::TopLeft, {404, 0}, {60, 50}, 16.0f);
        Text(scene, row, "Deaths", std::to_string(d[i]), UIAnchor::TopLeft, {474, 0}, {60, 50}, 16.0f, slate::Dim());
        Text(scene, row, "Score", std::to_string(sc[i]), UIAnchor::TopLeft, {544, 0}, {92, 50}, 16.0f, slate::Text(), 2, true);
    }
    return root;
}

// Match over: big VICTORY, stats, Play again / Main menu.
inline GameObject* AddGameOver(Scene& scene) {
    using namespace detail;
    GameObject* canvas = EnsureCanvas(scene);
    GameObject* root = Group(scene, canvas, "Game Over");
    Dimmer(scene, root, 0.6f);
    GameObject* card = Panel(scene, root, "Card", UIAnchor::Center, {0, 0}, {440, 392}, slate::Panel(), 16.0f, true);
    Caption(scene, card, "Caption", "MATCH OVER", UIAnchor::TopLeft, {0, 34}, 440, slate::Dim())
        ->GetComponent<TextRenderer>()->align = 1;
    Text(scene, card, "Result", "VICTORY", UIAnchor::TopLeft, {0, 56}, {440, 60}, 48.0f, slate::Success(), 1, true);
    Divider(scene, card, UIAnchor::TopLeft, {36, 134}, 368);
    KeyValue(scene, card, "Score", "Score", "1,980", {36, 152}, 368);
    KeyValue(scene, card, "Kills", "Eliminations", "17", {36, 184}, 368);
    KeyValue(scene, card, "Time", "Match time", "8:42", {36, 216}, 368);
    Badge(scene, card, "Reward", "+120 XP", UIAnchor::TopCenter, {0, 256}, slate::Warning());
    Button(scene, card, "PlayAgainButton", "Play again", UIAnchor::BottomLeft, {36, -30}, {178, 50});
    Button(scene, card, "MenuButton", "Main menu", UIAnchor::BottomRight, {-36, -30}, {178, 50}, ButtonKind::Secondary);
    IntroAnim(card, UIAnimation::Intro::Pop);
    return root;
}

// Loading screen: full-screen backdrop, title, tip, progress bar and a spinner.
inline GameObject* AddLoadingScreen(Scene& scene) {
    using namespace detail;
    GameObject* canvas = EnsureCanvas(scene);
    GameObject* root = Child(scene, canvas, "Loading Screen");
    { auto* p = root->AddComponent<UIPanel>();
      p->anchor = UIAnchor::StretchFull; p->position = {0, 0}; p->size = {0, 0};
      p->shape = UIShape::Rectangle; p->cornerRadius = 0.0f;
      p->color = slate::Backdrop(); p->useGradient = true; p->colorBottom = Color::FromBytes(28, 36, 56, 255); }
    Caption(scene, root, "Caption", "NOW LOADING", UIAnchor::BottomLeft, {64, -152}, 400);
    Text(scene, root, "Title", "Dunes", UIAnchor::BottomLeft, {64, -100}, {600, 52}, 44.0f, slate::Text(), 0, true);
    Text(scene, root, "Tip", "Tip: hold Shift to sprint.", UIAnchor::BottomLeft, {64, -72}, {700, 24}, 15.0f, slate::Dim());
    { GameObject* g = Child(scene, root, "Progress"); auto* pb = g->AddComponent<UIProgressBar>();
      pb->anchor = UIAnchor::BottomCenter; pb->position = {0, -40}; pb->size = {1152, 6};   // 64 px margins at 1280
      pb->value = 0.4f; pb->shape = UIShape::Pill; pb->fill = slate::Accent(); pb->background = Color::FromBytes(255, 255, 255, 24);
      auto* bb = g->AddComponent<UIBarBind>(); bb->var = "loading"; bb->min = 0.0f; bb->max = 1.0f; }
    { GameObject* g = Child(scene, root, "Spinner"); auto* r = g->AddComponent<UIRadialProgress>();
      r->anchor = UIAnchor::BottomRight; r->position = {-64, -64}; r->size = {36, 36};
      r->value = 0.25f; r->thickness = 4.0f; r->spin = true;
      r->background = Color::FromBytes(255, 255, 255, 20); r->fill = slate::Accent(); }
    return root;
}

namespace detail {
    // Shared by the loading screens: a full-screen backdrop, a progress bar that
    // follows the `loading` variable (0 to 1) and a spinning ring.
    inline GameObject* LoadingBackdrop(Scene& s, const char* name, Color top, Color bottom) {
        GameObject* root = Child(s, EnsureCanvas(s), name);
        auto* p = root->AddComponent<UIPanel>();
        p->anchor = UIAnchor::StretchFull; p->position = {0, 0}; p->size = {0, 0};
        p->shape = UIShape::Rectangle; p->cornerRadius = 0.0f;
        p->color = top; p->useGradient = top.r != bottom.r || top.g != bottom.g || top.b != bottom.b; p->colorBottom = bottom;
        return root;
    }
    inline GameObject* LoadingBar(Scene& s, GameObject* parent, UIAnchor anchor, Vec2 pos, Vec2 size,
                                  UIShape shape = UIShape::Pill) {
        GameObject* g = Child(s, parent, "Progress"); auto* pb = g->AddComponent<UIProgressBar>();
        pb->anchor = anchor; pb->position = pos; pb->size = size;
        pb->value = 0.4f; pb->shape = shape; pb->fill = slate::Accent(); pb->background = Color::FromBytes(255, 255, 255, 24);
        auto* bb = g->AddComponent<UIBarBind>(); bb->var = "loading"; bb->min = 0.0f; bb->max = 1.0f;
        return g;
    }
    inline GameObject* LoadingSpinner(Scene& s, GameObject* parent, UIAnchor anchor, Vec2 pos, float d,
                                      float thickness = 4.0f) {
        GameObject* g = Child(s, parent, "Spinner"); auto* r = g->AddComponent<UIRadialProgress>();
        r->anchor = anchor; r->position = pos; r->size = {d, d};
        r->value = 0.25f; r->thickness = thickness; r->spin = true;
        r->background = Color::FromBytes(255, 255, 255, 20); r->fill = slate::Accent();
        return g;
    }
}

// Minimal loading: a spinner and a small label in the middle, a thin bar under them.
inline GameObject* AddLoadingMinimal(Scene& scene) {
    using namespace detail;
    GameObject* root = LoadingBackdrop(scene, "Loading (Minimal)", Color::FromBytes(10, 11, 15), Color::FromBytes(10, 11, 15));
    LoadingSpinner(scene, root, UIAnchor::Center, {0, -36}, 48.0f, 5.0f);
    Text(scene, root, "Label", "Loading", UIAnchor::Center, {0, 20}, {300, 24}, 16.0f, slate::Text(), 1);
    LoadingBar(scene, root, UIAnchor::Center, {0, 56}, {240, 4});
    Text(scene, root, "Tip", "Tip: hold Shift to sprint.", UIAnchor::BottomCenter, {0, -48}, {700, 22}, 14.0f, slate::Dim(), 1);
    return root;
}

// Card loading: a raised card in the middle with the level name, a tip and the bar.
inline GameObject* AddLoadingCard(Scene& scene) {
    using namespace detail;
    GameObject* root = LoadingBackdrop(scene, "Loading (Card)", slate::Backdrop(), Color::FromBytes(28, 36, 56, 255));
    GameObject* card = Panel(scene, root, "Card", UIAnchor::Center, {0, 0}, {540, 210}, slate::Panel(), 16.0f, true);
    Caption(scene, card, "Caption", "NOW LOADING", UIAnchor::TopLeft, {32, 28}, 300);
    Text(scene, card, "Title", "Dunes", UIAnchor::TopLeft, {32, 50}, {400, 40}, 30.0f, slate::Text(), 0, true);
    LoadingSpinner(scene, card, UIAnchor::TopRight, {-32, 32}, 30.0f, 3.5f);
    Text(scene, card, "Tip", "Tip: hold Shift to sprint.", UIAnchor::BottomLeft, {32, -54}, {476, 22}, 14.0f, slate::Dim());
    LoadingBar(scene, card, UIAnchor::BottomCenter, {0, -32}, {476, 8});
    IntroAnim(card, UIAnimation::Intro::Pop);
    return root;
}

// Splash loading: a big centred logo and game name with a thin bar under them.
inline GameObject* AddLoadingSplash(Scene& scene) {
    using namespace detail;
    GameObject* root = LoadingBackdrop(scene, "Loading (Splash)", Color::FromBytes(8, 9, 12), Color::FromBytes(8, 9, 12));
    GameObject* logo = Panel(scene, root, "Logo", UIAnchor::Center, {0, -70}, {120, 120}, slate::Accent(), 28.0f);
    { auto* p = logo->GetComponent<UIPanel>(); p->useGradient = true; p->colorBottom = Color::FromBytes(120, 90, 240);
      p->gradientDir = UIPanel::GradientDir::DiagonalDown; p->borderWidth = 0.0f; }
    Text(scene, logo, "Mark", "OK", UIAnchor::TopLeft, {0, 0}, {120, 120}, 44.0f, Color::White, 1, true);
    Text(scene, root, "Title", "Star Arena", UIAnchor::Center, {0, 26}, {500, 40}, 32.0f, slate::Text(), 1, true);
    LoadingBar(scene, root, UIAnchor::Center, {0, 70}, {220, 4});
    Text(scene, root, "Tip", "Tip: hold Shift to sprint.", UIAnchor::BottomCenter, {0, -48}, {700, 22}, 14.0f, slate::Dim(), 1);
    IntroAnim(logo, UIAnimation::Intro::Pop);
    return root;
}

// Cinematic loading: letterbox bars, the chapter and level bottom-left, the bar along
// the lower letterbox edge and a spinner at the bottom-right.
inline GameObject* AddLoadingCinematic(Scene& scene) {
    using namespace detail;
    GameObject* root = LoadingBackdrop(scene, "Loading (Cinematic)", Color::FromBytes(20, 24, 32), Color::FromBytes(8, 9, 12));
    { GameObject* g = Child(scene, root, "Letterbox Top"); auto* p = g->AddComponent<UIPanel>();
      p->anchor = UIAnchor::StretchHorizontal; p->position = {0, 0}; p->size = {0, 86};
      p->shape = UIShape::Rectangle; p->cornerRadius = 0.0f; p->color = Color::Black; }
    { GameObject* g = Child(scene, root, "Letterbox Bottom"); auto* p = g->AddComponent<UIPanel>();
      p->anchor = UIAnchor::StretchHorizontal; p->position = {0, 634}; p->size = {0, 86};
      p->shape = UIShape::Rectangle; p->cornerRadius = 0.0f; p->color = Color::Black; }
    Caption(scene, root, "Chapter", "CHAPTER 2", UIAnchor::BottomLeft, {76, -186}, 400);
    Text(scene, root, "Title", "The Long Night", UIAnchor::BottomLeft, {76, -126}, {700, 52}, 44.0f, slate::Text(), 0, true);
    Text(scene, root, "Tip", "Tip: hold Shift to sprint.", UIAnchor::BottomLeft, {76, -32}, {800, 22}, 15.0f, slate::Dim());
    LoadingBar(scene, root, UIAnchor::BottomCenter, {0, -86}, {1280, 3}, UIShape::Rectangle);
    LoadingSpinner(scene, root, UIAnchor::BottomRight, {-76, -26}, 32.0f, 3.5f);
    return root;
}

// Shop: coins badge + three item cards with a price and Buy.
inline GameObject* AddShop(Scene& scene) {
    using namespace detail;
    GameObject* canvas = EnsureCanvas(scene);
    GameObject* root = Panel(scene, canvas, "Shop", UIAnchor::Center, {0, 0}, {780, 452}, slate::Panel(), 16.0f, true);
    Text(scene, root, "Title", "Shop", UIAnchor::TopLeft, {32, 24}, {300, 40}, 30.0f, slate::Text(), 0, true);
    Badge(scene, root, "Coins", "1,250 coins", UIAnchor::TopRight, {-72, 32}, slate::Warning());
    { GameObject* close = Button(scene, root, "CloseButton", "x", UIAnchor::TopRight, {-24, 26}, {34, 34}, ButtonKind::Secondary);
      close->GetComponent<UIButton>()->shape = UIShape::Circle; }
    const char* items[] = {"Plasma Blade", "Jet Boots", "Shield Orb"};
    const char* desc[]  = {"Melee - Epic", "Gear - Rare", "Gadget - Common"};
    const char* price[] = {"900", "450", "200"};
    const unsigned char tint[3][3] = {{170, 110, 240}, {66, 150, 250}, {72, 196, 128}};
    for (int i = 0; i < 3; ++i) {
        GameObject* card = Panel(scene, root, (std::string("Item") + std::to_string(i + 1)).c_str(), UIAnchor::TopLeft,
                                 {32.0f + i * 244.0f, 88}, {228, 332}, slate::Card(), 12.0f);
        GameObject* art = Panel(scene, card, "Art", UIAnchor::TopLeft, {14, 14}, {200, 140},
                                Color::FromBytes(tint[i][0], tint[i][1], tint[i][2], 60), 8.0f);
        auto* ap = art->GetComponent<UIPanel>();
        ap->useGradient = true; ap->gradientDir = UIPanel::GradientDir::DiagonalDown;
        ap->colorBottom = Color::FromBytes(tint[i][0], tint[i][1], tint[i][2], 20);
        Text(scene, card, "Name", items[i], UIAnchor::TopLeft, {16, 166}, {196, 24}, 18.0f, slate::Text(), 0, true);
        Text(scene, card, "Kind", desc[i], UIAnchor::TopLeft, {16, 192}, {196, 20}, 13.0f, slate::Dim());
        Text(scene, card, "Price", std::string(price[i]) + " coins", UIAnchor::TopLeft, {16, 222}, {196, 24}, 16.0f, slate::Warning(), 0, true);
        Button(scene, card, "BuyButton", "Buy", UIAnchor::BottomLeft, {14, -14}, {200, 44});
    }
    return root;
}

// Modal confirm: dim + card with a title, message, Cancel / Confirm.
inline GameObject* AddConfirmDialog(Scene& scene) {
    using namespace detail;
    GameObject* canvas = EnsureCanvas(scene);
    GameObject* root = Group(scene, canvas, "Confirm Dialog");
    Dimmer(scene, root, 0.55f);
    GameObject* card = Panel(scene, root, "Card", UIAnchor::Center, {0, 0}, {420, 216}, slate::Panel(), 14.0f, true);
    Text(scene, card, "Title", "Leave the match?", UIAnchor::TopLeft, {28, 24}, {364, 30}, 22.0f, slate::Text(), 0, true);
    auto* msg = Text(scene, card, "Message", "You'll lose your progress in this round. Your stats so far are kept.",
                     UIAnchor::TopLeft, {28, 62}, {364, 60}, 15.0f, slate::Dim())->GetComponent<TextRenderer>();
    msg->wrap = true; msg->vcenter = false;
    Button(scene, card, "CancelButton", "Cancel", UIAnchor::BottomRight, {-160, -24}, {120, 44}, ButtonKind::Secondary);
    Button(scene, card, "ConfirmButton", "Leave", UIAnchor::BottomRight, {-28, -24}, {120, 44}, ButtonKind::Danger);
    IntroAnim(card, UIAnimation::Intro::Pop);
    return root;
}

// A toast on its own (the kit piece, on the scene's canvas).
inline GameObject* AddNotificationScreen(Scene& scene) {
    GameObject* n = AddNotification(scene, EnsureCanvas(scene));
    UILayout::AvoidOverlap(scene, n);   // below a HUD score chip already in the corner
    return n;
}

// Matchmaking banner: spinner + "Finding match" + elapsed time + Cancel (top-centre).
inline GameObject* AddMatchmaking(Scene& scene) {
    using namespace detail;
    GameObject* canvas = EnsureCanvas(scene);
    GameObject* root = Panel(scene, canvas, "Matchmaking", UIAnchor::TopCenter, {0, 20}, {420, 72}, slate::Panel(), 14.0f, true);
    { GameObject* g = Child(scene, root, "Spinner"); auto* r = g->AddComponent<UIRadialProgress>();
      r->anchor = UIAnchor::TopLeft; r->position = {18, 18}; r->size = {36, 36};
      r->value = 0.25f; r->thickness = 4.0f; r->spin = true;
      r->background = Color::FromBytes(255, 255, 255, 20); r->fill = slate::Accent(); }
    Text(scene, root, "Title", "Finding a match...", UIAnchor::TopLeft, {70, 14}, {230, 22}, 16.0f, slate::Text(), 0, true);
    Text(scene, root, "Status", "Ranked - 0:12", UIAnchor::TopLeft, {70, 38}, {230, 20}, 13.0f, slate::Dim());
    Button(scene, root, "CancelButton", "Cancel", UIAnchor::MiddleRight, {-16, 0}, {96, 38}, ButtonKind::Secondary);
    IntroAnim(root, UIAnimation::Intro::SlideTop);
    UILayout::AvoidOverlap(scene, root);
    return root;
}

// Leaderboard: ranked rows with gold / silver / bronze medals for the top three.
inline GameObject* AddLeaderboard(Scene& scene) {
    using namespace detail;
    GameObject* canvas = EnsureCanvas(scene);
    GameObject* root = Panel(scene, canvas, "Leaderboard", UIAnchor::Center, {0, 0}, {460, 500}, slate::Panel(), 16.0f, true);
    Caption(scene, root, "Caption", "THIS SEASON", UIAnchor::TopLeft, {28, 24}, 300);
    Text(scene, root, "Title", "Leaderboard", UIAnchor::TopLeft, {28, 42}, {300, 36}, 28.0f, slate::Text(), 0, true);
    const char* names[] = {"Maya", "Leo", "Sam", "Ava", "Noah", "Zoe", "Kai"};
    const int pts[] = {4820, 4410, 3975, 3510, 3120, 2880, 2600};
    const unsigned char medal[3][3] = {{240, 196, 70}, {190, 198, 210}, {214, 140, 90}};
    for (int i = 0; i < 7; ++i) {
        GameObject* row = Panel(scene, root, (std::string("Row") + std::to_string(i + 1)).c_str(), UIAnchor::TopLeft,
                                {16, 96.0f + i * 54.0f}, {428, 48}, i % 2 ? Color::FromBytes(255, 255, 255, 6) : slate::Card(), 8.0f);
        GameObject* rank = Panel(scene, row, "Rank", UIAnchor::TopLeft, {10, 10}, {28, 28},
                                 i < 3 ? Color::FromBytes(medal[i][0], medal[i][1], medal[i][2]) : Color::FromBytes(255, 255, 255, 18), 0.0f);
        rank->GetComponent<UIPanel>()->shape = UIShape::Circle;
        Text(scene, rank, "Number", std::to_string(i + 1), UIAnchor::TopLeft, {0, 0}, {28, 28}, 13.0f,
             i < 3 ? Color::FromBytes(24, 27, 34) : slate::Dim(), 1, true);
        Avatar(scene, row, "Avatar", names[i], UIAnchor::TopLeft, {48, 8}, 32.0f);
        Text(scene, row, "Name", names[i], UIAnchor::TopLeft, {90, 0}, {200, 48}, 16.0f);
        Text(scene, row, "Points", std::to_string(pts[i]), UIAnchor::TopRight, {-14, 0}, {120, 48}, 16.0f, slate::Text(), 2, true);
    }
    return root;
}

} } // namespace okay::UITemplates
