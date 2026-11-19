#include "test_framework.hpp"
#include "okay/Environment/WindField.hpp"
#include <cmath>
#include <limits>

using namespace okay;

// Wind field maths (docs/wind.md): deterministic noise, gust period, zone blending, curve, no NaN at the edges.

namespace {
WindSource Global(Vec3 dir, float strength) {
    WindSource s; s.global = true; s.direction = dir; s.strength = strength; return s;
}
WindSource Box(Vec3 centre, float half, Vec3 dir, float strength) {
    WindSource s; s.global = false; s.center = centre; s.half = {half, half, half};
    s.direction = dir; s.strength = strength; s.edge = 0.0f; return s;
}
bool Finite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
}

static void Wind_noise_is_deterministic_and_bounded() {
    for (int i = 0; i < 200; ++i) {
        const float x = i * 0.37f - 20.0f, y = i * 0.11f, z = -i * 0.29f;
        const float a = windmath::Noise3(x, y, z, 7), b = windmath::Noise3(x, y, z, 7);
        CHECK(a == b);
        CHECK(a >= -1.0f && a <= 1.0f);
    }
    // A different seed gives a different field, and it is not flat.
    float diff = 0, mn = 1, mx = -1;
    for (int i = 0; i < 200; ++i) {
        const float a = windmath::Noise3(i * 0.31f, 0.5f, 0.2f, 1), b = windmath::Noise3(i * 0.31f, 0.5f, 0.2f, 2);
        diff += std::fabs(a - b); mn = std::min(mn, a); mx = std::max(mx, a);
    }
    CHECK(diff > 10.0f);
    CHECK(mx - mn > 0.8f);
    // Smooth: a tiny step changes it a tiny amount.
    CHECK(std::fabs(windmath::Noise3(3.3f, 1.1f, 0.7f, 5) - windmath::Noise3(3.3001f, 1.1f, 0.7f, 5)) < 0.01f);
    // The lattice corners are the noise values (whole numbers hit the hash).
    CHECK(windmath::Noise3(4.0f, 5.0f, 6.0f, 3) == windmath::Corner(4, 5, 6, 3));
}

static void Wind_noise_survives_bad_numbers() {
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    CHECK(windmath::Noise3(nan, 0, 0, 1) == 0.0f);
    CHECK(windmath::Noise3(inf, 0, 0, 1) == 0.0f);
    CHECK(std::isfinite(windmath::Noise3(1e30f, -1e30f, 5, 1)));
}

static void Wind_gust_repeats_every_period() {
    WindSource s = Global({1, 0, 0}, 10.0f);
    s.gustAmount = 0.8f; s.gustFrequency = 0.25f; s.gustSpeed = 0.0f;   // every 4 s, everywhere at once
    const Vec3 p{3, 1, -2};
    for (float t = 0; t < 4.0f; t += 0.37f)
        CHECK(std::fabs(s.Raw(p, t).x - s.Raw(p, t + 4.0f).x) < 1e-3f);
    // It really swings between (1 - amount) and (1 + amount) of the base.
    float lo = 1e9f, hi = -1e9f;
    for (float t = 0; t < 4.0f; t += 0.01f) { const float v = s.Raw(p, t).x; lo = std::min(lo, v); hi = std::max(hi, v); }
    CHECK(lo > 10.0f * 0.2f - 0.1f && lo < 10.0f * 0.2f + 0.2f);
    CHECK(hi > 10.0f * 1.8f - 0.2f && hi <= 10.0f * 1.8f + 0.01f);
}

static void Wind_gust_travels_downwind() {
    WindSource s = Global({1, 0, 0}, 10.0f);
    s.gustAmount = 1.0f; s.gustFrequency = 0.1f; s.gustSpeed = 10.0f;
    // The same gust reaches a point 20 m downwind 2 s later.
    for (float t = 0; t < 8.0f; t += 0.5f)
        CHECK(std::fabs(s.Raw({0, 0, 0}, t).x - s.Raw({20, 0, 0}, t + 2.0f).x) < 1e-3f);
}

