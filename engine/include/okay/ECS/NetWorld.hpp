#pragma once
// ---------------------------------------------------------------------------
// okay::ecs::NetWorld — an ECS World with networking built in. Register the
// component types that should replicate, then Snapshot() the authoritative world
// to a byte buffer, send it over any transport (okay NetworkManager / INetTransport
// / your own socket), and Apply() it on the remote: entities and their registered
// components are mirrored 1:1 (ids preserved, removed entities/components pruned).
//
//   struct Position { float x, y, z; };
//   NetWorld server;
//   server.replicate<Position>(1);          // type id 1, replicated
//   auto e = server.world.create();
//   server.world.add<Position>(e, {1,2,3});
//   std::vector<uint8_t> packet = server.snapshot();
//
//   NetWorld client;
//   client.replicate<Position>(1);          // same registration
//   client.apply(packet);                   // client now has the same entity+Position
//
// Full-state snapshots (simple and robust). Replicated components must be trivially
// copyable (plain data) — the common case for transform/health/state structs.
// ---------------------------------------------------------------------------
#include "okay/ECS/World.hpp"
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <functional>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <type_traits>

namespace okay {
namespace ecs {

class NetWorld {
public:
    World world;

    /// Register a component type for replication under a stable `typeId` (1..255).
    /// Call with the SAME id on every peer. Components must be trivially copyable.
    template <class T>
    void replicate(std::uint8_t typeId) {
        static_assert(std::is_trivially_copyable<T>::value,
                      "replicated ECS components must be trivially copyable (plain data)");
        Handler h;
        h.id = typeId;
        h.writeAll = [](World& w, std::vector<std::uint8_t>& b) {
            Pool<T>& p = w.pool<T>();
            PutU32(b, (std::uint32_t)p.size());
            for (std::size_t i = 0; i < p.dense.size(); ++i) {
                if (p.dense[i] == Null) continue;     // hole left by a removal during iteration
                PutU32(b, p.dense[i]);
                const std::uint8_t* raw = reinterpret_cast<const std::uint8_t*>(&p.comps[i]);
                b.insert(b.end(), raw, raw + sizeof(T));
            }
        };
        h.applyAll = [](World& w, const std::uint8_t*& cur, const std::uint8_t* end) -> bool {
            std::uint32_t n = GetU32(cur, end);
            // A count the remaining bytes cannot hold is a damaged / hostile packet:
            // leave the pool alone rather than loop for billions of entries.
            if ((std::size_t)n > (std::size_t)(end - cur) / (4 + sizeof(T))) { cur = end; return false; }
            w.clearPool<T>();
            for (std::uint32_t i = 0; i < n; ++i) {
                Entity e = GetU32(cur, end);
                T v{};
                std::memcpy(&v, cur, sizeof(T)); cur += sizeof(T);
                if (w.ensure(e) == Null) continue;
                w.add<T>(e, v);
            }
            return true;
        };
        h.capture = [](World& w, CompMap& out) {
            Pool<T>& p = w.pool<T>();
            out.reserve(p.size());
            for (std::size_t i = 0; i < p.dense.size(); ++i) {
                if (p.dense[i] == Null) continue;
                const std::uint8_t* raw = reinterpret_cast<const std::uint8_t*>(&p.comps[i]);
                out.emplace(p.dense[i], std::vector<std::uint8_t>(raw, raw + sizeof(T)));
            }
        };
        h.writeDelta = [](const CompMap& base, const CompMap& cur, std::vector<std::uint8_t>& b) {
            std::vector<Entity> changed, gone;
            for (const auto& kv : cur) {
                auto it = base.find(kv.first);
                if (it == base.end() || it->second != kv.second) changed.push_back(kv.first);
            }
            for (const auto& kv : base) if (cur.find(kv.first) == cur.end()) gone.push_back(kv.first);
            std::sort(changed.begin(), changed.end());
            std::sort(gone.begin(), gone.end());
            PutU32(b, (std::uint32_t)changed.size());
            for (Entity e : changed) { PutU32(b, e); const auto& v = cur.find(e)->second; b.insert(b.end(), v.begin(), v.end()); }
            PutU32(b, (std::uint32_t)gone.size());
            for (Entity e : gone) PutU32(b, e);
        };
        // Validate a delta block (advancing `cur`) without touching the world.
        h.checkDelta = [](const std::uint8_t*& cur, const std::uint8_t* end) -> bool {
            for (int part = 0; part < 2; ++part) {
                if (end - cur < 4) return false;
                std::uint32_t n = GetU32(cur, end);
                std::size_t each = part == 0 ? 4 + sizeof(T) : 4;
                if ((std::size_t)n > (std::size_t)(end - cur) / each) return false;
                cur += (std::size_t)n * each;
            }
            return true;
        };
        h.applyDelta = [](World& w, const std::uint8_t*& cur, const std::uint8_t* end) {
            std::uint32_t n = GetU32(cur, end);
            for (std::uint32_t i = 0; i < n; ++i) {
                Entity e = GetU32(cur, end);
                T v{};
                std::memcpy(&v, cur, sizeof(T)); cur += sizeof(T);
                if (w.ensure(e) == Null) continue;
                w.add<T>(e, v);
            }
            std::uint32_t g = GetU32(cur, end);
            for (std::uint32_t i = 0; i < g; ++i) w.remove<T>(GetU32(cur, end));
        };
        m_handlers.push_back(std::move(h));
    }

