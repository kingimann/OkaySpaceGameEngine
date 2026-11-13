// Regression tests for the round-3 hostile-input bug hunt: each case was a crash, hang,
// undefined behaviour or silent data loss on a malformed file / packet / extreme value.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Core/Localization.hpp"
#include "okay/Core/Subtitles.hpp"
#include "okay/Modeling/PolyMesh.hpp"
#include "okay/Net/NetChannels.hpp"
#include "okay/Net/NetDelta.hpp"
#include "okay/Net/Packet.hpp"
#include "okay/Render/ConsoleRenderer.hpp"
#include <chrono>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace okay;
using namespace okay::net;

int main() {
    RUN_SUITE("bughunt_round3");

    // ---- NetDelta: int32 position overflow in encode and decode ----
    {
        DeltaBase base, cur;
        base.pose["a"] = QPose{-2147483647, 0, 0, 0, 0, 0, 32767};
        cur.pose["a"] = QPose{2147483647, 0, 0, 0, 0, 0, 32767};
        auto enc = EncodeSnapshot(base, cur, 2, 0.0);   // difference does not fit int32 -> keyframe
        CHECK(!enc.empty());
        DeltaBase have; have.id = 1;
        have.pose["a"] = QPose{2147483647, 0, 0, 0, 0, 0, 32767};
        std::vector<std::uint8_t> b;
        auto u8 = [&](std::uint8_t v) { b.push_back(v); };
        auto u16 = [&](std::uint16_t v) { u8(v & 0xFF); u8(v >> 8); };
        auto u32 = [&](std::uint32_t v) { u16(v & 0xFFFF); u16(v >> 16); };
        u8(kSnapDelta); u32(1); u32(2);
        for (int i = 0; i < 8; ++i) u8(0);
        u16(1); u8(1); u8('a'); u8(0);
        u16(1000); u16(0); u16(0);
        u16(0); u16(0); u16(0); u16(32767);
        std::uint32_t bi, si; double t; std::vector<SnapChange> ch;
        const bool ok = DecodeSnapshot(b.data(), b.size(), have, bi, si, t, ch);
        if (ok && !ch.empty()) CHECK(ch[0].pose.x == 2147483647);   // saturated, not wrapped
    }

    // ---- SRT: a hostile timestamp is rejected, not overflowed ----
    {
        SubtitleTrack t; std::string err;
        CHECK(!SubtitleTrack::ParseSrt("1\n99999999999:00:00,000 --> 00:00:01,000\nhi\n", t, &err));
        CHECK(!SubtitleTrack::ParseSrt("1\n00:00:00,000 --> 2147483647:59:59,999\nhi\n", t, &err));
        CHECK(SubtitleTrack::ParseSrt("1\n00:00:01,500 --> 00:00:03,000\nhello\n", t, &err));
        CHECK(t.cues.size() == 1 && std::fabs(t.cues[0].start - 1.5f) < 1e-3f);
    }

    // ---- .okayloc: \v and \f are blanks, not an infinite loop ----
    {
        StringTable st; std::string err;
        const auto t0 = std::chrono::steady_clock::now();
        st.Parse("okayloc 1\nlanguage en\v\n", &err);
        st.Parse("okayloc 1\nlanguage en\f\nkey\vvalue\n", &err);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        CHECK(ms < 2000);
    }

    // ---- PolyMesh: an absurd declared count is an error, not an abort ----
    {
        modeling::PolyMesh pm; std::string err;
        bool threw = false;
        try { CHECK(!modeling::PolyMesh::Parse("okaymesh 1\nverts 99999999999999\nfaces 1\n", pm, &err)); }
        catch (...) { threw = true; }
        CHECK(!threw);
    }

    // ---- Ordered channel: a full window queues instead of dropping ----
    {
        OrderedEnd tx, rx;
        std::vector<std::string> got;
        std::vector<std::vector<std::uint8_t>> wire;
        const int total = 100;
        for (int i = 0; i < total; ++i) {
            auto b = tx.Push("c", "m" + std::to_string(i), 0);
            if (!b.empty()) wire.push_back(b);
        }
        CHECK(wire.size() == kOrderedWindow);            // the rest are queued, not lost
        for (int round = 0; round < 50 && (int)got.size() < total; ++round) {
            std::vector<std::vector<std::uint8_t>> next;
            for (auto& w : wire) {
                Packet p(w.data(), w.size());
                p.ReadU8(); std::uint32_t seq = p.ReadU32(); std::string c = p.ReadString(), d = p.ReadString();
                std::vector<ChanMsg> out; std::uint32_t ack = 0;
                rx.Receive(c, seq, d, true, 1, out, ack);
                for (auto& m : out) got.push_back(m.data);
                if (ack) tx.Ack(c, ack);
            }
            tx.CollectResend(1.0f, next);
            wire = next;
        }
        CHECK((int)got.size() == total);
        bool inOrder = true;
        for (int i = 0; i < (int)got.size(); ++i) if (got[i] != "m" + std::to_string(i)) inOrder = false;
        CHECK(inOrder);
    }

    // ---- ConsoleRenderer: NaN / infinite inputs don't crash or invoke UB ----
    {
        const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
        ConsoleRenderer cr(20, 10, false);
        cr.BeginFrame(Color{nan, nan, nan, 1.0f});
        cr.BeginFrame(Color{inf, inf, inf, 1.0f});
        cr.SetCamera(Vec3{0, 0, 0}, 5.0f, 1.0f);
        cr.DrawQuad(Vec3{nan, 0, 0}, Vec2{1, 1}, Color{1, 1, 1, 1}, 'X');
        cr.DrawQuad(Vec3{0, 0, 0}, Vec2{inf, inf}, Color{1, 1, 1, 1}, 'X');
        cr.DrawQuad(Vec3{1e30f, -1e30f, 0}, Vec2{1e30f, 1}, Color{1, 1, 1, 1}, 'X');
        CHECK(true);
    }

    TEST_MAIN_RESULT();
}
