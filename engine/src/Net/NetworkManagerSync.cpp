// NetworkManager, part: the per-frame Update, spawn, despawn and object sync (snapshots, interpolation), Deliver, chat, synced vars and the message channels and transport (reliable, ordered, sequenced, encryption, fragments).
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

GameObject* NetworkManager::EnsureRemote(std::uint32_t id, char glyph) {
    auto it = m_remotes.find(id);
    if (it != m_remotes.end()) return it->second;
    GameObject* go = m_spawnRemote ? m_spawnRemote(id, glyph) : nullptr;
    if (go) m_remotes[id] = go;
    return go;
}

void NetworkManager::ApplyPeer(const PeerState& s) {
    if (s.id == m_localId) return; // never mirror ourselves
    GameObject* go = EnsureRemote(s.id, s.glyph);
    if (!go || !go->transform) return;
    Vec3 target{s.x, s.y, s.z};
    if (interpolationRate > 0.0f) {
        // Ease toward the latest position over the next frames (set the target;
        // InterpolateRemotes moves there). Snap on the first sample so a freshly
        // spawned remote doesn't slide in from the origin.
        if (m_remoteTarget.find(s.id) == m_remoteTarget.end())
            go->transform->localPosition = target;
        m_remoteTarget[s.id] = target;
    } else {
        go->transform->localPosition = target;
    }
}

void NetworkManager::InterpolateRemotes(float dt) {
    if (interpolationRate <= 0.0f) return;
    float t = interpolationRate * dt;
    if (t > 1.0f) t = 1.0f;
    for (auto& [id, go] : m_remotes) {
        auto it = m_remoteTarget.find(id);
        if (it == m_remoteTarget.end() || !go || !go->transform) continue;
        Vec3 p = go->transform->localPosition;
        go->transform->localPosition = p + (it->second - p) * t;
    }
}

void NetworkManager::RegisterSync(const std::string& id, Transform* t, bool owned) {
    if (id.empty()) return;
    SyncObj o; o.t = t; o.owned = owned;
    if (t) { o.targetPos = t->localPosition; o.targetRot = t->localRotation; }
    m_syncObjs[id] = o;
    if (t) NoteSpawnedPawn(id, t->gameObject);
}

void NetworkManager::UnregisterSync(const std::string& id) {
    ReleasePawn(id);   // scene destroy: the component is going, and so is the claim
    m_syncObjs.erase(id);
}

void NetworkManager::SetSyncOwned(const std::string& id, bool owned) {
    auto it = m_syncObjs.find(id);
    if (it != m_syncObjs.end()) it->second.owned = owned;
}

void NetworkManager::SetSyncExtra(const std::string& id, std::function<std::string()> get,
                                  std::function<void(const std::string&)> set) {
    auto it = m_syncObjs.find(id);
    if (it == m_syncObjs.end()) return;
    it->second.getExtra = std::move(get);
    it->second.setExtra = std::move(set);
}

void NetworkManager::ApplyRemoteXform(const std::string& id, const Vec3& p, const Quat& q, double tick,
                                      const std::string& extra, bool hasExtra) {
    auto it = m_syncObjs.find(id);
    if (it == m_syncObjs.end() || it->second.owned) return;
    it->second.targetPos = p;
    it->second.targetRot = q;
    if (TickRateInForce() > 0.0f) {
        auto& buf = it->second.samples;
        const double now = m_smoothTick;
        // A sender's clock that's far off ours (or none) is stamped with when
        // it arrived instead.
        if (tick < 0.0 || std::fabs(tick - now) > 2.0 * TickRateInForce()) tick = now;
        auto at = buf.end();
        while (at != buf.begin() && (at - 1)->tick > tick) --at;
        const bool newest = at == buf.end();
        double gap = (newest && !buf.empty()) ? tick - buf.back().tick : 0.0;
        // An unchanged state isn't sent again, so a gap well past the send
        // spacing is the object holding still, not the network: it stays where
        // it was until one spacing before this state (instead of creeping
        // toward it across the whole pause), and the pause doesn't grow the
        // delay (it read as a full second of jitter and the buffer settled
        // near that, ~860 ms behind).
        const double spacing = syncRate > 0.0f ? (double)TickRateInForce() / (double)syncRate : 1.0;
        if (gap > 2.5 * spacing) {
            SyncObj::Sample hold = buf.back();
            hold.tick = tick - spacing;
            buf.push_back(hold);
            at = buf.end();
            gap = spacing;
        }
        if (at == buf.begin() || (at - 1)->tick < tick) buf.insert(at, SyncObj::Sample{tick, p, q});
        if (buf.size() > kMaxSamples) buf.erase(buf.begin());
        if (newest) {
            const double need = std::min((now - tick) + gap + 1.0, (double)TickRateInForce());
            if (need > m_interpNeed) m_interpNeed = need;
        }
    }
    // Snap on the first sample so a freshly seen object doesn't slide in.
    if (!it->second.hasTarget && it->second.t) {
        it->second.t->localPosition = p;
        it->second.t->localRotation = q;
    }
    it->second.hasTarget = true;
    if (it->second.setExtra && hasExtra) it->second.setExtra(extra);
}

void NetworkManager::SendSnap(net::SnapOut& out, const net::DeltaBase& current, double tick,
                              const net::Endpoint& to, std::uint64_t cookie) {
    auto stamp = [&](std::vector<std::uint8_t>& bytes) {
        if (!cookie) return;
        const std::uint32_t lo = static_cast<std::uint32_t>(cookie);
        const std::uint32_t hi = static_cast<std::uint32_t>(cookie >> 32);
        bytes.push_back(static_cast<std::uint8_t>(lo));
        bytes.push_back(static_cast<std::uint8_t>(lo >> 8));
        bytes.push_back(static_cast<std::uint8_t>(lo >> 16));
        bytes.push_back(static_cast<std::uint8_t>(lo >> 24));
        bytes.push_back(static_cast<std::uint8_t>(hi));
        bytes.push_back(static_cast<std::uint8_t>(hi >> 8));
        bytes.push_back(static_cast<std::uint8_t>(hi >> 16));
        bytes.push_back(static_cast<std::uint8_t>(hi >> 24));
    };
    if (net::SameBase(out.acked, current)) {
        if (out.waiting && !out.bytes.empty()) SendDatagram(to, out.bytes.data(), out.bytes.size());
        return;
    }
    const std::uint32_t id = out.next++;
    auto blob = net::EncodeSnapshot(out.acked, current, id, tick);
    if (blob.empty()) { --out.next; return; }
    stamp(blob);
    out.pending = current;
    out.pending.id = id;
    out.bytes = std::move(blob);
    out.waiting = true;
    SendDatagram(to, out.bytes.data(), out.bytes.size());
}

void NetworkManager::NoteSnapAck(net::SnapOut& out, std::uint32_t id) {
    if (out.waiting && id == out.pending.id) {
        out.acked = out.pending;
        out.waiting = false;
    }
}

