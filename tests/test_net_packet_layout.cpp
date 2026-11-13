#include "test_framework.hpp"
#include "okay/Net/Packet.hpp"
#include <cstdint>
#include <string>
#include <vector>

using namespace okay::net;

// The wire format is little-endian on every host. These pin the exact bytes, so a
// change of layout (or a port that writes in host order) fails here.
int main() {
    RUN_SUITE("net_packet_layout");

    auto bytes = [](const Packet& p) { return std::vector<std::uint8_t>(p.Data(), p.Data() + p.Size()); };
    using B = std::vector<std::uint8_t>;

    {
        Packet p;
        p.Write(std::uint8_t(0xAB));
        p.Write(std::uint32_t(0x01020304));
        p.Write(std::int32_t(-2));
        p.Write(1.0f);
        p.Write(-2.5f);
        p.Write(std::string("hi"));
        const B want = {0xAB,
                        0x04, 0x03, 0x02, 0x01,
                        0xFE, 0xFF, 0xFF, 0xFF,
                        0x00, 0x00, 0x80, 0x3F,        // 1.0f, IEEE-754 binary32
                        0x00, 0x00, 0x20, 0xC0,        // -2.5f
                        0x02, 0x00, 0x00, 0x00, 'h', 'i'};
        CHECK(bytes(p) == want);
    }
    {   // Reading fixed bytes gives the values, whatever the host's own order.
        const std::uint8_t in[] = {0xAB, 0x04, 0x03, 0x02, 0x01, 0xFE, 0xFF, 0xFF, 0xFF,
                                   0x00, 0x00, 0x80, 0x3F, 0x03, 0x00, 0x00, 0x00, 'a', 'b', 'c'};
        Packet p(in, sizeof in);
        CHECK(p.ReadU8() == 0xAB);
        CHECK(p.ReadU32() == 0x01020304u);
        CHECK(p.ReadI32() == -2);
        CHECK(p.ReadF32() == 1.0f);
        CHECK(p.ReadString() == "abc");
        CHECK(p.Ok() && p.Remaining() == 0);
    }
    {   // Extremes round-trip.
        Packet p;
        p.Write(std::uint32_t(0xFFFFFFFFu)); p.Write(std::int32_t(INT32_MIN)); p.Write(0.0f);
        Packet r(p.Data(), p.Size());
        CHECK(r.ReadU32() == 0xFFFFFFFFu && r.ReadI32() == INT32_MIN && r.ReadF32() == 0.0f && r.Ok());
        CHECK(bytes(p) == (B{0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x80, 0, 0, 0, 0}));
    }
    {   // The standalone helpers agree.
        std::uint8_t b[4];
        StoreU32LE(b, 0xA1B2C3D4u);
        CHECK(b[0] == 0xD4 && b[1] == 0xC3 && b[2] == 0xB2 && b[3] == 0xA1);
        CHECK(LoadU32LE(b) == 0xA1B2C3D4u);
    }
    {   // A short read is an overflow, not garbage.
        const std::uint8_t in[] = {1, 2, 3};
        Packet p(in, sizeof in);
        p.ReadU32();
        CHECK(!p.Ok());
    }

    TEST_MAIN_RESULT();
}
