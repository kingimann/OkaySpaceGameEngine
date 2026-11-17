#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/Interactable.hpp"
#include "okay/Modules/Weapon.hpp"

using namespace okay;

int main() {
    RUN_SUITE("weapon_contract");

    // ---- the weapon contract over both weapon components
    {
        Scene s("T");
        GameObject* g = s.CreateGameObject("Gun");
        auto* gun = g->AddComponent<RangedWeapon>();
        gun->magazine = 3; gun->reserve = 6; gun->fireRate = 1000.0f; gun->reloadTime = 0.2f; gun->fireButton = -1; gun->fireKey = 0;
        GameObject* t = s.CreateGameObject("Turret");
        auto* td = t->AddComponent<TopDownShooterController>();
        td->magazineSize = 4; td->fireRate = 1000.0f;
        GameObject* none = s.CreateGameObject("Rock");
        s.Start();
        auto w = weapon::Find(g);
        CHECK(w && std::string(w->Model()) == "RangedWeapon");
        CHECK(w->MagazineSize() == 3 && w->Rounds() == 3 && w->Spare() == 6 && !w->IsEmpty());
        CHECK(w->Fire() && w->Rounds() == 2);
        s.Update(0.01f);
        CHECK(w->Fire());
        s.Update(0.01f);
        CHECK(w->Fire() && w->Rounds() == 0 && w->IsEmpty());
        s.Update(0.3f);
        CHECK(!w->IsReloading() && w->Rounds() == 3 && w->Spare() == 3);
        auto w2 = weapon::Find(t);
        CHECK(w2 && std::string(w2->Model()) == "TopDownShooterController");
        CHECK(w2->MagazineSize() == 4 && w2->Rounds() == 4 && w2->Spare() == -1);
        CHECK(weapon::Find(none) == nullptr);
    }

    // ---- Interactable + InteractionSensor
    {
        Scene s("T");
        GameObject* hero = s.CreateGameObject("Hero");
        hero->AddComponent<InteractionSensor>();
        GameObject* lever = s.CreateGameObject("Lever");
        lever->transform->localPosition = Vec3{1, 0, 0};
        auto* ia = lever->AddComponent<Interactable>();
        ia->prompt = "pull the lever"; ia->oneShot = true;
        s.Start();
        InputActions::Restore();
        s.Update(0.016f);
        auto* sensor = hero->GetComponent<InteractionSensor>();
        CHECK(sensor->Target() == lever && sensor->Prompt() == "E to pull the lever");
        Input::FeedKeys({'e'}); s.Update(0.016f);
        Input::FeedKeys({});    s.Update(0.016f);
        CHECK(ia->useCount == 1);
        Input::FeedKeys({'e'}); s.Update(0.016f);
        Input::FeedKeys({});    s.Update(0.016f);
        CHECK(ia->useCount == 1);                                   // one-shot: used up
        CHECK(sensor->Target() == nullptr);                         // and no longer offered

        // hold: needs the key held long enough
        ia->oneShot = false; ia->holdSeconds = 0.5f; ia->useCount = 0;
        s.Update(0.016f);
        Input::FeedKeys({'e'});
        s.Update(0.2f);
        CHECK(ia->useCount == 0 && sensor->HoldProgress() > 0.3f);
        Input::FeedKeys({});
        s.Update(0.016f);
        CHECK(sensor->HoldProgress() == 0.0f && ia->useCount == 0);   // let go early: nothing
        Input::FeedKeys({'e'});
        s.Update(0.3f); s.Update(0.3f);
        CHECK(ia->useCount == 1);
        Input::FeedKeys({});

        // far away: not offered
        lever->transform->localPosition = Vec3{20, 0, 0};
        s.Update(0.016f);
        CHECK(sensor->Target() == nullptr);

        // saves and loads
        lever->transform->localPosition = Vec3{1, 0, 0};
        ia->prompt = "open the big door"; ia->holdSeconds = 1.5f; ia->range = 3.0f;
        Scene back("B");
        std::string err;
        CHECK(SceneSerializer::Deserialize(back, SceneSerializer::Serialize(s), &err));
        auto* b = back.Find("Lever") ? back.Find("Lever")->GetComponent<Interactable>() : nullptr;
        CHECK(b && b->prompt == "open the big door" && b->holdSeconds == 1.5f && b->range == 3.0f && !b->oneShot);
        CHECK(back.Find("Hero") && back.Find("Hero")->GetComponent<InteractionSensor>());
    }

    // ---- module list in the join handshake
    {
        ModuleRegistry r = ModuleRegistry::Empty();
        r.AddText("name=core\ncore=1\n", true);
        r.AddText("name=a\nrequires=core\n", true);
        r.AddText("name=b\nrequires=core\n", true);
        r.AddText("name=c\nrequires=core\n", false);
        CHECK(r.NetSignature() == "a,b,core");
        CHECK(r.DescribeMismatch("a,b,core").empty());
        std::string m = r.DescribeMismatch("a,c,core");
        CHECK(m.find("missing b") != std::string::npos && m.find("has c") != std::string::npos);
        CHECK(!r.DescribeMismatch("a,core").empty());
        CHECK(ModuleRegistry::Get().DescribeMismatch(ModuleRegistry::Get().NetSignature()).empty());
    }
    TEST_MAIN_RESULT();
}
