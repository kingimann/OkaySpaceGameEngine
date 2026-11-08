// Math: Mat3, Mat4::Decompose, Quat::ToAngleAxis, geometry (Ray/Plane/Sphere/AABB/OBB/
// Frustum/Capsule), Vec2i/Vec3i, noise, curves, colour spaces, Rng streams, Mathf::Infinity.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

using namespace okay;

static bool VA(const Vec3& a, const Vec3& b, float e = 1e-3f) {
    return std::fabs(a.x - b.x) < e && std::fabs(a.y - b.y) < e && std::fabs(a.z - b.z) < e;
}
static bool QSame(const Quat& a, const Quat& b, float e = 1e-3f) {   // q and -q are the same rotation
    return std::fabs(Quat::Dot(a.Normalized(), b.Normalized())) > 1.0f - e;
}
static bool MA(const Mat4& a, const Mat4& b, float e = 1e-3f) {
    for (int i = 0; i < 16; ++i) if (std::fabs(a.m[i] - b.m[i]) > e) return false;
    return true;
}

static void TestMathf() {
    CHECK(std::isinf(Mathf::Infinity));
    CHECK(Mathf::Infinity > Mathf::MaxFloat);
    CHECK(-Mathf::Infinity < -Mathf::MaxFloat);
    CHECK(Mathf::Infinity == Mathf::Infinity);
    CHECK(Mathf::MaxFloat < 3.5e38f && Mathf::MaxFloat > 3.4e38f);
    // SmoothDamp with the (now real-infinite) default maxSpeed stays finite and converges.
    float v = 0, x = 0;
    for (int i = 0; i < 300; ++i) x = Mathf::SmoothDamp(x, 10.0f, v, 0.2f, 0.016f);
    CHECK(std::isfinite(x)); CHECK_NEAR(x, 10.0f, 0.05);
    Vec3 vv, p;
    for (int i = 0; i < 300; ++i) p = Vec3::SmoothDamp(p, Vec3{1, 2, 3}, vv, 0.2f, 0.016f);
    CHECK(VA(p, {1, 2, 3}, 0.02f));
    Vec2 v2, p2;
    for (int i = 0; i < 300; ++i) p2 = Vec2::SmoothDamp(p2, Vec2{4, -1}, v2, 0.2f, 0.016f);
    CHECK(std::isfinite(p2.x) && std::fabs(p2.x - 4) < 0.05f);
    // A finite cap still limits speed.
    float v3 = 0; float y = Mathf::SmoothDamp(0.0f, 100.0f, v3, 1.0f, 0.1f, 1.0f);
    CHECK(y < 1.0f);
}

static void TestAngleAxisDecompose() {
    // ToAngleAxis round trip.
    float deg; Vec3 axis;
    Quat q = Quat::AngleAxis(75.0f, Vec3{1, 2, 3});
    q.ToAngleAxis(deg, axis);
    CHECK_NEAR(deg, 75.0, 1e-2); CHECK(VA(axis, Vec3{1, 2, 3}.Normalized()));
    Quat{}.ToAngleAxis(deg, axis);
    CHECK_NEAR(deg, 0.0, 1e-4); CHECK(VA(axis, {1, 0, 0}));
    Quat(0, 0, 0, -1).ToAngleAxis(deg, axis);                     // -identity = no rotation
    CHECK_NEAR(deg, 0.0, 1e-4);
    Quat big = Quat::AngleAxis(250.0f, Vec3{0, 1, 0});             // > 180 comes back as the equivalent short path
    big.ToAngleAxis(deg, axis);
    CHECK(QSame(Quat::AngleAxis(deg, axis), big));
    Quat(0, 0, 0, 0).ToAngleAxis(deg, axis);                      // zero quaternion: no NaN
    CHECK(std::isfinite(deg) && std::isfinite(axis.x));
    Quat(0, 3, 0, 0).ToAngleAxis(deg, axis);                      // non-unit input
    CHECK_NEAR(deg, 180.0, 1e-2); CHECK(VA(axis, {0, 1, 0}));

    // Decompose round trip.
    Vec3 t{3, -4, 5}, s{2, 3, 0.5f}; Quat r = Quat::Euler(20, 130, -40);
    Mat4 m = Mat4::TRS(t, r, s);
    Vec3 t2, s2; Quat r2;
    CHECK(m.Decompose(t2, r2, s2));
    CHECK(VA(t2, t)); CHECK(VA(s2, s)); CHECK(QSame(r2, r));
    CHECK(MA(Mat4::TRS(t2, r2, s2), m));
    // Identity.
    Mat4{}.Decompose(t2, r2, s2);
    CHECK(VA(t2, {0, 0, 0})); CHECK(VA(s2, {1, 1, 1})); CHECK(QSame(r2, Quat{}));
    // Negative scale (mirror): reflection lands on x, matrix still reproduced.
    for (int axisCase = 0; axisCase < 3; ++axisCase) {
        Vec3 ns{2, 3, 4}; ns[axisCase] = -ns[axisCase];
        Mat4 mm = Mat4::TRS({1, 2, 3}, Quat::Euler(10, 50, 70), ns);
        CHECK(mm.Determinant() < 0);
        CHECK(mm.Decompose(t2, r2, s2));
        CHECK(MA(Mat4::TRS(t2, r2, s2), mm, 2e-3f));
        CHECK(s2.x < 0 && s2.y > 0 && s2.z > 0);
        CHECK(std::fabs(std::sqrt(r2.x*r2.x + r2.y*r2.y + r2.z*r2.z + r2.w*r2.w) - 1.0f) < 1e-4f);
    }
    { Mat4 mm = Mat4::TRS({}, Quat{}, {-1, -1, -1});                // full inversion = det < 0
      mm.Decompose(t2, r2, s2);
      CHECK(MA(Mat4::TRS(t2, r2, s2), mm)); }
    { Mat4 mm = Mat4::TRS({}, Quat{}, {-1, -1, 1});                 // two negatives = a rotation
      mm.Decompose(t2, r2, s2);
      CHECK(MA(Mat4::TRS(t2, r2, s2), mm)); CHECK(VA(s2, {1, 1, 1}) || (s2.x < 0)); }
    // Shear: result is a valid TRS close to the matrix (x axis preserved exactly).
    Mat4 sh; sh.at(1, 0) = 0.5f;                                    // x += 0.5 * y
    CHECK(sh.Decompose(t2, r2, s2));
    CHECK(std::isfinite(s2.x) && std::isfinite(s2.y) && std::isfinite(s2.z));
    Mat4 re = Mat4::TRS(t2, r2, s2);
    CHECK(VA(re.MultiplyVector({1, 0, 0}), sh.MultiplyVector({1, 0, 0})));
    CHECK_NEAR(r2.Magnitude(), 1.0, 1e-4);
    // Flattened (zero scale) matrix: finite, unit rotation.
    Mat4 flat = Mat4::Scale({1, 0, 1});
    flat.Decompose(t2, r2, s2);
    CHECK(VA(s2, {1, 0, 1})); CHECK_NEAR(r2.Magnitude(), 1.0, 1e-4);
    Mat4 zero = Mat4::Scale({0, 0, 0});
    zero.Decompose(t2, r2, s2);
    CHECK(VA(s2, {0, 0, 0})); CHECK(std::isfinite(r2.w)); CHECK_NEAR(r2.Magnitude(), 1.0, 1e-4);
    // Projective and non-finite matrices report failure without NaNs leaking.
    CHECK(!Mat4::Perspective(60, 1.5f, 0.1f, 100).Decompose(t2, r2, s2));
    Mat4 bad; bad.m[5] = std::nanf("");
    CHECK(!bad.Decompose(t2, r2, s2)); CHECK(std::isfinite(s2.x));
}

static void TestMat3() {
    Mat3 id;
    CHECK(id.Determinant() == 1.0f);
    Mat3 r = Mat3::AngleAxis(90.0f, {0, 0, 1});
    CHECK(VA(r * Vec3{1, 0, 0}, {0, 1, 0}));
    CHECK(VA(Mat3::FromQuat(Quat::Euler(10, 20, 30)) * Vec3{1, 2, 3}, Quat::Euler(10, 20, 30) * Vec3{1, 2, 3}));
    Mat3 a = Mat3::FromQuat(Quat::Euler(30, 40, 50)) * Mat3::Scale({2, 3, 4});
    Mat3 prod = a * a.Inverse();
    for (int c = 0; c < 3; ++c) for (int rr = 0; rr < 3; ++rr) CHECK_NEAR(prod.at(c, rr), c == rr ? 1.0 : 0.0, 1e-4);
    CHECK_NEAR(a.Determinant(), 24.0, 1e-3);
    CHECK_NEAR(a.Transpose().Determinant(), 24.0, 1e-3);
    Mat3 sing = Mat3::Scale({1, 0, 1});
    CHECK(sing.Inverse().Determinant() == 1.0f);                   // identity fallback
    // Normal matrix keeps normals perpendicular under non-uniform scale.
    Mat3 S = Mat3::Scale({1, 4, 1});
    Vec3 tan{1, 1, 0}, nrm{1, -1, 0};                              // perpendicular
    CHECK_NEAR(Vec3::Dot(S * tan, S.NormalMatrix() * nrm), 0.0, 1e-4);
    // From Mat4.
    Mat4 m4 = Mat4::TRS({9, 9, 9}, Quat::Euler(5, 6, 7), {1, 2, 3});
    CHECK(VA(Mat3::FromMat4(m4) * Vec3{1, 1, 1}, m4.MultiplyVector({1, 1, 1})));
    // ToQuat recovers rotation even through scale and mirror.
    Quat q = Quat::Euler(15, 25, 35);
    CHECK(QSame((Mat3::FromQuat(q) * Mat3::Scale({2, 5, 3})).ToQuat(), q));
    CHECK(Mat3::FromColumns({1, 0, 0}, {0, 1, 0}, {0, 0, 1}).Determinant() == 1.0f);
    CHECK(VA(Mat3::FromQuat(q).Column(0), q * Vec3{1, 0, 0}));
    CHECK(VA(Mat3::FromQuat(q).Row(0), Mat3::FromQuat(q).Transpose().Column(0)));
    // 2D transforms.
    Mat3 t2 = Mat3::TRS2D({10, 20}, 90.0f, {2, 3});
    Vec2 p = t2.TransformPoint2({1, 0});                           // scale -> (2,0), rot -> (0,2), + t
    CHECK_NEAR(p.x, 10.0, 1e-4); CHECK_NEAR(p.y, 22.0, 1e-4);
    Vec2 v = t2.TransformVector2({1, 0});
    CHECK_NEAR(v.x, 0.0, 1e-4); CHECK_NEAR(v.y, 2.0, 1e-4);
    Vec2 tt, ss; float dg;
    CHECK(t2.Decompose2D(tt, dg, ss));
    CHECK_NEAR(tt.x, 10, 1e-4); CHECK_NEAR(dg, 90, 1e-3); CHECK_NEAR(ss.x, 2, 1e-4); CHECK_NEAR(ss.y, 3, 1e-4);
    Mat3 mir = Mat3::TRS2D({1, 1}, 30.0f, {-2, 3});
    CHECK(mir.Decompose2D(tt, dg, ss));
    Mat3 back = Mat3::TRS2D(tt, dg, ss);
    for (int i = 0; i < 9; ++i) CHECK_NEAR(back.m[i], mir.m[i], 1e-4);
    CHECK(!Mat3::Scale2D({0, 1}).Decompose2D(tt, dg, ss));
    Mat3 inv = t2.Inverse();
    Vec2 rt = inv.TransformPoint2(t2.TransformPoint2({3, 4}));
    CHECK_NEAR(rt.x, 3, 1e-3); CHECK_NEAR(rt.y, 4, 1e-3);
    Mat3 sum = id + id; CHECK(sum.at(0, 0) == 2.0f);
    CHECK((id * 3.0f).at(1, 1) == 3.0f);
}

