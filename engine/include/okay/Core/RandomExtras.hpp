#pragma once
// Random helpers on top of okay::Random (and the PCG okay::Rng: anything with Value()
// in [0, 1) and Below(n) works). Nothing here keeps global state; you pass the generator.
//
//   Random rng(1234);
//   Vec2 spawn = rnd::PointInCircle(rng, 5.0f, center);        // uniform over the disc
//   Vec3 dir   = rnd::Direction3D(rng);                        // uniform on the sphere
//   bool crit  = rng.Chance(12.5f);                            // 12.5 percent (Random::Chance)
//
//   ShuffleBag<int> bag;  bag.Add(1); bag.Add(2); bag.Add(3);  // every item once, then again
//   int next = bag.Draw(rng);                                  // never the same twice across a refill
//
//   WeightedTable<std::string> loot;  loot.Add("sword", 1); loot.Add("potion", 8);
//   const std::string* drop = loot.Draw(rng);                  // weighted
//
//   DiceParse d = ParseDice("2d6+3");                          // never throws; d.ok / d.message
//   int damage = d.expr.Roll(rng);
//   int hit    = RollDice(rng, "1d20+5", /*fallback*/ 0);      // parse + roll, fallback on a typo
//
//   std::vector<Vec2> trees = rnd::PoissonDisk2D(rng, 100, 100, 4.0f);   // blue noise: no two closer than 4
//   std::vector<std::size_t> picks = rnd::SampleIndices(rng, 40, 3);    // three DIFFERENT spawn points of 40
//
// Seeding from text and independent child streams are on Random itself:
// Random::HashSeed / SeedFromString / FromString, and Random::Fork().
//
// Bad numbers: a position helper never returns NaN or infinity. A NaN or infinite radius, corner or centre
// counts as 0, and a span too wide for a float is worked out in double. Every helper consumes the same
// number of draws whatever the arguments, so a stream stays in step when a size comes out as 0.
#include "okay/Core/Random.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Math/Vec3.hpp"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <numeric>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace okay {

