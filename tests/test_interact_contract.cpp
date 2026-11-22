#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Modules/ISeat.hpp"
#include "okay/Modules/Interact.hpp"

using namespace okay;


int main() {
    RUN_SUITE("interact_contract");

    // The key itself (the shared Interact action) is tested by main's test_interact_seat_dialogue; this test is the finding.
    ItemPickup::ClearInteractBlock();

    ItemDatabase::Get().Clear();
    ItemDatabase::Get().LoadText("okayitems 1\nitem \"crate\"\n  name \"Crate\"\nend\n");

    // ---- finding and using: pickup, chest-like, talker and seats all answer to the same questions
    {
        Scene s("T");
        GameObject* hero = s.CreateGameObject("Hero"); hero->tag = "Player";
        auto* inv = hero->AddComponent<Inventory>(); inv->capacity = 9;
        GameObject* crate = s.CreateGameObject("Crate");
        crate->transform->localPosition = Vec3{1, 0, 0};
        auto* pk = crate->AddComponent<ItemPickup>();
        pk->item = "crate"; pk->interactKey = 'e'; pk->pickupOnTouch = false; pk->interactRange = 2.0f;
        GameObject* far = s.CreateGameObject("Far");
        far->transform->localPosition = Vec3{30, 0, 0};
        auto* pf = far->AddComponent<ItemPickup>();
        pf->item = "crate"; pf->interactKey = 'e'; pf->pickupOnTouch = false;
        GameObject* chair = s.CreateGameObject("Chair");
        chair->transform->localPosition = Vec3{0, 0, 0.5f};
        chair->AddComponent<Seat>();
        s.Start();

        auto items = interact::Find(crate);
        CHECK(items.size() == 1 && std::string(items[0]->Model()) == "ItemPickup");
        CHECK(items[0]->Prompt(InteractContext{hero}).find("E to pick up") == 0);
        CHECK(items[0]->Range() == 2.0f);
        auto seats = interact::Find(chair);
        CHECK(seats.size() == 1 && std::string(seats[0]->Model()) == "Seat");

        float d = 0.0f;
        auto near = interact::Nearest(s, hero, &d);
        CHECK(near != nullptr);                                           // the chair is nearer (0.5 m) than the crate (1 m)
        CHECK(near && std::string(near->Model()) == "Seat" && d < 0.6f);
        chair->active = false;
        near = interact::Nearest(s, hero, &d);
        CHECK(near && near->Object() == crate);
        CHECK(!pf->enabled || interact::Find(far)[0]->CanInteract(InteractContext{hero}) == false);   // out of range
        CHECK(near && near->Interact(InteractContext{hero}));
        CHECK(inv->Count("crate") == 1);

        // seats: the contract sees the chair and the occupant
        chair->active = true;
        auto cs = seat::Find(chair);
        CHECK(cs && !cs->IsOccupied() && !cs->IsDriver());
        CHECK(seat::Find(crate) == nullptr);
        auto vs = s.CreateGameObject("Car")->AddComponent<VehicleSeat>();
        CHECK(vs != nullptr);
        auto cv = seat::Find(vs->gameObject);
        CHECK(cv && std::string(cv->Model()) == "VehicleSeat" && cv->IsDriver());
        CHECK(seat::SeatOf(hero) == nullptr);
    }
    TEST_MAIN_RESULT();
}
