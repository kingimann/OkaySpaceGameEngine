// Regression tests for renderer bugs found in audit round 2 (CPU side, headless).
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Render/Culling.hpp"
#include <cmath>
#include <limits>

using namespace okay;

int main() {
    RUN_SUITE("gradient and curve survive a NaN time");
    {
        // Array of two so that reading stops[-1] / keys[-1] would hit the first one's data.
        ParticleGradient g[2];
        g[0].count = ParticleGradient::kMaxStops;
        for (int i = 0; i < ParticleGradient::kMaxStops; ++i) g[0].stops[i] = {0.1f * i, Color(0.77f, 0.77f, 0.77f, 0.77f)};
        g[1].Add(0.0f, Color(1, 0, 0, 1));
        g[1].Add(1.0f, Color(0, 0, 1, 1));
        const float nan = std::numeric_limits<float>::quiet_NaN();
        Color c = g[1].Evaluate(nan);
        CHECK(c.r == 1.0f && c.g == 0.0f && c.b == 0.0f);   // the first stop, not memory before the array
        ParticleCurve k[2];
        k[0].count = ParticleCurve::kMaxKeys;
        for (int i = 0; i < ParticleCurve::kMaxKeys; ++i) k[0].keys[i] = {0.1f * i, 42.0f};
        k[1].SetLinear(2.0f, 4.0f);
        CHECK(k[1].Evaluate(nan) == 2.0f);
    }

    TEST_MAIN_RESULT();
}