namespace rnd {

constexpr float kTwoPi = 6.28318530717958647692f;

namespace detail {
/// A finite number, or 0 for NaN and infinity.
inline float Fin(float v) { return std::isfinite(v) ? v : 0.0f; }
inline Vec2 Fin(Vec2 v) { return {Fin(v.x), Fin(v.y)}; }
inline Vec3 Fin(Vec3 v) { return {Fin(v.x), Fin(v.y), Fin(v.z)}; }
/// The point `t` (0..1) of the way from the lower to the higher of a and b. A span beyond the float range
/// (-FLT_MAX..FLT_MAX) is worked out in double instead of overflowing to infinity.
inline float Between(float a, float b, float t) {
    a = Fin(a); b = Fin(b);
    const float lo = std::fmin(a, b), span = std::fabs(b - a);
    if (std::isfinite(span)) return lo + t * span;
    return (float)((double)lo + (double)t * ((double)std::fmax(a, b) - (double)lo));
}
}  // namespace detail

/// True `percent` percent of the time (0 never, 100 or more always, NaN never).
template <class G> bool Chance(G& g, float percent) { return g.Value() * 100.0f < percent; }

/// A unit vector, every direction equally likely.
template <class G> Vec2 Direction2D(G& g) {
    const float a = g.Value() * kTwoPi;
    return {std::cos(a), std::sin(a)};
}
template <class G> Vec3 Direction3D(G& g) {
    const float z = 1.0f - 2.0f * g.Value();                  // (-1, 1]: uniform in z is uniform on the sphere
    const float a = g.Value() * kTwoPi;
    const float r = std::sqrt(std::fmax(0.0f, 1.0f - z * z));
    return {r * std::cos(a), r * std::sin(a), z};
}

/// On the circle's rim.
template <class G> Vec2 PointOnCircle(G& g, float radius = 1.0f, Vec2 center = {}) {
    return detail::Fin(center) + Direction2D(g) * std::fabs(detail::Fin(radius));
}
/// Anywhere in the disc, evenly by area (not bunched at the centre).
template <class G> Vec2 PointInCircle(G& g, float radius = 1.0f, Vec2 center = {}) {
    const float r = std::fabs(detail::Fin(radius)) * std::sqrt(g.Value());
    return detail::Fin(center) + Direction2D(g) * r;
}
/// Evenly by area between two radii (a spawn ring around the player). The radii may come in either order.
template <class G> Vec2 PointInRing(G& g, float innerRadius, float outerRadius, Vec2 center = {}) {
    float a = std::fabs(detail::Fin(innerRadius)), b = std::fabs(detail::Fin(outerRadius));
    if (a > b) std::swap(a, b);
    const float v = g.Value();
    const float r = std::isfinite(b * b) ? std::sqrt(a * a + v * (b * b - a * a))
                                          : (float)std::sqrt((double)a * a + (double)v * ((double)b * b - (double)a * a));
    return detail::Fin(center) + Direction2D(g) * r;
}
template <class G> Vec3 PointOnSphere(G& g, float radius = 1.0f, Vec3 center = {}) {
    return detail::Fin(center) + Direction3D(g) * std::fabs(detail::Fin(radius));
}
/// Anywhere in the ball, evenly by volume.
template <class G> Vec3 PointInSphere(G& g, float radius = 1.0f, Vec3 center = {}) {
    const float r = std::fabs(detail::Fin(radius)) * std::cbrt(g.Value());
    return detail::Fin(center) + Direction3D(g) * r;
}
/// In the rectangle / box between two corners (given in either order).
template <class G> Vec2 PointInRect(G& g, Vec2 a, Vec2 b) {
    const float x = g.Value(), y = g.Value();
    return {detail::Between(a.x, b.x, x), detail::Between(a.y, b.y, y)};
}
template <class G> Vec3 PointInBox(G& g, Vec3 a, Vec3 b) {
    const float x = g.Value(), y = g.Value(), z = g.Value();
    return {detail::Between(a.x, b.x, x), detail::Between(a.y, b.y, y), detail::Between(a.z, b.z, z)};
}

/// A direction within `halfAngle` radians of `axis`, every direction in that cone equally
/// likely (a weapon's spread, a spray of sparks). The axis does not have to be a unit vector; a
/// zero or non-finite one means +Z. `halfAngle` is taken as a size in [0, pi]: 0 is always the axis,
/// pi is every direction (a negative one counts as its magnitude, NaN as 0).
template <class G> Vec3 DirectionInCone(G& g, Vec3 axis, float halfAngle) {
    const float half = std::isfinite(halfAngle) ? std::fmin(std::fabs(halfAngle), 3.14159265358979f) : 0.0f;
    const float cosMax = std::cos(half);
    const float cosT = 1.0f - g.Value() * (1.0f - cosMax);      // uniform in cos(theta): even by solid angle
    const float sinT = std::sqrt(std::fmax(0.0f, 1.0f - cosT * cosT));
    const float phi = g.Value() * kTwoPi;
    Vec3 w = std::isfinite(axis.x) && std::isfinite(axis.y) && std::isfinite(axis.z) ? axis.Normalized() : Vec3{};
    if (w.SqrMagnitude() < 0.5f) w = Vec3{0.0f, 0.0f, 1.0f};
    // Any two unit vectors that, with w, make a right-handed frame.
    const Vec3 helper = std::fabs(w.z) < 0.9f ? Vec3{0.0f, 0.0f, 1.0f} : Vec3{1.0f, 0.0f, 0.0f};
    const Vec3 u = Vec3::Cross(helper, w).Normalized();
    const Vec3 v = Vec3::Cross(w, u);
    return (u * (sinT * std::cos(phi)) + v * (sinT * std::sin(phi)) + w * cosT).Normalized();
}
/// The 2D form: an angle uniform in [-halfAngle, halfAngle] around `axis` (zero axis = +X).
template <class G> Vec2 DirectionInCone2D(G& g, Vec2 axis, float halfAngle) {
    const float half = std::isfinite(halfAngle) ? std::fmin(std::fabs(halfAngle), 3.14159265358979f) : 0.0f;
    Vec2 w = std::isfinite(axis.x) && std::isfinite(axis.y) ? axis.Normalized() : Vec2{};
    if (w.SqrMagnitude() < 0.5f) w = Vec2{1.0f, 0.0f};
    const float a = (g.Value() * 2.0f - 1.0f) * half;
    const float c = std::cos(a), s = std::sin(a);
    return {w.x * c - w.y * s, w.x * s + w.y * c};
}

/// Anywhere in the triangle a-b-c (Vec2 or Vec3), evenly by area. A degenerate triangle gives a
/// point on its segment or its single point, never NaN.
template <class G, class V> V PointInTriangle(G& g, V a, V b, V c) {
    const float r1 = std::sqrt(g.Value());
    const float r2 = g.Value();
    return a * (1.0f - r1) + b * (r1 * (1.0f - r2)) + c * (r1 * r2);
}

/// +1 or -1 with equal chance.
template <class G> int Sign(G& g) { return g.Value() < 0.5f ? -1 : 1; }

/// A uniform index in [0, n) for either generator (n clamped to what Below takes).
template <class G> std::size_t IndexBelow(G& g, std::size_t n) {
    if (n <= 1) return 0;
    if (n > 0xFFFFFFFFull) n = 0xFFFFFFFFull;
    return (std::size_t)g.Below(n);
}

/// `k` DIFFERENT indices out of [0, n), in random order ("three different spawn points out of forty", a
/// hand of cards): every set of k is equally likely, and so is every order. Fewer than k come back when n
/// is smaller; none for n or k of 0. It needs memory and time for k, not n (a pick of 3 from a million
/// does not build a million-entry list), and k is capped at kMaxSample. n above 4 294 967 295 counts as
/// that (the generators' range).
constexpr std::size_t kMaxSample = std::size_t(1) << 24;
template <class G> std::vector<std::size_t> SampleIndices(G& g, std::size_t n, std::size_t k) {
    std::vector<std::size_t> out;
    if (n > 0xFFFFFFFFull) n = 0xFFFFFFFFull;
    if (k > n) k = n;
    if (k > kMaxSample) k = kMaxSample;
    if (k == 0) return out;
    if (k * 2 >= n) {   // a large share of the range: a partial shuffle of the whole list
        out.resize(n);
        std::iota(out.begin(), out.end(), std::size_t(0));
        for (std::size_t i = 0; i < k; ++i) std::swap(out[i], out[i + IndexBelow(g, n - i)]);
        out.resize(k);
        return out;
    }
    // A small share: Floyd's algorithm picks the set in k draws, then a shuffle puts it in a random order.
    std::unordered_set<std::size_t> seen;
    seen.reserve(k * 2);
    out.reserve(k);
    for (std::size_t j = n - k; j < n; ++j) {
        const std::size_t t = IndexBelow(g, j + 1);
        const std::size_t pick = seen.insert(t).second ? t : j;
        if (pick == j) seen.insert(j);
        out.push_back(pick);
    }
    for (std::size_t i = out.size(); i > 1; --i) std::swap(out[i - 1], out[IndexBelow(g, i)]);
    return out;
}

/// Blue-noise points in a width x height rectangle (Bridson's algorithm): no two closer than
/// `minDistance`, and no big gaps either, which is what trees, rocks, pickups and spawn points want (uniform
/// random clumps and leaves holes). The points are in [origin, origin + size), in the order they were found
/// (the first is a random one, each next one is near an earlier one). `attempts` (1..100, default 30)
/// is how hard it tries to fit another point around each one before giving up on it; more packs a little
/// tighter. At most `maxPoints` come back. Deterministic for a generator state. An empty list for a size or
/// distance that is not a finite number > 0, and for a grid beyond kMaxPoissonCells cells (a tiny distance in
/// a huge area): nothing is allocated then.
constexpr std::size_t kMaxPoissonCells = std::size_t(1) << 22;
template <class G>
std::vector<Vec2> PoissonDisk2D(G& g, float width, float height, float minDistance, int attempts = 30,
                                std::size_t maxPoints = 100000, Vec2 origin = {}) {
    std::vector<Vec2> pts;
    if (!(width > 0.0f) || !(height > 0.0f) || !(minDistance > 0.0f) || !std::isfinite(width) || !std::isfinite(height) ||
        !std::isfinite(minDistance) || maxPoints == 0)
        return pts;
    origin = detail::Fin(origin);
    if (attempts < 1) attempts = 1;
    if (attempts > 100) attempts = 100;
    const double r = (double)minDistance, cell = r / 1.4142135623730951;
    const double gwD = std::ceil((double)width / cell), ghD = std::ceil((double)height / cell);
    if (!(gwD * ghD <= (double)kMaxPoissonCells)) return pts;
    const std::size_t gw = (std::size_t)gwD, gh = (std::size_t)ghD;
    std::vector<std::int32_t> grid(gw * gh, -1);   // index into pts, -1 = empty (one point per cell at most)
    std::vector<std::size_t> active;
    auto cellOf = [&](float x, float y, std::size_t& cx, std::size_t& cy) {
        cx = std::min(gw - 1, (std::size_t)std::max(0.0, (double)x / cell));
        cy = std::min(gh - 1, (std::size_t)std::max(0.0, (double)y / cell));
    };
    auto add = [&](float x, float y) {   // local coordinates; the origin is added at the end
        std::size_t cx, cy;
        cellOf(x, y, cx, cy);
        grid[cy * gw + cx] = (std::int32_t)pts.size();
        active.push_back(pts.size());
        pts.push_back({x, y});
    };
    auto fits = [&](float x, float y) {   // x and y are already floats: the test sees what is stored
        if (!(x >= 0.0f && y >= 0.0f && x < width && y < height)) return false;
        std::size_t cx, cy;
        cellOf(x, y, cx, cy);
        const std::size_t x0 = cx >= 2 ? cx - 2 : 0, y0 = cy >= 2 ? cy - 2 : 0;
        const std::size_t x1 = std::min(gw - 1, cx + 2), y1 = std::min(gh - 1, cy + 2);
        for (std::size_t yy = y0; yy <= y1; ++yy)
            for (std::size_t xx = x0; xx <= x1; ++xx) {
                const std::int32_t id = grid[yy * gw + xx];
                if (id < 0) continue;
                const double dx = (double)pts[(std::size_t)id].x - (double)x, dy = (double)pts[(std::size_t)id].y - (double)y;
                if (dx * dx + dy * dy < r * r) return false;
            }
        return true;
    };
    {
        const float fx = g.Value(), fy = g.Value();
        add(std::min((float)((double)fx * (double)width), std::nextafter(width, 0.0f)),
            std::min((float)((double)fy * (double)height), std::nextafter(height, 0.0f)));
    }
    while (!active.empty() && pts.size() < maxPoints) {
        const std::size_t slot = IndexBelow(g, active.size());
        const Vec2 c = pts[active[slot]];
        bool placed = false;
        for (int a = 0; a < attempts && !placed; ++a) {
            const double ang = (double)g.Value() * (double)kTwoPi;
            const double rad = std::sqrt(r * r + (double)g.Value() * 3.0 * r * r);   // even by area between r and 2r
            const float x = (float)((double)c.x + rad * std::cos(ang)), y = (float)((double)c.y + rad * std::sin(ang));
            if (fits(x, y)) { add(x, y); placed = true; }
        }
        if (!placed) { active[slot] = active.back(); active.pop_back(); }
    }
    for (Vec2& p : pts) { p.x += origin.x; p.y += origin.y; }
    return pts;
}

}  // namespace rnd

