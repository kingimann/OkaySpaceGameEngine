#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

// Every humanoid controller can opt into foot IK (footIK flag) and it wires a
// FootIK to the Character's leg bones at Start. Plus controller serialization.
template <typename Ctrl>
static void CheckWires(const char* name) {
    Scene s(name);
    auto* go = s.CreateGameObject("Player");
    auto* pc = go->AddComponent<Character>(); pc->Apply();
    auto* ctrl = go->AddComponent<Ctrl>();
    ctrl->footIK = true;
    s.Start();
    auto* fik = go->GetComponent<FootIK>();
    CHECK(fik != nullptr);                                  // foot IK was attached
    if (fik) {
        CHECK(fik->leftFoot  == pc->Part(11)->transform);  // wired to the real leg bones
        CHECK(fik->rightFoot == pc->Part(14)->transform);
        CHECK(fik->pelvis    == pc->Part(0)->transform);
    }
}

int main() {
    RUN_SUITE("controllers_ik");

    CheckWires<ThirdPersonController>("tp");
    CheckWires<TopDownController>("td");
    CheckWires<ClickToMoveController>("ctm");
    CheckWires<NPCController>("npc");
    CheckWires<CharacterController3D>("cc3");
    {   // FirstPerson: avoid cursor capture in headless
        Scene s("fp");
        auto* go = s.CreateGameObject("Player");
        auto* pc = go->AddComponent<Character>(); pc->Apply();
        auto* fp = go->AddComponent<FirstPersonController>();
        fp->lockCursor = false; fp->footIK = true;
        s.Start();
        CHECK(go->GetComponent<FootIK>() != nullptr);
    }

    // footIK off (default) wires nothing.
    {
        Scene s("off");
        auto* go = s.CreateGameObject("Player");
        go->AddComponent<Character>()->Apply();
        go->AddComponent<ThirdPersonController>();          // footIK defaults false
        s.Start();
        CHECK(go->GetComponent<FootIK>() == nullptr);
    }

    // ---- CharacterController3D: upgraded fields round-trip ----
    {
        Scene s("cc3ser");
        auto* go = s.CreateGameObject("P");
        auto* cc = go->AddComponent<CharacterController3D>();
        cc->speed = 4.0f; cc->runSpeed = 9.0f; cc->sprintKey = 'b';
        cc->acceleration = 30.0f; cc->deceleration = 20.0f; cc->airControl = 0.25f;
        cc->driveAnimation = false; cc->footIK = true;
        std::string text = SceneSerializer::Serialize(s);
        Scene s2("s2");
        CHECK(SceneSerializer::Deserialize(s2, text));
        auto* l = s2.Find("P")->GetComponent<CharacterController3D>();
        CHECK(l && std::fabs(l->runSpeed - 9.0f) < 1e-3f);
        CHECK(l && l->sprintKey == 'b');
        CHECK(l && std::fabs(l->airControl - 0.25f) < 1e-3f);
        CHECK(l && !l->driveAnimation && l->footIK);
    }

    // ---- FirstPersonController footIK round-trips ----
    {
        Scene s("fpser");
        auto* go = s.CreateGameObject("P");
        auto* fp = go->AddComponent<FirstPersonController>();
        fp->footIK = true; fp->runSpeed = 9.5f;
        std::string text = SceneSerializer::Serialize(s);
        Scene s2("s2");
        CHECK(SceneSerializer::Deserialize(s2, text));
        auto* l = s2.Find("P")->GetComponent<FirstPersonController>();
        CHECK(l && l->footIK);
        CHECK(l && std::fabs(l->runSpeed - 9.5f) < 1e-3f);
    }

    // ---- Bug fixes: TPS sprint enabled by default; ClickToMove keeps the cursor ----
    {
        // ThirdPersonShooter: sprint key defaults to Shift (was 0 = disabled).
        ThirdPersonShooterController tps;
        CHECK(tps.sprintKey == Input::KeyShift);

        // ClickToMove: with showCursor on, Start releases any cursor lock (you click).
        Scene s("ctm_cursor");
        Cursor::Capture(true);                       // simulate the runtime's default lock
        auto* go = s.CreateGameObject("Player");
        auto* cm = go->AddComponent<ClickToMoveController>();
        CHECK(cm->showCursor);                        // default on
        s.Start();
        CHECK(!Cursor::IsLocked());                   // cursor freed so the pointer shows

        // Round-trips.
        std::string text = SceneSerializer::Serialize(s);
        Scene s2("s2");
        CHECK(SceneSerializer::Deserialize(s2, text));
        auto* l = s2.Find("Player")->GetComponent<ClickToMoveController>();
        CHECK(l && l->showCursor);
    }

    TEST_MAIN_RESULT();
}
