// The Inspector lock and the other editor references to scene objects (editor/ObjectRef.hpp, ImGui-free), across the
// paths that rebuild every object: undo, redo, delete and a new scene.
//
// The bug: "Lock Inspector" kept a raw GameObject* and checked "is this pointer still in the scene?". Undo rebuilds the
// scene from text, so the locked object was freed and a new object (made by the rebuild, or created right after) could
// take its address; the Inspector then showed that other object. The lock is now held by GUID and looked up each time.
//
// Each case locks object A, rebuilds the scene through the editor's undo path, creates a new object B (which may land
// at A's old address), and checks that the Inspector shows A again (same GUID, same name) or nothing if A is gone,
// and never B. Whether another object really lands at A's old address depends on the allocator (with glibc the undo
// rebuild puts "Other" there, and the old pointer lock then showed "Other"); the test prints how often it happened,
// and the checks hold either way.
#include "test_framework.hpp"
#include "../editor/EditorState.hpp"
#include "../editor/ObjectRef.hpp"
#include "okay/Scene/SceneSerializer.hpp"

#include <cstdio>

using namespace okay;
using okay::editor::EditorState;
using okay::editor::InspectorLock;
using okay::editor::ObjectRef;

namespace {

int g_reused = 0;   // how often another object really sat at the locked object's old address (informational)

// After the rebuild and B's creation: is some object that is NOT A at A's old address? (The old pointer check would then
// have shown that object. With glibc this happens in the undo case: the rebuild puts "Other" where A was.)
void NoteReuse(const EditorState& ed, const void* oldAddr, const std::string& guidA) {
    for (const auto& up : ed.scene().Objects())
        if ((const void*)up.get() == oldAddr && up->Guid() != guidA) { ++g_reused; return; }
}

// Lock A, edit it, undo (the scene is rebuilt), create B: the Inspector shows A (by GUID), never B.
void TestLockSurvivesUndo() {
    EditorState ed;
    ed.CreateCube("Other");
    GameObject* a = ed.CreateCube("A");
    ed.CommitUndo();
    const std::string guidA = a->Guid();
    const void* oldAddr = a;

    InspectorLock lock;
    ed.Select(a);
    lock.Lock(ed.selected());
    CHECK(lock.locked);
    CHECK(lock.Resolve(ed.scene(), nullptr).object == a);

    ed.PushUndo("Rename");
    a->name = "A renamed";
    ed.CommitUndo();
    CHECK(ed.Undo());                       // rebuilds every object from the scene text: `a` is freed
    ed.Select(nullptr);                     // the user selects nothing / something else
    GameObject* b = ed.CreateCube("B");     // may reuse A's address
    NoteReuse(ed, oldAddr, guidA);
    ed.Select(b);                           // locked: selecting B must not change what is shown

    const InspectorLock::View v = lock.Resolve(ed.scene(), ed.selected());
    CHECK(v.object != nullptr);
    CHECK(!v.lockedMissing);
    CHECK(v.object != b);
    if (v.object) {
        CHECK(v.object->Guid() == guidA);
        CHECK(v.object->name == "A");       // the undone name: the same object, as it is after the undo
    }

    CHECK(ed.Redo() || true);               // (B's creation cut the redo branch; Redo may be a no-op)
    const InspectorLock::View v2 = lock.Resolve(ed.scene(), ed.selected());
    CHECK(v2.object && v2.object->Guid() == guidA);
}

// Lock A, delete it, create B: the lock stays on and shows nothing (and says so). Undo brings A back.
void TestLockedObjectGone() {
    EditorState ed;
    ed.CreateCube("Other");
    GameObject* a = ed.CreateCube("A");
    ed.CommitUndo();
    const std::string guidA = a->Guid();
    const void* oldAddr = a;

    InspectorLock lock;
    ed.Select(a);
    lock.Lock(a);

    ed.DeleteSelected();                    // undoable delete: A is freed
    ed.CommitUndo();
    GameObject* b = ed.CreateCube("B");
    NoteReuse(ed, oldAddr, guidA);
    ed.CommitUndo();
    ed.Select(b);

    InspectorLock::View v = lock.Resolve(ed.scene(), ed.selected());
    CHECK(v.object == nullptr);             // never B, never the selection
    CHECK(v.lockedMissing);
    CHECK(lock.locked);                     // the safe choice: stay locked, show nothing, say so

    CHECK(ed.Undo());                       // un-create B (rebuild)
    CHECK(ed.Undo());                       // un-delete A (rebuild, A comes back with its GUID)
    v = lock.Resolve(ed.scene(), ed.selected());
    CHECK(v.object != nullptr);
    CHECK(!v.lockedMissing);
    if (v.object) { CHECK(v.object->Guid() == guidA); CHECK(v.object->name == "A"); }

    lock.Unlock();                          // unlocked: back to following the selection
    GameObject* other = ed.scene().Find("Other");
    ed.Select(other);
    v = lock.Resolve(ed.scene(), ed.selected());
    CHECK(v.object == other);
    CHECK(!v.lockedMissing);
}

// Undoing A's creation removes it: a lock on A shows nothing, even when the next new object takes A's memory.
void TestLockOnUndoneCreation() {
    EditorState ed;
    ed.CreateCube("Keep");
    ed.CommitUndo();
    GameObject* a = ed.CreateCube("A");
    ed.CommitUndo();
    const void* oldAddr = a;
    const std::string guidA = a->Guid();
    InspectorLock lock;
    lock.Lock(a);

    CHECK(ed.Undo());                       // A never existed
    GameObject* b = ed.CreateCube("B");
    NoteReuse(ed, oldAddr, guidA);
    const InspectorLock::View v = lock.Resolve(ed.scene(), b);
    CHECK(v.object == nullptr);
    CHECK(v.lockedMissing);
}

// A new scene (every object freed) clears what a lock can find; locking nothing is not a lock.
void TestNewSceneAndNull() {
    EditorState ed;
    GameObject* a = ed.CreateCube("A");
    InspectorLock lock;
    lock.Lock(a);
    ed.NewScene();
    GameObject* b = ed.CreateCube("A");     // same name, different object
    const InspectorLock::View v = lock.Resolve(ed.scene(), b);
    CHECK(v.object == nullptr);
    CHECK(v.lockedMissing);

    InspectorLock none;
    none.Lock(nullptr);
    CHECK(!none.locked);
    CHECK(none.Resolve(ed.scene(), b).object == b);
}

// ObjectRef (the Hierarchy rename and Save-as-Prefab targets, the auto camera): same object after undo, by GUID.
void TestObjectRef() {
    EditorState ed;
    GameObject* a = ed.CreateCube("A");
    ed.CommitUndo();
    ObjectRef r(a);
    CHECK(r.IsSet());
    CHECK(r.Is(a));
    CHECK(r.Get(ed.scene()) == a);

    ed.PushUndo("Move");
    a->transform->localPosition = {5, 0, 0};
    ed.CommitUndo();
    CHECK(ed.Undo());
    GameObject* b = ed.CreateCube("B");
    GameObject* again = r.Get(ed.scene());
    CHECK(again != nullptr);
    CHECK(again != b);
    CHECK(!r.Is(b));
    if (again) { CHECK(again->name == "A"); CHECK(again->transform->localPosition.x == 0.0f); }

    ObjectRef empty;
    CHECK(!empty.IsSet());
    CHECK(empty.Get(ed.scene()) == nullptr);
    CHECK(!empty.IsMissing(ed.scene()));
    r.Clear();
    CHECK(r.Get(ed.scene()) == nullptr);
}

}  // namespace

int main() {
    RUN_SUITE("inspector_lock");
    Log::console = false;
    Game::Reset();
    Time::ResetForTesting();
    TestLockSurvivesUndo();
    TestLockedObjectGone();
    TestLockOnUndoneCreation();
    TestNewSceneAndNull();
    TestObjectRef();
    std::printf("  (another object sat at the locked object's old address in %d of 3 rebuild cases: the old pointer lock would have shown it)\n", g_reused);
    TEST_MAIN_RESULT();
}
