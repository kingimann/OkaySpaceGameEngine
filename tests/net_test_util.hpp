#pragma once
// Small readers the network tests share (net_easy, net_session, net_online, relay_security): a script's or a
// graph's variable as text, "does any overlay line contain this", and a bare relay peer that speaks the
// current relay protocol by hand (RelayHello.hpp), for tests that play a stranger or a host without a game.
#include <Okay.hpp>
#include "okay/Net/RelayHello.hpp"
#include "okay/Net/RelayProtocol.hpp"
#include "okay/Net/UdpSocket.hpp"
#include <array>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace nettest {

inline std::string ScriptVar(okay::GameObject* go, const char* name) {
    auto* sc = go ? go->GetComponent<okay::ScriptComponent>() : nullptr;
    return sc && sc->VM() ? sc->VM()->GetGlobal(name).AsString() : std::string();
}
inline std::string GraphVar(okay::GameObject* go, const char* name) {
    auto* v = go ? go->GetComponent<okay::VisualScriptComponent>() : nullptr;
    return v ? v->GetVariable(name).AsString() : std::string();
}
inline bool HasLine(const std::vector<std::string>& lines, const std::string& part) {
    for (const auto& l : lines) if (l.find(part) != std::string::npos) return true;
    return false;
}

/// A relay peer with no game behind it: says hello (echoing the relay's cookie and its own nonce) and
/// reads what the relay sends back (welcome, address frames), counting the bytes.
struct RawRelayPeer {
    okay::net::UdpSocket s;
    okay::net::Endpoint relay;
    std::array<std::uint8_t, okay::net::kRelayCookieBytes> cookie{};
    std::uint64_t nonce = 0;
    std::uint32_t slot = 0, hostSlot = 0;
    std::uint64_t hostKey = 0, succ = 0;
    std::vector<okay::net::RelayPeerAddrInfo> addrs;
    int welcomes = 0, pktsIn = 0;
    std::size_t bytesIn = 0, bytesOut = 0;
    explicit RawRelayPeer(std::uint16_t relayPort) {
        s.Open(); s.Bind(0);
        relay = okay::net::Endpoint{0x7F000001u, relayPort};
        std::random_device rd;
        do { nonce = (static_cast<std::uint64_t>(rd()) << 32) | rd(); } while (nonce == 0);
    }
    std::uint16_t Port() const { return s.LocalPort(); }
    void Send(const std::vector<std::uint8_t>& pk) { s.SendTo(relay, pk.data(), pk.size()); bytesOut += pk.size(); }
    void Hello(okay::net::RelayRole role, const std::string& code, std::uint64_t key, std::uint8_t flags) {
        okay::net::RelayHelloParams h;
        h.role = role; h.code = code; h.hostKey = key; h.cookie = cookie; h.nonce = nonce; h.flags = flags;
        Send(okay::net::BuildRelayHello(h));
    }
    void Bye() { Send({okay::net::RelayBye}); }
    void Drain() {
        std::uint8_t b[2048]; okay::net::Endpoint from; int n;
        while ((n = s.RecvFrom(b, sizeof b, from)) > 0) {
            bytesIn += static_cast<std::size_t>(n); ++pktsIn;
            if (b[0] == okay::net::RelayCookie && n >= 18) {
                std::memcpy(cookie.data(), b + 2, cookie.size());
            } else if (b[0] == okay::net::RelayWelcome) {
                okay::net::RelayWelcomeInfo w;
                if (okay::net::ParseRelayWelcome(b, static_cast<std::size_t>(n), nonce, false, w)) {
                    ++welcomes; slot = w.slot; hostSlot = w.hostSlot;
                    if (w.hostKey) hostKey = w.hostKey;
                    if (w.successorKey) succ = w.successorKey;
                }
            } else if (b[0] == okay::net::RelayPeerAddr) {
                okay::net::RelayPeerAddrInfo a;
                if (okay::net::ParseRelayPeerAddr(b, static_cast<std::size_t>(n), nonce, a)) addrs.push_back(a);
            }
        }
    }
};

} // namespace nettest
