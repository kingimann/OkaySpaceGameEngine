// Driver for engine/include/okay/Math/Easing.hpp (header only): every preset at a few times, names
// both ways, CSS-style curves read from text (cubic-bezier, steps, keywords), and the float Tween.
// Round 24 stopped at Easing.hpp:193 (a static table of string literals: "global initializer is
// not supported"), :219 (string::rfind of a string) and :344 (string::compare of a part).
#include "okay/Math/Easing.hpp"
#include <cstdio>
using namespace okay;
int main() {
    for (int i = 0; i < (int)Ease::Count; ++i) {
        Ease e = (Ease)i;
        std::printf("%-14s", Easing::Name(e));
        for (float t : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) std::printf(" %.4f", Easing::Evaluate(e, t));
        std::printf(" %s\n", Easing::ToName(e).c_str());
    }
    for (const char *n : {"out_quad", "OutQuad", "easeInOutCubic", "quadout", "nonsense", "Linear"})
        std::printf("%s -> %d\n", n, (int)Easing::FromName(n, Ease::BounceOut));
    for (const char *spec : {"ease-in-out", "cubic-bezier(0.4,0,0.2,1)", "steps(4)", "OutBack", "what"}) {
        EaseCurve c;
        bool ok = EaseCurve::Parse(spec, c);
        std::printf("%s %d %.4f %.4f\n", spec, (int)ok, c.Evaluate(0.3f), c.Evaluate(0.8f));
    }
    Tween tw(2.0f, 10.0f, 4.0f, Ease::QuadInOut);
    for (int k = 0; k < 5; ++k) {
        float v = tw.Update(1.0f);
        std::printf("%.4f %d\n", v, (int)tw.Done());
    }
}