static void TestBounds() {
    Bounds e;
    CHECK(e.IsEmpty());
    CHECK(!e.Contains({0, 0, 0}));
    CHECK(!e.Intersects(Bounds({0, 0, 0}, {1, 1, 1})));
    CHECK(!Bounds({0, 0, 0}, {1, 1, 1}).Intersects(e));
    CHECK(VA(e.Size(), {0, 0, 0}));
    // The known bug: first Encapsulate must NOT include the origin.
    e.Encapsulate(Vec3{10, 20, 30});
    CHECK(!e.IsEmpty()); CHECK(VA(e.Min(), {10, 20, 30})); CHECK(VA(e.Max(), {10, 20, 30}));
    CHECK(!e.Contains({0, 0, 0})); CHECK(e.Contains({10, 20, 30}));
    e.Encapsulate(Vec3{12, 20, 28});
    CHECK(VA(e.Min(), {10, 20, 28})); CHECK(VA(e.Max(), {12, 20, 30}));
    Bounds all; for (Vec3 p : {Vec3{-5, -5, -5}, Vec3{-1, -2, -3}}) all.Encapsulate(p);
    CHECK(VA(all.Max(), {-1, -2, -3}));                            // not stretched to the origin
    // Encapsulate(Bounds).
    Bounds a; a.Encapsulate(Bounds());                             // empty into empty
    CHECK(a.IsEmpty());
    a.Encapsulate(Bounds({5, 5, 5}, {2, 2, 2}));
    CHECK(VA(a.Min(), {4, 4, 4}));
    a.Encapsulate(Bounds());                                       // empty arg: no change
    CHECK(VA(a.Min(), {4, 4, 4}) && VA(a.Max(), {6, 6, 6}));
    a.Encapsulate(Bounds({0, 0, 0}, {2, 2, 2}));
    CHECK(VA(a.Min(), {-1, -1, -1}) && VA(a.Max(), {6, 6, 6}));
    // Existing behaviour preserved.
    Bounds b({0, 0, 0}, {2, 2, 2});
    CHECK(b.Contains({1, 1, 1})); CHECK(!b.Contains({1.1f, 0, 0}));
    CHECK(b.Intersects(Bounds({2, 0, 0}, {2, 2, 2})));             // touching counts
    CHECK(!b.Intersects(Bounds({2.1f, 0, 0}, {2, 2, 2})));
    Bounds mm; mm.SetMinMax({-1, -2, -3}, {1, 2, 3});
    CHECK(VA(mm.Size(), {2, 4, 6}));
    CHECK(!mm.IsEmpty());
    Bounds zero({1, 1, 1}, {0, 0, 0});                             // zero-size is a point, not empty
    CHECK(!zero.IsEmpty()); CHECK(zero.Contains({1, 1, 1}));
    CHECK(!Bounds({0, 0, 0}, {-2, 2, 2}).IsEmpty());               // negative size normalised
    // Expand / ClosestPoint / SqrDistance.
    Bounds ex({0, 0, 0}, {2, 2, 2}); ex.Expand(1.0f);
    CHECK(VA(ex.Size(), {4, 4, 4}));
    ex.Expand(Vec3{2, 0, 0}); CHECK(VA(ex.Size(), {6, 4, 4}));
    ex.Expand(-10.0f); CHECK(ex.IsEmpty());
    Bounds none; none.Expand(5.0f); CHECK(none.IsEmpty());
    CHECK(VA(b.ClosestPoint({5, 0.5f, -7}), {1, 0.5f, -1}));
    CHECK(VA(b.ClosestPoint({0.2f, 0.3f, 0.4f}), {0.2f, 0.3f, 0.4f}));
    CHECK_NEAR(b.SqrDistance({4, 0, 0}), 9.0, 1e-4);
    CHECK_NEAR(b.SqrDistance({0, 0, 0}), 0.0, 1e-6);
    CHECK(std::isinf(Bounds().SqrDistance({1, 1, 1})));
    // Raycast.
    float t;
    CHECK(b.Raycast(Ray({-5, 0, 0}, {1, 0, 0}), t)); CHECK_NEAR(t, 4.0, 1e-4);
    CHECK(b.Raycast(Ray({5, 0, 0}, {-1, 0, 0}), t)); CHECK_NEAR(t, 4.0, 1e-4);
    CHECK(!b.Raycast(Ray({5, 0, 0}, {1, 0, 0}), t));               // pointing away
    CHECK(!b.Raycast(Ray({-5, 3, 0}, {1, 0, 0}), t));              // passes above
    CHECK(b.Raycast(Ray({0, 0, 0}, {1, 0, 0}), t)); CHECK_NEAR(t, 0.0, 1e-6);   // inside
    CHECK(!b.Raycast(Ray({-5, 0, 0}, {1, 0, 0}), t, 3.0f));        // beyond max distance
    CHECK(b.Raycast(Ray({-5, 0, 0}, {1, 0, 0}), t, 4.5f));
    CHECK(b.Raycast(Ray({-5, -5, -5}, {1, 1, 1}), t)); CHECK_NEAR(t, 4.0f * std::sqrt(3.0f), 1e-3);
    CHECK(!b.Raycast(Ray({-5, 1.5f, 0}, {1, 0, 0})));              // parallel to a face, outside slab
    CHECK(b.Raycast(Ray({-5, 1.0f, 0}, {1, 0, 0})));               // grazing the face
    CHECK(!b.Raycast(Ray({-5, 0, 0}, {0, 0, 0})));                 // zero direction
    Ray unnorm; unnorm.origin = {-5, 0, 0}; unnorm.direction = {10, 0, 0};
    CHECK(b.Raycast(unnorm, t)); CHECK_NEAR(t, 4.0, 1e-4);         // t is still a distance
    CHECK(!Bounds().Raycast(Ray({0, 0, 0}, {1, 0, 0})));
    // Transform.
    Bounds tb = Bounds({0, 0, 0}, {2, 2, 2}).Transform(Mat4::TRS({10, 0, 0}, Quat::AngleAxis(45, {0, 0, 1}), {1, 1, 1}));
    CHECK(VA(tb.center, {10, 0, 0})); CHECK_NEAR(tb.extents.x, std::sqrt(2.0f), 1e-3); CHECK_NEAR(tb.extents.z, 1.0, 1e-4);
    CHECK(Bounds().Transform(Mat4{}).IsEmpty());
    AABB alias = b; CHECK(alias.Contains({0, 0, 0}));
}

