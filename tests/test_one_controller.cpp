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
    std::string text = SceneSerializer::Serialize(a);
    const std::size_t at = text.find("Player");
    const std::size_t eol = at == std::string::npos ? at : text.find('\n', at);
    CHECK(eol != std::string::npos);
    // The old controller record, straight after the object's header line: walk 3.1, run 6.2, jump 5.5, mouse 0.4.
    text.insert(eol + 1, "  tpctrl 3.1 6.2 5.5 0.4 12 5 1.5 1 1\n");
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
        a.CreateGameObject("Player");
        std::string text = SceneSerializer::Serialize(a);
        const std::size_t eol = text.find('\n', text.find("Player"));
        text.insert(eol + 1, "  fpctrl 3.3 9.1 7.7 0.33 1 1 1\n  tpctrl 2.2 5.5 6 0.2 12 5 1.5 1 1\n");
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
