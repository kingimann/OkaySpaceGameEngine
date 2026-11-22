#include "test_framework.hpp"
#include <Okay.hpp>
#include <string>

using namespace okay;

// One Player Controller (owner request, 2026-11-17): there is no separate Humanoid or First / Third Person controller
// to add. Scenes saved before that, with the old records, must still load and keep their settings. A "Humanoid" scene
// was a Character with Foot IK and Root Motion plus a third-person controller (tpctrl); there never was a Humanoid
// controller record. Written from the scene file format in SceneSerializer.cpp, not from another project.

static std::string OldHumanoidText() {
    Scene a("old humanoid");
    GameObject* p = Templates::BuildHumanoidBody(a, {0, 1, 0}, "Player");   // rig + capsule + foot IK + root motion
    CHECK(p != nullptr);
    // What the Humanoid starter saved before the Player Controller: a bare third-person controller and a switched-off
    // view toggle (that layout is still written as the separate tpctrl / playerview records).
    auto* tp = p->AddComponent<ThirdPersonController>();
    tp->walkSpeed = 3.1f; tp->runSpeed = 6.2f; tp->jumpForce = 5.5f; tp->mouseSensitivity = 0.4f;
    Templates::AddViewToggle(p, PlayerView::View::Third, 0.45f)->allowToggle = false;
    const std::string text = SceneSerializer::Serialize(a);
    CHECK(text.find("tpctrl") != std::string::npos);
    CHECK(text.find("playercontroller") == std::string::npos);
    return text;
}

int main() {
    RUN_SUITE("one controller");

    // --- An old Humanoid scene (bare third-person controller + foot IK + root motion) loads as a Player Controller ---
    {
        Scene b("b");
        CHECK(SceneSerializer::Deserialize(b, OldHumanoidText()));
        GameObject* p = b.Find("Player");
        CHECK(p != nullptr);
        if (!p) return 1;
        PlayerController* pc = PlayerController::Of(p);
        CHECK(pc != nullptr);                                           // wrapped, not dropped
        if (pc) CHECK(pc->viewMode() == PlayerController::ViewMode::Third);   // the view it had
        auto* tp = p->GetComponent<ThirdPersonController>();
        CHECK(tp != nullptr);
        if (tp) {                                                       // settings kept
            CHECK_NEAR(tp->walkSpeed, 3.1f, 1e-4f); CHECK_NEAR(tp->runSpeed, 6.2f, 1e-4f);
            CHECK_NEAR(tp->jumpForce, 5.5f, 1e-4f); CHECK_NEAR(tp->mouseSensitivity, 0.4f, 1e-4f);
        }
        CHECK(p->GetComponent<FootIK>() != nullptr);                    // the humanoid parts are untouched
        CHECK(p->GetComponent<RootMotion>() != nullptr);
        CHECK(p->GetComponent<Character>() != nullptr);
        CHECK(p->GetComponent<FirstPersonController>() == nullptr);     // no first person invented
        // Saving it again writes the one-controller form and loads back the same.
        const std::string t2 = SceneSerializer::Serialize(b);
        CHECK(t2.find("playercontroller") != std::string::npos);
        CHECK(t2.find("tpctrl") == std::string::npos);
        Scene c("c");
        CHECK(SceneSerializer::Deserialize(c, t2));
        CHECK(SceneSerializer::Serialize(c) == t2);
        GameObject* q = c.Find("Player");
        CHECK(q && PlayerController::Of(q) && q->GetComponent<FootIK>());
    }

    // --- Old separate first AND third person controllers on one object become one Player Controller in Both ---
    {
        Scene a("a");
        GameObject* src = a.CreateGameObject("Player");
        src->AddComponent<FirstPersonController>()->walkSpeed = 3.3f;
        src->AddComponent<ThirdPersonController>()->walkSpeed = 2.2f;
        Templates::AddViewToggle(src, PlayerView::View::Third, 0.45f)->allowToggle = false;
        const std::string text = SceneSerializer::Serialize(a);
        CHECK(text.find("fpctrl") != std::string::npos && text.find("tpctrl") != std::string::npos);   // the old records
        Scene b("b");
        CHECK(SceneSerializer::Deserialize(b, text));
        GameObject* p = b.Find("Player");
        PlayerController* pc = p ? PlayerController::Of(p) : nullptr;
        CHECK(pc != nullptr);
        if (pc) CHECK(pc->viewMode() == PlayerController::ViewMode::Both);
        auto* fp = p ? p->GetComponent<FirstPersonController>() : nullptr;
        auto* tp = p ? p->GetComponent<ThirdPersonController>() : nullptr;
        CHECK(fp && tp);
        if (fp && tp) { CHECK_NEAR(fp->walkSpeed, 3.3f, 1e-4f); CHECK_NEAR(tp->walkSpeed, 2.2f, 1e-4f); }
    }

    // --- The editor's one create entry is a Both-view Player Controller (what GameObject > Player > Player Controller adds) ---
    {
        Scene s("s");
        GameObject* p = Templates::AddThirdPersonPlayer(s);
        PlayerController* pc = PlayerController::Of(p);
        CHECK(pc != nullptr);
        if (pc) CHECK(pc->viewMode() == PlayerController::ViewMode::Both);
    }

    TEST_MAIN_RESULT();
}