// ---------------------------------------------------------------------------------
/// Draws every item once, in a random order, before any comes round again, so a streak
/// of the same loot or the same enemy cannot happen. Across a refill the first item of
/// the new round is never the last one drawn when another choice exists, for an entry
/// that is in the bag once. An entry added with several copies ("common" x 3) is not
/// held to that: forcing it would pin the rare entries to the start of every round.
///   Add(item, copies) puts `copies` of one entry in the bag ("common" x 3, "rare" x 1).
template <class T>
class ShuffleBag {
public:
    /// At most kMaxSlots copies in all (a million): more are dropped, so a typo of a
    /// weight cannot ask for gigabytes of slots.
    static constexpr std::size_t kMaxSlots = std::size_t(1) << 20;

    void Add(const T& item, int copies = 1) {
        if (copies <= 0) return;
        if (copies > 100000) copies = 100000;
        if (m_slots.size() >= kMaxSlots) return;
        if (m_slots.size() + (std::size_t)copies > kMaxSlots) copies = (int)(kMaxSlots - m_slots.size());
        m_items.push_back(item);
        m_copies.push_back(copies);
        for (int i = 0; i < copies; ++i) m_slots.push_back(m_items.size() - 1);
        m_order.clear();   // the new round includes it
    }
    void Clear() { m_items.clear(); m_copies.clear(); m_slots.clear(); m_order.clear(); m_last = kNone; m_rounds = 0; }
    bool Empty() const { return m_slots.empty(); }
    /// Items in a full bag (counting copies), distinct entries, and what is left this round.
    std::size_t Size() const { return m_slots.size(); }
    std::size_t Entries() const { return m_items.size(); }
    std::size_t Remaining() const { return m_order.empty() ? m_slots.size() : m_order.size(); }
    /// Rounds started so far.
    int Rounds() const { return m_rounds; }