    /// Snapshot as a string (handy for transports whose payload is a std::string,
    /// like NetworkManager's RPC/Send).
    std::string snapshotStr() const {
        std::vector<std::uint8_t> b = snapshot();
        return std::string(b.begin(), b.end());
    }
    /// Apply a snapshot delivered as a string.
    void applyStr(const std::string& s) {
        apply(std::vector<std::uint8_t>(s.begin(), s.end()));
    }

    /// Serialize the whole world (live entities + every replicated component).
    std::vector<std::uint8_t> snapshot() const {
        std::vector<std::uint8_t> b;
        std::vector<Entity> ents = world.entities();
        PutU32(b, (std::uint32_t)ents.size());
        for (Entity e : ents) PutU32(b, e);
        b.push_back((std::uint8_t)m_handlers.size());
        World& w = const_cast<World&>(world);   // pool access is logically const here
        for (const Handler& h : m_handlers) {
            b.push_back(h.id);
            h.writeAll(w, b);
        }
        return b;
    }

    // ---- delta snapshots ------------------------------------------------
    // Wire format (new packet types; the legacy full snapshot above is unchanged and
    // still accepted by apply(): its first u32 is an entity count, which can never be
    // 0xFFFFFFFF in a valid packet, so that value marks the versioned packets):
    //   [u32 0xFFFFFFFF][u8 kind][u32 tick] ...
    //   kind 1 FULL : the legacy snapshot body follows
    //   kind 2 DELTA: [u32 baselineTick][u32 nAdded][ids][u32 nDestroyed][ids]
    //                 [u8 typeCount] { [u8 id] [u32 nChanged]{id,bytes} [u32 nGone]{id} }
    //   kind 3 ACK  : client -> server, "I have applied everything up to `tick`" (0 = I
    //                 have nothing, send a full snapshot)
    // The server sends a delta against the newest tick the client acknowledged, and
    // falls back to a full snapshot when there is no usable baseline.

    enum class ApplyResult { Applied, Ignored, Malformed, NeedFull };

    /// Server: build the next snapshot, as small as the acknowledged baseline allows.
    /// Each call advances the tick and remembers the world state under it.
    std::vector<std::uint8_t> deltaSnapshot() {
        std::uint32_t tick = ++m_tick;
        State cur = captureState();
        std::vector<std::uint8_t> b;
        auto it = m_history.find(m_acked);
        if (m_acked == 0 || it == m_history.end()) {
            PutU32(b, kMagic); b.push_back(kFull); PutU32(b, tick);
            std::vector<std::uint8_t> body = snapshot();
            b.insert(b.end(), body.begin(), body.end());
        } else {
            const State& base = it->second;
            PutU32(b, kMagic); b.push_back(kDelta); PutU32(b, tick); PutU32(b, m_acked);
            std::vector<Entity> added, destroyed;
            for (Entity e : cur.alive) if (!std::binary_search(base.alive.begin(), base.alive.end(), e)) added.push_back(e);
            for (Entity e : base.alive) if (!std::binary_search(cur.alive.begin(), cur.alive.end(), e)) destroyed.push_back(e);
            PutU32(b, (std::uint32_t)added.size());
            for (Entity e : added) PutU32(b, e);
            PutU32(b, (std::uint32_t)destroyed.size());
            for (Entity e : destroyed) PutU32(b, e);
            b.push_back((std::uint8_t)m_handlers.size());
            for (std::size_t i = 0; i < m_handlers.size(); ++i) {
                b.push_back(m_handlers[i].id);
                m_handlers[i].writeDelta(base.comps[i], cur.comps[i], b);
            }
        }
        m_history[tick] = std::move(cur);
        pruneHistory();
        return b;
    }
    std::string deltaSnapshotStr() {
        std::vector<std::uint8_t> b = deltaSnapshot();
        return std::string(b.begin(), b.end());
    }

