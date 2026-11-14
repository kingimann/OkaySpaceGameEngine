#pragma once
// ---------------------------------------------------------------------------
// NetValidator — a per-RPC rule the server checks before running a client's
// RPC (needs an AntiCheatConfig in the scene, which turns anti-cheat on).
//
// Each NetValidator names one RPC ("*" = every RPC) and refuses a client's call
// when its payload is too long, it comes too often (calls/s, or sooner than its
// cooldown after the last one: a fire rate), its budget is spent (a token
// bucket: ammo, with an optional refill), or clients may not send it at all.
// Game code tops a budget up (a reload, a pickup) with TopUp / net_budget /
// the Server Budget node. A refused RPC never runs on the server and isn't relayed; it's
// reported as "rpc rejected" (On Cheat Detected) and counts as a violation.
// ---------------------------------------------------------------------------
#include "okay/Scene/Component.hpp"
#include "okay/Scene/Scene.hpp"
#include <cmath>
#include <cstdint>
#include <string>
#include <iterator>
#include <unordered_map>

namespace okay {

class NetValidator : public Behaviour {
public:
    std::string rpc = "*";        ///< the RPC this rule covers ("*" = all)
    int   maxBytes = 0;           ///< longest payload a client may send (0 = any)
    float maxPerSecond = 0.0f;    ///< calls per second per client (0 = any)
    bool  clientsMaySend = true;  ///< false = only the host may call it
    float cooldown = 0.0f;        ///< seconds a client must wait between calls (0 = none)
    float budget = 0.0f;          ///< calls a client can bank (0 = no budget); each call spends one
    float refillPerSecond = 0.0f; ///< budget regained per second (0 = only TopUp refills it)

    /// Does this rule limit how often a client calls (calls/s, cooldown, budget)?
    bool LimitsRate() const { return maxPerSecond > 0.0f || cooldown > 0.0f || budget > 0.0f; }

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
        if (cooldown > 0.0f || budget > 0.0f) {
            Bucket& b = Refilled(peer);
            if (cooldown > 0.0f && b.called && m_time - b.last < cooldown) return false;
            if (budget > 0.0f) {
                if (b.tokens < 1.0f) return false;
                b.tokens -= 1.0f;
            }
            b.called = true;
            b.last = m_time;
        }
        return true;
    }

    /// What's left of `peer`'s budget (the full budget before its first call).
    float Budget(std::uint32_t peer) {
        if (budget <= 0.0f) return 0.0f;
        return Refilled(peer).tokens;
    }
    /// Give `peer` more budget (a reload, a pickup), up to the full budget; a
    /// negative amount takes some away. Returns the new value.
    float TopUp(std::uint32_t peer, float amount) {
        if (budget <= 0.0f) return 0.0f;
        Bucket& b = Refilled(peer);
        b.tokens = std::fmax(0.0f, std::fmin(budget, b.tokens + amount));
        return b.tokens;
    }

    void Update(float dt) override {
        m_time += dt;
        // Peers that went quiet (and anyone AntiCheatConfig didn't tell us
        // about) drop out after a while, so the map can't grow without end.
        if ((m_sincePrune += dt) >= 10.0f) {
            m_sincePrune = 0.0f;
            for (auto it = m_calls.begin(); it != m_calls.end();)
                it = m_time - it->second.start > 10.0f ? m_calls.erase(it) : std::next(it);
        }
    }
    /// A peer left: drop its call counter and budget. (Budgets aren't dropped
    /// for going quiet: a full magazine back after a pause would be a free reload.)
    void ForgetPeer(std::uint32_t peer) { m_calls.erase(peer); m_buckets.erase(peer); }
    /// A peer is leaving: keep its budget and cooldown under `ident` for 5
    /// minutes, so emptying the magazine and reconnecting isn't a reload.
    void Park(std::uint32_t peer, const std::string& ident) {
        for (auto it = m_parked.begin(); it != m_parked.end();)
            it = m_time - it->second.second > 300.0f ? m_parked.erase(it) : std::next(it);
        if (auto it = m_buckets.find(peer); it != m_buckets.end()) m_parked[ident] = {it->second, m_time};
    }
    /// A peer joined as `ident`: it gets back what that ident parked.
    void Restore(std::uint32_t peer, const std::string& ident) {
        auto it = m_parked.find(ident);
        if (it == m_parked.end()) return;
        if (m_time - it->second.second <= 300.0f) m_buckets[peer] = it->second.first;
        m_parked.erase(it);
    }
    std::size_t TrackedPeers() const { return m_calls.size(); }

    /// The enabled rule with a budget for RPC `name` in `scene` (one naming it
    /// exactly first, then "*"), or null: what net_budget / Server Budget use.
    static NetValidator* BudgetFor(Scene& scene, const std::string& name) {
        NetValidator* any = nullptr;
        for (auto* nv : scene.FindObjectsOfType<NetValidator>()) {
            if (!nv->enabled || nv->budget <= 0.0f) continue;
            if (nv->rpc == name) return nv;
            if (!any && nv->rpc == "*") any = nv;
        }
        return any;
    }

private:
    struct Window { float start = 0.0f; int count = 0; };
    struct Bucket { float tokens = 0.0f, refilled = 0.0f, last = 0.0f; bool called = false; };
    Bucket& Refilled(std::uint32_t peer) {
        auto it = m_buckets.find(peer);
        if (it == m_buckets.end()) it = m_buckets.emplace(peer, Bucket{budget, m_time, 0.0f, false}).first;
        Bucket& b = it->second;
        if (refillPerSecond > 0.0f) b.tokens = std::fmin(budget, b.tokens + (m_time - b.refilled) * refillPerSecond);
        b.refilled = m_time;
        return b;
    }
    std::unordered_map<std::uint32_t, Window> m_calls;
    std::unordered_map<std::uint32_t, Bucket> m_buckets;   // cooldown + budget, per peer
    std::unordered_map<std::string, std::pair<Bucket, float>> m_parked;   // ident -> a peer's bucket when it left, and when
    float m_time = 0.0f;
    float m_sincePrune = 0.0f;
};

} // namespace okay
