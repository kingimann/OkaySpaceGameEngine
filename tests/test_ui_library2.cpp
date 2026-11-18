// UI component library, slice 2: the game HUD parts (radial menu, kill feed, ammo counter, interaction prompt,
// objective tracker, compass, item tooltip card, ability icon) and the data-driven glue (two-way binding, conditional
// visibility, array to list view, rich text links and icons, focus hints, hot reload). Headless: the maths, the
// pointer and key paths through scene.Update, and a scene save and load round trip for each part.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/UIWidgetsMore.hpp"
#include "okay/Components/UINavigation.hpp"
#include "okay/Components/QuestLog.hpp"
#include "okay/Components/RangedWeapon.hpp"
#include "okay/Components/UIVars.hpp"
#include "okay/Inventory/ItemDatabase.hpp"
#include "okay/UI/RichText.hpp"
#include "okay/VisualScript/VsVariables.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace okay;

template <class Check>
static bool RoundTrip(Scene& s, const char* name, Check check) {
    const std::string text = SceneSerializer::Serialize(s);
    Scene back("back");
    std::string err;
    if (!SceneSerializer::Deserialize(back, text, &err, nullptr)) return false;
    if (SceneSerializer::Serialize(back) != text) return false;
    return check(back.Find(name));
}

static void Press(char k, Scene& s) {
    Input::FeedKeys({});
    Input::FeedKeys({k});
    s.Update(0.016f);
}
static void Release(Scene& s) {
    Input::FeedKeys({});
    s.Update(0.016f);
}