void NetworkManager::CullInterest(net::DeltaBase& mine, const Client& c) const {
    if (interestRadius <= 0.0f) return;
    // Same hold as the avatar snapshot: no position yet, so send no objects.
    if (!c.hasState) { mine.pose.clear(); mine.extra.clear(); return; }
    const float cell = interestRadius;
    const float r2 = cell * cell;
    const int cx = static_cast<int>(std::floor(c.state.x / cell));
    const int cy = static_cast<int>(std::floor(c.state.y / cell));
    const int cz = static_cast<int>(std::floor(c.state.z / cell));
    for (auto it = mine.pose.begin(); it != mine.pose.end();) {
        Vec3 p; Quat q;
        net::DequantizePose(it->second, p, q);
        const int ox = static_cast<int>(std::floor(p.x / cell));
        const int oy = static_cast<int>(std::floor(p.y / cell));
        const int oz = static_cast<int>(std::floor(p.z / cell));
        const int cheb = std::max(std::abs(ox - cx), std::max(std::abs(oy - cy), std::abs(oz - cz)));
        const float dx = p.x - c.state.x, dy = p.y - c.state.y, dz = p.z - c.state.z;
        if (cheb > 1 || dx * dx + dy * dy + dz * dz > r2) {
            mine.extra.erase(it->first);
            it = mine.pose.erase(it);
        } else ++it;
    }
}


void NetworkManager::BroadcastBinarySnapshots(double tick) {
    net::DeltaBase owned;
    for (auto& [id, o] : m_syncObjs) {
        if (!o.owned || !o.t || id.empty() || id.size() > 64) continue;
        owned.pose[id] = net::QuantizePose(o.t->localPosition, o.t->localRotation);
        if (o.getExtra) {
            std::string extra = o.getExtra();
            if (!extra.empty()) owned.extra[id] = std::move(extra);
        }
    }
    if (m_mode == Mode::Server) {
        for (auto it = m_relaySnap.begin(); it != m_relaySnap.end();)
            it = m_syncObjs.count(it->first) ? std::next(it) : m_relaySnap.erase(it);
        for (auto& [ep, c] : m_clients) {
            if (c.room != m_localRoom) continue;
            net::DeltaBase mine = owned;
            for (const auto& [id, rel] : m_relaySnap) {
                if (rel.room != c.room || mine.pose.count(id)) continue;
                mine.pose[id] = rel.pose;
                if (!rel.extra.empty()) mine.extra[id] = rel.extra;
            }
            CullInterest(mine, c);
            SendSnap(c.snapOut, mine, tick, c.endpoint, 0);
        }
    } else if (m_mode == Mode::Client && m_joined) {
        SendSnap(m_snapUp, owned, tick, m_serverEp, m_sessionCookie);
    }
}

void NetworkManager::OnSnapDelta(net::SnapIn& in, const std::uint8_t* data, std::size_t size,
                                 const net::Endpoint& ackTo, Client* sender) {
    std::uint32_t baselineId = 0, snapId = 0;
    if (!net::PeekSnapshot(data, size, baselineId, snapId)) return;
    auto ack = [&](std::uint32_t id) {
        if (!id) return;
        auto bytes = net::EncodeSnapAck(id);
        // A client acks the server with the session cookie. The server's ack does not.
        if (!sender && m_sessionCookie) {
            const std::uint32_t lo = static_cast<std::uint32_t>(m_sessionCookie);
            const std::uint32_t hi = static_cast<std::uint32_t>(m_sessionCookie >> 32);
            bytes.push_back(static_cast<std::uint8_t>(lo));
            bytes.push_back(static_cast<std::uint8_t>(lo >> 8));
            bytes.push_back(static_cast<std::uint8_t>(lo >> 16));
            bytes.push_back(static_cast<std::uint8_t>(lo >> 24));
            bytes.push_back(static_cast<std::uint8_t>(hi));
            bytes.push_back(static_cast<std::uint8_t>(hi >> 8));
            bytes.push_back(static_cast<std::uint8_t>(hi >> 16));
            bytes.push_back(static_cast<std::uint8_t>(hi >> 24));
        }
        SendDatagram(ackTo, bytes.data(), bytes.size());
    };
    // The cookie, if any, sits after a well-formed snapshot. Checked by the caller
    // before we get here when the packet came from a client.
    if (in.haveLatest && baselineId == in.latestId && baselineId != in.have.id) {
        in.have = in.latest;
    }
    if (baselineId != in.have.id) {
        if (in.haveLatest) ack(in.latestId);
        return;
    }
    if (in.haveLatest && static_cast<std::int32_t>(snapId - in.latestId) <= 0) {
        ack(in.latestId);
        return;
    }
    double tick = -1.0;
    std::vector<net::SnapChange> changes;
    if (!net::DecodeSnapshot(data, size, in.have, baselineId, snapId, tick, changes)) return;
    const net::Endpoint fromEp = sender ? sender->endpoint : net::Endpoint{};
    bool refused = false;
    net::DeltaBase merged = in.have;
    merged.id = snapId;
    for (const net::SnapChange& c : changes) {
        Vec3 p; Quat q;
        net::DequantizePose(c.pose, p, q);
        if (sender) {
            if (!ClientMaySendXform(*sender, c.id)) { refused = true; continue; }
            std::ostringstream os;
            os << c.id << '|' << p.x << ' ' << p.y << ' ' << p.z << ' '
               << q.x << ' ' << q.y << ' ' << q.z << ' ' << q.w;
            const std::string wire = os.str();
            if (!AntiCheatAllows(sender->id, "__xform", wire)) {
                refused = true;
                if (m_clients.find(fromEp) == m_clients.end()) return;   // a kick erased it
                continue;
            }
        }
        merged.pose[c.id] = c.pose;
        if (c.hasExtra) merged.extra[c.id] = c.extra;
        else if (auto old = in.have.extra.find(c.id); old != in.have.extra.end()) merged.extra[c.id] = old->second;
        ApplyRemoteXform(c.id, p, q, tick, c.extra, c.hasExtra);
        if (sender) m_relaySnap[c.id] = RelayPose{c.pose, c.hasExtra ? c.extra : merged.extra[c.id], sender->room};
    }
    if (refused) return;
    in.latest = std::move(merged);
    in.latestId = snapId;
    in.haveLatest = true;
    ack(snapId);
}

