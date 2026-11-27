// NetworkManager, part: the server's per-frame tick (ServerTick).
// Moved unchanged from NetworkManager.cpp (docs/engine/modularization_plan.md, batch N1).
// The class is declared in okay/Net/NetworkManager.hpp; file-local helpers shared by the parts are in
// NetworkManagerInternal.hpp.
#include "okay/Net/NetworkManager.hpp"
#include "okay/Net/NetSim.hpp"
#include "okay/Net/SessionTicket.hpp"
#include "okay/AntiCheat/AntiCheatCrypto.hpp"   // ed25519 for the host identity (#269)
#include "OfficialGame.hpp"      // shared/: SafeRelPath / ResolveInside for network prefab paths
#include "okay/Components/NetworkPlayerSpawner.hpp"
#include <cstdio>
#include <cstring>
#include <ctime>
#include <unordered_set>
#include "okay/Net/Packet.hpp"
#include "okay/Net/RelayProtocol.hpp"
#include "okay/Net/RelayHello.hpp"
#include "okay/Net/ServerQuery.hpp"
#include "okay/Net/JoinLink.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Components/SpriteRenderer.hpp"
#include "okay/Components/NetworkSync.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Physics/Physics2D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Net/NetBuildSync.hpp"
#include "okay/Net/NetQuestSync.hpp"
#include <algorithm>
#include <cmath>
#include <random>
#include <iomanip>
#include <sstream>
#include "NetworkManagerInternal.hpp"

