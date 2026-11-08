#include "test_framework.hpp"
#include "okay/Animation/HumanoidMotion.hpp"
#include <cmath>
#include <cstdio>

using namespace okay;

// The built-in gait's hip height must be a continuous curve over the stride.
// It used to jump 3-10 cm at every foot touchdown at walking speeds (a foot
// left the set of planted feet at the moment it needed the most hip drop), which
// read as the legs and head jittering on every step.
int main() {
    RUN_SUITE("gait_continuity");
    const int N = 4000;                     // samples per stride
    for (float v : {0.6f, 1.0f, 1.4f, 2.0f, 2.6f, 3.0f, 4.4f, 6.8f, 7.9f}) {
        HumanoidMotion::Inputs in{};
        in.speed = v;
        in.runW = HumanoidMotion::JogWeight(v);
        const int state = in.runW >= 0.5f ? 3 : 2;
        float worst = 0.0f, prev = 0.0f;
        for (int i = 0; i <= N; ++i) {
            in.phase = (float)i / N;
            const float y = HumanoidMotion::HipY(state, in);
            CHECK(std::isfinite(y));
            if (i > 0) worst = std::fmax(worst, std::fabs(y - prev));
            prev = y;
        }
        std::printf("  speed %.1f: biggest hip step per 1/%d stride %.4f\n", v, N, worst);
        CHECK(worst < 0.01f);               // was 0.02-0.1 at walking speeds
    }
    TEST_MAIN_RESULT();
}