void NetworkManager::SyncTick(float dt) {
    if (m_syncObjs.empty()) return;
    // Move non-owned objects toward their last received state every frame: ease at
    // interpolationRate, or snap exactly when it's <= 0.
    {
        float t = interpolationRate > 0.0f ? interpolationRate * dt : 1.0f;
        if (t > 1.0f) t = 1.0f;
        const bool buffered = InterpolationBuffer();
        AdvanceSmoothTick(dt);
        // The delay follows what the network needs (m_interpNeed) a little at a
        // time, so the shown clock runs slightly slower or faster for a moment
        // instead of jumping; the need eases back to kInterpTicks slowly.
        if (buffered) {
            const double ticks = (double)dt * (double)TickRateInForce();
            m_interpNeed = std::max(kInterpTicks, m_interpNeed - kInterpRecover * ticks);
            const double step = kInterpSlew * ticks;
            if (m_interpDelay < m_interpNeed) m_interpDelay = std::min(m_interpNeed, m_interpDelay + step);
            else m_interpDelay = std::max(m_interpNeed, m_interpDelay - step);
        }
        const double showAt = buffered ? m_smoothTick - m_interpDelay : 0.0;
        for (auto& [id, o] : m_syncObjs) {
            if (o.owned || !o.t || !o.hasTarget) continue;
            if (m_predicting && id == m_pawnId) continue;   // our predicted player: reconciled by PawnAck
            if (buffered && !o.samples.empty()) {
                // Drop what's older than the pair around showAt, then show that moment.
                std::size_t keep = 0;
                while (keep + 1 < o.samples.size() && o.samples[keep + 1].tick <= showAt) ++keep;
                if (keep) o.samples.erase(o.samples.begin(), o.samples.begin() + (std::ptrdiff_t)keep);
                SampleAt(o, showAt, o.t->localPosition, o.t->localRotation);
                continue;
            }
            o.t->localPosition = o.t->localPosition + (o.targetPos - o.t->localPosition) * t;
            o.t->localRotation = Quat::Slerp(o.t->localRotation, o.targetRot, t);
        }
    }
    // Broadcast the objects we own at the sync rate.
    if (!IsConnected()) return;
    float interval = syncRate > 0.0f ? 1.0f / syncRate : 0.0f;
    m_syncTimer += dt;
    if (m_syncTimer < interval) return;
    m_syncTimer = 0.0f;
    if (binarySnapshots) {
        BroadcastBinarySnapshots(TickRateInForce() > 0.0f ? m_smoothTick : -1.0);
        return;
    }
    for (auto& [id, o] : m_syncObjs) {
        if (!o.owned || !o.t) continue;
        const Vec3& p = o.t->localPosition;
        const Quat& q = o.t->localRotation;
        std::ostringstream os;
        os << id << '|' << p.x << ' ' << p.y << ' ' << p.z << ' '
           << q.x << ' ' << q.y << ' ' << q.z << ' ' << q.w;
        // With a fixed tick: when this state is from (the sender's tick, with
        // its fraction), for receivers' interpolation buffers. Older receivers
        // read the seven numbers and stop.
        if (TickRateInForce() > 0.0f) os << ' ' << std::setprecision(10) << m_smoothTick << std::setprecision(6);
        if (o.getExtra) os << '|' << o.getExtra();   // optional per-object state
        Send("__xform", os.str());
    }
}

double NetworkManager::TickTime() const {
    const float rate = TickRateInForce();
    return rate > 0.0f ? (double)m_netTick + (double)m_tickAccum * (double)rate : 0.0;
}

// A tick clock that runs at the frame rate and is pulled gently toward the
// tick (a client's estimate jumps when a snapshot resyncs it): stamps and
// shows states without that jitter. Far off (a start, a stall): it jumps.
void NetworkManager::AdvanceSmoothTick(float dt) {
    const float rate = TickRateInForce();
    if (rate <= 0.0f) { m_smoothTickInit = false; return; }
    const double target = TickTime();
    if (!m_smoothTickInit || std::fabs(target - m_smoothTick) > 4.0) {
        m_smoothTick = target; m_smoothTickInit = true; return;
    }
    m_smoothTick += (double)dt * (double)rate;
    m_smoothTick += (target - m_smoothTick) * 0.05;
}

// The state at `tick`, between the two buffered samples either side of it;
// before the first, the first; after the last, the last (held, not guessed).
void NetworkManager::SampleAt(const SyncObj& o, double tick, Vec3& pos, Quat& rot) { PoseAt(o.samples, tick, pos, rot); }

void NetworkManager::PoseAt(const std::vector<SyncObj::Sample>& b, double tick, Vec3& pos, Quat& rot) {
    if (tick <= b.front().tick) { pos = b.front().pos; rot = b.front().rot; return; }
    for (std::size_t i = 0; i + 1 < b.size(); ++i)
        if (tick < b[i + 1].tick) {
            const float k = (float)((tick - b[i].tick) / (b[i + 1].tick - b[i].tick));
            pos = b[i].pos + (b[i + 1].pos - b[i].pos) * k;
            rot = Quat::Slerp(b[i].rot, b[i + 1].rot, k);
            return;
        }
    pos = b.back().pos; rot = b.back().rot;
}

namespace {
// "__xform" after the id: "px py pz qx qy qz qw", an optional tick, then an
// optional "|extra". All 7 pose floats must parse and be finite, and the
// quaternion must have a length, before it is stored as a unit quaternion.
bool ParseXformPose(const std::string& data, std::string& id, Vec3& p, Quat& q,
                    std::string& extra, double& tick) {
    const auto bar = data.find('|');
    if (bar == std::string::npos) return false;
    id = data.substr(0, bar);
    std::string rest = data.substr(bar + 1);
    extra.clear();
    const auto bar2 = rest.find('|');
    if (bar2 != std::string::npos) { extra = rest.substr(bar2 + 1); rest.resize(bar2); }
    std::stringstream ss(rest);
    if (!(ss >> p.x >> p.y >> p.z >> q.x >> q.y >> q.z >> q.w)) return false;
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return false;
    if (!std::isfinite(q.x) || !std::isfinite(q.y) || !std::isfinite(q.z) || !std::isfinite(q.w)) return false;
    const float len = q.Magnitude();
    if (!(len > 1e-6f)) return false;
    q.x /= len; q.y /= len; q.z /= len; q.w /= len;
    tick = -1.0;
    if (!(ss >> tick) || !std::isfinite(tick)) tick = -1.0;
    return true;
}
} // namespace

void NetworkManager::Update(float dt) {
    net::PumpSimulator(m_socket);
    net::ProfileAdvance(dt);
    m_buildSync->Tick(GetScene(), dt);
    m_questSync->Tick(GetScene(), dt);
    if (m_mode == Mode::Server) ServerTick(dt);
    else if (m_mode == Mode::Client) ClientTick(dt);
    else if (m_reconnecting) ReconnectTick(dt);
    InterpolateRemotes(dt);
    SyncTick(dt);
    PlayersTick(dt);
    ErrorsTick();
}

