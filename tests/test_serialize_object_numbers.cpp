// Object numbers in saved scene text (SceneSerializer::Serialize): only the objects that are written get a
// number, so an object that is never saved (a `transient` one, a Character's generated "Rig") leaves no gap.
//
// Bug: Serialize numbered every object in the scene, written or not. The rig a Character builds is not saved
// and is rebuilt on load at another place in the object list, so after Play/Stop (which reloads the scene from
// its text) every object after it got a different number, the text no longer matched the saved text and the
// editor showed the scene as changed. Loading still matches objects by these numbers (`parent N`), so the
// tests also check that parent links resolve, and that an older file with gaps in its numbers still loads.
#include "test_framework.hpp"
#include "EditorState.hpp"
#include <Okay.hpp>
#include <cstdio>
#include <string>

using namespace okay;
using okay::editor::EditorState;

namespace {

bool Contains(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

// A normal object, a transient one, then a normal one with a child.
struct Three { GameObject *a, *tmp, *b, *child; };
Three Build(Scene& s) {
    Three t{};
    t.a = s.CreateGameObject("A");
    t.tmp = s.CreateGameObject("Temp");
    t.tmp->transient = true;
    t.b = s.CreateGameObject("B");
    t.b->transform->localPosition = {1, 2, 3};
    t.child = s.CreateGameObject("Child of B");
    t.child->transform->SetParent(t.b->transform, false);
    return t;
}

void TestNumbering() {
    RUN_SUITE("object numbers: only written objects are numbered");
    Scene s("numbers");
    Build(s);
    const std::string t = SceneSerializer::Serialize(s);
    CHECK(!Contains(t, "\"Temp\""));                            // the transient object is not written
    CHECK(Contains(t, "gameobject 0 \"A\""));
    CHECK(Contains(t, "gameobject 1 \"B\""));                   // no gap where the transient one sits
    CHECK(Contains(t, "gameobject 2 \"Child of B\""));
    CHECK(Contains(t, "gameobject 2 \"Child of B\"\n") && Contains(t, "  parent 1\n"));
}

void TestTransientComesAndGoes() {
    RUN_SUITE("object numbers: a transient object coming and going leaves the text the same");
    // Saved with the transient object in the middle, then the same scene without it.
    Scene s("come-go");
    Three t = Build(s);
    const std::string with = SceneSerializer::Serialize(s);
    s.DestroyImmediate(t.tmp);
    const std::string without = SceneSerializer::Serialize(s);
    CHECK(with == without);
    // And a transient object made later, at the end of the list, changes nothing either.
    s.CreateGameObject("Late temp")->transient = true;
    CHECK(SceneSerializer::Serialize(s) == with);

    // Play / Stop in short: load the text into a fresh scene (no transient object there) and write it again.
    Scene back("reloaded");
    std::string err;
    CHECK(SceneSerializer::Deserialize(back, with, &err));
    const std::string again = SceneSerializer::Serialize(back);
    CHECK(again == with);
    if (again != with) std::printf("--- before ---\n%s--- after reload ---\n%s", with.c_str(), again.c_str());

    // The parent link to the third object still resolves after the load.
    GameObject* b = back.Find("B");
    GameObject* child = back.Find("Child of B");
    CHECK(b && child);
    if (b && child) {
        CHECK(child->transform->Parent() == b->transform);
        CHECK(b->transform->localPosition.x == 1.0f && b->transform->localPosition.z == 3.0f);
    }
}

void TestOldFilesWithGaps() {
    RUN_SUITE("object numbers: an older file with gaps still loads, links intact");
    // How the writer numbered objects before: the transient object's number (1) is missing.
    const std::string old =
        "okayscene 3\n"
        "gameobject 0 \"A\"\n"
        "  active 1\n"
        "  parent -1\n"
        "end\n"
        "gameobject 2 \"B\"\n"
        "  active 1\n"
        "  parent -1\n"
        "end\n"
        "gameobject 3 \"Child of B\"\n"
        "  active 1\n"
        "  parent 2\n"
        "end\n";
    Scene s("old");
    std::string err;
    CHECK(SceneSerializer::Deserialize(s, old, &err));
    GameObject* b = s.Find("B");
    GameObject* child = s.Find("Child of B");
    CHECK(b && child && child->transform->Parent() == b->transform);
    // Written back, it is numbered without the gap.
    const std::string t = SceneSerializer::Serialize(s);
    CHECK(Contains(t, "gameobject 1 \"B\"") && Contains(t, "gameobject 2 \"Child of B\""));
}

void TestEditorPlayStop() {
    RUN_SUITE("object numbers: the Humanoid template is not changed by Play / Stop");
    EditorState ed;
    ed.NewHumanoid();
    CHECK(!ed.HasUnsavedChanges());
    const std::string before = SceneSerializer::Serialize(ed.scene());
    ed.Play();
    for (int i = 0; i < 10; ++i) ed.Tick(1.0f / 60.0f);
    ed.Stop();
    const std::string after = SceneSerializer::Serialize(ed.scene());
    CHECK(after == before);
    CHECK(!ed.HasUnsavedChanges());
    CHECK(!ed.SyncDirty() && !ed.dirty);                         // the title bar's " *" follows `dirty`
    if (after != before) {
        // Show the first line that differs.
        std::size_t i = 0;
        while (i < before.size() && i < after.size() && before[i] == after[i]) ++i;
        const std::size_t from = before.rfind('\n', i) == std::string::npos ? 0 : before.rfind('\n', i) + 1;
        std::printf("first difference: before \"%s\" after \"%s\"\n",
                    before.substr(from, before.find('\n', i) - from).c_str(),
                    after.substr(from, after.find('\n', i) - from).c_str());
    }
}

}  // namespace

int main() {
    RUN_SUITE("serialize_object_numbers");
    Log::console = false;
    Game::Reset();
    Time::ResetForTesting();
    TestNumbering();
    TestTransientComesAndGoes();
    TestOldFilesWithGaps();
    TestEditorPlayStop();
    TEST_MAIN_RESULT();
}
