// Shared UI styles (okay/Components/UIUserStyle.hpp): a named style object that widgets point to by name. Editing the
// style and applying it again updates every widget that uses it; widgets that do not use one are never touched, and a
// scene that does not use styles saves no new text.
#include "test_framework.hpp"
#include "Okay.hpp"
#include <sstream>

using namespace okay;

int main() {
    Log::console = false;
    {
        RUN_SUITE("a style updates every widget that uses it");
        Scene s("t");
        GameObject* st = s.CreateGameObject("Primary button");
        auto* us = st->AddComponent<UIUserStyle>();
        us->styleName = "Primary"; us->fill = Color(1, 0, 0, 1); us->textColor = Color(0, 1, 0, 1); us->cornerRadius = 20; us->borderWidth = 3;
        GameObject* b1 = s.CreateGameObject("B1"); b1->AddComponent<UIButton>(); b1->AddComponent<UIStyleVariant>()->userStyle = "Primary";
        GameObject* b2 = s.CreateGameObject("B2"); b2->AddComponent<UIButton>(); b2->AddComponent<UIStyleVariant>()->userStyle = "Primary";
        GameObject* p = s.CreateGameObject("P"); p->AddComponent<UIPanel>(); p->AddComponent<UIStyleVariant>()->userStyle = "Primary";
        GameObject* other = s.CreateGameObject("Other"); auto* ob = other->AddComponent<UIButton>(); const Color keep = ob->color;
        CHECK(ApplyUserStyles(s) == 3);
        CHECK(b1->GetComponent<UIButton>()->color.r == 1.0f && b2->GetComponent<UIButton>()->cornerRadius == 20.0f);
        CHECK(b1->GetComponent<UIButton>()->textColor.g == 1.0f && b1->GetComponent<UIButton>()->borderWidth == 3.0f);
        CHECK(p->GetComponent<UIPanel>()->color.r == 1.0f && p->GetComponent<UIPanel>()->cornerRadius == 20.0f);
        CHECK(ob->color.r == keep.r && ob->color.b == keep.b);   // not using a style: untouched
        us->fill = Color(0, 0, 1, 1); us->cornerRadius = 4;
        ApplyUserStyles(s);
        CHECK(b2->GetComponent<UIButton>()->color.b == 1.0f && b2->GetComponent<UIButton>()->cornerRadius == 4.0f);
        CHECK(b1->GetComponent<UIButton>()->hoverColor.b >= b1->GetComponent<UIButton>()->color.b);   // hover follows
    }
    {
        RUN_SUITE("a style that sets only some things leaves the rest");
        Scene s("t");
        GameObject* st = s.CreateGameObject("S"); auto* us = st->AddComponent<UIUserStyle>();
        us->styleName = "OnlyText"; us->applyFill = false; us->applyBorder = false; us->cornerRadius = -1; us->textColor = Color(0, 0, 1, 1); us->textSize = 0.25f;
        GameObject* t = s.CreateGameObject("T"); auto* tr = t->AddComponent<TextRenderer>(); tr->screenSpace = true;
        const float oldPx = tr->pixelSize; (void)oldPx;
        t->AddComponent<UIStyleVariant>()->userStyle = "OnlyText";
        GameObject* b = s.CreateGameObject("B"); auto* bt = b->AddComponent<UIButton>(); const Color c0 = bt->color; const float r0 = bt->cornerRadius;
        b->AddComponent<UIStyleVariant>()->userStyle = "OnlyText";
        ApplyUserStyles(s);
        CHECK(tr->color.b == 1.0f && tr->pixelSize == 0.25f);
        CHECK(bt->color.r == c0.r && bt->cornerRadius == r0 && bt->textColor.b == 1.0f);
    }
    {
        RUN_SUITE("saved: round trip, and old scenes write nothing new");
        Scene plain("p"); plain.CreateGameObject("X")->AddComponent<UIButton>(); plain.CreateGameObject("Y")->AddComponent<UIStyleVariant>();
        const std::string text = SceneSerializer::Serialize(plain);
        CHECK(text.find("vuser") == std::string::npos && text.find("uiuserstyle") == std::string::npos);
        Scene s("t");
        auto* us = s.CreateGameObject("Style")->AddComponent<UIUserStyle>(); us->styleName = "Title"; us->fill = Color(0.2f, 0.4f, 0.6f, 1); us->borderWidth = 2;
        s.CreateGameObject("W")->AddComponent<UIStyleVariant>()->userStyle = "Title";
        const std::string saved = SceneSerializer::Serialize(s);
        Scene b("b"); std::string err; CHECK(SceneSerializer::Deserialize(b, saved, &err, nullptr));
        auto* u2 = b.Find("Style") ? b.Find("Style")->GetComponent<UIUserStyle>() : nullptr;
        CHECK(u2 && u2->styleName == "Title" && u2->borderWidth == 2.0f && std::fabs(u2->fill.b - 0.6f) < 0.01f);
        auto* v2 = b.Find("W") ? b.Find("W")->GetComponent<UIStyleVariant>() : nullptr;
        CHECK(v2 && v2->userStyle == "Title");
        CHECK(SceneSerializer::Serialize(b) == saved);
    }
    {
        RUN_SUITE("saved guides: only when kept, and they come back");
        Scene plain("p"); plain.CreateGameObject("X");
        CHECK(SceneSerializer::Serialize(plain).find("uiguides") == std::string::npos);
        Scene s("t"); auto* g = s.CreateGameObject("UI Guides")->AddComponent<UIGuides>();
        g->vertical = {100.5f, 640}; g->horizontal = {360};
        const std::string saved = SceneSerializer::Serialize(s);
        Scene b("b"); std::string err; CHECK(SceneSerializer::Deserialize(b, saved, &err, nullptr));
        auto* g2 = b.Find("UI Guides") ? b.Find("UI Guides")->GetComponent<UIGuides>() : nullptr;
        CHECK(g2 && g2->vertical.size() == 2 && g2->vertical[0] == 100.5f && g2->horizontal.size() == 1 && g2->horizontal[0] == 360.0f);
        CHECK(SceneSerializer::Serialize(b) == saved);
    }
    {
        RUN_SUITE("constraints: limits, aspect lock, portrait look, saved only when used");
        UIConstraints c; c.minSize = {50, 0}; c.maxSize = {200, 0};
        CHECK(ConstrainSize({10, 30}, c).x == 50 && ConstrainSize({500, 30}, c).x == 200 && ConstrainSize({80, 30}, c).y == 30);
        c.lockAspect = true; c.aspect = 2.0f;
        Vec2 a = ConstrainSize({100, 999}, c); CHECK(a.x == 100 && a.y == 50);
        c.maxSize = {200, 60}; Vec2 b = ConstrainSize({200, 0}, c); CHECK(b.y == 60 && b.x == 120);   // the height limit keeps the shape
        Scene s("c");
        GameObject* g = s.CreateGameObject("W"); auto* p = g->AddComponent<UIPanel>(); p->position = {10, 20}; p->size = {300, 100};
        auto* uc = g->AddComponent<UIConstraints>(); uc->maxSize = {200, 0}; uc->portraitOverride = true; uc->portraitPosition = {5, 6}; uc->portraitSize = {80, 40};
        CHECK(ApplyUIConstraints(s, 1280, 720) == 1 && p->size.x == 200 && p->position.x == 10);   // landscape: only the limit
        CHECK(ApplyUIConstraints(s, 720, 1280) >= 1 && p->position.x == 5 && p->size.x == 80);       // portrait look
        CHECK(ApplyUIConstraints(s, 720, 1280) == 0);                                                // stable
        ApplyUIConstraints(s, 1280, 720); CHECK(p->position.x == 10 && p->position.y == 20 && p->size.x == 200);   // back
        GameObject* free = s.CreateGameObject("F"); auto* fp = free->AddComponent<UIPanel>(); fp->size = {999, 999};
        ApplyUIConstraints(s, 720, 1280); CHECK(fp->size.x == 999);   // no component: untouched
        Scene plain("p"); plain.CreateGameObject("X")->AddComponent<UIPanel>();
        CHECK(SceneSerializer::Serialize(plain).find("uiconstraints") == std::string::npos);
        const std::string saved = SceneSerializer::Serialize(s);
        Scene back("b"); std::string err; CHECK(SceneSerializer::Deserialize(back, saved, &err, nullptr));
        auto* u2 = back.Find("W") ? back.Find("W")->GetComponent<UIConstraints>() : nullptr;
        CHECK(u2 && u2->maxSize.x == 200 && u2->portraitOverride && u2->portraitSize.x == 80 && !u2->inPortrait);
        CHECK(SceneSerializer::Serialize(back) == saved);
    }
    TEST_MAIN_RESULT();
}
