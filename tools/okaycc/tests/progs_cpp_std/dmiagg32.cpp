// Round 32: a default member initializer in braces for a member whose class is an aggregate with
// default member initializers (Components/VehicleSeatFit.hpp:39, `V3 floorPoint{0.0f, 0.6f, 0.0f};`):
// aggregate initialization ([dcl.init.list] p3.4), not a constructor call. And std::unique with a
// predicate ([alg.unique], Components/PoseCapture.hpp:267).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
namespace fit {
struct V3 { float x = 0, y = 0, z = 0; };
struct SeatAnchor {
    V3 floorPoint{0.0f, 0.6f, 0.0f};
    float yawDeg = 180.0f;
    V3 partial{2.0f};
    V3 none{};
};
}
int main() {
    fit::SeatAnchor a;
    std::printf("%g %g %g %g | %g %g %g | %g\n", a.floorPoint.x, a.floorPoint.y, a.floorPoint.z, a.yawDeg, a.partial.x,
                a.partial.y, a.partial.z, a.none.z);
    std::vector<float> t{1.0f, 1.00001f, 2.0f, 2.0f, 2.00002f, 3.0f, 1.0f};
    t.erase(std::unique(t.begin(), t.end(), [](float x, float y) { return std::fabs(x - y) < 1e-4f; }), t.end());
    for (float f : t) std::printf("%g ", f);
    std::vector<int> u{1, 1, 2, 3, 3, 3, 4};
    u.erase(std::unique(u.begin(), u.end()), u.end());
    std::printf("| %zu %d\n", u.size(), u.back());
    return 0;
}