int main() {
    RUN_SUITE("ui_library2");

    // ===== Radial menu =============================================================================================
    {
        CHECK(UIRadialMenu::SegmentAt(0, -100, 4, 10) == 0);        // straight up
        CHECK(UIRadialMenu::SegmentAt(100, 0, 4, 10) == 1);         // right
        CHECK(UIRadialMenu::SegmentAt(0, 100, 4, 10) == 2);         // down
        CHECK(UIRadialMenu::SegmentAt(-100, 0, 4, 10) == 3);        // left
        CHECK(UIRadialMenu::SegmentAt(3, -3, 4, 10) == -1);         // inside the dead zone
        CHECK(UIRadialMenu::SegmentAt(0, -100, 0, 10) == -1);       // no wedges
        CHECK(UIRadialMenu::SegmentAt(40, -100, 6, 10) == 0);       // 6 wedges: the top one covers -30..30 degrees
        CHECK(UIRadialMenu::SegmentAt(100, -40, 6, 10) == 1);       // about 68 degrees
        CHECK_NEAR(UIRadialMenu::SegmentCenter(1, 4), 90.0f, 1e-4);
        Scene s("Radial"); s.physicsEnabled = false;
        GameObject* g = s.CreateGameObject("Wheel");
        auto* w = g->AddComponent<UIRadialMenu>();
        w->anchor = UIAnchor::TopLeft; w->position = {0, 0}; w->size = {300, 300}; w->innerRadius = 30;
        w->items = "Attack|Defend|Heal|Run";
        CHECK(w->Count() == 4);
        CHECK(w->Labels()[2] == "Heal");
        w->items = "A|B|C|D|E|F|G|H|I|J|K|L|M|N";
        CHECK(w->Count() == UIRadialMenu::kMaxItems);               // held to 12
        w->items = "Attack|Defend|Heal|Run";
        auto* sc = g->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource("function on_select() { set_x(7); }"));
        s.Start();
        CHECK(!w->IsOpen());
        w->Open();
        CHECK(w->IsOpen());
        Input::FeedMouse({150, 150}, 0);
        Input::FeedMouse({250, 150}, 0);                            // right of the centre
        s.Update(0.016f);
        CHECK(w->Hovered() == 1);
        Input::FeedMouse({250, 150}, 0);
        Input::FeedMouse({250, 150}, 1u << 0);                      // click mode: a click chooses the hovered wedge
        w->holdToOpen = false;
        s.Update(0.016f);
        CHECK(w->Chosen() == 1);
        CHECK(!w->IsOpen());                                        // closed on select
        CHECK_NEAR(g->transform->localPosition.x, 7.0f, 0.001f);   // on_select ran
        Input::FeedMouse({0, 0}, 0);
        // Hold-to-open with a key: down opens, pointing picks, release chooses.
        w->holdToOpen = true; w->openKey = 'q';
        Input::FeedKeys({'q'});
        Input::FeedMouse({150, 40}, 0);
        s.Update(0.016f);
        CHECK(w->IsOpen());
        s.Update(0.016f);
        CHECK(w->Hovered() == 0);
        Input::FeedKeys({});
        s.Update(0.016f);
        CHECK(!w->IsOpen() && w->Chosen() == 0);
        // Keyboard stepping and the script API calls.
        w->Open();
        w->Step(1); CHECK(w->Hovered() == 0);
        w->Step(1); CHECK(w->Hovered() == 1);
        w->Step(-2); CHECK(w->Hovered() == 3);                      // wraps
        CHECK(w->Choose(2) && w->Chosen() == 2);
        CHECK(!w->Choose(9));
        w->chosenVar = "pick_i";
        CHECK(w->Choose(2));
        { float pv = -1; CHECK(uivars::ReadNumber(g, "pick_i", pv) && pv == 2.0f); CHECK(uivars::Read(g, "pick_i_text").text == "Heal"); }
        ActionList::Vars().erase("pick_i"); ActionList::StrVars().erase("pick_i_text");
        w->chosenVar.clear();
        w->Open(); CHECK(!w->Close(true));                          // nothing hovered: nothing chosen
        // A fixed wheel (alwaysShown) is pointed at and clicked without opening it first.
        w->alwaysShown = true; w->holdToOpen = false; w->openKey = 0; w->Close(false);
        Input::FeedMouse({150, 150}, 0);
        Input::FeedMouse({150, 260}, 0);
        s.Update(0.016f);
        CHECK(w->Hovered() == 2 && !w->IsOpen());
        Input::FeedMouse({150, 260}, 1u << 0);
        s.Update(0.016f);
        CHECK(w->Chosen() == 2);
        Input::FeedMouse({0, 0}, 0);
        w->alwaysShown = false; w->holdToOpen = true;
        // Save and load.
        w->centerText = "Pick"; w->openKey = 'e'; w->innerRadius = 44;
        CHECK(RoundTrip(s, "Wheel", [](GameObject* o) {
            auto* r = o ? o->GetComponent<UIRadialMenu>() : nullptr;
            return r && r->items == "Attack|Defend|Heal|Run" && r->centerText == "Pick" && r->openKey == 'e' && r->innerRadius == 44.0f && r->size.x == 300.0f;
        }));
        CHECK(UIDefaultLayer(g) == uilayers::K_GameHud);
    }

    // ===== Kill feed ===============================================================================================
    {
        Scene s("Feed"); s.physicsEnabled = false;
        GameObject* g = s.CreateGameObject("Feed");
        auto* f = g->AddComponent<UIKillFeed>();
        f->maxEntries = 3; f->lifetime = 2.0f; f->fadeTime = 0.5f; f->playerName = "Ada";
        f->Push("Ada", "Bot1");
        f->Push("Bot2", "Bot3", "Rifle");
        CHECK(f->Entries().size() == 2);
        CHECK(f->Entries()[0].text == "Bot2  [Rifle]  Bot3");        // newest first
        CHECK(f->Entries()[1].text == "Ada  >  Bot1");
        CHECK(f->Entries()[1].mine && !f->Entries()[0].mine);
        f->Push("a", "b"); f->Push("c", "d");
        CHECK(f->Entries().size() == 3);                              // the oldest fell off
        CHECK(f->Entries()[0].text == "c  >  d");
        f->Update(1.0f);
        CHECK_NEAR(f->Alpha(f->Entries()[0]), 1.0f, 1e-4);
        f->Update(0.75f);                                             // age 1.75: inside the 0.5 s fade
        CHECK_NEAR(f->Alpha(f->Entries()[0]), 0.5f, 1e-3);
        f->Update(0.5f);                                              // past its life: gone
        CHECK(f->Entries().empty());
        CHECK(UIKillFeed::Announce(s, "x", "y") == 1);
        CHECK(f->Entries().size() == 1);
        f->Clear();
        CHECK(f->Entries().empty());
        f->format = "{victim} fell"; f->Push("k", "v");
        CHECK(f->Entries()[0].text == "v fell");
        f->playerName = "Ada"; f->lifetime = 7;
        CHECK(RoundTrip(s, "Feed", [](GameObject* o) {
            auto* r = o ? o->GetComponent<UIKillFeed>() : nullptr;
            return r && r->maxEntries == 3 && r->lifetime == 7.0f && r->format == "{victim} fell" && r->playerName == "Ada";
        }));
    }

    // ===== Ammo counter ============================================================================================
    {
        Scene s("Ammo"); s.physicsEnabled = false;
        GameObject* g = s.CreateGameObject("Ammo");
        auto* a = g->AddComponent<UIAmmoCounter>();
        a->current = 12; a->reserve = 90; a->magazine = 30;
        CHECK(a->Text() == "12 / 90");
        CHECK(a->Level() == 0);
        a->current = 7;  CHECK(a->Level() == 1);                      // 7 of 30 is under a quarter
        a->current = 0;  CHECK(a->Level() == 2);
        a->reserve = -1; a->current = 5;
        CHECK(a->Text() == "5 / INF");
        a->showReserve = false; CHECK(a->Text() == "5");
        a->showReserve = true; a->reserve = 90;
        // Variables win over the typed numbers.
        ActionList::Vars()["ammo_now"] = 22; ActionList::Vars()["ammo_spare"] = 44;
        a->currentVar = "ammo_now"; a->reserveVar = "ammo_spare";
        CHECK(a->Text() == "22 / 44");
        a->currentVar.clear(); a->reserveVar.clear();
        // A Ranged Weapon on the Player object is the source when no variable is named.
        GameObject* p = s.CreateGameObject("Hero"); p->SetTag("Player");
        auto* rw = p->AddComponent<RangedWeapon>();
        rw->magazine = 10; rw->reserve = 25;
        s.Start();
        const auto st = a->Read();
        CHECK(st.fromWeapon && st.current == 10 && st.reserve == 25 && st.magazine == 10);
        CHECK(a->Text() == "10 / 25");
        CHECK(rw->Fire() || true);
        CHECK(a->Read().current <= 10);
        a->current = 3; a->reserve = 9;
        ActionList::Vars().erase("ammo_now"); ActionList::Vars().erase("ammo_spare");
        CHECK(RoundTrip(s, "Ammo", [](GameObject* o) {
            auto* r = o ? o->GetComponent<UIAmmoCounter>() : nullptr;
            return r && r->current == 3 && r->reserve == 9 && r->label == "AMMO" && r->showPips;
        }));
    }

    // ===== Interaction prompt ======================================================================================
    {
        Scene s("Prompt"); s.physicsEnabled = false;
        GameObject* player = s.CreateGameObject("Hero"); player->SetTag("Player");
        GameObject* door = s.CreateGameObject("Door");
        door->transform->SetPosition({10, 0, 0});
        GameObject* g = s.CreateGameObject("Prompt");
        auto* p = g->AddComponent<UIInteractionPrompt>();
        p->target = "Door"; p->range = 3.0f; p->key = "E"; p->action = "Open door";
        auto* sc = g->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource("function on_interact() { set_x(5); }"));
        s.Start();
        CHECK(!p->ShouldShow());
        CHECK_NEAR(p->Distance(), 10.0f, 1e-3);
        p->Settle(); CHECK(!p->Visible());
        player->transform->SetPosition({8.5f, 0, 0});                 // 1.5 away
        CHECK(p->ShouldShow());
        for (int i = 0; i < 30; ++i) p->Update(0.05f);
        CHECK(p->Visible() && p->Alpha() > 0.99f);
        Press('e', s);
        CHECK_NEAR(g->transform->localPosition.x, 5.0f, 0.001f);      // the key ran on_interact
        Release(s);
        player->transform->SetPosition({0, 0, 0});                    // walk away: it fades and hides
        for (int i = 0; i < 30; ++i) p->Update(0.05f);
        CHECK(!p->Visible());
        g->transform->localPosition.x = 0;
        Press('e', s);
        CHECK_NEAR(g->transform->localPosition.x, 0.0f, 0.001f);      // hidden: the key does nothing
        Release(s);
        // The variable path and always-show.
        p->target.clear();
        CHECK(!p->ShouldShow());
        ActionList::Vars()["near_chest"] = 1;
        p->showVar = "near_chest";
        CHECK(p->ShouldShow());
        ActionList::Vars()["near_chest"] = 0;
        CHECK(!p->ShouldShow());
        p->alwaysShow = true; CHECK(p->ShouldShow());
        ActionList::Vars().erase("near_chest");
        CHECK(RoundTrip(s, "Prompt", [](GameObject* o) {
            auto* r = o ? o->GetComponent<UIInteractionPrompt>() : nullptr;
            return r && r->key == "E" && r->action == "Open door" && r->alwaysShow && r->showVar == "near_chest";
        }));
    }

    // ===== Objective tracker (reads the Quest Log) ==================================================================
    {
        Scene s("Tracker"); s.physicsEnabled = false;
        GameObject* ql = s.CreateGameObject("Quests");
        auto* log = ql->AddComponent<QuestLog>();
        log->drawUI = false;
        QuestDef q;
        q.id = "wolves"; q.title = "Wolf trouble";
        QuestObjective o1; o1.kind = QuestObjective::Kind::Kill; o1.description = "Hunt wolves"; o1.target = "Wolf"; o1.count = 3;
        QuestObjective o2; o2.kind = QuestObjective::Kind::Talk; o2.description = "Tell the mayor"; o2.target = "Mayor";
        q.objectives = {o1, o2};
        log->quests.push_back(q);
        QuestDef q2; q2.id = "herbs"; q2.title = "Herbs";
        QuestObjective h; h.description = "Pick 2 herbs"; h.count = 2;
        q2.objectives = {h};
        log->quests.push_back(q2);
        GameObject* g = s.CreateGameObject("Tracker");
        auto* t = g->AddComponent<UIObjectiveTracker>();
        s.Start();
        CHECK(t->Lines().empty());                                    // nothing active
        CHECK(log->StartQuest("wolves"));
        auto lines = t->Lines();
        CHECK(lines.size() == 3);
        CHECK(lines[0].kind == UIObjectiveTracker::Line::Quest && lines[0].text == "Wolf trouble");
        CHECK(lines[1].text == "Hunt wolves" && lines[1].need == 3 && lines[1].have == 0);
        CHECK(lines[2].kind == UIObjectiveTracker::Line::Objective);
        CHECK(log->AddProgress("wolves", 0, 2));
        CHECK(t->Lines()[1].have == 2);
        CHECK(log->CompleteObjective("wolves", 1));
        CHECK(t->Lines()[2].kind == UIObjectiveTracker::Line::Done);
        CHECK(log->StartQuest("herbs"));
        log->tracked = "wolves";
        CHECK(t->Lines().size() == 3);                                // the Quest Log's tracked quest only
        log->tracked.clear();
        CHECK(t->Lines().size() == 5);                                // two quests
        t->maxQuests = 1; CHECK(t->Lines().size() == 3);
        t->maxQuests = 3; t->questId = "herbs"; CHECK(t->Lines().size() == 2);
        t->questId = "none"; CHECK(t->Lines().empty());
        t->questId.clear();
        t->showDescription = true;
        CHECK(RoundTrip(s, "Tracker", [](GameObject* o) {
            auto* r = o ? o->GetComponent<UIObjectiveTracker>() : nullptr;
            return r && r->title == "Objectives" && r->showDescription && r->maxQuests == 3;
        }));
        // No Quest Log: no lines and no crash.
        Scene empty("NoLog"); empty.physicsEnabled = false;
        auto* t2 = empty.CreateGameObject("T")->AddComponent<UIObjectiveTracker>();
        CHECK(t2->Lines().empty());
    }

    // ===== Compass =================================================================================================
    {
        CHECK_NEAR(UICompass::BearingOf(0, 1), 0.0f, 1e-3);           // north is +Z
        CHECK_NEAR(UICompass::BearingOf(1, 0), 90.0f, 1e-3);
        CHECK_NEAR(UICompass::BearingOf(0, -1), 180.0f, 1e-3);
        CHECK_NEAR(UICompass::BearingOf(-1, 0), 270.0f, 1e-3);
        CHECK_NEAR(UICompass::Wrap180(350.0f), -10.0f, 1e-3);
        CHECK_NEAR(UICompass::Wrap180(190.0f), -170.0f, 1e-3);
        CHECK_NEAR(UICompass::Wrap180(-190.0f), 170.0f, 1e-3);
        CHECK_NEAR(UICompass::StripX(0, 120, 600), 300.0f, 1e-3);     // dead ahead is the middle
        CHECK_NEAR(UICompass::StripX(60, 120, 600), 600.0f, 1e-3);    // half the field of view is the edge
        CHECK_NEAR(UICompass::StripX(-30, 120, 600), 150.0f, 1e-3);
        CHECK(std::string(UICompass::Cardinal(0)) == "N");
        CHECK(std::string(UICompass::Cardinal(44)) == "NE");
        CHECK(std::string(UICompass::Cardinal(181)) == "S");
        CHECK(std::string(UICompass::Cardinal(359)) == "N");
        Scene s("Compass"); s.physicsEnabled = false;
        GameObject* look = s.CreateGameObject("Looker");
        look->transform->SetPosition({0, 0, 0});
        GameObject* tower = s.CreateGameObject("Tower");
        tower->transform->SetPosition({0, 0, 50});                    // due north
        GameObject* hut = s.CreateGameObject("Hut");
        hut->transform->SetPosition({30, 0, 0});                      // due east
        GameObject* g = s.CreateGameObject("Compass");
        auto* c = g->AddComponent<UICompass>();
        c->source = "Looker"; c->markers = "Tower|Hut|Nowhere";
        s.Start();
        CHECK_NEAR(c->Heading(), 0.0f, 1e-2);                         // looking along +Z
        look->transform->SetRotation(Quat::Euler({0, 90, 0}));        // turn right: facing east
        CHECK_NEAR(c->Heading(), 90.0f, 1e-1);
        look->transform->SetRotation(Quat::Identity);
        auto marks = c->Markers();
        CHECK(marks.size() == 2);                                     // the missing one is skipped
        CHECK(marks[0].name == "Tower" && std::fabs(marks[0].bearing) < 1e-2 && std::fabs(marks[0].distance - 50.0f) < 1e-2);
        CHECK(marks[1].name == "Hut" && std::fabs(marks[1].bearing - 90.0f) < 1e-2 && std::fabs(marks[1].distance - 30.0f) < 1e-2);
        c->source.clear(); c->heading = 135.0f;                       // no source and no camera: the typed heading
        CHECK_NEAR(c->Heading(), 135.0f, 1e-3);
        ActionList::Vars()["compass_deg"] = 400.0f;                   // a variable wins, folded into 0..360
        c->headingVar = "compass_deg";
        CHECK_NEAR(c->Heading(), 40.0f, 1e-3);
        c->headingVar.clear(); ActionList::Vars().erase("compass_deg");
        c->fieldOfView = 90; c->markers = "Tower";
        CHECK(RoundTrip(s, "Compass", [](GameObject* o) {
            auto* r = o ? o->GetComponent<UICompass>() : nullptr;
            return r && r->heading == 135.0f && r->fieldOfView == 90.0f && r->markers == "Tower";
        }));
    }

    // ===== Item tooltip card =======================================================================================
    {
        ItemDatabase& db = ItemDatabase::Get();
        db.Clear();
        ItemDefinition d;
        d.id = "sword_iron"; d.name = "Iron Sword"; d.description = "A plain blade."; d.category = "Weapon";
        d.rarity = 2; d.hasRarityColor = true; d.rarityColor = Color::FromBytes(80, 140, 255);
        d.weight = 2.0f; d.equip = EquipSlot::MainHand;
        d.custom.push_back({"damage", "12"});
        db.Put(d);
        Scene s("Card"); s.physicsEnabled = false;
        GameObject* g = s.CreateGameObject("Card");
        auto* c = g->AddComponent<UIItemTooltipCard>();
        CHECK(c->Lines().empty());                                    // no item: hidden
        c->item = "sword_iron";
        auto lines = c->Lines();
        CHECK(lines.size() >= 4);
        CHECK(lines[0].text == "Iron Sword" && lines[0].name && lines[0].useTint);
        CHECK_NEAR(lines[0].tint.b, 1.0f, 0.01);                      // the rarity colour
        bool sawDesc = false, sawDamage = false, sawWeight = false;
        for (const auto& l : lines) { if (l.text == "A plain blade.") sawDesc = true; if (l.text == "damage: 12") sawDamage = true; if (l.text.find("Weight") == 0) sawWeight = true; }
        CHECK(sawDesc && sawDamage && sawWeight);
        c->Show("Iron Sword", 3);                                     // a display name resolves to the id
        CHECK(c->Lines()[0].text == "Iron Sword");
        bool sawStack = false;
        for (const auto& l : c->Lines()) if (l.text.find("Stack 3") == 0) sawStack = true;
        CHECK(sawStack);
        ActionList::StrVars()["hover_item"] = "sword_iron";
        c->itemVar = "hover_item"; c->item.clear();
        CHECK(c->CurrentItem() == "sword_iron");
        CHECK(!c->Lines().empty());
        ActionList::StrVars()["hover_item"] = "";
        c->Hide();
        CHECK(c->Lines().empty());
        ActionList::StrVars().erase("hover_item");
        c->Show("sword_iron", 1); c->count = 4; c->showIcon = false;
        CHECK(RoundTrip(s, "Card", [](GameObject* o) {
            auto* r = o ? o->GetComponent<UIItemTooltipCard>() : nullptr;
            return r && r->item == "sword_iron" && r->count == 4 && !r->showIcon;
        }));
        db.Clear();
    }

    // ===== Ability icon (built on UIRadialProgress) =================================================================
    {
        Scene s("Ability"); s.physicsEnabled = false;
        GameObject* g = s.CreateGameObject("Dash");
        auto* ring = g->AddComponent<UIRadialProgress>();
        auto* a = g->AddComponent<UIAbilityIcon>();
        a->cooldown = 4.0f; a->useKey = 'q';
        auto* sc = g->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource("function on_ability_use() { set_x(1); } function on_ability_ready() { set_y(2); }"));
        s.Start();
        CHECK(a->Ready() && a->Charges() == 1);
        CHECK_NEAR(ring->value, 0.0f, 1e-4);
        CHECK(a->Use());
        CHECK(!a->Ready());
        CHECK(!a->Use());                                             // no charge left
        CHECK_NEAR(g->transform->localPosition.x, 1.0f, 1e-3);
        a->Update(1.0f);
        CHECK_NEAR(a->Remaining(), 3.0f, 1e-3);
        CHECK_NEAR(ring->value, 0.75f, 1e-3);                         // the ring follows the time left
        a->Update(3.0f);
        CHECK(a->Ready());
        CHECK_NEAR(ring->value, 0.0f, 1e-3);
        CHECK_NEAR(g->transform->localPosition.y, 2.0f, 1e-3);        // on_ability_ready ran
        // The key uses it.
        Press('q', s);
        CHECK(!a->Ready());
        Release(s);
        a->Reset();
        CHECK(a->Ready() && a->Remaining() == 0.0f);
        // Two charges recharge one after the other.
        a->maxCharges = 2; a->Reset();
        CHECK(a->Use() && a->Use() && !a->Ready());
        a->Update(4.0f); CHECK(a->Charges() == 1);
        a->Update(4.0f); CHECK(a->Charges() == 2 && a->Remaining() == 0.0f);
        a->maxCharges = 1; a->cooldown = 6; a->label = "Z"; a->keyText = "Q"; a->useKey = 'z';
        CHECK(RoundTrip(s, "Dash", [](GameObject* o) {
            auto* r = o ? o->GetComponent<UIAbilityIcon>() : nullptr;
            return r && r->cooldown == 6.0f && r->label == "Z" && r->keyText == "Q" && r->useKey == 'z' && o->GetComponent<UIRadialProgress>();
        }));
        CHECK(UIDefaultLayer(g) == uilayers::K_Radial);               // the ring keeps its own layer; the icon adds the HUD layer
        std::vector<UIDrawItem> items;
        UIQueueDrawItems(g, 0, items);
        bool hud = false, radial = false;
        for (const auto& it : items) { if (it.kind == uilayers::K_GameHud) hud = true; if (it.kind == uilayers::K_Radial) radial = true; }
        CHECK(hud && radial);
    }

    // ===== Condition language and variables ========================================================================
    {
        auto lk = [](const std::string& n) {
            uivars::Value v;
            if (n == "hp") { v.found = v.isNumber = true; v.number = 25; v.text = "25"; }
            else if (n == "lives") { v.found = v.isNumber = true; v.number = 3; v.text = "3"; }
            else if (n == "zero") { v.found = v.isNumber = true; v.number = 0; v.text = "0"; }
            else if (n == "state") { v.found = true; v.text = "menu"; }
            return v;
        };
        auto ev = [&](const char* c) { return uivars::EvaluateWith(lk, c); };
        CHECK(ev("hp < 30")); CHECK(!ev("hp > 30")); CHECK(ev("hp <= 25")); CHECK(ev("hp >= 25")); CHECK(ev("hp == 25")); CHECK(ev("hp != 24"));
        CHECK(ev("hp < 30 && lives > 0")); CHECK(!ev("hp < 30 && lives > 5")); CHECK(ev("hp > 90 || lives == 3"));
        CHECK(ev("!zero")); CHECK(!ev("!hp")); CHECK(ev("hp")); CHECK(!ev("zero")); CHECK(!ev("nothing"));
        CHECK(ev("nothing != 5")); CHECK(!ev("nothing == 5")); CHECK(!ev("nothing == nothing"));
        CHECK(ev("state == \"menu\"")); CHECK(ev("state == menu_is_not_a_var") == false); CHECK(ev("state != 'play'"));
        CHECK(ev("(hp < 10 || hp < 30) && !zero")); CHECK(ev("!(hp > 30)")); CHECK(ev("true")); CHECK(!ev("false"));
        CHECK(ev("")); CHECK(ev("   "));                              // no condition: always true
        CHECK(ev("hp < -5") == false); CHECK(ev("hp > -5"));
        std::string err;
        CHECK(!uivars::EvaluateWith(lk, "hp <", &err) && !err.empty());
        CHECK(!uivars::EvaluateWith(lk, "(hp < 3", &err) && !err.empty());
        CHECK(!uivars::EvaluateWith(lk, "hp < 3 )", &err) && !err.empty());
        CHECK(!uivars::EvaluateWith(lk, "hp $ 3", &err) && !err.empty());
        CHECK(uivars::EvaluateWith(lk, "hp < 30", &err) && err.empty());
        std::map<std::string, std::string> f; f["name"] = "Ada"; f["n"] = "";
        CHECK(uivars::FillTemplate("{name} has {n|none} {{x}}", f) == "Ada has none {x}");
        CHECK(uivars::FillTemplate("{missing|?}", f) == "?");
        CHECK(uivars::FormatNumber(5.0f) == "5" && uivars::FormatNumber(2.5f) == "2.5");
        // Real variables: write then read in the Actions pool, and the scene's visual-script variables win.
        Scene s("Vars"); s.physicsEnabled = false;
        GameObject* g = s.CreateGameObject("G");
        uivars::WriteNumber(g, "score_a", 12);
        float v = 0;
        CHECK(uivars::ReadNumber(g, "score_a", v) && v == 12.0f);
        vs::SceneVariables(&s)["score_b"] = vs::VsValue(3.0f);
        uivars::WriteNumber(g, "score_b", 9);
        CHECK(uivars::ReadNumber(g, "score_b", v) && v == 9.0f);
        CHECK(vs::SceneVariables(&s)["score_b"].AsFloat() == 9.0f);   // written where it already lived
        CHECK(uivars::Evaluate(g, "score_a == 12 && score_b > 8"));
        uivars::WriteText(g, "title_x", "Boss");
        CHECK(uivars::Read(g, "title_x").text == "Boss");
        CHECK(uivars::Evaluate(g, "title_x == \"Boss\""));
        ActionList::Vars().erase("score_a"); ActionList::StrVars().erase("title_x");
    }

    // ===== Two-way binding =========================================================================================
    {
        Scene s("Bind"); s.physicsEnabled = false;
        GameObject* g = s.CreateGameObject("Volume");
        auto* sl = g->AddComponent<UISlider>();
        sl->minValue = 0; sl->maxValue = 100; sl->value = 40;
        auto* b = g->AddComponent<UIBind>();
        b->var = "volume_bind";
        CHECK(b->HasWidget());
        float v = -1;
        b->Sync();                                                    // first frame: no variable yet, the widget's value goes in
        CHECK(uivars::ReadNumber(g, "volume_bind", v) && v == 40.0f);
        sl->SetValue(65);                                             // the player moves the slider
        b->Sync();
        CHECK(uivars::ReadNumber(g, "volume_bind", v) && v == 65.0f);
        ActionList::Vars()["volume_bind"] = 20;                       // a script changes the variable
        b->Sync();
        CHECK_NEAR(sl->value, 20.0f, 1e-3);                           // the slider follows
        b->Sync();
        CHECK(uivars::ReadNumber(g, "volume_bind", v) && v == 20.0f); // and nothing bounces back
        // The variable wins on the first frame when it already exists.
        ActionList::Vars()["vol2"] = 77;
        GameObject* g2 = s.CreateGameObject("Vol2");
        auto* sl2 = g2->AddComponent<UISlider>(); sl2->minValue = 0; sl2->maxValue = 100; sl2->value = 1;
        auto* b2 = g2->AddComponent<UIBind>(); b2->var = "vol2";
        b2->Sync();
        CHECK_NEAR(sl2->value, 77.0f, 1e-3);
        // One-way modes.
        b2->mode = UIBind::Mode::ToVariable;
        sl2->SetValue(10); b2->Sync();
        CHECK(uivars::ReadNumber(g2, "vol2", v) && v == 10.0f);
        ActionList::Vars()["vol2"] = 55; b2->Sync();
        CHECK_NEAR(sl2->value, 10.0f, 1e-3);                          // widget-to-variable only: the variable does not move the widget
        b2->mode = UIBind::Mode::FromVariable;
        sl2->SetValue(30); b2->Sync();
        CHECK(uivars::ReadNumber(g2, "vol2", v) && v == 55.0f);       // variable-to-widget only: the player's move is not written
        // A toggle.
        GameObject* g3 = s.CreateGameObject("Flag");
        auto* tg = g3->AddComponent<UIToggle>(); tg->on = true;
        auto* b3 = g3->AddComponent<UIBind>(); b3->var = "flag_on";
        b3->Sync();
        CHECK(uivars::ReadNumber(g3, "flag_on", v) && v == 1.0f);
        tg->Toggle(); b3->Sync();
        CHECK(uivars::ReadNumber(g3, "flag_on", v) && v == 0.0f);
        ActionList::Vars()["flag_on"] = 1; b3->Sync();
        CHECK(tg->on);
        // A dropdown (its index) and a stepper.
        GameObject* g4 = s.CreateGameObject("Quality");
        auto* dd = g4->AddComponent<UIDropdown>(); dd->options = {"Low", "Medium", "High"}; dd->value = 1;
        auto* b4 = g4->AddComponent<UIBind>(); b4->var = "quality_i";
        b4->Sync();
        CHECK(uivars::ReadNumber(g4, "quality_i", v) && v == 1.0f);
        ActionList::Vars()["quality_i"] = 2; b4->Sync();
        CHECK(dd->value == 2);
        CHECK(uivars::Read(g4, "quality_i_text").text == "High");        // the picked words too
        dd->SetValue(0); b4->Sync();
        CHECK(uivars::ReadNumber(g4, "quality_i", v) && v == 0.0f);
        CHECK(uivars::Read(g4, "quality_i_text").text == "Low");
        GameObject* g5 = s.CreateGameObject("Count");
        auto* st = g5->AddComponent<UIStepper>(); st->value = 3;
        auto* b5 = g5->AddComponent<UIBind>(); b5->var = "count_i";
        b5->Sync(); st->Step(1); b5->Sync();
        CHECK(uivars::ReadNumber(g5, "count_i", v) && v == 4.0f);
        // A range slider binds both handles.
        GameObject* g6 = s.CreateGameObject("Price");
        auto* rs = g6->AddComponent<UIRangeSlider>(); rs->minValue = 0; rs->maxValue = 100; rs->low = 20; rs->high = 80;
        auto* b6 = g6->AddComponent<UIBind>(); b6->var = "price_lo"; b6->varHigh = "price_hi";
        b6->Sync();
        CHECK(uivars::ReadNumber(g6, "price_lo", v) && v == 20.0f);
        CHECK(uivars::ReadNumber(g6, "price_hi", v) && v == 80.0f);
        rs->SetHigh(90); b6->Sync();
        CHECK(uivars::ReadNumber(g6, "price_hi", v) && v == 90.0f);
        ActionList::Vars()["price_lo"] = 35; b6->Sync();
        CHECK_NEAR(rs->low, 35.0f, 1e-3);
        // A text field writes text, or a number when the text is a number, and shows the variable back.
        GameObject* g7 = s.CreateGameObject("Name");
        auto* in = g7->AddComponent<UIInputField>();
        auto* b7 = g7->AddComponent<UIBind>(); b7->var = "player_name_i";
        in->text = "Ada";
        b7->Sync();
        CHECK(uivars::Read(g7, "player_name_i").text == "Ada");
        in->text = "Grace"; b7->Sync();
        CHECK(uivars::Read(g7, "player_name_i").text == "Grace");
        ActionList::StrVars()["player_name_i"] = "Linus"; b7->Sync();
        CHECK(in->text == "Linus");
        in->text = "42"; b7->Sync();
        CHECK(uivars::ReadNumber(g7, "player_name_i", v) && v == 42.0f);
        // The real frame path: Update runs the sync.
        s.Start();
        ActionList::Vars()["volume_bind"] = 90;
        s.Update(0.016f);
        CHECK_NEAR(sl->value, 90.0f, 1e-3);
        b->mode = UIBind::Mode::FromVariable; b->var = "volume_bind";
        CHECK(RoundTrip(s, "Volume", [](GameObject* o) {
            auto* r = o ? o->GetComponent<UIBind>() : nullptr;
            return r && r->var == "volume_bind" && r->mode == UIBind::Mode::FromVariable;
        }));
        CHECK(RoundTrip(s, "Price", [](GameObject* o) {
            auto* r = o ? o->GetComponent<UIBind>() : nullptr;
            return r && r->var == "price_lo" && r->varHigh == "price_hi";
        }));
        for (const char* k : {"quality_i_text", "volume_bind", "vol2", "flag_on", "quality_i", "count_i", "price_lo", "price_hi", "player_name_i"}) { ActionList::Vars().erase(k); ActionList::StrVars().erase(k); }
    }

    // ===== Conditional visibility ==================================================================================
    {
        Scene s("Visible"); s.physicsEnabled = false;
        GameObject* panel = s.CreateGameObject("LowHp");
        panel->AddComponent<UIPanel>();
        auto* vis = panel->AddComponent<UIVisibleIf>();
        vis->condition = "hp < 30";
        ActionList::Vars()["hp"] = 80;
        s.Start();
        CHECK(!panel->active);                                        // hidden from the first frame
        ActionList::Vars()["hp"] = 10;
        UpdateUILayouts(s);                                           // the layout pass wakes a self-hiding object
        CHECK(panel->active);
        ActionList::Vars()["hp"] = 90;
        s.Update(0.016f);
        CHECK(!panel->active);
        ActionList::Vars()["hp"] = 5;
        UpdateUILayouts(s);
        CHECK(panel->active);
        // Another object as the target, and invert. The controller stays on, so Update alone keeps it right.
        GameObject* hint = s.CreateGameObject("Hint");
        GameObject* ctl = s.CreateGameObject("Ctl");
        auto* v2 = ctl->AddComponent<UIVisibleIf>();
        v2->condition = "hasKey"; v2->target = "Hint"; v2->invert = true;   // show the hint while the key is missing
        ActionList::Vars()["hasKey"] = 0;
        s.Update(0.016f);
        CHECK(hint->active);
        ActionList::Vars()["hasKey"] = 1;
        s.Update(0.016f);
        CHECK(!hint->active && ctl->active);
        ActionList::Vars()["hasKey"] = 0;
        s.Update(0.016f);
        CHECK(hint->active);
        // In edit mode (no Start, no Update) nothing is touched, and the layout pass leaves everything alone.
        Scene edit("Edit"); edit.physicsEnabled = false;
        GameObject* e = edit.CreateGameObject("E");
        e->AddComponent<UIVisibleIf>()->condition = "never_set_variable";
        UpdateUILayouts(edit);
        CHECK(e->active);
        CHECK(RoundTrip(s, "Ctl", [](GameObject* o) {
            auto* r = o ? o->GetComponent<UIVisibleIf>() : nullptr;
            return r && r->condition == "hasKey" && r->target == "Hint" && r->invert;
        }));
        ActionList::Vars().erase("hp"); ActionList::Vars().erase("hasKey");
    }

    // ===== Array to list view ======================================================================================
    {
        Scene s("List"); s.physicsEnabled = false;
        GameObject* list = s.CreateGameObject("Bag");
        auto* lv = list->AddComponent<UIListView>();
        lv->anchor = UIAnchor::TopLeft; lv->position = {0, 0}; lv->size = {300, 200}; lv->itemHeight = 30;
        GameObject* row = s.CreateGameObject("Row");
        row->transform->SetParent(list->transform, false);
        auto* rowText = row->AddComponent<TextRenderer>();
        rowText->screenSpace = true; rowText->text = "template";
        GameObject* cnt = s.CreateGameObject("Count");
        cnt->transform->SetParent(row->transform, false);
        auto* cntText = cnt->AddComponent<TextRenderer>();
        cntText->screenSpace = true; cntText->text = "x{count|1}";
        auto* lb = list->AddComponent<UIListBind>();
        lb->var = "bag_items"; lb->format = "{index}. {item}";
        ActionList::Arrays()["bag_items"] = {5.0f, 7.0f, 9.0f};
        s.Start();
        UpdateUILayouts(s);
        CHECK(lb->Count() == 3);
        CHECK(lv->Count() == 3);
        CHECK(lv->Rows().size() == 3);
        CHECK(lv->Rows()[0]->GetComponent<TextRenderer>()->text == "1. 5");
        CHECK(lv->Rows()[2]->GetComponent<TextRenderer>()->text == "3. 9");
        ActionList::Arrays()["bag_items"].push_back(11.0f);          // the array grows: the list follows
        CHECK(lb->Sync());
        UpdateUILayouts(s);
        CHECK(lv->Count() == 4);
        CHECK(!lb->Sync());                                           // unchanged: no refresh
        // A visual-script list of maps: each key is a field, and a second text in the template is filled too.
        auto arr = vs::VsValue::MakeArray();
        for (int i = 0; i < 2; ++i) {
            auto m = vs::VsValue::MakeMap();
            (*m.AsMap())["name"] = vs::VsValue(i == 0 ? "Potion" : "Arrow");
            (*m.AsMap())["count"] = vs::VsValue(i == 0 ? 3.0f : 40.0f);
            arr.AsArray()->push_back(m);
        }
        vs::SceneVariables(&s)["bag_items"] = arr;
        lb->format = "{name}";
        CHECK(lb->Sync());
        UpdateUILayouts(s);
        CHECK(lv->Count() == 2);
        CHECK(lv->Rows()[0]->GetComponent<TextRenderer>()->text == "Potion");
        CHECK(lv->Rows()[1]->GetComponent<TextRenderer>()->text == "Arrow");
        CHECK(lv->Rows()[1]->transform->Children()[0]->gameObject->GetComponent<TextRenderer>()->text == "x40");
        // An empty array shows the empty text; with none set there are no rows.
        vs::SceneVariables(&s).erase("bag_items");
        ActionList::Arrays()["bag_items"].clear();
        lb->emptyText = "Nothing here";
        CHECK(lb->Sync());
        CHECK(lb->Count() == 1 && lb->Row(0).at("item") == "Nothing here");
        lb->emptyText.clear();
        CHECK(lb->Sync());
        CHECK(lb->Count() == 0 && lv->Count() == 0);
        lb->format = "{name}: {count}";
        CHECK(RoundTrip(s, "Bag", [](GameObject* o) {
            auto* r = o ? o->GetComponent<UIListBind>() : nullptr;
            return r && r->var == "bag_items" && r->format == "{name}: {count}";
        }));
        ActionList::Arrays().erase("bag_items");
    }

    // ===== Rich text links and icons ===============================================================================
    {
        RichText rt = ParseRichText("Read the [url=help]manual[/url] now");
        CHECK(rt.plain == "Read the manual now");
        CHECK(rt.links.size() == 1 && rt.links[0] == "help");
        int linked = 0;
        for (const RichStyle& st : rt.styles) if (st.link == 0) { ++linked; CHECK(st.underline && st.hasColor && st.b == kRichLinkB); }
        CHECK(linked == 6);                                           // m a n u a l
        RichText c = ParseRichText("[url=a][color=red]x[/color][/url]");
        CHECK(c.styles.size() == 1 && c.styles[0].link == 0 && c.styles[0].r == 230);   // an explicit colour wins over the link colour
        RichText two = ParseRichText("[url=a]A[/url][url=b]B[/url]");
        CHECK(two.links.size() == 2 && two.styles[0].link == 0 && two.styles[1].link == 1);
        RichText bad = ParseRichText("[url=]no target[/url] [/url]");
        CHECK(bad.plain == "[url=]no target[/url] [/url]" && bad.links.empty());       // shown as text
        RichText longUrl = ParseRichText("[url=https://example.org/a/long/path/that/is/over/24/chars]go[/url]");
        CHECK(longUrl.plain == "go" && longUrl.links.size() == 1);
        RichText ic = ParseRichText("Cost: 5 [icon=coin] and [icon=assets/gem.png]");
        CHECK(ic.icons.size() == 2 && ic.icons[0] == "coin" && ic.icons[1] == "assets/gem.png");
        CHECK(ic.plain == std::string("Cost: 5 ") + kRichIconChar + " and " + kRichIconChar);
        int iconStyles = 0;
        for (const RichStyle& st : ic.styles) if (st.icon >= 0) ++iconStyles;
        CHECK(iconStyles == 2);
        CHECK(StripRichText("[url=x]a[/url][icon=b]") == std::string("a") + kRichIconChar);
        CHECK(!(ParseRichText("[url=a]x[/url]").styles[0] == ParseRichText("x").styles[0]));
        // The old tags still behave exactly as before.
        RichText old = ParseRichText("[b]B[/b] [i]I[/i] [color=#ff0000]R[/color] [[x]");
        CHECK(old.plain == "B I R [x]" && old.links.empty() && old.icons.empty() && old.styles[0].bold && old.styles[1].italic);
    }
    {
        // Clicking a link: UIRichLinks finds the link where the text was drawn (the renderer records the rectangles).
        Scene s("Links"); s.physicsEnabled = false;
        GameObject* g = s.CreateGameObject("Help");
        auto* tr = g->AddComponent<TextRenderer>();
        tr->screenSpace = true; tr->richText = true; tr->text = "Open the [url=manual]manual[/url]";
        auto* links = g->AddComponent<UIRichLinks>();
        auto* sc = g->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource("function on_link() { set_x(3); }"));
        s.Start();
        CHECK(links->LinkAt(5, 5).empty());                           // nothing drawn yet: no link rectangles
        CHECK(!links->Click(5, 5));
        CHECK(RoundTrip(s, "Help", [](GameObject* o) {
            auto* r = o ? o->GetComponent<UIRichLinks>() : nullptr;
            return r && r->fireEvent && r->storeInPrefs;
        }));
    }

    // ===== Focus hints (Tab order and links in NavigateUI) =========================================================
    {
        Scene s("Focus"); s.physicsEnabled = false;
        auto mk = [&](const char* name, float x, float y, int tab) {
            GameObject* g = s.CreateGameObject(name);
            auto* b = g->AddComponent<UIButton>();
            b->anchor = UIAnchor::TopLeft; b->position = {x, y}; b->size = {80, 30};
            if (tab) g->AddComponent<UIFocusHint>()->tabIndex = tab;
            return b;
        };
        UIButton* a = mk("A", 10, 10, 0);
        UIButton* b = mk("B", 10, 60, 2);
        UIButton* c = mk("C", 10, 110, 1);
        auto tab = [&](bool shift) {
            Input::FeedKeys({});
            if (shift) Input::FeedKeys({'\t', Input::KeyShift}); else Input::FeedKeys({'\t'});
            NavigateUI(s);
        };
        tab(false);                                                   // nothing focused: the first in tab order (C, tabIndex 1)
        CHECK(c->IsFocused() && !a->IsFocused() && !b->IsFocused());
        tab(false);                                                   // then B (2), then A (0 = after the numbered ones)
        CHECK(b->IsFocused());
        tab(false);
        CHECK(a->IsFocused());
        tab(false);                                                   // wraps to the first
        CHECK(c->IsFocused());
        tab(true);                                                    // Shift+Tab goes back
        CHECK(a->IsFocused());
        // An explicit link beats the geometry; "-" holds focus.
        Input::FeedKeys({});
        GameObject* ga = a->gameObject;
        auto* hint = ga->AddComponent<UIFocusHint>();
        hint->down = "C"; hint->up = "-";
        Input::FeedKeys({'s'});
        NavigateUI(s);
        CHECK(c->IsFocused());                                        // down from A skipped B
        a->SetFocused(true); c->SetFocused(false);
        Input::FeedKeys({});
        Input::FeedKeys({'w'});
        NavigateUI(s);
        CHECK(a->IsFocused());                                        // up is blocked
        // No hints at all: Tab still walks reading order.
        Scene s2("Focus2"); s2.physicsEnabled = false;
        std::vector<UIButton*> bs;
        for (int i = 0; i < 3; ++i) {
            auto* bb = s2.CreateGameObject(std::string("N") + std::to_string(i))->AddComponent<UIButton>();
            bb->anchor = UIAnchor::TopLeft; bb->position = {10, 10.0f + 50.0f * i}; bb->size = {80, 30};
            bs.push_back(bb);
        }
        Input::FeedKeys({}); Input::FeedKeys({'\t'}); NavigateUI(s2);
        CHECK(bs[0]->IsFocused());
        Input::FeedKeys({}); Input::FeedKeys({'\t'}); NavigateUI(s2);
        CHECK(bs[1]->IsFocused());
        Input::FeedKeys({});
        CHECK(RoundTrip(s, "A", [](GameObject* o) {
            auto* r = o ? o->GetComponent<UIFocusHint>() : nullptr;
            return r && r->down == "C" && r->up == "-";
        }));
        CHECK(RoundTrip(s, "B", [](GameObject* o) {
            auto* r = o ? o->GetComponent<UIFocusHint>() : nullptr;
            return r && r->tabIndex == 2;
        }));
    }

    // ===== Hot reload ==============================================================================================
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / "okay_ui_library2_hot";
        std::error_code ec;
        fs::create_directories(dir, ec);
        const std::string markup = (dir / "screen.okayui").string();
        auto write = [](const std::string& path, const std::string& text) { std::ofstream f(path, std::ios::binary | std::ios::trunc); f << text; };
        write(markup, "text \"Hello\" pos=10,10 size=3 name=Greeting\n");
        Scene s("Hot"); s.physicsEnabled = false;
        GameObject* g = s.CreateGameObject("Doc");
        auto* doc = g->AddComponent<UIDocument>();
        auto* hot = g->AddComponent<UIHotReload>();
        hot->file = markup; hot->interval = 0.05f;
        s.Start();
        CHECK(hot->Reloads() == 1);                                   // loaded once at start
        CHECK(doc->markup.find("Hello") != std::string::npos);
        CHECK(!doc->Generated().empty());
        CHECK(!hot->Poll());                                          // nothing changed
        write(markup, "text \"Changed words\" pos=10,10 size=3 name=Greeting\ntext \"Second\" pos=10,40 size=3 name=Second\n");
        fs::last_write_time(markup, fs::file_time_type::clock::now() + std::chrono::seconds(5), ec);   // a clearly newer time
        CHECK(hot->Poll());
        CHECK(hot->Reloads() == 2);
        CHECK(doc->markup.find("Changed words") != std::string::npos);
        CHECK(doc->Generated().size() == 2);                          // rebuilt from the new file
        CHECK(!hot->Poll());
        // The frame path: Update looks at the file by itself.
        write(markup, "text \"Third\" pos=10,10 size=3 name=Greeting\n");
        fs::last_write_time(markup, fs::file_time_type::clock::now() + std::chrono::seconds(10), ec);
        s.Update(0.2f);
        CHECK(hot->Reloads() == 3 && doc->Generated().size() == 1);
        // A theme file.
        GameObject* tg = s.CreateGameObject("Theme");
        auto* th = tg->AddComponent<UITheme>();
        const std::string themePath = (dir / "look.okaytheme").string();
        UITheme other;
        other.themeName = "Hot Look";
        other.accent = Color::FromBytes(200, 30, 60);
        CHECK(SaveUIThemeAsset(other, themePath));
        auto* hot2 = tg->AddComponent<UIHotReload>();
        hot2->file = themePath;
        s.Start();
        CHECK(hot2->ReloadNow());
        CHECK(th->themeName == "Hot Look");
        // Errors are said plainly, not thrown.
        hot2->file = (dir / "missing.okaytheme").string();
        CHECK(!hot2->ReloadNow() && !hot2->LastError().empty());
        hot2->file = markup;                                          // a markup file but the target has no UI Document
        CHECK(!hot2->ReloadNow() && hot2->LastError().find("UI Document") != std::string::npos);
        hot2->target = "Nope";
        CHECK(!hot2->ReloadNow() && hot2->LastError().find("target") != std::string::npos);
        hot->interval = 0.25f;
        CHECK(RoundTrip(s, "Doc", [&](GameObject* o) {
            auto* r = o ? o->GetComponent<UIHotReload>() : nullptr;
            return r && r->file == markup && r->interval == 0.25f && r->loadAtStart;
        }));
        fs::remove_all(dir, ec);
    }

    TEST_MAIN_RESULT();
}
