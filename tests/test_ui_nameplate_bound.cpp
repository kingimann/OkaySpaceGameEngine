// UI nameplate bound to the character it belongs to: `owner.` keys in UITextBind / UIBarBind (UIOwnerData). Each
// nameplate made a child of a character shows that character's name, Stats level and health, two characters show
// their own values, a character without some data never shows the player's, and a nameplate with no owner keeps the
// old preview behaviour (the player's published health). Written for this engine; no other project's code was used.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/UIOwnerData.hpp"
#include "okay/Scene/UIGallery.hpp"
#include <cmath>
#include <string>

using namespace okay;

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
            CHECK(std::fabs(ps.bar->value - 0.9f) > 0.1f);                   // no health: not the player's 0.9
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

    ActionList::ResetVars();
    TEST_MAIN_RESULT();
}
