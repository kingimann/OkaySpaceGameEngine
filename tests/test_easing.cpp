#include "test_framework.hpp"
#include <Okay.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

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

    // --- Every ease (the later ones too) pins its endpoints ---
    for (int i = 0; i < (int)Ease::Count; ++i) {
        const Ease e = (Ease)i;
        CHECK_NEAR(Easing::Evaluate(e, 0.0f), 0.0f, 1e-5f);
        CHECK_NEAR(Easing::Evaluate(e, 1.0f), 1.0f, 1e-5f);
        CHECK(std::isfinite(Easing::Evaluate(e, 0.5f)));
    }
    // An out-of-range value is not a valid ease (falls back to linear).
    CHECK_NEAR(Easing::Evaluate(Ease::Count, 0.3f), 0.3f, 1e-6f);
    CHECK(std::string(Easing::Name(Ease::Count)) == "Linear");

    // --- Names: display names, compact names and forgiving parsing all round trip ---
    for (int i = 0; i < (int)Ease::Count; ++i) {
        const Ease e = (Ease)i;
        CHECK(Easing::FromName(Easing::Name(e), Ease::Count) == e);
        CHECK(Easing::FromName(Easing::ToName(e), Ease::Count) == e);
        CHECK(Easing::ToName(e).find(' ') == std::string::npos);
        for (int j = i + 1; j < (int)Ease::Count; ++j) CHECK(Easing::ToName(e) != Easing::ToName((Ease)j));
    }
    CHECK(Easing::ToName(Ease::QuadOut) == "OutQuad");
    CHECK(Easing::ToName(Ease::Linear) == "Linear");
    CHECK(Easing::FromName("easeInOutCubic") == Ease::CubicInOut);
    CHECK(Easing::FromName("in_out_cubic") == Ease::CubicInOut);
    CHECK(Easing::FromName("cubic_in_out") == Ease::CubicInOut);
    CHECK(Easing::FromName("OUT-BOUNCE") == Ease::BounceOut);
    CHECK(Easing::FromName("nonsense", Ease::BackOut) == Ease::BackOut);
    CHECK(Easing::FromName("", Ease::BackOut) == Ease::BackOut);

    // --- Cubic bezier (CSS style) ---
    CHECK(Easing::CubicBezier(0.3f, 0.1f, 0.6f, 0.9f, 0.0f) == 0.0f);
    CHECK(Easing::CubicBezier(0.3f, 0.1f, 0.6f, 0.9f, 1.0f) == 1.0f);
    for (float t = 0.0f; t <= 1.0f; t += 0.05f)
        CHECK_NEAR(Easing::CubicBezier(0.0f, 0.0f, 1.0f, 1.0f, t), t, 1e-5f);   // a straight line
    CHECK_NEAR(Easing::CubicBezier(0.42f, 0.0f, 0.58f, 1.0f, 0.5f), 0.5f, 1e-5f);   // symmetric ease-in-out
    CHECK_NEAR(Easing::CubicBezier(0.25f, 0.1f, 0.25f, 1.0f, 0.5f), 0.8024f, 1e-3f); // the CSS "ease" curve
    CHECK(Easing::CubicBezier(0.42f, 0.0f, 1.0f, 1.0f, 0.25f) < 0.25f);              // ease-in starts slow
    CHECK(Easing::CubicBezier(0.0f, 0.0f, 0.58f, 1.0f, 0.25f) > 0.25f);              // ease-out starts fast
    {   // Monotonic in time for any control points, and overshoot when y leaves 0..1.
        float prev = 0.0f;
        for (int i = 1; i <= 200; ++i) {
            const float v = Easing::CubicBezier(0.1f, 1.8f, 0.9f, -0.8f, (float)i / 200.0f);
            CHECK(std::isfinite(v));
            (void)prev; prev = v;
        }
        float peak = 0.0f;
        for (int i = 1; i < 100; ++i) peak = std::max(peak, Easing::CubicBezier(0.3f, 1.6f, 0.6f, 1.0f, (float)i / 100.0f));
        CHECK(peak > 1.0f);
        // x control points outside 0..1 are limited; NaN and out-of-range times are safe.
        CHECK(std::isfinite(Easing::CubicBezier(5.0f, 0.0f, -3.0f, 1.0f, 0.5f)));
        const float nan = std::numeric_limits<float>::quiet_NaN();
        CHECK(Easing::CubicBezier(0.2f, 0.2f, 0.8f, 0.8f, nan) == 0.0f);
        CHECK(Easing::CubicBezier(0.2f, 0.2f, 0.8f, 0.8f, -4.0f) == 0.0f);
        CHECK(Easing::CubicBezier(0.2f, 0.2f, 0.8f, 0.8f, 9.0f) == 1.0f);
        CHECK(std::isfinite(Easing::CubicBezier(nan, nan, nan, nan, 0.5f)));
    }

    // --- Steps ---
    CHECK(Easing::Steps(4, 0.0f) == 0.0f && Easing::Steps(4, 1.0f) == 1.0f);
    CHECK(Easing::Steps(4, 0.24f) == 0.0f);
    CHECK(Easing::Steps(4, 0.25f) == 0.25f);
    CHECK(Easing::Steps(4, 0.5f) == 0.5f);
    CHECK(Easing::Steps(4, 0.99f) == 0.75f);
    CHECK(Easing::Steps(4, 0.01f, true) == 0.25f);   // jump-start moves at once
    CHECK(Easing::Steps(4, 0.25f, true) == 0.25f);
    CHECK(Easing::Steps(4, 0.26f, true) == 0.5f);
    CHECK(Easing::Steps(1, 0.5f) == 0.0f && Easing::Steps(1, 0.5f, true) == 1.0f);
    CHECK(Easing::Steps(0, 0.5f) == 0.0f);           // fewer than one step counts as one
    CHECK(Easing::Steps(3, std::numeric_limits<float>::quiet_NaN()) == 0.0f);

    // --- EaseCurve: ease described as data, with a text form ---
    {
        EaseCurve c;
        CHECK(EaseCurve::Parse("OutQuad", c) && c.kind == EaseCurve::Kind::Preset && c.ease == Ease::QuadOut);
        CHECK(EaseCurve::Parse("in_out_sine", c) && c.ease == Ease::SineInOut);
        CHECK(EaseCurve::Parse("Linear", c) && c.ease == Ease::Linear);
        CHECK(EaseCurve::Parse(" cubic-bezier( 0.4 , 0 , 0.2 , 1 ) ", c) && c.kind == EaseCurve::Kind::Bezier);
        CHECK(c.x1 == 0.4f && c.y1 == 0.0f && c.x2 == 0.2f && c.y2 == 1.0f);
        CHECK(EaseCurve::Parse("Cubic-Bezier(.25,.1,.25,1)", c) && c.x1 == 0.25f);
        CHECK(EaseCurve::Parse("steps(5)", c) && c.kind == EaseCurve::Kind::Steps && c.steps == 5 && !c.jumpStart);
        CHECK(EaseCurve::Parse("steps(3, start)", c) && c.steps == 3 && c.jumpStart);
        CHECK(EaseCurve::Parse("steps(3,jump-end)", c) && !c.jumpStart);
        CHECK(EaseCurve::Parse("ease-in-out", c) && c.kind == EaseCurve::Kind::Bezier && c.x1 == 0.42f);
        CHECK(EaseCurve::Parse("ease", c) && c.y1 == 0.1f);
        // Text it does not understand is refused and leaves the target alone.
        c = EaseCurve::OfSteps(7);
        for (const char* bad : {"", "   ", "bogus", "cubic-bezier(1,2,3)", "cubic-bezier(1,2,3,4,5)", "cubic-bezier(a,b,c,d)",
                                "cubic-bezier(1,2,3,nan)", "cubic-bezier(1,2,3,inf)", "steps(0)", "steps(2.5)", "steps(-1)",
                                "steps()", "steps(2,middle)", "steps(2,start,end)", "steps(99999)", "cubic-bezier(1,2,3,4"})
            CHECK(!EaseCurve::Parse(bad, c) && c.kind == EaseCurve::Kind::Steps && c.steps == 7);
        // Evaluate follows the kind; ToString reads back exactly (floats included).
        CHECK_NEAR(EaseCurve::OfPreset(Ease::QuadIn).Evaluate(0.5f), 0.25f, 1e-6f);
        CHECK(EaseCurve::OfSteps(2).Evaluate(0.6f) == 0.5f);
        CHECK_NEAR(EaseCurve::OfBezier(0.0f, 0.0f, 1.0f, 1.0f).Evaluate(0.3f), 0.3f, 1e-5f);
        const EaseCurve list[] = {EaseCurve::OfPreset(Ease::ElasticInOut), EaseCurve::OfBezier(0.1f, 1.0f / 3.0f, 0.7f, 1.25f),
                                  EaseCurve::OfBezier(0.0f, -0.5f, 1.0f, 1.5f), EaseCurve::OfSteps(6, true), EaseCurve::OfSteps(12)};
        for (const EaseCurve& e : list) {
            EaseCurve back;
            CHECK(EaseCurve::Parse(e.ToString(), back));
            CHECK(back.ToString() == e.ToString());
            CHECK(back.kind == e.kind && back.ease == e.ease && back.x1 == e.x1 && back.y1 == e.y1 &&
                  back.x2 == e.x2 && back.y2 == e.y2 && back.steps == e.steps && back.jumpStart == e.jumpStart);
        }
        CHECK(EaseCurve::OfBezier(0.4f, 0.0f, 0.2f, 1.0f).ToString() == "cubic-bezier(0.4,0,0.2,1)");
        CHECK(EaseCurve::OfSteps(4, true).ToString() == "steps(4,start)");
        CHECK(EaseCurve::OfPreset(Ease::BackOut).ToString() == "OutBack");
    }

    // --- The plain Tween struct shrugs off a bad step and a bad duration ---
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        Tween tw(0.0f, 10.0f, 1.0f, Ease::Linear);
        tw.Update(nan); tw.Update(-3.0f); tw.Update(inf);
        CHECK_NEAR(tw.Value(), 0.0f, 1e-6f);
        CHECK(!tw.Done());
        tw.Update(0.5f);
        CHECK_NEAR(tw.Value(), 5.0f, 1e-5f);
        Tween bad(0.0f, 4.0f, nan, Ease::Linear);
        CHECK(bad.Done() && bad.Value() == 4.0f && bad.Progress() == 1.0f);
        Tween zero(1.0f, 2.0f, 0.0f);
        CHECK(zero.Done() && zero.Value() == 2.0f);
    }

    TEST_MAIN_RESULT();
}
