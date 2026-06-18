#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("easing");

    // --- All easings pin their endpoints: f(0)=0, f(1)=1 ---
    Ease all[] = {
        Ease::Linear, Ease::QuadIn, Ease::QuadOut, Ease::QuadInOut,
        Ease::CubicIn, Ease::CubicOut, Ease::CubicInOut,
        Ease::SineIn, Ease::SineOut, Ease::SineInOut,
        Ease::ExpoIn, Ease::ExpoOut, Ease::ExpoInOut,
        Ease::BackIn, Ease::BackOut, Ease::BackInOut,
        Ease::ElasticOut, Ease::BounceOut,
    };
    for (Ease e : all) {
        CHECK_NEAR(Easing::Evaluate(e, 0.0f), 0.0f, 0.01f);
        CHECK_NEAR(Easing::Evaluate(e, 1.0f), 1.0f, 0.01f);
    }

    // --- Shape checks ---
    CHECK_NEAR(Easing::Linear(0.5f), 0.5f, 0.0001f);
    CHECK(Easing::QuadIn(0.5f) < 0.5f);   // ease-in starts slow
    CHECK(Easing::QuadOut(0.5f) > 0.5f);  // ease-out starts fast
    CHECK_NEAR(Easing::QuadInOut(0.5f), 0.5f, 0.0001f); // symmetric midpoint
    CHECK(Easing::BackIn(0.3f) < 0.0f);   // anticipation undershoots

    // --- Tween animates from -> to over duration ---
    {
        Tween tw(0.0f, 10.0f, 1.0f, Ease::Linear);
        CHECK_NEAR(tw.Value(), 0.0f, 0.0001f);
        tw.Update(0.5f);
        CHECK_NEAR(tw.Value(), 5.0f, 0.0001f);
        tw.Update(0.5f);
        CHECK_NEAR(tw.Value(), 10.0f, 0.0001f);
        CHECK(tw.Done());
        // Overshooting time stays clamped at the endpoint.
        tw.Update(5.0f);
        CHECK_NEAR(tw.Value(), 10.0f, 0.0001f);
    }

    // --- Reset rewinds the tween ---
    {
        Tween tw(2.0f, 4.0f, 2.0f);
        tw.Update(2.0f);
        CHECK(tw.Done());
        tw.Reset();
        CHECK(!tw.Done());
        CHECK_NEAR(tw.Value(), 2.0f, 0.0001f);
        CHECK_NEAR(tw.Progress(), 0.0f, 0.0001f);
    }

    TEST_MAIN_RESULT();
}