    /// Start over: a fresh shuffle on the next draw, and the "last drawn" memory forgotten.
    void Reset() { m_order.clear(); m_last = kNone; }

    // ---- saving the bag mid-round ----------------------------------------------------------------
    // A save game that stores only the generator (Random::State, TimeSnapshot) loses where the BAG was: a
    // reload would start a fresh round, so reloading re-rolls what the player was about to get, and a
    // replay drifts. This is what the bag adds to the generator's state. The ENTRIES are yours: rebuild the
    // bag with the same Add calls (the game's own data does that anyway), then restore.
    struct State {
        std::vector<std::uint32_t> order;   ///< the entries left this round, in the order they will come OUT (the back first)
        std::int64_t last = -1;             ///< the entry drawn last (-1 = none)
        int rounds = 0;
    };
    State SaveState() const {
        State st;
        st.order.assign(m_order.begin(), m_order.end());
        st.last = m_last == kNone ? -1 : (std::int64_t)m_last;
        st.rounds = m_rounds;
        return st;
    }
    /// Put a saved state back. False, and the bag is left as it was, when the state does not fit this bag: an
    /// entry number that does not exist, an entry in the round more often than it has copies, more entries
    /// than the bag holds, a bad "last" or a negative round count.
    bool RestoreState(const State& st) {
        if (st.rounds < 0 || st.order.size() > m_slots.size()) return false;
        if (st.last < -1 || st.last >= (std::int64_t)m_items.size()) return false;
        std::vector<int> used(m_items.size(), 0);
        for (std::uint32_t i : st.order) {
            if ((std::size_t)i >= m_items.size() || ++used[i] > m_copies[i]) return false;
        }
        m_order.assign(st.order.begin(), st.order.end());
        m_last = st.last < 0 ? kNone : (std::size_t)st.last;
        m_rounds = st.rounds;
        return true;
    }
    /// For the save framework (Core/SaveFramework.hpp; templates, so this header does not include it):
    ///     bag.Save(w.Scope("loot"));   ...   bag.Load(r.Scope("loot"));
    /// Load is false, and the bag untouched, when the block holds no state, or one written for a bag of
    /// another shape (different number of entries or copies), or a damaged one.
    template <class W> void Save(W&& w) const {
        std::string order;
        for (std::size_t i = 0; i < m_order.size(); ++i) { if (i) order += ' '; order += std::to_string(m_order[i]); }
        w.String("order", order);
        w.Int64("last", m_last == kNone ? -1 : (long long)m_last);
        w.Int("rounds", m_rounds);
        w.Int("entries", (int)m_items.size());
        w.Int("slots", (int)m_slots.size());
    }
    template <class R> bool Load(const R& r) {
        if (r.Int("entries", -1) != (int)m_items.size() || r.Int("slots", -1) != (int)m_slots.size()) return false;
        State st;
        const std::string order = r.String("order", "");
        std::size_t i = 0;
        while (i < order.size()) {
            while (i < order.size() && order[i] == ' ') ++i;
            if (i >= order.size()) break;
            std::size_t j = i;
            unsigned long long v = 0;
            while (j < order.size() && order[j] >= '0' && order[j] <= '9' && j - i < 10) v = v * 10 + (unsigned long long)(order[j++] - '0');
            if (j == i || (j < order.size() && order[j] != ' ') || v > 0xFFFFFFFFull || st.order.size() >= m_slots.size()) return false;
            st.order.push_back((std::uint32_t)v);
            i = j;
        }
        st.last = r.Int64("last", -1);
        st.rounds = r.Int("rounds", 0);
        return RestoreState(st);
    }

