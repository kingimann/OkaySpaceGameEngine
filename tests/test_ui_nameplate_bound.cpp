// UI nameplate bound to the character it belongs to: `owner.` keys in UITextBind / UIBarBind (UIOwnerData). Each
// nameplate made a child of a character shows that character's name, Stats level and health, two characters show
// their own values, a character without some data never shows the player's, and a nameplate with no owner keeps the
// old preview behaviour (the player's published health). Written for this engine; no other project's code was used.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/UIOwnerData.hpp"
#include "okay/Scene/UIGallery.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/UI/UIRenderer.hpp"
#include "ui_record_painter.hpp"
#include <cmath>
#include <string>

using namespace okay;
namespace np = okay::UIGallery::nameplate;   // the plate's measurements

namespace {
struct Plate {
    TextRenderer* name = nullptr;
    TextRenderer* level = nullptr;
    UIProgressBar* bar = nullptr;
};
Plate PlateOf(GameObject* plate) {
    Plate p;
    if (Transform* t = plate->transform->Find("SwatchPlate/Name")) p.name = t->gameObject->GetComponent<TextRenderer>();
    if (Transform* t = plate->transform->Find("SwatchPlate/SwatchLevel/Label")) p.level = t->gameObject->GetComponent<TextRenderer>();
    if (Transform* t = plate->transform->Find("SwatchPlate/Health")) p.bar = t->gameObject->GetComponent<UIProgressBar>();
    return p;
}
GameObject* Mob(Scene& s, const char* name, float hp, float maxHp, int level) {
    GameObject* g = s.CreateGameObject(name);
    auto* h = g->AddComponent<HealthStat>();
    h->publishPrefs = false;
    h->maxHealth = maxHp; h->health = hp;
    if (level > 0) g->AddComponent<Stats>()->level = level;
    return g;
}
GameObject* PlateOn(Scene& s, GameObject* owner) {
    GameObject* plate = UIGallery::AddNameplate(s, true);
    if (owner) plate->transform->SetParent(owner->transform, false);
    return plate;
}
void Tick(Scene& s, int n = 2) { for (int i = 0; i < n; ++i) s.Update(0.016f); }
} // namespace

