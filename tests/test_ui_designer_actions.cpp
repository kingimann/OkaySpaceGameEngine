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
    {
        RUN_SUITE("selection and layout tools on a scene: right result, one undo step");
        editor::EditorState ed;
        auto mk = [&](const char* n, float x, float y, float w, float h, bool button) {
            GameObject* g = ed.scene().CreateGameObject(n);
            if (button) { auto* b = g->AddComponent<UIButton>(); b->anchor = UIAnchor::TopLeft; b->position = {x, y}; b->size = {w, h}; }
            else { auto* p = g->AddComponent<UIPanel>(); p->anchor = UIAnchor::TopLeft; p->position = {x, y}; p->size = {w, h}; }
            return g; };
        mk("A", 10, 10, 50, 20, true); mk("B", 100, 40, 30, 30, true); mk("C", 300, 5, 60, 10, false);
        auto none = [](GameObject*) { return false; };
        auto P = [&](const char* n) { auto* g = ed.scene().Find(n); return GetUIRect(g); };
        CHECK(oked::uid::SelectTool(ed, oked::uid::SelOp::All, none) == 3 && ed.MultiSelection().size() == 3);
        CHECK(oked::uid::SelectTool(ed, oked::uid::SelOp::Invert, none) == 0);
        ed.Select(ed.scene().Find("A"));
        CHECK(oked::uid::SelectTool(ed, oked::uid::SelOp::SameKind, none) == 2);   // A and B are buttons
        CHECK(oked::uid::SelectTool(ed, oked::uid::SelOp::SameKind, [](GameObject* g) { return g->name == "B"; }) == 1);
        auto sel3 = [&]() { ed.Select(ed.scene().Find("A")); ed.ToggleSelect(ed.scene().Find("B")); ed.ToggleSelect(ed.scene().Find("C")); };   // primary = C (undo rebuilds objects)
        sel3();
        CHECK(oked::uid::AlignSelection(ed, oked::uid::AlignMode::Top) == 2);   // to C's top; C stays
        CHECK(P("A").position->y == 5 && P("B").position->y == 5 && P("C").position->y == 5);
        CHECK(ed.Undo()); CHECK(P("A").position->y == 10 && P("B").position->y == 40);
        sel3();
        CHECK(oked::uid::MatchSelection(ed, true, false) == 2);   // width of C (60)
        CHECK(P("A").sizePtr->x == 60 && P("B").sizePtr->x == 60 && P("B").sizePtr->y == 30);
        CHECK(ed.Undo()); CHECK(P("A").sizePtr->x == 50);
        sel3();
        CHECK(oked::uid::DistributeSelection(ed, true, 8) == 2);
        CHECK(P("A").position->x == 10 && P("B").position->x == 68 && P("C").position->x == 106);
        sel3();
        CHECK(oked::uid::FlipSelection(ed, true) == 3);
        CHECK(P("B").position->x == 78);
        CHECK(ed.Undo()); CHECK(P("B").position->x == 68);   // only the flip went back
        sel3();
        CHECK(oked::uid::TidySelection(ed, 2, 4) > 0);
        ed.Select(ed.scene().Find("A"));
        CHECK(oked::uid::AlignSelection(ed, oked::uid::AlignMode::CenterH) == 1);   // one widget: centred on the screen
        CHECK(std::fabs(P("A").position->x + P("A").sizePtr->x * 0.5f - UICanvas::Width() * 0.5f) < 0.01f);
        ed.Select(nullptr);
        CHECK(oked::uid::AlignSelection(ed, oked::uid::AlignMode::Left) == 0 && oked::uid::FlipSelection(ed, true) == 0);
    }
    {
        RUN_SUITE("find and replace: colours and styles");
        editor::EditorState ed;
        GameObject* a = ed.scene().CreateGameObject("A"); auto* ba = a->AddComponent<UIButton>(); ba->color = Color(1, 0, 0, 1);
        GameObject* b = ed.scene().CreateGameObject("B"); auto* pb = b->AddComponent<UIPanel>(); pb->color = Color(0.98f, 0.01f, 0, 1);
        GameObject* c = ed.scene().CreateGameObject("C"); auto* pc = c->AddComponent<UIPanel>(); pc->color = Color(0, 1, 0, 1);
        CHECK(oked::uid::ColorsClose(Color(1, 0, 0, 1), Color(0.97f, 0, 0, 1), 0.05f) && !oked::uid::ColorsClose(Color(1, 0, 0, 1), Color(0, 1, 0, 1), 0.05f));
        CHECK(oked::uid::ReplaceColor(ed, Color(1, 0, 0, 1), Color(0, 0, 1, 1), 0.05f) == 2);
        CHECK(ed.scene().Find("A")->GetComponent<UIButton>()->color.b == 1.0f && ed.scene().Find("B")->GetComponent<UIPanel>()->color.b == 1.0f);
        CHECK(ed.scene().Find("C")->GetComponent<UIPanel>()->color.g == 1.0f);
        CHECK(ed.Undo()); CHECK(ed.scene().Find("A")->GetComponent<UIButton>()->color.r == 1.0f && ed.scene().Find("B")->GetComponent<UIPanel>()->color.b == 0.0f);
        CHECK(oked::uid::ReplaceColor(ed, Color(0.5f, 0.5f, 0.5f, 1), Color(0, 0, 0, 1), 0.01f) == 0);   // nothing matched: no undo entry, no change
        GameObject* s1 = ed.scene().Find("A"); s1->AddComponent<UIStyleVariant>()->userStyle = "Old";
        GameObject* sty = ed.scene().CreateGameObject("S"); auto* us = sty->AddComponent<UIUserStyle>(); us->styleName = "New"; us->fill = Color(0, 1, 1, 1);
        CHECK(oked::uid::ReplaceStyle(ed, "Old", "New") == 1);
        CHECK(ed.scene().Find("A")->GetComponent<UIStyleVariant>()->userStyle == "New" && ed.scene().Find("A")->GetComponent<UIButton>()->color.b == 1.0f);
        CHECK(oked::uid::ReplaceStyle(ed, "", "New") == 0);
    }
    TEST_MAIN_RESULT();
}