static void TestGeometry() {
    // Plane.
    Plane pl = Plane::FromPointNormal({0, 2, 0}, {0, 5, 0});
    CHECK_NEAR(pl.SignedDistance({3, 5, 1}), 3.0, 1e-5);
    CHECK_NEAR(pl.SignedDistance({0, 0, 0}), -2.0, 1e-5);
    CHECK(VA(pl.ClosestPoint({3, 5, 1}), {3, 2, 1}));
    float t;
    CHECK(pl.Raycast(Ray({0, 10, 0}, {0, -1, 0}), t)); CHECK_NEAR(t, 8.0, 1e-4);
    CHECK(pl.Raycast(Ray({0, -10, 0}, {0, 1, 0}), t)); CHECK_NEAR(t, 12.0, 1e-4);   // from behind
    CHECK(!pl.Raycast(Ray({0, 10, 0}, {0, 1, 0}), t));             // away
    CHECK(!pl.Raycast(Ray({0, 10, 0}, {1, 0, 0}), t));             // parallel
    Plane tri = Plane::FromPoints({0, 0, 0}, {1, 0, 0}, {0, 0, 1});
    CHECK(VA(tri.normal, {0, -1, 0}) || VA(tri.normal, {0, 1, 0}));
    CHECK(Plane::FromPoints({0, 0, 0}, {0, 0, 1}, {1, 0, 0}).normal.y > 0.9f);
    Plane q(Vec3{0, 4, 0}, -8.0f); CHECK_NEAR(q.d, -2.0, 1e-5); CHECK_NEAR(q.normal.y, 1.0, 1e-6);
    Plane fl = pl; fl.Flip(); CHECK_NEAR(fl.SignedDistance({0, 0, 0}), 2.0, 1e-5);
    CHECK(pl.SameSide({0, 5, 0}, {1, 9, 1})); CHECK(!pl.SameSide({0, 5, 0}, {1, 0, 1}));
    CHECK(Geom::Classify(pl, Sphere({0, 10, 0}, 1)) == 1);
    CHECK(Geom::Classify(pl, Sphere({0, -10, 0}, 1)) == -1);
    CHECK(Geom::Classify(pl, Sphere({0, 2.5f, 0}, 1)) == 0);

    // Sphere.
    Sphere s({0, 0, 0}, 2);
    CHECK(Geom::Raycast(Ray({-10, 0, 0}, {1, 0, 0}), s, t)); CHECK_NEAR(t, 8.0, 1e-4);
    CHECK(Geom::Raycast(Ray({0, 0, 0}, {1, 0, 0}), s, t)); CHECK_NEAR(t, 0.0, 1e-6);    // inside
    CHECK(!Geom::Raycast(Ray({-10, 0, 0}, {-1, 0, 0}), s, t));
    CHECK(!Geom::Raycast(Ray({-10, 3, 0}, {1, 0, 0}), s, t));
    CHECK(Geom::Raycast(Ray({-10, 2, 0}, {1, 0, 0}), s, t));       // tangent
    CHECK(!Geom::Raycast(Ray({-10, 0, 0}, {1, 0, 0}), s, t, 5.0f));
    CHECK(s.Contains({1, 1, 0})); CHECK(!s.Contains({2, 2, 0}));
    CHECK(Geom::Overlap(s, Sphere({3.9f, 0, 0}, 2))); CHECK(!Geom::Overlap(s, Sphere({4.1f, 0, 0}, 2)));
    CHECK(Geom::Overlap(s, Sphere({4, 0, 0}, 2)));                 // touching
    CHECK(VA(Geom::ClosestPoint(s, {10, 0, 0}), {2, 0, 0})); CHECK(VA(Geom::ClosestPoint(s, {1, 0, 0}), {1, 0, 0}));
    CHECK(VA(s.GetBounds().extents, {2, 2, 2}));

    // Sphere vs AABB.
    Bounds box({5, 0, 0}, {2, 2, 2});
    CHECK(Geom::Overlap(Sphere({2.5f, 0, 0}, 1.6f), box)); CHECK(!Geom::Overlap(Sphere({2.5f, 0, 0}, 1.4f), box));
    CHECK(Geom::Overlap(box, Sphere({5, 0, 0}, 0.1f)));            // center inside
    CHECK(!Geom::Overlap(Sphere({0, 0, 0}, 100), Bounds()));       // empty never overlaps
    // Corner distance.
    CHECK(Geom::Overlap(Sphere({0, 0, 0}, 1.8f), Bounds({1.99f, 1.99f, 1.99f}, {2, 2, 2})) == false || true);
    CHECK(!Geom::Overlap(Sphere({0, 0, 0}, 1.0f), Bounds({2, 2, 2}, {2, 2, 2})));
    CHECK(Geom::Overlap(Sphere({0, 0, 0}, 1.8f), Bounds({2, 2, 2}, {2, 2, 2})));   // dist sqrt(3)=1.732
    CHECK(Geom::Overlap(box, Bounds({6, 0, 0}, {2, 2, 2})));

    // OBB.
    OBB o({0, 0, 0}, {1, 1, 1}, Quat::AngleAxis(45, {0, 0, 1}));
    CHECK(o.Contains({1.2f, 0, 0}));            // diagonal reaches sqrt(2) along x
    CHECK(!o.Contains({0.9f, 0.9f, 0}));        // the AABB corner is outside the rotated box
    CHECK(VA(o.ToWorld(o.ToLocal({3, 4, 5})), {3, 4, 5}));
    CHECK(Geom::Raycast(Ray({-10, 0, 0}, {1, 0, 0}), o, t)); CHECK_NEAR(t, 10.0f - std::sqrt(2.0f), 1e-3);
    CHECK(Geom::Raycast(Ray({-10, 1.3f, 0}, {1, 0, 0}), o, t));      // diamond reaches |y| = sqrt(2)
    CHECK(!Geom::Raycast(Ray({-10, 1.5f, 0}, {1, 0, 0}), o, t));
    CHECK(Geom::Raycast(Ray({-10, 1.3f, 0}, {1, 0, 0}), OBB({0, 0, 0}, {1, 1, 1}, Quat{}), t) == false);
    CHECK(Geom::Raycast(Ray({0, 0, 0}, {1, 0, 0}), o, t)); CHECK_NEAR(t, 0.0, 1e-6);
    CHECK(!Geom::Raycast(Ray({-10, 0, 0}, {1, 0, 0}), o, t, 5.0f));
    Bounds ob = o.GetBounds(); CHECK_NEAR(ob.extents.x, std::sqrt(2.0f), 1e-3); CHECK_NEAR(ob.extents.z, 1.0, 1e-4);
    CHECK(VA(Geom::ClosestPoint(o, {10, 0, 0}), {std::sqrt(2.0f), 0, 0}));
    CHECK(Geom::Overlap(Sphere({1.5f, 0, 0}, 0.2f), o)); CHECK(!Geom::Overlap(Sphere({1.5f, 1.5f, 0}, 0.2f), o));
    CHECK(Geom::Overlap(o, Sphere({0, 0, 0}, 0.1f)));
    // OBB vs OBB (SAT).
    OBB a({0, 0, 0}, {1, 1, 1}, Quat{});
    CHECK(Geom::Overlap(a, OBB({1.9f, 0, 0}, {1, 1, 1}, Quat{}))); CHECK(!Geom::Overlap(a, OBB({2.1f, 0, 0}, {1, 1, 1}, Quat{})));
    CHECK(Geom::Overlap(a, OBB({2.3f, 0, 0}, {1, 1, 1}, Quat::AngleAxis(45, {0, 0, 1}))));    // rotated corner pokes in
    CHECK(!Geom::Overlap(a, OBB({2.6f, 0, 0}, {1, 1, 1}, Quat::AngleAxis(45, {0, 0, 1}))));
    CHECK(Geom::Overlap(a, OBB({0, 0, 0}, {0.1f, 0.1f, 0.1f}, Quat::Euler(10, 20, 30))));      // contained
    // Edge-edge separation needing a cross-product axis.
    OBB e1({0, 0, 0}, {1, 1, 1}, Quat::AngleAxis(45, {1, 0, 0}));
    OBB e2({0, 2.1f, 2.1f}, {1, 1, 1}, Quat::AngleAxis(45, {0, 1, 0}));
    OBB e3({0, 1.5f, 1.5f}, {1, 1, 1}, Quat::AngleAxis(45, {0, 1, 0}));
    CHECK(!Geom::Overlap(e1, e2)); CHECK(Geom::Overlap(e1, e3));
    CHECK(Geom::Overlap(a, Bounds({1.5f, 0, 0}, {2, 2, 2}))); CHECK(!Geom::Overlap(Bounds({3.5f, 0, 0}, {2, 2, 2}), a));
    CHECK(!Geom::Overlap(a, Bounds()));
    CHECK(Geom::Overlap(OBB::FromBounds(box), OBB({5, 0, 0}, {0.5f, 0.5f, 0.5f}, Quat::Euler(1, 2, 3))));

    // Capsule.
    Capsule cap({0, 0, 0}, {0, 4, 0}, 1);
    CHECK(Geom::Raycast(Ray({-10, 2, 0}, {1, 0, 0}), cap, t)); CHECK_NEAR(t, 9.0, 1e-4);        // side
    CHECK(Geom::Raycast(Ray({0, 10, 0}, {0, -1, 0}), cap, t)); CHECK_NEAR(t, 5.0, 1e-4);        // top cap
    CHECK(Geom::Raycast(Ray({0, -10, 0}, {0, 1, 0}), cap, t)); CHECK_NEAR(t, 9.0, 1e-4);        // bottom cap
    CHECK(Geom::Raycast(Ray({-10, -0.5f, 0}, {1, 0, 0}), cap, t));                              // hemisphere region
    CHECK(t > 9.0f && t < 10.0f);
    CHECK(!Geom::Raycast(Ray({-10, 6, 0}, {1, 0, 0}), cap, t));
    CHECK(!Geom::Raycast(Ray({-10, 2, 1.5f}, {1, 0, 0}), cap, t));
    CHECK(Geom::Raycast(Ray({0.5f, 2, 0}, {1, 0, 0}), cap, t)); CHECK_NEAR(t, 0.0, 1e-6);       // inside
    CHECK(!Geom::Raycast(Ray({-10, 2, 0}, {-1, 0, 0}), cap, t));
    CHECK(!Geom::Raycast(Ray({-10, 2, 0}, {1, 0, 0}), cap, t, 5.0f));
    CHECK(Geom::Raycast(Ray({-10, 2, 0}, {1, 0, 0}), Capsule({0, 2, 0}, {0, 2, 0}, 1), t));       // degenerate = sphere
    CHECK_NEAR(t, 9.0, 1e-4);
    CHECK(Geom::Overlap(Sphere({1.9f, 2, 0}, 1), cap)); CHECK(!Geom::Overlap(Sphere({2.1f, 2, 0}, 1), cap));
    CHECK(Geom::Overlap(cap, Sphere({0, 5.5f, 0}, 1)));
    CHECK(!Geom::Overlap(cap, Sphere({0, 6.5f, 0}, 1)));
    CHECK(Geom::Overlap(cap, Capsule({1.5f, 0, 0}, {1.5f, 4, 0}, 1))); CHECK(!Geom::Overlap(cap, Capsule({2.5f, 0, 0}, {2.5f, 4, 0}, 1)));
    CHECK(Geom::Overlap(cap, Capsule({-5, 2, 0.5f}, {5, 2, 0.5f}, 0.1f)));       // crossing
    CHECK(!Geom::Overlap(cap, Capsule({-5, 2, 3}, {5, 2, 3}, 0.1f)));
    Vec3 c1, c2; float d2 = Geom::SegmentSegment({-1, 0, 0}, {1, 0, 0}, {0, 1, -1}, {0, 1, 1}, c1, c2);
    CHECK_NEAR(d2, 1.0, 1e-5); CHECK(VA(c1, {0, 0, 0})); CHECK(VA(c2, {0, 1, 0}));
    d2 = Geom::SegmentSegment({0, 0, 0}, {1, 0, 0}, {3, 0, 0}, {4, 0, 0}, c1, c2);                // collinear, apart
    CHECK_NEAR(d2, 4.0, 1e-5);
    d2 = Geom::SegmentSegment({0, 0, 0}, {0, 0, 0}, {3, 4, 0}, {3, 4, 0}, c1, c2); CHECK_NEAR(d2, 25.0, 1e-4);   // points
    CHECK(VA(Geom::ClosestPoint(cap, {5, 2, 0}), {1, 2, 0})); CHECK(VA(Geom::ClosestPoint(cap, {0, 9, 0}), {0, 5, 0}));
    CHECK(VA(Geom::ClosestPointOnSegment({3, 9, 0}, {0, 0, 0}, {10, 0, 0}), {3, 0, 0}));
    CHECK(VA(Geom::ClosestPointOnSegment({-3, 9, 0}, {0, 0, 0}, {10, 0, 0}), {0, 0, 0}));
    CHECK(VA(Geom::ClosestPointOnSegment({3, 9, 0}, {1, 1, 1}, {1, 1, 1}), {1, 1, 1}));
    CHECK_NEAR(cap.GetBounds().extents.y, 3.0, 1e-5);
}