static void Wind_steady_without_gusts_or_turbulence() {
    WindSource s = Global({0, 0, 2}, 6.0f);   // direction need not be unit length
    const Vec3 a = s.Raw({0, 0, 0}, 0.0f), b = s.Raw({50, 9, -3}, 33.3f);
    CHECK(std::fabs(a.z - 6.0f) < 1e-5f);
    CHECK(a.x == 0.0f && a.y == 0.0f);
    CHECK(a == b);
}

static void Wind_same_seed_same_result_different_seed_differs() {
    WindSource a = Global({1, 0, 0}, 8.0f); a.turbulence = 0.6f; a.seed = 11;
    WindSource b = a;
    WindSource c = a; c.seed = 12;
    int differ = 0;
    for (int i = 0; i < 50; ++i) {
        const Vec3 p{i * 1.7f, i * 0.3f, -i * 2.1f};
        CHECK(a.Raw(p, i * 0.2f) == b.Raw(p, i * 0.2f));
        if (!(a.Raw(p, i * 0.2f) == c.Raw(p, i * 0.2f))) ++differ;
    }
    CHECK(differ > 40);
    // Turbulence is a fraction of the strength: it never blows the wind far from its base.
    for (int i = 0; i < 200; ++i) CHECK((a.Raw({i * 0.9f, 0.f, i * 0.4f}, i * 0.1f) - Vec3{8, 0, 0}).Magnitude() < 8.0f * 0.6f * 1.8f);
}

static void Wind_zone_coverage_and_edges() {
    WindSource z = Box({0, 0, 0}, 2.0f, {1, 0, 0}, 10.0f);   // hard edge
    CHECK(z.Coverage({0, 0, 0}) == 1.0f);
    CHECK(z.Coverage({1.9f, 0, 0}) == 1.0f);
    CHECK(z.Coverage({2.1f, 0, 0}) == 0.0f);
    CHECK(z.Coverage({0, 0, 3.0f}) == 0.0f);
    CHECK(z.Coverage({2.0f, 0, 0}) == 0.0f);   // exactly on the surface
    z.edge = 0.5f;                              // soft edge: half way in is full
    CHECK(z.Coverage({1.0f, 0, 0}) > 0.99f);
    const float mid = z.Coverage({1.5f, 0, 0});
    CHECK(mid > 0.1f && mid < 0.9f);
    CHECK(z.Coverage({1.99f, 0, 0}) < mid);
    // A sphere is the shape inscribed in the box: the corner of the box is outside it.
    z.shape = WindSource::Shape::Sphere; z.edge = 0.0f;
    CHECK(z.Coverage({1.9f, 0, 0}) == 1.0f);
    CHECK(z.Coverage({1.5f, 1.5f, 1.5f}) == 0.0f);
    // A box with no size covers nothing and gives no NaN.
    z.shape = WindSource::Shape::Box; z.half = {0, 1, 1};
    CHECK(z.Coverage({0, 0, 0}) == 0.0f);
    CHECK(Finite(z.Raw({0, 0, 0}, 1.0f)));
}

static void Wind_edges_and_zero_distance_are_finite() {
    WindSource r = Box({0, 0, 0}, 3.0f, {1, 0, 0}, 9.0f);
    r.kind = WindSource::Kind::Radial; r.turbulence = 1.0f; r.gustAmount = 1.0f; r.edge = 0.4f; r.falloff = 1.0f;
    const Vec3 spots[] = {{0, 0, 0}, {3, 0, 0}, {-3, 3, 3}, {2.999f, 0, 0}, {1e5f, 1e5f, 1e5f}, {0, 0.00001f, 0}};
    for (const Vec3& p : spots)
        for (float t : {0.0f, 1.0f, 1e6f, -5.0f}) {
            CHECK(Finite(r.Raw(p, t)));
            CHECK(Finite(WindField::Evaluate({r}, {1, 2, 3}, p, t)));
        }
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(Finite(WindField::Evaluate({r}, {nan, 0, 0}, {0, 0, 0}, nan)));
    CHECK(Finite(WindField::Evaluate({r}, {0, 0, 0}, {nan, nan, nan}, 1.0f)));
    // Zero-length direction: no wind, not NaN.
    WindSource zero = Global({0, 0, 0}, 5.0f);
    CHECK(Finite(zero.Raw({1, 1, 1}, 1.0f)) && zero.Raw({1, 1, 1}, 1.0f).Magnitude() == 0.0f);
}

