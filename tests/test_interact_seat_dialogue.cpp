// Feature map 4.11 and 4.8:
//  - one shared "Interact" action (default E, pad X) that seats, dialogue and pickups read, and
//    first-person lean-right moved off E, so one press sits without also leaning;
//  - a seated body that dies is put at the seat's exit point;
//  - a dialogue line can name a virtual camera shot: it blends there when the line starts and back
//    when the conversation ends; the shots are saved in the scene record.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/Seat.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/FirstPersonController.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include "okay/Components/DialogueGraph.hpp"
#include "okay/Components/DialogueRunner.hpp"
#include "okay/Components/VirtualCamera.hpp"
#include "okay/Components/CinemachineBrain.hpp"
#include "okay/Input/InputActions.hpp"
#include "okay/Input/GameInput.hpp"
#include "okay/Core/Prefs.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include <string>

using namespace okay;

namespace {

struct SeatWorld {
    Scene s{"seat"};
    GameObject* player = nullptr;
    GameObject* chair = nullptr;
    Seat* seat = nullptr;
    FirstPersonController* fp = nullptr;
    SeatWorld() {
        s.physicsEnabled = false;
        player = s.CreateGameObject("Player");
        player->AddComponent<Character>();
        fp = player->AddComponent<FirstPersonController>();
        GameObject* cam = s.CreateGameObject("Cam");
        cam->transform->SetParent(player->transform);
        cam->AddComponent<Camera>();
        chair = s.CreateGameObject("Chair");
        chair->transform->SetPosition({0.5f, 0.0f, 0.0f});   // in reach (useRadius 1.2)
        seat = chair->AddComponent<Seat>();
        seat->exitPoint = {0.0f, 0.0f, 2.0f};
        s.Start();
        Input::FeedKeys({});
        s.Update(0.016f);
    }
    void Press(char k) {
        Input::FeedKeys({k});
        s.Update(0.016f);
        Input::FeedKeys({});
    }
};

} // namespace

