// The Inspector's component fold state (editor/ComponentFoldModel.hpp, no ImGui). The old store was keyed by the
// component's address, so a new address (other object, Play/Stop, Undo, scene reload, restart) forgot the fold.
// Here the "address" never enters: the same type stays folded across all of those, and survives a save/load.
#include "test_framework.hpp"
#include "../editor/ComponentFoldModel.hpp"
using namespace okay::editor;

static void fold_default_then_remembered_per_type() {
    ComponentFoldModel m;
    CHECK(m.IsOpen("Rigidbody", "objA", true));
    m.Set("Rigidbody", "objA", false);
    CHECK(!m.IsOpen("Rigidbody", "objA", true));
    CHECK(!m.IsOpen("Rigidbody", "objB", true));      // another object: still folded
    CHECK(!m.IsOpen("Rigidbody", "", true));           // after undo/play/reload the object key may differ: type decides
    CHECK(m.IsOpen("Collider", "objA", true));         // other types unaffected
}
static void fold_managed_default_is_closed_until_user_opens() {
    ComponentFoldModel m;
    CHECK(!m.IsOpen("Character", "o", false));
    m.Set("Character", "o", true);
    CHECK(m.IsOpen("Character", "o", false));
}
static void fold_override_wins_and_is_per_object() {
    ComponentFoldModel m;
    m.Set("Light", "a", false);
    m.SetOverride("Light", "b", true, true);
    CHECK(m.IsOpen("Light", "b", true));
    CHECK(!m.IsOpen("Light", "a", true));
    m.Set("Light", "b", false);                        // goes to b's override, not the type
    CHECK(!m.IsOpen("Light", "b", true));
    m.Set("Light", "a", true);                         // type opens: b keeps its own
    CHECK(m.IsOpen("Light", "a", false));
    CHECK(!m.IsOpen("Light", "b", true));
    m.SetOverride("Light", "b", false, false);
    CHECK(m.IsOpen("Light", "b", false));
}
static void fold_collapse_expand_all_and_ctrl_click() {
    ComponentFoldModel m;
    std::vector<std::string> shown = {"A", "B", "C"};
    m.SetAll(shown, "o", false);
    CHECK(!m.IsOpen("A", "o", true) && !m.IsOpen("B", "o", true) && !m.IsOpen("C", "o", true));
    m.SetAll(shown, "o", true);
    CHECK(m.IsOpen("A", "o", false) && m.IsOpen("C", "o", false));
    m.SetOnly("B", shown, "o", true);
    CHECK(!m.IsOpen("A", "o", true)); CHECK(m.IsOpen("B", "o", false)); CHECK(!m.IsOpen("C", "o", true));
}
static void fold_save_and_restore() {
    ComponentFoldModel m;
    m.Set("Mesh Renderer", "o", false);
    m.Set("Audio Source", "o", true);
    CHECK(m.Dirty());
    ComponentFoldModel n;
    n.Deserialize(m.Serialize() + "junk line\nBad\t7\n");
    CHECK(!n.IsOpen("Mesh Renderer", "x", true));
    CHECK(n.IsOpen("Audio Source", "x", false));
    CHECK(n.TypeCount() == 2);
    CHECK(!n.Dirty());
}
static void fold_duplicate_types_have_own_keys() {
    CHECK(ComponentFoldKey("Action", 0) == "Action");
    CHECK(ComponentFoldKey("Action", 1) == "Action #2");
    ComponentFoldModel m;
    m.Set(ComponentFoldKey("Action", 1), "o", false);
    CHECK(m.IsOpen(ComponentFoldKey("Action", 0), "o", true));
}

int main() {
    RUN_SUITE("component_fold");
    fold_default_then_remembered_per_type();
    fold_managed_default_is_closed_until_user_opens();
    fold_override_wins_and_is_per_object();
    fold_collapse_expand_all_and_ctrl_click();
    fold_save_and_restore();
    fold_duplicate_types_have_own_keys();
    TEST_MAIN_RESULT();
}
