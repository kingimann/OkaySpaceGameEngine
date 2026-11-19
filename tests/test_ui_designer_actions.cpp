// Canvas actions of the UI designer (editor/UIDesignerActions.hpp) are one undo step each.
#include "test_framework.hpp"
#include "EditorState.hpp"
#include "../editor/UIDesignerActions.hpp"

using namespace okay;

int main() {
    Log::console = false;
    {
        RUN_SUITE("nudge: moves the selection, one undo step, locked widgets stay");
        editor::EditorState ed;
        GameObject* a = ed.scene().CreateGameObject("A"); auto* pa = a->AddComponent<UIPanel>(); pa->position = {10, 10};
        GameObject* b = ed.scene().CreateGameObject("B"); auto* pb = b->AddComponent<UIPanel>(); pb->position = {50, 50};
        ed.Select(a); ed.ToggleSelect(b);
        auto P = [&](const char* n) { return ed.scene().Find(n)->GetComponent<UIPanel>(); };   // undo rebuilds the objects
        auto none = [](GameObject*) { return false; };
        CHECK(oked::uid::NudgeSelection(ed, 1, 0, false, 8, none) == 2);
        CHECK(pa->position.x == 11 && pb->position.x == 51 && pa->position.y == 10);
        CHECK(oked::uid::NudgeSelection(ed, 0, -1, true, 8, none) == 2);
        CHECK(pa->position.y == 2 && pb->position.y == 42);
        CHECK(ed.Undo()); CHECK(P("A")->position.y == 10 && P("A")->position.x == 11);
        CHECK(ed.Undo()); CHECK(P("A")->position.x == 10 && P("B")->position.x == 50);
        CHECK(ed.Redo()); CHECK(P("A")->position.x == 11);
        ed.Select(ed.scene().Find("A")); ed.ToggleSelect(ed.scene().Find("B"));
        GameObject* b2 = ed.scene().Find("B"); auto lockB = [&](GameObject* g) { return g == b2; };
        CHECK(oked::uid::NudgeSelection(ed, 1, 0, false, 8, lockB) == 1);
        CHECK(P("B")->position.x == 51 && P("A")->position.x == 12);
        CHECK(oked::uid::NudgeSelection(ed, 0, 0, false, 8, none) == 0);   // no key: nothing, no undo entry
        ed.Select(nullptr);
        CHECK(oked::uid::NudgeSelection(ed, 1, 0, false, 8, none) == 0);
    }
    TEST_MAIN_RESULT();
}