    /// The next item; false (and `out` untouched) when the bag has nothing in it.
    template <class G> bool TryDraw(G& rng, T& out) {
        if (m_slots.empty()) return false;
        if (m_order.empty()) Refill(rng);
        const std::size_t idx = m_order.back();
        m_order.pop_back();
        m_last = idx;
        out = m_items[idx];
        return true;
    }
    /// The next item, T() when the bag is empty.
    template <class G> T Draw(G& rng) { T v{}; TryDraw(rng, v); return v; }

private:
    static constexpr std::size_t kNone = ~std::size_t(0);
    template <class G> void Refill(G& rng) {
        m_order = m_slots;
        for (std::size_t i = m_order.size(); i > 1; --i)
            std::swap(m_order[i - 1], m_order[rnd::IndexBelow(rng, i)]);
        ++m_rounds;
        // The first draw comes off the back: if it repeats the last one, swap in another entry.
        if (m_last != kNone && m_copies[m_last] == 1 && m_order.back() == m_last) {
            std::size_t others = 0;
            for (std::size_t s : m_order) if (s != m_last) ++others;
            if (others > 0) {
                std::size_t pick = rnd::IndexBelow(rng, others);
                for (std::size_t j = 0; j < m_order.size(); ++j) {
                    if (m_order[j] == m_last) continue;
                    if (pick-- == 0) { std::swap(m_order[j], m_order.back()); break; }
                }
            }
        }
    }
    std::vector<T> m_items;
    std::vector<int> m_copies;
    std::vector<std::size_t> m_slots, m_order;
    std::size_t m_last = kNone;
    int m_rounds = 0;
};

