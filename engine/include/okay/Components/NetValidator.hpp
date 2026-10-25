#pragma once
// ---------------------------------------------------------------------------
// NetValidator — a per-RPC rule the server checks before running a client's
// RPC (needs an AntiCheatConfig in the scene, which turns anti-cheat on).
//
// Each NetValidator names one RPC ("*" = every RPC) and refuses a client's call
// when its payload is too long, it comes too often, or clients may not send it
// at all. A refused RPC never runs on the server and isn't relayed; it's
// reported as "rpc rejected" (On Cheat Detected) and counts as a violation.
// ---------------------------------------------------------------------------
#include "okay/Scene/Component.hpp"
#include <cstdint>
#include <string>
#include <unordered_map>

namespace okay {

class NetValidator : public Behaviour {
public:
    std::string rpc = "*";        ///< the RPC this rule covers ("*" = all)
    int   maxBytes = 0;           ///< longest payload a client may send (0 = any)
    float maxPerSecond = 0.0f;    ///< calls per second per client (0 = any)
    bool  clientsMaySend = true;  ///< false = only the host may call it

    bool Covers(const std::string& name) const { return rpc == "*" || rpc == name; }

    /// May client `peer` call RPC `name` with `data`? (Counts the call.)
    bool Allows(std::uint32_t peer, const std::string& name, const std::string& data) {
        if (!Covers(name)) return true;
        if (!clientsMaySend) return false;
        if (maxBytes > 0 && data.size() > (std::size_t)maxBytes) return false;
        if (maxPerSecond > 0.0f) {
            Window& w = m_calls[peer];
            if (m_time - w.start >= 1.0f) { w.start = m_time; w.count = 0; }
            if (++w.count > maxPerSecond) return false;
        }
        return true;
    }

    void Update(float dt) override { m_time += dt; }

private:
    struct Window { float start = 0.0f; int count = 0; };
    std::unordered_map<std::uint32_t, Window> m_calls;
    float m_time = 0.0f;
};

} // namespace okay
