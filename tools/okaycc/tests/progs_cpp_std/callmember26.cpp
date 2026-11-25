// A member of a call result as an operand of an arithmetic operator (GaitFor(3, v).freq -
// GaitFor(2, v).freq in Animation/HumanoidMotion.cpp): the result goes into a hidden object
// ([class.temporary] p4). Kept as a test after a round 26 change broke it ("not lowered").
#include <cstdio>
struct Gait { float freq; float len; };
static Gait GaitFor(int s, float v) { return Gait{s * 0.5f + v, v * 2}; }
static float Cycle(int state, float v, float w) {
    if (state == 2) return GaitFor(2, v).freq + (GaitFor(3, v).freq - GaitFor(2, v).freq) * w;
    return GaitFor(state, v).len * GaitFor(1, v).freq;
}
int main() { std::printf("%g %g\n", Cycle(2, 1.5f, 0.25f), Cycle(4, 2.0f, 0)); }
