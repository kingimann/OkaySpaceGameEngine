#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("uidocument");

    // --- Markup builds a nested widget tree of the right types ----------
    {
        Scene s("doc"); s.physicsEnabled = false;
        GameObject* canvas = s.CreateGameObject("Canvas");
        canvas->AddComponent<Canvas>();
        GameObject* docGo = s.CreateGameObject("Doc");
        auto* doc = docGo->AddComponent<UIDocument>();
        docGo->transform->SetParent(canvas->transform, false);
        doc->markup =
            "panel pos=40,40 size=360,240 color=30,36,52,220\n"
            "  text \"MY GAME\" pos=70,70 size=5 color=255,255,255\n"
            "  button \"Play\" pos=70,170 size=300,60 anchor=center onclick=load_scene(\"game\")\n"
            "  slider pos=70,250 size=300,16 value=0.5\n"
            "  toggle \"Sound\" pos=70,290 size=28,28 on=1\n"
            "progress pos=70,330 size=300,18 value=0.7\n";

        doc->Rebuild();
        s.Update(0.0f);   // flush creations

        CHECK(doc->Generated().size() == 6);

        GameObject* panel = nullptr;
        int panels = 0, buttons = 0, texts = 0, sliders = 0, toggles = 0, bars = 0;
        for (GameObject* g : doc->Generated()) {
            if (g->GetComponent<UIPanel>())        { ++panels; panel = g; }
            if (g->GetComponent<UIButton>())       ++buttons;
            if (g->GetComponent<TextRenderer>())   ++texts;
            if (g->GetComponent<UISlider>())       ++sliders;
            if (g->GetComponent<UIToggle>())       ++toggles;
            if (g->GetComponent<UIProgressBar>())  ++bars;
        }
        CHECK(panels == 1 && buttons == 1 && texts == 1);
        CHECK(sliders == 1 && toggles == 1 && bars == 1);

        // The indented children parent under the panel; the progress bar (no
        // indent) parents under the document GameObject.
        CHECK(panel != nullptr);
        CHECK(panel->transform->ChildCount() == 4);   // text, button, slider, toggle
        CHECK(OwningCanvas(panel) == canvas->GetComponent<Canvas>());
    }

    // --- Property parsing: pos/size/color/value/on/anchor/onclick -------
    {
        Scene s("props"); s.physicsEnabled = false;
        GameObject* docGo = s.CreateGameObject("Doc");
        auto* doc = docGo->AddComponent<UIDocument>();
        doc->markup =
            "button \"Hit\" pos=10,20 size=120,40 anchor=center onclick=set(\"x\", 1)\n"
            "slider pos=0,0 size=200,16 value=0.25\n"
            "toggle \"T\" on=1\n";
        doc->Rebuild();
        s.Update(0.0f);

        UIButton* btn = nullptr; UISlider* sl = nullptr; UIToggle* tg = nullptr;
        for (GameObject* g : doc->Generated()) {
            if (auto* b = g->GetComponent<UIButton>()) btn = b;
            if (auto* x = g->GetComponent<UISlider>()) sl = x;
            if (auto* x = g->GetComponent<UIToggle>()) tg = x;
        }
        CHECK(btn != nullptr);
        CHECK(btn->label == "Hit");
        CHECK_NEAR(btn->position.x, 10.0f, 1e-4f);
        CHECK_NEAR(btn->size.y, 40.0f, 1e-4f);
        CHECK(btn->anchor == UIAnchor::Center);
        CHECK(btn->gameObject->GetComponent<ScriptComponent>() != nullptr); // onclick wired
        CHECK(sl != nullptr);
        CHECK_NEAR(sl->value, 0.25f, 1e-4f);
        CHECK(tg != nullptr && tg->on);
    }

    // --- Rebuild replaces previously generated widgets ------------------
    {
        Scene s("rebuild"); s.physicsEnabled = false;
        GameObject* docGo = s.CreateGameObject("Doc");
        auto* doc = docGo->AddComponent<UIDocument>();
        doc->markup = "button \"A\" pos=0,0 size=100,40\nbutton \"B\" pos=0,50 size=100,40\n";
        doc->Rebuild(); s.Update(0.0f);
        CHECK(doc->Generated().size() == 2);

        doc->markup = "panel pos=0,0 size=200,200\n";
        doc->Rebuild(); s.Update(0.0f);
        CHECK(doc->Generated().size() == 1);
        CHECK(doc->Generated()[0]->GetComponent<UIPanel>() != nullptr);
    }

    // --- Comments and blank lines are ignored ---------------------------
    {
        Scene s("comments"); s.physicsEnabled = false;
        GameObject* docGo = s.CreateGameObject("Doc");
        auto* doc = docGo->AddComponent<UIDocument>();
        doc->markup = "# a menu\n\nbutton \"Only\" pos=0,0 size=80,30\n\n# end\n";
        doc->Rebuild(); s.Update(0.0f);
        CHECK(doc->Generated().size() == 1);
    }

    // --- Markup survives a serialization round-trip (Build Game) --------
    {
        Scene s("ser"); s.physicsEnabled = false;
        GameObject* docGo = s.CreateGameObject("Doc");
        auto* doc = docGo->AddComponent<UIDocument>();
        doc->markup = "panel pos=1,2 size=3,4\n  text \"Hi\" pos=5,6 size=2\n";

        std::string txt = SceneSerializer::Serialize(s);
        Scene s2("x"); SceneSerializer::Deserialize(s2, txt);
        GameObject* g2 = s2.Find("Doc");
        CHECK(g2 != nullptr);
        auto* d2 = g2->GetComponent<UIDocument>();
        CHECK(d2 != nullptr);
        CHECK(d2->markup == doc->markup);
        // And it rebuilds from the restored markup.
        d2->Rebuild(); s2.Update(0.0f);
        CHECK(d2->Generated().size() == 2);
    }

    TEST_MAIN_RESULT();
}
