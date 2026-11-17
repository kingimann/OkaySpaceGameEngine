#pragma once
// The UI component library example scenes (File > Examples > UI library, and examples/ui_library/*.okayscene).
//
// BuildSlice1() fills a scene with the parts added in slice 1 (range slider, input validation, aspect-ratio
// container) next to plain on-screen instructions. BuildSlice2() does the same for slice 2 (game HUD parts and the
// data-driven glue: bindings, conditions, array lists, rich text links, hot reload). BuildSlice3() does it for slice 3 (app parts: menu bar,
// toolbar, tree view, data table, wizard, form group, the colour picker's advanced mode). Written for this engine from its own UI helpers
// (UITemplates, UIGallery's card helpers); no other project's code or assets were used.
#include "okay/Scene/UIGallery.hpp"
#include "okay/Scene/EnsureCamera.hpp"
#include "okay/Components/UIWidgetsMore.hpp"
#include "okay/Components/QuestLog.hpp"
#include "okay/Components/UITextBind.hpp"
#include "okay/Components/UIAppParts.hpp"
#include "okay/Components/UIColorPicker.hpp"
#include "okay/Scene/UIScreens.hpp"
#include "okay/Scene/UIStyleLib.hpp"
#include "okay/UI/UIIcons.hpp"
#include <string>

