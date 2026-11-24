// Playing over the internet for NetworkManager: direct paths punched through home routers
// (NAT hole punching) beside the relay, host migration through the relay, and online play
// by a short room code (HostOnline / JoinCode). Written for this engine from its own relay
// protocol (RelayProtocol.hpp, version 5) and the general hole-punching idea (both sides
// send to each other's public address at once; RFC 4787 describes the router behaviours);
// no outside code.
//
// Direct paths:
//   1. A client that wants one and its host both set RelayHelloDirect in their relay hellos
//      (directConnect). The relay then tells each the other's public address and one random
//      token (RelayPeerAddr, echoing the hello nonce, so a forged one is dropped).
//   2. Both send RelayPunch probes straight to that address, ten a second. A probe that carries
//      the token is answered; where it came from is where the other side really is (a router
//      that gave it a different port than the relay saw still works one way round).
//   3. An answer means the path works both ways: game datagrams for that peer go straight there
//      (Transmit) and come back keyed by its relay slot, so everything above sees no change.
//   4. No answer in punchTimeoutSeconds (a symmetric NAT on both sides, a firewall): the relay
//      carries on carrying it. An open path that goes quiet for 3 s goes back to the relay too.
#include "okay/Net/NetworkManager.hpp"
#include "okay/Net/JoinLink.hpp"
#include "okay/Net/NetSim.hpp"
#include "okay/Net/RelayHello.hpp"
#include "okay/Net/ServerQuery.hpp"
#include "okay/Core/Log.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include "NetworkManagerInternal.hpp"

