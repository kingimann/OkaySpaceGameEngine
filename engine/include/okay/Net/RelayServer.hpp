#pragma once
#include "okay/Net/RelayProtocol.hpp"
#include "okay/Net/UdpSocket.hpp"
#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace okay::net {

/// A small TURN-style relay so two peers behind NATs can play together without
/// either one accepting an inbound connection or configuring port-forwarding.
/// Both peers connect *out* to this relay (NATs allow that) and announce a shared
/// session code; the relay pairs them and forwards datagrams between them by a
/// per-peer "slot" id, rewriting the slot so each side sees a stable sender.
///
/// It is deliberately game-agnostic: it never parses the inner message bytes, so
/// the engine's encryption and reliability still run end-to-end through it.
///
/// It is also exposed to the internet, so it trusts nothing:
///  * data is forwarded only between a session's host and its other peers (never
///    peer to peer, never across sessions);
///  * a version-2 host is given a secret key in its welcome, and while the
///    session lives only a hello carrying that key can host the code (the host
///    may move to a new address with it; nobody else can take the code over).
///    Version-1 sessions (older games, the launcher's voice calls, where every
///    member announces the host role) keep the old rule: when the host goes,
///    the next host-role hello takes over;
///  * when the host goes, the session's other peers are told (a welcome with
///    host slot 0), so a game's clients disconnect instead of waiting for
///    whoever hosts next;
///  * a new peer's version-3 hello gets only a cookie (an HMAC of its address
///    and the minute under a per-process secret); a slot is allocated only
///    for a hello that echoes it, so a spoofed source can't fill anyone's
///    quota. Version-1 and -2 hellos (no cookie) are still accepted while
///    legacy hellos are on (the default for now), and counted in the log;
///  * hellos from unknown senders are rate limited per address;
///  * each peer's forwarded packets and bytes per second are capped, and so is
///    what each peer receives in total from its session;
///  * slots are random (not guessable), and peers, sessions and code lengths
///    are capped, per source address too, so one machine can't take every slot
///    and lock everyone else out. Drive
/// it by calling Poll() in a loop (the standalone `okayspace-relay` binary does
/// exactly that); tests pump it in-process alongside the peers.
class RelayServer {
public:
    static constexpr std::size_t kMaxPeers = 4096;          ///< peers across all sessions
    static constexpr std::size_t kMaxSessions = 2048;
    static constexpr std::uint32_t kMaxPeersPerSession = 32;
    /// Peers one IP address may hold at once (a LAN party or a shared NAT fits;
    /// a flood from one machine stops here, far below the global caps).
    static constexpr std::uint32_t kMaxPeersPerAddress = 64;
    static constexpr std::size_t kMaxCodeLength = 64;       ///< session code bytes
    /// Forwarding caps per peer: packets and bytes per second, with twice that
    /// as a burst. A host's caps are multiplied by its session's client count,
    /// since it sends to each of them. Far above what a game sends (a client
    /// sends its state once a frame), far below a flood.
    static constexpr float kPacketsPerSecond = 1000.0f;
    static constexpr float kBytesPerSecond = 1024.0f * 1024.0f;
    /// Bytes per second one peer (a host, above all) receives from its whole
    /// session, with one second of it as a burst.
    static constexpr float kInboundBytesPerSecond = 4.0f * 1024.0f * 1024.0f;
    /// Hellos per second from one IP address that isn't a registered peer yet
    /// (twice that as a burst). Loopback is exempt: it can't be spoofed from
    /// outside, and a local test or LAN tool may start many peers at once.
    static constexpr float kHellosPerSecond = 10.0f;
    static constexpr std::size_t kMaxHelloTrackers = 65536;   ///< addresses rate-tracked at once

    RelayServer();
    /// Accept version-1 and -2 hellos, which carry no cookie (on by default:
    /// older games and the launcher's voice calls speak them). Off, they're
    /// refused with RelayRefusedVersion.
    void SetLegacyHello(bool on) { m_legacyHello = on; }
    bool LegacyHello() const { return m_legacyHello; }

    /// Bind the relay to a UDP port (0 picks an ephemeral one). Returns false if
    /// the platform socket can't be opened/bound.
    bool Start(std::uint16_t port);
    void Stop();
    bool IsRunning() const { return m_socket.IsOpen(); }
    /// The port actually bound (useful after Start(0)).
    std::uint16_t Port() const { return m_socket.LocalPort(); }

    /// Process all pending datagrams and expire peers that have gone silent for
    /// longer than `timeout` seconds. Call it often; `dt` is seconds since the
    /// last call (used only for the idle timeout).
    void Poll(float dt = 0.0f, float timeout = 30.0f);

    /// Process one datagram as if it arrived from `from` (Poll() calls this;
    /// tests use it to feed crafted or random datagrams).
    void HandleDatagram(const Endpoint& from, const std::uint8_t* data, std::size_t len);
    /// Block until a datagram arrives or `timeoutMs` passes (the standalone relay
    /// waits here instead of sleeping between polls).
    bool Wait(int timeoutMs) { return m_socket.WaitReadable(timeoutMs); }