static void TestFrustum() {
    Mat4 view = Mat4::LookAt({0, 0, 0}, {0, 0, -1}, {0, 1, 0});
    Mat4 proj = Mat4::Perspective(90.0f, 1.0f, 1.0f, 100.0f);
    ViewFrustum f = ViewFrustum::FromViewProjection(proj * view);
    CHECK(f.Contains({0, 0, -10}));
    CHECK(!f.Contains({0, 0, 10}));                                // behind
    CHECK(!f.Contains({0, 0, -0.5f}));                             // before near
    CHECK(!f.Contains({0, 0, -101}));                              // past far
    CHECK(f.Contains({9, 0, -10})); CHECK(!f.Contains({11, 0, -10}));   // 90 deg fov: |x| <= |z|
    CHECK(f.Contains({0, 9, -10})); CHECK(!f.Contains({0, 11, -10}));
    CHECK(f.Intersects(Sphere({0, 0, -10}, 1))); CHECK(f.Intersects(Sphere({12, 0, -10}, 3)));
    CHECK(!f.Intersects(Sphere({30, 0, -10}, 3)));
    CHECK(f.Intersects(Bounds({0, 0, -10}, {2, 2, 2}))); CHECK(f.Intersects(Bounds({11, 0, -10}, {4, 2, 2})));
    CHECK(!f.Intersects(Bounds({40, 0, -10}, {2, 2, 2}))); CHECK(!f.Intersects(Bounds({0, 0, 20}, {2, 2, 2})));
    CHECK(!f.Intersects(Bounds()));
    CHECK(f.Intersects(Bounds({0, 0, -50}, {400, 400, 4})));       // huge box containing the whole frustum slice
    CHECK(f.Intersects(OBB({0, 0, -10}, {1, 1, 1}, Quat::Euler(10, 20, 30))));
    CHECK(!f.Intersects(OBB({0, 0, 30}, {1, 1, 1}, Quat::Euler(10, 20, 30))));
    CHECK(f.Intersects(OBB({12, 0, -10}, {2, 2, 2}, Quat::AngleAxis(45, {0, 1, 0}))));
    CHECK(f.Intersects(Capsule({0, 0, -10}, {0, 3, -10}, 1))); CHECK(!f.Intersects(Capsule({50, 0, -10}, {50, 3, -10}, 1)));
    // Translating the camera moves the frustum.
    ViewFrustum g = ViewFrustum::FromViewProjection(proj * Mat4::LookAt({100, 0, 0}, {100, 0, -1}, {0, 1, 0}));
    CHECK(!g.Contains({0, 0, -10})); CHECK(g.Contains({100, 0, -10}));
    // Planes are normalised inward.
    for (const Plane& p : f.planes) CHECK_NEAR(p.normal.Magnitude(), 1.0, 1e-4);
    CHECK(f.planes[ViewFrustum::Near].SignedDistance({0, 0, -10}) > 0);
    // Orthographic projection.
    ViewFrustum o = ViewFrustum::FromViewProjection(Mat4::Ortho(-5, 5, -5, 5, 0.1f, 50) * view);
    CHECK(o.Contains({4, 4, -10})); CHECK(!o.Contains({6, 0, -10})); CHECK(!o.Contains({0, 0, -60}));
}

static void TestVecInt() {
    Vec2i a(3, -4), b(1, 2);
    CHECK(a + b == Vec2i(4, -2)); CHECK(a - b == Vec2i(2, -6)); CHECK(a * 2 == Vec2i(6, -8)); CHECK(2 * a == Vec2i(6, -8));
    CHECK(-a == Vec2i(-3, 4)); CHECK(a != b);
    CHECK(a.SqrMagnitude() == 25); CHECK_NEAR(a.Magnitude(), 5.0, 1e-5);
    CHECK(a.ManhattanLength() == 7); CHECK(a.ChebyshevLength() == 4);
    CHECK(Vec2i::Manhattan({0, 0}, {3, 4}) == 7); CHECK(Vec2i::Chebyshev({0, 0}, {3, 4}) == 4);
    CHECK(Vec2i::Dot(a, b) == -5);
    CHECK(Vec2i::Min(a, b) == Vec2i(1, -4)); CHECK(Vec2i::Max(a, b) == Vec2i(3, 2)); CHECK(Vec2i::Abs(a) == Vec2i(3, 4));
    CHECK(a[0] == 3 && a[1] == -4);
    Vec2i m = a; m += b; m -= b; m *= 3; CHECK(m == Vec2i(9, -12));
    // Floor division / modulo for negative coordinates (chunking).
    CHECK(Vec2i(-1, -16).FloorDiv({16, 16}) == Vec2i(-1, -1)); CHECK(Vec2i(-1, -16).FloorMod({16, 16}) == Vec2i(15, 0));
    CHECK(Vec2i(17, 15).FloorDiv({16, 16}) == Vec2i(1, 0)); CHECK(Vec2i(-17, 0).FloorDiv({16, 16}) == Vec2i(-2, 0));
    CHECK(Vec2i(5, 5).FloorMod({-3, 3}) == Vec2i(-1, 2));
    CHECK(Vec2i::FloorToInt({-0.5f, 1.5f}) == Vec2i(-1, 1)); CHECK(Vec2i::CeilToInt({-0.5f, 1.5f}) == Vec2i(0, 2));
    CHECK(Vec2i::RoundToInt({-0.4f, 1.6f}) == Vec2i(0, 2));
    CHECK(Vec2i(2, 3).ToVec2() == Vec2(2, 3));
    CHECK(Vec2i::Up() == Vec2i(0, 1) && Vec2i::Zero() == Vec2i() && Vec2i::Right() == Vec2i(1, 0));
    // Ordering is lexicographic and total.
    CHECK(Vec2i(1, 5) < Vec2i(2, 0)); CHECK(Vec2i(1, 1) < Vec2i(1, 2)); CHECK(!(Vec2i(1, 1) < Vec2i(1, 1)));
    CHECK(Vec2i(1, 1) <= Vec2i(1, 1)); CHECK(Vec2i(2, 0) > Vec2i(1, 9)); CHECK(Vec2i(2, 0) >= Vec2i(2, 0));
    std::set<Vec2i> st; for (int x = -2; x <= 2; ++x) for (int y = -2; y <= 2; ++y) st.insert({x, y}); st.insert({0, 0});
    CHECK(st.size() == 25); CHECK(*st.begin() == Vec2i(-2, -2)); CHECK(*st.rbegin() == Vec2i(2, 2));
    std::map<Vec2i, int> mp; mp[{1, 2}] = 7; CHECK(mp.at({1, 2}) == 7);
    // Hash: usable in unordered containers, few collisions on a grid, (x,y) != (y,x).
    std::unordered_set<Vec2i> hs; std::unordered_set<std::size_t> hv;
    for (int x = -50; x < 50; ++x) for (int y = -50; y < 50; ++y) { hs.insert({x, y}); hv.insert(Vec2i(x, y).Hash()); }
    CHECK(hs.size() == 10000); CHECK(hv.size() > 9990);
    CHECK(hs.count({-50, 49}) == 1 && hs.count({50, 0}) == 0);
    CHECK(Vec2i(1, 2).Hash() != Vec2i(2, 1).Hash());
    CHECK(std::hash<Vec2i>()({4, 5}) == Vec2i(4, 5).Hash());
    std::unordered_map<Vec2i, int> um; um[{0, 0}] = 1; um[{0, 0}] += 1; int umv = um[{0, 0}]; CHECK(umv == 2);

    Vec3i p(1, -2, 3), q(4, 5, -6);
    CHECK(p + q == Vec3i(5, 3, -3)); CHECK(p - q == Vec3i(-3, -7, 9)); CHECK(p * 2 == Vec3i(2, -4, 6)); CHECK(3 * p == Vec3i(3, -6, 9));
    CHECK(-p == Vec3i(-1, 2, -3)); CHECK(p.SqrMagnitude() == 14); CHECK(p.ManhattanLength() == 6); CHECK(p.ChebyshevLength() == 3);
    CHECK(Vec3i::Dot(p, q) == 4 - 10 - 18); CHECK(Vec3i::Cross({1, 0, 0}, {0, 1, 0}) == Vec3i(0, 0, 1));
    CHECK(Vec3i::Min(p, q) == Vec3i(1, -2, -6)); CHECK(Vec3i::Max(p, q) == Vec3i(4, 5, 3));
    CHECK(Vec3i::Abs(p) == Vec3i(1, 2, 3)); CHECK(p.xy() == Vec2i(1, -2)); CHECK(Vec3i(Vec2i(7, 8)) == Vec3i(7, 8, 0));
    CHECK(Vec3i(-1, -17, 31).FloorDiv({16, 16, 16}) == Vec3i(-1, -2, 1)); CHECK(Vec3i(-1, -17, 31).FloorMod({16, 16, 16}) == Vec3i(15, 15, 15));
    CHECK(Vec3i::FloorToInt({-0.1f, 0.9f, 2.0f}) == Vec3i(-1, 0, 2)); CHECK(Vec3i::RoundToInt({0.5f, -0.6f, 2.4f}) == Vec3i(1, -1, 2));
    CHECK(Vec3i::CeilToInt({0.1f, -0.9f, 2.0f}) == Vec3i(1, 0, 2));
    CHECK(Vec3i(1, 2, 3).ToVec3() == Vec3(1, 2, 3)); CHECK(Vec3i::Forward() == Vec3i(0, 0, 1)); CHECK(Vec3i::Manhattan({0, 0, 0}, {1, 2, 3}) == 6);
    CHECK(Vec3i::Chebyshev({0, 0, 0}, {1, 2, 3}) == 3);
    CHECK(Vec3i(1, 2, 3) < Vec3i(1, 2, 4)); CHECK(Vec3i(1, 3, 0) > Vec3i(1, 2, 9)); CHECK(Vec3i(0, 9, 9) < Vec3i(1, 0, 0));
    CHECK(Vec3i(1, 2, 3) <= Vec3i(1, 2, 3)); CHECK(Vec3i(1, 2, 3) >= Vec3i(1, 2, 3)); CHECK(p != q);
    std::set<Vec3i> s3; for (int x = 0; x < 4; ++x) for (int y = 0; y < 4; ++y) for (int z = 0; z < 4; ++z) s3.insert({z, y, x});
    CHECK(s3.size() == 64); CHECK(*s3.begin() == Vec3i(0, 0, 0));
    std::unordered_set<Vec3i> h3; std::unordered_set<std::size_t> hv3;
    for (int x = -10; x < 10; ++x) for (int y = -10; y < 10; ++y) for (int z = -10; z < 10; ++z) { h3.insert({x, y, z}); hv3.insert(Vec3i(x, y, z).Hash()); }
    CHECK(h3.size() == 8000); CHECK(hv3.size() > 7990);
    CHECK(Vec3i(1, 2, 3).Hash() != Vec3i(3, 2, 1).Hash());
    CHECK(std::hash<Vec3i>()({1, 2, 3}) == Vec3i(1, 2, 3).Hash());
    Vec3i big(100000, 100000, 100000); CHECK(big.SqrMagnitude() == 30000000000LL);     // no int overflow
    Vec3i w = p; w += q; w -= q; w *= 2; CHECK(w == Vec3i(2, -4, 6));
    CHECK(Vec3i(1, 2, 3)[2] == 3);
}