void NetworkManager::Deliver(std::uint32_t from, const std::string& channel,
                             const std::string& data) {
    net::NoteObject(net::ObjectKey(channel, data), channel.size() + data.size(), true);
    if (channel.compare(0, 6, "__req/") == 0) return;   // a request is the server's to handle, never a message
    if (channel == "__bld") {   // build mode: what players built, from the server
        if (m_mode == Mode::Client && from == 0) m_buildSync->OnServerMessage(data);
        return;
    }
    if (channel == "__qst") {   // quests and story variables, from the server
        if (m_mode == Mode::Client && from == 0) m_questSync->OnServerMessage(data);
        return;
    }
    if (channel == "__life") {   // the server's ordered death / respawn / health stream: "D|R|H" + the old message
        if (m_mode != Mode::Client || from != 0 || data.size() < 2) return;
        const char* old = data[0] == 'D' ? "__death" : data[0] == 'R' ? "__respawn" : data[0] == 'H' ? "__dmg" : nullptr;
        if (old) Deliver(from, old, data.substr(1));
        return;
    }
    // The reserved "__spawn" channel instantiates a prefab on this peer instead
    // of surfacing as a user message (replicated object creation).
    if (channel == "__match_start") {   // lobby: the match has begun for this room
        m_matchStarted = true;
        return;
    }
    if (channel == "__spawn") {
        Scene* s = GetScene();
        if (!s) return;
        // "<path>|x|y|z[|<netId>]"; an optional netId marks a SpawnOwned object:
        // attach a follower NetworkSync so its transform/animation track the
        // authority that spawned it. (The same parser the anti-cheat checks with.)
        const SpawnMessage m = ParseSpawnMessage(data);
        // The server checked a client's spawn before relaying it; a client
        // checks what the host sends, since nothing on the wire is trusted.
        if (m_mode == Mode::Client && !SpawnPathOk(from, m.path, false)) return;
        const std::string& netId = m.netId;
        if (GameObject* g = SceneSerializer::InstantiateFromFile(*s, m.path)) {
            if (g->transform) g->transform->localPosition = m.pos;
            if (!netId.empty()) {
                auto* ns = g->GetComponent<NetworkSync>();
                if (!ns) ns = g->AddComponent<NetworkSync>();
                ns->netId = netId; ns->authority = NetworkSync::Authority::Manual; ns->owned = false;
                // Claim before this object's NetworkSync starts, so a transform the
                // client sends in the same breath as the spawn is already too late.
                NoteSpawnedPawn(netId, g);
            }
        }
        return;
    }
    if (channel == "__dmg") {   // "<netId>|<health>|<damage>|<shooter>": the server applied a hit
        if (m_mode != Mode::Client || from != 0) return;
        const std::size_t a = data.find('|');
        if (a == std::string::npos) return;
        auto obj = m_syncObjs.find(data.substr(0, a));
        if (obj == m_syncObjs.end() || !obj->second.t || !obj->second.t->gameObject) return;
        if (auto* hs = obj->second.t->gameObject->GetComponent<HealthStat>())
            hs->SetHealthFromServer(std::strtof(data.c_str() + a + 1, nullptr));
        return;
    }
    if (channel == "__death") {   // "<hasFrom 0|1>|<x>|<y>|<z>|<netId>": the server killed an object (not by a hit)
        if (m_mode != Mode::Client || from != 0) return;
        float v[4] = {0, 0, 0, 0};
        std::size_t at = 0;
        for (int i = 0; i < 4; ++i) {
            const std::size_t bar = data.find('|', at);
            if (bar == std::string::npos) return;
            const std::string num = data.substr(at, bar - at);
            char* end = nullptr;
            v[i] = std::strtof(num.c_str(), &end);
            if (num.empty() || end != num.c_str() + num.size() || !std::isfinite(v[i])) return;
            at = bar + 1;
        }
        if (v[0] != 0.0f && v[0] != 1.0f) return;
        for (int i = 1; i < 4; ++i) if (std::fabs(v[i]) > 1.0e7f) return;
        auto obj = m_syncObjs.find(data.substr(at));
        if (obj == m_syncObjs.end() || !obj->second.t || !obj->second.t->gameObject) return;
        if (auto* hs = obj->second.t->gameObject->GetComponent<HealthStat>())
            hs->DieFromServer(v[0] == 1.0f, Vec3(v[1], v[2], v[3]));
        return;
    }
    if (channel == "__respawn") {   // "<health>|<x>|<y>|<z>|<netId>": the server respawned an object
        if (m_mode != Mode::Client || from != 0) return;
        float v[4] = {0, 0, 0, 0};
        std::size_t at = 0;
        for (int i = 0; i < 4; ++i) {
            const std::size_t bar = data.find('|', at);
            if (bar == std::string::npos) return;
            const std::string num = data.substr(at, bar - at);
            char* end = nullptr;
            v[i] = std::strtof(num.c_str(), &end);
            if (num.empty() || end != num.c_str() + num.size() || !std::isfinite(v[i])) return;
            at = bar + 1;
        }
        const std::string netId = data.substr(at);
        auto obj = m_syncObjs.find(netId);
        if (netId.empty() || obj == m_syncObjs.end() || !obj->second.t || !obj->second.t->gameObject) return;
        // Positions far outside any world are not a respawn point (the same bound as a snapshot's).
        for (int i = 1; i < 4; ++i) if (std::fabs(v[i]) > 1.0e7f) return;
        const Vec3 pos(v[1], v[2], v[3]);
        SyncObj& o = obj->second;
        GameObject* go = o.t->gameObject;
        if (auto* hs = go->GetComponent<HealthStat>()) hs->RespawnFromServer(v[0]);
        // Stand there now: no glide from where it died, nothing older in the interpolation buffer.
        o.t->SetPosition(pos);
        o.targetPos = pos;
        o.samples.clear();
        o.history.clear();
        if (netId == m_pawnId) ResetPawnPrediction();   // our own pawn: what was predicted is void
        else if (o.owned && o.t == m_localAvatar) m_predPending = Vec3(0.0f, 0.0f, 0.0f);
        return;
    }
    if (channel == "__despawn") {   // remove a SpawnOwned object on this peer
        DestroyLocalSync(data);
        return;
    }
    // Networked transform: "__xform" carries "<id>|px py pz qx qy qz qw". The
    // matching registered object eases toward it (unless we own it — then we're
    // authoritative and ignore remote state).
    if (channel == "__xform") {
        std::string::size_type bar = data.find('|');
        if (bar != std::string::npos) {
            std::string id, extra;
            Vec3 p; Quat q; double tick = -1.0;
            if (!ParseXformPose(data, id, p, q, extra, tick)) return;
            auto it = m_syncObjs.find(id);
            if (it != m_syncObjs.end() && !it->second.owned) {
                const bool hasExtra = data.find('|', bar + 1) != std::string::npos;
                ApplyRemoteXform(id, p, q, tick, extra, hasExtra);
            }
        }
        return;
    }
    // Named network events (the easy layer, NetworkManagerEvents.cpp) and the host's clock.
    if (channel == "__ev") { OnEventWire(from, data); return; }
    if (channel == "__time") { if (m_mode == Mode::Client && from == 0) OnHostTime(data); return; }
    // First-class RPC: "__rpc/<name>" dispatches by name to a registered handler
    // (with the sender id + payload) instead of surfacing as a raw message.
    if (channel.size() > 6 && channel.compare(0, 6, "__rpc/") == 0) {
        const std::string rpcName = channel.substr(6);
        auto it = m_rpcHandlers.find(rpcName);
        if (it != m_rpcHandlers.end() && it->second) it->second(from, data);
        if (m_rpcObserver) m_rpcObserver(rpcName, from, data);
        return;
    }
    // Chat: "__chat" carries "<name>\n<text>"; logged + handed to the chat handler.
    if (channel == "__chat") {
        ChatEntry e{from, "", data};
        std::string::size_type nl = data.find('\n');
        if (nl != std::string::npos) { e.name = data.substr(0, nl); e.text = data.substr(nl + 1); }
        m_chatLog.push_back(e);
        if (chatHistory > 0 && (int)m_chatLog.size() > chatHistory)
            m_chatLog.erase(m_chatLog.begin(), m_chatLog.begin() + ((int)m_chatLog.size() - chatHistory));
        if (m_chatHandler) m_chatHandler(e);
        return;
    }
    NetMessage m{from, channel, data};
    if (m_msgHandler) m_msgHandler(m);
    m_inbox.push_back(std::move(m));
}

void NetworkManager::Chat(const std::string& text) {
    // Show our own line locally first (a broadcast Send never echoes to the sender),
    // then fan it out to everyone else in the room.
    ChatEntry e{m_localId, m_localName, text};
    m_chatLog.push_back(e);
    if (chatHistory > 0 && (int)m_chatLog.size() > chatHistory)
        m_chatLog.erase(m_chatLog.begin(), m_chatLog.begin() + ((int)m_chatLog.size() - chatHistory));
    if (m_chatHandler) m_chatHandler(e);
    Send("__chat", m_localName + "\n" + text);
}

void NetworkManager::Spawn(const std::string& prefabPath, const Vec3& position) {
    // Instantiate locally right away so the spawner sees it without a round trip.
    if (Scene* s = GetScene()) {
        if (GameObject* g = SceneSerializer::InstantiateFromFile(*s, prefabPath))
            if (g->transform) g->transform->localPosition = position;
    }
    // Tell every other peer to make the same object (server relays for clients).
    std::ostringstream os;
    os << prefabPath << '|' << position.x << '|' << position.y << '|' << position.z;
    Send("__spawn", os.str());
}

