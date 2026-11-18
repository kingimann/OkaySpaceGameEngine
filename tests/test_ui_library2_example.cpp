// The shipped sample scene examples/ui_library/ui_library_2.okayscene (UI library slice 2): game HUD parts and the
// data-driven glue. This test loads the file from disk and plays it like a person would (press the buttons, move the slider,
// type in the box) and checks what the screen would show.
//
// The files are made by this test: run it with OKAY_WRITE_UI_LIBRARY_EXAMPLE2=<folder> to write ui_library_2.okayscene,
// ui_library_items.okayitems and hot_reload_demo.okayui there (after a change to the builder or to the serializer). Without
// it the test only reads the committed files. Written for this engine; no other project's code or assets were used.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Scene/UILibrary.hpp"
#include "okay/Inventory/ItemDatabase.hpp"
#include "okay/UI/UILayoutUpdate.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

using namespace okay;

static std::string WithoutGuids(const std::string& text) {
    std::string out, line;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == '\n') {
            if (line.rfind("  guid ", 0) != 0) { out += line; out += '\n'; }
            line.clear();
        } else line += text[i];
    }
    return out;
}

// The three sample items the example's tooltip card shows (the file is the same text the item editor writes).
static std::string SampleItems() {
    ItemDatabase db;
    ItemDefinition sword;
    sword.id = "sword_iron"; sword.name = "Iron Sword"; sword.category = "Weapon"; sword.description = "A plain, honest blade.";
    sword.rarity = 2; sword.rarityColor = Color::FromBytes(90, 150, 255); sword.hasRarityColor = true;
    sword.weight = 2.4f; sword.maxStack = 1; sword.equip = EquipSlot::MainHand;
    sword.custom.push_back({"damage", "12"}); sword.custom.push_back({"speed", "1.1"});
    db.Put(sword);
    ItemDefinition potion;
    potion.id = "potion_heal"; potion.name = "Healing Potion"; potion.category = "Potion"; potion.description = "Heals a little, tastes of mint.";
    potion.rarity = 1; potion.rarityColor = Color::FromBytes(110, 215, 150); potion.hasRarityColor = true;
    potion.weight = 0.3f; potion.maxStack = 20; potion.useAction = "Heal"; potion.useAmount = 25.0f;
    db.Put(potion);
    ItemDefinition cap;
    cap.id = "cap_leather"; cap.name = "Leather Cap"; cap.category = "Armor"; cap.description = "Keeps the rain out of your eyes.";
    cap.rarity = 0; cap.weight = 0.5f; cap.maxStack = 1; cap.equip = EquipSlot::Head;
    cap.custom.push_back({"armor", "3"});
    db.Put(cap);
    return db.Serialize();
}

