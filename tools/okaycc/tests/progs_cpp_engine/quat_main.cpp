// Driver for engine/include/okay/Math/Quat.hpp alone (round 24: overloaded member operators on
// call results, qy * qx * qz with Quat * Quat beside Quat * Vec3; overloaded static members,
// Euler(Vec3) calling Euler(x, y, z); braced returns; static Vec3 constants). The header is the
// engine part tested. Prints rotations, rotated vectors, angles and round trips.
#include "okay/Math/Quat.hpp"

#include <cstdio>

using okay::Quat;
using okay::Vec3;

static void show(const char* what, const Quat& q) { std::printf("%s %.4f %.4f %.4f %.4f\n", what, q.x, q.y, q.z, q.w); }
static void show(const char* what, const Vec3& v) { std::printf("%s %.4f %.4f %.4f\n", what, v.x, v.y, v.z); }

int main() {
    Quat a = Quat::Euler(30.0f, 45.0f, 60.0f);
    Quat b = Quat::Euler(Vec3{10.0f, -20.0f, 5.0f});
    show("a", a);
    show("b", b);
    show("ab", a * b);
    show("a*right", a * Vec3::Right);
    show("inverse", a.Inverse());
    show("normalized", (a * b * a.Inverse()).Normalized());
    show("euler", a.ToEuler());
    show("axis90", Quat::AngleAxis(90.0f, Vec3::Up) * Vec3::Forward);
    show("slerp", Quat::Slerp(a, b, 0.25f));
    show("lerp", Quat::Lerp(a, b, 0.75f));
    show("look", Quat::LookRotation(Vec3{1.0f, 0.0f, 1.0f}));
    show("fromto", Quat::FromToRotation(Vec3::Right, Vec3::Up));
    show("towards", Quat::RotateTowards(a, b, 5.0f));
    std::printf("dot %.4f angle %.3f\n", Quat::Dot(a, b), Quat::Angle(a, b));
    float deg = 0.0f;
    Vec3 axis{0.0f, 0.0f, 0.0f};
    Quat::AngleAxis(40.0f, Vec3{0.0f, 1.0f, 0.0f}).ToAngleAxis(deg, axis);
    std::printf("toangleaxis %.3f\n", deg);
    show("axis", axis);
    return 0;
}