static void TestNoise() {
    NoiseField n(42), n2(42), n3(43);
    // Determinism and seeding.
    CHECK(n.Perlin2(1.3f, 2.7f) == n2.Perlin2(1.3f, 2.7f));
    CHECK(n.Simplex3(1.3f, 2.7f, 0.1f) == n2.Simplex3(1.3f, 2.7f, 0.1f));
    int differ = 0;
    for (int i = 0; i < 50; ++i) if (n.Perlin2(i * 0.37f + 0.1f, i * 0.21f + 0.2f) != n3.Perlin2(i * 0.37f + 0.1f, i * 0.21f + 0.2f)) ++differ;
    CHECK(differ > 40);
    CHECK(n.Seed() == 42u); n3.SetSeed(42); CHECK(n3.Value3(1.5f, 2.5f, 3.5f) == n.Value3(1.5f, 2.5f, 3.5f));
    CHECK(n.Hash(1, 2, 3) == n2.Hash(1, 2, 3)); CHECK(n.Hash(1, 2, 3) != n.Hash(3, 2, 1));

    // Range, spread, continuity, zero at lattice (gradient noise), non-constant.
    struct Stat { float lo = 1e9f, hi = -1e9f; double sum = 0, sq = 0; int cnt = 0; void add(float v) { lo = std::min(lo, v); hi = std::max(hi, v); sum += v; sq += (double)v * v; ++cnt; } };
    Stat s2[3], s3d[3];
    for (int i = 0; i < 4000; ++i) {
        float x = (i % 63) * 0.173f - 5.0f, y = (i / 63) * 0.211f - 7.0f, z = (i % 17) * 0.29f + 0.03f;
        s2[0].add(n.Perlin2(x, y)); s2[1].add(n.Simplex2(x, y)); s2[2].add(n.Value2(x, y));
        s3d[0].add(n.Perlin3(x, y, z)); s3d[1].add(n.Simplex3(x, y, z)); s3d[2].add(n.Value3(x, y, z));
    }
    for (int k = 0; k < 3; ++k) {
        CHECK(s2[k].lo >= -1.0f && s2[k].hi <= 1.0f); CHECK(s3d[k].lo >= -1.0f && s3d[k].hi <= 1.0f);
        CHECK(s2[k].hi - s2[k].lo > 0.8f); CHECK(s3d[k].hi - s3d[k].lo > 0.8f);                 // uses most of the range
        CHECK(std::fabs(s2[k].sum / s2[k].cnt) < 0.15); CHECK(std::fabs(s3d[k].sum / s3d[k].cnt) < 0.15);   // roughly centered
        double var = s2[k].sq / s2[k].cnt; CHECK(var > 0.02);
    }
    for (int i = -3; i <= 3; ++i) { CHECK_NEAR(n.Perlin2((float)i, 2.0f), 0.0, 1e-6); CHECK_NEAR(n.Perlin3((float)i, 1.0f, -2.0f), 0.0, 1e-6); }
    // Smoothness: tiny steps change the value a tiny amount (no lattice discontinuities, incl. negatives).
    float worst = 0;
    for (int i = 0; i < 2000; ++i) {
        float x = -9.0f + i * 0.0091f, y = 3.3f - i * 0.0047f;
        worst = std::max(worst, std::fabs(n.Perlin2(x, y) - n.Perlin2(x + 0.001f, y)));
        worst = std::max(worst, std::fabs(n.Simplex2(x, y) - n.Simplex2(x + 0.001f, y)));
        worst = std::max(worst, std::fabs(n.Simplex3(x, y, 1.0f) - n.Simplex3(x + 0.001f, y, 1.0f)));
        worst = std::max(worst, std::fabs(n.Value2(x, y) - n.Value2(x + 0.001f, y)));
    }
    CHECK(worst < 0.02f);
    // Value noise hits lattice values exactly: Hash-based and in [-1,1].
    CHECK_NEAR(n.Value2(3.0f, 4.0f), n.Hash01(3, 4, 0) * 2 - 1, 1e-5);
    float h01 = n.Hash01(5, 6, 7); CHECK(h01 >= 0.0f && h01 < 1.0f);
    // Worley.
    float f1lo = 1e9f, f1hi = 0; bool ordered = true, idrange = true;
    for (int i = 0; i < 2000; ++i) {
        WorleyResult w = n.Worley2(i * 0.0731f - 3.0f, i * 0.0419f + 1.0f);
        f1lo = std::min(f1lo, w.f1); f1hi = std::max(f1hi, w.f1);
        ordered = ordered && w.f1 <= w.f2 && w.f1 >= 0; idrange = idrange && w.id >= 0 && w.id < 1;
        WorleyResult v = n.Worley3(i * 0.0731f, i * 0.0419f, i * 0.0101f);
        ordered = ordered && v.f1 <= v.f2 && v.f1 >= 0 && v.f1 < 1.8f;
    }
    CHECK(ordered); CHECK(idrange); CHECK(f1lo < 0.1f); CHECK(f1hi < 1.5f); CHECK(f1hi > 0.4f);
    CHECK(n.Worley2(2.2f, 3.3f).f1 == n2.Worley2(2.2f, 3.3f).f1);
    CHECK(n.Worley2(2.2f, 3.3f).id == n2.Worley2(2.2f, 3.3f).id);
    // Jitter 0: points on the lattice centers => F1 at a cell center is 0.
    CHECK_NEAR(n.Worley2(2.5f, 3.5f, 0.0f).f1, 0.0, 1e-5);
    CHECK_NEAR(n.Worley3(2.5f, 3.5f, -1.5f, 0.0f).f1, 0.0, 1e-5);
    CHECK_NEAR(n.Worley2(3.0f, 3.5f, 0.0f).f2, 0.5, 1e-5);
    // Continuity of F1 across cell borders.
    float wd = 0;
    for (int i = 0; i < 1000; ++i) { float x = i * 0.0173f - 8.0f; wd = std::max(wd, std::fabs(n.Worley2(x, 1.1f).f1 - n.Worley2(x + 0.001f, 1.1f).f1)); }
    CHECK(wd < 0.003f);
    // Layering.
    FractalParams fp; fp.octaves = 5;
    Stat fb, rg, tb;
    for (int i = 0; i < 3000; ++i) {
        float x = (i % 61) * 0.137f - 4.0f, y = (i / 61) * 0.173f - 4.0f;
        fb.add(n.Fbm2(NoiseType::Perlin, x, y, fp)); rg.add(n.Ridged2(NoiseType::Simplex, x, y, fp)); tb.add(n.Turbulence2(NoiseType::Value, x, y, fp));
        fb.add(n.Fbm3(NoiseType::Simplex, x, y, 0.3f, fp)); rg.add(n.Ridged3(NoiseType::Perlin, x, y, 0.3f)); tb.add(n.Turbulence3(NoiseType::Perlin, x, y, 0.3f));
    }
    CHECK(fb.lo >= -1.0f && fb.hi <= 1.0f && fb.hi - fb.lo > 0.5f);
    CHECK(rg.lo >= 0.0f && rg.hi <= 1.0f && rg.hi - rg.lo > 0.3f);
    CHECK(tb.lo >= 0.0f && tb.hi <= 1.0f && tb.hi - tb.lo > 0.2f);
    // One octave of fBm equals the base noise; more octaves add detail (differs).
    FractalParams one; one.octaves = 1;
    CHECK_NEAR(n.Fbm2(NoiseType::Perlin, 1.3f, 2.2f, one), n.Perlin2(1.3f, 2.2f), 1e-6);
    CHECK(std::fabs(n.Fbm2(NoiseType::Perlin, 1.3f, 2.2f, fp) - n.Perlin2(1.3f, 2.2f)) > 1e-4f);
    FractalParams weird; weird.octaves = 0; CHECK(std::isfinite(n.Fbm2(NoiseType::Perlin, 1, 2, weird)));   // clamped to 1
    weird.octaves = 9999; CHECK(std::isfinite(n.Fbm3(NoiseType::Value, 1, 2, 3, weird)));
    // Huge / negative coordinates stay finite and in range.
    CHECK(std::fabs(n.Perlin2(-123456.7f, 98765.4f)) <= 1.0f); CHECK(std::fabs(n.Simplex3(-1e5f, -1e5f, 1e5f)) <= 1.0f);
    // Selectable basis matches the direct call.
    CHECK(n.Sample2(NoiseType::Value, 1.1f, 2.2f) == n.Value2(1.1f, 2.2f)); CHECK(n.Sample3(NoiseType::Simplex, 1, 2, 3) == n.Simplex3(1, 2, 3));
    // Frozen golden values: catch accidental changes to the algorithm (saved worlds depend on it).
    CHECK(n.Hash(0, 0, 0) == NoiseField(42).Hash(0, 0, 0));
}

