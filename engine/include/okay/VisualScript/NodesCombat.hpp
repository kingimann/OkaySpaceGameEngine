#pragma once
// Visual-script nodes for weapons: swing, fire, reload, the rounds left, and
// which weapon definition this object uses. They talk to the Melee Attacker and
// Ranged Weapon on this object, the same way weapon_swing / weapon_fire do.
#include "okay/VisualScript/NodeGraph.hpp"
#include "okay/Components/MeleeAttacker.hpp"
#include "okay/Components/RangedWeapon.hpp"
#include "okay/Components/TopDownShooterController.hpp"
#include <string>

namespace okay::vs {

struct SwingWeaponNode : VsNode {
    bool swung = false;
    int ExecOutCount() const override { return 1; }
    VsValue Eval(int, NodeGraph&, VsContext&) override { return swung; }
    int Exec(NodeGraph&, VsContext& ctx) override {
        auto* m = ctx.gameObject ? ctx.gameObject->GetComponent<MeleeAttacker>() : nullptr;
        swung = m != nullptr;
        if (m) m->Swing();
        return 0;
    }
};

struct FireWeaponNode : VsNode {
    bool fired = false;
    int ExecOutCount() const override { return 1; }
    VsValue Eval(int, NodeGraph&, VsContext&) override { return fired; }
    int Exec(NodeGraph&, VsContext& ctx) override {
        auto* g = ctx.gameObject ? ctx.gameObject->GetComponent<RangedWeapon>() : nullptr;
        fired = g && g->Fire();
        return 0;
    }
};

struct ReloadWeaponNode : VsNode {
    bool started = false;
    int ExecOutCount() const override { return 1; }
    VsValue Eval(int, NodeGraph&, VsContext&) override { return started; }
    int Exec(NodeGraph&, VsContext& ctx) override {
        auto* g = ctx.gameObject ? ctx.gameObject->GetComponent<RangedWeapon>() : nullptr;
        started = g != nullptr;
        if (g) g->Reload();
        return 0;
    }
};

struct WeaponAmmoNode : VsNode {
    VsValue Eval(int, NodeGraph&, VsContext& ctx) override {
        auto* g = ctx.gameObject ? ctx.gameObject->GetComponent<RangedWeapon>() : nullptr;
        return (float)(g ? g->Rounds() : 0);
    }
};

struct SetWeaponNode : VsNode {
    std::string id;
    bool applied = false;
    explicit SetWeaponNode(std::string weaponId) : id(std::move(weaponId)) {}
    int ExecOutCount() const override { return 1; }
    VsValue Eval(int, NodeGraph&, VsContext&) override { return applied; }
    int Exec(NodeGraph&, VsContext& ctx) override {
        GameObject* go = ctx.gameObject;
        applied = go != nullptr;
        if (!go) return 0;
        if (auto* m = go->GetComponent<MeleeAttacker>()) m->weapon = id;
        if (auto* g = go->GetComponent<RangedWeapon>()) g->weapon = id;
        if (auto* s = go->GetComponent<TopDownShooterController>()) s->weapon = id;
        return 0;
    }
};

} // namespace okay::vs