int main() {
    RUN_SUITE("ui_library2_example");
    const std::string dir = std::string(OKAY_SOURCE_DIR) + "/examples/ui_library";
    const std::string path = dir + "/ui_library_2.okayscene";

    if (const char* out = std::getenv("OKAY_WRITE_UI_LIBRARY_EXAMPLE2")) {
        const std::string base = out;
        Scene s("UI Library 2");
        UILibrary::BuildSlice2(s);
        std::string err;
        const bool ok = SceneSerializer::SaveToFile(s, base + "/ui_library_2.okayscene", &err);
        std::printf("  wrote %s/ui_library_2.okayscene: %s %s\n", out, ok ? "ok" : "FAILED", err.c_str());
        CHECK(ok);
        { std::ofstream f(base + "/ui_library_items.okayitems", std::ios::binary | std::ios::trunc); f << SampleItems(); CHECK((bool)f); }
        { std::ofstream f(base + "/hot_reload_demo.okayui", std::ios::binary | std::ios::trunc); f << UILibrary::HotReloadDemoMarkup(); CHECK((bool)f); }
        TEST_MAIN_RESULT();
    }

    // The shipped items file is what the builder above writes (so the tooltip card has real stats to show).
    {
        std::ifstream f(dir + "/ui_library_items.okayitems", std::ios::binary);
        std::string have((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        CHECK(have == SampleItems());
        std::ifstream g(dir + "/hot_reload_demo.okayui", std::ios::binary);
        std::string demo((std::istreambuf_iterator<char>(g)), std::istreambuf_iterator<char>());
        CHECK(demo == UILibrary::HotReloadDemoMarkup());
    }

    Scene s("play");
    s.physicsEnabled = false;
    std::string err;
    std::vector<std::string> warnings;
    const bool loaded = SceneSerializer::LoadFromFile(s, path, &err, &warnings);
    if (!loaded) std::printf("  could not load %s: %s\n", path.c_str(), err.c_str());
    CHECK(loaded);
    CHECK(warnings.empty());
    for (const auto& w : warnings) std::printf("  warning: %s\n", w.c_str());
    if (!loaded) TEST_MAIN_RESULT();

    {   // the file is what the builder makes today (a stale file would hide a change from the owner)
        Scene fresh("UI Library 2");
        UILibrary::BuildSlice2(fresh);
        Scene reread("reread");
        std::string e2;
        CHECK(SceneSerializer::Deserialize(reread, SceneSerializer::Serialize(fresh), &e2, nullptr));
        CHECK(WithoutGuids(SceneSerializer::Serialize(reread)) == WithoutGuids(SceneSerializer::Serialize(s)));
    }

    // Item definitions from a file the scene names: the card finds them wherever the test runs (the absolute path stands in
    // for the install folder the editor opens the example from).
    ItemDatabase::Get().Clear();
    GameObject* cardObj = s.Find("Card");
    CHECK(cardObj && cardObj->GetComponent<UIItemTooltipCard>());
    if (cardObj) cardObj->GetComponent<UIItemTooltipCard>()->itemsFile = dir + "/ui_library_items.okayitems";
    GameObject* hotObj = s.Find("HotDoc");
    CHECK(hotObj && hotObj->GetComponent<UIHotReload>());
    if (hotObj) hotObj->GetComponent<UIHotReload>()->file = dir + "/hot_reload_demo.okayui";

    ActionList::ResetVars();
    s.Start();
    UpdateUILayouts(s);
    s.Update(0.016f);

    UICanvas::Set(1280.0f, 720.0f);
    // The window host tells the widgets where everything really is on screen (a button inside a card sits at the card's place plus its own);
    // this installs the same rectangle lookup, so a click lands on what is drawn there and the cards in front of other cards still block.
    UIWorld().rectOf = [](GameObject* go, Vec2& o, Vec2& sz) { return GetUIScreenRect(go, 1280.0f, 720.0f, o, sz); };
    UIWorld().screenW = 1280.0f; UIWorld().screenH = 720.0f;
    // A real click: the pointer goes down and up over the middle of the button.
    auto press = [&](const char* name) {
        GameObject* g = s.Find(name);
        CHECK(g && g->GetComponent<UIButton>());
        if (!g || !g->GetComponent<UIButton>()) return;
        Vec2 o, sz;
        CHECK(GetUIScreenRect(g, 1280.0f, 720.0f, o, sz));
        const Vec2 c{o.x + sz.x * 0.5f, o.y + sz.y * 0.5f};
        Input::FeedMouse(c, 0);
        Input::FeedMouse(c, 1u << 0);
        s.Update(0.016f);
        Input::FeedMouse(c, 0);
        s.Update(0.016f);
        Input::FeedMouse({0, 0}, 0);
        UpdateUILayouts(s);
        s.Update(0.016f);
    };
    float v = 0;

    {   // ammo: Shoot takes a round, Reload refills from the spare rounds
        auto* ammo = s.Find("Ammo")->GetComponent<UIAmmoCounter>();
        CHECK(ammo->Text() == "24 / 90");
        press("Shoot");
        CHECK(ammo->Text() == "23 / 90");
        press("Reload");
        CHECK(ammo->Text() == "30 / 83");
    }
    {   // ability: the button uses it, the ring sweeps, time brings it back
        auto* ab = s.Find("Dash")->GetComponent<UIAbilityIcon>();
        auto* ring = s.Find("Dash")->GetComponent<UIRadialProgress>();
        CHECK(ab->Ready());
        press("UseAbility");
        CHECK(!ab->Ready() && ring->value > 0.9f);
        for (int i = 0; i < 20; ++i) s.Update(0.25f);                 // 5 seconds
        CHECK(ab->Ready() && ring->value == 0.0f);
        Input::FeedKeys({'q'});                                       // the Q key does the same
        s.Update(0.016f);
        CHECK(!ab->Ready());
        Input::FeedKeys({});
        s.Update(0.016f);
    }
    {   // prompt: hidden until the toggle says the player is near
        auto* ip = s.Find("Prompt")->GetComponent<UIInteractionPrompt>();
        for (int i = 0; i < 10; ++i) s.Update(0.05f);
        CHECK(!ip->Visible());
        s.Find("NearDoor")->GetComponent<UIToggle>()->Toggle();
        for (int i = 0; i < 20; ++i) s.Update(0.05f);
        CHECK(uivars::ReadNumber(s.Find("NearDoor"), "near_door", v) && v == 1.0f);
        CHECK(ip->Visible());
        s.Find("NearDoor")->GetComponent<UIToggle>()->Toggle();
        for (int i = 0; i < 20; ++i) s.Update(0.05f);
        CHECK(!ip->Visible());
    }
    {   // radial menu: a click on a wedge chooses it, and the text under it follows
        auto* wheel = s.Find("Wheel")->GetComponent<UIRadialMenu>();
        wheel->Choose(2);
        s.Update(0.016f);
        CHECK(uivars::Read(s.Find("Wheel"), "radial_pick_text").text == "Heal");
        CHECK(s.Find("Picked")->GetComponent<TextRenderer>()->text == "Heal");
    }
    {   // kill feed: the New kill button adds lines at the top right
        auto* feed = s.Find("KillFeed")->GetComponent<UIKillFeed>();
        feed->Clear();
        press("NewKill");
        press("NewKill");
        CHECK(feed->Entries().size() == 2);
        CHECK(feed->Entries()[0].text != feed->Entries()[1].text);
    }
    {   // quests: started by the demo script; the buttons move them on; Again starts over
        auto* tr = s.Find("Tracker")->GetComponent<UIObjectiveTracker>();
        CHECK(tr->Lines().size() == 4);                               // title + 3 objectives
        press("Wolf"); press("Wolf");
        auto ln = tr->Lines();
        CHECK(ln.size() == 4 && ln[1].have == 2 && ln[1].need == 3);
        press("Talk");
        ln = tr->Lines();
        CHECK(ln.size() == 4 && ln[2].kind == UIObjectiveTracker::Line::Done);
        press("Again");
        ln = tr->Lines();
        CHECK(ln.size() == 4 && ln[1].have == 0 && ln[2].kind == UIObjectiveTracker::Line::Objective);
    }
    {   // compass: the slider writes the heading variable, the strip follows
        auto* sl = s.Find("Heading")->GetComponent<UISlider>();
        auto* cp = s.Find("Compass")->GetComponent<UICompass>();
        sl->SetValue(90.0f);
        s.Update(0.016f);
        CHECK_NEAR(cp->Heading(), 90.0f, 0.01);
    }
    {   // item tooltip card: the dropdown picks the item; the card shows its stats from the items file
        auto* dd = s.Find("ItemPick")->GetComponent<UIDropdown>();
        auto* card = s.Find("Card")->GetComponent<UIItemTooltipCard>();
        s.Update(0.016f);
        CHECK(card->Lines()[0].text == "Iron Sword");
        bool damage = false;
        for (const auto& l : card->Lines()) if (l.text == "damage: 12") damage = true;
        CHECK(damage);
        dd->SetValue(1);
        s.Update(0.016f);
        CHECK(card->Lines()[0].text == "Healing Potion");
        dd->SetValue(2);
        s.Update(0.016f);
        CHECK(card->Lines()[0].text == "Leather Cap");
    }
    {   // bindings and conditions: slider and box are one number; below 20 the warning shows; Mute swaps it
        auto* sl = s.Find("Volume")->GetComponent<UISlider>();
        auto* box = s.Find("VolumeBox")->GetComponent<UIInputField>();
        GameObject* warn = s.Find("LowWarning");
        GameObject* mutedNote = s.Find("MutedNote");
        CHECK(box->text == "60");                                     // the box shows the slider's number from the first frame
        CHECK(!warn->active && !mutedNote->active);
        sl->SetValue(35.0f);
        s.Update(0.016f); s.Update(0.016f);
        CHECK(box->text == "35");
        CHECK(s.Find("VolNow")->GetComponent<TextRenderer>()->text == "Volume is 35");
        box->text = "10";                                             // typing in the box moves the slider
        s.Update(0.016f); s.Update(0.016f);
        CHECK_NEAR(sl->value, 10.0f, 1e-3);
        UpdateUILayouts(s);
        CHECK(warn->active && !mutedNote->active);
        s.Find("Mute")->GetComponent<UIToggle>()->Toggle();
        s.Update(0.016f);
        UpdateUILayouts(s);
        s.Update(0.016f);
        CHECK(!warn->active && mutedNote->active);
        sl->SetValue(80.0f);
        s.Find("Mute")->GetComponent<UIToggle>()->Toggle();
        s.Update(0.016f); s.Update(0.016f);
        UpdateUILayouts(s);
        CHECK(!warn->active && !mutedNote->active);
    }
    {   // list from an array: three rows from the script, then one more
        GameObject* lg = s.Find("Scores");
        auto* lv = lg->GetComponent<UIListView>();
        auto* lb = lg->GetComponent<UIListBind>();
        UpdateUILayouts(s);
        CHECK(lb->Count() == 3 && lv->Count() == 3);
        CHECK(lv->Rows().size() >= 3);
        CHECK(lv->Rows()[0]->GetComponent<TextRenderer>()->text == "Score 1:  120 points");
        press("AddScore");
        UpdateUILayouts(s);
        CHECK(lb->Count() == 4);
        CHECK(lv->Rows()[3]->GetComponent<TextRenderer>()->text == "Score 4:  55 points");
        press("ClearScores");
        UpdateUILayouts(s);
        CHECK(lb->Count() == 1 && lb->Row(0).at("item") == "No scores yet");
    }
    {   // rich text: one link, three inline pictures
        auto* tr = s.Find("Links")->GetComponent<TextRenderer>();
        const RichText rt = ParseRichText(tr->text);
        CHECK(rt.links.size() == 1 && rt.links[0] == "manual");
        CHECK(rt.icons.size() == 3);
        CHECK(s.Find("Links")->GetComponent<UIRichLinks>() != nullptr);
    }
    {   // hot reload: the file the scene names is the one the shipped demo text is in
        auto* hot = s.Find("HotDoc")->GetComponent<UIHotReload>();
        CHECK(hot->Reloads() >= 1);
        auto* doc = s.Find("HotDoc")->GetComponent<UIDocument>();
        CHECK(doc->markup.find("HOT RELOAD") != std::string::npos);
        CHECK(!doc->Generated().empty());
    }
    UIWorld().rectOf = nullptr;
    ActionList::ResetVars();
    TEST_MAIN_RESULT();
}