namespace okay {

void NetworkManager::ServerTick(float dt) {
    if (!m_protectionsLogged) {   // on the first tick: the scene's AntiCheatConfig has applied by then
        m_protectionsLogged = true;
        std::string off;
        auto add = [&off](const char* what) { off += off.empty() ? what : std::string(", ") + what; };
        if (!anticheat.settings.enabled) add("anti-cheat");
        if (!encryption && !(anticheat.settings.enabled && anticheat.settings.requireEncryption)) add("encryption (players are known by address and a session cookie)");
        if (maxMoveSpeed <= 0.0f && !authoritativeMovement) add("move check");
        if (maxMessagesPerSecond <= 0) add("message rate limit");
        if (!off.empty()) OKAY_WARN("net: protections off: ", off);
    }
    m_serverTime += dt;
    m_acClockMs += static_cast<double>(dt) * 1000.0;
    ++m_tickCount;
    if (anticheat.settings.enabled) anticheat.Tick(dt);
    // The detection engine's kick / ban request for a player: acted on here too, once a tick. Before, only some
    // message paths (inputs, RPCs, hit claims, SyncVars) noticed it, so a player who only sent positions (State
    // packets) could reach the kick rung and stay in the game.
    if (anticheat.DetectOn()) {
        std::vector<std::uint32_t> due;
        for (const auto& [ep, c] : m_clients) if (anticheat.detector.KickRequested(c.id)) due.push_back(c.id);
        for (std::uint32_t id : due) AntiCheatAfterRefusal(id);
    }
    // Rate limit: true if this client may send another custom message this second.
    auto rateOk = [this](Client& c, std::size_t bytes) {
        if (maxMessagesPerSecond > 0 && ++c.msgCount > maxMessagesPerSecond) return false;
        return maxRelayedBytes <= 0 || (c.msgBytes += bytes) <= 4 * (std::size_t)maxRelayedBytes;
    };
    // A full-size receive buffer: a UDP datagram can be up to ~64 KB, so a small
    // fixed buffer silently truncated any larger message (e.g. a big reliable
    // payload). Reused across ticks (thread-local) to avoid per-frame allocation.
    RelayKeepAlive(dt);
    PruneFragments(dt);
    VarResendTick(dt);   // shared variable changes not acknowledged yet
    static thread_local std::vector<std::uint8_t> buf(net::kMaxDatagram);
    net::Endpoint from;
    int n;
    while ((n = ReceiveFrom(buf.data(), buf.size(), from)) > 0) {
        const std::uint8_t* data = buf.data();
        std::size_t len = static_cast<std::size_t>(n);
        std::vector<std::uint8_t> assembled;   // holds the message if this was a fragment
        if (len >= 1 && data[0] == FragAck) { OnFragAck(from, data, len); continue; }   // which pieces a client has
        if (len >= 1 && data[0] == Fragment) {
            if (!TakeFragment(from, data, len, assembled)) continue;  // wait for the rest
            data = assembled.data(); len = assembled.size();
        }
        std::vector<std::uint8_t> decrypted;   // holds the message if it was encrypted
        const bool sealed = len >= 1 && data[0] == kEncEnvelope;
        if (sealed) {
            net::SecureChannel* s = SessionFor(from);
            const std::uint64_t replaysBefore = s ? s->Replays() : 0;
            if (!s || !s->Open(data + 1, len - 1, decrypted)) {   // can't open (or a replay) -> drop
                if (s && anticheat.DetectOn()) {
                    if (auto cl = m_clients.find(from); cl != m_clients.end()) {   // evidence, from a joined peer's address
                        if (s->Replays() > replaysBefore) anticheat.detector.OnReplay(cl->second.id, anticheat.Clock());
                        else anticheat.detector.OnMalformed(cl->second.id, "packet would not open", anticheat.Clock());
                    }
                }
                continue;
            }
            data = decrypted.data(); len = decrypted.size();
        }
        net::Packet p(data, len);
        std::uint8_t type = p.ReadU8();
        // Anti-cheat P2: once encryption is required, a joined client's game
        // traffic must be sealed; plaintext from its address is someone else's.
        if (!sealed && anticheat.settings.enabled && anticheat.settings.requireEncryption &&
            type != Join && type != Query && type != PlayerQuery && m_clients.count(from))
            continue;
        // The anti-cheat's packet ceiling: any packet type from a joined client counts (State, inputs, pings,
        // acks and the rest have no limit of their own), so a flood of one cheap type can't eat the server's time.
        const int pktCeiling = anticheat.settings.maxPacketsPerSecond < 0 ? (dedicated ? 600 : 0) : anticheat.settings.maxPacketsPerSecond;
        if (anticheat.settings.enabled && pktCeiling > 0 && type != Join && type != JoinHello &&
            type != Query && type != PlayerQuery) {
            if (auto pc = m_clients.find(from); pc != m_clients.end()) {
                Client& c = pc->second;
                if (++c.pktCount > pktCeiling) {
                    if (!c.pktReported) {
                        c.pktReported = true;
                        const std::uint32_t pid = c.id;
                        anticheat.Report(pid, CheatKind::RpcFlood, "packet flood: over " + std::to_string(pktCeiling) + " packets in a second");
                        AntiCheatAfterRefusal(pid);
                    }
                    continue;   // dropped (the reference above may be gone if the peer was just kicked)
                }
            }
        }
        // And in every mode: once a peer's session is keyed, it seals everything
        // (a plaintext Join or key hello is only a retry from before it knew).
        if (!sealed && type != Join && type != JoinHello && type != Query && type != PlayerQuery) {
            const net::SecureChannel* s = SessionFor(from);
            if (s && s->Ready()) continue;
        }
        if (type == JoinHello) {
            // A sealed join starts: trade keys, then the client sends its Join
            // encrypted. The hello is padded to at least the answer's size.
            if (len < kJoinHelloSize || m_clients.count(from)) continue;
            if (JoinBlocked(from)) continue;   // too many attempts from this address: its key exchange waits
            const std::uint8_t version = p.ReadU8();
            const std::string clientPk = p.ReadString();
            if (!p.Ok() || version < kJoinVersion) continue;
            if (anticheat.settings.enabled && anticheat.settings.requireEncryption) encryption = true;   // the rules ask for it
            net::Packet k(JoinKey);
            k.Write(kJoinVersion);
            if (!encryption || !net::SecureChannel::Available() ||
                clientPk.size() != net::SecureChannel::kPublicKeyBytes) {
                // No encryption here: the client may join in the clear.
                k.Write(std::string()); k.Write(std::string());
                SendDatagram(from, k.Data(), k.Size(), /*allowEncrypt=*/false);
                continue;
            }
            auto pj = m_pendingJoins.find(from);
            if (pj == m_pendingJoins.end()) {
                if (m_pendingJoins.size() >= kMaxPendingJoins) {
                    // Full: the oldest exchange goes (it has had the longest).
                    auto oldest = std::max_element(m_pendingJoins.begin(), m_pendingJoins.end(),
                        [](const auto& a, const auto& b) { return a.second.age < b.second.age; });
                    m_pendingJoins.erase(oldest);
                }
                pj = m_pendingJoins.emplace(from, PendingJoin{}).first;
            }
            PendingJoin& pending = pj->second;
            const std::vector<std::uint8_t> pkBytes(clientPk.begin(), clientPk.end());
            if (pending.clientPk != pkBytes || !pending.secure.Ready()) {
                CountJoinAttempt(from, 1);   // new keys (and a signature): an attempt
                pending.secure = net::SecureChannel{};
                pending.secure.GenerateKeys();
                if (!pending.secure.Establish(pkBytes, /*asServer=*/true)) { m_pendingJoins.erase(pj); continue; }
                pending.secure.SetSequenced(true);
                pending.secure.SetAssociatedData({kEncEnvelope, kJoinVersion});
                pending.clientPk = pkBytes;
            }
            pending.age = 0.0f;
            const auto spk = pending.secure.PublicKey();
            const std::string serverPk(spk.begin(), spk.end());
            k.Write(serverPk);
            std::string sig;
            if (m_hostSeed.size() == 32) {
                okayac::ed25519::Seed seed;
                std::copy(m_hostSeed.begin(), m_hostSeed.end(), seed.begin());
                const auto sg = okayac::ed25519::Sign(JoinKeyMessage(serverPk, clientPk), seed);
                okayac::Wipe(seed.data(), seed.size());
                sig.assign(sg.begin(), sg.end());
            }
            k.Write(sig);
            SendDatagram(from, k.Data(), k.Size(), /*allowEncrypt=*/false);
            continue;
        }
        if (type == Query) {
            // A server browser asking for our details (no join, no state). The
            // request is padded to at least the size of a typical answer so the
            // server can't be used to amplify traffic at a spoofed address.
            if (len < net::kQueryMinSize || p.ReadString() != net::kQueryMagic) continue;
            const std::uint32_t nonce = p.ReadU32();
            net::Packet q(QueryInfo);
            q.Write(std::string(net::kQueryMagic)); q.Write(nonce);
            q.Write(std::uint8_t(net::kQueryProtocol));
            q.Write(serverName); q.Write(gameId); q.Write(mapName);
            q.Write(std::uint32_t(PlayerCount()));
            q.Write(std::uint32_t(maxPlayers + (dedicated ? 0 : 1)));
            std::uint8_t flags = 0;
            if (!password.empty()) flags |= net::kQueryPassword;
            if (dedicated) flags |= net::kQueryDedicated;
            if (m_joinCheck || m_verifyToken || (anticheat.settings.enabled && !anticheat.settings.ticketKey.empty()))
                flags |= net::kQueryAuth;   // it checks sign-ins (or session tickets): clients send their token
            if (encryption && net::SecureChannel::Available()) flags |= net::kQueryEncrypted;
            q.Write(flags);
            SendDatagram(from, q.Data(), q.Size(), /*allowEncrypt=*/false);
            continue;
        }
        if (type == PlayerQuery) {
            // Who's playing (server browser). Answered only to a sender that echoes
            // the challenge we gave its address, so the (larger) list can't be
            // aimed at a spoofed victim.
            if (len < net::kQueryMinSize || p.ReadString() != net::kQueryMagic) continue;
            const std::uint32_t got = p.ReadU32(), want = QueryChallenge(from);
            if (got != want) {
                net::Packet c(PlayerChallenge); c.Write(std::string(net::kQueryMagic)); c.Write(want);
                SendDatagram(from, c.Data(), c.Size(), /*allowEncrypt=*/false);
                continue;
            }
            const auto roster = Roster();
            const std::uint32_t n = (std::uint32_t)std::min<std::size_t>(roster.size(), 64);
            net::Packet l(PlayerList); l.Write(std::string(net::kQueryMagic)); l.Write(n);
            for (std::uint32_t i = 0; i < n; ++i) {
                l.Write(roster[i].name.substr(0, 32)); l.Write(roster[i].userId);
                l.Write(std::int32_t(roster[i].score)); l.Write(roster[i].seconds);
            }
            SendDatagram(from, l.Data(), l.Size(), /*allowEncrypt=*/false);
            continue;
        }
        if (type == Join) {
            std::string name = p.ReadString();           // display name (may be empty)
            std::string room = p.ReadString();           // lobby room (may be empty)
            std::string pass = p.ReadString();           // password (may be empty)
            std::string token = p.ReadString();           // identity token (may be empty)
            std::string clientPk = p.ReadString();        // encryption public key (may be empty)
            bool existed = m_clients.count(from) != 0;
            // A sealed join arrives encrypted under its key exchange's session.
            auto pendingIt = (!existed && sealed) ? m_pendingJoins.find(from) : m_pendingJoins.end();
            const bool viaPending = pendingIt != m_pendingJoins.end();
            // Trailing: the join version byte (sealed joins only), then, from a client
            // that is rejoining, the resume token its last Welcome gave it.
            std::uint64_t resumeToken = 0;
            if (!existed && p.Ok()) {
                if (viaPending) p.ReadU8();
                if (p.Remaining() >= 8) { const std::uint64_t t = ReadU64(p); if (p.Ok()) resumeToken = t; }
            }
            std::string userId;
            const bool acOn = anticheat.settings.enabled;
            const bool needCrypto = acOn && anticheat.settings.requireEncryption;
            if (needCrypto && net::SecureChannel::Available()) encryption = true;   // the rules ask for it
            if (!existed && JoinBlocked(from)) {
                // Too many attempts from this address: say so once, then ignore.
                JoinLimit& l = m_joinLimits[from.address];
                if (!l.told) {
                    l.told = true;
                    net::Packet r(Reject);
                    r.Write("too many join attempts; try again in " + std::to_string((int)std::ceil(l.blockedUntil - m_serverTime)) + " s");
                    SendDatagram(from, r.Data(), r.Size(), /*allowEncrypt=*/viaPending);
                }
                continue;
            }
            if (!existed) {
                // Gate new joins on capacity, then encryption (nothing secret is
                // checked in the clear), then the password, then identity;
                // refuse politely.
                const char* why = nullptr;
                std::string ticketWhy, banWhy;
                const std::string addrIdent = "a:" + std::to_string(from.address);
                auto kickedRecently = [&](const std::string& ident) {
                    const float left = KickBanLeft(ident);
                    if (left <= 0.0f) return false;
                    banWhy = "kicked by the anti-cheat; try again in " + std::to_string((int)std::ceil(left)) + " s";
                    return true;
                };
                // A rejoin that replaces a connection the host still holds needs no free slot.
                auto holdsToken = [&](std::uint64_t t) {
                    if (t == 0 || resumeGraceSeconds <= 0.0f) return false;
                    for (const auto& [ep2, other] : m_clients) if (other.resumeToken == t) return true;
                    return false;
                };
                if (!anticheat.configError.empty()) why = "server misconfigured (its anti-cheat rules didn't load)";
                else if ((int)m_clients.size() >= maxPlayers && !holdsToken(resumeToken)) why = "server is full";
                else if (needCrypto && !viaPending)
                    // The password and ticket must not travel in the clear.
                    why = net::SecureChannel::Available() ? kNeedsEncryption
                                                          : "encryption required, but this server was built without libsodium";
                else if (!password.empty() && pass != password) why = "wrong password";
                else if (acOn && !anticheat.settings.ticketKey.empty()) {
                    // A signed session ticket names the account; it takes the
                    // place of the token verifiers. It must be for this server
                    // (not any: "*" only when the rules allow it) and, when it
                    // names a session key (cnf), for this very connection.
                    const AntiCheatSettings& s = anticheat.settings;
                    std::vector<std::uint8_t> key;
                    net::SessionTicket::Base64UrlDecode(s.ticketKey, key);
                    net::SessionTicketClaims claims;
                    std::string err;
                    if (s.ticketAudience.empty()) {
                        if (!m_audienceWarned) OKAY_ERROR("anti-cheat: a ticketKey needs a ticketAudience (this server's id); refusing joins");
                        m_audienceWarned = true;
                        why = "server misconfigured (no ticket audience)";
                    } else if (!net::SessionTicket::Verify(token, key, s.ticketAudience, (std::int64_t)std::time(nullptr), claims, &err)) {
                        ticketWhy = "session ticket refused: " + err; why = ticketWhy.c_str();
                    } else if (claims.audience == "*" && !s.ticketAnyAudience) {
                        why = "session ticket refused: ticket is for another server";
                    } else if (!claims.keyBinding.empty()) {
                        const std::vector<std::uint8_t> pk = viaPending ? pendingIt->second.clientPk
                                                                        : std::vector<std::uint8_t>(clientPk.begin(), clientPk.end());
                        if (net::SessionTicket::KeyBinding(pk) != claims.keyBinding)
                            why = "session ticket refused: ticket is for another connection";
                    }
                    if (!why) userId = claims.userId;
                }
                else if (m_joinCheck) {
                    const int r = m_joinCheck(token, userId);
                    if (r == -2) why = "server busy, try again";
                    else if (r < 0) continue;                  // still checking: the client retries
                    else if (r == 0) why = "authentication failed";
                }
                else if (m_verifyToken && !m_verifyToken(token, userId)) why = "authentication failed";
                // Kicked by the anti-cheat a moment ago: the account, or for an
                // anonymous join the address (other signed-in players behind
                // the same address aren't locked out).
                if (!why && kickedRecently(userId.empty() ? addrIdent : "u:" + userId)) why = banWhy.c_str();
                // On the server's ban list (a file an admin can read and edit).
                if (!why && acOn) {
                    const std::string who = userId.empty() ? addrIdent : "u:" + userId;
                    if (BannedByList(who, banWhy)) { why = banWhy.c_str(); AcAudit("join_refused", who, "banned"); }
                }
                if (why) {
                    // A refusal is an attempt; a wrong password counts double
                    // (guessing it is what the limit is for).
                    CountJoinAttempt(from, std::string(why) == "wrong password" ? 2 : 1);
                    net::Packet r(Reject); r.Write(std::string(why));
                    SendDatagram(from, r.Data(), r.Size(), /*allowEncrypt=*/viaPending);
                    continue;
                }
                // One connection per account: a new sign-in replaces the old one.
                if (!userId.empty())
                    for (auto& [ep, other] : m_clients)
                        if (!(ep == from) && other.userId == userId && !(resumeToken != 0 && other.resumeToken == resumeToken)) { Kick(other.id, "signed in elsewhere"); break; }
            }
            // A client that is rejoining after a drop takes its state back: the
            // token must match, and (for a signed-in account) so must the account.
            // If the host has not noticed the drop yet, the old connection is
            // released first (same cleanup as a timeout).
            Parked resumed; bool haveResumed = false;
            if (!existed && resumeToken != 0 && resumeGraceSeconds > 0.0f) {
                for (auto old = m_clients.begin(); old != m_clients.end(); ++old)
                    if (old->second.resumeToken == resumeToken && old->second.userId == userId) { ReleaseClient(old); break; }
                for (auto pk = m_parked.begin(); pk != m_parked.end(); ++pk)
                    if (pk->token == resumeToken) {
                        if (pk->userId == userId && pk->age <= resumeGraceSeconds) { resumed = *pk; haveResumed = true; }
                        m_parked.erase(pk);   // one use: a token that failed is dead too
                        break;
                    }
            }
            auto& c = m_clients[from];
            bool isNew = (c.id == 0);
            bool migratedBack = false;
            if (isNew) {
                c.endpoint = from;
                std::uint32_t returning = 0;   // host migration: a player of the old host, back with its id
                migratedBack = !haveResumed && TakeExpectedId(from, returning);
                c.id = haveResumed ? resumed.id : (migratedBack ? returning : m_nextId++);
                c.state = {c.id, 0, 0, 0, '@'};
                if (haveResumed) {
                    c.state = {c.id, resumed.pos.x, resumed.pos.y, resumed.pos.z, resumed.glyph};
                    c.hasState = resumed.hasState;
                    c.stateAge = resumed.age;   // the move check allows for the time away
                    c.score = resumed.score;
                    c.ready = resumed.ready;
                    c.resumed = true;
                }
                // The token is a bearer secret: with resumeRequiresEncryption it is only issued
                // where the Welcome that carries it is sealed (the join came through a key exchange: viaPending).
                if (resumeGraceSeconds > 0.0f && (!resumeRequiresEncryption || viaPending)) {
                    std::random_device rdt;
                    do { c.resumeToken = (static_cast<std::uint64_t>(rdt()) << 32) | rdt(); } while (c.resumeToken == 0);
                }
                c.name = name.empty() ? ("Player" + std::to_string(c.id)) : name;
                c.room = room;
                c.joinedAt = m_serverTime;
                c.userId = userId;
                // A join that got in costs this address nothing (its key
                // exchange is given back): players behind one address still
                // all get in, a flood that never finishes doesn't.
                if (auto jl = m_joinLimits.find(from.address); jl != m_joinLimits.end() && jl->second.count > 0) --jl->second.count;
                c.ident = userId.empty() ? "a:" + std::to_string(from.address) : "u:" + userId;
                anticheat.RestorePeer(c.id, c.ident);   // what it had if it left a moment ago
                if (viaPending) {
                    // The key exchange's session becomes the client's (numbered,
                    // with the version authenticated).
                    c.secure = pendingIt->second.secure;
                    c.sealedJoin = true;
                    m_pendingJoins.erase(pendingIt);
                }
                // Establish this client's encrypted session from its public key.
                else if (encryption && net::SecureChannel::Available() && !clientPk.empty()) {
                    c.secure.GenerateKeys();
                    c.secure.Establish(std::vector<std::uint8_t>(clientPk.begin(), clientPk.end()),
                                       /*asServer=*/true);
                    // Replay protection rides on required encryption (the Welcome
                    // tells the client to number its packets too).
                    if (needCrypto) c.secure.SetSequenced(true);
                }
            }
            if (isNew && !c.secure.Ready()) {
                std::random_device rd;
                c.cookie = ((static_cast<std::uint64_t>(rd()) << 32) ^ rd()) | 1u;
            }
            c.lastSeen = 0.0f;
            net::Packet w(Welcome);
            w.Write(c.id);
            w.Write(serverName);
            w.Write(c.userId);   // tell the client its verified identity ("" if anon)
            {   // our public key so the client can establish its side ("" if none)
                auto pk = c.secure.PublicKey();
                w.Write(std::string(pk.begin(), pk.end()));
            }
            // A trailing flags byte only when there's something to say (bit 0 =
            // number sealed packets); a server without anti-cheat P2 sends the
            // same Welcome as before, and older clients stop reading earlier.
            // bit 1 = a fixed net tick, its rate (f32) follows.
            std::uint8_t welcomeFlags = c.secure.Sequenced() ? 1 : 0;
            if (HostTickRate() > 0.0f) welcomeFlags |= 2;
            if (authoritativeMovement) welcomeFlags |= 4 | 8;   // bit 2: send inputs, not positions; bit 3: PawnAcks (predict)
            if (c.cookie) welcomeFlags |= 16;                    // bit 4: a session cookie (u64) follows
            if (c.resumeToken) welcomeFlags |= 32;               // bit 5: a resume token (u64) follows
            if (c.resumed) welcomeFlags |= 64;                   // bit 6: your old state was restored (also on a resent Welcome)
            if (welcomeFlags) w.Write(welcomeFlags);
            if (welcomeFlags & 2) w.Write(HostTickRate());
            if (welcomeFlags & 16) WriteU64(w, c.cookie);
            if (welcomeFlags & 32) WriteU64(w, c.resumeToken);
            // A sealed joiner already has the keys; a plaintext joiner gets them here.
            SendDatagram(from, w.Data(), w.Size(), /*allowEncrypt=*/c.sealedJoin);
            if (isNew) {
                OKAY_INFO("net: client ", c.id, " '", c.name, "' joined from ", from.ToString());
                m_rosterBurst = 3;   // everyone (the newcomer too) hears the new player list now, not in a second
                // Who holds what, for a player coming back after a host migration (PlayersTick
                // sends the table to new players; this one is not new to the game).
                if (migratedBack)
                    for (const auto& [netId, owner] : m_netOwners) SendOrderedTo(c.id, "__own", std::to_string(owner) + "|" + netId);
                if (m_peerJoined) m_peerJoined(c.id, c.name);
                if (haveResumed && m_peerResumed) {
                    ResumedPeer rp; rp.id = c.id; rp.position = resumed.pos; rp.hasPosition = resumed.hasState;
                    rp.score = c.score; rp.ready = c.ready;
                    m_peerResumed(rp);
                }
                if (haveResumed) ResendUnacked(c.id, resumed.resend);   // what it had not acked when it dropped
                // Full sync: send every current synced variable to the newcomer.
                for (auto& [k, v] : m_syncVars) SendVarTo(c, k);   // sent again until it acknowledges each
                SendLifeSnapshot(c.id);   // who is dead or hurt right now
                {   // replicated systems (build mode, quests) send what the newcomer missed
                    const std::uint32_t joined = c.id;
                    const auto hooks = m_joinHooks;   // a hook may add or remove hooks
                    for (const auto& h : hooks) if (h.second) h.second(joined);
                }
            }
        } else if (type == SyncVar) {
            // A client requested a variable change; apply it and fan out to all.
            std::string key = p.ReadString();
            std::string value = p.ReadString();
            auto cl = m_clients.find(from);   // only joined clients may change shared state
            if (!p.Ok() || cl == m_clients.end() || !CookieOk(cl->second, p) || !rateOk(cl->second, len)) continue;
            const std::uint32_t ver = p.ReadU32();   // absent from an older client
            HandleClientVar(cl->second, key, value, p.Ok(), ver);
        } else if (type == VarAck) {
            // A client has one of our variable changes: stop sending it.
            auto cl = m_clients.find(from);
            if (cl == m_clients.end()) continue;
            const std::uint8_t count = p.ReadU8();
            for (std::uint8_t i = 0; i < count && p.Ok(); ++i) {
                const std::string key = p.ReadString();
                const std::uint32_t ver = p.ReadU32();
                if (!p.Ok()) break;
                auto v = cl->second.varOut.find(key);
                if (v != cl->second.varOut.end() && ver >= v->second.ver) cl->second.varOut.erase(v);
            }
        } else if (type == ClockPong) {
            // A client's clock, for the heartbeat (anti-cheat maxClockDrift).
            auto it = m_clients.find(from);
            if (it == m_clients.end()) continue;
            const std::uint64_t nonce = ReadU64(p);
            const std::uint32_t seq = p.ReadU32();
            const std::uint64_t clientMs = ReadU64(p);
            const std::string tag = p.ReadString();
            if (!p.Ok()) continue;
            Client& c = it->second;
            if (nonce == c.pingNonce && c.pingNonce != 0) {   // its round trip, for hit claims
                const float rtt = static_cast<float>((m_acClockMs - c.pingSentMs) / 1000.0);
                c.rttSamples[c.rttNext] = rtt;
                c.rttNext = (c.rttNext + 1) % 8;
                if (c.rttCount < 8) ++c.rttCount;
                c.rtt = *std::min_element(c.rttSamples, c.rttSamples + c.rttCount);
                c.pingNonce = 0;
            }
            if (!c.clock) continue;
            const auto v = c.clock->Accept(nonce, seq, clientMs, tag, static_cast<std::uint64_t>(m_acClockMs));
            if (v == net::ClockCheck::Verdict::BadResponse) continue;
            c.clockQuiet = 0.0f;
            if (v == net::ClockCheck::Verdict::Drift && c.clockReported >= kClockReportEvery) {
                c.clockReported = 0.0f;
                const double d = c.clock->LastDrift() * 100.0;
                char detail[48];
                std::snprintf(detail, sizeof detail, "%+.0f%%", d);
                const std::uint32_t id = c.id;
                anticheat.Report(id, CheatKind::ClockDrift, detail);
                AntiCheatAfterRefusal(id);   // (may kick: c is gone after this)
            }
        } else if (type == InputCmd) {
            // A player's inputs (authoritative movement): queued by number.
            // ServerMovementTick applies one a tick, or two while a hitch drains.
            auto it = m_clients.find(from);
            if (it == m_clients.end() || !authoritativeMovement) continue;
            Client& c = it->second;
            const std::string pawn = p.ReadString();
            const std::uint8_t n = p.ReadU8();
            if (!p.Ok() || n == 0 || n > 8) continue;
            if (c.pawn.empty()) {
                // Its player: one of its own SpawnOwned objects ("sp/<its id>/...").
                const std::string prefix = "sp/" + std::to_string(c.id) + "/";
                if (pawn.compare(0, prefix.size(), prefix) != 0 || !m_syncObjs.count(pawn) || m_pawnOwner.count(pawn)) continue;
                c.pawn = pawn;
                m_pawnOwner[pawn] = c.id;
                c.lastSeen = 0.0f;
            } else if (pawn != c.pawn) {
                continue;
            }
            const float rate = HostTickRate();
            for (std::uint8_t i = 0; i < n; ++i) {
                const std::uint32_t tick = p.ReadU32(), seq = p.ReadU32();
                PlayerInput in;
                in.move.x = p.ReadF32(); in.move.y = p.ReadF32();
                in.yaw = p.ReadF32(); in.pitch = p.ReadF32();
                in.held = p.ReadU32(); in.pressed = p.ReadU32();
                if (!p.Ok()) break;
                bool seenDrop = false;
                for (std::uint8_t d = 0; d < c.droppedN; ++d) if (c.droppedSeq[d] == seq) { seenDrop = true; break; }
                if (seq <= c.lastInputSeq || c.inputs.count(seq) || seenDrop) continue;   // applied, queued, or a dropped newest
                // Commands for ticks too far ahead of or behind the server's are refused.
                if (std::fabs((double)tick - (double)m_netTick) > rate) { ++m_inputsDropped; continue; }
                in.Clamp();
                if (anticheat.DetectOn()) {   // what the player asked for: ghost movement, aim snaps, jump spam
                    const double now = anticheat.Clock();
                    anticheat.detector.OnInput(c.id, in.move.x, in.move.y, now);
                    anticheat.detector.OnAim(c.id, in.yaw, in.pitch, now);
                    if (in.Pressed(PlayerInput::Jump)) anticheat.detector.OnJump(c.id, now);
                    // Every button press on the player's own tick clock: a macro presses at one steady gap.
                    for (int bit = 0; bit < 31; ++bit)
                        if ((in.pressed >> bit) & 1u) anticheat.detector.OnPress(c.id, bit, (double)tick / (double)std::max(rate, 1.0f), now);
                }
                c.inputs[seq] = {tick, in};
            }
            // Past the budget, drop the newest input, not the oldest unapplied one
            // (that one may be a jump still waiting). Its press is kept and applied
            // with the next input that does run; the seq is remembered so the
            // client's resend of it doesn't run twice.
            while (c.inputs.size() > kMaxQueuedInputs) {
                auto last = std::prev(c.inputs.end());
                c.droppedPressed |= last->second.second.pressed;
                if (c.droppedN < 8) c.droppedSeq[c.droppedN++] = last->first;
                else {
                    for (int d = 1; d < 8; ++d) c.droppedSeq[d - 1] = c.droppedSeq[d];
                    c.droppedSeq[7] = last->first;
                }
                c.inputs.erase(last);
                ++m_inputsDropped;
            }
        } else if (type == Ping) {
            // Echo the client's timestamp straight back so it can measure RTT
            // (a joined client's, or one mid-join: not a stranger's).
            if (!m_clients.count(from) && !m_pendingJoins.count(from)) continue;
            float stamp = p.ReadF32();
            net::Packet pong(Pong); pong.Write(stamp);
            SendDatagram(from, pong.Data(), pong.Size());
        } else if (type == Pong) {
            // A client's answer to the host's own ping (loss and round trip measured here too).
            auto it = m_clients.find(from);
            const float stamp = p.ReadF32();
            if (it == m_clients.end() || !p.Ok()) continue;
            for (auto& m : it->second.linkMarks)
                if (m.stamp == stamp && !m.answered) {
                    m.answered = true;
                    it->second.linkRtt = std::max(0.0f, (m_serverTime - stamp) * 1000.0f);
                    break;
                }
        } else if (type == Ready) {
            std::uint8_t r = p.ReadU8();
            auto it = m_clients.find(from);
            if (it != m_clients.end()) it->second.ready = (r != 0);
        } else if (type == ReliableMsg) {
            auto it = m_clients.find(from);
            if (it != m_clients.end()) {
                std::uint32_t seq = p.ReadU32();
                std::string channel = p.ReadString();
                std::string data    = p.ReadString();
                // Over the rate: not acked either, so the client sends it again later.
                if (!it->second.relSeen.Seen(seq) && !rateOk(it->second, len)) continue;
                net::Packet ack(ReliableAck); ack.Write(seq);
                SendDatagram(from, ack.Data(), ack.Size());
                if (!it->second.relSeen.Accept(seq)) continue;   // a repeat (the rate was charged before the ack)
                if (channel == "__hit") { HandleHitClaim(it->second, data); continue; }   // the server's to judge
                if (HandleClientRequest(it->second, channel, data)) continue;
                if (ClientMessageOk(it->second, channel, data) && AntiCheatAllows(it->second.id, channel, data))
                    Deliver(it->second.id, channel, data);
            }
        } else if (type == ReliableAck) {
            auto it = m_clients.find(from);
            if (it != m_clients.end()) it->second.relOut.erase(p.ReadU32());
        } else if (type == net::kSnapDelta || type == net::kSnapAck) {
            auto it = m_clients.find(from);
            if (it == m_clients.end() || !rateOk(it->second, len)) continue;
            std::size_t body = len;
            if (it->second.cookie) {
                if (len < 8) continue;
                net::Packet ck(data + len - 8, 8);
                if (!CookieOk(it->second, ck)) continue;
                body = len - 8;
            }
            if (type == net::kSnapAck) {
                std::uint32_t id = 0;
                if (net::DecodeSnapAck(data, body, id)) NoteSnapAck(it->second.snapOut, id);
            } else {
                OnSnapDelta(it->second.snapIn, data, body, from, &it->second);
            }
        } else if (type == net::kChanRel || type == net::kChanRelAck || type == net::kChanSeq) {
            auto it = m_clients.find(from);
            if (it == m_clients.end()) continue;
            if (type == net::kChanRel) OnOrdered(it->second.pipes, from, it->second.id, &it->second, p, len);
            else if (type == net::kChanRelAck) OnOrderedAck(it->second.pipes, p);
            else OnSequencedFromClient(it->second, p, len);
        } else if (type == DirectMessage) {
            auto it = m_clients.find(from);
            if (it != m_clients.end() && rateOk(it->second, len)) {
                std::uint32_t sender = it->second.id;
                p.ReadU32();                              // sender-reported id (ignored)
                std::uint32_t target = p.ReadU32();
                std::string channel = p.ReadString();
                std::string data    = p.ReadString();
                if (!ClientMessageOk(it->second, channel, data)) continue;
                if (!AntiCheatAllows(sender, channel, data)) continue;   // refused (and maybe kicked)
                if (target == m_localId) {                // addressed to the host
                    Deliver(sender, channel, data);
                } else {                                   // route to one client
                    for (auto& [ep, cl] : m_clients)
                        if (cl.id == target) {
                            net::Packet fwd(Message);
                            fwd.Write(sender); fwd.Write(channel); fwd.Write(data);
                            SendDatagram(cl.endpoint, fwd.Data(), fwd.Size());
                            break;
                        }
                }
            }
        } else if (type == State) {
            auto it = m_clients.find(from);
            if (it != m_clients.end()) {
                PeerState s;
                s.id    = it->second.id;
                p.ReadU32(); // sender-reported id (ignored; endpoint is truth)
                s.x = p.ReadF32(); s.y = p.ReadF32(); s.z = p.ReadF32();
                s.glyph = static_cast<char>(p.ReadU8());
                Client& c = it->second;
                if (p.Remaining() == 4 || p.Remaining() == 12) p.ReadU32();   // its net tick
                if (!CookieOk(c, p)) continue;
                if (authoritativeMovement) {   // where a player is is the server's call, claimed or not
                    c.state.glyph = s.glyph;
                    c.lastSeen = 0.0f;
                    continue;
                }
                c.lastSeen = 0.0f;
                if (!std::isfinite(s.x) || !std::isfinite(s.y) || !std::isfinite(s.z)) {   // garbage
                    if (anticheat.DetectOn()) anticheat.detector.OnMalformed(c.id, "state: not finite", anticheat.Clock());
                    continue;
                }
                if (anticheat.DetectOn() && m_serverTime >= c.teleportUntil)
                    anticheat.detector.OnMove(c.id, "@avatar", Vec3(s.x, s.y, s.z), anticheat.Clock());
                if (maxMoveSpeed > 0.0f && c.hasState && m_serverTime >= c.teleportUntil) {   // too fast? keep the last good spot
                    const float dx = s.x - c.state.x, dy = s.y - c.state.y, dz = s.z - c.state.z;
                    // At most a quarter second of travel, however long it was quiet:
                    // going silent mustn't bank a long jump (a bigger gap is
                    // corrected back to the last good spot).
                    const float allowed = maxMoveSpeed * std::min(std::max(c.stateAge, m_snapshotInterval), 0.25f) * 1.25f + 0.25f;
                    if (dx * dx + dy * dy + dz * dz > allowed * allowed) {
                        ++c.violations;
                        if (anticheat.DetectOn()) anticheat.detector.OnCorrection(c.id, anticheat.Clock());
                        net::Packet cp(Correct); cp.Write(c.state.x); cp.Write(c.state.y); cp.Write(c.state.z);
                        SendDatagram(c.endpoint, cp.Data(), cp.Size());
                        if (kickAfterViolations > 0 && c.violations >= kickAfterViolations)
                            Kick(c.id, "movement check failed");   // (erases c; nothing below touches it)
                        continue;
                    }
                }
                c.state = s;
                c.hasState = true;   // we now know where this client is (for interest culling)
                c.stateAge = 0.0f;
                ApplyPeer(s); // host sees the client move
            }
        } else if (type == Leave) {
            auto it = m_clients.find(from);
            p.ReadU32();   // sender-reported id (ignored)
            if (it != m_clients.end() && CookieOk(it->second, p)) {
                ReleaseClient(it);
            }
        } else if (type == Message) {
            auto it = m_clients.find(from);
            if (it != m_clients.end() && rateOk(it->second, len)) {
                std::uint32_t sender = it->second.id;   // endpoint is the source of truth
                const std::string senderRoom = it->second.room;
                p.ReadU32();                            // sender-reported id (ignored)
                std::string channel = p.ReadString();
                std::string data    = p.ReadString();
                if (!CookieOk(it->second, p)) continue;
                if (channel == "__hit") { HandleHitClaim(it->second, data); continue; }   // never relayed
                if (HandleClientRequest(it->second, channel, data)) continue;   // a request for the server, never relayed
                if (!ClientMessageOk(it->second, channel, data)) continue;
                if (!AntiCheatAllows(sender, channel, data)) continue;   // not applied, not relayed
                if (dedicated || m_localRoom == senderRoom)   // host only sees its room (a dedicated server sees all)
                    Deliver(sender, channel, data);
                // Relay to every other client in the sender's room.
                net::Packet relay(Message);
                relay.Write(sender);
                relay.Write(channel);
                relay.Write(data);
                for (auto& [ep, c] : m_clients)
                    if (c.id != sender && c.room == senderRoom)
                        SendDatagram(c.endpoint, relay.Data(), relay.Size());
            }
        }
    }
    if (m_relayLost) {   // the relay refused this host (RelayError())
        if (RetryOnlineCode()) return;   // HostOnline's made-up code was taken: another one
        NoteRelayError();
        Stop();
        return;
    }
    for (auto it = m_pendingJoins.begin(); it != m_pendingJoins.end();)   // key exchanges never finished
        it = (it->second.age += dt) > kPendingJoinTtl ? m_pendingJoins.erase(it) : std::next(it);

    for (auto pk = m_parked.begin(); pk != m_parked.end();)
        pk = (pk->age += dt) > resumeGraceSeconds ? m_parked.erase(pk) : std::next(pk);

    // Time out silent clients (and age the move-check / rate-limit windows).
    for (auto it = m_clients.begin(); it != m_clients.end();) {
        it->second.lastSeen += dt;
        it->second.stateAge += dt;
        {   // the host's own ping, once a second: loss and round trip measured on this side too (Stats, PeerLink)
            Client& lc = it->second;
            for (auto& m : lc.linkMarks) m.age += dt;
            if ((lc.linkTimer -= dt) <= 0.0f) {
                lc.linkTimer = 1.0f;
                net::Packet pg(Ping); pg.Write(m_serverTime);
                SendDatagram(lc.endpoint, pg.Data(), pg.Size());
                if (lc.linkMarks.size() >= 16) lc.linkMarks.erase(lc.linkMarks.begin());
                lc.linkMarks.push_back(Client::Mark{m_serverTime, 0.0f, false});
            }
        }
        if ((it->second.msgTimer += dt) >= 1.0f) { it->second.msgTimer = 0.0f; it->second.msgCount = 0; it->second.msgBytes = 0; it->second.hitClaims = 0; it->second.pktCount = 0; it->second.pktReported = false; }
        if ((it->second.violationTimer += dt) >= 60.0f) { it->second.violationTimer = 0.0f; it->second.violations = 0; }
        if (it->second.lastSeen > kClientTimeout) {
            auto dead = it++;
            ReleaseClient(dead);
        } else {
            ++it;
        }
    }

    // Anti-cheat clock heartbeat: ask each client for its game clock about once
    // a second (the answer is checked above). One that won't answer is reported
    // too, or skipping the answer would be the way around it.
    // With the anti-cheat on, or a fixed tick (lag compensation clamps its
    // rewind to the shooter's round trip), the same pings also measure each
    // client's round trip, whether or not the clock is checked.
    if (anticheat.settings.enabled || HostTickRate() > 0.0f) {
        const bool checkClock = anticheat.settings.enabled && anticheat.settings.maxClockDrift > 0.0f;
        std::vector<std::uint32_t> quiet;
        for (auto& [ep, c] : m_clients) {
            if (checkClock && !c.clock) c.clock = std::make_shared<net::ClockCheck>(anticheat.settings.maxClockDrift);
            c.clockReported += dt;
            if ((c.clockTimer -= dt) <= 0.0f) {
                c.clockTimer = 1.0f;
                net::Packet cp(ClockPing);
                if (c.clock) {
                    const auto ch = c.clock->Issue(static_cast<std::uint64_t>(m_acClockMs));
                    WriteU64(cp, ch.nonce); cp.Write(ch.seq); cp.Write(c.clock->Key());
                    c.pingNonce = ch.nonce;
                } else {
                    std::random_device rd;
                    c.pingNonce = ((static_cast<std::uint64_t>(rd()) << 32) ^ rd()) | 1u;
                    WriteU64(cp, c.pingNonce); cp.Write(std::uint32_t(0)); cp.Write(std::string("rtt"));
                }
                c.pingSentMs = m_acClockMs;
                SendDatagram(c.endpoint, cp.Data(), cp.Size());
            }
            if (!checkClock) continue;
            if ((c.clockQuiet += dt) > kClockQuietLimit) {
                c.clockQuiet = 0.0f;
                quiet.push_back(c.id);
            }
        }
        // Reported after the loop: the cheat handler (game code) may kick.
        for (std::uint32_t id : quiet) {
            anticheat.Report(id, CheatKind::ClockDrift, "no clock answer");
            AntiCheatAfterRefusal(id);
        }
    }

    // Once a second, tell each client who's in its room (names, scores, time).
    m_rosterTimer += dt;
    if ((m_rosterTimer >= 1.0f || m_rosterBurst > 0) && !m_clients.empty()) {
        m_rosterTimer = 0.0f;
        if (m_rosterBurst > 0) --m_rosterBurst;
        std::unordered_map<std::string, std::vector<RosterEntry>> byRoom;
        if (!dedicated) byRoom[m_localRoom].push_back({0, m_localName, std::string(), m_hostScore, m_serverTime});
        for (const MigPeer& w : m_migExpect)   // after a host migration: players still on their way back
            byRoom[m_localRoom].push_back({w.id, w.name, std::string(), 0, 0.0f});
        for (auto& [ep, c] : m_clients) byRoom[c.room].push_back({c.id, c.name, c.userId, c.score, m_serverTime - c.joinedAt});
        for (auto& [ep, c] : m_clients) {
            const auto& list = byRoom[c.room];
            net::Packet r(RosterMsg); r.Write(std::uint32_t(list.size()));
            for (const auto& e : list) { r.Write(e.id); r.Write(e.name); r.Write(e.userId); r.Write(std::int32_t(e.score)); r.Write(e.seconds); }
            SendDatagram(c.endpoint, r.Data(), r.Size());
        }
    }

    // Send each client a snapshot of just its own room: the host (id 0) if the
    // host shares that room, plus every client in the same room. This is what
    // makes lobbies work — separate rooms never see each other.
    // With a fixed net tick, a snapshot goes out once a tick and carries it
    // (even when nothing moved, so clients keep the tick); otherwise at snapshotRate.
    const bool fixedTick = HostTickRate() > 0.0f;
    const bool tickNow = fixedTick && StepNetTick(HostTickRate(), dt) > 0;
    m_snapshotTimer += dt;
    if ((fixedTick ? tickNow : m_snapshotTimer >= m_snapshotInterval) && !m_clients.empty()) {
        m_snapshotTimer = 0.0f;
        Vec3 lp = m_localAvatar ? m_localAvatar->localPosition : Vec3::Zero;
        for (auto& [ep, recipient] : m_clients) {
            // A lambda below needs the recipient; capturing a structured binding directly
            // is ill-formed before C++20 (Clang/Emscripten rejects it), so alias it to a
            // plain reference the closure can capture portably.
            auto& rc = recipient;
            const std::string& room = recipient.room;
            bool hostHere = !dedicated && (m_localRoom == room);   // a dedicated server has no avatar
            // Interest culling needs the recipient's own position; until it has
            // reported one, hold its snapshots (a tick or two) so we never spawn a
            // far peer on it from a default origin.
            if (interestRadius > 0.0f && !recipient.hasState) continue;
            Vec3 rpos{recipient.state.x, recipient.state.y, recipient.state.z};

            // Build only the peers this recipient needs THIS tick: skip its own
            // avatar (interest), skip peers outside interestRadius (interest), and
            // skip peers whose state is unchanged since we last sent it (delta).
            std::vector<PeerState> include;
            auto consider = [&](const PeerState& s, bool known) {
                if (s.id == rc.id) return;                          // don't echo their own avatar
                if (interestRadius > 0.0f) {                        // area-of-interest cull
                    if (!known) return;                             // position unknown -> don't sync yet
                    float dx = s.x - rpos.x, dy = s.y - rpos.y, dz = s.z - rpos.z;
                    if (dx * dx + dy * dy + dz * dz > interestRadius * interestRadius) return;
                }
                auto it = rc.sentStates.find(s.id);                 // delta: only if changed
                bool changed = it == rc.sentStates.end() ||
                               std::fabs(it->second.x - s.x) > 1e-4f ||
                               std::fabs(it->second.y - s.y) > 1e-4f ||
                               std::fabs(it->second.z - s.z) > 1e-4f ||
                               it->second.glyph != s.glyph;
                if (!changed) return;
                rc.sentStates[s.id] = s;
                include.push_back(s);
            };
            if (hostHere) consider(PeerState{0, lp.x, lp.y, lp.z, m_localGlyph}, true);
            for (auto& [ep2, c] : m_clients) if (c.room == room) consider(c.state, c.hasState);
            if (include.empty() && !fixedTick) continue;            // nothing changed -> send nothing

            net::Packet snap(Snapshot);
            snap.Write(std::uint32_t(include.size()));
            for (const PeerState& s : include) {
                snap.Write(s.id);
                snap.Write(s.x); snap.Write(s.y); snap.Write(s.z);
                snap.Write(std::uint8_t(s.glyph));
            }
            if (fixedTick) snap.Write(m_netTick);   // trailing: older clients stop reading before it
            if (fixedTick && authoritativeMovement) snap.Write(recipient.lastInputSeq);   // its newest input applied
            SendDatagram(recipient.endpoint, snap.Data(), snap.Size());
        }
    }

    // Resend any unacked reliable messages to each client.
    for (auto& [ep, c] : m_clients) {
        ResendReliable(c.relOut, c.endpoint, dt);
        std::vector<std::vector<std::uint8_t>> due;
        c.pipes.ordered.CollectResend(dt, due);
        for (const auto& bytes : due) SendDatagram(c.endpoint, bytes.data(), bytes.size());
    }
}

// A client's SyncVar (its cookie and rate already checked): the checks, then apply it and send it to
// its room. With a version (a client of this version or newer) the change is acknowledged whether
// it was taken or refused, so the client stops sending it again, and a late copy of an older write
// of its own is not applied over a newer one.
void NetworkManager::HandleClientVar(Client& c, const std::string& key, const std::string& value, bool hasVer, std::uint32_t ver) {
    const net::Endpoint from = c.endpoint;
    const std::uint32_t id = c.id;
    // Sized like a variable, and not a key factory: a client makes at most kMaxClientVars keys of its own.
    const bool isNew = !m_syncVars.count(key);
    if (key.empty() || key.size() > kMaxVarKey || value.size() > kMaxVarValue || (isNew && c.varsCreated >= kMaxClientVars)) {
        if (hasVer && !key.empty() && key.size() <= kMaxVarKey) SendVarAck(from, key, ver);   // refused: do not send it again
        return;
    }
    if (hasVer) {
        auto seen = c.varIn.find(key);
        if (seen != c.varIn.end() && ver <= seen->second) { SendVarAck(from, key, ver); return; }   // a repeat, or older than one applied
        if (c.varIn.size() >= kMaxVarPending && seen == c.varIn.end()) c.varIn.clear();
        c.varIn[key] = ver;
    }
    if (anticheat.settings.enabled && !AntiCheatAllowsSyncVar(id, key)) {
        // Put the sender's copy back to the server's value (it set it locally first).
        auto cl = m_clients.find(from);
        if (cl == m_clients.end()) return;
        if (hasVer) SendVarAck(from, key, ver);
        BumpVar(key);   // newer than what the sender holds of ours
        SendVarTo(cl->second, key);
        return;
    }
    // A counter the rules name (detect.economy): the detector checks the claimed value against what the server expects.
    if (anticheat.DetectOn() && anticheat.detector.settings.FindEconomy(key)) {
        char* end = nullptr;
        const float v = std::strtof(value.c_str(), &end);
        if (end && end != value.c_str() && *end == '\0') anticheat.detector.OnClaimedValue(id, key, v, anticheat.Clock());
        else anticheat.detector.OnMalformed(id, "economy value unreadable", anticheat.Clock());
        if (anticheat.detector.KickRequested(id)) { AntiCheatAfterRefusal(id); return; }
    }
    auto cl = m_clients.find(from);
    if (cl == m_clients.end()) return;
    if (isNew) ++cl->second.varsCreated;
    if (hasVer) SendVarAck(from, key, ver);
    net::NoteObject(key, key.size() + value.size(), true);
    ApplySyncVar(key, value);
    BumpVar(key);
    // A client's write goes to its own room (the host applies every one).
    const std::string room = cl->second.room;
    for (auto& [ep, other] : m_clients)
        if (other.room == room) SendVarTo(other, key);
}

} // namespace okay