int main() {
    RUN_SUITE("interact_seat_dialogue");
    Prefs::Delete("input.interact");
    InputActions::Restore();

    // ---- The shared Interact action exists and is E / pad X by default ----
    {
        const InputActions::Action* a = InputActions::Find("Interact");
        CHECK(a != nullptr);
        CHECK(IsInteractKey('e'));
        CHECK(!IsInteractKey('f'));
    }

    // ---- First-person lean-right is no longer on the Interact key ----
    {
        FirstPersonController fp;
        CHECK(fp.leanRightKey != 0);
        CHECK(fp.leanRightKey != 'e');
        CHECK(!IsInteractKey(fp.leanRightKey));
        CHECK(fp.leanRightKey != fp.leanLeftKey);
    }

    // ---- A seat in range and lean enabled: Interact sits, and does not lean ----
    {
        SeatWorld w;
        CHECK(w.seat->playerInRange);
        w.Press('e');
        CHECK(w.seat->occupied && w.seat->Occupant() == w.player);
        for (int i = 0; i < 10; ++i) w.s.Update(0.016f);
        CHECK(w.fp->lean() == 0.0f);
    }
    // An old scene that saved lean-right on E keeps it, and the seat still owns that press.
    {
        SeatWorld w;
        w.fp->leanRightKey = 'e';
        for (int i = 0; i < 20; ++i) { Input::FeedKeys({'e'}); w.s.Update(0.016f); }
        Input::FeedKeys({});
        CHECK(w.seat->occupied && w.seat->Occupant() == w.player);
        CHECK(w.fp->lean() < 0.05f);
    }
    // The saved lean key survives a save and load.
    {
        Scene s("lean");
        auto* fp = s.CreateGameObject("FP")->AddComponent<FirstPersonController>();
        fp->leanRightKey = 'e';
        Scene s2("lean2");
        SceneSerializer::Deserialize(s2, SceneSerializer::Serialize(s));
        auto* fp2 = s2.Find("FP") ? s2.Find("FP")->GetComponent<FirstPersonController>() : nullptr;
        CHECK(fp2 && fp2->leanRightKey == 'e');
    }

    // ---- Rebinding Interact moves every user: seat and dialogue ----
    {
        CHECK(InputActions::Rebind("Interact", "key:f"));
        CHECK(IsInteractKey('f'));
        CHECK(!IsInteractKey('e'));
        {
            SeatWorld w;
            w.Press('e');
            CHECK(!w.seat->occupied);           // E no longer sits
            w.Press('f');
            CHECK(w.seat->occupied && w.seat->Occupant() == w.player);
        }
        {
            Scene s("talk"); s.physicsEnabled = false;
            GameObject* player = s.CreateGameObject("Player");
            player->tag = "Player";
            GameObject* npc = s.CreateGameObject("Npc");
            npc->transform->SetPosition({1.0f, 0.0f, 0.0f});
            auto* dr = npc->AddComponent<DialogueRunner>();
            dr->drawUI = false;
            DialogueGraph g;
            auto& l0 = g.Add(DialogueNode::Kind::Line, 0, 0);
            l0.speaker = "Npc"; l0.text = "Hi."; l0.next = 1;
            g.Add(DialogueNode::Kind::End, 100, 0);
            g.start = 0;
            dr->SetGraph(g);
            CHECK(dr->interactKey == 'e');      // the component still says "the Interact key"
            s.Start();
            Input::FeedKeys({}); s.Update(0.016f);
            Input::FeedKeys({'e'}); s.Update(0.016f); Input::FeedKeys({}); s.Update(0.016f);
            CHECK(!dr->IsActive());             // E is not the Interact key any more
            Input::FeedKeys({'f'}); s.Update(0.016f); Input::FeedKeys({}); s.Update(0.016f);
            CHECK(dr->IsActive());              // F is
            dr->Stop();
        }
        InputActions::Reset("Interact");
        Prefs::Delete("input.interact");
        CHECK(IsInteractKey('e'));
    }

    // ---- Death on a seat puts the body at the exit point ----
    {
        SeatWorld w;
        auto* hp = w.player->AddComponent<HealthStat>();
        w.Press('e');
        CHECK(w.seat->occupied);
        for (int i = 0; i < 40; ++i) w.s.Update(0.016f);   // the sit blend reaches the seat
        hp->Damage(1000.0f);
        CHECK(hp->IsDead());
        w.s.Update(0.016f);
        CHECK(!w.seat->occupied);
        const Vec3 exit = w.seat->ExitWorld();
        const Vec3 at = w.player->transform->Position();
        CHECK_NEAR(at.x, exit.x, 1e-3f);
        CHECK_NEAR(at.y, exit.y, 1e-3f);
        CHECK_NEAR(at.z, exit.z, 1e-3f);
    }

    // ---- A dialogue line's camera shot: blend there on the line, back at the end ----
    {
        Scene s("shots"); s.physicsEnabled = false;
        GameObject* camGo = s.CreateGameObject("Main Camera");
        auto* cam = camGo->AddComponent<Camera>();
        auto* brain = camGo->AddComponent<CinemachineBrain>();
        s.mainCamera = cam;
        auto* wide = s.CreateGameObject("Wide")->AddComponent<VirtualCamera>();
        wide->priority = 20;
        auto* close = s.CreateGameObject("CloseUp")->AddComponent<VirtualCamera>();
        close->priority = 1;
        auto* other = s.CreateGameObject("Other")->AddComponent<VirtualCamera>();
        other->priority = 1;
        GameObject* npc = s.CreateGameObject("Npc");
        auto* dr = npc->AddComponent<DialogueRunner>();
        dr->drawUI = false; dr->startMode = 1; dr->typewriterSpeed = 0.0f;
        DialogueGraph g;
        auto& l0 = g.Add(DialogueNode::Kind::Line, 0, 0);       // 0: no shot
        l0.speaker = "Npc"; l0.text = "Hello."; l0.next = 1;
        auto& l1 = g.Add(DialogueNode::Kind::Line, 100, 0);     // 1: the close-up
        l1.speaker = "Npc"; l1.text = "Look at me."; l1.next = 2;
        g.Add(DialogueNode::Kind::End, 200, 0);                 // 2
        g.start = 0;
        dr->SetGraph(g);
        dr->SetCameraShot(1, "CloseUp");
        CHECK(dr->CameraShot(1) == "CloseUp");
        CHECK(dr->CameraShot(0).empty());
        s.Start();

        CHECK(dr->StartDialogue());
        CHECK(brain->Forced() == nullptr);      // line 0 names no shot
        dr->Advance();
        CHECK(brain->Forced() == close);        // line 1 starts: the close-up
        dr->Advance();
        CHECK(!dr->IsActive());
        CHECK(brain->Forced() == nullptr);      // the end: back to choosing by priority

        // A shot a script forced before the talk is the one it goes back to.
        brain->ForceLive(other);
        CHECK(dr->StartDialogue());
        dr->Advance();
        CHECK(brain->Forced() == close);
        dr->Stop();
        CHECK(brain->Forced() == other);
        brain->ReleaseForced();

        // Saved in the scene record and read back.
        dr->shotBlend = 0.75f;
        Scene s2("shots2");
        SceneSerializer::Deserialize(s2, SceneSerializer::Serialize(s));
        auto* dr2 = s2.Find("Npc") ? s2.Find("Npc")->GetComponent<DialogueRunner>() : nullptr;
        CHECK(dr2 && dr2->cameraShots.size() == 1);
        CHECK(dr2 && dr2->CameraShot(1) == "CloseUp");
        CHECK(dr2 && dr2->shotBlend > 0.74f && dr2->shotBlend < 0.76f);
    }
    // An old scene record without shots loads with none.
    {
        Scene s("old"); s.physicsEnabled = false;
        s.CreateGameObject("Npc")->AddComponent<DialogueRunner>();
        const std::string text = SceneSerializer::Serialize(s);
        CHECK(text.find("shots") == std::string::npos);   // nothing new is written for a runner without shots
        Scene s2("old2");
        SceneSerializer::Deserialize(s2, text);
        auto* dr2 = s2.Find("Npc") ? s2.Find("Npc")->GetComponent<DialogueRunner>() : nullptr;
        CHECK(dr2 && dr2->cameraShots.empty());
        CHECK(dr2 && dr2->shotBlend < 0.0f);
    }

    TEST_MAIN_RESULT();
}
