#include "test_framework.hpp"
#include "okay/Net/NetDelta.hpp"
#include <cstdint>
#include <string>
#include <vector>

using namespace okay;
using namespace okay::net;

// Named tests for the delta snapshot layer (NetDelta.hpp): pose quantization, delta against an
// acknowledged baseline, a lost ack, and hostile or truncated datagrams. Hand-computed numbers.

namespace {
QPose P(int x, int y, int z) { QPose q; q.x = x; q.y = y; q.z = z; return q; }
const SnapChange* Find(const std::vector<SnapChange>& v, const std::string& id) {
    for (const auto& c : v) if (c.id == id) return &c;
    return nullptr;
}
}

int main() {
    // ---- QPose: millimetres and 15-bit components ----
    {
        QPose q = QuantizePose(Vec3(1.2345f, -2.0f, 0.0004f), Quat(0, 0, 0, 1));
        CHECK(q.x == 1235);            // float 1.2345 is 1234.50005 mm, rounds to 1235
        CHECK(q.y == -2000);
        CHECK(q.z == 0);               // 0.4 mm rounds to 0
        CHECK(q.qw == 32767 && q.qx == 0);
        Vec3 p; Quat r;
        DequantizePose(P(1500, -250, 0), p, r);
        CHECK_NEAR(p.x, 1.5, 1e-6); CHECK_NEAR(p.y, -0.25, 1e-6); CHECK_NEAR(p.z, 0.0, 1e-6);
        CHECK_NEAR(r.w, 1.0, 1e-6);    // the default identity qw
        // Non-finite input collapses to zero instead of poisoning the wire
        QPose bad = QuantizePose(Vec3(NAN, INFINITY, -INFINITY), Quat(NAN, 0, 0, 1));
        CHECK(bad.x == 0 && bad.y == 0 && bad.z == 0 && bad.qx == 0);
        // An all-zero quaternion decodes to identity, not NaN
        QPose zero; zero.qw = 0;
        DequantizePose(zero, p, r);
        CHECK_NEAR(r.w, 1.0, 1e-6); CHECK_NEAR(r.x, 0.0, 1e-6);
        // A 90 degree turn about Y survives the round trip within 15-bit precision
        Quat y90(0, 0.70710678f, 0, 0.70710678f);
        DequantizePose(QuantizePose(Vec3(0, 0, 0), y90), p, r);
        CHECK_NEAR(r.y, 0.70710678, 1e-3); CHECK_NEAR(r.w, 0.70710678, 1e-3);
    }

    // ---- First snapshot (baseline id 0) sends keyframes; the receiver reads them back ----
    DeltaBase empty;
    DeltaBase s1; s1.id = 0;
    s1.pose["a"] = P(1000, 2000, 3000);
    s1.pose["b"] = P(-5, 0, 7);
    s1.extra["a"] = "hp=10";
    std::vector<std::uint8_t> d1 = EncodeSnapshot(empty, s1, 1, 12.5);
    CHECK(!d1.empty());
    // header 1+4+4+8+2 = 19; "a": 1+1+1+12+8+(2+5) = 30; "b": 1+1+1+12+8 = 23
    CHECK(d1.size() == 19 + 30 + 23);
    {
        std::uint32_t base = 99, id = 99; double tick = 0; std::vector<SnapChange> ch;
        CHECK(PeekSnapshot(d1.data(), d1.size(), base, id));
        CHECK(base == 0 && id == 1);
        CHECK(DecodeSnapshot(d1.data(), d1.size(), empty, base, id, tick, ch));
        CHECK(tick == 12.5);
        CHECK(ch.size() == 2);
        const SnapChange* a = Find(ch, "a"); const SnapChange* b = Find(ch, "b");
        CHECK(a && b);
        if (a && b) {
            CHECK(a->pose == s1.pose["a"] && a->hasExtra && a->extra == "hp=10");
            CHECK(b->pose == s1.pose["b"] && !b->hasExtra);
        }
    }

    // ---- Nothing changed: no datagram at all ----
    CHECK(EncodeSnapshot(s1, s1, 2, 13.0).empty());

    // ---- Delta against an acked baseline is small and exact ----
    DeltaBase acked = s1; acked.id = 1;
    DeltaBase s2 = acked; s2.id = 0;
    s2.pose["a"].x += 30;     // a moved 3 cm in x
    std::vector<std::uint8_t> d2 = EncodeSnapshot(acked, s2, 2, 13.0);
    // header 19 + only "a": 1+1+1 + 6 (three int16 deltas) + 8 = 17 -> 36 bytes; "b" not sent
    CHECK(d2.size() == 19 + 17);
    {
        std::uint32_t base = 0, id = 0; double tick = 0; std::vector<SnapChange> ch;
        CHECK(DecodeSnapshot(d2.data(), d2.size(), acked, base, id, tick, ch));
        CHECK(base == 1 && id == 2 && ch.size() == 1);
        if (ch.size() == 1) {
            CHECK(ch[0].id == "a" && ch[0].pose.x == 1030 && ch[0].pose.y == 2000 && ch[0].pose.z == 3000);
            CHECK(ch[0].extra == "hp=10");   // unchanged extra is carried from the baseline
        }
        // Lost ack / stale receiver: the same datagram against a baseline with another id is refused
        DeltaBase stale = acked; stale.id = 7;
        CHECK(!DecodeSnapshot(d2.data(), d2.size(), stale, base, id, tick, ch));
        CHECK(ch.empty());
        // A later packet still decodes against the same baseline (ack not yet landed)
        DeltaBase s3 = s2; s3.pose["b"].z = 100;
        std::vector<std::uint8_t> d3 = EncodeSnapshot(acked, s3, 3, 13.5);
        CHECK(DecodeSnapshot(d3.data(), d3.size(), acked, base, id, tick, ch));
        CHECK(ch.size() == 2 && id == 3);
        const SnapChange* b = Find(ch, "b");
        CHECK(b && b->pose.z == 100 && b->pose.x == -5);
    }

    // ---- A jump over 32767 mm falls back to a keyframe (still exact) ----
    {
        DeltaBase far = acked; far.id = 0;
        far.pose["a"] = P(1000 + 40000, 2000, 3000);
        std::vector<std::uint8_t> d = EncodeSnapshot(acked, far, 4, 1.0);
        CHECK(d.size() == 19 + 1 + 1 + 1 + 12 + 8);
        std::uint32_t base, id; double tick; std::vector<SnapChange> ch;
        CHECK(DecodeSnapshot(d.data(), d.size(), acked, base, id, tick, ch));
        CHECK(ch.size() == 1 && ch[0].pose.x == 41000);
    }

    // ---- An extra change that clears the string is sent and decoded as empty ----
    {
        DeltaBase cleared = acked; cleared.id = 0; cleared.extra["a"] = "";
        std::vector<std::uint8_t> d = EncodeSnapshot(acked, cleared, 5, 2.0);
        std::uint32_t base, id; double tick; std::vector<SnapChange> ch;
        CHECK(DecodeSnapshot(d.data(), d.size(), acked, base, id, tick, ch));
        CHECK(ch.size() == 1 && ch[0].hasExtra && ch[0].extra.empty());
    }

    // ---- Hostile and truncated datagrams ----
    {
        std::uint32_t base, id; double tick; std::vector<SnapChange> ch;
        for (std::size_t n = 0; n < d1.size(); ++n) {   // every truncation is refused
            CHECK(!DecodeSnapshot(d1.data(), n, empty, base, id, tick, ch));
        }
        std::vector<std::uint8_t> m = d1;
        m[0] = kSnapAck;                                  // wrong type byte
        CHECK(!DecodeSnapshot(m.data(), m.size(), empty, base, id, tick, ch));
        m = d1; m[5] = m[6] = m[7] = m[8] = 0;            // snapshot id 0 is refused
        CHECK(!DecodeSnapshot(m.data(), m.size(), empty, base, id, tick, ch));
        m = d1; m[17] = 0xFF; m[18] = 0xFF;               // 65535 objects, over the cap of 256
        CHECK(!DecodeSnapshot(m.data(), m.size(), empty, base, id, tick, ch));
        // A delta record for an object the baseline does not have
        DeltaBase other; other.id = 1; other.pose["zzz"] = P(0, 0, 0);
        CHECK(!DecodeSnapshot(d2.data(), d2.size(), other, base, id, tick, ch));
        // A hostile delta added to a saturated baseline clamps instead of overflowing
        DeltaBase sat; sat.id = 1; sat.pose["a"] = P(2147483647, 0, 0);
        sat.extra["a"] = "hp=10";
        DeltaBase mv = sat; mv.id = 0; mv.pose["a"].x = 2147483647 - 100;
        std::vector<std::uint8_t> dd = EncodeSnapshot(sat, mv, 2, 0);
        CHECK(DecodeSnapshot(dd.data(), dd.size(), sat, base, id, tick, ch));
        CHECK(ch.size() == 1 && ch[0].pose.x == 2147483647 - 100);
        CHECK(!PeekSnapshot(nullptr, 0, base, id));
    }

    // ---- Ack datagram ----
    {
        std::vector<std::uint8_t> a = EncodeSnapAck(0x01020304u);
        CHECK(a.size() == 5 && a[0] == kSnapAck && a[1] == 0x04 && a[4] == 0x01);
        std::uint32_t id = 0;
        CHECK(DecodeSnapAck(a.data(), a.size(), id) && id == 0x01020304u);
        CHECK(!DecodeSnapAck(a.data(), 4, id));
        std::vector<std::uint8_t> z = EncodeSnapAck(0);
        CHECK(!DecodeSnapAck(z.data(), z.size(), id));   // id 0 means nothing acknowledged
    }

    // ---- SameBase ----
    {
        DeltaBase x = s1, y = s1;
        CHECK(SameBase(x, y));
        y.pose["a"].x += 1;
        CHECK(!SameBase(x, y));
    }

    std::cout << okaytest::g_checks << " checks, " << okaytest::g_failures << " failures\n";
    return okaytest::g_failures ? 1 : 0;
}
