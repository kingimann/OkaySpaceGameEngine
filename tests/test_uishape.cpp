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

    TEST_MAIN_RESULT();
}
