#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

static int countRigs(Scene& s) {
    int n = 0;
    for (const auto& o : s.Objects()) if (o && o->name == "Rig") ++n;
    return n;
}

// The part rig is a real, editor-built set of objects (not spawned at Play) and is a
// GENERATED view of the Character: building it twice never duplicates, it isn't
// serialized, and it rebuilds (once) on load. RemoveParts tears it back down.
int main() {
    RUN_SUITE("char_editor_parts");

    Scene s("A");
    auto* go = s.CreateGameObject("Hero");
    auto* ch = go->AddComponent<Character>();
    ch->Apply();

    // Editor "Separate Into Parts": build the rig right now (no Play).
    ch->separateParts = true;
    ch->BuildParts();
    CHECK(ch->PartsBuilt());
    CHECK(countRigs(s) == 1);
    CHECK(ch->Part(6) != nullptr);                 // right upper arm exists as an object
    // The baked mesh is hidden in favour of the parts.
    CHECK(go->GetComponent<MeshRenderer>() && !go->GetComponent<MeshRenderer>()->enabled);

    // Building again adopts the existing rig — never a second one.
    ch->BuildParts();
    CHECK(countRigs(s) == 1);

    // The rig is NOT serialized (it's regenerated from the Character on load).
    std::string text = SceneSerializer::Serialize(s);
    CHECK(text.find("\"Rig\"") == std::string::npos);

    // Round-trip: load has no rig until it's rebuilt, then exactly one.
    Scene b("B");
    CHECK(SceneSerializer::Deserialize(b, text));
    CHECK(countRigs(b) == 0);
    auto* lch = b.Find("Hero") ? b.Find("Hero")->GetComponent<Character>() : nullptr;
    CHECK(lch != nullptr);
    if (lch) {
        CHECK(lch->separateParts);
        lch->BuildParts();
        CHECK(countRigs(b) == 1);
        CHECK(lch->Part(2) != nullptr);            // head

        // RemoveParts (editor toggle off) destroys the rig and shows the mesh again.
        lch->separateParts = false;
        lch->RemoveParts();
        CHECK(!lch->PartsBuilt());
        b.Update(0.0f);                            // flush the deferred destroy
        CHECK(countRigs(b) == 0);
        CHECK(b.Find("Hero")->GetComponent<MeshRenderer>()->enabled);
    }

    TEST_MAIN_RESULT();
}