int main() {
    RUN_SUITE("ui_nameplate_bound");
    ActionList::ResetVars();

    {   // two characters, each with its own nameplate
        Scene s("plates");
        GameObject* a = Mob(s, "Goblin Scout", 30.0f, 40.0f, 3);
        GameObject* b = Mob(s, "Orc Chief", 90.0f, 100.0f, 12);
        const Plate pa = PlateOf(PlateOn(s, a)), pb = PlateOf(PlateOn(s, b));
        CHECK(pa.name && pa.level && pa.bar && pb.name && pb.level && pb.bar);
        s.Start();
        // Health Stat fills health to the maximum when it starts: set the values to show after that.
        a->GetComponent<HealthStat>()->health = 30.0f;
        b->GetComponent<HealthStat>()->health = 90.0f;
        ActionList::Vars()["healthFrac"] = 0.9f;   // what the player's Health Stat would publish
        Tick(s);
        if (pa.name && pb.name && pa.level && pb.level && pa.bar && pb.bar) {
            CHECK(pa.name->text == "Goblin Scout");
            CHECK(pb.name->text == "Orc Chief");
            CHECK(pa.level->text == "Lv 3");
            CHECK(pb.level->text == "Lv 12");
            CHECK_NEAR(pa.bar->value, 0.75f, 1e-4f);
            CHECK_NEAR(pb.bar->value, 0.90f, 1e-4f);
            // live: damage, a level up and a rename show on the next frame
            a->GetComponent<HealthStat>()->Damage(20.0f);
            a->GetComponent<Stats>()->level = 4;
            a->name = "Goblin Captain";
            Tick(s);
            CHECK_NEAR(pa.bar->value, 0.25f, 1e-4f);
            CHECK(pa.level->text == "Lv 4");
            CHECK(pa.name->text == "Goblin Captain");
            CHECK_NEAR(pb.bar->value, 0.90f, 1e-4f);                   // the other plate did not move
        }
        // the keys directly
        GameObject* label = pa.name ? pa.name->gameObject : nullptr;
        CHECK(uiowner::Find(label) == a);
        CHECK(UITextBind::Resolve("{owner.health}/{owner.maxHealth} ({owner.healthPct}%)", label) == "10/40 (25%)");
        CHECK(UITextBind::Resolve("{owner.mana|-}", label) == "-");          // a key no owner has: the default
    }

    {   // an owner without Stats or without health never shows the player's values
        Scene s("partial");
        GameObject* npc = s.CreateGameObject("Villager");
        auto* n = npc->AddComponent<NPCController>();
        n->maxHealth = 50.0f; n->health = 20.0f;
        GameObject* shade = s.CreateGameObject("Shade");
        shade->AddComponent<Character>();
        const Plate pn = PlateOf(PlateOn(s, npc)), ps = PlateOf(PlateOn(s, shade));
        s.Start();
        n->health = 20.0f;                                                    // after Start (it fills to the maximum)
        ActionList::Vars()["healthFrac"] = 0.9f;
        ActionList::Vars()["level"] = 30.0f;                                  // a global "level" must not leak in
        Tick(s);
        if (pn.name && pn.level && pn.bar && ps.name && ps.bar) {
            CHECK(pn.name->text == "Villager");
            CHECK(pn.level->text == "Lv 1");                                  // no Stats: level 1, not the global 30
            CHECK_NEAR(pn.bar->value, 0.4f, 1e-4f);                          // from the NPC Controller
            CHECK(ps.name->text == "Shade");
            CHECK_NEAR(ps.bar->value, 0.72f, 1e-4f);                        // no health: keeps its built fill, never the player's 0.9
        }
    }

    {   // no owner (the gallery preview): the old behaviour, the player's published health and the defaults
        Scene s("preview");
        ActionList::ResetVars();
        ActionList::Vars()["healthFrac"] = 0.6f;
        const Plate p = PlateOf(PlateOn(s, nullptr));
        s.Start(); Tick(s);
        if (p.name && p.level && p.bar) {
            CHECK(p.name->text == "Goblin");
            CHECK(p.level->text == "Lv 1");
            CHECK_NEAR(p.bar->value, 0.6f, 1e-4f);
        }
        CHECK(UITextBind::Resolve("{owner.healthFrac}", nullptr) == "0.6");   // falls back to the global variable
    }

    {   // the measurements add up: the pill, the bar and the name boxes fit the card with room to spare
        using namespace np;
        CHECK(kPad + kNameW + 8.0f + kBadgeW + kPad <= kCardW + 1e-3f);            // name | gap | pill, both sides padded
        CHECK(kBadgeTop + kBadgeH <= kNameTop + kNameH + 1e-3f);                    // the pill is level with the name row
        CHECK(kBarTop + kBarH + 6.0f <= kCardH);                                    // the bar has room under it
        CHECK(kNameTop + kNameH <= kBarTop);                                        // and does not touch the name row
    }

    {   // what is DRAWN, at every size: the card, the level pill, the bar and both texts all stay inside the card
        // (the panels and the bar used to be drawn at their design size while the texts and the places of the parts were scaled, so at
        // any size but 1:1 the level pill stuck out of the card; world texts also ignored their centring, so the level sat on the
        // pill's left edge and the name on the top of its box)
        constexpr int W = 1280, H = 720;
        struct Case { const char* what; bool constant; float pxPerUnit; const char* font; const char* name; int level; bool readable; };
        const Case cases[] = {
            {"constant size (the default)", true, 100.0f, "", "Goblin Scout", 12, true},
            {"far away, scaling with distance (k 0.7)", false, 70.0f, "", "Goblin Scout", 12, true},
            {"close up, scaling with distance (k 1.6)", false, 160.0f, "", "Goblin Scout", 12, true},
            {"a long name and a three-digit level", true, 100.0f, "", "Grand Goblin Warlord of Doom", 100, false},
            {"the pixel font", true, 100.0f, "pixel", "Goblin Scout", 12, true},
            {"the pixel font, long name, three digits", true, 100.0f, "pixel", "Goblin Warlord Chief", 100, false},
        };
        for (const Case& c : cases) {
            Scene s("drawn"); s.physicsEnabled = false; s.uiFont = c.font;
            GameObject* mob = Mob(s, c.name, 40.0f, 40.0f, c.level);
            mob->transform->localPosition = {2.0f, 1.0f, 0.0f};
            GameObject* plate = PlateOn(s, mob);
            plate->transform->localPosition = {0.0f, 1.2f, 0.0f};          // above the head
            auto* ws = plate->GetComponent<WorldSpaceUI>();
            CHECK(ws && ws->constantSize);                                   // by default the plate keeps its design size at any range
            if (ws && !c.constant) ws->constantSize = false;                 // else it scales with distance: pixelsPerUnit 100
            const float k = c.constant || !ws ? 1.0f : c.pxPerUnit / ws->pixelsPerUnit;
            s.Start(); Tick(s);
            UIWorld().active = true; UIWorld().screenW = (float)W; UIWorld().screenH = (float)H; UIWorld().right = {1, 0, 0};
            const float pxu = c.pxPerUnit;
            UIWorld().project = [pxu](const Vec3& p, Vec2& o, float& d) { o = Vec2{W * 0.5f + p.x * pxu, H * 0.5f - p.y * pxu}; d = 10.0f; return true; };
            uitest::RecordPainter rp;
            ui2d::RenderSceneUI(s, rp, W, H);
            UIWorld().active = false;
            const Vec2 at{W * 0.5f + 2.0f * pxu, H * 0.5f - 2.2f * pxu};   // where the plate's 3D point lands on the screen
            const auto card = rp.Of(16, 18, 24), pill = rp.Of(240, 180, 70), bar = rp.Of(226, 82, 82);
            const auto name = rp.Of(232, 235, 240), lvl = rp.Of(249, 224, 181);
            std::printf("  %-44s card %.0f x %.0f, pill %.0f x %.0f, name %.0f..%.0f, level %.0f..%.0f\\n", c.what,
                        card.W(), card.H(), pill.W(), pill.H(), name.x0, name.x1, lvl.x0, lvl.x1);
            CHECK(card.Any() && pill.Any() && bar.Any() && name.Any() && lvl.Any());
            if (!(card.Any() && pill.Any() && bar.Any() && name.Any() && lvl.Any())) continue;
            const float tol = 2.0f;
            CHECK_NEAR(card.W(), np::kCardW * k, 3.0f);                  // the card has its design size, times the scale
            CHECK_NEAR(card.H(), np::kCardH * k, 3.0f);
            CHECK_NEAR(card.CX(), at.x, 3.0f);                                  // centred on the 3D point, hanging above it
            CHECK_NEAR(card.y1, at.y, 3.0f);
            CHECK_NEAR(pill.W(), np::kBadgeW * k, 3.0f);                 // the pill is drawn at the scale too
            CHECK_NEAR(pill.H(), np::kBadgeH * k, 3.0f);
            CHECK(pill.InsideOf(card, tol));                                    // the level pill is inside the card
            CHECK(pill.x1 <= card.x1 - np::kPad * k * 0.5f);             // with a margin to the card's edge
            CHECK(bar.InsideOf(card, tol));
            CHECK_NEAR(bar.W(), (np::kCardW - 2.0f * np::kPad) * k, 4.0f);   // a full bar spans the card minus its margins
            CHECK(name.InsideOf(card, tol));
            CHECK(name.x1 <= pill.x0 + tol);                                    // the name stops before the pill
            CHECK(lvl.InsideOf(pill, tol));                                     // the level text stays in the pill
            CHECK(std::fabs(lvl.CX() - pill.CX()) <= 4.0f * k + 2.0f);          // and is centred in it
            CHECK(std::fabs(lvl.CY() - pill.CY()) <= 5.0f * k + 2.0f);
            CHECK(name.CY() > card.y0 && name.CY() < bar.y0);                   // the name sits in the row above the bar
            if (c.readable) {
                CHECK(name.H() >= 9.0f * k);                                    // text a person can read: at least 9 px at 1:1
                CHECK(lvl.H() >= 7.0f * k);
            }
        }
    }

    {   // the plate saved and loaded again still shows its character (the binding keys are plain strings in the scene file)
        Scene s("save");
        GameObject* mob = Mob(s, "Orc Chief", 90.0f, 100.0f, 12);
        const Plate before = PlateOf(PlateOn(s, mob));
        CHECK(before.name && before.level && before.bar);
        const std::string text = SceneSerializer::Serialize(s);
        CHECK(text.find("owner.name") != std::string::npos);                // the format of the name label
        CHECK(text.find("owner.level") != std::string::npos);
        CHECK(text.find("owner.healthFrac") != std::string::npos);          // the bar's variable
        Scene t("load");
        std::string err;
        CHECK(SceneSerializer::Deserialize(t, text, &err));
        GameObject* mob2 = t.Find("Orc Chief");
        CHECK(mob2 != nullptr);
        GameObject* plateObj = nullptr;
        for (const auto& o : t.Objects()) if (o->name == "Nameplate") plateObj = o.get();
        CHECK(plateObj != nullptr);
        if (plateObj && mob2) {
            CHECK(plateObj->transform->Parent() && plateObj->transform->Parent()->gameObject == mob2);   // still its child
            const Plate p = PlateOf(plateObj);
            CHECK(p.name && p.level && p.bar);
            auto* ws = plateObj->GetComponent<WorldSpaceUI>();
            CHECK(ws && ws->constantSize);                                    // and still at its design size
            t.Start();
            mob2->GetComponent<HealthStat>()->health = 25.0f;                 // Start fills it to the maximum
            Tick(t);
            if (p.name && p.level && p.bar) {
                CHECK(p.name->text == "Orc Chief");
                CHECK(p.level->text == "Lv 12");
                CHECK_NEAR(p.bar->value, 0.25f, 1e-4f);
                CHECK(p.name->autoSize && p.level->autoSize);                 // Best Fit (what keeps long texts inside) round-trips too
                CHECK_NEAR(p.name->autoSizeMax, np::kNameFont / 8.0f, 1e-4f);
            }
        }
    }

    ActionList::ResetVars();
    TEST_MAIN_RESULT();
}