// ---------------------------------------------------------------------------------
/// Items with weights: Draw picks one with chance proportional to its weight. Weights
/// that are zero, negative, NaN or infinite make an entry undrawable (it is not an
/// error). Draw* return -1 / nullptr when nothing can be drawn. The without-replacement
/// calls take an entry out until Refill(), for "three different rewards" or a deck.
template <class T>
class WeightedTable {
public:
    int Add(const T& item, float weight) {
        m_items.push_back(item);
        m_weights.push_back(Clean(weight));
        m_used.push_back(false);
        return (int)m_items.size() - 1;
    }
    void Clear() { m_items.clear(); m_weights.clear(); m_used.clear(); }
    std::size_t Size() const { return m_items.size(); }
    const T& At(int i) const { return m_items[(std::size_t)i]; }
    float Weight(int i) const { return m_weights[(std::size_t)i]; }
    void SetWeight(int i, float w) { if (i >= 0 && (std::size_t)i < m_weights.size()) m_weights[(std::size_t)i] = Clean(w); }
    /// Sum of the weights of entries that can still be drawn without replacement.
    double TotalWeight() const { double t = 0; for (std::size_t i = 0; i < m_weights.size(); ++i) if (!m_used[i]) t += m_weights[i]; return t; }
    /// Entries left for DrawWithout (positive weight, not drawn yet).
    int Remaining() const { int n = 0; for (std::size_t i = 0; i < m_weights.size(); ++i) if (!m_used[i] && m_weights[i] > 0.0f) ++n; return n; }