GameObject* NetworkManager::SpawnOwned(const std::string& prefabPath, const Vec3& position) {
    // A globally-unique id: our peer id namespaces it, so two peers spawning at the
    // same moment never collide.
    std::string id = "sp/" + std::to_string(m_localId) + "/" + std::to_string(++m_spawnCounter);
    GameObject* g = nullptr;
    if (Scene* s = GetScene()) {
        g = SceneSerializer::InstantiateFromFile(*s, prefabPath);
        if (g) {
            if (g->transform) g->transform->localPosition = position;
            // We own it: drive its replication. NetworkSync registers on its first
            // Update (the instance is updated every frame once spawned).
            auto* ns = g->GetComponent<NetworkSync>();
            if (!ns) ns = g->AddComponent<NetworkSync>();
            ns->netId = id; ns->authority = NetworkSync::Authority::Manual; ns->owned = true;
            m_ownSpawns[id] = prefabPath;
        }
    }
    std::ostringstream os;
    os << prefabPath << '|' << position.x << '|' << position.y << '|' << position.z << '|' << id;
    Send("__spawn", os.str());
    return g;
}

void NetworkManager::DespawnPeerObjects(std::uint32_t id) {
    const std::string prefix = "sp/" + std::to_string(id) + "/";
    std::vector<std::string> ids;
    for (const auto& [netId, o] : m_syncObjs) if (netId.compare(0, prefix.size(), prefix) == 0) ids.push_back(netId);
    for (const auto& netId : ids) {
        DestroyLocalSync(netId);
        Send("__despawn", netId);   // and on every client still here
    }
}

void NetworkManager::DestroyLocalSync(const std::string& id) {
    m_ownSpawns.erase(id);
    ReleasePawn(id);   // before the sync entry (and its history) goes
    auto it = m_syncObjs.find(id);
    if (it != m_syncObjs.end()) {
        GameObject* go = it->second.t ? it->second.t->gameObject : nullptr;
        if (go) if (Scene* s = GetScene()) s->Destroy(go);
        m_syncObjs.erase(it);
    }
}

void NetworkManager::Despawn(const std::string& netId) {
    DestroyLocalSync(netId);
    Send("__despawn", netId);
}
void NetworkManager::ApplySyncVar(const std::string& key, const std::string& value) {
    m_syncVars[key] = value;
}

void NetworkManager::SetReady(bool ready) {
    m_ready = ready;
    if (m_mode == Mode::Client && m_joined) {
        net::Packet p(Ready); p.Write(std::uint8_t(ready ? 1 : 0));
        SendDatagram(m_serverEp, p.Data(), p.Size());
    }
}

int NetworkManager::ReadyCount() const {
    int n = 0;
    for (auto& [ep, c] : m_clients) if (c.room == m_localRoom && c.ready) ++n;
    return n;
}

bool NetworkManager::AllReady() const {
    int inRoom = 0, ready = 0;
    for (auto& [ep, c] : m_clients)
        if (c.room == m_localRoom) { ++inRoom; if (c.ready) ++ready; }
    return inRoom > 0 && ready == inRoom;
}

void NetworkManager::StartMatch() {
    m_matchStarted = true;
    Send("__match_start", "1");   // room-scoped broadcast
}

std::string NetworkManager::GetVar(const std::string& key) const {
    auto it = m_syncVars.find(key);
    return it != m_syncVars.end() ? it->second : std::string{};
}

void NetworkManager::SetVar(const std::string& key, const std::string& value) {
    net::NoteObject(key, key.size() + value.size(), false);
    ApplySyncVar(key, value);                 // optimistic / offline local apply
    if (m_mode == Mode::Server) {
        net::Packet p(SyncVar); p.Write(key); p.Write(value);
        for (auto& [ep, c] : m_clients) SendDatagram(c.endpoint, p.Data(), p.Size());
    } else if (m_mode == Mode::Client && m_joined) {
        net::Packet p(SyncVar); p.Write(key); p.Write(value);
        if (m_sessionCookie) WriteU64(p, m_sessionCookie);
        SendDatagram(m_serverEp, p.Data(), p.Size());
    }
}

void NetworkManager::SendTo(std::uint32_t targetId, const std::string& channel,
                            const std::string& data) {
    if (m_mode == Mode::Server) {
        if (targetId == m_localId) { Deliver(m_localId, channel, data); return; }
        for (auto& [ep, c] : m_clients)
            if (c.id == targetId) {
                net::Packet p(Message);
                p.Write(m_localId); p.Write(channel); p.Write(data);
                net::NoteObject(net::ObjectKey(channel, data), channel.size() + data.size(), false);
                SendDatagram(c.endpoint, p.Data(), p.Size());
                return;
            }
    } else if (m_mode == Mode::Client && m_joined) {
        net::NoteObject(net::ObjectKey(channel, data), channel.size() + data.size(), false);
        net::Packet p(DirectMessage);
        p.Write(m_localId); p.Write(targetId); p.Write(channel); p.Write(data);
        SendDatagram(m_serverEp, p.Data(), p.Size());
    }
}

void NetworkManager::SendReliableTo(std::uint32_t targetId, const std::string& channel,
                                    const std::string& data) {
    if (m_mode == Mode::Server) {
        for (auto& [ep, c] : m_clients)
            if (c.id == targetId) {
                std::uint32_t seq = c.relSeq++;
                auto bytes = BuildReliable(seq, channel, data);
                c.relOut[seq] = {bytes, 0.0f};
                net::NoteObject(net::ObjectKey(channel, data), channel.size() + data.size(), false);
                SendDatagram(c.endpoint, bytes.data(), bytes.size());
                return;
            }
    } else if (m_mode == Mode::Client && m_joined) {
        // Clients can only address the server reliably (id 0).
        std::uint32_t seq = m_relSeq++;
        auto bytes = BuildReliable(seq, channel, data);
        m_relOut[seq] = {bytes, 0.0f};
        net::NoteObject(net::ObjectKey(channel, data), channel.size() + data.size(), false);
        SendDatagram(m_serverEp, bytes.data(), bytes.size());
    }
}

void NetworkManager::SendReliable(const std::string& channel, const std::string& data) {
    if (m_mode == Mode::Server) {
        for (auto& [ep, c] : m_clients) SendReliableTo(c.id, channel, data);
    } else {
        SendReliableTo(0, channel, data);
    }
}

void NetworkManager::SendOrderedTo(std::uint32_t targetId, const std::string& channel,
                                   const std::string& data) {
    if (m_mode == Mode::Server) {
        for (auto& [ep, c] : m_clients)
            if (c.id == targetId) {
                auto bytes = c.pipes.ordered.Push(channel, data, 0);
                if (!bytes.empty()) SendDatagram(c.endpoint, bytes.data(), bytes.size());
                return;
            }
    } else if (m_mode == Mode::Client && m_joined && targetId == 0) {
        auto bytes = m_toServer.ordered.Push(channel, data, m_sessionCookie);
        if (!bytes.empty()) SendDatagram(m_serverEp, bytes.data(), bytes.size());
    }
}

void NetworkManager::SendOrdered(const std::string& channel, const std::string& data) {
    if (m_mode == Mode::Server) {
        for (auto& [ep, c] : m_clients) SendOrderedTo(c.id, channel, data);
    } else {
        SendOrderedTo(0, channel, data);
    }
}

