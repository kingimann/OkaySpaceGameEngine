// NetworkManager, part: the client's per-frame tick (ClientTick).
// Moved unchanged from NetworkManager.cpp (docs/engine/modularization_plan.md, batch N1).
// The class is declared in okay/Net/NetworkManager.hpp; file-local helpers shared by the parts are in
// NetworkManagerInternal.hpp.
#include "okay/Net/NetworkManager.hpp"
#include "okay/Net/NetSim.hpp"
#include "okay/Net/SessionTicket.hpp"
#include "AntiCheatCrypto.hpp"   // shared/: ed25519 for the host identity (#269)
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

void NetworkManager::ClientTick(float dt) {
    m_clock += dt;
    ++m_tickCount;   // the frame count a hit claim carries
    m_gameClockMs += static_cast<double>(dt) * 1000.0;
    RelayKeepAlive(dt);
    // Through a relay we don't know the host's (synthetic) endpoint until the relay
    // pairs us, so hold the join handshake until then (the recv loop below still
    // runs, so the relay's welcome is processed).
    bool awaitingRelay = m_relay && !m_relayWelcomed;
    if (!m_joined && !awaitingRelay) {
        m_joinTimer -= dt;
        const bool canSeal = encryption && net::SecureChannel::Available();
        const bool pinned = !pinnedHostKey.empty();
        // With a token to give, first ask the host whether it wants one (a
        // server-browser query: plaintext, padded, answered by every version).
        if (!m_authDecided && (m_authToken.empty() || authTokenToAnyHost)) m_authDecided = true;
        if (m_joinTimer <= 0.0f && !m_authDecided) {
            if (m_authQueries >= 4) {
                m_authDecided = true;   // no answer: an older host; it gets no token
                OKAY_WARN("net: ", m_serverEp.ToString(), " didn't say whether it checks sign-ins; joining without the account token");
            } else {
                if (m_authNonce == 0) { std::random_device rd; m_authNonce = rd() | 1u; }
                net::Packet q(Query);
                q.Write(std::string(net::kQueryMagic));
                q.Write(m_authNonce);
                std::vector<std::uint8_t> d(q.Data(), q.Data() + q.Size());
                if (d.size() < net::kQueryMinSize) d.resize(net::kQueryMinSize, 0);
                SendDatagram(m_serverEp, d.data(), d.size(), /*allowEncrypt=*/false);
                ++m_authQueries;
                m_joinTimer = 0.25f;
            }
        }
        if (!m_authDecided) {
            // still asking
        } else if (m_joinTimer <= 0.0f && pinned && !canSeal) {
            // A pin can only be checked over the sealed join.
            if (!m_pinWarned) OKAY_ERROR("net: a pinned host key needs encryption on (and libsodium); not joining");
            m_pinWarned = true;
            m_joinTimer = 1.0f;
        } else if (m_joinTimer <= 0.0f && canSeal && !m_keyed && !m_legacyJoin &&
                   (pinned || m_joinHellos < kSealedJoinTries)) {
            // Sealed join, step 1: trade keys (padded, so the answer is no bigger).
            net::Packet h(JoinHello);
            h.Write(kJoinVersion);
            const auto pk = m_secure.PublicKey();
            h.Write(std::string(pk.begin(), pk.end()));
            std::vector<std::uint8_t> d(h.Data(), h.Data() + h.Size());
            if (d.size() < kJoinHelloSize) d.resize(kJoinHelloSize, 0);
            SendDatagram(m_serverEp, d.data(), d.size(), /*allowEncrypt=*/false);
            ++m_joinHellos;
            m_joinTimer = 0.5f;
        } else if (m_joinTimer <= 0.0f) {
            if (canSeal && !m_keyed && !m_legacyJoin) {
                m_legacyJoin = true;   // an older server: it never answered the key hello
                OKAY_WARN("net: ", m_serverEp.ToString(), " doesn't do the sealed join; joining in the clear");
            }
            net::Packet p(Join);
            p.Write(m_localName);
            p.Write(m_localRoom);
            p.Write(password);          // matched against the server's password
            // The identity token the server may verify: only to a host that asked for it.
            p.Write((authTokenToAnyHost || m_hostWantsAuth) ? m_authToken : std::string());
            {   // our encryption public key ("" if encryption is off / unavailable, or
                // already traded in the sealed join)
                auto pk = (encryption && !m_keyed ? m_secure.PublicKey() : std::vector<std::uint8_t>{});
                p.Write(std::string(pk.begin(), pk.end()));
            }
            if (m_keyed) p.Write(kJoinVersion);
            if (m_resumeToken) WriteU64(p, m_resumeToken);   // rejoining: take our old state back
            // Step 2 of the sealed join goes encrypted; the plaintext Join is pre-handshake.
            SendDatagram(m_serverEp, p.Data(), p.Size(), /*allowEncrypt=*/m_keyed);
            m_joinTimer = 0.5f;
        }
    } else {
        // Ping the server about once a second to measure round-trip time.
        m_pingTimer -= dt;
        if (m_pingTimer <= 0.0f) {
            net::Packet p(Ping); p.Write(m_clock);
            SendDatagram(m_serverEp, p.Data(), p.Size());
            NotePingSent(m_clock);
            m_pingTimer = 1.0f;
        }
        ResendReliable(m_relOut, m_serverEp, dt);   // keep retrying unacked messages
        {
            std::vector<std::vector<std::uint8_t>> due;
            m_toServer.ordered.CollectResend(dt, due);
            for (const auto& bytes : due) SendDatagram(m_serverEp, bytes.data(), bytes.size());
        }
    }

    PruneFragments(dt);
    static thread_local std::vector<std::uint8_t> buf(net::kMaxDatagram);
    net::Endpoint from;
    int n;
    bool heard = false;   // any datagram from our server this tick
    while ((n = ReceiveFrom(buf.data(), buf.size(), from)) > 0) {
        if (!(from == m_serverEp)) continue;   // only our server talks to us
        heard = true;
        const std::uint8_t* data = buf.data();
        std::size_t len = static_cast<std::size_t>(n);
        std::vector<std::uint8_t> assembled;
        if (len >= 1 && data[0] == Fragment) {
            if (!TakeFragment(from, data, len, assembled)) continue;
            data = assembled.data(); len = assembled.size();
        }
        std::vector<std::uint8_t> decrypted;
        const bool sealed = len >= 1 && data[0] == kEncEnvelope;
        if (sealed) {
            net::SecureChannel* s = SessionFor(from);
            if (!s || !s->Open(data + 1, len - 1, decrypted)) continue;
            data = decrypted.data(); len = decrypted.size();
        }
        net::Packet p(data, len);
        std::uint8_t type = p.ReadU8();
        // Once our session is keyed the server seals everything: plaintext now
        // is someone else's (spoofing the server's address).
        if (!sealed && m_secure.Ready()) continue;
        // In a sealed join (not the clear join an older or unencrypted host
        // gets), the server's Welcome, Reject and Kicked come sealed under the
        // JoinKey session; before that only its JoinKey and QueryInfo are in the
        // clear. A plaintext one is a forgery that would skip the pin or end the
        // join.
        if (!sealed && encryption && net::SecureChannel::Available() && !m_legacyJoin &&
            type != JoinKey && type != QueryInfo) continue;
        if (type == QueryInfo) {
            // The host's answer to "do you check sign-ins?" (before the join).
            if (m_authDecided || p.ReadString() != net::kQueryMagic || p.ReadU32() != m_authNonce) continue;
            p.ReadU8(); p.ReadString(); p.ReadString(); p.ReadString(); p.ReadU32(); p.ReadU32();
            const std::uint8_t flags = p.ReadU8();
            if (!p.Ok()) continue;
            m_hostWantsAuth = (flags & net::kQueryAuth) != 0;
            m_authDecided = true;
            m_joinTimer = 0.0f;
            continue;
        }
        if (type == JoinKey) {
            // Sealed join, step 1's answer: the server's key (signed, if it has
            // an identity). Step 2, the sealed Join, goes on the next tick.
            if (m_joined || m_keyed || m_legacyJoin || !encryption) continue;
            const std::uint8_t version = p.ReadU8();
            const std::string serverPk = p.ReadString(), sig = p.ReadString();
            if (!p.Ok() || version < kJoinVersion) continue;
            if (serverPk.empty()) {
                // The server has encryption off.
                if (!pinnedHostKey.empty()) {
                    if (!m_pinWarned) OKAY_ERROR("net: the server has encryption off, so its pinned key can't be checked; not joining");
                    m_pinWarned = true;
                    m_joinRejected = true;
                    m_joinRejectReason = "the server has encryption off, so its pinned key can't be checked";
                    m_joinTimer = 1e9f;   // stop retrying
                    continue;
                }
                m_legacyJoin = true;
                m_joinTimer = 0.0f;
                continue;
            }
            const auto mine = m_secure.PublicKey();
            if (!pinnedHostKey.empty()) {
                std::vector<std::uint8_t> pin;
                okayac::ed25519::PublicKey pk{};
                okayac::ed25519::Signature sg{};
                const bool shaped = net::SessionTicket::Base64UrlDecode(pinnedHostKey, pin) && pin.size() == pk.size() &&
                                    sig.size() == sg.size();
                if (shaped) { std::copy(pin.begin(), pin.end(), pk.begin()); std::copy(sig.begin(), sig.end(), sg.begin()); }
                if (!shaped || !okayac::ed25519::Verify(JoinKeyMessage(serverPk, std::string(mine.begin(), mine.end())), sg, pk)) {
                    // Not the pinned host. Stop: waiting forever looks like a hang.
                    if (!m_pinWarned) OKAY_WARN("net: ", m_serverEp.ToString(), " didn't prove the pinned host key");
                    m_pinWarned = true;
                    m_joinRejected = true;
                    m_joinRejectReason = "server identity doesn't match the pinned key";
                    m_joinTimer = 1e9f;
                    continue;
                }
            }
            if (!m_secure.Establish(std::vector<std::uint8_t>(serverPk.begin(), serverPk.end()), /*asServer=*/false))
                continue;
            m_secure.SetSequenced(true);
            m_secure.SetAssociatedData({kEncEnvelope, kJoinVersion});
            m_keyed = true;
            m_joinTimer = 0.0f;
            continue;
        }
        if (type == Welcome) {
            m_localId = p.ReadU32();
            m_serverName = p.ReadString();
            m_localUserId = p.ReadString();   // our verified identity ("" if anon)
            std::string serverPk = p.ReadString();   // server's encryption public key
            // Only the clear join keys its session from the Welcome (an older
            // host; never with a pin, which needs the sealed join).
            if (m_legacyJoin && pinnedHostKey.empty() && !serverPk.empty() && !m_secure.Ready())
                m_secure.Establish(std::vector<std::uint8_t>(serverPk.begin(), serverPk.end()),
                                   /*asServer=*/false);
            const std::uint8_t welcomeFlags = p.ReadU8();   // optional: absent from older servers
            if (p.Ok() && (welcomeFlags & 1) && m_secure.Ready()) m_secure.SetSequenced(true);
            if (p.Ok() && (welcomeFlags & 2)) {   // the host runs a fixed net tick
                const float rate = p.ReadF32();
                if (p.Ok() && std::isfinite(rate) && rate >= 1.0f && rate <= 240.0f) m_hostTickRate = rate;
            }
            if (p.Ok() && (welcomeFlags & 16)) {   // an unencrypted session's cookie, for what we send
                const std::uint64_t cookie = ReadU64(p);
                if (p.Ok()) m_sessionCookie = cookie;
            }
            if (p.Ok() && (welcomeFlags & 32)) {   // a token to resume with if we drop
                const std::uint64_t t = ReadU64(p);
                if (p.Ok()) m_resumeToken = t;
            }
            m_resumed = p.Ok() && (welcomeFlags & 64) != 0;
            if (p.Ok() && (welcomeFlags & 4) && m_hostTickRate > 0.0f) m_hostAuthoritative = true;
            m_hostAcksPawn = m_hostAuthoritative && (welcomeFlags & 8);   // an older 5b host doesn't: follow it instead
            m_joined = true;
            m_joinRejected = false;
            m_joinRejectReason.clear();
            OKAY_INFO("net: joined '", m_serverName, "' as peer ", m_localId);
        } else if (type == Reject) {
            std::string why = p.ReadString();
            if (!m_joined && !encryption && net::SecureChannel::Available() && why.rfind(kNeedsEncryption, 0) == 0) {
                // The host only takes encrypted players: join again, sealed.
                encryption = true;
                m_secure.GenerateKeys();
                m_joinTimer = 0.0f;
                OKAY_INFO("net: ", m_serverEp.ToString(), " requires encryption; joining encrypted");
                continue;
            }
            m_joinRejected = true;
            m_joinRejectReason = why;
            m_joinTimer = 1e9f;      // stop retrying
            OKAY_WARN("net: join refused: ", why);
        } else if (type == Kicked) {
            m_kickReason = p.ReadString();
            m_kicked = true;
            OKAY_WARN("net: kicked by server: ", m_kickReason);
            const bool wasTrying = m_reconnecting;
            Stop();
            if (wasTrying) { m_reconnectGaveUp = true; if (m_reconnectHandler) m_reconnectHandler(ReconnectEvent::GaveUp, m_reconnectAttempt); }
            return;
        } else if (type == ReliableMsg) {
            std::uint32_t seq = p.ReadU32();
            std::string channel = p.ReadString();
            std::string data    = p.ReadString();
            net::Packet ack(ReliableAck); ack.Write(seq);
            SendDatagram(m_serverEp, ack.Data(), ack.Size());
            if (m_relSeen.Accept(seq)) Deliver(0, channel, data);
        } else if (type == ReliableAck) {
            m_relOut.erase(p.ReadU32());
        } else if (type == net::kSnapDelta) {
            OnSnapDelta(m_snapDown, data, len, m_serverEp, nullptr);
        } else if (type == net::kSnapAck) {
            std::uint32_t id = 0;
            if (net::DecodeSnapAck(data, len, id)) NoteSnapAck(m_snapUp, id);
        } else if (type == net::kChanRel) {
            OnOrdered(m_toServer, m_serverEp, 0, nullptr, p, len);
        } else if (type == net::kChanRelAck) {
            OnOrderedAck(m_toServer, p);
        } else if (type == net::kChanSeq) {
            OnSequencedFromServer(p);
        } else if (type == Snapshot) {
            std::uint32_t count = p.ReadU32();
            for (std::uint32_t i = 0; i < count && p.Ok(); ++i) {
                PeerState s;
                s.id = p.ReadU32();
                s.x = p.ReadF32(); s.y = p.ReadF32(); s.z = p.ReadF32();
                s.glyph = static_cast<char>(p.ReadU8());
                ApplyPeer(s);
            }
            if (m_hostTickRate > 0.0f) {   // the host's tick (trailing)
                const std::uint32_t tick = p.ReadU32();
                // Snapshots can arrive out of order: only move forward.
                // Forward only (snapshots arrive out of order), unless we're more
                // than a second ahead of the host: then start again from its tick.
                if (p.Ok() && (tick > m_netTick || (double)m_netTick - (double)tick > m_hostTickRate)) {
                    // Moving forward keeps the fraction of the tick already
                    // accumulated, so a snapshot doesn't reset the send phase.
                    // More than a second ahead of the host: start again from its tick.
                    const bool resync = (double)m_netTick - (double)tick > (double)m_hostTickRate;
                    m_netTick = tick;
                    if (resync) m_tickAccum = 0.0f;
                }
                if (m_hostAuthoritative) {   // the newest input the host applied for us
                    const std::uint32_t acked = p.ReadU32();
                    if (p.Ok() && acked > m_lastAckedSeq) m_lastAckedSeq = acked;
                }
            }
        } else if (type == Message) {
            std::uint32_t sender = p.ReadU32();
            std::string channel = p.ReadString();
            std::string data    = p.ReadString();
            if (sender != m_localId) Deliver(sender, channel, data);
        } else if (type == SyncVar) {
            std::string key = p.ReadString();
            std::string value = p.ReadString();
            net::NoteObject(key, key.size() + value.size(), true);
            ApplySyncVar(key, value);
        } else if (type == Correct) {
            // The server rejected a move (or teleported us): go where it says.
            const float x = p.ReadF32(), y = p.ReadF32(), z = p.ReadF32();
            if (p.Ok() && m_localAvatar) m_localAvatar->localPosition = Vec3(x, y, z);
            ++m_corrections;
        } else if (type == PawnAck) {
            const std::string pawn = p.ReadString();
            const std::uint32_t seq = p.ReadU32();
            const float x = p.ReadF32(), y = p.ReadF32(), z = p.ReadF32();
            if (p.Ok() && m_hostAcksPawn && pawn == m_pawnId) Reconcile(seq, Vec3(x, y, z));
        } else if (type == RosterMsg) {
            const std::uint32_t n = p.ReadU32();
            std::vector<RosterEntry> list;
            for (std::uint32_t i = 0; i < n && i < 1024 && p.Ok(); ++i) {
                RosterEntry e; e.id = p.ReadU32(); e.name = p.ReadString(); e.userId = p.ReadString();
                e.score = p.ReadI32(); e.seconds = p.ReadF32();
                list.push_back(e);
            }
            if (p.Ok()) {
                m_roster = std::move(list);
                // A player who left is no longer in the roster the host sends every second:
                // its stand-in goes too (after two rosters without it, so one that arrives
                // late and out of order can't remove somebody who just joined).
                for (auto r = m_remotes.begin(); r != m_remotes.end();) {
                    bool listed = false;
                    for (const auto& e : m_roster) if (e.id == r->first) { listed = true; break; }
                    if (listed) { m_remoteMissing.erase(r->first); ++r; continue; }
                    if (++m_remoteMissing[r->first] < 2) { ++r; continue; }
                    if (GetScene() && r->second) GetScene()->Destroy(r->second);
                    m_remoteTarget.erase(r->first);
                    m_remoteMissing.erase(r->first);
                    r = m_remotes.erase(r);
                }
            }
        } else if (type == ClockPing) {
            // The server's clock heartbeat: answer with our game clock.
            const std::uint64_t nonce = ReadU64(p);
            const std::uint32_t seq = p.ReadU32();
            const std::string key = p.ReadString();
            if (!p.Ok() || key.empty() || key.size() > 128) continue;
            const auto ms = static_cast<std::uint64_t>(m_gameClockMs);
            net::Packet r(ClockPong);
            WriteU64(r, nonce); r.Write(seq); WriteU64(r, ms);
            r.Write(net::ClockCheck::Answer(key, nonce, seq, ms));
            SendDatagram(m_serverEp, r.Data(), r.Size());
        } else if (type == Pong) {
            float stamp = p.ReadF32();
            NotePong(stamp);
            m_rtt = (m_clock - stamp) * 1000.0f;   // round-trip in ms
            if (m_rtt < 0.0f) m_rtt = 0.0f;
        }
    }
    if (m_relayLost) {   // host gone or changed, or refused (RelayError())
        if (m_reconnectTrying) { FailReconnectTry(); return; }
        if (m_joined && autoReconnect && m_lastConn.kind) {
            m_connectionLost = true;
            m_connectionLostReason = m_relayError.empty() ? "the relay session ended" : m_relayError;
            BeginReconnect();
            return;
        }
        Stop();
        return;
    }

    // A server that stops answering ends the session instead of leaving a
    // frozen world; the join gives up the same way. A hitch on this machine
    // doesn't count: datagrams queued meanwhile are read above first.
    if (heard) {
        m_serverSilence = 0.0f;
        m_joinWait = 0.0f;
    } else if (m_joined) {
        m_serverSilence += dt;
        if (serverTimeoutSeconds > 0.0f && m_serverSilence >= serverTimeoutSeconds) {
            m_connectionLost = true;
            m_connectionLostReason = "the host stopped responding";
            OKAY_WARN("net: no word from ", m_serverEp.ToString(), " for ", serverTimeoutSeconds, " s; leaving the session");
            if (autoReconnect && m_lastConn.kind) BeginReconnect();
            else Stop();
            return;
        }
    } else if (!awaitingRelay && !m_joinRejected) {
        m_joinWait += dt;
        if (joinTimeoutSeconds > 0.0f && m_joinWait >= joinTimeoutSeconds) {
            m_joinRejected = true;
            m_joinRejectReason = "the host did not answer";
            m_joinTimer = 1e9f;   // stop retrying
            OKAY_WARN("net: no answer from ", m_serverEp.ToString(), " after ", joinTimeoutSeconds, " s");
        }
    }

    if (m_reconnectTrying) {
        if (m_joined) {
            m_reconnecting = false; m_reconnectTrying = false;
            m_connectionLost = false; m_connectionLostReason.clear();
            ++m_reconnectCount;
            OKAY_INFO("net: rejoined '", m_serverName, "' as peer ", m_localId, m_resumed ? " (state resumed)" : "");
            if (m_wasReady) SetReady(true);
            ReannounceOwnSpawns();
            if (m_resumed) ResendUnacked(0, m_resendQ);   // what the host had not acked, after the spawns
            else { m_resendDropped += m_resendQ.size(); m_resendQ.clear(); }   // a new peer: not addressed to it
            if (m_reconnectHandler) m_reconnectHandler(ReconnectEvent::Restored, m_reconnectAttempt);
        } else if (m_joinRejected) {
            FailReconnectTry();
            return;
        }
    }

    // Authoritative movement: sample this frame's input for the next tick's
    // command (a press between ticks still counts).
    m_predicting = false;
    if (m_hostAuthoritative && m_joined) {
        PlayerInput f;
        bool have = false;
        auto sync = m_syncObjs.find(m_pawnId);
        Transform* pawnT = sync != m_syncObjs.end() ? sync->second.t : nullptr;
        IPlayerSim* sim = pawnT ? FindPlayerSim(pawnT->gameObject) : nullptr;
        if (m_inputSource) { f = m_inputSource(); have = true; }
        else if (sim) { f = sim->ReadLocalInput(); have = true; }
        if (have) { m_heldInput = f; m_pressedAccum |= f.pressed; }
        // Prediction: move our own player here, through the same Simulate the
        // host will run, with the input we last sent (so the two move the
        // same way: a key let go between ticks would otherwise move us a frame
        // further than the host and pull us back), but this frame's look; then
        // ease in what's left of any correction.
        if (have && sim && m_hostAcksPawn) {
            m_predicting = true;
            PlayerInput local = m_predCmd;
            local.yaw = f.yaw; local.pitch = f.pitch;
            local.pressed = m_predPressOnce;   // a press counts once, as on the host
            m_predPressOnce = 0;
            local.Clamp();   // as the host will
            sim->Simulate(local, dt);
            const float k = reconcileRate > 0.0f ? 1.0f - std::exp(-reconcileRate * dt) : 1.0f;
            const Vec3 step = m_predPending * k;
            pawnT->localPosition = pawnT->localPosition + step;
            m_predPending = m_predPending - step;
            sim->OnCorrected(step);
        }
    }
    // Send our local state to the server: once a frame, or once a tick when
    // the host runs a fixed net tick (with our estimate of that tick).
    const bool sendNow = m_hostTickRate > 0.0f ? (m_joined && StepNetTick(m_hostTickRate, dt) > 0) : true;
    if (m_joined && m_localAvatar && sendNow) {
        Vec3 lp = m_localAvatar->localPosition;
        net::Packet p(State);
        p.Write(m_localId);
        p.Write(lp.x); p.Write(lp.y); p.Write(lp.z);
        p.Write(std::uint8_t(m_localGlyph));
        if (m_hostTickRate > 0.0f) p.Write(m_netTick);   // trailing: an older host stops reading before it
        if (m_sessionCookie) WriteU64(p, m_sessionCookie);
        SendDatagram(m_serverEp, p.Data(), p.Size());
        ++m_statesSent;
    }
}

} // namespace okay