    /// With replacement: the table is unchanged, the same entry can come again.
    template <class G> int DrawIndex(G& rng) const { return Pick(rng, false); }
    template <class G> const T* Draw(G& rng) const { int i = DrawIndex(rng); return i < 0 ? nullptr : &m_items[(std::size_t)i]; }

    /// Without replacement: the entry drawn is out until Refill().
    template <class G> int DrawIndexWithout(G& rng) {
        const int i = Pick(rng, true);
        if (i >= 0) m_used[(std::size_t)i] = true;
        return i;
    }
    template <class G> const T* DrawWithout(G& rng) { int i = DrawIndexWithout(rng); return i < 0 ? nullptr : &m_items[(std::size_t)i]; }
    /// Up to `n` different entries (fewer if the table runs out), best first-drawn first.
    /// Takes them out like DrawWithout does.
    template <class G> std::vector<int> DrawDistinct(G& rng, int n) {
        std::vector<int> out;
        for (int k = 0; k < n; ++k) { int i = DrawIndexWithout(rng); if (i < 0) break; out.push_back(i); }
        return out;
    }
    /// Put every drawn entry back.
    void Refill() { for (std::size_t i = 0; i < m_used.size(); ++i) m_used[i] = false; }
    /// Has entry `i` been drawn without replacement (false for an index that does not exist)?
    bool Drawn(int i) const { return i >= 0 && (std::size_t)i < m_used.size() && m_used[(std::size_t)i]; }

    // ---- saving the deck mid-way -------------------------------------------------------------------
    // Which entries DrawWithout has taken out ("0110": the second and third are gone), so a save game can carry
    // a deck or a reward pool half used. The entries and weights are yours: rebuild the table first.
    std::string DrawnMask() const {
        std::string m(m_used.size(), '0');
        for (std::size_t i = 0; i < m_used.size(); ++i) if (m_used[i]) m[i] = '1';
        return m;
    }
    /// False, and nothing changes, unless `mask` has exactly one '0' or '1' per entry.
    bool RestoreDrawn(const std::string& mask) {
        if (mask.size() != m_used.size()) return false;
        for (char c : mask) if (c != '0' && c != '1') return false;
        for (std::size_t i = 0; i < mask.size(); ++i) m_used[i] = mask[i] == '1';
        return true;
    }
    /// For the save framework: table.Save(w.Scope("rewards")) / table.Load(r.Scope("rewards")). Load is false
    /// (the table untouched) for a block that holds no state or was written for a table of another size.
    template <class W> void Save(W&& w) const { w.String("drawn", DrawnMask()); w.Int("size", (int)m_items.size()); }
    template <class R> bool Load(const R& r) {
        if (r.Int("size", -1) != (int)m_items.size()) return false;
        return RestoreDrawn(r.String("drawn", ""));
    }

private:
    static float Clean(float w) { return (w > 0.0f && std::isfinite(w)) ? w : 0.0f; }
    template <class G> int Pick(G& rng, bool withoutReplacement) const {
        double total = 0.0;
        for (std::size_t i = 0; i < m_weights.size(); ++i)
            if (m_weights[i] > 0.0f && !(withoutReplacement && m_used[i])) total += m_weights[i];
        if (!(total > 0.0)) return -1;
        const double pick = (double)rng.Value() * total;
        double acc = 0.0;
        int last = -1;
        for (std::size_t i = 0; i < m_weights.size(); ++i) {
            if (!(m_weights[i] > 0.0f) || (withoutReplacement && m_used[i])) continue;
            acc += m_weights[i];
            last = (int)i;
            if (pick < acc) return last;
        }
        return last;   // rounding left pick == total
    }
    std::vector<T> m_items;
    std::vector<float> m_weights;
    std::vector<bool> m_used;
};