    /// Diagnostics for tests / a status line.
    std::size_t SessionCount() const { return m_sessions.size(); }
    std::size_t PeerCount() const { return m_peers.size(); }
    /// The slot a peer was given (0 if `ep` isn't registered).
    std::uint32_t SlotOf(const Endpoint& ep) const {
        auto it = m_peers.find(ep);
        return it != m_peers.end() ? it->second.slot : 0;
    }
    /// Datagrams dropped by the forwarding rules and caps (for tests / status).
    std::uint64_t DroppedRoute() const { return m_droppedRoute; }   ///< not client<->host
    std::uint64_t DroppedRate() const { return m_droppedRate; }     ///< over a peer's caps
    std::uint64_t RefusedHellos() const { return m_refusedHellos; }
    std::uint64_t CookiesSent() const { return m_cookiesSent; }
    std::uint64_t LegacyHellos() const { return m_legacyHellos; }      ///< v1/v2 hellos from new senders
    std::uint64_t DroppedHellos() const { return m_droppedHellos; }    ///< over an address's hello rate

private:
    struct Peer {
        std::uint32_t slot = 0;
        std::string   code;
        bool          host = false;     // announced the host role (the session's host is Session::hostSlot)
        std::uint8_t  version = 1;      // relay protocol version it spoke
        float         idle = 0.0f;
        float         packets = 0.0f;   // forwarding allowance (token buckets)
        float         bytes = 0.0f;
        double        refilled = 0.0;   // m_now when the buckets were last topped up
        float         inBytes = 0.0f;   // what it may still receive from its session
        double        inRefilled = 0.0;
    };
    struct HelloBucket { float tokens = 0.0f; double refilled = 0.0; };
    using Cookie = std::array<std::uint8_t, kRelayCookieBytes>;
    struct Session {
        std::uint32_t hostSlot = 0;     // 0 while no host is registered
        std::uint32_t peers = 0;        // registered peers with this code
        std::uint64_t hostKey = 0;      // set when a version-2 host claims the code: protected
        std::uint32_t lastHostSlot = 0; // the slot a returning host gets back
    };
    using PeerMap = std::unordered_map<Endpoint, Peer, EndpointHash>;

    Peer* FindBySlot(std::uint32_t slot);
    void  SendWelcome(const Endpoint& to, std::uint32_t yourSlot, std::uint32_t hostSlot,
                      std::uint64_t hostKey = 0);
    void  SendRefused(const Endpoint& to, std::uint8_t reason);
    void  HandleHello(const Endpoint& from, const std::uint8_t* data, std::size_t len);
    void  HandleData(const Endpoint& from, const std::uint8_t* data, std::size_t len);
    bool  WithinRate(Peer& peer, std::size_t len);
    bool  WithinInbound(Peer& peer, std::size_t len);
    bool  HelloAllowed(std::uint32_t address);
    void  PruneHelloRate();
    Cookie MakeCookie(const Endpoint& from, std::uint64_t minute) const;
    bool  CookieValid(const Endpoint& from, const Cookie& c) const;
    void  SendCookie(const Endpoint& to);
    void  ClaimHost(const Endpoint& from, Peer& peer, Session& s);
    std::uint32_t NewSlot();
    std::uint64_t NewHostKey();
    PeerMap::iterator RemovePeer(PeerMap::iterator it);

    UdpSocket m_socket;
    std::mt19937 m_rng{std::random_device{}()};
    std::vector<std::uint8_t> m_out;   // reused forward buffer
    std::unordered_map<Endpoint, Peer, EndpointHash> m_peers;   // by real address
    std::unordered_map<std::uint32_t, Endpoint>      m_slotEp;  // slot -> real address
    std::unordered_map<std::string, Session>         m_sessions;// by code
    std::unordered_map<std::uint32_t, std::uint32_t> m_perAddress;   // IP -> registered peers
    std::unordered_map<std::uint32_t, HelloBucket>   m_helloRate;    // IP -> hellos it may still send
    std::string m_cookieSecret;   // random per process: cookies don't outlive a restart
    bool m_legacyHello = true;
    double m_now = 0.0;   // seconds of Poll() time, for the rate caps
    std::uint64_t m_droppedRoute = 0, m_droppedRate = 0, m_refusedHellos = 0;
    std::uint64_t m_cookiesSent = 0, m_legacyHellos = 0, m_droppedHellos = 0;
    std::uint64_t m_legacyLogged = 0;   // m_legacyHellos at the last log line
    double m_legacyLogAt = 60.0, m_helloPruneAt = 10.0;   // m_now of the next legacy count / rate prune
    float m_logBudget = 20.0f;   // join log lines allowed right now (refills 5/s), so a flood can't spam the log
    std::uint32_t m_quietJoins = 0;   // joins not logged since the last line
};

} // namespace okay::net
