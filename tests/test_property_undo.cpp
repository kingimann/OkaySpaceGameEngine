// Property undo in the editor (EditorState::PropertyEdit + editor/PropertyUndo.hpp): a value the Inspector writes
// through the property registry (InspectorModel's WriteField, the path DrawFallbackInspector uses) is recorded as a
// property command that names the object by GUID, not as a whole-scene text delta. Undo and redo of such a step set the
// value back without rebuilding the scene, so the same GameObject* and Component* stay valid. The checks:
//   - set Light.intensity, undo, redo: the value comes back and goes again, the pointers do not change;
//   - a drag (10 small writes before the field is let go) is one step;
//   - property edit, add object (whole-scene delta), property edit: three undos restore everything in order;
//   - an object deleted and brought back by undo (new pointer, same GUID) is still found by the property step;
//   - the saved / unsaved flag follows; an edit nobody announced during a drag stays its own step; Play records nothing.
#include "test_framework.hpp"
#include "../editor/EditorState.hpp"
#include "../editor/InspectorModel.hpp"
#include "okay/Scene/SceneSerializer.hpp"

using namespace okay;
using okay::editor::EditorState;

namespace {

std::string Ser(const EditorState& ed) { return SceneSerializer::Serialize(ed.scene()); }

const PropertyDesc& Desc(const char* type, const char* prop) {
    static PropertyDesc none;
    auto info = PropertyRegistry::FindByName(type);
    const PropertyDesc* d = info ? info->Find(prop) : nullptr;
    CHECK(d != nullptr);
    return d ? *d : none;
}

// One frame of an Inspector edit: what PropertyField does when a widget reports a change.
void InspectorWrite(EditorState& ed, GameObject* go, const char* type, const char* prop, float v) {
    EditorState::PropertyEdit edit(ed, std::string(type) + "." + prop + "|" + go->Guid());
    oked::WriteField({go}, type, 0, Desc(type, prop), PropertyValue::MakeFloat(v));
}

struct Fixture {
    EditorState ed;
    GameObject* go = nullptr;
    Light* light = nullptr;
    Fixture() {
        go = ed.CreateCube("Lamp");
        light = go->AddComponent<Light>();
        light->intensity = 1.0f;
        ed.CreateCube("Other");
        ed.CommitUndo();
        ed.undoStack().Clear();
        ed.MarkClean();
    }
    GameObject* Lamp() { return ed.scene().Find("Lamp"); }
};

void TestSetUndoRedoKeepsPointers() {
    Fixture f;
    const std::string s0 = Ser(f.ed);
    InspectorWrite(f.ed, f.go, "Light", "intensity", 5.0f);
    f.ed.FinishPropertyEdit();
    const std::string s1 = Ser(f.ed);
    CHECK(f.light->intensity == 5.0f);
    CHECK(f.ed.UndoDepth() == 1);
    CHECK(f.ed.CanUndo());
    CHECK(f.ed.Undo());
    CHECK(f.Lamp() == f.go);                               // not rebuilt
    CHECK(f.go->GetComponent<Light>() == f.light);
    CHECK(f.light->intensity == 1.0f);
    CHECK(Ser(f.ed) == s0);
    CHECK(f.ed.CanRedo());
    CHECK(f.ed.Redo());
    CHECK(f.Lamp() == f.go && f.go->GetComponent<Light>() == f.light);
    CHECK(f.light->intensity == 5.0f);
    CHECK(Ser(f.ed) == s1);
    CHECK(f.ed.Undo());
    CHECK(f.light->intensity == 1.0f && f.Lamp() == f.go);
}

void TestDragIsOneStep() {
    Fixture f;
    for (int i = 1; i <= 10; ++i) InspectorWrite(f.ed, f.go, "Light", "intensity", 1.0f + 0.1f * (float)i);   // ten frames
    f.ed.FinishPropertyEdit();                              // the mouse is let go
    CHECK(f.ed.UndoDepth() == 1);
    CHECK(f.ed.Undo());
    CHECK(f.light->intensity == 1.0f);
    CHECK(!f.ed.CanUndo());
    CHECK(f.ed.Redo());
    CHECK(f.light->intensity == 1.0f + 0.1f * 10.0f);

    // A second drag of the same field after letting go is a step of its own.
    InspectorWrite(f.ed, f.go, "Light", "intensity", 7.0f);
    f.ed.FinishPropertyEdit();
    CHECK(f.ed.UndoDepth() == 2);
    // Another field edited without letting go of the first still ends the first step.
    InspectorWrite(f.ed, f.go, "Light", "range", 3.0f);
    InspectorWrite(f.ed, f.go, "Light", "intensity", 8.0f);
    f.ed.FinishPropertyEdit();
    CHECK(f.ed.UndoDepth() == 4);
    CHECK(f.ed.Undo() && f.light->intensity == 7.0f && f.light->range == 3.0f);
}

void TestInterleaveWithSceneDelta() {
    Fixture f;
    const std::string s0 = Ser(f.ed);
    InspectorWrite(f.ed, f.go, "Light", "intensity", 2.0f);
    f.ed.FinishPropertyEdit();
    const std::string s1 = Ser(f.ed);
    f.ed.CreateCube("Added");                               // PushUndo + change: a whole-scene delta
    f.ed.CommitUndo();
    const std::string s2 = Ser(f.ed);
    GameObject* lamp = f.Lamp();                            // pointers may change across the delta's undo/redo
    InspectorWrite(f.ed, lamp, "Light", "intensity", 3.0f);
    f.ed.FinishPropertyEdit();
    const std::string s3 = Ser(f.ed);
    CHECK(f.ed.UndoDepth() == 3);
    Light* l = lamp->GetComponent<Light>();
    CHECK(f.ed.Undo());
    CHECK(Ser(f.ed) == s2);
    CHECK(f.Lamp() == lamp && lamp->GetComponent<Light>() == l);   // the property step did not rebuild
    CHECK(f.ed.Undo());
    CHECK(Ser(f.ed) == s1);
    CHECK(f.ed.scene().Find("Added") == nullptr);
    CHECK(f.ed.Undo());
    CHECK(Ser(f.ed) == s0);
    CHECK(f.Lamp()->GetComponent<Light>()->intensity == 1.0f);
    CHECK(f.ed.Redo() && Ser(f.ed) == s1);
    CHECK(f.ed.Redo() && Ser(f.ed) == s2);
    CHECK(f.ed.Redo() && Ser(f.ed) == s3);
    CHECK(!f.ed.CanRedo());
}

void TestFindsRecreatedObjectByGuid() {
    Fixture f;
    const std::string guid = f.go->Guid();
    InspectorWrite(f.ed, f.go, "Light", "intensity", 4.0f);
    f.ed.FinishPropertyEdit();
    f.ed.Select(f.go);
    f.ed.DeleteSelected();
    f.ed.CommitUndo();
    CHECK(f.ed.scene().FindByGuid(guid) == nullptr);
    CHECK(f.ed.Undo());                                    // the delete is undone: a new object, the same GUID
    GameObject* back = f.ed.scene().FindByGuid(guid);
    CHECK(back != nullptr);
    if (!back) return;
    CHECK(back->GetComponent<Light>()->intensity == 4.0f);
    CHECK(f.ed.Undo());                                    // the property step finds it by GUID
    CHECK(f.ed.scene().FindByGuid(guid) == back);
    CHECK(back->GetComponent<Light>()->intensity == 1.0f);
    CHECK(f.ed.Redo());
    CHECK(back->GetComponent<Light>()->intensity == 4.0f);
    CHECK(f.ed.Redo());
    CHECK(f.ed.scene().FindByGuid(guid) == nullptr);
}

void TestDirtyFlag() {
    Fixture f;
    CHECK(!f.ed.HasUnsavedChanges());
    InspectorWrite(f.ed, f.go, "Light", "intensity", 6.0f);
    f.ed.dirty = true;                                     // what PropertyField does after a write
    f.ed.FinishPropertyEdit();
    CHECK(f.ed.HasUnsavedChanges());
    CHECK(f.ed.Undo());
    CHECK(!f.ed.dirty);                                    // back on the saved text
    CHECK(!f.ed.HasUnsavedChanges());
    CHECK(f.ed.Redo());
    CHECK(f.ed.dirty);
}

void TestUnannouncedEditStaysItsOwnStep() {
    Fixture f;
    const std::string s0 = Ser(f.ed);
    InspectorWrite(f.ed, f.go, "Light", "intensity", 9.0f);
    f.ed.scene().Find("Other")->name = "Renamed";           // a change nobody announced, during the drag
    InspectorWrite(f.ed, f.go, "Light", "intensity", 9.5f);
    f.ed.FinishPropertyEdit();
    CHECK(f.ed.UndoDepth() == 2);
    CHECK(f.ed.Undo());                                    // the property step first (it was the newest edit)
    CHECK(f.light->intensity == 1.0f);
    CHECK(f.ed.scene().Find("Renamed") != nullptr);
    CHECK(f.ed.Undo());
    CHECK(Ser(f.ed) == s0);
}

void TestPendingEditCountsAndPlayRecordsNothing() {
    Fixture f;
    InspectorWrite(f.ed, f.go, "Light", "intensity", 2.5f);
    CHECK(f.ed.CanUndo());                                 // the open edit can be undone before it is let go
    CHECK(f.ed.UndoDepth() == 1);
    CHECK(f.ed.Undo());                                    // Undo closes it and undoes it
    CHECK(f.light->intensity == 1.0f);
    CHECK(f.ed.CanRedo());

    f.ed.Play();
    GameObject* playLamp = f.Lamp();
    InspectorWrite(f.ed, playLamp, "Light", "intensity", 50.0f);
    f.ed.FinishPropertyEdit();
    f.ed.Stop();
    CHECK(f.Lamp()->GetComponent<Light>()->intensity == 1.0f);
    CHECK(f.ed.UndoDepth() == 0);
}

}  // namespace

int main() {
    RUN_SUITE("property_undo");
    Log::console = false;
    Game::Reset();
    Time::ResetForTesting();
    TestSetUndoRedoKeepsPointers();
    TestDragIsOneStep();
    TestInterleaveWithSceneDelta();
    TestFindsRecreatedObjectByGuid();
    TestDirtyFlag();
    TestUnannouncedEditStaysItsOwnStep();
    TestPendingEditCountsAndPlayRecordsNothing();
    TEST_MAIN_RESULT();
}
