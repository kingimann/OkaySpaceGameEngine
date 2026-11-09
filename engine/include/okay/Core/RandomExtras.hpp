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
// Seeding from text and independent child streams are on Random itself:
// Random::HashSeed / SeedFromString / FromString, and Random::Fork().
#include "okay/Core/Random.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Math/Vec3.hpp"
#include <climits>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace okay {

namespace rnd {

constexpr float kTwoPi = 6.28318530717958647692f;

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
    return center + Direction2D(g) * std::fabs(radius);
}
/// Anywhere in the disc, evenly by area (not bunched at the centre).
template <class G> Vec2 PointInCircle(G& g, float radius = 1.0f, Vec2 center = {}) {
    const float r = std::fabs(radius) * std::sqrt(g.Value());
    return center + Direction2D(g) * r;
}
/// Evenly by area between two radii (a spawn ring around the player). The radii may come in either order.
template <class G> Vec2 PointInRing(G& g, float innerRadius, float outerRadius, Vec2 center = {}) {
    float a = std::fabs(innerRadius), b = std::fabs(outerRadius);
    if (a > b) std::swap(a, b);
    const float r = std::sqrt(a * a + g.Value() * (b * b - a * a));
    return center + Direction2D(g) * r;
}
template <class G> Vec3 PointOnSphere(G& g, float radius = 1.0f, Vec3 center = {}) {
    return center + Direction3D(g) * std::fabs(radius);
}
/// Anywhere in the ball, evenly by volume.
template <class G> Vec3 PointInSphere(G& g, float radius = 1.0f, Vec3 center = {}) {
    const float r = std::fabs(radius) * std::cbrt(g.Value());
    return center + Direction3D(g) * r;
}
/// In the rectangle / box between two corners (given in either order).
template <class G> Vec2 PointInRect(G& g, Vec2 a, Vec2 b) {
    const float x = g.Value(), y = g.Value();
    return {std::fmin(a.x, b.x) + x * std::fabs(b.x - a.x), std::fmin(a.y, b.y) + y * std::fabs(b.y - a.y)};
}
template <class G> Vec3 PointInBox(G& g, Vec3 a, Vec3 b) {
    const float x = g.Value(), y = g.Value(), z = g.Value();
    return {std::fmin(a.x, b.x) + x * std::fabs(b.x - a.x),
            std::fmin(a.y, b.y) + y * std::fabs(b.y - a.y),
            std::fmin(a.z, b.z) + z * std::fabs(b.z - a.z)};
}

/// A uniform index in [0, n) for either generator (n clamped to what Below takes).
template <class G> std::size_t IndexBelow(G& g, std::size_t n) {
    if (n <= 1) return 0;
    if (n > 0xFFFFFFFFull) n = 0xFFFFFFFFull;
    return (std::size_t)g.Below(n);
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