static void Wind_radial_blows_away_from_centre() {
    WindSource r = Box({10, 0, 0}, 5.0f, {1, 0, 0}, 4.0f);
    r.kind = WindSource::Kind::Radial;
    const Vec3 right = r.Raw({13, 0, 0}, 0), left = r.Raw({7, 0, 0}, 0), up = r.Raw({10, 2, 0}, 0);
    CHECK(right.x > 3.9f && std::fabs(right.y) < 1e-4f);
    CHECK(left.x < -3.9f);
    CHECK(up.y > 3.9f);
    r.strength = -4.0f;   // a negative strength sucks in
    CHECK(r.Raw({13, 0, 0}, 0).x < -3.9f);
    // falloff 1: nothing at the surface, full in the middle
    r.strength = 4.0f; r.falloff = 1.0f;
    CHECK(r.Raw({10.0f, 0.5f, 0}, 0).Magnitude() > r.Raw({14.5f, 0, 0}, 0).Magnitude());
    CHECK(r.Raw({14.99f, 0, 0}, 0).Magnitude() < 0.1f);
}

static void Wind_blending_priority_replace_and_add() {
    const Vec3 base{1, 0, 0};
    // Global adds to the base wind.
    WindSource g = Global({0, 0, 1}, 3.0f); g.additive = true;
    Vec3 w = WindField::Evaluate({g}, base, {0, 0, 0}, 0);
    CHECK(std::fabs(w.x - 1.0f) < 1e-5f && std::fabs(w.z - 3.0f) < 1e-5f);
    // A calm zone (strength 0, replace) shelters its inside and leaves the outside alone.
    WindSource calm = Box({0, 0, 0}, 2.0f, {1, 0, 0}, 0.0f);
    w = WindField::Evaluate({g, calm}, base, {0, 0, 0}, 0);
    CHECK(w.Magnitude() < 1e-5f);
    w = WindField::Evaluate({g, calm}, base, {5, 0, 0}, 0);
    CHECK(std::fabs(w.x - 1.0f) < 1e-5f && std::fabs(w.z - 3.0f) < 1e-5f);
    // Priority, not list order, decides who wins where two replace.
    WindSource lo = Box({0, 0, 0}, 2.0f, {1, 0, 0}, 2.0f); lo.priority = 0;
    WindSource hi = Box({0, 0, 0}, 2.0f, {0, 0, 1}, 7.0f); hi.priority = 5;
    const Vec3 a = WindField::Evaluate({lo, hi}, base, {0, 0, 0}, 0), b = WindField::Evaluate({hi, lo}, base, {0, 0, 0}, 0);
    CHECK(a == b);
    CHECK(std::fabs(a.z - 7.0f) < 1e-5f && std::fabs(a.x) < 1e-5f);
    // Half coverage blends half way between what is below and the zone.
    WindSource soft = Box({0, 0, 0}, 2.0f, {0, 0, 1}, 8.0f); soft.edge = 1.0f;
    const float c = soft.Coverage({1.0f, 0, 0});   // depth 0.5 with edge 1
    CHECK(c > 0.4f && c < 0.6f);
    w = WindField::Evaluate({soft}, {4, 0, 0}, {1.0f, 0, 0}, 0);
    CHECK(std::fabs(w.x - 4.0f * (1.0f - c)) < 1e-4f);
    CHECK(std::fabs(w.z - 8.0f * c) < 1e-4f);
    // Two overlapping additive zones sum.
    WindSource p1 = Box({0, 0, 0}, 2.0f, {1, 0, 0}, 2.0f); p1.additive = true;
    WindSource p2 = Box({0, 0, 0}, 2.0f, {1, 0, 0}, 3.0f); p2.additive = true;
    CHECK(std::fabs(WindField::Evaluate({p1, p2}, {0, 0, 0}, {0, 0, 0}, 0).x - 5.0f) < 1e-5f);
}