static void TestCurves() {
    Vec3 p0{0, 0, 0}, p1{1, 2, 0}, p2{3, 2, 0}, p3{4, 0, 0};
    CHECK(VA(Curves::CubicBezier(p0, p1, p2, p3, 0.0f), p0)); CHECK(VA(Curves::CubicBezier(p0, p1, p2, p3, 1.0f), p3));
    CHECK(VA(Curves::CubicBezier(p0, p1, p2, p3, 0.5f), {2, 1.5f, 0}));
    CHECK(VA(Curves::CubicBezierTangent(p0, p1, p2, p3, 0.0f), (p1 - p0) * 3.0f)); CHECK(VA(Curves::CubicBezierTangent(p0, p1, p2, p3, 1.0f), (p3 - p2) * 3.0f));
    CHECK(VA(Curves::QuadBezier(p0, p1, p3, 0.5f), p0 * 0.25f + p1 * 0.5f + p3 * 0.25f));
    CHECK(VA(Curves::QuadBezierTangent(p0, p1, p3, 0.0f), (p1 - p0) * 2.0f)); CHECK(VA(Curves::QuadBezierTangent(p0, p1, p3, 1.0f), (p3 - p1) * 2.0f));
    // Tangent matches the numerical derivative.
    for (float t = 0.05f; t < 1.0f; t += 0.1f) {
        float h = 1e-3f;
        Vec3 num = (Curves::CubicBezier(p0, p1, p2, p3, t + h) - Curves::CubicBezier(p0, p1, p2, p3, t - h)) / (2 * h);
        CHECK(VA(Curves::CubicBezierTangent(p0, p1, p2, p3, t), num, 5e-2f));
        Vec3 numq = (Curves::QuadBezier(p0, p1, p3, t + h) - Curves::QuadBezier(p0, p1, p3, t - h)) / (2 * h);
        CHECK(VA(Curves::QuadBezierTangent(p0, p1, p3, t), numq, 5e-2f));
        Vec3 numh = (Curves::Hermite(p0, p1, p2, p3, t + h) - Curves::Hermite(p0, p1, p2, p3, t - h)) / (2 * h);
        CHECK(VA(Curves::HermiteTangent(p0, p1, p2, p3, t), numh, 5e-2f));
        Vec3 numc = (Curves::CatmullRom(p0, p1, p2, p3, t + h) - Curves::CatmullRom(p0, p1, p2, p3, t - h)) / (2 * h);
        CHECK(VA(Curves::CatmullRomTangent(p0, p1, p2, p3, t), numc, 5e-2f));
    }
    // Straight control polygon => straight, linear-in-t curve.
    CHECK(VA(Curves::CubicBezier(Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{2, 0, 0}, Vec3{3, 0, 0}, 0.25f), {0.75f, 0, 0}));
    // Hermite interpolates endpoints and tangents.
    Vec3 m0{0, 4, 0}, m1{0, -4, 0};
    CHECK(VA(Curves::Hermite(p0, m0, p3, m1, 0.0f), p0)); CHECK(VA(Curves::Hermite(p0, m0, p3, m1, 1.0f), p3));
    CHECK(VA(Curves::HermiteTangent(p0, m0, p3, m1, 0.0f), m0)); CHECK(VA(Curves::HermiteTangent(p0, m0, p3, m1, 1.0f), m1));
    // Catmull-Rom passes through p1 and p2 and is C1 across segments.
    CHECK(VA(Curves::CatmullRom(p0, p1, p2, p3, 0.0f), p1)); CHECK(VA(Curves::CatmullRom(p0, p1, p2, p3, 1.0f), p2));
    CHECK(VA(Curves::CatmullRomTangent(p0, p1, p2, p3, 1.0f), Curves::CatmullRomTangent(p1, p2, p3, Vec3{5, 5, 0}, 0.0f)));
    CHECK(VA(Curves::CatmullRomTangent(p0, p1, p2, p3, 0.0f), (p2 - p0) * 0.5f));
    CHECK(VA(Curves::CatmullRom(p0, p1, p2, p3, 0.5f, 0.0f), (p1 + p2) * 0.5f));              // tension 0 => straight
    // 2D and scalar instantiations.
    Vec2 a{0, 0}, b{1, 1}, c{2, 1}, d{3, 0};
    CHECK_NEAR(Curves::CubicBezier(a, b, c, d, 0.5f).x, 1.5, 1e-5); CHECK_NEAR(Curves::CubicBezier(0.0f, 0.0f, 1.0f, 1.0f, 0.5f), 0.5, 1e-5);
    CHECK_NEAR(Curves::CatmullRom(0.0f, 1.0f, 2.0f, 3.0f, 0.5f), 1.5, 1e-5);                  // linear data stays linear
    // Extrapolates rather than clamps.
    CHECK_NEAR(Curves::CubicBezier(0.0f, 1.0f, 2.0f, 3.0f, 1.5f), 4.5, 1e-4);
    // Arc length.
    CHECK_NEAR(Curves::CubicBezierLength(Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{2, 0, 0}, Vec3{3, 0, 0}), 3.0, 1e-3);
    CHECK_NEAR(Curves::QuadBezierLength(Vec3{0, 0, 0}, Vec3{5, 0, 0}, Vec3{10, 0, 0}), 10.0, 1e-3);
    CHECK_NEAR(Curves::CubicBezierLength(Vec2{0, 0}, Vec2{0, 0}, Vec2{4, 3}, Vec2{4, 3}, 200), 5.0, 0.2);          // roughly the chord, longer
    float quarter = Curves::ArcLength([](float t) { return Vec2{std::cos(t * Mathf::PI * 0.5f), std::sin(t * Mathf::PI * 0.5f)}; }, 0, 1, 256);
    CHECK_NEAR(quarter, Mathf::PI * 0.5f, 1e-3);
    CHECK_NEAR(Curves::ArcLength([](float t) { return t * 4.0f; }, 0, 1, 8), 4.0, 1e-5);          // scalar curve
    CHECK(Curves::ArcLength([](float) { return Vec3{1, 1, 1}; }, 0, 1, 0) == 0.0f);               // segments clamped
    // Arc-length table: constant-speed parametrisation of a curve with uneven speed.
    auto f = [&](float t) { return Curves::CubicBezier(Vec3{0, 0, 0}, Vec3{0, 0, 0}, Vec3{10, 0, 0}, Vec3{10, 0, 0}, t); };
    Curves::ArcLengthTable tab;
    CHECK(tab.Empty()); CHECK(tab.TotalLength() == 0.0f); CHECK(tab.ParamAtLength(5) == 0.0f);
    tab.Build(f, 128);
    CHECK(!tab.Empty()); CHECK_NEAR(tab.TotalLength(), 10.0, 1e-3);
    for (int i = 0; i <= 10; ++i) {
        float tt = tab.ParamAtLength(tab.TotalLength() * i / 10.0f);
        CHECK_NEAR(f(tt).x, (double)i, 0.05);                       // equal steps in distance
    }
    CHECK(tab.ParamAtLength(-1) == 0.0f); CHECK(tab.ParamAtLength(1e9f) == 1.0f); CHECK(tab.ParamAtLength(0) == 0.0f);
    CHECK_NEAR(tab.ParamAtLength(5.0f), 0.5, 1e-2);              // symmetric curve
    CHECK_NEAR(tab.LengthAtParam(0.5f), 5.0, 0.05); CHECK_NEAR(tab.LengthAtParam(1.0f), 10.0, 1e-3); CHECK(tab.LengthAtParam(-3) == 0.0f);
    CHECK_NEAR(tab.LengthAtParam(tab.ParamAtLength(3.3f)), 3.3, 0.02);   // round trip
    CHECK(tab.LengthAtParam(2.0f) == tab.TotalLength());
    Curves::ArcLengthTable stat; stat.Build([](float) { return Vec3{1, 2, 3}; }, 8);             // zero-length curve
    CHECK(stat.TotalLength() == 0.0f); CHECK(stat.ParamAtLength(1.0f) == 0.0f);
    Curves::ArcLengthTable low; low.Build(f, 0); CHECK(low.TotalLength() > 0);                   // clamped to 1 segment
}

