#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Net/Packet.hpp"
#include "okay/Net/UdpSocket.hpp"
#include <string>
#include <vector>

using namespace okay;
using namespace okay::net;

// Network input is untrusted: malformed packets must be rejected, forged
// fragment headers must not balloon memory (test_net_races covers threads).

namespace {

std::vector<std::uint8_t> Frag(std::uint32_t id, std::uint32_t index, std::uint32_t count, std::size_t payload) {
    Packet p(16);                                  // Fragment tag
    p.Write(id); p.Write(index); p.Write(count);
    std::vector<std::uint8_t> d(p.Data(), p.Data() + p.Size());
    d.resize(d.size() + payload, static_cast<std::uint8_t>(index));
    return d;
}

} // namespace

int main() {
    RUN_SUITE("net_hardening");

    // ---- Packet::ReadString: a length past the end is malformed ----
    {
        Packet w(1); w.Write(std::uint32_t(1000)); w.Write(std::uint8_t('x'));   // claims 1000 bytes, has 1
        Packet r(w.Data(), w.Size());
        r.ReadU8();
        CHECK(r.ReadString().empty());
        CHECK(!r.Ok());
    }
    {
        Packet w(1); w.Write(std::uint32_t(0xFFFFFFFFu));                       // would wrap m_read + n
        Packet r(w.Data(), w.Size());
        r.ReadU8();
        CHECK(r.ReadString().empty());
        CHECK(!r.Ok());
    }
    {
        Packet w(1); w.Write(std::string("name")); w.Write(std::string());      // empty string ends the packet
        Packet r(w.Data(), w.Size());
        r.ReadU8();
        CHECK(r.ReadString() == "name");
        CHECK(r.ReadString().empty());
        CHECK(r.Ok());
        r.ReadU8();                                                             // nothing left
        CHECK(!r.Ok());
    }

    // ---- Fragment reassembly: strangers get a tiny budget ----
    {
        Scene s("frag");
        auto* nm = s.CreateGameObject("Net")->AddComponent<NetworkManager>();
        std::vector<std::uint8_t> out;
        const Endpoint a{0x0A000001u, 1000};
        auto huge = Frag(1, 0, 65535, 0);
        CHECK(!nm->FeedFragmentForTest(a, huge.data(), huge.size(), out));
        CHECK(nm->PendingFragmentMessages(a) == 0);                            // nothing allocated for it
        auto big = Frag(2, 0, 2, net::kFragmentThreshold);                     // over the MTU: forged
        CHECK(!nm->FeedFragmentForTest(a, big.data(), big.size(), out));
        // A small two-piece message (a Join with a long token) still reassembles.
        auto f0 = Frag(3, 0, 2, 100), f1 = Frag(3, 1, 2, 50);
        CHECK(!nm->FeedFragmentForTest(a, f0.data(), f0.size(), out));
        auto other = Frag(4, 0, 2, 10);                                        // a second message at once
        CHECK(!nm->FeedFragmentForTest(a, other.data(), other.size(), out));
        CHECK(nm->PendingFragmentMessages(a) == 1);
        CHECK(nm->FeedFragmentForTest(a, f1.data(), f1.size(), out));
        CHECK(out.size() == 150);
        CHECK(nm->PendingFragmentMessages(a) == 0);

        // 13-byte headers claiming huge messages from thousands of addresses:
        // memory stays tiny and bounded.
        for (std::uint32_t i = 0; i < 20000; ++i) {
            auto h = Frag(i, 0, 8, 1000);
            nm->FeedFragmentForTest(Endpoint{0x0B000000u + i, 2000}, h.data(), h.size(), out);
        }
        CHECK(nm->PendingFragmentBytes() <= NetworkManager::kMaxStrangers * 1000);
    }

    // ---- ...a handshaken peer (our server) gets the full one, still capped ----
    {
        Scene s("frag2");
        auto* nm = s.CreateGameObject("Net")->AddComponent<NetworkManager>();
        CHECK(nm->StartClient("127.0.0.1", 47999));
        const Endpoint server{0x7F000001u, 47999};
        std::vector<std::uint8_t> out;
        auto ok = Frag(1, 0, NetworkManager::kMaxFragments, 10);
        CHECK(!nm->FeedFragmentForTest(server, ok.data(), ok.size(), out));
        CHECK(nm->PendingFragmentMessages(server) == 1);                       // accepted, waiting for the rest
        auto tooMany = Frag(2, 0, NetworkManager::kMaxFragments + 1, 10);
        CHECK(!nm->FeedFragmentForTest(server, tooMany.data(), tooMany.size(), out));
        CHECK(nm->PendingFragmentMessages(server) == 1);
        for (std::uint32_t id = 10; id < 100; ++id) {
            auto f = Frag(id, 0, 4, 10);
            nm->FeedFragmentForTest(server, f.data(), f.size(), out);
        }
        CHECK(nm->PendingFragmentMessages(server) == NetworkManager::kMaxPendingMessages);
        // Byte cap: one huge message fed piece by piece stops at kMaxPendingBytes.
        nm->Stop();
        CHECK(nm->StartClient("127.0.0.1", 47999));
        const std::size_t chunk = net::kFragmentThreshold - 13;
        std::uint32_t taken = 0;
        for (std::uint32_t m = 0; m < 4; ++m)
            for (std::uint32_t i = 0; i < NetworkManager::kMaxFragments - 1; ++i) {
                auto f = Frag(500 + m, i, NetworkManager::kMaxFragments, chunk);
                nm->FeedFragmentForTest(server, f.data(), f.size(), out);
                ++taken;
            }
        CHECK(nm->PendingFragmentBytes() <= NetworkManager::kMaxPendingBytes);
        CHECK(taken > 0);
        nm->Stop();
    }

    TEST_MAIN_RESULT();
}