namespace okay {

namespace {
constexpr std::size_t kMaxDirectPaths = 64;   // a relay session holds at most 32 peers
constexpr float kDirectQuietSeconds = 3.0f;   // an open path silent this long goes back to the relay
constexpr float kProbeEverySeconds = 0.1f;    // while punching
constexpr float kKeepEverySeconds = 0.5f;     // once open: keeps the routers' mappings alive
constexpr int kMaxCodeTries = 5;              // HostOnline: made-up codes tried when one is taken
constexpr char kCodeLetters[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";   // no 0/O, 1/I

bool GitHubHost(std::string h) {   // rule: nothing a player uses may depend on GitHub
    for (char& c : h) c = (char)std::tolower((unsigned char)c);
    const auto colon = h.find(':');
    if (colon != std::string::npos) h.resize(colon);
    for (const char* bad : {"github.com", "github.io", "githubusercontent.com"}) {
        const std::string b = bad;
        if (h == b || (h.size() > b.size() && h.compare(h.size() - b.size(), b.size(), b) == 0 && h[h.size() - b.size() - 1] == '.'))
            return true;
    }
    return false;
}
} // namespace

// ---- direct paths ----------------------------------------------------------------------------------------

NetworkManager::DirectPath* NetworkManager::DirectFor(std::uint32_t slot) {
    for (DirectPath& d : m_direct) if (d.slot == slot) return &d;
    return nullptr;
}
const NetworkManager::DirectPath* NetworkManager::DirectFor(std::uint32_t slot) const {
    for (const DirectPath& d : m_direct) if (d.slot == slot) return &d;
    return nullptr;
}

void NetworkManager::SendRaw(const net::Endpoint& to, const void* data, std::size_t size) {
    if (!net::SendThroughNat(m_socket, to, data, size)) m_socket.SendTo(to, data, size);
}

bool NetworkManager::WantDirectNow() const {
    if (!directConnect || !m_relay || !m_relayWelcomed) return false;
    if (m_mode == Mode::Server) return !m_relayPromoting;   // the host: consent, while it is the host
    if (m_mode != Mode::Client || m_serverEp.port != net::kRelaySlotPort || m_serverEp.address == 0) return false;
    const DirectPath* d = DirectFor(m_serverEp.address);
    return !d || (!d->open && !d->failed);   // a client: only while it still wants its host's address
}

void NetworkManager::OnRelayPeerAddr(const std::uint8_t* d, std::size_t n) {
    net::RelayPeerAddrInfo a;
    if (!net::ParseRelayPeerAddr(d, n, m_relayNonce, a)) return;   // forged, or not for this attempt
    if (!directConnect || m_relayPromoting) return;
    if (m_mode == Mode::Client && !(m_serverEp == SlotEndpoint(a.slot))) return;   // only our host's
    if (m_mode != Mode::Client && m_mode != Mode::Server) return;
    DirectPath* p = DirectFor(a.slot);
    if (p && p->token == a.token) return;   // the relay repeats itself until we stop asking
    if (!p) {
        if (m_direct.size() >= kMaxDirectPaths) return;
        m_direct.emplace_back();
        p = &m_direct.back();
    }
    *p = DirectPath{};
    p->slot = a.slot;
    p->ep = net::Endpoint{a.address, a.port};
    p->token = a.token;
    OKAY_INFO("net: trying a direct path to ", m_mode == Mode::Server ? "a player" : "the host", " (relay slot ", a.slot, ")");
}

bool NetworkManager::OnDirectDatagram(std::uint8_t* b, int n, net::Endpoint& from) {
    if (m_direct.empty() || n <= 0) return false;
    if ((std::size_t)n == net::kRelayPunchBytes && b[0] == net::RelayPunch) {
        const std::uint8_t kind = b[1];
        const std::uint64_t token = static_cast<std::uint64_t>(net::detail::GetU32LE(b + 2)) |
                                    (static_cast<std::uint64_t>(net::detail::GetU32LE(b + 6)) << 32);
        const std::uint32_t sender = net::detail::GetU32LE(b + 10);
        for (DirectPath& p : m_direct) {
            if (p.failed || p.token != token || p.slot != sender) continue;
            p.heard = true;
            p.ep = from;      // where it really is (its router may have given it another port for us)
            p.quiet = 0.0f;
            if (kind == 0) {
                const auto reply = net::BuildRelayPunch(1, p.token, m_relaySlot);
                if (!net::HoldDatagram(m_socket, p.ep, reply.data(), reply.size())) m_socket.SendTo(p.ep, reply.data(), reply.size());
            } else if (!p.open) {
                p.open = true;
                ++m_punchOpened;
                OKAY_INFO("net: direct path to relay slot ", p.slot, " is open (", p.age, " s); traffic skips the relay");
            }
            break;
        }
        return false;   // consumed
    }
    for (DirectPath& p : m_direct) {
        if (p.failed || !(p.ep == from) || !(p.open || p.heard)) continue;
        // Game traffic on the path: the other side has it open, so ours works both ways too.
        if (!p.open) { p.open = true; ++m_punchOpened; }
        p.quiet = 0.0f;
        from = SlotEndpoint(p.slot);
        return true;
    }
    return false;
}

void NetworkManager::DirectTick(float dt) {
    for (auto it = m_direct.begin(); it != m_direct.end();) {
        DirectPath& p = *it;
        // The host forgets a path whose player left (and that is no use any more).
        if (m_mode == Mode::Server && p.failed && !m_clients.count(SlotEndpoint(p.slot)) && p.age > 30.0f) {
            it = m_direct.erase(it);
            continue;
        }
        p.age += dt;
        if (p.failed) { ++it; continue; }
        p.quiet += dt;
        p.probeTimer -= dt;
        if (!p.open && p.age >= punchTimeoutSeconds) {
            p.failed = true;
            ++m_punchFailed;
            OKAY_INFO("net: no direct path to relay slot ", p.slot, " after ", punchTimeoutSeconds,
                      " s (a strict router on both sides?); it stays on the relay");
        } else if (p.open && p.quiet >= kDirectQuietSeconds) {
            p.open = false;
            p.failed = true;
            ++m_punchFailed;
            OKAY_INFO("net: the direct path to relay slot ", p.slot, " went quiet; back on the relay");
        } else if (p.probeTimer <= 0.0f) {
            p.probeTimer = p.open ? kKeepEverySeconds : kProbeEverySeconds;
            const auto probe = net::BuildRelayPunch(0, p.token, m_relaySlot);
            if (!net::HoldDatagram(m_socket, p.ep, probe.data(), probe.size())) m_socket.SendTo(p.ep, probe.data(), probe.size());
        }
        ++it;
    }
}

bool NetworkManager::DirectToHost() const {
    if (m_mode != Mode::Client) return false;
    if (!m_relay) return true;
    const DirectPath* d = m_serverEp.port == net::kRelaySlotPort ? DirectFor(m_serverEp.address) : nullptr;
    return d && d->open && !d->failed;
}

int NetworkManager::DirectPeers() const {
    if (m_mode == Mode::Client) return DirectToHost() ? 1 : 0;
    if (m_mode != Mode::Server) return 0;
    if (!m_relay) return (int)m_clients.size();
    int n = 0;
    for (const auto& [ep, c] : m_clients)
        if (const DirectPath* d = DirectFor(ep.address); d && d->open && !d->failed) ++n;
    return n;
}

int NetworkManager::RelayedPeers() const {
    if (!m_relay) return 0;
    if (m_mode == Mode::Client) return DirectToHost() ? 0 : 1;
    if (m_mode != Mode::Server) return 0;
    return (int)m_clients.size() - DirectPeers();
}

std::string NetworkManager::PathText() const {
    if (m_mode == Mode::Offline) return std::string();
    if (!m_relay) return "direct";
    bool trying = false;
    for (const DirectPath& d : m_direct) if (!d.open && !d.failed) trying = true;
    if (m_mode == Mode::Client) return DirectToHost() ? "direct (punched)" : trying ? "relayed (trying direct)" : "relayed";
    if (!m_clients.empty() && RelayedPeers() == 0) return "direct (punched)";
    return trying ? "relayed (trying direct)" : "relayed";
}

// ---- host migration through the relay ----------------------------------------------------------------------

bool NetworkManager::RelaySwitch(bool asHost) {
    if (!m_relay || !m_socket.IsOpen()) return false;
    // What the relay knows this peer by stays as it is: the socket (its address), slot, cookie and nonce.
    const net::Endpoint relayEp = m_relayEp;
    const std::string code = m_relayCode;
    const std::uint64_t nonce = m_relayNonce;
    std::uint8_t cookie[sizeof m_relayCookie];
    std::memcpy(cookie, m_relayCookie, sizeof cookie);
    const bool heard = m_relayHeard;
    m_keepRelay = true;
    Stop();
    m_keepRelay = false;
    m_relay = true;
    m_relayWelcomed = true;
    m_relayEp = relayEp;
    m_relayCode = code;
    m_relayNonce = nonce;
    std::memcpy(m_relayCookie, cookie, sizeof cookie);
    m_relayHeard = heard;
    m_relayError.clear();
    m_relayWait = 0.0f;
    m_relayHelloTimer = 0.0f;   // say the new role at once
    m_relayNewHost = 0;
    m_relaySuccessorKey = 0;
    m_kicked = false; m_kickReason.clear();
    if (asHost) {
        InitServerState();
        m_relayHostKey = m_mig.relayKey;   // the successor key: the relay checks it
        m_relayPromoting = true;
        m_relayPromoteWait = 0.0f;
        OKAY_INFO("net: taking the host's place in relay session '", code, "'");
    } else {
        m_relayHostKey = 0;
        m_relayPromoting = false;
        m_mode = Mode::Client;
        m_joined = false;
        m_serverSilence = 0.0f; m_joinWait = 0.0f;
        m_localUserId.clear();
        UseNextSessionKeys();
    }
    return true;
}

// ---- plain errors -------------------------------------------------------------------------------------------

std::string NetworkManager::RelayPlainError() const {
    const std::string where = m_relayEp.address ? m_relayEp.ToString() : std::string("the relay server");
    const std::string& why = m_relayError;
    if (why.find("is already hosted") != std::string::npos)
        return "Room code " + m_relayCode + " is already in use on the relay; pick another code.";
    if (why.find("protocol version") != std::string::npos)
        return "The relay server " + where + " does not speak this game's version: " + why + ".";
    if (!m_relayHeard && !m_relayWelcomed) {
        char wait[32];
        std::snprintf(wait, sizeof wait, "%.0f s", relayTimeoutSeconds);
        return "Could not reach the relay server " + where + " (no answer after " + wait +
               "). Is it running, and does this network let UDP through?";
    }
    if (m_mode == Mode::Client && !m_joined && !m_relayWelcomed)
        return "No game is hosted with code " + m_relayCode + " (check the code; the host may have stopped).";
    if (why.find("another player took over") != std::string::npos)
        return "Another player took over as the host of this online game.";
    return "The online game ended: " + (why.empty() ? std::string("the relay session closed") : why) + ".";
}

void NetworkManager::NoteRelayError() {
    if (m_errorNoted) return;
    SetError(RelayPlainError());
    m_errorNoted = true;
}

// ---- online play by room code -------------------------------------------------------------------------------

std::string NetworkManager::OnlineRelay() const {
    std::string r = relayServer;
    if (r.empty()) if (const char* e = std::getenv("OKAY_RELAY")) r = e;
#ifdef OKAY_DEFAULT_RELAY
    if (r.empty()) r = OKAY_DEFAULT_RELAY;
#endif
#ifdef OKAY_DEFAULT_VOICE_RELAY
    if (r.empty()) r = OKAY_DEFAULT_VOICE_RELAY;
#endif
    while (!r.empty() && std::isspace((unsigned char)r.back())) r.pop_back();
    while (!r.empty() && std::isspace((unsigned char)r.front())) r.erase(r.begin());
    if (GitHubHost(r)) return std::string();
    return r;
}

std::string NetworkManager::MakeRoomCode() {
    std::random_device rd;
    std::string c;
    for (int i = 0; i < 6; ++i) c += kCodeLetters[rd() % (sizeof kCodeLetters - 1)];
    return c;
}

std::string NetworkManager::NormalizeRoomCode(const std::string& code) {
    std::string c;
    for (char ch : code) {
        if (ch == ' ' || ch == '-' || ch == '\t') continue;
        if (!std::isalnum((unsigned char)ch)) return std::string();
        c += (char)std::toupper((unsigned char)ch);
    }
    return c.size() >= 4 && c.size() <= 12 ? c : std::string();
}

bool NetworkManager::HostOnline(const std::string& code) {
    m_lastError.clear(); m_errorNoted = false;
    const std::string relay = OnlineRelay();
    if (relay.empty()) {
        SetError("Online play needs a relay server: set the Network Manager's Relay Server (host:port), or OKAY_RELAY.");
        return false;
    }
    std::string h; std::uint16_t port = 0;
    if (!SplitHostPort(relay, h, port, 45100)) { SetError("The relay server \"" + relay + "\" is not a host:port address."); return false; }
    const std::string c = code.empty() ? MakeRoomCode() : NormalizeRoomCode(code);
    if (c.empty()) {
        SetError("\"" + code + "\" is not a room code: use 4 to 12 letters and digits, like K7Q29X.");
        return false;
    }
    if (!HostViaRelay(h, port, c)) return false;
    m_onlineMadeCode = code.empty();
    m_onlineCodeTries = 0;
    OKAY_INFO("net: hosting online with room code ", c);
    return true;
}

bool NetworkManager::JoinCode(const std::string& code) {
    m_lastError.clear(); m_errorNoted = false;
    const std::string c = NormalizeRoomCode(code);
    if (c.empty()) {
        SetError("\"" + code + "\" is not a room code: use 4 to 12 letters and digits, like K7Q29X.");
        return false;
    }
    const std::string relay = OnlineRelay();
    if (relay.empty()) {
        SetError("Online play needs a relay server: set the Network Manager's Relay Server (host:port), or OKAY_RELAY.");
        return false;
    }
    JoinLink l;   // the same path a join link from the launcher or an invite takes
    l.relay = relay;
    l.code = c;
    if (!JoinByLink(l.Format())) {
        if (m_lastError.empty()) SetError("Could not join the online game with code " + c + ".");
        return false;
    }
    m_onlineMadeCode = false;
    return true;
}

std::string NetworkManager::OnlineJoinLink() const {
    if (!m_relay) return std::string();
    JoinLink l;
    l.game = serverName;
    l.relay = m_relayEp.ToString();
    l.code = m_relayCode;
    l.needsPassword = !password.empty();
    return l.Format();
}

// The relay refused a made-up room code (someone has it): try another. True if a new try started.
bool NetworkManager::RetryOnlineCode() {
    if (!m_onlineMadeCode || m_relayError.find("is already hosted") == std::string::npos || ++m_onlineCodeTries > kMaxCodeTries)
        return false;
    const std::string h = m_relayEp.ToString();
    const std::uint16_t port = m_relayEp.port;
    const std::string ip = h.substr(0, h.find(':'));
    const int tries = m_onlineCodeTries;
    const std::string c = MakeRoomCode();
    OKAY_INFO("net: room code ", m_relayCode, " is taken; trying ", c);
    if (!HostViaRelay(ip, port, c)) return false;
    m_onlineMadeCode = true;
    m_onlineCodeTries = tries;
    return true;
}

} // namespace okay