namespace okay { namespace UILibrary {

using namespace okay::UITemplates;
using namespace okay::UITemplates::detail;
namespace gx = okay::UIGallery::gx;   // the gallery's card, label and button helpers

namespace detail2 {
inline GameObject* Field(Scene& s, GameObject* parent, const char* name, Vec2 pos, Vec2 size, const char* placeholder,
                         UIInputValidator::Kind kind) {
    GameObject* g = Child(s, parent, name);
    auto* in = g->AddComponent<UIInputField>();
    in->anchor = UIAnchor::TopLeft; in->position = pos; in->size = size; in->placeholder = placeholder;
    in->color = slate::Field(); in->focusColor = slate::FieldHover(); in->borderWidth = 1.0f;
    auto* v = g->AddComponent<UIInputValidator>();
    v->kind = kind;
    return g;
}
/// A few lines of small grey text that wrap inside `width`.
inline GameObject* Note(Scene& s, GameObject* parent, const char* name, const std::string& t, Vec2 pos, float width, float height) {
    GameObject* g = Text(s, parent, name, t, UIAnchor::TopLeft, pos, {width, height}, 13.0f, slate::Dim());
    auto* tr = g->GetComponent<TextRenderer>();
    tr->wrap = true; tr->vcenter = false;
    return g;
}
} // namespace detail2

/// Slice 1: essentials and layout. Returns the page root.
inline GameObject* BuildSlice1(Scene& scene) {
    EnsureSceneCamera(scene, true);   // a scene without a main camera renders nothing when Play starts (tests/test_examples_work)
    GameObject* canvas = EnsureCanvas(scene);
    if (Canvas* cv = canvas->GetComponent<Canvas>()) cv->referenceResolution = {1280.0f, 720.0f};
    GameObject* root = Group(scene, canvas, "UI Library 1");
    Panel(scene, root, "Backdrop", UIAnchor::StretchFull, {0, 0}, {0, 0}, slate::Backdrop(), 0.0f);
    scene.Find("Backdrop")->GetComponent<UIPanel>()->borderWidth = 0.0f;
    Text(scene, root, "Title", "UI Library 1", UIAnchor::TopLeft, {32, 12}, {600, 40}, 30.0f, slate::Text(), 0, true);
    detail2::Note(scene, root, "Subtitle",
         "Essentials and layout. Drag the handles. Click a box and type (try a wrong email). Make the window taller or wider: the blue frame keeps its shape.",
         {34, 50}, 1200, 40);

    // ---- Card 1: range slider -------------------------------------------------------------
    GameObject* c1 = gx::Card(scene, root, "Range", "Range slider", {32, 104}, {392, 330});
    gx::Label(scene, c1, "PriceLabel", "Price range", {20, 44}, 200);
    {
        GameObject* g = Child(scene, c1, "Price");
        auto* w = g->AddComponent<UIRangeSlider>();
        w->anchor = UIAnchor::TopLeft; w->position = {20, 96}; w->size = {352, 30};
        w->minValue = 0; w->maxValue = 500; w->low = 120; w->high = 380; w->step = 10;
        w->valueFormat = "${lo} to ${hi}"; w->fill = slate::Accent();
    }
    gx::Label(scene, c1, "LevelLabel", "Level range (3 apart)", {20, 156}, 352);
    {
        GameObject* g = Child(scene, c1, "Level");
        auto* w = g->AddComponent<UIRangeSlider>();
        w->anchor = UIAnchor::TopLeft; w->position = {20, 208}; w->size = {352, 30};
        w->minValue = 1; w->maxValue = 60; w->low = 10; w->high = 25; w->wholeNumbers = true; w->minGap = 3;
        w->valueFormat = "Levels {lo} to {hi}"; w->fill = slate::Success();
    }
    detail2::Note(scene, c1, "RangeHint", "Drag a handle. They never cross. Left and right move one, Enter swaps.", {20, 262}, 352, 60);

    // ---- Card 2: input validation ---------------------------------------------------------
    GameObject* c2 = gx::Card(scene, root, "Form", "Input validation", {448, 104}, {392, 500});
    gx::Label(scene, c2, "EmailLabel", "Email", {20, 44}, 200);
    GameObject* email = detail2::Field(scene, c2, "Email", {20, 66}, {352, 38}, "name@site.com", UIInputValidator::Kind::Email);
    email->GetComponent<UIInputValidator>()->showOk = true;
    gx::Label(scene, c2, "AgeLabel", "Age (13 to 120)", {20, 138}, 200);
    GameObject* age = detail2::Field(scene, c2, "Age", {20, 160}, {352, 38}, "a whole number", UIInputValidator::Kind::Integer);
    {
        auto* v = age->GetComponent<UIInputValidator>();
        v->useRange = true; v->minNumber = 13; v->maxNumber = 120; v->showOk = true;
        age->GetComponent<UIInputField>()->contentType = UIInputField::ContentType::Standard;
    }
    gx::Label(scene, c2, "PassLabel", "Password (6 or more)", {20, 232}, 352);
    GameObject* pass = detail2::Field(scene, c2, "Password", {20, 254}, {352, 38}, "choose a password", UIInputValidator::Kind::Any);
    pass->GetComponent<UIInputField>()->contentType = UIInputField::ContentType::Password;
    pass->GetComponent<UIInputValidator>()->minLength = 6;
    gx::Label(scene, c2, "RepeatLabel", "Repeat the password", {20, 326}, 352);
    GameObject* rep = detail2::Field(scene, c2, "Repeat", {20, 348}, {352, 38}, "type it again", UIInputValidator::Kind::Any);
    rep->GetComponent<UIInputField>()->contentType = UIInputField::ContentType::Password;
    {
        auto* v = rep->GetComponent<UIInputValidator>();
        v->mustMatch = "Password"; v->message = "That is not the same password"; v->showOk = true;
    }
    detail2::Note(scene, c2, "FormHint", "A red ring and a short sentence appear once you type a wrong value. A green ring means the box is fine.", {20, 404}, 352, 60);

    // ---- Card 3: aspect ratio, the card is as tall as the window ------------------------------
    GameObject* c3 = Panel(scene, root, "Aspect", UIAnchor::StretchVertical, {864, 104}, {384, 32}, slate::Panel(), 14.0f, true);
    c3->GetComponent<UIPanel>()->blocksPointer = true;
    Caption(scene, c3, "Caption", "Aspect ratio box (16:9)", UIAnchor::TopLeft, {20, 14}, 344, slate::Accent());
    {
        // A transparent holder that leaves room for the caption on top and the hint at the bottom; the box fits inside it.
        GameObject* holder = Panel(scene, c3, "Holder", UIAnchor::StretchFull, {0, 44}, {0, 90}, Color(0, 0, 0, 0), 0.0f);
        holder->GetComponent<UIPanel>()->borderWidth = 0.0f;
        GameObject* area = Child(scene, holder, "Area");
        auto* ar = area->AddComponent<UIAspectRatio>();
        ar->ratio = 16.0f / 9.0f; ar->padding = 20.0f; ar->alignY = 0.0f;
        GameObject* frame = Panel(scene, area, "Frame", UIAnchor::StretchFull, {0, 0}, {0, 0}, Color::FromBytes(54, 92, 150), 10.0f);
        auto* fp = frame->GetComponent<UIPanel>();
        fp->borderWidth = 3.0f; fp->borderColor = Color::FromBytes(230, 236, 250);
        GameObject* dot = Child(scene, frame, "Dot");
        auto* dp = dot->AddComponent<UIPanel>();
        dp->anchor = UIAnchor::Center; dp->position = {0, 0}; dp->size = {56, 56}; dp->shape = UIShape::Circle;
        dp->color = Color::FromBytes(250, 200, 80);
        Text(scene, frame, "Label", "16 : 9", UIAnchor::BottomCenter, {0, -14}, {200, 24}, 18.0f, Color::White, 1, true);
    }
    // The bottom of the card carries the hint so it stays readable at any window height.
    {
        GameObject* hint = detail2::Note(scene, c3, "Hint", "The frame stays 16:9 on a phone, a tablet and an ultrawide screen.", {0, 0}, 344, 50);
        auto* tr = hint->GetComponent<TextRenderer>();
        tr->anchor = UIAnchor::BottomLeft; tr->screenPos = {20, -70};
    }

    // ---- Notes ---------------------------------------------------------------------------------
    GameObject* c4 = gx::Card(scene, root, "More", "Also in the library", {32, 448}, {392, 170});
    detail2::Note(scene, c4, "More1", "More parts: File > Examples > UI Gallery.", {20, 44}, 352, 40);
    detail2::Note(scene, c4, "More2", "Add any part: GameObject > UI > Controls, or Layout & Overlays.", {20, 96}, 352, 60);
    return root;
}

/// The markup the hot-reload demo starts with (also the content of examples/ui_library/hot_reload_demo.okayui).
inline const char* HotReloadDemoMarkup() {
    return "text \"HOT RELOAD: edit and save\" pos=20,254 size=1.5 color=250,200,80,255\n"
           "text \"hot_reload_demo.okayui\" pos=20,270 size=1.5 color=250,200,80,255\n";
}

/// Slice 2: game HUD parts and the data-driven glue. Returns the page root.
inline GameObject* BuildSlice2(Scene& scene) {
    EnsureSceneCamera(scene, true);   // a scene without a main camera renders nothing when Play starts (tests/test_examples_work)
    GameObject* canvas = EnsureCanvas(scene);
    if (Canvas* cv = canvas->GetComponent<Canvas>()) cv->referenceResolution = {1280.0f, 720.0f};
    GameObject* root = Group(scene, canvas, "UI Library 2");
    Panel(scene, root, "Backdrop", UIAnchor::StretchFull, {0, 0}, {0, 0}, slate::Backdrop(), 0.0f);
    scene.Find("Backdrop")->GetComponent<UIPanel>()->borderWidth = 0.0f;
    Text(scene, root, "Title", "UI Library 2", UIAnchor::TopLeft, {32, 12}, {600, 40}, 30.0f, slate::Text(), 0, true);
    detail2::Note(scene, root, "Subtitle",
         "Game parts and data-driven parts. Press Play, then click the buttons and drag the sliders. Q uses the ability, Tab moves the focus ring.",
         {34, 50}, 800, 40);

    // The no-code glue: a script on one object runs the quest buttons and fills the list.
    GameObject* demo = Child(scene, root, "Demo");
    demo->AddComponent<ScriptComponent>()->LoadSource(
        "function start() {\n"
        "  start_quest(\"wolves\");\n"
        "  run_action(\"array_push scores 120 ; array_push scores 85 ; array_push scores 40\");\n"
        "}\n"
        "function quest_wolf() { add_quest_progress(\"wolves\", 0, 1); }\n"
        "function quest_talk() { complete_objective(\"wolves\", 1); }\n"
        "function quest_bridge() { complete_objective(\"wolves\", 2); }\n"
        "function quest_again() { fail_quest(\"wolves\"); start_quest(\"wolves\"); }\n"
        "function more_scores() { run_action(\"array_push scores 55\"); }\n"
        "function empty_scores() { run_action(\"array_clear scores\"); }\n");
    {
        GameObject* q = Child(scene, root, "Quests");
        auto* log = q->AddComponent<QuestLog>();
        log->showTracker = false;       // our own tracker below draws the list
        log->showNotifications = false;
        log->allowJournal = false;
        QuestDef d;
        d.id = "wolves"; d.title = "Wolf trouble"; d.repeatable = true;
        QuestObjective o1; o1.kind = QuestObjective::Kind::Custom; o1.description = "Hunt the wolves"; o1.target = "wolf"; o1.count = 3;
        QuestObjective o2; o2.kind = QuestObjective::Kind::Custom; o2.description = "Tell the mayor"; o2.target = "mayor";
        QuestObjective o3; o3.kind = QuestObjective::Kind::Custom; o3.description = "Cross the bridge"; o3.target = "bridge";
        d.objectives = {o1, o2, o3};
        log->quests.push_back(d);
    }
    auto onClick = [&](GameObject* btn, const char* target, const char* fn) {
        auto* b = btn->GetComponent<UIButton>();
        b->clickTarget = target; b->clickFunction = fn; b->fontScale = 1.5f;
        return btn;
    };

    // ---- Card 1: ammo, ability, prompt ----------------------------------------------------------------------
    GameObject* c1 = gx::Card(scene, root, "AmmoCard", "Ammo, ability, prompt", {32, 96}, {392, 304});
    {
        GameObject* g = Child(scene, c1, "Ammo");
        auto* w = g->AddComponent<UIAmmoCounter>();
        w->anchor = UIAnchor::TopLeft; w->position = {20, 44}; w->size = {214, 84};
        w->current = 24; w->reserve = 90; w->magazine = 30;
        onClick(gx::Btn(scene, c1, "Shoot", "Shoot", {248, 44}, {124, 36}, 0), "Ammo", "Shoot");
        onClick(gx::Btn(scene, c1, "Reload", "Reload", {248, 92}, {124, 36}, 0), "Ammo", "Reload");
    }
    {
        GameObject* g = Child(scene, c1, "Dash");
        auto* ring = g->AddComponent<UIRadialProgress>();
        ring->anchor = UIAnchor::TopLeft; ring->position = {20, 142}; ring->size = {72, 72};
        ring->thickness = 7.0f; ring->value = 0.0f; ring->showPercent = false;
        ring->fill = Color::FromBytes(14, 16, 24, 240); ring->background = Color::FromBytes(110, 215, 150, 220);
        auto* a = g->AddComponent<UIAbilityIcon>();
        a->label = "Q"; a->keyText = "Q"; a->useKey = 'q'; a->cooldown = 4.0f;
        onClick(gx::Btn(scene, c1, "UseAbility", "Use (or press Q)", {108, 160}, {264, 36}, 0), "Dash", "Use");
    }
    {
        GameObject* g = Child(scene, c1, "NearDoor");
        auto* t = g->AddComponent<UIToggle>();
        t->anchor = UIAnchor::TopLeft; t->position = {20, 226}; t->size = {28, 28};
        t->label = "I am near the door"; t->on = false;
        g->AddComponent<UIBind>()->var = "near_door";
        GameObject* p = Child(scene, c1, "Prompt");
        auto* ip = p->AddComponent<UIInteractionPrompt>();
        ip->anchor = UIAnchor::TopLeft; ip->position = {20, 262}; ip->size = {352, 34};
        ip->key = "E"; ip->action = "Open the door"; ip->showVar = "near_door";
    }

    // ---- Card 2: radial menu and kill feed ------------------------------------------------------------------
    GameObject* c2 = gx::Card(scene, root, "WheelCard", "Radial menu, kill feed", {444, 96}, {392, 304});
    {
        GameObject* g = Child(scene, c2, "Wheel");
        auto* w = g->AddComponent<UIRadialMenu>();
        w->anchor = UIAnchor::TopLeft; w->position = {10, 58}; w->size = {220, 220};
        w->items = "Attack|Defend|Heal|Run"; w->alwaysShown = true; w->holdToOpen = false; w->innerRadius = 30.0f;
        w->chosenVar = "radial_pick";
        gx::Label(scene, c2, "PickedLabel", "You picked", {244, 62}, 130, slate::Dim(), 12.0f);
        GameObject* t = Text(scene, c2, "Picked", "nothing yet", UIAnchor::TopLeft, {244, 82}, {130, 40}, 15.0f, slate::Text());
        t->GetComponent<TextRenderer>()->wrap = true; t->GetComponent<TextRenderer>()->vcenter = false;
        t->AddComponent<UITextBind>()->format = "{radial_pick_text|nothing yet}";
        onClick(gx::Btn(scene, c2, "NewKill", "New kill", {244, 140}, {128, 36}, 0), "KillFeed", "AddSampleKill");
        GameObject* hint = Text(scene, c2, "WheelHint", "Click a wedge. Kills show at the top right of the screen.", UIAnchor::TopLeft, {244, 186}, {128, 90}, 12.0f, slate::Dim());
        hint->GetComponent<TextRenderer>()->wrap = true; hint->GetComponent<TextRenderer>()->vcenter = false;
    }
    {
        GameObject* g = Child(scene, root, "KillFeed");   // a HUD part: top right of the screen, over the cards' header row
        auto* f = g->AddComponent<UIKillFeed>();
        f->anchor = UIAnchor::TopRight; f->position = {-32, 10}; f->size = {400, 80};
        f->maxEntries = 3; f->rowHeight = 26.0f; f->playerName = "Player";
        f->Push("Raider", "Wolf", "Axe");
        f->Push("Player", "Raider", "Rifle");
        f->lifetime = 12.0f;
    }

    // ---- Card 3: quests and compass -------------------------------------------------------------------------
    GameObject* c3 = gx::Card(scene, root, "QuestCard", "Objectives, compass", {856, 96}, {392, 304});
    {
        GameObject* g = Child(scene, c3, "Tracker");
        auto* w = g->AddComponent<UIObjectiveTracker>();
        w->anchor = UIAnchor::TopLeft; w->position = {20, 44}; w->size = {352, 124};
        onClick(gx::Btn(scene, c3, "Wolf", "Wolf", {20, 176}, {80, 32}, 0), "Demo", "quest_wolf");
        onClick(gx::Btn(scene, c3, "Talk", "Talk", {108, 176}, {80, 32}, 0), "Demo", "quest_talk");
        onClick(gx::Btn(scene, c3, "Bridge", "Bridge", {196, 176}, {88, 32}, 0), "Demo", "quest_bridge");
        onClick(gx::Btn(scene, c3, "Again", "Again", {292, 176}, {80, 32}, 1), "Demo", "quest_again");
        gx::Label(scene, c3, "CompassLabel", "Turn the compass", {20, 218}, 200);
        GameObject* sl = Child(scene, c3, "Heading");
        auto* s = sl->AddComponent<UISlider>();
        s->anchor = UIAnchor::TopLeft; s->position = {20, 240}; s->size = {352, 18};
        s->minValue = 0; s->maxValue = 360; s->value = 20; s->showValue = true; s->wholeNumbers = true;
        sl->AddComponent<UIBind>()->var = "compass_deg";
        GameObject* cg = Child(scene, c3, "Compass");
        auto* c = cg->AddComponent<UICompass>();
        c->anchor = UIAnchor::TopLeft; c->position = {20, 264}; c->size = {352, 32};
        c->headingVar = "compass_deg"; c->fieldOfView = 140.0f; c->showDegrees = false;
    }

    // ---- Card 4: item tooltip card --------------------------------------------------------------------------
    GameObject* c4 = gx::Card(scene, root, "ItemCard", "Item tooltip card", {32, 412}, {392, 292});
    {
        GameObject* g = Child(scene, c4, "ItemPick");
        auto* dd = g->AddComponent<UIDropdown>();
        dd->anchor = UIAnchor::TopLeft; dd->position = {20, 44}; dd->size = {352, 34};
        dd->options = {"Iron Sword", "Healing Potion", "Leather Cap"}; dd->value = 0;
        g->AddComponent<UIBind>()->var = "item_pick";
        GameObject* cg = Child(scene, c4, "Card");
        auto* card = cg->AddComponent<UIItemTooltipCard>();
        card->anchor = UIAnchor::TopLeft; card->position = {20, 92}; card->size = {352, 190};
        card->item = "Iron Sword"; card->itemVar = "item_pick_text";
        card->itemsFile = "examples/ui_library/ui_library_items.okayitems";
    }

    // ---- Card 5: two-way binding and conditions -------------------------------------------------------------
    GameObject* c5 = gx::Card(scene, root, "BindCard", "Bindings and conditions", {444, 412}, {392, 292});
    {
        gx::Label(scene, c5, "VolLabel", "Volume: slider and box match", {20, 44}, 352);
        GameObject* sl = Child(scene, c5, "Volume");
        auto* s = sl->AddComponent<UISlider>();
        s->anchor = UIAnchor::TopLeft; s->position = {20, 70}; s->size = {352, 20};
        s->minValue = 0; s->maxValue = 100; s->value = 60; s->wholeNumbers = true;
        sl->AddComponent<UIBind>()->var = "volume";
        GameObject* in = Child(scene, c5, "VolumeBox");
        auto* ib = in->AddComponent<UIInputField>();
        ib->anchor = UIAnchor::TopLeft; ib->position = {20, 110}; ib->size = {170, 36}; ib->placeholder = "0 to 100";
        ib->color = slate::Field(); ib->focusColor = slate::FieldHover(); ib->borderWidth = 1.0f;
        in->AddComponent<UIBind>()->var = "volume";
        GameObject* mute = Child(scene, c5, "Mute");
        auto* mt = mute->AddComponent<UIToggle>();
        mt->anchor = UIAnchor::TopLeft; mt->position = {212, 114}; mt->size = {28, 28}; mt->label = "Mute"; mt->on = false;
        mute->AddComponent<UIBind>()->var = "muted";
        GameObject* now = detail2::Note(scene, c5, "VolNow", "Volume is 60", {20, 160}, 352, 20);
        now->AddComponent<UITextBind>()->format = "Volume is {volume|?}";
        now->GetComponent<TextRenderer>()->color = slate::Text();
        GameObject* warn = detail2::Note(scene, c5, "LowWarning", "Very quiet: turn it up", {20, 190}, 352, 20);
        warn->GetComponent<TextRenderer>()->color = slate::Danger();
        warn->AddComponent<UIVisibleIf>()->condition = "volume < 20 && !muted";
        GameObject* muted = detail2::Note(scene, c5, "MutedNote", "Muted", {300, 190}, 72, 20);
        muted->GetComponent<TextRenderer>()->color = slate::Accent();
        muted->AddComponent<UIVisibleIf>()->condition = "muted";
        detail2::Note(scene, c5, "BindHint", "Drag the slider, or type in the box. Slide below 20 for the warning. Turn Mute on to swap it.", {20, 224}, 352, 56);
    }

    // ---- Card 6: list from an array, links, hot reload ------------------------------------------------------
    GameObject* c6 = gx::Card(scene, root, "ListCard", "List, links, hot reload", {856, 412}, {392, 292});
    {
        GameObject* lg = Child(scene, c6, "Scores");
        auto* lv = lg->AddComponent<UIListView>();
        lv->anchor = UIAnchor::TopLeft; lv->position = {20, 44}; lv->size = {352, 84}; lv->itemHeight = 26.0f; lv->spacing = 2.0f;
        lv->background = Color::FromBytes(255, 255, 255, 14);
        GameObject* row = Text(scene, lg, "Row", "{item}", UIAnchor::TopLeft, {8, 0}, {336, 26}, 14.0f, slate::Text());
        (void)row;
        auto* lb = lg->AddComponent<UIListBind>();
        lb->var = "scores"; lb->format = "Score {index}:  {item} points"; lb->emptyText = "No scores yet";
        onClick(gx::Btn(scene, c6, "AddScore", "Add a score", {20, 136}, {172, 30}, 0), "Demo", "more_scores");
        onClick(gx::Btn(scene, c6, "ClearScores", "Clear", {200, 136}, {172, 30}, 1), "Demo", "empty_scores");
        GameObject* t = Text(scene, c6, "Links", "Read the [url=manual]manual[/url], or buy [icon=Iron Sword] and [icon=Healing Potion] for 5 [icon=coin].",
                             UIAnchor::TopLeft, {20, 176}, {352, 40}, 14.0f, slate::Text());
        auto* tr = t->GetComponent<TextRenderer>();
        tr->richText = true; tr->wrap = true; tr->vcenter = false;
        t->AddComponent<UIRichLinks>();
        GameObject* last = detail2::Note(scene, c6, "LastLink", "Link you clicked: none yet", {20, 222}, 352, 20);
        last->AddComponent<UITextBind>()->format = "Link you clicked: {ui_link|none yet}";
        GameObject* doc = Child(scene, c6, "HotDoc");
        doc->AddComponent<UIDocument>()->markup = HotReloadDemoMarkup();
        auto* hot = doc->AddComponent<UIHotReload>();
        hot->file = "examples/ui_library/hot_reload_demo.okayui"; hot->interval = 0.5f;
    }
    return root;
}

/// Slice 3: app parts. Returns the page root. Everything on the page is wired with no script: the status card shows what each part
/// chose through variables (menu_pick_text, tool_pick, tb_bold, tree_pick_text, table_pick_text, wiz_step, signup_name ...).
inline GameObject* BuildSlice3(Scene& scene) {
    EnsureSceneCamera(scene, true);   // a scene without a main camera renders nothing when Play starts (tests/test_examples_work)
    GameObject* canvas = EnsureCanvas(scene);
    if (Canvas* cv = canvas->GetComponent<Canvas>()) cv->referenceResolution = {1280.0f, 720.0f};
    GameObject* root = Group(scene, canvas, "UI Library 3");
    Panel(scene, root, "Backdrop", UIAnchor::StretchFull, {0, 0}, {0, 0}, slate::Backdrop(), 0.0f);
    scene.Find("Backdrop")->GetComponent<UIPanel>()->borderWidth = 0.0f;
    Text(scene, root, "Title", "UI Library 3", UIAnchor::TopLeft, {32, 40}, {400, 36}, 28.0f, slate::Text(), 0, true);
    detail2::Note(scene, root, "Subtitle",
         "Press Play. Try the menu bar, toolbar, tree, table, wizard and colour picker.",
         {34, 76}, 1220, 20);
    auto onClick = [&](GameObject* btn, const char* target, const char* fn, float arg = 0.0f) {
        auto* b = btn->GetComponent<UIButton>();
        b->clickTarget = target; b->clickFunction = fn; b->clickArg = arg; b->fontScale = 1.5f;
        return btn;
    };
    auto line = [&](GameObject* parent, const char* name, const char* format, Vec2 pos, float width) {
        GameObject* g = Text(scene, parent, name, format, UIAnchor::TopLeft, pos, {width, 28.0f}, 11.0f, slate::Text());
        g->GetComponent<TextRenderer>()->wrap = true; g->GetComponent<TextRenderer>()->vcenter = false;
        g->AddComponent<UITextBind>()->format = format;
        return g;
    };

    // ---- Toolbar -------------------------------------------------------------------------------------------
    {
        GameObject* g = Child(scene, root, "Toolbar");
        auto* w = g->AddComponent<UIToolbar>();
        w->anchor = UIAnchor::TopLeft; w->position = {32, 100}; w->size = {420, 40};
        w->items = "New@N|Open@O|Save@S|-|Bold@B*|Italic@I*|Underline@U*|-|Left@L=|Center@C=|Right@R=";
        w->states = "00000000010";                 // Center starts on
        w->showLabels = false;
        w->chosenVar = "tool_pick"; w->varPrefix = "tb_";
        w->accent = slate::Accent();
    }

    // ---- Card 1: tree view ---------------------------------------------------------------------------------
    GameObject* c1 = gx::Card(scene, root, "TreeCard", "Tree view", {32, 152}, {330, 262});
    {
        GameObject* g = Child(scene, c1, "Tree");
        auto* w = g->AddComponent<UITreeView>();
        w->anchor = UIAnchor::TopLeft; w->position = {20, 44}; w->size = {290, 170};
        w->nodes = "World|>Forest|>>Wolf den|>>Old oak|>Castle|>>Gate|>>Throne room|Settings|>Audio|>Video";
        w->expanded = "0,1"; w->selectedVar = "tree_pick";
        w->background = Color::FromBytes(255, 255, 255, 14); w->accent = slate::Accent();
        detail2::Note(scene, c1, "TreeHint", "Arrows open rows. Then use the arrow keys.", {20, 222}, 290, 34);
    }

    // ---- Card 2: data table ----------------------------------------------------------------------------------
    GameObject* c2 = gx::Card(scene, root, "TableCard", "Data table", {382, 152}, {520, 262});
    {
        GameObject* g = Child(scene, c2, "Table");
        auto* w = g->AddComponent<UIDataTable>();
        w->anchor = UIAnchor::TopLeft; w->position = {20, 44}; w->size = {480, 172};
        w->columns = "Name:170|Class:170|Level";
        w->rows = "Ava|Mage|42;Bo|Rogue|17;Cy|Knight|30;Di|Mage|8;Eli|Rogue|25;Fen|Knight|51;Gia|Mage|3;Hux|Rogue|12";
        w->selectedVar = "table_pick";
        w->background = Color::FromBytes(255, 255, 255, 14); w->accent = slate::Accent();
        onClick(gx::Btn(scene, c2, "AddRow", "Add a row", {20, 224}, {150, 28}, 0), "Table", "AddSampleRow");
        onClick(gx::Btn(scene, c2, "SortLevel", "By level", {178, 224}, {140, 28}, 0), "Table", "SortBy", 2.0f);
        onClick(gx::Btn(scene, c2, "Unsorted", "Unsorted", {326, 224}, {130, 28}, 1), "Table", "ClearSort");
    }

    // ---- Card 3: what each part chose ----------------------------------------------------------------------
    GameObject* c3 = gx::Card(scene, root, "StatusCard", "What you chose", {922, 152}, {326, 262});
    line(c3, "SMenu", "Menu: {menu_pick_text|none yet}", {20, 48}, 290);
    line(c3, "STool", "Tool: {tool_pick|none yet}", {20, 78}, 290);
    line(c3, "SToggles", "B {tb_bold|0}  I {tb_italic|0}  U {tb_underline|0}", {20, 108}, 290);
    line(c3, "STree", "Tree: {tree_pick_text|none yet}", {20, 138}, 290);
    line(c3, "STable", "Row: {table_pick_text|none yet}", {20, 168}, 290);
    line(c3, "SWizard", "Wizard step {wiz_step|0}, {wiz_progress|0} percent", {20, 198}, 290);
    line(c3, "SForm", "Fields all valid: {wiz_valid|0}", {20, 228}, 290);

    // ---- Card 4: wizard with form groups -------------------------------------------------------------------
    GameObject* c4 = gx::Card(scene, root, "WizardCard", "Wizard and form group", {32, 426}, {700, 282});
    {
        GameObject* g = Child(scene, c4, "Wizard");
        auto* w = g->AddComponent<UIWizard>();
        w->anchor = UIAnchor::TopLeft; w->position = {20, 44}; w->size = {660, 228};
        w->steps = "Account|Profile|Done"; w->pages = "AccountPage|ProfilePage|DonePage"; w->validate = "AccountForm;ProfileForm;";
        w->progressVar = "wiz_progress"; w->stepVar = "wiz_step";
        w->background = Color::FromBytes(255, 255, 255, 10); w->accent = slate::Accent();
    }
    {
        GameObject* page = Group(scene, c4, "AccountPage");
        GameObject* g = Child(scene, page, "AccountForm");
        auto* f = g->AddComponent<UIFormGroup>();
        f->anchor = UIAnchor::TopLeft; f->position = {50, 118}; f->size = {580, 100};
        f->fields = "Name:text|Email:email"; f->labelWidth = 110.0f; f->rowHeight = 50.0f; f->fieldHeight = 32.0f;
        f->submitLabel.clear(); f->valuesVar = "signup_"; f->validVar = "wiz_valid"; f->showSummary = false;
        f->Rebuild();
    }
    {
        GameObject* page = Group(scene, c4, "ProfilePage");
        GameObject* g = Child(scene, page, "ProfileForm");
        auto* f = g->AddComponent<UIFormGroup>();
        f->anchor = UIAnchor::TopLeft; f->position = {50, 118}; f->size = {580, 100};
        f->fields = "Age:int:13..120|Nickname:text:?"; f->labelWidth = 110.0f; f->rowHeight = 50.0f; f->fieldHeight = 32.0f;
        f->submitLabel.clear(); f->valuesVar = "signup_"; f->showSummary = false;
        f->Rebuild();
        page->active = false;
    }
    {
        GameObject* page = Group(scene, c4, "DonePage");
        GameObject* t = Text(scene, page, "DoneText", "Welcome, {signup_name|friend}! Press Finish.", UIAnchor::TopLeft, {50, 128}, {580, 30}, 20.0f, slate::Text(), 0, true);
        t->AddComponent<UITextBind>()->format = "Welcome, {signup_name|friend}! Press Finish.";
        GameObject* n = detail2::Note(scene, page, "DoneNote", "We will write to {signup_email|your address}. Back goes to the steps before.", {50, 164}, 580, 20);
        n->AddComponent<UITextBind>()->format = "We will write to {signup_email|your address}. Back goes to the steps before.";
        page->active = false;
    }

    // ---- Card 5: colour picker, advanced ----------------------------------------------------------------------
    GameObject* c5 = gx::Card(scene, root, "ColorCard", "Colour picker, advanced", {752, 426}, {496, 282});
    {
        GameObject* g = Child(scene, c5, "Picker");
        auto* cp = g->AddComponent<UIColorPicker>();
        cp->anchor = UIAnchor::TopLeft; cp->position = {20, 44};
        cp->advanced = true;
        cp->Rebuild();
        detail2::Note(scene, c5, "ColorHint", "Drag in the square, then the two sliders.",
                      {246, 150}, 230, 60);
        onClick(gx::Btn(scene, c5, "ToggleAdvanced", "Advanced on / off", {246, 232}, {230, 30}, 1), "Picker", "ToggleAdvanced");
    }

    // ---- Menu bar (last, so its dropdown is drawn over everything) ------------------------------------------------
    {
        GameObject* g = Child(scene, root, "MenuBar");
        auto* w = g->AddComponent<UIMenuBar>();
        w->anchor = UIAnchor::TopLeft; w->position = {0, 0}; w->size = {1280, 32};
        w->menus = "File:New scene|Open@Ctrl+O|Save@Ctrl+S|-|~Export (soon)|Quit;Edit:Undo@Ctrl+Z|Redo@Ctrl+Y|-|Cut|Copy|Paste;View:Zoom in|Zoom out|-|Full screen;Help:Manual|About";
        w->chosenVar = "menu_pick"; w->accent = slate::Accent();
        w->background = Color::FromBytes(20, 24, 34, 255);
    }
    return root;
}

/// Slice 4: the scale of choice. One page with a theme switcher (click a theme: the whole page restyles, and the choice is kept), a strip of
/// buttons in twelve named styles from the style library, and a sheet of forty of the vector icons. Written from the style, theme and icon
/// registries (okay/Scene/UIStyleLib.hpp, UIIcons.hpp); no other project's looks or artwork.
inline GameObject* BuildSlice4(Scene& scene) {
    EnsureSceneCamera(scene, true);   // a scene without a main camera renders nothing when Play starts (tests/test_examples_work)
    GameObject* canvas = EnsureCanvas(scene);
    if (Canvas* cv = canvas->GetComponent<Canvas>()) cv->referenceResolution = {1280.0f, 720.0f};
    GameObject* root = Group(scene, canvas, "UI Library 4");
    Panel(scene, root, "Backdrop", UIAnchor::StretchFull, {0, 0}, {0, 0}, slate::Backdrop(), 0.0f);
    scene.Find("Backdrop")->GetComponent<UIPanel>()->borderWidth = 0.0f;
    Text(scene, root, "Title", "UI Library 4", UIAnchor::TopLeft, {32, 12}, {600, 40}, 30.0f, slate::Text(), 0, true);
    detail2::Note(scene, root, "Subtitle",
         "Press Play, then click a theme on the left: every part on this page changes look. The buttons in the middle use named styles. "
         "The grid on the right is vector icons, drawn by our own code.", {34, 50}, 1180, 40);

    // Themes: one button each; the action lists a theme by name.
    GameObject* themes = gx::Card(scene, root, "ThemeCard", "Themes (click one)", {24, 100}, {300, 596});
    static const char* kThemes[] = {"Dark", "Light", "Neon", "Retro Pixel", "Candy", "Forest", "Paper", "Cyber Pink", "Wood Plank",
                                    "Space Station", "Terminal Green", "Comic", "Ocean", "Sunset", "Stone Keep", "Mobile Clean"};
    for (int i = 0; i < 16; ++i) {
        GameObject* b = gx::Btn(scene, themes, (std::string("Theme") + std::to_string(i + 1)).c_str(), kThemes[i], {20.0f + (i % 2) * 130.0f, 52.0f + (i / 2) * 44.0f}, {122, 36}, 1);
        UIScreens::detail3::Script(b, std::string("theme:") + kThemes[i], "on_click");
    }
    Text(scene, themes, "ThemeNote", "Script: ui_set_theme(\"Neon\")", UIAnchor::TopLeft, {20, 410}, {260, 20}, 13.0f, slate::Dim());
    detail2::Note(scene, themes, "ThemeNote2", "Your pick is saved (Prefs key ui.theme) and comes back next time. A widget with its own theme name keeps it.", {20, 436}, 260, 70);

    // Styles: twelve buttons, each a different named style, re-applied under every theme.
    GameObject* styles = gx::Card(scene, root, "StyleCard", "Styles (same button, 12 looks)", {340, 100}, {470, 330});
    static const char* kStyles[] = {"Flat Pill", "Neon Pill", "Glass", "Pixel", "Chamfer", "Bevel", "Brutal", "Game Button", "Bubble", "Cartoon", "Frosted", "Hex"};
    for (int i = 0; i < 12; ++i) {
        GameObject* b = gx::Btn(scene, styles, (std::string("Style") + std::to_string(i + 1)).c_str(), kStyles[i], {20.0f + (i % 3) * 148.0f, 56.0f + (i / 3) * 62.0f}, {136, 46}, 0);
        auto* sv = b->GetComponent<UIStyleVariant>();
        if (!sv) sv = b->AddComponent<UIStyleVariant>();
        sv->variant = 0; sv->style = kStyles[i];
        UIStyleLib::ApplyStyle(b, UIStyleLib::SpecOf(kStyles[i]), UIStyleLib::PaletteOf(nullptr));
    }
    detail2::Note(scene, styles, "StyleNote", "A style is a mix of shape, fill, border, effect and four state looks (hover, pressed, disabled, focus). Hover and press the buttons.", {20, 306 - 40}, 430, 50);

    // Sizes and densities of one style.
    GameObject* sizes = gx::Card(scene, root, "SizeCard", "Sizes XS to XL", {340, 444}, {470, 252});
    static const char* kSizes[] = {"xs", "s", "m", "l", "xl"};
    float x = 20;
    for (int i = 0; i < 5; ++i) {
        GameObject* b = gx::Btn(scene, sizes, (std::string("Size") + std::to_string(i + 1)).c_str(), kSizes[i], {x, 70}, {80, 36}, 1);
        auto* sv = b->GetComponent<UIStyleVariant>(); sv->style = "Flat Pill"; sv->styleSize = kSizes[i];
        UIStyleLib::ApplyStyle(b, UIStyleLib::SpecOf("Flat Pill", kSizes[i]), UIStyleLib::PaletteOf(nullptr));
        x += 86.0f;
    }
    detail2::Note(scene, sizes, "SizeNote", "Five sizes (XS to XL) and four densities (compact, cosy, comfortable, spacious) for any style.", {20, 150}, 430, 50);

    // Icons: a sheet of forty.
    GameObject* icons = gx::Card(scene, root, "IconCard", "Icons (371 to choose from)", {826, 100}, {430, 596});
    static const char* kIcons[] = {"heart", "star", "coin", "sword", "shield", "potion", "key", "map", "bag", "chat",
                                   "mail", "lock", "play", "pause", "gear", "home", "user", "flag", "clock", "bell",
                                   "trophy", "gem", "crown", "skull", "bomb", "fire", "lightning", "sun", "moon", "cloud",
                                   "tree", "fish", "car", "rocket", "gamepad", "camera", "music", "globe", "search", "check"};
    for (int i = 0; i < 40; ++i) {
        UIScreens::detail3::AddIconPicture(scene, icons, (std::string("Icon") + std::to_string(i + 1)).c_str(), kIcons[i], UIAnchor::TopLeft,
                                           {24.0f + (i % 8) * 49.0f, 56.0f + (i / 8) * 49.0f}, 36.0f, slate::Text());
    }
    detail2::Note(scene, icons, "IconNote", "Use one on any Image (texture icon:heart) or Button (icon icon:play). Pick from all 371 in GameObject > UI > Browse All UI...", {20, 470}, 390, 60);
    return root;
}

/// The Starter Pack as an example scene: a main menu, settings, game HUD, pause menu and game over screen already joined together.
inline GameObject* BuildStarterExample(Scene& scene) {
    EnsureSceneCamera(scene, true);
    GameObject* canvas = EnsureCanvas(scene);
    if (Canvas* cv = canvas->GetComponent<Canvas>()) cv->referenceResolution = {1280.0f, 720.0f};
    return UIScreens::BuildStarterPack(scene, "Midnight Blue");
}

}} // namespace okay::UILibrary