    /// Server: a client acknowledged `tick` (from ackPacket()). Unknown / older ticks
    /// are ignored; 0 means "I lost my state", so the next snapshot is a full one.
    /// (One acked baseline per NetWorld: with several clients, use one NetWorld
    /// sender per client or ack the slowest client's tick.)
    bool receiveAck(const std::vector<std::uint8_t>& data) {
        const std::uint8_t* cur = data.data(); const std::uint8_t* end = cur + data.size();
        if (data.size() != 9) return false;
        if (GetU32(cur, end) != kMagic || *cur++ != kAck) return false;
        std::uint32_t t = GetU32(cur, end);
        if (t == 0) { m_acked = 0; return true; }
        if (m_history.find(t) == m_history.end()) return false;
        if (t > m_acked || m_acked == 0) m_acked = t;
        pruneHistory();
        return true;
    }
    /// Client: the packet to send back so the server uses our last applied tick as
    /// the baseline.
    std::vector<std::uint8_t> ackPacket() const {
        std::vector<std::uint8_t> b;
        PutU32(b, kMagic); b.push_back(kAck); PutU32(b, m_appliedTick);
        return b;
    }
    std::uint32_t tick() const { return m_tick; }              // server: latest tick sent
    std::uint32_t ackedTick() const { return m_acked; }        // server: current baseline
    std::uint32_t appliedTick() const { return m_appliedTick; }// client: last tick applied

    /// Apply a snapshot from the authority: mirror its entities and components,
    /// pruning anything not present (full-state sync). Also accepts the versioned
    /// FULL / DELTA packets (see applyPacket for the result).
    void apply(const std::vector<std::uint8_t>& data) { applyPacket(data); }

    ApplyResult applyPacket(const std::vector<std::uint8_t>& data) {
        const std::uint8_t* cur = data.data();
        const std::uint8_t* end = cur + data.size();
        if (data.size() >= 9 && data[0] == 0xFF && data[1] == 0xFF && data[2] == 0xFF && data[3] == 0xFF) {
            cur += 4;
            std::uint8_t kind = *cur++;
            std::uint32_t tick = GetU32(cur, end);
            if (kind == kFull) {
                bool ok = applyFull(cur, end);
                if (!ok) return ApplyResult::Malformed;
                m_appliedTick = tick;
                return ApplyResult::Applied;
            }
            if (kind == kDelta) return applyDelta(tick, cur, end);
            return ApplyResult::Malformed;
        }
        if (data.size() < 4) return ApplyResult::Malformed;   // not even a count: don't treat as "empty world"
        return applyFull(cur, end) ? ApplyResult::Applied : ApplyResult::Malformed;
    }

private:
    static constexpr std::uint32_t kMagic = 0xFFFFFFFFu;
    static constexpr std::uint8_t kFull = 1, kDelta = 2, kAck = 3;
    using CompMap = std::unordered_map<Entity, std::vector<std::uint8_t>>;
    struct State {
        std::vector<Entity> alive;       // sorted
        std::vector<CompMap> comps;      // per handler
    };
    std::map<std::uint32_t, State> m_history;   // server: sent states by tick
    std::uint32_t m_tick = 0, m_acked = 0, m_appliedTick = 0;

    State captureState() {
        State s;
        s.alive = world.entities();
        std::sort(s.alive.begin(), s.alive.end());
        s.comps.resize(m_handlers.size());
        for (std::size_t i = 0; i < m_handlers.size(); ++i) m_handlers[i].capture(world, s.comps[i]);
        return s;
    }
    void pruneHistory() {
        while (!m_history.empty() && m_history.begin()->first < m_acked) m_history.erase(m_history.begin());
        while (m_history.size() > 64) {
            auto it = m_history.begin();
            if (it->first == m_acked) ++it;       // never drop the acked baseline
            m_history.erase(it);
        }
    }