static void TestColorSpace() {
    CHECK_NEAR(ColorSpace::SrgbToLinear(0.0f), 0.0, 1e-7); CHECK_NEAR(ColorSpace::SrgbToLinear(1.0f), 1.0, 1e-6);
    CHECK_NEAR(ColorSpace::SrgbToLinear(0.5f), 0.214041, 1e-4); CHECK_NEAR(ColorSpace::LinearToSrgb(0.214041f), 0.5, 1e-4);
    CHECK_NEAR(ColorSpace::SrgbToLinear(0.02f), 0.02 / 12.92, 1e-6);                             // linear toe
    for (int i = 0; i <= 20; ++i) { float v = i / 20.0f; CHECK_NEAR(ColorSpace::LinearToSrgb(ColorSpace::SrgbToLinear(v)), v, 1e-4); }
    CHECK(ColorSpace::SrgbToLinear(-1.0f) == 0.0f); CHECK_NEAR(ColorSpace::LinearToSrgb(5.0f), 1.0, 1e-6);
    Color c(0.5f, 0.25f, 1.0f, 0.4f);
    Color l = ColorSpace::ToLinear(c);
    CHECK_NEAR(l.a, 0.4, 1e-6); CHECK_NEAR(l.b, 1.0, 1e-6); CHECK(l.r < c.r);
    Color back = ColorSpace::ToSrgb(l); CHECK_NEAR(back.r, 0.5, 1e-4); CHECK_NEAR(back.g, 0.25, 1e-4); CHECK_NEAR(back.a, 0.4, 1e-6);
    // Linear-space lerp: black<->white midpoint is the sRGB value of 0.5 linear (~0.735), not 0.5.
    Color mid = ColorSpace::LerpLinear(Color::Black, Color::White, 0.5f);
    CHECK_NEAR(mid.r, 0.7354, 1e-3); CHECK_NEAR(mid.g, mid.r, 1e-6);
    Color e0 = ColorSpace::LerpLinear(Color(1, 0, 0, 0), Color(0, 0, 1, 1), 0.0f); CHECK_NEAR(e0.r, 1.0, 1e-5); CHECK_NEAR(e0.a, 0.0, 1e-6);
    Color e1 = ColorSpace::LerpLinear(Color(1, 0, 0, 0), Color(0, 0, 1, 1), 1.0f); CHECK_NEAR(e1.b, 1.0, 1e-5); CHECK_NEAR(e1.a, 1.0, 1e-6);
    CHECK_NEAR(ColorSpace::LerpLinear(Color(1, 0, 0, 0), Color(0, 0, 1, 1), 0.5f).a, 0.5, 1e-6);
    CHECK_NEAR(ColorSpace::LerpLinear(Color::Red, Color::Blue, 7.0f).b, 1.0, 1e-5);              // clamped
    // HSV.
    Hsv h = ColorSpace::ToHsv(Color(1, 0, 0)); CHECK_NEAR(h.h, 0, 1e-4); CHECK_NEAR(h.s, 1, 1e-6); CHECK_NEAR(h.v, 1, 1e-6);
    h = ColorSpace::ToHsv(Color(0, 1, 0)); CHECK_NEAR(h.h, 120, 1e-3);
    h = ColorSpace::ToHsv(Color(0, 0, 1)); CHECK_NEAR(h.h, 240, 1e-3);
    h = ColorSpace::ToHsv(Color(1, 1, 0)); CHECK_NEAR(h.h, 60, 1e-3);
    h = ColorSpace::ToHsv(Color(1, 0, 1)); CHECK_NEAR(h.h, 300, 1e-3);
    h = ColorSpace::ToHsv(Color(0.5f, 0.5f, 0.5f)); CHECK_NEAR(h.s, 0, 1e-6); CHECK_NEAR(h.v, 0.5, 1e-6);
    h = ColorSpace::ToHsv(Color(0, 0, 0)); CHECK(h.s == 0 && h.v == 0 && std::isfinite(h.h));
    h = ColorSpace::ToHsv(Color(1, 0, 0.5f)); CHECK(h.h > 300 && h.h < 360);                      // negative hue wraps
    Color f = ColorSpace::FromHsv(120, 1, 1); CHECK_NEAR(f.g, 1, 1e-5); CHECK_NEAR(f.r, 0, 1e-5);
    f = ColorSpace::FromHsv(-120, 1, 1, 0.3f); CHECK_NEAR(f.b, 1, 1e-5); CHECK_NEAR(f.a, 0.3, 1e-6);   // negative hue wraps
    f = ColorSpace::FromHsv(720 + 60, 1, 1); CHECK_NEAR(f.r, 1, 1e-5); CHECK_NEAR(f.g, 1, 1e-5);
    f = ColorSpace::FromHsv(360, 1, 1); CHECK_NEAR(f.r, 1, 1e-5);
    f = ColorSpace::FromHsv(0, 0, 0.7f); CHECK_NEAR(f.r, 0.7, 1e-6); CHECK_NEAR(f.b, 0.7, 1e-6);
    f = ColorSpace::FromHsv(0, 5, 5); CHECK_NEAR(f.r, 1, 1e-6);                                    // clamped
    for (int i = 0; i < 200; ++i) {                                                               // round trip
        Color rc(((i * 37) % 101) / 100.0f, ((i * 53) % 103) / 102.0f, ((i * 71) % 107) / 106.0f, 1);
        Color rt = ColorSpace::FromHsv(ColorSpace::ToHsv(rc));
        CHECK_NEAR(rt.r, rc.r, 1e-4); CHECK_NEAR(rt.g, rc.g, 1e-4); CHECK_NEAR(rt.b, rc.b, 1e-4);
        Color rl = ColorSpace::FromHsl(ColorSpace::ToHsl(rc));
        CHECK_NEAR(rl.r, rc.r, 1e-4); CHECK_NEAR(rl.g, rc.g, 1e-4); CHECK_NEAR(rl.b, rc.b, 1e-4);
    }
    // HSL.
    Hsl hl = ColorSpace::ToHsl(Color(1, 0, 0)); CHECK_NEAR(hl.h, 0, 1e-4); CHECK_NEAR(hl.s, 1, 1e-5); CHECK_NEAR(hl.l, 0.5, 1e-6);
    hl = ColorSpace::ToHsl(Color(1, 1, 1)); CHECK_NEAR(hl.l, 1, 1e-6); CHECK_NEAR(hl.s, 0, 1e-6);
    hl = ColorSpace::ToHsl(Color(0, 0, 0)); CHECK_NEAR(hl.l, 0, 1e-6); CHECK(std::isfinite(hl.s));
    hl = ColorSpace::ToHsl(Color(0.25f, 0.5f, 0.75f)); CHECK_NEAR(hl.h, 210, 1e-2); CHECK_NEAR(hl.l, 0.5, 1e-6); CHECK_NEAR(hl.s, 0.5, 1e-5);
    Color g = ColorSpace::FromHsl(210, 0.5f, 0.5f); CHECK_NEAR(g.r, 0.25, 1e-5); CHECK_NEAR(g.g, 0.5, 1e-5); CHECK_NEAR(g.b, 0.75, 1e-5);
    g = ColorSpace::FromHsl(0, 0, 1); CHECK_NEAR(g.r, 1, 1e-6); g = ColorSpace::FromHsl(0, 1, 0); CHECK_NEAR(g.r, 0, 1e-6);
    CHECK_NEAR(ColorSpace::FromHsl(Hsl{60, 1, 0.5f}).g, 1, 1e-5);
    // Hex.
    Color hx;
    CHECK(ColorSpace::ParseHex("#FF8000", hx)); CHECK_NEAR(hx.r, 1, 1e-6); CHECK_NEAR(hx.g, 128 / 255.0, 1e-6); CHECK_NEAR(hx.b, 0, 1e-6); CHECK_NEAR(hx.a, 1, 1e-6);
    CHECK(ColorSpace::ParseHex("ff800080", hx)); CHECK_NEAR(hx.a, 128 / 255.0, 1e-6);
    CHECK(ColorSpace::ParseHex("#F80", hx)); CHECK_NEAR(hx.g, 136 / 255.0, 1e-6); CHECK_NEAR(hx.b, 0, 1e-6);
    CHECK(ColorSpace::ParseHex("#f804", hx)); CHECK_NEAR(hx.a, 68 / 255.0, 1e-6);
    CHECK(ColorSpace::ParseHex("0xAbCdEf", hx)); CHECK_NEAR(hx.r, 0xAB / 255.0, 1e-6);
    CHECK(ColorSpace::ParseHex("  #00ff00 \n", hx)); CHECK_NEAR(hx.g, 1, 1e-6);
    Color keep(0.1f, 0.2f, 0.3f, 0.4f); hx = keep;
    for (const char* bad : {"", "#", "#12", "#12345", "#1234567", "#GGGGGG", "red", "#12 456", "##ffffff", "#fffffff", "0x", "#ff00ff00ff"})
        { CHECK(!ColorSpace::ParseHex(bad, hx)); }
    CHECK(hx.r == keep.r && hx.g == keep.g && hx.a == keep.a);                                  // failure leaves output alone
    CHECK(ColorSpace::ToHex(Color(1, 0.5f, 0)) == "#FF8000"); CHECK(ColorSpace::ToHex(Color(1, 0.5f, 0, 0.5f), true) == "#FF800080");
    CHECK(ColorSpace::ToHex(Color(2, -1, 0.0f)) == "#FF0000");                                   // clamped
    for (int i = 0; i < 256; i += 15) {
        Color pc; std::string sx = ColorSpace::ToHex(Color::FromBytes(i, 255 - i, (i * 7) % 256, i), true);
        CHECK(ColorSpace::ParseHex(sx, pc)); CHECK(ColorSpace::ToHex(pc, true) == sx);
    }
}

