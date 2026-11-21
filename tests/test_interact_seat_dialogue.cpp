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

    // ---- A scene with no CinemachineBrain: after the talk the main camera is the player's again ----
    // The runner adds a brain to show the shot. When the talk ends it must take it away again and put the
    // camera back where it was, or the brain keeps driving the camera to the shot for good.
    {
        Scene s("nobrain"); s.physicsEnabled = false;
        GameObject* camGo = s.CreateGameObject("Main Camera");
        camGo->transform->SetPosition({0.0f, 1.6f, 5.0f});
        auto* cam = camGo->AddComponent<Camera>();
        cam->fieldOfView = 60.0f;
        s.mainCamera = cam;
        GameObject* shotGo = s.CreateGameObject("Shot");
        shotGo->transform->SetPosition({3.0f, 2.0f, 0.0f});
        auto* vc = shotGo->AddComponent<VirtualCamera>();
        vc->fieldOfView = 35.0f;
        GameObject* npc = s.CreateGameObject("Npc");
        auto* dr = npc->AddComponent<DialogueRunner>();
        dr->drawUI = false; dr->startMode = 1; dr->typewriterSpeed = 0.0f;
        dr->shotBlend = 0.0f;   // a cut, so the shot is reached in one frame
        DialogueGraph g;
        auto& l0 = g.Add(DialogueNode::Kind::Line, 0, 0);
        l0.speaker = "Npc"; l0.text = "Over here."; l0.next = 1;
        g.Add(DialogueNode::Kind::End, 100, 0);
        g.start = 0;
        dr->SetGraph(g);
        dr->SetCameraShot(0, "Shot");
        s.Start();
        s.Update(0.016f);
        CHECK(camGo->GetComponent<CinemachineBrain>() == nullptr);
        CHECK(dr->StartDialogue());
        for (int i = 0; i < 3; ++i) s.Update(0.016f);
        CHECK(camGo->GetComponent<CinemachineBrain>() != nullptr);
        CHECK(Vec3::Distance(camGo->transform->Position(), {3.0f, 2.0f, 0.0f}) < 1e-3f);   // the shot is live
        dr->Advance();
        CHECK(!dr->IsActive());
        for (int i = 0; i < 3; ++i) s.Update(0.016f);
        auto* br = camGo->GetComponent<CinemachineBrain>();
        CHECK(br == nullptr || !br->enabled);                                               // the runner's brain is gone
        CHECK(Vec3::Distance(camGo->transform->Position(), {0.0f, 1.6f, 5.0f}) < 1e-3f);   // back where it was
        CHECK_NEAR(cam->fieldOfView, 60.0f, 1e-3f);
        // Whatever drives the camera now (here: the test) is not overridden by a leftover brain.
        camGo->transform->SetPosition({7.0f, 0.0f, 0.0f});
        for (int i = 0; i < 3; ++i) s.Update(0.016f);
        CHECK(Vec3::Distance(camGo->transform->Position(), {7.0f, 0.0f, 0.0f}) < 1e-3f);
        // A second talk works the same way (the brain comes back for the shot and goes again).
        CHECK(dr->StartDialogue());
        for (int i = 0; i < 2; ++i) s.Update(0.016f);
        CHECK(Vec3::Distance(camGo->transform->Position(), {3.0f, 2.0f, 0.0f}) < 1e-3f);
        dr->Stop();
        for (int i = 0; i < 2; ++i) s.Update(0.016f);
        CHECK(Vec3::Distance(camGo->transform->Position(), {7.0f, 0.0f, 0.0f}) < 1e-3f);
    }

    // ---- The seated body is destroyed (not killed): the seat lets go of it at once ----
    {
        SeatWorld w;
        w.Press('e');
        CHECK(w.seat->occupied && w.seat->Occupant() == w.player);
        for (int i = 0; i < 40; ++i) w.s.Update(0.016f);
        w.s.Destroy(w.player);
        w.s.Update(0.016f);                 // the object goes at the end of this frame
        CHECK(w.seat->Occupant() == nullptr);   // no pointer to the freed body is kept
        CHECK(!w.seat->occupied);
        w.s.Update(0.016f);
        w.s.Update(0.016f);
        CHECK(!w.seat->occupied && w.seat->Occupant() == nullptr);
    }

    // ---- Logic Button: the controller's X turns pieces in build mode, so it does not press there ----
    {
        Scene s("button"); s.physicsEnabled = false;
        GameObject* camGo = s.CreateGameObject("Main Camera");   // at the origin, looking down -Z
        s.mainCamera = camGo->AddComponent<Camera>();
        GameObject* btn = s.CreateGameObject("Button");
        btn->transform->SetPosition({0.0f, 0.0f, -2.0f});
        btn->AddComponent<BoxCollider3D>();
        auto* ls = btn->AddComponent<LogicSource>();
        ls->kind = LogicSource::Kind::Button;
        ls->toggle = true;
        auto* bm = s.CreateGameObject("Builder")->AddComponent<BuildMode>();
        s.Start();
        Input::FeedKeys({}); Input::FeedGamepad({0, 0}, 0); s.Update(0.016f);
        auto padX = [&]() {
            Input::FeedGamepad({0, 0}, 1u << 2); s.Update(0.016f);
            Input::FeedGamepad({0, 0}, 0); s.Update(0.016f);
        };
        auto keyE = [&]() {
            Input::FeedKeys({'e'}); s.Update(0.016f);
            Input::FeedKeys({}); s.Update(0.016f);
        };
        padX();
        CHECK(ls->Output());                // outside build mode the pad's X presses it
        keyE();
        CHECK(!ls->Output());               // and so does E
        bm->SetActive(true);
        s.Update(0.016f);
        CHECK(bm->Active());
        padX();
        CHECK(!ls->Output());               // in build mode the pad's X does not (it turns pieces)
        keyE();
        CHECK(ls->Output());                // E still presses it there, as before the shared action
        bm->SetActive(false);
        Input::FeedGamepad({0, 0}, 0);
    }

    // ---- Lean only steps aside for a seat that is about this player ----
    {
        Scene s("lean-seat"); s.physicsEnabled = false;
        GameObject* player = s.CreateGameObject("Player");
        player->AddComponent<Character>();
        GameObject* npc = s.CreateGameObject("Npc");
        npc->transform->SetPosition({40.0f, 0.0f, 0.0f});
        GameObject* chair = s.CreateGameObject("FarChair");
        chair->transform->SetPosition({40.0f, 0.0f, 0.0f});
        auto* seat = chair->AddComponent<Seat>();
        s.Start();
        Input::FeedKeys({}); s.Update(0.016f);
        seat->Claim(npc);                   // an NPC sits far away
        CHECK(seat->occupied && !seat->playerInRange);
        CHECK(!LeanKeyTaken(&s, 'e', player));   // not this player's seat: leaning on E still works
        seat->Release();
        seat->Claim(player);
        CHECK(LeanKeyTaken(&s, 'e', player));    // the player's own seat takes E
        seat->Release();
        // Build mode's delete key (X) is also the new lean-right default: in build mode it deletes, not leans.
        auto* bm = s.CreateGameObject("Builder")->AddComponent<BuildMode>();
        s.Update(0.016f);
        CHECK(!LeanKeyTaken(&s, bm->bindings.removeKey, player));
        bm->SetActive(true);
        s.Update(0.016f);
        CHECK(LeanKeyTaken(&s, bm->bindings.removeKey, player));
        FirstPersonController fp;
        CHECK(LeanKeyTaken(&s, fp.leanRightKey, player));
        bm->SetActive(false);
        s.Update(0.016f);
        CHECK(!LeanKeyTaken(&s, fp.leanRightKey, player));
    }

    TEST_MAIN_RESULT();
}
