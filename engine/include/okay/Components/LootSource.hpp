#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Components/ChestInventory.hpp"
#include "okay/Components/GridInventory.hpp"
#include "okay/Components/Inventory.hpp"
#include "okay/Components/ItemPickup.hpp"
#include "okay/Components/NPCController.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include "okay/Core/Random.hpp"
#include "okay/Inventory/LootTable.hpp"
#include <chrono>
#include <cmath>
#include <string>

namespace okay {

/// Rolls a LootTable (`.okayloot`) into this object: into its Inventory or grid bag on
/// Start (a crate that comes pre-filled) or the first time its chest is opened, or —
/// for an NPC or anything with a HealthStat — onto the ground as pickups when it dies.
/// `refill` re-rolls a container that's been emptied after that many seconds; `seed`
/// makes the rolls repeatable (0 = different every time).
class LootSource : public Behaviour {
public:
    enum class When { Start, Open, Death };
    std::string table;        ///< .okayloot path
    When  when = When::Start;
    float refill = 0.0f;      ///< seconds after being emptied before it fills again (0 = only once)
    int   seed = 0;           ///< 0 = random

    void Start() override {
        m_rng.Seed(seed != 0 ? (std::uint64_t)seed
                             : (std::uint64_t)std::chrono::steady_clock::now().time_since_epoch().count() ^ (std::uint64_t)(std::uintptr_t)this);
        if (when == When::Start) Fill();
    }

    void Update(float dt) override {
        if (when == When::Death) {   // drops once per death (again after a respawn)
            if (Dead()) DropOnGround(); else m_rolled = false;
            return;
        }
        if (when == When::Open && !m_rolled)
            if (auto* ch = gameObject ? gameObject->GetComponent<ChestInventory>() : nullptr)
                if (ch->open) Fill();
        if (refill > 0.0f && m_rolled && Empty()) {
            m_refillTimer += dt;
            if (m_refillTimer >= refill) {
                m_refillTimer = 0.0f;
                if (when == When::Start) Fill(); else m_rolled = false;   // Open: fills on the next open
            }
        } else {
            m_refillTimer = 0.0f;
        }
    }

    // A dead NPC removes itself and a dead HealthStat deactivates its object (no more
    // Update), so the drop also happens when it goes away.
    void OnDestroy() override { if (when == When::Death && Dead()) DropOnGround(); }
    void OnDisable() override { if (when == When::Death && Dead()) DropOnGround(); }

    /// Roll the table into this object's Inventory (else its grid bag); what doesn't fit
    /// lands next to it as pickups. Returns how many items it rolled.
    int Fill() {
        m_rolled = true;
        int n = 0;
        for (const auto& s : Roll()) {
            n += s.count;
            int left = s.count;
            if (auto* inv = gameObject->GetComponent<Inventory>()) left = inv->AddPartial(s.item, left);
            else if (auto* g = gameObject->GetComponent<GridInventory>()) for (; left > 0 && g->AddDefined(s.item, 1) >= 0; --left) {}
            if (left > 0) Scatter(s.item, left, 0);
        }
        return n;
    }

private:
    std::vector<Inventory::Stack> Roll() {
        if (table.empty() || !gameObject) return {};
        return LootTable::Cached(table).Roll(m_rng);
    }
    bool Dead() const {
        if (!gameObject) return false;
        if (auto* n = gameObject->GetComponent<NPCController>()) if (n->IsDead()) return true;
        if (auto* h = gameObject->GetComponent<HealthStat>()) if (h->IsDead()) return true;
        return false;
    }
    bool Empty() const {
        if (auto* inv = gameObject->GetComponent<Inventory>()) return inv->SlotsUsed() == 0;
        if (auto* g = gameObject->GetComponent<GridInventory>()) return g->items.empty();
        return true;
    }
    void DropOnGround() {
        if (m_rolled) return;
        m_rolled = true;
        int k = 0;
        for (const auto& s : Roll()) Scatter(s.item, s.count, k++);
    }
    void Scatter(const std::string& item, int count, int k) {   // spread pickups in a small ring
        Scene* sc = gameObject ? gameObject->scene() : nullptr;
        if (!sc || !gameObject->transform) return;
        const float a = k * 2.399963f;   // golden angle
        const Vec3 p = gameObject->transform->Position() + Vec3{std::cos(a) * 0.6f, 0.3f, std::sin(a) * 0.6f};
        ItemPickup::Spawn(*sc, item, count, ItemInstanceData{}, p);
    }

    Random m_rng;
    bool  m_rolled = false;
    float m_refillTimer = 0.0f;
};

} // namespace okay