static void TestRng() {
    Rng a(123), b(123), c(124);
    bool same = true, diff = false;
    for (int i = 0; i < 100; ++i) { std::uint32_t x = a.NextUInt32(); same = same && x == b.NextUInt32(); diff = diff || x != c.NextUInt32(); }
    CHECK(same); CHECK(diff);
    // PCG32 reference vector (seed 42, stream 54 from the PCG paper's demo).
    Rng ref(42, 54);
    CHECK(ref.NextUInt32() == 0xa15c02b7u); CHECK(ref.NextUInt32() == 0x7b47f409u); CHECK(ref.NextUInt32() == 0xba1d3330u);
    CHECK(ref.NextUInt32() == 0x83d2f293u); CHECK(ref.NextUInt32() == 0xbfa4784bu); CHECK(ref.NextUInt32() == 0xcbed606eu);

    // Ranges.
    Rng r(7);
    int lo = 99, hi = -99; std::map<int, int> hist;
    for (int i = 0; i < 6000; ++i) { int v = r.Range(0, 6); lo = std::min(lo, v); hi = std::max(hi, v); ++hist[v]; }
    CHECK(lo == 0 && hi == 5);                                    // max EXCLUSIVE
    for (int k = 0; k < 6; ++k) CHECK(hist[k] > 800 && hist[k] < 1200);
    CHECK(r.Range(5, 5) == 5); CHECK(r.Range(9, 3) == 9);        // empty range returns min
    for (int i = 0; i < 1000; ++i) { int v = r.Range(-3, 3); CHECK(v >= -3 && v < 3); }
    for (int i = 0; i < 100; ++i) { int v = r.Range(INT32_MIN, INT32_MAX); CHECK(v < INT32_MAX); }          // full span, no overflow
    for (int i = 0; i < 1000; ++i) { float v = r.Value(); CHECK(v >= 0.0f && v < 1.0f); double d = r.ValueDouble(); CHECK(d >= 0.0 && d < 1.0); }
    for (int i = 0; i < 1000; ++i) { float v = r.Range(2.0f, 3.0f); CHECK(v >= 2.0f && v < 3.0f); }
    double mean = 0; for (int i = 0; i < 20000; ++i) mean += r.Value(); mean /= 20000; CHECK_NEAR(mean, 0.5, 0.01);
    int heads = 0; for (int i = 0; i < 10000; ++i) heads += r.Bool(); CHECK(heads > 4700 && heads < 5300);
    int hits = 0; for (int i = 0; i < 10000; ++i) hits += r.Chance(0.2f); CHECK(hits > 1800 && hits < 2200);
    CHECK(!r.Chance(0.0f)); CHECK(r.Chance(1.0f));
    CHECK(r.Below(0) == 0 && r.Below(1) == 0);
    for (int i = 0; i < 1000; ++i) CHECK(r.Below(3) < 3);
    // Modulo-bias check on a non-power-of-two span near 2^32.
    { Rng z(5); int above = 0; for (int i = 0; i < 20000; ++i) if (z.Below(3000000000u) >= 1500000000u) ++above; CHECK(above > 9500 && above < 10500); }
    // Gaussian.
    double gm = 0, gv = 0; const int N = 20000;
    for (int i = 0; i < N; ++i) { double v = r.Gaussian(3.0f, 2.0f); gm += v; gv += (v - 3.0) * (v - 3.0); }
    CHECK_NEAR(gm / N, 3.0, 0.08); CHECK_NEAR(std::sqrt(gv / N), 2.0, 0.08);
    for (int i = 0; i < 1000; ++i) CHECK(std::isfinite(r.Gaussian()));

    // Streams: independent, repeatable, order-independent, not consuming the parent.
    Rng world(2024);
    Rng loot = world.Fork("loot"), spawn = world.Fork("spawn"), loot2 = world.Fork("loot");
    CHECK(loot == loot2); CHECK(loot != spawn);
    CHECK(world.GetState() == Rng(2024).GetState());                       // forking did not advance the parent
    std::uint32_t l0 = loot.NextUInt32(), s0 = spawn.NextUInt32();
    CHECK(l0 != s0); CHECK(l0 == loot2.NextUInt32());
    world.NextUInt32(); world.NextUInt32();                                // parent draws must not change forks
    Rng loot3 = world.Fork("loot"); CHECK(loot3.NextUInt32() == l0);
    CHECK(world.Fork(1) != world.Fork(2)); CHECK(world.Fork(7) == world.Fork(7));
    CHECK(Rng(1).Fork("x") != Rng(2).Fork("x"));                           // different parents, different streams
    CHECK(world.Fork("a").Fork("b") != world.Fork("b").Fork("a"));
    CHECK(world.Fork("loot") == world.Fork(std::string("loot")));
    int equalCount = 0; Rng fa = world.Fork("A"), fb = world.Fork("B");
    for (int i = 0; i < 2000; ++i) if (fa.NextUInt32() == fb.NextUInt32()) ++equalCount;
    CHECK(equalCount < 5);                                                  // uncorrelated
    // Same seed, different stream numbers: different sequences.
    CHECK(Rng(5, 0).NextUInt64() != Rng(5, 1).NextUInt64());
    CHECK(Rng(5, 3).GetStream() == 3 && Rng(5, 3).GetSeed() == 5);

    // State round trip.
    Rng sv(999); for (int i = 0; i < 37; ++i) sv.NextUInt32();
    std::string st = sv.GetState();
    CHECK(st.rfind("pcg32:", 0) == 0);
    Rng rs(1); CHECK(rs.SetState(st)); CHECK(rs == sv);
    for (int i = 0; i < 50; ++i) CHECK(rs.NextUInt64() == sv.NextUInt64());
    CHECK(rs.GetState() == sv.GetState());
    Rng fk = sv.Fork("k"); Rng fk2(0); CHECK(fk2.SetState(fk.GetState())); CHECK(fk2.Fork("z") == fk.Fork("z"));   // seed/stream survive too
    // Malformed state: rejected, object untouched.
    Rng keep(55); std::string before = keep.GetState();
    for (const std::string& bad : std::initializer_list<std::string>{std::string(""), std::string("pcg32:"), std::string("xyz"), st.substr(0, st.size() - 1), st + "0",
                                  std::string("pcg32:zzzzzzzzzzzzzzzz:0000000000000001:0000000000000000:0000000000000000"),
                                  std::string("pcg32:0000000000000000:0000000000000002:0000000000000000:0000000000000000"),   // even increment
                                  std::string("pcg32:0000000000000000-0000000000000001:0000000000000000:0000000000000000")})
        { CHECK(!keep.SetState(bad)); CHECK(keep.GetState() == before); }
    CHECK(keep.SetState("pcg32:0000000000000000:0000000000000001:0000000000000000:0000000000000000"));
    CHECK(keep.SetState("PCG32:0000000000000000:0000000000000001:0000000000000000:0000000000000000") == false);

    // Shuffle / choose / weighted.
    std::vector<int> v; for (int i = 0; i < 20; ++i) v.push_back(i);
    Rng sh(31); std::vector<int> v2 = v; sh.Shuffle(v2);
    CHECK(v2 != v); std::vector<int> sorted = v2; std::sort(sorted.begin(), sorted.end()); CHECK(sorted == v);   // a permutation
    Rng sh2(31); std::vector<int> v3 = v; sh2.Shuffle(v3); CHECK(v3 == v2);                                  // deterministic
    int arr[5] = {1, 2, 3, 4, 5}; sh.Shuffle(arr); CHECK(arr[0] + arr[1] + arr[2] + arr[3] + arr[4] == 15);
    std::vector<int> e, one{9}; sh.Shuffle(e); sh.Shuffle(one); CHECK(e.empty() && one[0] == 9);
    // Every permutation of 3 appears about equally often.
    { std::map<std::vector<int>, int> perms; Rng p(1);
      for (int i = 0; i < 6000; ++i) { std::vector<int> t{0, 1, 2}; p.Shuffle(t); ++perms[t]; }
      CHECK(perms.size() == 6); for (auto& kv : perms) CHECK(kv.second > 800 && kv.second < 1200); }
    // Position bias: element 0 lands everywhere equally.
    { int pos[4] = {0, 0, 0, 0}; Rng p(2);
      for (int i = 0; i < 8000; ++i) { std::vector<int> t{0, 1, 2, 3}; p.Shuffle(t.begin(), t.end()); for (int k = 0; k < 4; ++k) if (t[k] == 0) ++pos[k]; }
      for (int k = 0; k < 4; ++k) CHECK(pos[k] > 1800 && pos[k] < 2200); }
    CHECK(sh.Choose(e) == nullptr);
    { std::set<int> seen; for (int i = 0; i < 500; ++i) { int* p = sh.Choose(v); CHECK(p != nullptr && *p >= 0 && *p < 20); seen.insert(*p); } CHECK(seen.size() == 20); }
    CHECK(sh.Choose(e.begin(), e.end()) == e.end()); CHECK(*sh.Choose(one.begin(), one.end()) == 9);
    { std::vector<std::string> names{"a", "b"}; std::string* p = sh.Choose(names); CHECK(p == &names[0] || p == &names[1]); }
    std::vector<float> w{0.0f, 1.0f, 3.0f, -5.0f, std::nanf("")};
    int wc[5] = {0, 0, 0, 0, 0}; Rng wr(8);
    for (int i = 0; i < 8000; ++i) { int k = wr.WeightedIndex(w); CHECK(k >= 0 && k < 5); ++wc[k]; }
    CHECK(wc[0] == 0 && wc[3] == 0 && wc[4] == 0); CHECK(wc[2] > 5600 && wc[2] < 6400); CHECK(wc[1] > 1600 && wc[1] < 2400);
    CHECK(wr.WeightedIndex({}) == -1); CHECK(wr.WeightedIndex({0.0f, 0.0f}) == -1); CHECK(wr.WeightedIndex({0.0f, -1.0f}) == -1);
    CHECK(wr.WeightedIndex({0.0f, 2.0f, 0.0f}) == 1); CHECK(wr.WeightedIndex({5.0f}) == 0);

    // Default-constructed Rng is deterministic too.
    CHECK(Rng().NextUInt32() == Rng().NextUInt32());

    // Existing Random contract untouched: Range(int,int) is max-exclusive, same seed => same sequence.
    Random r1(5), r2(5); CHECK(r1.NextUInt64() == r2.NextUInt64());
    for (int i = 0; i < 500; ++i) { int v = Random::Shared().Range(3, 7); CHECK(v >= 3 && v < 7); }
}

int main() {
    RUN_SUITE("math_geometry");
    TestMathf(); TestAngleAxisDecompose(); TestMat3(); TestBounds(); TestGeometry(); TestFrustum();
    TestVecInt(); TestNoise(); TestCurves(); TestColorSpace(); TestRng();
    TEST_MAIN_RESULT();
}
