// Inventory + Procedural Object Holding: a main-hand item goes into the character's
// hands (and is let go when it comes off), and a pickup marked "hold in hands" is
// carried in the hands instead of going into the bag.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <okay/Components/Equipment.hpp>
#include <okay/Components/ItemPickup.hpp>
#include <okay/Components/ProceduralObjectHolding.hpp>
#include <cstdio>
#include <string>

using namespace okay;

static void Press(Scene& s, char key) {
    Input::FeedKeys({key}); s.Update(0.016f);
    Input::FeedKeys({});    s.Update(0.016f);
}

int main() {
    RUN_SUITE("inventory_holding");
    {
        Scene p("P");
        GameObject* sword = p.CreateGameObject("SwordModel");
        sword->AddComponent<BoxCollider3D>();
        SceneSerializer::SaveObjectToFile(*sword, "test_hold_sword.okayprefab");
    }
    ItemDatabase& db = ItemDatabase::Get();
    db.Clear();
    db.LoadText("okayitems 1\n"
                "item \"sword\"\n  name \"Sword\"\n  equip mainhand\n  prefab \"test_hold_sword.okayprefab\"\nend\n"
                "item \"crate\"\n  name \"Crate\"\nend\n");

    // --- A main-hand item goes into the hands; unequipping lets go of it ---
    {
        Scene s("T");
        GameObject* hero = s.CreateGameObject("Hero");
        hero->AddComponent<Character>();
        auto* hands = hero->AddComponent<ProceduralObjectHolding>();
        auto* inv = hero->AddComponent<Inventory>(); inv->capacity = 9;
        auto* eq = hero->AddComponent<Equipment>();
        s.Start();
        inv->Add("sword", 1);
        const int hand = eq->Find("Main Hand");
        CHECK(eq->EquipFrom(*inv, 0, hand).Ok());
        GameObject* shown = eq->Shown(hand);
        CHECK(shown && hands->Held() == shown);
        CHECK(shown && !shown->IsSelfOrDescendantOf(hero));          // held, not parented to a bone
        s.Update(0.016f);
        CHECK(hands->Held() == eq->Shown(hand));
        CHECK(eq->Unequip(hand, *inv).Ok() && !hands->IsHolding() && inv->Count("sword") == 1);
        // Without a holding component it sits at the hand socket as before.
        hero->RemoveComponent(hands);
        CHECK(eq->EquipFrom(*inv, 0, hand).Ok());
        GameObject* atSocket = eq->Shown(hand);
        CHECK(atSocket && atSocket->IsSelfOrDescendantOf(hero));
    }

    // --- "Hold in hands" pickups are carried, not stored; while held they aren't collected ---
    {
        Scene s("T");
        GameObject* hero = s.CreateGameObject("Hero");
        hero->tag = "Player";
        auto* hands = hero->AddComponent<ProceduralObjectHolding>();
        auto* inv = hero->AddComponent<Inventory>(); inv->capacity = 9;
        GameObject* crate = s.CreateGameObject("Crate");
        crate->transform->localPosition = Vec3{1, 0, 0};
        auto* pk = crate->AddComponent<ItemPickup>();
        pk->item = "crate"; pk->interactKey = 'e'; pk->holdInHands = true; pk->pickupOnTouch = true;
        GameObject* apple = s.CreateGameObject("Loose");
        apple->transform->localPosition = Vec3{-1, 0, 0};
        auto* pa = apple->AddComponent<ItemPickup>();
        pa->item = "crate"; pa->interactKey = 'f';                     // not marked: goes in the bag
        s.Start();
        Press(s, 'e');
        CHECK(hands->Held() == crate && inv->Count("crate") == 0 && pk->Held());
        CHECK(pk->GiveTo(hero) == 0 && inv->Count("crate") == 0);      // touching it while carried doesn't bag it
        Press(s, 'f');
        CHECK(inv->Count("crate") == 1);
        hands->Drop();
        CHECK(!pk->Held() && pk->GiveTo(hero) == 1 && inv->Count("crate") == 2);

        // The flag saves and loads.
        Scene t("S");
        auto* p2 = t.CreateGameObject("C")->AddComponent<ItemPickup>();
        p2->item = "crate"; p2->holdInHands = true;
        Scene back("B");
        std::string err;
        CHECK(SceneSerializer::Deserialize(back, SceneSerializer::Serialize(t), &err));
        GameObject* c = back.Find("C");
        CHECK(c && c->GetComponent<ItemPickup>() && c->GetComponent<ItemPickup>()->holdInHands);
    }

    std::remove("test_hold_sword.okayprefab");
    TEST_MAIN_RESULT();
}