    bool applyFull(const std::uint8_t* cur, const std::uint8_t* end) {
        // Entities present in the snapshot.
        std::uint32_t n = GetU32(cur, end);
        // Reject a count the packet cannot hold before touching the world (and before
        // reserving memory for it).
        if ((std::size_t)n > (std::size_t)(end - cur) / 4) return false;
        std::unordered_set<Entity> snap; snap.reserve(n);
        for (std::uint32_t i = 0; i < n; ++i) snap.insert(GetU32(cur, end));
        // Destroy local entities the authority no longer has.
        for (Entity e : world.entities())
            if (snap.find(e) == snap.end()) world.destroy(e);
        // Ensure all snapshot entities exist.
        for (Entity e : snap) world.ensure(e);
        // Apply each component block (clears+rebuilds that pool).
        std::uint8_t typeCount = (cur < end) ? *cur++ : 0;
        for (std::uint8_t i = 0; i < typeCount; ++i) {
            if (cur >= end) break;
            std::uint8_t id = *cur++;
            Handler* h = handlerFor(id);
            if (!h || !h->applyAll(world, cur, end)) break;   // unknown id: can't know its length, stop safely
        }
        return true;
    }

    ApplyResult applyDelta(std::uint32_t tick, const std::uint8_t* cur, const std::uint8_t* end) {
        if (end - cur < 4) return ApplyResult::Malformed;
        std::uint32_t baseline = GetU32(cur, end);
        // Validate the whole packet first so a hostile one changes nothing.
        const std::uint8_t* chk = cur;
        for (int part = 0; part < 2; ++part) {
            if (end - chk < 4) return ApplyResult::Malformed;
            std::uint32_t n = GetU32(chk, end);
            if ((std::size_t)n > (std::size_t)(end - chk) / 4) return ApplyResult::Malformed;
            chk += (std::size_t)n * 4;
        }
        if (chk >= end) return ApplyResult::Malformed;
        std::uint8_t typeCount = *chk++;
        for (std::uint8_t i = 0; i < typeCount; ++i) {
            if (chk >= end) return ApplyResult::Malformed;
            Handler* h = handlerFor(*chk++);
            if (!h || !h->checkDelta(chk, end)) return ApplyResult::Malformed;
        }
        if (chk != end) return ApplyResult::Malformed;
        // Well formed; is it for the state we hold?
        if (tick <= m_appliedTick) return ApplyResult::Ignored;           // stale / duplicate
        if (baseline != m_appliedTick) return ApplyResult::NeedFull;      // we are not at its baseline
        std::uint32_t nAdd = GetU32(cur, end);
        for (std::uint32_t i = 0; i < nAdd; ++i) world.ensure(GetU32(cur, end));
        std::uint32_t nDel = GetU32(cur, end);
        std::vector<Entity> del; del.reserve(nDel);
        for (std::uint32_t i = 0; i < nDel; ++i) del.push_back(GetU32(cur, end));
        std::uint8_t tc = *cur++;
        for (std::uint8_t i = 0; i < tc; ++i) {
            std::uint8_t id = *cur++;
            handlerFor(id)->applyDelta(world, cur, end);
        }
        for (Entity e : del) world.destroy(e);
        m_appliedTick = tick;
        return ApplyResult::Applied;
    }

    struct Handler {
        std::uint8_t id = 0;
        std::function<void(World&, std::vector<std::uint8_t>&)> writeAll;
        std::function<bool(World&, const std::uint8_t*&, const std::uint8_t*)> applyAll;
        std::function<void(World&, CompMap&)> capture;
        std::function<void(const CompMap&, const CompMap&, std::vector<std::uint8_t>&)> writeDelta;
        std::function<bool(const std::uint8_t*&, const std::uint8_t*)> checkDelta;
        std::function<void(World&, const std::uint8_t*&, const std::uint8_t*)> applyDelta;
    };
    std::vector<Handler> m_handlers;

    Handler* handlerFor(std::uint8_t id) {
        for (Handler& h : m_handlers) if (h.id == id) return &h;
        return nullptr;
    }

    static void PutU32(std::vector<std::uint8_t>& b, std::uint32_t v) {
        b.push_back((std::uint8_t)(v & 0xFF));
        b.push_back((std::uint8_t)((v >> 8) & 0xFF));
        b.push_back((std::uint8_t)((v >> 16) & 0xFF));
        b.push_back((std::uint8_t)((v >> 24) & 0xFF));
    }
    static std::uint32_t GetU32(const std::uint8_t*& cur, const std::uint8_t* end) {
        if (cur + 4 > end) { cur = end; return 0; }
        std::uint32_t v = (std::uint32_t)cur[0] | ((std::uint32_t)cur[1] << 8) |
                          ((std::uint32_t)cur[2] << 16) | ((std::uint32_t)cur[3] << 24);
        cur += 4;
        return v;
    }
};

} // namespace ecs
} // namespace okay