void NetworkManager::SendSequencedTo(std::uint32_t targetId, const std::string& channel,
                                     const std::string& data) {
    if (!net::ChannelNameOk(channel) || data.size() > net::kChannelPayloadMax) return;
    if (m_mode == Mode::Server) {
        const std::uint32_t seq = m_seqOrigin.Next(channel);
        if (!seq) return;
        auto bytes = net::BuildChanSeq(m_localId, seq, targetId, channel, data, 0);
        for (auto& [ep, c] : m_clients)
            if (c.id == targetId) {
                SendDatagram(c.endpoint, bytes.data(), bytes.size());
                return;
            }
    } else if (m_mode == Mode::Client && m_joined) {
        // The server routes a directed packet (target 0 is the host), same as SendTo.
        const std::uint32_t seq = m_toServer.sequencedTx.Next(channel);
        if (!seq) return;
        auto bytes = net::BuildChanSeq(m_localId, seq, targetId, channel, data, m_sessionCookie);
        SendDatagram(m_serverEp, bytes.data(), bytes.size());
    }
}

void NetworkManager::SendSequenced(const std::string& channel, const std::string& data) {
    if (!net::ChannelNameOk(channel) || data.size() > net::kChannelPayloadMax) return;
    if (m_mode == Mode::Server) {
        const std::uint32_t seq = m_seqOrigin.Next(channel);
        if (!seq) return;
        auto bytes = net::BuildChanSeq(m_localId, seq, net::kChanBroadcast, channel, data, 0);
        for (auto& [ep, c] : m_clients)
            if (c.room == m_localRoom)
                SendDatagram(c.endpoint, bytes.data(), bytes.size());
    } else if (m_mode == Mode::Client && m_joined) {
        const std::uint32_t seq = m_toServer.sequencedTx.Next(channel);
        if (!seq) return;
        auto bytes = net::BuildChanSeq(m_localId, seq, net::kChanBroadcast, channel, data, m_sessionCookie);
        SendDatagram(m_serverEp, bytes.data(), bytes.size());
    }
}

bool NetworkManager::AllowMessage(Client& c, std::size_t bytes) {
    if (maxMessagesPerSecond > 0 && ++c.msgCount > maxMessagesPerSecond) return false;
    return maxRelayedBytes <= 0 || (c.msgBytes += bytes) <= 4 * (std::size_t)maxRelayedBytes;
}

void NetworkManager::OnOrdered(net::ChannelEnd& link, const net::Endpoint& ackTo, std::uint32_t fromId,
                               Client* sender, net::Packet& p, std::size_t len) {
    const std::uint32_t seq = p.ReadU32();
    std::string channel = p.ReadString();
    std::string data = p.ReadString();
    if (!p.Ok() || !net::ChannelNameOk(channel)) return;
    if (sender && !CookieOk(*sender, p)) return;
    const bool seen = link.ordered.Seen(channel, seq);
    if (sender && !seen && !AllowMessage(*sender, len)) return;
    bool deliver = sender == nullptr;
    if (sender && !seen) {
        if (channel == "__hit") {
            HandleHitClaim(*sender, data);
        } else if (channel.compare(0, 6, "__req/") == 0) {
            deliver = true;   // a request for the server: run in order, below (never relayed or queued)
        } else {
            deliver = ClientMessageOk(*sender, channel, data) && AntiCheatAllows(sender->id, channel, data);
        }
    }
    std::vector<net::ChanMsg> got;
    std::uint32_t ack = 0;
    if (!link.ordered.Receive(channel, seq, std::move(data), deliver, fromId, got, ack)) return;
    if (ack) {
        auto bytes = net::BuildChanRelAck(channel, ack);
        SendDatagram(ackTo, bytes.data(), bytes.size());
    }
    for (const net::ChanMsg& m : got) {
        if (sender && m.channel.compare(0, 6, "__req/") == 0) { HandleClientRequest(*sender, m.channel, m.data); continue; }
        Deliver(m.from, m.channel, m.data);
    }
}

void NetworkManager::OnOrderedAck(net::ChannelEnd& link, net::Packet& p) {
    const std::string channel = p.ReadString();
    const std::uint32_t ack = p.ReadU32();
    if (!p.Ok() || !net::ChannelNameOk(channel)) return;
    link.ordered.Ack(channel, ack);
}

void NetworkManager::OnSequencedFromClient(Client& from, net::Packet& p, std::size_t len) {
    p.ReadU32();   // sender-reported id (the endpoint is the source of truth)
    const std::uint32_t seq = p.ReadU32();
    const std::uint32_t target = p.ReadU32();
    std::string channel = p.ReadString();
    std::string data = p.ReadString();
    if (!p.Ok() || !net::ChannelNameOk(channel)) return;
    if (!CookieOk(from, p)) return;
    if (!from.pipes.sequencedRx.IsStale(from.id, channel, seq) && !AllowMessage(from, len)) return;
    if (!from.pipes.sequencedRx.Accept(from.id, channel, seq)) return;
    if (channel == "__hit") { HandleHitClaim(from, data); return; }
    if (HandleClientRequest(from, channel, data)) return;
    if (!ClientMessageOk(from, channel, data)) return;
    if (!AntiCheatAllows(from.id, channel, data)) return;
    const std::uint32_t sender = from.id;
    const bool toHost = target == net::kChanBroadcast || target == m_localId;
    if (toHost && (dedicated || m_localRoom == from.room)) Deliver(sender, channel, data);
    if (target == net::kChanBroadcast) {
        auto bytes = net::BuildChanSeq(sender, seq, net::kChanBroadcast, channel, data, 0);
        for (auto& [ep, c] : m_clients)
            if (c.id != sender && c.room == from.room)
                SendDatagram(c.endpoint, bytes.data(), bytes.size());
    } else if (target != m_localId) {
        auto bytes = net::BuildChanSeq(sender, seq, target, channel, data, 0);
        for (auto& [ep, c] : m_clients)
            if (c.id == target) {
                SendDatagram(c.endpoint, bytes.data(), bytes.size());
                break;
            }
    }
}

void NetworkManager::OnSequencedFromServer(net::Packet& p) {
    const std::uint32_t sender = p.ReadU32();
    const std::uint32_t seq = p.ReadU32();
    const std::uint32_t target = p.ReadU32();
    const std::string channel = p.ReadString();
    const std::string data = p.ReadString();
    if (!p.Ok() || !net::ChannelNameOk(channel)) return;
    if (sender == m_localId) return;
    if (target != net::kChanBroadcast && target != m_localId) return;
    if (!m_toServer.sequencedRx.Accept(sender, channel, seq)) return;
    Deliver(sender, channel, data);
}

void NetworkManager::ResendReliable(
        std::unordered_map<std::uint32_t, std::pair<std::vector<std::uint8_t>, float>>& out,
        const net::Endpoint& to, float dt) {
    for (auto& [seq, item] : out) {
        item.second += dt;
        if (item.second >= kReliableResend) {
            item.second = 0.0f;
            SendDatagram(to, item.first.data(), item.first.size());
        }
    }
}

// Send a datagram, splitting it into MTU-sized fragments if it's too big to fit in
// one. Small messages (the vast majority) go out directly. Each fragment carries
// {Fragment, msgId, index, count} so the receiver can reassemble in order.
net::SecureChannel* NetworkManager::SessionFor(const net::Endpoint& ep) {
    if (m_mode == Mode::Client) return (ep == m_serverEp) ? &m_secure : nullptr;
    if (m_mode == Mode::Server) {
        auto it = m_clients.find(ep);
        if (it != m_clients.end()) return &it->second.secure;
        auto pj = m_pendingJoins.find(ep);   // a sealed join in progress
        return pj != m_pendingJoins.end() ? &pj->second.secure : nullptr;
    }
    return nullptr;
}

