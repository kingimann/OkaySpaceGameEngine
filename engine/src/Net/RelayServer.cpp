#include "okay/Net/RelayServer.hpp"
#include "okay/Net/RelayProtocol.hpp"
#include "okay/Net/Packet.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Net/SecureChannel.hpp"
#include "AntiCheatChecks.hpp"   // shared/: okayac::detail::HmacSha256
#include <cmath>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

namespace okay::net {

RelayServer::RelayServer() {
    std::uint8_t secret[32];
    for (std::size_t i = 0; i < sizeof secret; i += 8) {
        const std::uint64_t k = NewHostKey();
        std::memcpy(secret + i, &k, 8);
    }
    m_cookieSecret.assign(reinterpret_cast<const char*>(secret), sizeof secret);
}

bool RelayServer::Start(std::uint16_t port) {
    Stop();
    if (!Startup()) { OKAY_ERROR("relay: net startup failed"); return false; }
    if (!m_socket.Open() || !m_socket.Bind(port)) {
        OKAY_ERROR("relay: cannot bind UDP port ", port);
        m_socket.Close();
        return false;
    }
    OKAY_INFO("relay: listening on UDP ", m_socket.LocalPort());
    return true;
}

void RelayServer::Stop() {
    m_socket.Close();
    m_peers.clear();
    m_slotEp.clear();
    m_sessions.clear();
    m_perAddress.clear();
    m_helloRate.clear();
}

RelayServer::Peer* RelayServer::FindBySlot(std::uint32_t slot) {
    auto it = m_slotEp.find(slot);
    if (it == m_slotEp.end()) return nullptr;
    auto p = m_peers.find(it->second);
    return p != m_peers.end() ? &p->second : nullptr;
}

namespace {
void WriteKey(Packet& p, std::uint64_t key) {
    p.Write(static_cast<std::uint32_t>(key));
    p.Write(static_cast<std::uint32_t>(key >> 32));
}
} // namespace

void RelayServer::SendWelcome(const Endpoint& to, std::uint32_t yourSlot, std::uint32_t hostSlot,
                              std::uint64_t hostKey) {
    Packet w(RelayWelcome);
    w.Write(yourSlot);
    w.Write(hostSlot);
    w.Write(kRelayProtocolVersion);   // version-1 peers stop reading before this
    WriteKey(w, hostKey);
    m_socket.SendTo(to, w.Data(), w.Size());
}

void RelayServer::SendRefused(const Endpoint& to, std::uint8_t reason) {
    ++m_refusedHellos;
    Packet r(RelayRefused);
    r.Write(reason);
    r.Write(kRelayProtocolVersion);
    m_socket.SendTo(to, r.Data(), r.Size());
}

// The cookie for an address in a given minute: only the relay can make it, and
// only the machine at that address receives it.
RelayServer::Cookie RelayServer::MakeCookie(const Endpoint& from, std::uint64_t minute) const {
    const auto mac = okayac::detail::HmacSha256(
        m_cookieSecret, "okay-relay-cookie|" + std::to_string(from.address) + "|" +
                        std::to_string(from.port) + "|" + std::to_string(minute));
    Cookie c;
    std::memcpy(c.data(), mac.data(), c.size());
    return c;
}

// This minute's or the last one's, so a cookie sent just before the minute
// turns still works.
bool RelayServer::CookieValid(const Endpoint& from, const Cookie& c) const {
    const std::uint64_t minute = static_cast<std::uint64_t>(m_now / 60.0);
    auto same = [&](const Cookie& want) {
        std::uint8_t diff = 0;
        for (std::size_t i = 0; i < c.size(); ++i) diff |= static_cast<std::uint8_t>(c[i] ^ want[i]);
        return diff == 0;
    };
    return same(MakeCookie(from, minute)) || (minute > 0 && same(MakeCookie(from, minute - 1)));
}

void RelayServer::SendCookie(const Endpoint& to) {
    ++m_cookiesSent;
    Packet c(RelayCookie);
    c.Write(kRelayProtocolVersion);
    for (std::uint8_t b : MakeCookie(to, static_cast<std::uint64_t>(m_now / 60.0))) c.Write(b);
    m_socket.SendTo(to, c.Data(), c.Size());
}

// One hello from an address that isn't a registered peer: within its rate?
bool RelayServer::HelloAllowed(std::uint32_t address) {
    if ((address >> 24) == 127) return true;
    auto it = m_helloRate.find(address);
    if (it == m_helloRate.end()) {
        if (m_helloRate.size() >= kMaxHelloTrackers) {
            PruneHelloRate();
            if (m_helloRate.size() >= kMaxHelloTrackers) { ++m_droppedHellos; return false; }
        }
        it = m_helloRate.emplace(address, HelloBucket{2.0f * kHellosPerSecond, m_now}).first;
    }
    HelloBucket& b = it->second;
    b.tokens = std::fmin(2.0f * kHellosPerSecond, b.tokens + static_cast<float>(m_now - b.refilled) * kHellosPerSecond);
    b.refilled = m_now;
    if (b.tokens < 1.0f) { ++m_droppedHellos; return false; }
    b.tokens -= 1.0f;
    return true;
}

// Forget addresses whose bucket has filled up again (they're back to the burst).
void RelayServer::PruneHelloRate() {
    const double full = 2.0;   // seconds for an empty bucket to refill
    for (auto it = m_helloRate.begin(); it != m_helloRate.end(); )
        it = m_now - it->second.refilled >= full ? m_helloRate.erase(it) : std::next(it);
}

// A random, unused, non-zero slot: a peer can't guess another session's slots.
std::uint32_t RelayServer::NewSlot() {
    for (;;) {
        std::uint32_t s = static_cast<std::uint32_t>(m_rng());
        if (s != 0 && m_slotEp.find(s) == m_slotEp.end()) return s;
    }
}

// The host's secret. Slots are visible to every peer of a session, so this
// doesn't come from the slot generator.
std::uint64_t RelayServer::NewHostKey() {
    std::uint64_t k = 0;
    while (k == 0) {
#ifdef OKAY_HAVE_SODIUM
        if (SecureChannel::Init()) { randombytes_buf(&k, sizeof k); continue; }
#endif
        std::random_device rd;
        k = (static_cast<std::uint64_t>(rd()) << 32) ^ rd();
    }
    return k;
}

// Forget a peer; a session goes when its last peer does. When the host goes,
// the rest are told (a welcome with host slot 0): a game's clients disconnect.
// A protected session keeps its host key, so only that host can claim the code
// again; in a version-1 session the next host-role hello takes over.
RelayServer::PeerMap::iterator RelayServer::RemovePeer(PeerMap::iterator it) {
    const std::uint32_t slot = it->second.slot;
    m_slotEp.erase(slot);
    auto pa = m_perAddress.find(it->first.address);
    if (pa != m_perAddress.end() && --pa->second == 0) m_perAddress.erase(pa);
    auto s = m_sessions.find(it->second.code);
    if (s != m_sessions.end()) {
        if (s->second.hostSlot == slot) {
            s->second.hostSlot = 0;
            s->second.lastHostSlot = slot;
            for (auto& [ep, other] : m_peers)
                if (other.code == it->second.code && other.slot != slot)
                    SendWelcome(ep, other.slot, 0);
        }
        if (s->second.peers > 0) --s->second.peers;
        if (s->second.peers == 0) m_sessions.erase(s);
    }
    return m_peers.erase(it);
}

// Top up a peer's token buckets and spend one datagram of `len` bytes. A host
// sends to each client, so its rate scales with the session's client count.
// What a peer receives from its whole session, however many senders: a host's
// clients can't add up to a flood of it.
bool RelayServer::WithinInbound(Peer& peer, std::size_t len) {
    peer.inBytes = std::fmin(kInboundBytesPerSecond,
                             peer.inBytes + static_cast<float>(m_now - peer.inRefilled) * kInboundBytesPerSecond);
    peer.inRefilled = m_now;
    if (peer.inBytes < static_cast<float>(len)) return false;
    peer.inBytes -= static_cast<float>(len);
    return true;
}

bool RelayServer::WithinRate(Peer& peer, std::size_t len) {
    float scale = 1.0f;
    auto s = m_sessions.find(peer.code);
    if (s != m_sessions.end() && s->second.hostSlot == peer.slot && s->second.peers > 2)
        scale = static_cast<float>(s->second.peers - 1);
    const float elapsed = static_cast<float>(m_now - peer.refilled);
    peer.refilled = m_now;
    peer.packets = std::fmin(2.0f * kPacketsPerSecond * scale, peer.packets + elapsed * kPacketsPerSecond * scale);
    peer.bytes = std::fmin(2.0f * kBytesPerSecond * scale, peer.bytes + elapsed * kBytesPerSecond * scale);
    if (peer.packets < 1.0f || peer.bytes < static_cast<float>(len)) return false;
    peer.packets -= 1.0f;
    peer.bytes -= static_cast<float>(len);
    return true;
}

void RelayServer::HandleHello(const Endpoint& from, const std::uint8_t* data, std::size_t len) {
    Packet p(data, len);
    p.ReadU8();
    const std::uint8_t role = p.ReadU8();
    const std::string code = p.ReadString();
    if (!p.Ok() || code.empty() || code.size() > kMaxCodeLength) return;
    if (role != RelayHost && role != RelayClient) return;
    // Version 2 appends [u8 version][u64 host key]; version 1 ends at the code.
    // Version 3 adds the cookie and pads the hello.
    std::uint8_t version = 1;
    std::uint64_t key = 0;
    {
        const std::uint8_t v = p.ReadU8();
        const std::uint32_t lo = p.ReadU32(), hi = p.ReadU32();
        if (p.Ok()) { version = v; key = (static_cast<std::uint64_t>(hi) << 32) | lo; }
    }
    Cookie cookie{};
    if (version >= 3) {
        if (len < kRelayHelloMinBytes) return;
        for (auto& b : cookie) b = p.ReadU8();
        if (!p.Ok()) return;
    }
    if (version < kRelayMinProtocolVersion) { SendRefused(from, RelayRefusedVersion); return; }

    // Re-hellos from the same address keep the slot, session and role they
    // started with, so a NAT-keepalive hello is idempotent.
    if (auto existing = m_peers.find(from); existing != m_peers.end()) {
        Peer& peer = existing->second;
        peer.idle = 0.0f;
        peer.version = version;
        auto sit = m_sessions.find(peer.code);
        if (sit == m_sessions.end()) return;
        Session& s = sit->second;
        // A host-role peer waiting in a session with no host takes over, as
        // version 1 did (the launcher's voice calls hand their hub on this way);
        // in a protected session, only with the key.
        if (peer.host && s.hostSlot == 0 && (s.hostKey == 0 || key == s.hostKey)) {
            ClaimHost(from, peer, s);
            return;
        }
        if (peer.slot == s.hostSlot) SendWelcome(from, peer.slot, s.hostSlot, s.hostKey);
        else if (peer.host || s.hostSlot != 0) SendWelcome(from, peer.slot, s.hostSlot);
        // else: a client with no host yet keeps re-helloing until one registers.
        return;
    }

    // A new sender. Nothing is allocated until it proves it receives at its
    // address: a version-3 hello without this minute's cookie only gets one.
    if (!HelloAllowed(from.address)) return;
    if (version >= 3) {
        if (!CookieValid(from, cookie)) { SendCookie(from); return; }
    } else {
        ++m_legacyHellos;
        if (!m_legacyHello) { SendRefused(from, RelayRefusedVersion); return; }
    }

    const bool asHost = (role == RelayHost);
    auto sit = m_sessions.find(code);
    bool reclaim = false;
    if (asHost && sit != m_sessions.end() && sit->second.hostKey != 0) {
        // A protected session (a version-2 host claimed it): only its key may
        // host it again.
        Session& s = sit->second;
        if (key != s.hostKey) { SendRefused(from, RelayRefusedHosted); return; }
        if (s.hostSlot != 0) {
            // The host moved to a new address (its NAT rebound): it keeps its
            // slot, so its clients see no change.
            auto slotEp = m_slotEp.find(s.hostSlot);
            if (slotEp == m_slotEp.end()) return;
            auto pa = m_perAddress.find(from.address);
            if (from.address != slotEp->second.address && pa != m_perAddress.end() &&
                pa->second >= kMaxPeersPerAddress) return;
            auto old = m_peers.find(slotEp->second);
            if (old == m_peers.end()) return;
            Peer moved = old->second;
            moved.idle = 0.0f;
            moved.version = version;
            auto opa = m_perAddress.find(old->first.address);
            if (opa != m_perAddress.end() && --opa->second == 0) m_perAddress.erase(opa);
            m_peers.erase(old);
            m_perAddress[from.address]++;
            slotEp->second = from;
            m_peers.emplace(from, moved);
            SendWelcome(from, moved.slot, s.hostSlot, s.hostKey);
            return;
        }
        reclaim = true;   // the host left and is back: same slot if it's free
    } else if (asHost && version >= 2 && sit != m_sessions.end() && sit->second.hostSlot != 0) {
        // A version-2 host doesn't join someone else's version-1 session.
        SendRefused(from, RelayRefusedHosted);
        return;
    }

    // A new peer: only within the limits.
    if (m_peers.size() >= kMaxPeers) return;
    auto pa = m_perAddress.find(from.address);
    if (pa != m_perAddress.end() && pa->second >= kMaxPeersPerAddress) return;
    if (sit == m_sessions.end() && m_sessions.size() >= kMaxSessions) return;
    if (sit != m_sessions.end() && sit->second.peers >= kMaxPeersPerSession) return;
    Peer peer;
    peer.slot = (reclaim && sit->second.lastHostSlot != 0 && !m_slotEp.count(sit->second.lastHostSlot))
                    ? sit->second.lastHostSlot : NewSlot();
    peer.code = code;
    peer.host = asHost;
    peer.version = version;
    peer.packets = 2.0f * kPacketsPerSecond;
    peer.bytes = 2.0f * kBytesPerSecond;
    peer.refilled = m_now;
    peer.inBytes = kInboundBytesPerSecond;
    peer.inRefilled = m_now;
    m_slotEp[peer.slot] = from;
    Session& s = m_sessions[code];
    s.peers++;
    m_perAddress[from.address]++;
    Peer& added = m_peers.emplace(from, peer).first->second;
    if (m_logBudget >= 1.0f) {
        m_logBudget -= 1.0f;
        OKAY_INFO("relay: peer slot ", peer.slot, peer.host ? " (host)" : " (client)",
                  " joined session '", code, "'",
                  m_quietJoins ? " (+" + std::to_string(m_quietJoins) + " more not logged)" : std::string());
        m_quietJoins = 0;
    } else {
        ++m_quietJoins;
    }
    if (asHost && s.hostSlot == 0) ClaimHost(from, added, s);
    else if (s.hostSlot != 0 || asHost) SendWelcome(from, peer.slot, s.hostSlot);   // pair now
}

// `peer` becomes its session's host: a version-2 host gets the code's key (the
// session is protected from then on); it's welcomed, and so is every other
// peer waiting in the session.
void RelayServer::ClaimHost(const Endpoint& from, Peer& peer, Session& s) {
    if (s.hostKey == 0 && peer.version >= 2) s.hostKey = NewHostKey();
    s.hostSlot = peer.slot;
    SendWelcome(from, peer.slot, s.hostSlot, s.hostKey);
    for (auto& [ep, other] : m_peers)
        if (other.code == peer.code && other.slot != peer.slot)
            SendWelcome(ep, other.slot, s.hostSlot);
}

void RelayServer::HandleData(const Endpoint& from, const std::uint8_t* data, std::size_t len) {
    Packet p(data, len);
    p.ReadU8();
    std::uint32_t destSlot = p.ReadU32();
    if (!p.Ok()) return;
    auto sender = m_peers.find(from);
    if (sender == m_peers.end()) return;           // unknown sender: ignore
    sender->second.idle = 0.0f;
    auto destEp = m_slotEp.find(destSlot);
    if (destEp == m_slotEp.end()) return;          // dest gone: drop
    auto dest = m_peers.find(destEp->second);
    // Only between a session's host and its clients: a peer can't inject packets
    // into someone else's game by naming their slot, and a client can't reach
    // another client (everything between players goes through the host, which
    // checks it).
    auto session = m_sessions.find(sender->second.code);
    const std::uint32_t hostSlot = session != m_sessions.end() ? session->second.hostSlot : 0;
    if (dest == m_peers.end() || dest->second.code != sender->second.code || hostSlot == 0 ||
        (sender->second.slot == hostSlot) == (destSlot == hostSlot)) { ++m_droppedRoute; return; }
    if (!WithinRate(sender->second, len) || !WithinInbound(dest->second, len)) { ++m_droppedRate; return; }
    // Rewrite the slot field from dest to the *sender's* slot, then forward
    // the untouched payload.
    const std::size_t payloadOff = 1 + 4;          // tag + destSlot
    const std::uint32_t src = sender->second.slot;
    m_out.resize(len);
    m_out[0] = RelayData;
    std::memcpy(m_out.data() + 1, &src, sizeof(src));   // Packet's layout: native u32
    if (len > payloadOff) std::memcpy(m_out.data() + payloadOff, data + payloadOff, len - payloadOff);
    m_socket.SendTo(destEp->second, m_out.data(), len);
}

void RelayServer::HandleDatagram(const Endpoint& from, const std::uint8_t* data, std::size_t len) {
    if (len < 1) return;
    const std::uint8_t tag = data[0];
    if (tag == RelayHello) {
        HandleHello(from, data, len);
    } else if (tag == RelayData) {
        HandleData(from, data, len);
    } else if (tag == RelayBye) {
        auto it = m_peers.find(from);
        if (it != m_peers.end()) RemovePeer(it);
    }
}

void RelayServer::Poll(float dt, float timeout) {
    if (!m_socket.IsOpen()) return;
    m_now += dt;
    static thread_local std::vector<std::uint8_t> buf(kMaxDatagram);
    Endpoint from;
    int n;
    while ((n = m_socket.RecvFrom(buf.data(), buf.size(), from)) > 0)
        HandleDatagram(from, buf.data(), static_cast<std::size_t>(n));

    m_logBudget = std::fmin(20.0f, m_logBudget + 5.0f * dt);
    if (m_now >= m_helloPruneAt) { m_helloPruneAt = m_now + 10.0; PruneHelloRate(); }
    // How many peers still speak version 1 or 2, so legacy hellos can be
    // turned off once there are none.
    if (m_now >= m_legacyLogAt) {
        m_legacyLogAt = m_now + 60.0;
        if (m_legacyHellos != m_legacyLogged)
            OKAY_INFO("relay: ", m_legacyHellos - m_legacyLogged, " legacy (version 1/2) hellos in the last minute",
                      m_legacyHello ? "" : ", refused");
        m_legacyLogged = m_legacyHellos;
    }
    // Expire peers we haven't heard from in a while (NAT mapping likely gone).
    for (auto it = m_peers.begin(); it != m_peers.end(); ) {
        it->second.idle += dt;
        if (it->second.idle > timeout) it = RemovePeer(it);
        else ++it;
    }
}

} // namespace okay::net