static void Wind_curve_loops_and_interpolates() {
    windmath::Curve c; c.length = 10.0f;
    CHECK(c.Eval(3.0f) == 1.0f);   // empty = steady
    c.keys = {{0.0f, 0.0f}, {5.0f, 2.0f}};
    CHECK(std::fabs(c.Eval(0.0f) - 0.0f) < 1e-5f);
    CHECK(std::fabs(c.Eval(2.5f) - 1.0f) < 1e-5f);
    CHECK(std::fabs(c.Eval(5.0f) - 2.0f) < 1e-5f);
    CHECK(std::fabs(c.Eval(7.5f) - 1.0f) < 1e-5f);                       // falls back toward the first key
    CHECK(std::fabs(c.Eval(12.5f) - c.Eval(2.5f)) < 1e-5f);               // loops
    CHECK(std::fabs(c.Eval(-7.5f) - c.Eval(2.5f)) < 1e-5f);               // negative time loops too
    c.keys = {{5.0f, 2.0f}, {0.0f, 0.0f}};                                // unsorted keys are fine
    CHECK(std::fabs(c.Eval(2.5f) - 1.0f) < 1e-5f);
    c.keys = {{3.0f, 0.7f}};
    CHECK(c.Eval(9.0f) == 0.7f);
    WindSource s = Global({1, 0, 0}, 10.0f);
    s.curve.length = 10.0f; s.curve.keys = {{0.0f, 0.0f}, {5.0f, 1.0f}};
    CHECK(std::fabs(s.Raw({0, 0, 0}, 0.0f).x) < 1e-4f);
    CHECK(std::fabs(s.Raw({0, 0, 0}, 5.0f).x - 10.0f) < 1e-4f);
}

static void Wind_published_field_and_lean() {
    WindField::Publish({});
    WindVector() = {2, 0, 0};
    CHECK(!WindField::Active());
    CHECK(WindField::At({5, 5, 5}) == Vec3(2, 0, 0));   // no components: the weather's wind exactly as before
    WindSource g = Global({0, 0, 1}, 3.0f); g.additive = true; g.priority = 3;
    WindSource z = Box({0, 0, 0}, 1.0f, {0, 1, 0}, 1.0f); z.priority = -1; z.additive = true;
    WindField::Publish({g, z});
    CHECK(WindField::Active());
    CHECK(WindField::Sources().front().priority == -1);   // sorted
    const Vec3 w = WindField::At({50, 0, 0}, 0.0f);
    CHECK(std::fabs(w.x - 2.0f) < 1e-5f && std::fabs(w.z - 3.0f) < 1e-5f);
    WindField::Publish({});
    WindVector() = {0, 0, 0};
    CHECK(WindField::Lean(0.0f) == 0.0f);
    CHECK(WindField::Lean(6.0f) > 0.49f && WindField::Lean(6.0f) < 0.51f);
    CHECK(WindField::Lean(1000.0f) <= 1.5f);
    CHECK(WindField::Lean(std::numeric_limits<float>::quiet_NaN()) == 0.0f);
}

int main() {
    RUN_SUITE("Wind_noise_is_deterministic_and_bounded"); Wind_noise_is_deterministic_and_bounded();
    RUN_SUITE("Wind_noise_survives_bad_numbers"); Wind_noise_survives_bad_numbers();
    RUN_SUITE("Wind_gust_repeats_every_period"); Wind_gust_repeats_every_period();
    RUN_SUITE("Wind_gust_travels_downwind"); Wind_gust_travels_downwind();
    RUN_SUITE("Wind_steady_without_gusts_or_turbulence"); Wind_steady_without_gusts_or_turbulence();
    RUN_SUITE("Wind_same_seed_same_result_different_seed_differs"); Wind_same_seed_same_result_different_seed_differs();
    RUN_SUITE("Wind_zone_coverage_and_edges"); Wind_zone_coverage_and_edges();
    RUN_SUITE("Wind_edges_and_zero_distance_are_finite"); Wind_edges_and_zero_distance_are_finite();
    RUN_SUITE("Wind_radial_blows_away_from_centre"); Wind_radial_blows_away_from_centre();
    RUN_SUITE("Wind_blending_priority_replace_and_add"); Wind_blending_priority_replace_and_add();
    RUN_SUITE("Wind_curve_loops_and_interpolates"); Wind_curve_loops_and_interpolates();
    RUN_SUITE("Wind_published_field_and_lean"); Wind_published_field_and_lean();
    TEST_MAIN_RESULT();
}

