#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

static int countNamed(Scene& s, const char* name) {
    int n = 0;
    for (const auto& up : s.Objects()) if (up && up->name == name) ++n;
    return n;
}

// The pause menu builds EDITABLE UI objects in the scene (not spawned at Play),
// they serialize, and Play adopts them (no duplicate) + starts hidden.
int main() {
    RUN_SUITE("pause_menu");

    Scene s("S");
    auto* pm = s.CreateGameObject("PauseHolder")->AddComponent<PauseMenu>();
    CHECK(!pm->HasUI());
    pm->EnsureBuilt();                       // editor does this when you add the component
    CHECK(pm->HasUI());
    CHECK(countNamed(s, "PauseMenu UI") == 1);
    CHECK(countNamed(s, "Pause_Card") == 1);
    CHECK(s.Find("Pause_Resume") != nullptr);   // real, editable button objects exist
    CHECK(s.Find("Pause_Quit") != nullptr);

    // Round-trip: the UI persists in the scene file.
    std::string text = SceneSerializer::Serialize(s);
    Scene b("B");
    CHECK(SceneSerializer::Deserialize(b, text));
    CHECK(countNamed(b, "PauseMenu UI") == 1);
    CHECK(countNamed(b, "Pause_Card") == 1);

    // Play: the PauseMenu adopts the existing UI (no second copy) and starts hidden.
    b.Start();
    b.Update(0.016f);
    CHECK(countNamed(b, "PauseMenu UI") == 1);   // adopted, NOT rebuilt/duplicated
    CHECK(countNamed(b, "Pause_Card") == 1);
    GameObject* root = b.Find("PauseMenu UI");
    CHECK(root != nullptr);
    CHECK(root && root->active == false);        // hidden until paused

    TEST_MAIN_RESULT();
}