bool NetworkManager::Encrypted() const {
    if (m_mode == Mode::Client) return m_secure.Ready();
    return m_mode == Mode::Server && encryption && net::SecureChannel::Available();
}

void NetworkManager::SendDatagram(const net::Endpoint& to, const std::uint8_t* data, std::size_t size,
                                  bool allowEncrypt) {
    // Encrypt once the session for this peer is established. The sealed blob is
    // tagged with kEncEnvelope, then handed to the fragmentation step below — so
    // encryption sits above fragmentation (the receiver reassembles, then decrypts).
    std::vector<std::uint8_t> enc;
    if (allowEncrypt && encryption) {
        if (net::SecureChannel* s = SessionFor(to)) {
            if (s->Ready() && s->Seal(data, size, enc)) {
                enc.insert(enc.begin(), kEncEnvelope);
                data = enc.data(); size = enc.size();
            }
        }
    }
    if (size <= net::kFragmentThreshold) { Transmit(to, data, size); return; }
    const std::size_t header = 1 + 4 + 4 + 4;            // type + msgId + index + count
    const std::size_t chunkMax = net::kFragmentThreshold - header;
    std::uint32_t count = static_cast<std::uint32_t>((size + chunkMax - 1) / chunkMax);
    std::uint32_t id = m_fragNextId++;
    for (std::uint32_t i = 0; i < count; ++i) {
        std::size_t off = static_cast<std::size_t>(i) * chunkMax;
        std::size_t len = std::min(chunkMax, size - off);
        net::Packet h(Fragment); h.Write(id); h.Write(i); h.Write(count);
        std::vector<std::uint8_t> dg(h.Data(), h.Data() + h.Size());
        dg.insert(dg.end(), data + off, data + off + len);
        Transmit(to, dg.data(), dg.size());
    }
}

// One physical datagram out. Direct mode sends straight to the peer; relay mode
// wraps it in a RelayData frame addressed by the peer's slot (carried in `to`'s
// address) and sends it to the relay, which forwards it on.
void NetworkManager::Transmit(const net::Endpoint& to, const std::uint8_t* data, std::size_t size) {
    if (!m_relay) {
        if (!net::HoldDatagram(m_socket, to, data, size)) {
            if (m_socket.SendTo(to, data, size)) net::NoteWire(size, false);
        }
        return;
    }
    net::Packet h(net::RelayData);
    h.Write(to.address);                       // destination slot
    std::vector<std::uint8_t> dg(h.Data(), h.Data() + h.Size());
    dg.insert(dg.end(), data, data + size);
    if (!net::HoldDatagram(m_socket, m_relayEp, dg.data(), dg.size())) {
        if (m_socket.SendTo(m_relayEp, dg.data(), dg.size())) net::NoteWire(dg.size(), false);
    }
}

// One physical datagram in. Direct mode is a pass-through. Relay mode peels the
// RelayData header (substituting a synthetic slot-endpoint as the sender) and
// quietly handles RelayWelcome control frames, looping until it has an actual
// application payload to return (or nothing is pending).
int NetworkManager::ReceiveFrom(void* buffer, std::size_t capacity, net::Endpoint& from) {
    // Control frames and stray datagrams are consumed without returning, so a flood of
    // them is only drained a bounded amount per call; the rest waits for the next.
    int consumed = 0;
    for (;;) {
        if (m_relay && consumed++ >= kMaxRelayFramesPerCall) return 0;
        int n = net::TakeDatagram(m_socket, buffer, capacity, from);
        if (n <= 0 || !m_relay) return n;
        if (!(from == m_relayEp)) continue;          // only the relay should reach us
        auto* b = static_cast<std::uint8_t*>(buffer);
        std::uint8_t tag = b[0];
        if (tag == net::RelayWelcome) {
            // Version 2 relays add [u8 version][u64 host key]; the key comes
            // only to the host, which sends it back in every hello so only it
            // can host this code (from this address or a new one). Version 4
            // adds the u64 nonce our hello carried. Anyone can forge a UDP
            // source address, so a welcome counts only if it echoes that nonce
            // (a missing one only from an older relay when allowed).
            net::RelayWelcomeInfo w;
            if (n < 9) continue;
            if (!net::ParseRelayWelcome(b, static_cast<std::size_t>(n), m_relayNonce, allowUnverifiedRelay, w)) {
                ++m_relayBadWelcomes;
                continue;
            }
            const std::uint32_t hostSlot = w.hostSlot;
            const bool haveKey = w.haveKey;
            const std::uint32_t keyLo = static_cast<std::uint32_t>(w.hostKey), keyHi = static_cast<std::uint32_t>(w.hostKey >> 32);
            // A host needs the key fields: without them it would hello with key 0
            // and be refused as if the code were taken.
            if (m_mode == Mode::Server && !haveKey) continue;
            if (m_mode == Mode::Server && (keyLo | keyHi))
                m_relayHostKey = (static_cast<std::uint64_t>(keyHi) << 32) | keyLo;
            if (m_mode == Mode::Client) {
                // The relay knows us by our source address, and a new address (a NAT rebinding, a
                // phone changing network) is a new peer with a new slot: the host still lists the old
                // slot, which now leads nowhere. Say so now rather than after the silence timeout; the
                // automatic reconnect then resumes the player through the new address.
                if (m_relayWelcomed && m_joined && m_relaySlot != 0 && w.slot != m_relaySlot) {
                    m_relayError = "this connection's address changed on the relay";
                    OKAY_WARN("net: relay slot ", m_relaySlot, " -> ", w.slot, ": ", m_relayError);
                    m_connectionLost = true;
                    m_connectionLostReason = m_relayError;
                    m_relayLost = true;
                    return 0;
                }
                m_relaySlot = w.slot;
                // The host we were paired with is the only server we talk to.
                // Any change (it left: slot 0, or another host took the code)
                // ends this session instead of re-pointing us at someone else.
                if (m_relayWelcomed && !(m_serverEp == SlotEndpoint(hostSlot))) {
                    m_relayError = hostSlot ? "the session's host changed" : "the host left the session";
                    OKAY_WARN("net: relay session '", m_relayCode, "' ended: ", m_relayError);
                    m_relayLost = true;
                    return 0;
                }
                if (hostSlot == 0) continue;   // no host yet
                m_serverEp = SlotEndpoint(hostSlot);
            }
            m_relayWelcomed = true;
            continue;
        }
        if (tag == net::RelayCookie) {
            // A version-3 relay wants proof we receive at our address: hello
            // again now with its cookie.
            net::Packet p(b, static_cast<std::size_t>(n));
            p.ReadU8();
            p.ReadU8();
            std::uint8_t c[sizeof m_relayCookie];
            for (auto& x : c) x = p.ReadU8();
            if (!p.Ok()) continue;
            std::memcpy(m_relayCookie, c, sizeof c);
            m_relayHelloTimer = 0.0f;
            continue;
        }
        if (tag == net::RelayRefused) {
            net::Packet p(b, static_cast<std::size_t>(n));
            p.ReadU8();
            const std::uint8_t why = p.ReadU8();
            const std::uint8_t relayVersion = p.ReadU8();
            if (!p.Ok()) continue;
            if (why == net::RelayRefusedHosted)
                m_relayError = "session code '" + m_relayCode + "' is already hosted";
            else if (why == net::RelayRefusedVersion)
                m_relayError = "the relay needs protocol version " + std::to_string(relayVersion) +
                               " (this game speaks " + std::to_string(net::kRelayProtocolVersion) + ")";
            else
                m_relayError = "the relay refused us for an unknown reason (code " +
                               std::to_string(why) + ")";
            OKAY_WARN("net: relay: ", m_relayError);
            m_relayLost = true;
            return 0;
        }
        if (tag == net::RelayData) {
            const std::size_t payloadOff = 1 + 4;    // tag + srcSlot
            if (static_cast<std::size_t>(n) < payloadOff) continue;
            net::Packet hp(b, payloadOff);
            hp.ReadU8();
            std::uint32_t srcSlot = hp.ReadU32();
            std::size_t payload = static_cast<std::size_t>(n) - payloadOff;
            std::memmove(buffer, b + payloadOff, payload);
            from = SlotEndpoint(srcSlot);
            return static_cast<int>(payload);
        }
        // Unknown relay tag — ignore and keep reading.
    }
}

