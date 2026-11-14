#include "okay/Components/MeleeAttacker.hpp"
#include "okay/Combat/WeaponDefinition.hpp"
#include "okay/Components/ActionList.hpp"
#include "okay/Components/NPCController.hpp"
#include "okay/Components/Stats.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include "okay/Components/SurvivalStats.hpp"
#include "okay/Math/Mathf.hpp"
#include <algorithm>
#include <cmath>

namespace okay {

namespace {

struct MeleeTune {
    float damage = 15.0f;
    float range = 2.5f;
    float arc = 120.0f;
    float recovery = 0.5f;
    float swing = 0.0f;
    float hitStart = 0.0f;
    float hitEnd = 0.0f;
    float knockback = 0.0f;
    int combo = 1;
    float comboWindow = 0.6f;
    float comboScale = 0.25f;
    bool hitbox = false;
    float width = 1.2f;
    float height = 1.6f;
    float depth = 1.4f;
};

Vec3 Flat(const Vec3& v) {
    Vec3 p{v.x, 0.0f, v.z};
    const float m = std::sqrt(p.x * p.x + p.z * p.z);
    return m > 1e-5f ? Vec3{p.x / m, 0.0f, p.z / m} : Vec3{0.0f, 0.0f, 1.0f};
}

bool CanBeHurt(const GameObject* g) {
    return g->GetComponent<HealthStat>() || g->GetComponent<SurvivalStats>() ||
           g->GetComponent<Stats>() || g->GetComponent<NPCController>();
}

const WeaponDefinition* Chosen(const MeleeAttacker& m) {
    const WeaponDefinition* w = FindWeapon(m.weapon, m.weaponFile);
    if (!w && m.weapon.empty() && m.weaponFile.empty()) w = WeaponForHolder(m.gameObject);
    if (w && w->kind != WeaponKind::Melee) return nullptr;
    return w;
}

MeleeTune TuneOf(const MeleeAttacker& m) {
    MeleeTune t;
    t.damage = m.damage;
    t.range = m.range;
    t.arc = m.arc;
    t.recovery = m.cooldown;
    t.swing = m.swingTime;
    t.hitStart = m.hitStart;
    t.hitEnd = m.hitEnd;
    t.knockback = m.knockback;
    t.combo = std::max(1, m.comboSteps);
    t.comboWindow = m.comboWindow;
    t.comboScale = m.comboBonus;
    t.hitbox = m.useHitbox;
    t.width = m.hitboxWidth;
    t.height = m.hitboxHeight;
    t.depth = m.hitboxDepth;
    if (const WeaponDefinition* w = Chosen(m)) {
        t.damage = w->damage;
        t.range = w->range;
        t.arc = w->arc;
        t.knockback = w->knockback;
        t.swing = w->swingTime;
        t.hitStart = w->hitStart;
        t.hitEnd = w->hitEnd;
        t.combo = std::max(1, w->combo);
        t.comboWindow = w->comboWindow;
        t.comboScale = w->comboScale;
        t.hitbox = w->useHitbox;
        t.width = w->hitboxWidth;
        t.height = w->hitboxHeight;
        t.depth = w->hitboxDepth;
        t.recovery = std::max(m.cooldown, w->swingTime);
    }
    return t;
}

bool Inside(const MeleeAttacker& m, const MeleeTune& t, const Vec3& target) {
    if (!m.transform) return false;
    const Vec3 pos = m.transform->Position();
    const Vec3 fwd = Flat(m.transform->Forward());
    const Vec3 right{-fwd.z, 0.0f, fwd.x};
    const Vec3 d = target - pos;
    if (t.hitbox) {
        const float side = d.x * right.x + d.z * right.z;
        const float ahead = d.x * fwd.x + d.z * fwd.z;
        return std::fabs(side) <= t.width * 0.5f && d.y >= -0.25f && d.y <= t.height &&
               ahead >= 0.0f && ahead <= t.depth;
    }
    const float dist = std::sqrt(d.x * d.x + d.z * d.z);
    if (dist > t.range || dist < 1e-4f) return false;
    const float cosHalf = std::cos(t.arc * 0.5f * Mathf::Deg2Rad);
    const float dot = (d.x / dist) * fwd.x + (d.z / dist) * fwd.z;
    return dot >= cosHalf - 1e-4f;
}

} // namespace

void MeleeAttacker::Swing() {
    if (!gameObject || !transform || m_swinging) return;
    const MeleeTune t = TuneOf(*this);
    if (m_comboTimer > 0.0f && m_comboStep + 1 < t.combo) ++m_comboStep;
    else m_comboStep = 0;
    m_comboTimer = 0.0f;
    m_hit.clear();
    m_swing = 0.0f;
    m_timer = std::max(0.0f, t.recovery);
    const bool timed = t.swing > 0.0f && t.hitEnd > t.hitStart;
    m_swinging = timed;
    if (!timed) {
        const float amount = t.damage * (1.0f + m_comboStep * t.comboScale);
        const Vec3 pos = transform->Position();
        Scene* scene = gameObject->scene();
        if (scene && amount > 0.0f) {
            for (const auto& up : scene->Objects()) {
                GameObject* g = up.get();
                if (!g || g == gameObject || !g->ActiveInHierarchy() || !g->transform) continue;
                if (g->IsSelfOrDescendantOf(gameObject) || !CanBeHurt(g)) continue;
                if (!Inside(*this, t, g->transform->Position())) continue;
                const Vec3 away = Flat(g->transform->Position() - pos);
                ApplyCombatDamage(g, amount, pos, away * t.knockback);
            }
        }
        m_comboTimer = t.comboWindow;
    }
    if (Scene* scene = gameObject->scene())
        for (ActionList* al : scene->FindObjectsOfType<ActionList>()) al->ReceiveMessage("player_attack");
}

void MeleeAttacker::Update(float dt) {
    if (m_timer > 0.0f) m_timer -= dt;
    if (m_comboTimer > 0.0f) m_comboTimer -= dt;
    if (m_swinging && transform && gameObject) {
        const MeleeTune t = TuneOf(*this);
        const float prev = m_swing;
        m_swing += dt;
        if (m_swing >= t.hitStart && prev <= t.hitEnd) {
            const float amount = t.damage * (1.0f + m_comboStep * t.comboScale);
            const Vec3 pos = transform->Position();
            if (Scene* scene = gameObject->scene(); scene && amount > 0.0f) {
                for (const auto& up : scene->Objects()) {
                    GameObject* g = up.get();
                    if (!g || g == gameObject || !g->ActiveInHierarchy() || !g->transform) continue;
                    if (g->IsSelfOrDescendantOf(gameObject) || !CanBeHurt(g)) continue;
                    if (std::find(m_hit.begin(), m_hit.end(), g) != m_hit.end()) continue;
                    if (!Inside(*this, t, g->transform->Position())) continue;
                    m_hit.push_back(g);
                    const Vec3 away = Flat(g->transform->Position() - pos);
                    ApplyCombatDamage(g, amount, pos, away * t.knockback);
                }
            }
        }
        if (m_swing >= t.swing) {
            m_swinging = false;
            m_comboTimer = t.comboWindow;
        }
    }
    if (!IsLocallyControlled(gameObject)) return;
    if (GameInputBlocked(gameObject ? gameObject->scene() : nullptr)) return;
    const bool pressed = (attackKey && Input::GetKeyDown(attackKey)) ||
                         (useMouse && Input::GetMouseButtonDown(0));
    if (pressed && Ready()) Swing();
}

} // namespace okay
