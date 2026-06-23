#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("uiwidgets");

    // --- UIStepper: hit-test, stepping, clamp/wrap, round-trip ---
    {
        Scene s("ST"); s.physicsEnabled = false;
        auto* st = s.CreateGameObject("Step")->AddComponent<UIStepper>();
        st->anchor = UIAnchor::TopLeft; st->position = {0, 0}; st->size = {160, 32};
        st->minValue = 0; st->maxValue = 5; st->step = 1; st->value = 2;

        float bw = st->ButtonWidth();
        CHECK(st->PartAt({bw * 0.5f, 16}) == UIStepper::Part::Minus);     // left button
        CHECK(st->PartAt({160 - bw * 0.5f, 16}) == UIStepper::Part::Plus); // right button
        CHECK(st->PartAt({80, 16}) == UIStepper::Part::None);             // value area
        CHECK(st->PartAt({500, 16}) == UIStepper::Part::None);           // outside

        st->Step(1.0f);  CHECK_NEAR(st->value, 3.0f, 1e-4f);
        st->Step(-1.0f); CHECK_NEAR(st->value, 2.0f, 1e-4f);
        for (int i = 0; i < 10; ++i) st->Step(1.0f);
        CHECK_NEAR(st->value, 5.0f, 1e-4f);                              // clamps at max
        for (int i = 0; i < 10; ++i) st->Step(-1.0f);
        CHECK_NEAR(st->value, 0.0f, 1e-4f);                             // clamps at min

        st->wrap = true; st->value = 5; st->Step(1.0f);
        CHECK_NEAR(st->value, 0.0f, 1e-4f);                             // wraps past the top

        st->wholeNumbers = true; st->step = 0.4f; st->value = 2; st->wrap = false;
        st->Step(1.0f); CHECK_NEAR(st->value, 2.0f, 1e-4f);            // 2.4 rounds back to 2

        st->value = 3; st->step = 1;
        Scene s2("x"); SceneSerializer::Deserialize(s2, SceneSerializer::Serialize(s));
        auto* st2 = s2.Find("Step") ? s2.Find("Step")->GetComponent<UIStepper>() : nullptr;
        CHECK(st2 != nullptr);
        if (st2) {
            CHECK_NEAR(st2->value, 3.0f, 1e-4f);
            CHECK_NEAR(st2->maxValue, 5.0f, 1e-4f);
            CHECK(st2->wholeNumbers);
        }
    }

    // --- UIRating: hit-test, fill, click-to-set, half stars, round-trip ---
    {
        Scene s("RT"); s.physicsEnabled = false;
        auto* rt = s.CreateGameObject("Rate")->AddComponent<UIRating>();
        rt->anchor = UIAnchor::TopLeft; rt->position = {0, 0}; rt->size = {180, 36};
        rt->count = 5; rt->value = 3;

        float cw = rt->CellWidth();
        CHECK_NEAR(cw, 36.0f, 1e-3f);
        CHECK(rt->StarAt({cw * 0.5f, 18}) == 0);
        CHECK(rt->StarAt({cw * 4.5f, 18}) == 4);
        CHECK(rt->StarAt({-5, 18}) == -1);

        // value 3 -> first three stars full, rest empty.
        CHECK_NEAR(rt->StarFill(0), 1.0f, 1e-4f);
        CHECK_NEAR(rt->StarFill(2), 1.0f, 1e-4f);
        CHECK_NEAR(rt->StarFill(3), 0.0f, 1e-4f);

        rt->SetValue(5); CHECK_NEAR(rt->value, 5.0f, 1e-4f);
        rt->SetValue(99); CHECK_NEAR(rt->value, 5.0f, 1e-4f);          // clamped to count

        // Half stars.
        rt->allowHalf = true; rt->SetValue(2.5f);
        CHECK_NEAR(rt->StarFill(1), 1.0f, 1e-4f);
        CHECK_NEAR(rt->StarFill(2), 0.5f, 1e-4f);

        rt->allowHalf = false; rt->value = 3;
        Scene s2("x"); SceneSerializer::Deserialize(s2, SceneSerializer::Serialize(s));
        auto* rt2 = s2.Find("Rate") ? s2.Find("Rate")->GetComponent<UIRating>() : nullptr;
        CHECK(rt2 != nullptr);
        if (rt2) {
            CHECK(rt2->count == 5);
            CHECK_NEAR(rt2->value, 3.0f, 1e-4f);
        }
    }

    TEST_MAIN_RESULT();
}