// Collect one fragment from `from`. Returns true (and fills `out` with the whole
// reassembled message) only once every fragment of that message has arrived.
// A peer we've completed a handshake with: a joined client (server side) or our
// server (client side).
bool NetworkManager::Handshaken(const net::Endpoint& from) const {
    if (m_mode == Mode::Server) return m_clients.find(from) != m_clients.end();
    if (m_mode == Mode::Client) return from == m_serverEp;
    return false;
}

bool NetworkManager::TakeFragment(const net::Endpoint& from, const std::uint8_t* data,
                                  std::size_t size, std::vector<std::uint8_t>& out) {
    const std::size_t header = 1 + 4 + 4 + 4;
    // Senders keep every fragment under the threshold; anything bigger is forged.
    if (size < header || size > net::kFragmentThreshold) return false;
    net::Packet p(data, size);
    p.ReadU8();                                   // Fragment tag
    std::uint32_t id = p.ReadU32(), index = p.ReadU32(), count = p.ReadU32();
    const bool known = Handshaken(from);
    const std::uint32_t maxCount = known ? kMaxFragments : kStrangerMaxFragments;
    if (!p.Ok() || count == 0 || count > maxCount || index >= count) return false;
    auto epIt = m_fragIn.find(from);
    if (epIt == m_fragIn.end()) {
        if (!known) {                             // only so many strangers mid-message at once
            std::size_t strangers = 0;
            for (const auto& e : m_fragIn) if (!Handshaken(e.first)) ++strangers;
            if (strangers >= kMaxStrangers) return false;
        }
        epIt = m_fragIn.emplace(from, std::unordered_map<std::uint32_t, FragAsm>{}).first;
    }
    auto& msgs = epIt->second;
    auto mIt = msgs.find(id);
    if (mIt == msgs.end()) {
        if (msgs.size() >= (known ? kMaxPendingMessages : 1)) return false;
        mIt = msgs.emplace(id, FragAsm{}).first;
        mIt->second.count = count;
        mIt->second.chunks.resize(count);
    }
    FragAsm& asm_ = mIt->second;
    if (asm_.count != count) return false;        // inconsistent — drop this stray fragment
    if (asm_.chunks[index].empty()) {             // first time we see this index
        std::size_t held = 0;
        for (const auto& m : msgs) held += m.second.bytes;
        if (held + (size - header) > kMaxPendingBytes) return false;
        asm_.chunks[index].assign(data + header, data + size);
        asm_.bytes += size - header;
        asm_.got++;
    }
    asm_.age = 0.0f;
    if (asm_.got < asm_.count) return false;
    out.clear();
    for (auto& c : asm_.chunks) out.insert(out.end(), c.begin(), c.end());
    msgs.erase(mIt);
    if (msgs.empty()) m_fragIn.erase(epIt);
    return true;
}

// Discard half-assembled messages whose fragments stopped arriving, so a lost
// fragment can't leak memory forever.
void NetworkManager::PruneFragments(float dt) {
    for (auto epIt = m_fragIn.begin(); epIt != m_fragIn.end(); ) {
        for (auto it = epIt->second.begin(); it != epIt->second.end(); ) {
            it->second.age += dt;
            if (it->second.age > kFragmentTtl) it = epIt->second.erase(it);
            else ++it;
        }
        if (epIt->second.empty()) epIt = m_fragIn.erase(epIt);
        else ++epIt;
    }
}

void NetworkManager::Send(const std::string& channel, const std::string& data) {
    if (m_mode == Mode::Server) {
        // Server originates a message: stamp it with our id (0) and fan out to
        // the clients in the host's room.
        net::NoteObject(net::ObjectKey(channel, data), channel.size() + data.size(), false);
        net::Packet p(Message);
        p.Write(m_localId);
        p.Write(channel);
        p.Write(data);
        for (auto& [ep, c] : m_clients)
            if (c.room == m_localRoom)
                SendDatagram(c.endpoint, p.Data(), p.Size());
    } else if (m_mode == Mode::Client && m_joined) {
        net::NoteObject(net::ObjectKey(channel, data), channel.size() + data.size(), false);
        net::Packet p(Message);
        p.Write(m_localId);
        p.Write(channel);
        p.Write(data);
        if (m_sessionCookie) WriteU64(p, m_sessionCookie);
        SendDatagram(m_serverEp, p.Data(), p.Size());
    }
}

bool NetworkManager::ClientMessageOk(const Client& from, const std::string& channel, std::string& data) const {
    // Nothing big enough to choke every peer it's relayed to.
    if (maxRelayedBytes > 0 && channel.size() + data.size() > (std::size_t)maxRelayedBytes) return false;
    if (channel.size() < 2 || channel[0] != '_' || channel[1] != '_') return true;   // a game channel
    if (channel == "__chat") {
        // "<name>\n<text>": the name is the server's, not the sender's choice;
        // the text is cut to 512 bytes (on a UTF-8 character boundary).
        const std::size_t nl = data.find('\n');
        std::string text = nl == std::string::npos ? data : data.substr(nl + 1);
        if (text.size() > 512) {
            std::size_t cut = 512;
            while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
            text.resize(cut);
        }
        data = from.name + "\n" + text;
        return true;
    }
    if (channel == "__xform") {
        // A player the server moves: its position is the server's, never the client's.
        if (!ClientMaySendXform(from, data.substr(0, data.find('|')))) return false;
        // The same pose check as Deliver: a bad sample is not relayed.
        std::string id, extra; Vec3 p; Quat q; double tick = -1.0;
        return ParseXformPose(data, id, p, q, extra, tick);
    }
    if (channel == "__spawn") {
        const SpawnMessage m = ParseSpawnMessage(data);
        if (!std::isfinite(m.pos.x) || !std::isfinite(m.pos.y) || !std::isfinite(m.pos.z)) return false;
        return SpawnPathOk(from.id, m.path, true);
    }
    if (channel == "__despawn") return true;
    if (channel.size() > 6 && channel.compare(0, 6, "__rpc/") == 0) return true;
    if (channel == "__ev")   // a named event (checked again when it is opened): bounded
        return data.size() <= kMaxEventData + kMaxEventName + 16;
    return false;   // __match_start and any other reserved channel: host only
}

} // namespace okay
