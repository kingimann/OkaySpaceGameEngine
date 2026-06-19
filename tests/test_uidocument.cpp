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

    // --- Styles (USS-like classes): defaults pulled in via class= ------
    {
        Scene s("styles"); s.physicsEnabled = false;
        GameObject* docGo = s.CreateGameObject("Doc");
        auto* doc = docGo->AddComponent<UIDocument>();
        doc->markup =
            "style primary color=60,90,150 size=300,60 anchor=center\n"
            "button \"Play\" class=primary pos=70,170\n"
            "button \"Quit\" class=primary pos=70,250 size=200,40\n";  // override size
        doc->Rebuild(); s.Update(0.0f);

        // Style lines don't create widgets; the two buttons do.
        CHECK(doc->Generated().size() == 2);
        UIButton* play = nullptr; UIButton* quit = nullptr;
        for (GameObject* g : doc->Generated()) {
            auto* b = g->GetComponent<UIButton>();
            if (!b) continue;
            if (b->label == "Play") play = b; else if (b->label == "Quit") quit = b;
        }
        CHECK(play && quit);
        CHECK(play->anchor == UIAnchor::Center);            // from the style
        CHECK_NEAR(play->size.x, 300.0f, 1e-4f);            // from the style
        CHECK_NEAR(quit->size.x, 200.0f, 1e-4f);            // widget overrides style
        CHECK(quit->anchor == UIAnchor::Center);            // still inherits anchor
    }

    // --- Custom widgets (define): reusable controls, instanced + shifted ---
    {
        Scene s("define"); s.physicsEnabled = false;
        GameObject* docGo = s.CreateGameObject("Doc");
        auto* doc = docGo->AddComponent<UIDocument>();
        doc->markup =
            "define card\n"
            "  panel pos=0,0 size=200,120 color=40,40,60\n"
            "  text \"Title\" pos=10,10 size=2\n"
            "card pos=40,40\n"
            "card pos=300,40\n";
        doc->Rebuild(); s.Update(0.0f);

        // Two instances: each makes a group + panel + text = 3 objects -> 6 total.
        int groups = 0, panels = 0, texts = 0;
        for (GameObject* g : doc->Generated()) {
            if (g->GetComponent<UIPanel>()) ++panels;
            else if (g->GetComponent<TextRenderer>()) ++texts;
            else ++groups;   // empty group node
        }
        CHECK(panels == 2);
        CHECK(texts == 2);
        CHECK(groups == 2);

        // The instances are shifted to their pos: panels at x=40 and x=300.
        std::vector<float> xs;
        for (GameObject* g : doc->Generated())
            if (auto* p = g->GetComponent<UIPanel>()) xs.push_back(p->position.x);
        bool has40 = false, has300 = false;
        for (float x : xs) { if (x > 39 && x < 41) has40 = true; if (x > 299 && x < 301) has300 = true; }
        CHECK(has40 && has300);
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

    // --- Expanded widgets: input, dropdown, tooltip + customization keys --
    {
        Scene s("toolkit"); s.physicsEnabled = false;
        GameObject* docGo = s.CreateGameObject("Doc");
        auto* doc = docGo->AddComponent<UIDocument>();
        doc->markup =
            "panel pos=0,0 size=400,300 corner=12 border=2 gradient=10,10,20,255 tooltip=\"the frame\"\n"
            "  button \"Go\" pos=10,10 size=120,40 corner=8 font=3 tooltip=\"start\" onclick=set(\"x\",1)\n"
            "  input \"hi\" pos=10,60 size=200,32 placeholder=\"name\" max=12\n"
            "  dropdown pos=10,110 size=180,32 options=Low|Medium|High value=2\n"
            "  text \"Title\" pos=10,160 size=3 align=center outline=0,0,0,255\n";
        doc->Rebuild(); s.Update(0.0f);

        UIPanel* pn = nullptr; UIButton* bt = nullptr; UIInputField* in = nullptr;
        UIDropdown* dd = nullptr; TextRenderer* tx = nullptr;
        int tooltips = 0;
        for (GameObject* g : doc->Generated()) {
            if (auto* x = g->GetComponent<UIPanel>())      pn = x;
            if (auto* x = g->GetComponent<UIButton>())     bt = x;
            if (auto* x = g->GetComponent<UIInputField>()) in = x;
            if (auto* x = g->GetComponent<UIDropdown>())   dd = x;
            if (auto* x = g->GetComponent<TextRenderer>()) tx = x;
            if (g->GetComponent<UITooltip>())              ++tooltips;
        }
        CHECK(pn && pn->useGradient);
        CHECK_NEAR(pn->cornerRadius, 12.0f, 1e-4f);
        CHECK_NEAR(pn->borderWidth, 2.0f, 1e-4f);
        CHECK(bt && bt->label == "Go");
        CHECK_NEAR(bt->cornerRadius, 8.0f, 1e-4f);
        CHECK_NEAR(bt->fontScale, 3.0f, 1e-4f);
        CHECK(bt->gameObject->GetComponent<ScriptComponent>() != nullptr);
        CHECK(in && in->text == "hi" && in->placeholder == "name" && in->maxLength == 12);
        CHECK(dd && dd->options.size() == 3 && dd->value == 2);
        CHECK(dd->options[1] == "Medium");
        CHECK(tx && tx->align == 1 && tx->outline);
        CHECK(tooltips == 2);   // panel + button
    }

    TEST_MAIN_RESULT();
}
