#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>

using namespace okay;

int main() {
    RUN_SUITE("uishape");

    // Rectangle: every row spans the full width.
    {
        float x0, x1;
        CHECK(UIShapeRowSpan(UIShape::Rectangle, 100, 40, 0, 0, x0, x1));
        CHECK_NEAR(x0, 0.0f, 1e-4f); CHECK_NEAR(x1, 100.0f, 1e-4f);
        CHECK(UIShapeRowSpan(UIShape::Rectangle, 100, 40, 0, 39, x0, x1));
        CHECK_NEAR(x1 - x0, 100.0f, 1e-4f);
        CHECK(!UIShapeRowSpan(UIShape::Rectangle, 100, 40, 0, 40, x0, x1));  // out of range
    }

    // Circle: the middle row is widest (~full width), the very top/bottom narrow.
    {
        float x0, x1;
        UIShapeRowSpan(UIShape::Circle, 100, 100, 0, 50, x0, x1);
        CHECK_NEAR(x1 - x0, 100.0f, 2.0f);                 // diameter at the equator
        UIShapeRowSpan(UIShape::Circle, 100, 100, 0, 1, x0, x1);
        CHECK(x1 - x0 < 40.0f);                            // narrow near the top
        // A corner pixel is outside the circle; the center is inside.
        CHECK(!UIShapeContains(UIShape::Circle, 100, 100, 0, 2.0f, 2.0f));
        CHECK(UIShapeContains(UIShape::Circle, 100, 100, 0, 50.0f, 50.0f));
    }

    // Rounded: corners are inset, the middle band is full width.
    {
        float x0, x1;
        UIShapeRowSpan(UIShape::Rounded, 200, 80, 20, 40, x0, x1);  // mid row
        CHECK_NEAR(x1 - x0, 200.0f, 1e-3f);
        UIShapeRowSpan(UIShape::Rounded, 200, 80, 20, 0, x0, x1);   // top row -> inset
        CHECK(x0 > 0.0f);
        // Inside the flat middle but a clipped top corner is outside.
        CHECK(UIShapeContains(UIShape::Rounded, 200, 80, 20, 100.0f, 40.0f));
        CHECK(!UIShapeContains(UIShape::Rounded, 200, 80, 20, 1.0f, 1.0f));
    }

    // Pill: a 200x40 capsule has fully round ends (radius = 20 = half height).
    {
        float x0, x1;
        UIShapeRowSpan(UIShape::Pill, 200, 40, 0, 20, x0, x1);      // mid row -> ~full
        CHECK_NEAR(x1 - x0, 200.0f, 0.5f);
        UIShapeRowSpan(UIShape::Pill, 200, 40, 0, 0, x0, x1);       // top -> strong inset
        CHECK(x0 > 10.0f);
    }

    // Round-trip the new UIPanel shape + gradient direction, and UIImage shape.
    {
        Scene s("U"); s.physicsEnabled = false;
        auto* pn = s.CreateGameObject("P")->AddComponent<UIPanel>();
        pn->shape = UIShape::Pill; pn->gradientHorizontal = true; pn->useGradient = true;
        auto* im = s.CreateGameObject("I")->AddComponent<UIImage>();
        im->shape = UIShape::Circle;

        Scene s2("x"); SceneSerializer::Deserialize(s2, SceneSerializer::Serialize(s));
        auto* pn2 = s2.Find("P") ? s2.Find("P")->GetComponent<UIPanel>() : nullptr;
        auto* im2 = s2.Find("I") ? s2.Find("I")->GetComponent<UIImage>() : nullptr;
        CHECK(pn2 && pn2->shape == UIShape::Pill);
        CHECK(pn2 && pn2->gradientHorizontal);
        CHECK(im2 && im2->shape == UIShape::Circle);
    }

    // A circle button only accepts clicks inside its disc, not the corners.
    {
        Scene s("B"); s.physicsEnabled = false;
        auto* btn = s.CreateGameObject("Btn")->AddComponent<UIButton>();
        btn->anchor = UIAnchor::TopLeft; btn->position = {0, 0}; btn->size = {100, 100};
        btn->shape = UIShape::Circle;
        CHECK(btn->Contains({50, 50}));     // center hits
        CHECK(!btn->Contains({3, 3}));      // corner misses the disc
    }

    // UIButton shape + drop shadow round-trip through the scene.
    {
        Scene s("B2"); s.physicsEnabled = false;
        auto* btn = s.CreateGameObject("Btn")->AddComponent<UIButton>();
        btn->shape = UIShape::Pill; btn->shadow = true; btn->shadowOffset = {2, 5};
        Scene s2("x"); SceneSerializer::Deserialize(s2, SceneSerializer::Serialize(s));
        auto* b2 = s2.Find("Btn") ? s2.Find("Btn")->GetComponent<UIButton>() : nullptr;
        CHECK(b2 && b2->shape == UIShape::Pill);
        CHECK(b2 && b2->shadow);
        if (b2) CHECK_NEAR(b2->shadowOffset.y, 5.0f, 1e-4f);
    }

    // Progress bar + slider shape/style fields round-trip through the scene.
    {
        Scene s("PS"); s.physicsEnabled = false;
        auto* pb = s.CreateGameObject("PB")->AddComponent<UIProgressBar>();
        pb->shape = UIShape::Pill; pb->gradientFill = true; pb->fillEnd = Color::FromBytes(200, 60, 60);
        auto* sl = s.CreateGameObject("SL")->AddComponent<UISlider>();
        sl->trackShape = UIShape::Pill; sl->roundKnob = true;

        Scene s2("x"); SceneSerializer::Deserialize(s2, SceneSerializer::Serialize(s));
        auto* pb2 = s2.Find("PB") ? s2.Find("PB")->GetComponent<UIProgressBar>() : nullptr;
        auto* sl2 = s2.Find("SL") ? s2.Find("SL")->GetComponent<UISlider>() : nullptr;
        CHECK(pb2 && pb2->shape == UIShape::Pill);
        CHECK(pb2 && pb2->gradientFill);
        CHECK(sl2 && sl2->trackShape == UIShape::Pill);
        CHECK(sl2 && sl2->roundKnob);
    }

    // The toggle's switch knob eases from off toward on over time (animation).
    {
        Scene s("TG"); s.physicsEnabled = false;
        auto* tg = s.CreateGameObject("T")->AddComponent<UIToggle>();
        tg->style = UIToggle::Style::Switch; tg->animSpeed = 12.0f; tg->on = true;
        CHECK_NEAR(tg->AnimT(), 0.0f, 1e-4f);        // starts off
        for (int i = 0; i < 60; ++i) s.Update(1.0f / 60.0f);
        CHECK(tg->AnimT() > 0.9f);                    // glides toward on

        // Round-trip the animation speed.
        Scene s2("x"); SceneSerializer::Deserialize(s2, SceneSerializer::Serialize(s));
        auto* tg2 = s2.Find("T") ? s2.Find("T")->GetComponent<UIToggle>() : nullptr;
        CHECK(tg2 && tg2->style == UIToggle::Style::Switch);
        if (tg2) CHECK_NEAR(tg2->animSpeed, 12.0f, 1e-3f);
    }

    // Input field + dropdown shape/style fields round-trip through the scene.
    {
        Scene s("FD"); s.physicsEnabled = false;
        auto* inp = s.CreateGameObject("IN")->AddComponent<UIInputField>();
        inp->shape = UIShape::Pill; inp->cornerRadius = 9.0f; inp->borderWidth = 2.0f;
        auto* dd = s.CreateGameObject("DD")->AddComponent<UIDropdown>();
        dd->shape = UIShape::Rounded; dd->cornerRadius = 7.0f;

        Scene s2("x"); SceneSerializer::Deserialize(s2, SceneSerializer::Serialize(s));
        auto* in2 = s2.Find("IN") ? s2.Find("IN")->GetComponent<UIInputField>() : nullptr;
        auto* dd2 = s2.Find("DD") ? s2.Find("DD")->GetComponent<UIDropdown>() : nullptr;
        CHECK(in2 && in2->shape == UIShape::Pill);
        if (in2) CHECK_NEAR(in2->borderWidth, 2.0f, 1e-4f);
        CHECK(dd2 && dd2->shape == UIShape::Rounded);
        if (dd2) CHECK_NEAR(dd2->cornerRadius, 7.0f, 1e-4f);
    }

    TEST_MAIN_RESULT();
}
