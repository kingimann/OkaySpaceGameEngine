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
#include <functional>
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
            PutU32(b, (std::uint32_t)p.dense.size());
            for (std::size_t i = 0; i < p.dense.size(); ++i) {
                PutU32(b, p.dense[i]);
                const std::uint8_t* raw = reinterpret_cast<const std::uint8_t*>(&p.comps[i]);
                b.insert(b.end(), raw, raw + sizeof(T));
            }
        };
        h.applyAll = [](World& w, const std::uint8_t*& cur, const std::uint8_t* end) {
            w.clearPool<T>();
            std::uint32_t n = GetU32(cur, end);
            for (std::uint32_t i = 0; i < n; ++i) {
                Entity e = GetU32(cur, end);
                T v{};
                if (cur + sizeof(T) <= end) { std::memcpy(&v, cur, sizeof(T)); cur += sizeof(T); }
                w.ensure(e);
                w.add<T>(e, v);
            }
        };
        m_handlers.push_back(std::move(h));
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

    /// Apply a snapshot from the authority: mirror its entities and components,
    /// pruning anything not present (full-state sync).
    void apply(const std::vector<std::uint8_t>& data) {
        const std::uint8_t* cur = data.data();
        const std::uint8_t* end = cur + data.size();
        // Entities present in the snapshot.
        std::uint32_t n = GetU32(cur, end);
        std::vector<Entity> snap; snap.reserve(n);
        for (std::uint32_t i = 0; i < n; ++i) snap.push_back(GetU32(cur, end));
        // Destroy local entities the authority no longer has.
        std::vector<Entity> local = world.entities();
        for (Entity e : local) {
            bool keep = false;
            for (Entity s : snap) if (s == e) { keep = true; break; }
            if (!keep) world.destroy(e);
        }
        // Ensure all snapshot entities exist.
        for (Entity e : snap) world.ensure(e);
        // Apply each component block (clears+rebuilds that pool).
        std::uint8_t typeCount = (cur < end) ? *cur++ : 0;
        for (std::uint8_t i = 0; i < typeCount; ++i) {
            if (cur >= end) break;
            std::uint8_t id = *cur++;
            Handler* h = handlerFor(id);
            if (h) h->applyAll(world, cur, end);
            else break;   // unknown id: can't know its length, stop safely
        }
    }

private:
    struct Handler {
        std::uint8_t id = 0;
        std::function<void(World&, std::vector<std::uint8_t>&)> writeAll;
        std::function<void(World&, const std::uint8_t*&, const std::uint8_t*)> applyAll;
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