// ---------------------------------------------------------------------------------
// Dice notation: "d20", "2d6+3", "3d8-1", "4d6kh3" (keep the 3 highest), "2d20kl1" (keep
// the lowest), "d%" (a d100), "1d6+1d4+2", spaces around the + and - allowed. Limits keep the numbers sane: at most
// 1000 dice a term, 100000 sides, 16 terms, constants up to 1000000.
// ---------------------------------------------------------------------------------

enum class DiceError {
    None,
    Empty,                 ///< nothing but whitespace
    TooLong,               ///< more than 256 characters
    UnexpectedCharacter,   ///< a character that has no meaning here
    ExpectedNumber,        ///< "2d6+" or "+": a term is missing
    ExpectedSides,         ///< "2d": how many sides?
    NumberTooLarge,        ///< more than 9 digits, or a constant above 1000000
    ZeroDice,              ///< "0d6"
    ZeroSides,             ///< "2d0"
    TooManyDice,           ///< "1001d6"
    TooManySides,          ///< "1d100001"
    BadKeep,               ///< "kh" without a number, or keeping more dice than were rolled
    TooManyTerms,          ///< more than 16 terms
};

struct DiceTerm {
    int sign = 1;           ///< +1 or -1
    int count = 0;          ///< dice rolled (for a constant: the value)
    int sides = 0;          ///< 0 = a constant
    int keep = 0;           ///< 0 = all dice; else the number kept
    bool keepHighest = true;
};

class DiceExpression {
public:
    DiceExpression() = default;
    explicit DiceExpression(std::vector<DiceTerm> terms) : m_terms(std::move(terms)) {}
    bool Valid() const { return !m_terms.empty(); }
    const std::vector<DiceTerm>& Terms() const { return m_terms; }
    /// Lowest, highest and average total.
    int    Min() const;
    int    Max() const;
    double Average() const;
    /// The notation again, normalised ("D20 + 3" -> "1d20+3").
    std::string ToString() const;

    /// Roll it: one draw from `rng` per die. An invalid expression rolls 0.
    template <class G> int Roll(G& rng) const {
        long long total = 0;
        for (const DiceTerm& t : m_terms) {
            long long sum = 0;
            if (t.sides == 0) {
                sum = t.count;
            } else if (t.keep <= 0 || t.keep >= t.count) {
                for (int i = 0; i < t.count; ++i) sum += 1 + (long long)rnd::IndexBelow(rng, (std::size_t)t.sides);
            } else {
                int buf[1000];
                const int n = t.count < 1000 ? t.count : 1000;
                for (int i = 0; i < n; ++i) buf[i] = 1 + (int)rnd::IndexBelow(rng, (std::size_t)t.sides);
                // selection by insertion sort: n <= 1000 and this is a rare form
                for (int i = 1; i < n; ++i) { int v = buf[i], j = i - 1; while (j >= 0 && buf[j] > v) { buf[j + 1] = buf[j]; --j; } buf[j + 1] = v; }
                if (t.keepHighest) for (int i = n - t.keep; i < n; ++i) sum += buf[i];
                else               for (int i = 0; i < t.keep; ++i)     sum += buf[i];
            }
            total += t.sign * sum;
        }
        if (total > INT_MAX) total = INT_MAX;
        if (total < INT_MIN) total = INT_MIN;
        return (int)total;
    }

private:
    std::vector<DiceTerm> m_terms;
};

struct DiceParse {
    bool ok = false;
    DiceExpression expr;       ///< valid only when ok
    DiceError error = DiceError::None;
    int position = 0;          ///< byte offset of the problem in the text
    std::string message;       ///< "expected a number after '+' at position 4"; empty when ok
};

/// Parse dice notation. Never throws; a bad string gives ok == false with the error,
/// where it is and a readable message.
DiceParse ParseDice(const std::string& text);

/// Parse and roll in one go. Returns `fallback` when the text does not parse
/// (`parse`, when given, tells why).
template <class G> int RollDice(G& rng, const std::string& text, int fallback = 0, DiceParse* parse = nullptr) {
    DiceParse p = ParseDice(text);
    const int v = p.ok ? p.expr.Roll(rng) : fallback;
    if (parse) *parse = std::move(p);
    return v;
}

}  // namespace okay
