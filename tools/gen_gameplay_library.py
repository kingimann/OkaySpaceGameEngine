#!/usr/bin/env python3
"""Generate the gameplay library: nodes that act on objects and components.

Each entry in NODES is one distinct node. It carries its own behaviour (C++),
its plain-English name, help line, topic and level, and the scene test that
proves it works. The same list writes:

  engine/include/okay/VisualScript/NodesGameplayLib.hpp   the runtime nodes
  engine/include/okay/VisualScript/gplib_rows.inc         pin types
  editor/generated/gplib_*.inc                            registry, names, help, topic, level, playground
  tests/test_vs_gameplay_library.cpp                      one scene test per node
  examples/visual_script/playground/gl_*.okayvs           one playground graph per topic

Run from the repo root: python3 tools/gen_gameplay_library.py
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PG = "examples/visual_script/playground"

NODES = []

# Input kinds: N number, B yes/no, T text, O object (empty means this object), R required object.
# An input is (name, kind) or (name, kind, runtime fallback as a C++ literal).
def N(name, fb=None): return (name, "N", fb)
def B(name, fb=None): return (name, "B", fb)
def T(name, fb=None): return (name, "T", fb)
def O(name="Target"): return (name, "O", None)
def R(name): return (name, "R", None)

def add(typ, title, topic, sub, level, help_, ins, outs, cpp, *, reg="CAT_DATA",
        exec_=False, exec_outs=None, defaults=None, expect=None, etype="f",
        setup="", post="", check=None, steps=1, dt=0.1, update=False, graph=None, demo=None):
    NODES.append(dict(type=typ, title=title, topic=topic, sub=sub, level=level, help=help_,
                      ins=ins, outs=outs, cpp=cpp, reg=reg, exec=exec_, exec_outs=exec_outs or [],
                      defaults=defaults, expect=expect, etype=etype, setup=setup, post=post, check=check,
                      steps=steps, dt=dt, update=update, graph=graph, demo=demo))

# ---------------------------------------------------------------------------
# Movement
# ---------------------------------------------------------------------------
def movement():
    M = ("Movement", "")
    add("GlGetSpeed", "Get Speed", *M, "Basic", "How fast this object is moving, in units per second. Needs a Rigidbody.",
        [O()], [N("Speed")],
        "Rigidbody3D* rb = o ? GlBody(this, o, g, ctx) : nullptr; out = VsValue(rb ? rb->velocity.Magnitude() : 0.0f);",
        reg="CAT_DATA", setup="me->AddComponent<Rigidbody3D>()->velocity = Vec3(3, 0, 4);", expect=5.0, demo="movement")
    add("GlIsMoving", "Is Moving", *M, "Basic", "True while this object moves faster than Slowest Speed.",
        [N("Slowest Speed", "0.1f"), O()], [B("Result")],
        "Rigidbody3D* rb = o ? GlBody(this, o, g, ctx) : nullptr; out = VsValue(rb && rb->velocity.Magnitude() > n0);",
        reg="CAT_LOGIC", defaults=["0.1", None], setup="me->AddComponent<Rigidbody3D>()->velocity = Vec3(0, 0, 0.5f);",
        expect=True, etype="b")
    add("GlSetVelocity", "Set Velocity", *M, "Intermediate", "Gives a Rigidbody this exact speed on each axis, in units per second.",
        [N("Right"), N("Up"), N("Forward"), O()], [],
        "if (Rigidbody3D* rb = GlBody(this, o, g, ctx)) { rb->velocity = Vec3(n0, n1, n2); rb->WakeUp(); }",
        reg="CAT_XFORM", exec_=True, defaults=["2", "0", "1", None], setup="me->AddComponent<Rigidbody3D>();",
        check="CHECK_NEAR(me->GetComponent<Rigidbody3D>()->velocity.x, 2.0f, 0.01f); CHECK_NEAR(me->GetComponent<Rigidbody3D>()->velocity.z, 1.0f, 0.01f);",
        demo="movement")
    add("GlPushWithImpulse", "Push With Impulse", *M, "Intermediate", "A one-time shove. A heavy Rigidbody moves less than a light one.",
        [N("Right"), N("Up"), N("Forward"), O()], [],
        "if (Rigidbody3D* rb = GlBody(this, o, g, ctx)) rb->AddImpulse(Vec3(n0, n1, n2));",
        reg="CAT_XFORM", exec_=True, defaults=["2", "0", "0", None], setup="me->AddComponent<Rigidbody3D>()->mass = 2.0f;",
        check="CHECK_NEAR(me->GetComponent<Rigidbody3D>()->velocity.x, 1.0f, 0.01f);")
    add("GlStopMoving", "Stop Moving", *M, "Basic", "Sets a Rigidbody's speed and spin to zero.",
        [O()], [],
        "if (Rigidbody3D* rb = GlBody(this, o, g, ctx)) { rb->velocity = Vec3::Zero; rb->angularVelocity = Vec3::Zero; }",
        reg="CAT_XFORM", exec_=True, setup="me->AddComponent<Rigidbody3D>()->velocity = Vec3(1, 2, 3);",
        check="CHECK_NEAR(me->GetComponent<Rigidbody3D>()->velocity.Magnitude(), 0.0f, 0.001f);")
    add("GlMoveForward", "Move Forward", *M, "Basic", "Moves this object along the way it faces, Speed units each second.",
        [N("Speed", "1.0f"), O()], [],
        "if (o && o->transform) o->transform->SetPosition(o->transform->Position() + o->transform->Forward() * (n0 * ctx.deltaTime));",
        reg="CAT_XFORM", exec_=True, defaults=["4", None], update=True, dt=0.5,
        check="{ Vec3 d = me->transform->Position(); Vec3 f = me->transform->Forward(); CHECK_NEAR(d.x, f.x * 2.0f, 0.02f); CHECK_NEAR(d.z, f.z * 2.0f, 0.02f); }",
        demo="movement")
    add("GlMoveSideways", "Move Sideways", *M, "Basic", "Moves this object to its right (negative Speed goes left), Speed units each second.",
        [N("Speed", "1.0f"), O()], [],
        "if (o && o->transform) o->transform->SetPosition(o->transform->Position() + o->transform->Right() * (n0 * ctx.deltaTime));",
        reg="CAT_XFORM", exec_=True, defaults=["2", None], update=True, dt=0.5,
        check="{ Vec3 d = me->transform->Position(); Vec3 r = me->transform->Right(); CHECK_NEAR(d.x, r.x, 0.02f); CHECK_NEAR(d.z, r.z, 0.02f); }")
    add("GlTurnBy", "Turn by Degrees", "Movement", "Turn", "Basic", "Turns this object around its up axis, Degrees each second.",
        [N("Degrees", "90.0f"), O()], [],
        "if (o && o->transform) o->transform->Rotate(Vec3(0.0f, n0 * ctx.deltaTime, 0.0f));",
        reg="CAT_XFORM", exec_=True, defaults=["90", None], update=True, dt=0.5,
        check="CHECK_NEAR(std::fabs(me->transform->EulerAngles().y), 45.0f, 0.5f);", demo="movement")
    add("GlFaceDirection", "Face Direction", "Movement", "Turn", "Intermediate", "Turns this object flat to look along Right and Forward.",
        [N("Right"), N("Forward"), O()], [],
        "if (o && o->transform && (n0 != 0.0f || n1 != 0.0f)) o->transform->LookAtYaw(o->transform->Position() + Vec3(n0, 0.0f, n1));",
        reg="CAT_XFORM", exec_=True, defaults=["1", "0", None],
        check="CHECK_NEAR(me->transform->Forward().x, 1.0f, 0.02f);")
    add("GlKeepInsideBox", "Keep Inside Box", *M, "Intermediate", "Pushes this object back inside a box on the ground plane.",
        [N("Left Edge", "-1.0f"), N("Right Edge", "1.0f"), N("Back Edge", "-1.0f"), N("Front Edge", "1.0f"), O()], [],
        "if (o && o->transform) { Vec3 p = o->transform->Position(); p.x = std::min(std::max(p.x, std::min(n0, n1)), std::max(n0, n1));"
        " p.z = std::min(std::max(p.z, std::min(n2, n3)), std::max(n2, n3)); o->transform->SetPosition(p); }",
        reg="CAT_XFORM", exec_=True, defaults=["-1", "1", "-2", "2", None], setup="me->transform->SetPosition(Vec3(10, 0, -10));",
        check="CHECK_NEAR(me->transform->Position().x, 1.0f, 0.01f); CHECK_NEAR(me->transform->Position().z, -2.0f, 0.01f);",
        demo="movement")
    add("GlWrapAroundBox", "Wrap Around Box", *M, "Advanced", "Leaving one side of a box on the ground plane brings this object back in from the other side.",
        [N("Left Edge", "-1.0f"), N("Right Edge", "1.0f"), N("Back Edge", "-1.0f"), N("Front Edge", "1.0f"), O()], [],
        "if (o && o->transform) { Vec3 p = o->transform->Position(); auto wrap = [](float v, float lo, float hi) {"
        " float size = hi - lo; if (size <= 1e-4f) return lo; float t = std::fmod(v - lo, size); if (t < 0.0f) t += size; return lo + t; };"
        " p.x = wrap(p.x, std::min(n0, n1), std::max(n0, n1)); p.z = wrap(p.z, std::min(n2, n3), std::max(n2, n3)); o->transform->SetPosition(p); }",
        reg="CAT_XFORM", exec_=True, defaults=["-1", "1", "-1", "1", None], setup="me->transform->SetPosition(Vec3(3, 0, -2.5f));",
        check="CHECK_NEAR(me->transform->Position().x, -1.0f, 0.01f); CHECK_NEAR(me->transform->Position().z, -0.5f, 0.01f);")
    add("GlSnapToGrid", "Snap to Grid", *M, "Intermediate", "Rounds this object's position to the nearest multiple of Cell Size.",
        [N("Cell Size", "1.0f"), O()], [],
        "if (o && o->transform && n0 > 1e-4f) { Vec3 p = o->transform->Position();"
        " p.x = std::round(p.x / n0) * n0; p.y = std::round(p.y / n0) * n0; p.z = std::round(p.z / n0) * n0; o->transform->SetPosition(p); }",
        reg="CAT_XFORM", exec_=True, defaults=["1", None], setup="me->transform->SetPosition(Vec3(1.4f, 0.2f, 2.6f));",
        check="CHECK_NEAR(me->transform->Position().x, 1.0f, 0.01f); CHECK_NEAR(me->transform->Position().z, 3.0f, 0.01f);")

# ---------------------------------------------------------------------------
# Health and combat
# ---------------------------------------------------------------------------
def health():
    H = ("Objects", "Survival")
    hp = "me->AddComponent<HealthStat>();"
    add("GlGetHealth", "Get Health", *H, "Basic", "How much health this object has now.",
        [O()], [N("Health")], "HealthStat* h = GpHealth(this, o, g, ctx); out = VsValue(h ? h->health : 0.0f);",
        setup=hp, post="me->GetComponent<HealthStat>()->health = 60;", update=True, expect=60.0, demo="health")
    add("GlGetHealthPortion", "Get Health Portion", *H, "Basic", "Health as a share of the most it can have: 0 is empty, 1 is full.",
        [O()], [N("Portion")], "HealthStat* h = GpHealth(this, o, g, ctx); out = VsValue(h ? h->Fraction() : 0.0f);",
        setup=hp + " me->GetComponent<HealthStat>()->maxHealth = 80;", post="me->GetComponent<HealthStat>()->health = 20;", update=True, expect=0.25)
    add("GlGetMaxHealth", "Get Maximum Health", *H, "Basic", "The most health this object can have.",
        [O()], [N("Maximum")], "HealthStat* h = GpHealth(this, o, g, ctx); out = VsValue(h ? h->maxHealth : 0.0f);",
        setup=hp + " me->GetComponent<HealthStat>()->maxHealth = 80;", expect=80.0)
    add("GlSetHealth", "Set Health", *H, "Intermediate", "Sets health to Amount, never above the maximum.",
        [N("Amount", "100.0f"), O()], [],
        "if (HealthStat* h = GpHealth(this, o, g, ctx)) h->health = std::min(std::max(n0, 0.0f), h->maxHealth);",
        reg="CAT_ACTION", exec_=True, defaults=["30", None], setup=hp,
        check="CHECK_NEAR(me->GetComponent<HealthStat>()->health, 30.0f, 0.01f);", demo="health")
    add("GlSetMaximumHealth", "Set Maximum Health", *H, "Intermediate", "Changes the most health this object can have. Health above the new maximum is cut down.",
        [N("Amount", "100.0f"), O()], [],
        "if (HealthStat* h = GpHealth(this, o, g, ctx)) { h->maxHealth = std::max(1.0f, n0); h->health = std::min(h->health, h->maxHealth); }",
        reg="CAT_ACTION", exec_=True, defaults=["50", None], setup=hp,
        check="CHECK_NEAR(me->GetComponent<HealthStat>()->maxHealth, 50.0f, 0.01f); CHECK_NEAR(me->GetComponent<HealthStat>()->health, 50.0f, 0.01f);")
    add("GlIsHealthLow", "Is Health Low", *H, "Basic", "True when health is at or below this share of the maximum (0.25 means a quarter).",
        [N("Portion", "0.25f"), O()], [B("Result")],
        "HealthStat* h = GpHealth(this, o, g, ctx); out = VsValue(h && h->Fraction() <= n0);",
        reg="CAT_LOGIC", defaults=["0.25", None],
        setup=hp, post="me->GetComponent<HealthStat>()->health = 20;", update=True, expect=True, etype="b")
    add("GlIsHealthFull", "Is Health Full", *H, "Basic", "True when health is at its maximum.",
        [O()], [B("Result")], "HealthStat* h = GpHealth(this, o, g, ctx); out = VsValue(h && h->health >= h->maxHealth - 1e-4f);",
        reg="CAT_LOGIC", setup=hp, expect=True, etype="b")
    add("GlHealToFull", "Heal to Full", *H, "Basic", "Brings health back up to the maximum.",
        [O()], [], "if (HealthStat* h = GpHealth(this, o, g, ctx)) h->Heal(h->maxHealth);",
        reg="CAT_ACTION", exec_=True, setup=hp, post="me->GetComponent<HealthStat>()->health = 10;", update=True,
        check="CHECK_NEAR(me->GetComponent<HealthStat>()->health, 100.0f, 0.01f);")
    add("GlKill", "Kill", *H, "Basic", "Takes all the health away, whatever the armor.",
        [O()], [],
        "if (HealthStat* h = GpHealth(this, o, g, ctx)) if (!h->IsDead()) h->Damage(h->health + h->armor + h->maxHealth + 1000.0f);",
        reg="CAT_ACTION", exec_=True, setup=hp + " me->GetComponent<HealthStat>()->playDeathAnim = false;",
        check="CHECK(me->GetComponent<HealthStat>()->IsDead());")
    add("GlSetInvincible", "Set Invincible", *H, "Intermediate", "While on, no damage gets through.",
        [B("On", "true"), O()], [], "if (HealthStat* h = GpHealth(this, o, g, ctx)) h->godMode = b0;",
        reg="CAT_ACTION", exec_=True, defaults=["true", None], setup=hp,
        check="CHECK(me->GetComponent<HealthStat>()->IsInvincible()); me->GetComponent<HealthStat>()->Damage(40); CHECK_NEAR(me->GetComponent<HealthStat>()->health, 100.0f, 0.01f);")
    add("GlIsInvincible", "Is Invincible", *H, "Intermediate", "True while damage is being ignored.",
        [O()], [B("Result")], "HealthStat* h = GpHealth(this, o, g, ctx); out = VsValue(h && h->IsInvincible());",
        reg="CAT_LOGIC", setup=hp + " me->GetComponent<HealthStat>()->godMode = true;", expect=True, etype="b")
    add("GlDamageOverTime", "Damage Over Time", *H, "Intermediate", "Hurts a little each frame: Per Second times the frame time. Put it under On Every Frame.",
        [N("Per Second", "5.0f"), O()], [],
        "if (HealthStat* h = GpHealth(this, o, g, ctx)) h->Damage(std::max(0.0f, n0) * ctx.deltaTime);",
        reg="CAT_ACTION", exec_=True, defaults=["10", None], update=True, dt=0.5, setup=hp,
        check="CHECK_NEAR(me->GetComponent<HealthStat>()->health, 95.0f, 0.05f);", demo="health")
    add("GlHealOverTime", "Heal Over Time", *H, "Intermediate", "Heals a little each frame: Per Second times the frame time. Put it under On Every Frame.",
        [N("Per Second", "5.0f"), O()], [],
        "if (HealthStat* h = GpHealth(this, o, g, ctx)) h->Heal(std::max(0.0f, n0) * ctx.deltaTime);",
        reg="CAT_ACTION", exec_=True, defaults=["10", None], update=True, dt=0.5, setup=hp, post="me->GetComponent<HealthStat>()->health = 50;",
        check="CHECK_NEAR(me->GetComponent<HealthStat>()->health, 55.0f, 0.05f);")
    add("GlSetArmor", "Set Armor", *H, "Intermediate", "Sets how much each hit is reduced by before health is lost.",
        [N("Amount", "5.0f"), O()], [], "if (HealthStat* h = GpHealth(this, o, g, ctx)) h->armor = std::max(0.0f, n0);",
        reg="CAT_ACTION", exec_=True, defaults=["5", None], setup=hp,
        check="me->GetComponent<HealthStat>()->Damage(8); CHECK_NEAR(me->GetComponent<HealthStat>()->health, 97.0f, 0.01f);")
    add("GlGetArmor", "Get Armor", *H, "Intermediate", "How much each hit is reduced by.",
        [O()], [N("Armor")], "HealthStat* h = GpHealth(this, o, g, ctx); out = VsValue(h ? h->armor : 0.0f);",
        setup=hp + " me->GetComponent<HealthStat>()->armor = 7;", expect=7.0)
    add("GlSetRegeneration", "Set Regeneration", *H, "Intermediate", "Health comes back by Per Second on its own, after Delay seconds without a hit.",
        [N("Per Second", "2.0f"), N("Delay", "3.0f"), O()], [],
        "if (HealthStat* h = GpHealth(this, o, g, ctx)) { h->regenPerSecond = std::max(0.0f, n0); h->regenDelay = std::max(0.0f, n1); }",
        reg="CAT_ACTION", exec_=True, defaults=["4", "1", None], setup=hp,
        check="CHECK_NEAR(me->GetComponent<HealthStat>()->regenPerSecond, 4.0f, 0.01f); CHECK_NEAR(me->GetComponent<HealthStat>()->regenDelay, 1.0f, 0.01f);")
    add("GlDamageAfterArmor", "Get Damage After Armor", *H, "Advanced", "Works out what a hit does once Armor is taken off and Resistance (0 to 1) removes a share of the rest.",
        [N("Damage"), N("Armor"), N("Resistance")], [N("Result")],
        "float r = std::min(std::max(n2, 0.0f), 1.0f); out = VsValue(std::max(0.0f, n0 - std::max(0.0f, n1)) * (1.0f - r));",
        reg="CAT_MATH", defaults=["20", "5", "0.5"], expect=7.5)

# ---------------------------------------------------------------------------
# Inventory and items
# ---------------------------------------------------------------------------
def inventory():
    I = ("Objects", "Items")
    inv = "me->AddComponent<Inventory>(); "
    add("GlCountItem", "Count Item", *I, "Basic", "How many of this item the inventory holds.",
        [T("Item"), O()], [N("Count")], "Inventory* inv = GlInventory(this, o, g, ctx); out = VsValue(inv ? (float)inv->Count(t0) : 0.0f);",
        defaults=['"coin"', None], setup=inv + 'me->GetComponent<Inventory>()->Add("coin", 7);', expect=7.0, demo="inventory")
    add("GlIsInventoryFull", "Is Inventory Full", *I, "Basic", "True when every slot is taken.",
        [O()], [B("Result")], "Inventory* inv = GlInventory(this, o, g, ctx); out = VsValue(inv && inv->IsFull());",
        reg="CAT_LOGIC", setup=inv + 'me->GetComponent<Inventory>()->capacity = 1; me->GetComponent<Inventory>()->Add("coin", 1);', expect=True, etype="b")
    add("GlGetFreeSlots", "Get Free Slots", *I, "Intermediate", "How many slots are still empty.",
        [O()], [N("Slots")], "Inventory* inv = GlInventory(this, o, g, ctx); out = VsValue(inv ? (float)std::max(0, inv->capacity - inv->SlotsUsed()) : 0.0f);",
        setup=inv + 'me->GetComponent<Inventory>()->capacity = 5; me->GetComponent<Inventory>()->Add("coin", 1);', expect=4.0)
    add("GlGetUsedSlots", "Get Used Slots", *I, "Basic", "How many slots hold something.",
        [O()], [N("Slots")], "Inventory* inv = GlInventory(this, o, g, ctx); out = VsValue(inv ? (float)inv->SlotsUsed() : 0.0f);",
        setup=inv + 'me->GetComponent<Inventory>()->Add("coin", 1); me->GetComponent<Inventory>()->Add("key", 1);', expect=2.0)
    add("GlCanFitItem", "Can Fit Item", *I, "Intermediate", "True when Count of this item would fit without leaving any behind.",
        [T("Item"), N("Count", "1.0f"), O()], [B("Result")],
        "Inventory* inv = GlInventory(this, o, g, ctx); out = VsValue(inv && inv->CanAdd(t0, std::max(1, (int)std::lround(n0))) >= std::max(1, (int)std::lround(n0)));",
        reg="CAT_LOGIC", defaults=['"coin"', "3", None], setup=inv, expect=True, etype="b")
    add("GlHasBothItems", "Has Both Items", *I, "Intermediate", "True when the inventory holds enough of the first item and enough of the second.",
        [T("First Item"), N("First Count", "1.0f"), T("Second Item"), N("Second Count", "1.0f"), O()], [B("Result")],
        "Inventory* inv = GlInventory(this, o, g, ctx); out = VsValue(inv && inv->Has(t0, std::max(1, (int)std::lround(n0))) && inv->Has(t1, std::max(1, (int)std::lround(n1))));",
        reg="CAT_LOGIC", defaults=['"coin"', "2", '"key"', "1", None],
        setup=inv + 'me->GetComponent<Inventory>()->Add("coin", 2); me->GetComponent<Inventory>()->Add("key", 1);', expect=True, etype="b")
    add("GlRemoveAllOfItem", "Remove All of Item", *I, "Intermediate", "Takes every one of this item out of the inventory.",
        [T("Item"), O()], [], "if (Inventory* inv = GlInventory(this, o, g, ctx)) { int c = inv->Count(t0); if (c > 0) inv->Remove(t0, c); }",
        reg="CAT_ACTION", exec_=True, defaults=['"coin"', None], setup=inv + 'me->GetComponent<Inventory>()->Add("coin", 9);',
        check='CHECK(me->GetComponent<Inventory>()->Count("coin") == 0);')
    add("GlEmptyInventory", "Empty Inventory", *I, "Intermediate", "Removes everything from the inventory.",
        [O()], [], "if (Inventory* inv = GlInventory(this, o, g, ctx)) inv->Clear();",
        reg="CAT_ACTION", exec_=True, setup=inv + 'me->GetComponent<Inventory>()->Add("coin", 9); me->GetComponent<Inventory>()->Add("key", 1);',
        check="CHECK(me->GetComponent<Inventory>()->SlotsUsed() == 0);")
    add("GlGetCarriedWeight", "Get Carried Weight", *I, "Intermediate", "The combined weight of everything carried.",
        [O()], [N("Weight")], "Inventory* inv = GlInventory(this, o, g, ctx); out = VsValue(inv ? inv->TotalWeight() : 0.0f);",
        setup=inv, expect=0.0)
    add("GlSetInventorySize", "Set Inventory Size", *I, "Intermediate", "Sets how many slots the inventory has.",
        [N("Slots", "20.0f"), O()], [], "if (Inventory* inv = GlInventory(this, o, g, ctx)) inv->capacity = std::max(1, (int)std::lround(n0));",
        reg="CAT_ACTION", exec_=True, defaults=["6", None], setup=inv,
        check="CHECK(me->GetComponent<Inventory>()->capacity == 6);")
    add("GlMoveItem", "Move Item to Object", *I, "Advanced", "Takes Count of an item from this object's inventory and gives it to To. Runs Moved on success, Not Enough or Too Full on failure.",
        [T("Item"), N("Count", "1.0f"), R("To"), O("From")], [],
        "GameObject* dest = o; Inventory* to = dest ? dest->GetComponent<Inventory>() : nullptr; Inventory* from = GlInventory(this, o2, g, ctx);"
        " int want = std::max(1, (int)std::lround(n0)); flow = 2;"
        " if (!to && dest) g.Error(this, ctx, \"the object it moves to has no Inventory\");"
        " else if (from && to) { if (!from->Has(t0, want)) flow = 1; else if (to->CanAdd(t0, want) < want) flow = 2;"
        " else { from->Remove(t0, want); to->Add(t0, want); flow = 0; } }",
        reg="CAT_ACTION", exec_=True, exec_outs=["Moved", "Not Enough", "Too Full"], defaults=['"coin"', "3", None, None],
        graph="giver", demo="inventory")
    NODES[-1]["move_item"] = True

health()
movement()
inventory()

import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import gameplay_nodes_batch2
gameplay_nodes_batch2.define(add, N, B, T, O, R)
import gameplay_nodes_batch3
gameplay_nodes_batch3.define(add, N, B, T, O, R)
import gameplay_nodes_batch4
gameplay_nodes_batch4.define(add, N, B, T, O, R)
import gameplay_nodes_batch5
gameplay_nodes_batch5.define(add, N, B, T, O, R)

# ---------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------
def lint_title(title, kind):
    small = {"a","and","at","by","for","in","is","of","on","or","the","to","up"}
    banned = {"anim","api","bool","dt","euler","float","fov","fps","gameobject","id","idx","ik","int","max","min","nav","num","obj","pos","prefab","old","quat","rad","rigidbody","rot","rpc","string","transform","ui","var","vec","vec3"}
    words = title.split(" ")
    if not words or any(w == "" for w in words):
        return "space"
    for i, w in enumerate(words):
        low = w.lower()
        if low in banned: return "banned " + w
        if any(ch.isdigit() or not ch.isalpha() for ch in w): return "symbol " + w
        if i == 0:
            if not (w[0].isupper() and (len(w) >= 3 or low in small)): return "first " + w
        elif low in small:
            if w != low: return "small " + w
        else:
            if len(w) < 3 or not w[0].isupper() or w[1:] != w[1:].lower(): return "word " + w
    return ""

def kind_of(n):
    if n["exec"]: return "Action"
    if n["title"].startswith(("Is ", "Has ")): return "Question"
    if n["title"].startswith("Get "): return "Reading"
    return "Calculation"

def decl_inputs(n):
    lines = []
    fi = bi = ti = oi = 0
    for i, spec in enumerate(n["ins"]):
        name, kind, fb = spec
        if kind == "N":
            lines.append(f"float n{fi} = In({i}, g, ctx, VsValue{{{fb or '0.0f'}}}).AsFloat(); (void)n{fi};"); fi += 1
        elif kind == "B":
            lines.append(f"bool b{bi} = In({i}, g, ctx, VsValue{{{fb or 'false'}}}).AsBool(); (void)b{bi};"); bi += 1
        elif kind == "T":
            lines.append(f"std::string t{ti} = In({i}, g, ctx).AsString(); (void)t{ti};"); ti += 1
        elif kind == "O":
            tail = "" if oi == 0 else str(oi + 1)
            lines.append(f"GameObject* o{tail} = GpPin(this, {i}, g, ctx, false, \"a target\"); (void)o{tail};"); oi += 1
        elif kind == "R":
            tail = "" if oi == 0 else str(oi + 1)
            lines.append(f"GameObject* o{tail} = GpPin(this, {i}, g, ctx, true, \"{name.lower()}\"); (void)o{tail};"); oi += 1
    return "\n".join(lines)

def fix_object_names(n):
    """The first object pin is `o`, later ones `o2`, `o3`. Move Item reads From as o and To as o2,
    so the order in `ins` decides: required pins come first in the list, From last."""
    return n

def letters(spec_list):
    out = ""
    for _, kind, _ in spec_list:
        out += {"N": "N", "B": "B", "T": "T", "O": "O", "R": "O"}[kind]
    return out

def checks():
    seen = set(); titles = set()
    for n in NODES:
        if n["type"] in seen: raise SystemExit("duplicate type " + n["type"])
        if n["title"] in titles: raise SystemExit("duplicate title " + n["title"])
        seen.add(n["type"]); titles.add(n["title"])
        why = lint_title(n["title"], kind_of(n))
        if why: raise SystemExit(f'{n["type"]} "{n["title"]}": {why}')
        for spec in n["ins"] + n["outs"]:
            why = lint_title(spec[0], "Calculation")
            if why: raise SystemExit(f'{n["type"]} pin "{spec[0]}": {why}')
        for p in n["exec_outs"]:
            why = lint_title(p, "Calculation")
            if why: raise SystemExit(f'{n["type"]} exec "{p}": {why}')
        if len(n["help"]) > 180 or "\n" in n["help"]:
            raise SystemExit(f'{n["type"]}: help must be one line under 180 characters')
        if n["exec"] and n["outs"]:
            raise SystemExit(n["type"] + ": an action has no data outputs")

def main():
    checks()
    # Object pins: o is the FIRST object pin in `ins` order. Reorder helper not needed: Move Item lists To first.
    cases, exec_cases, op_of = [], [], []
    for i, n in enumerate(NODES):
        op_of.append(f'if (type == "{n["type"]}") return {i};')
        body = f"case {i}: {{\n{decl_inputs(n)}\n"
        if n["exec"]:
            exec_cases.append(body + f"int flow = 0; (void)flow;\n{n['cpp']}\nreturn flow;\n}}")
        else:
            cases.append(body + f"VsValue out; (void)pin;\n{n['cpp']}\nreturn out;\n}}")
    exec_count = "\n".join(f"        case {i}: return {max(1, len(n['exec_outs']))};" for i, n in enumerate(NODES) if n["exec"])
    header = f'''#pragma once
// Generated by tools/gen_gameplay_library.py. {len(NODES)} gameplay nodes. Do not edit by hand.
// They act on this object (or a Target) through its components, and report a node
// error when the component they need is missing.
#include "okay/VisualScript/NodesGameplay.hpp"
#include "okay/Components/Inventory.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/AudioSource.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/SpriteRenderer.hpp"
#include "okay/Components/QuestLog.hpp"
#include "okay/Core/Prefs.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Input/Input.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>
namespace okay::vs {{
inline Camera* GlCamera(VsNode* n, GameObject* o, NodeGraph& g, VsContext& ctx) {{
    if (!o) return nullptr;
    if (auto* c = o->GetComponent<Camera>()) return c;
    g.Error(n, ctx, "its object has no Camera");
    return nullptr;
}}
inline AudioSource* GlAudio(VsNode* n, GameObject* o, NodeGraph& g, VsContext& ctx) {{
    if (!o) return nullptr;
    if (auto* a = o->GetComponent<AudioSource>()) return a;
    g.Error(n, ctx, "its object has no Audio Source");
    return nullptr;
}}
inline TextRenderer* GlText(VsNode* n, GameObject* o, NodeGraph& g, VsContext& ctx) {{
    if (!o) return nullptr;
    if (auto* t = o->GetComponent<TextRenderer>()) return t;
    g.Error(n, ctx, "its object has no Text");
    return nullptr;
}}
inline SpriteRenderer* GlSprite(VsNode* n, GameObject* o, NodeGraph& g, VsContext& ctx) {{
    if (!o) return nullptr;
    if (auto* sr = o->GetComponent<SpriteRenderer>()) return sr;
    g.Error(n, ctx, "its object has no Sprite");
    return nullptr;
}}
inline QuestLog* GlQuest(VsContext& ctx) {{
    return QuestLog::Get(ctx.gameObject ? ctx.gameObject->scene() : nullptr, false);
}}
inline float GlRandom01() {{
    static std::uint32_t seed = 2463534242u;
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
    return (float)(seed & 0xFFFFFF) / 16777216.0f;
}}
inline std::string GlNumberText(float v, int decimals) {{
    char b[64];
    std::snprintf(b, sizeof b, "%.*f", std::min(std::max(decimals, 0), 6), (double)v);
    return b;
}}
/// Degrees between where `looker` faces and the direction to `target` (0 ahead, 180 behind).
inline float GlAngleTo(GameObject* looker, GameObject* target) {{
    if (!looker || !target || !looker->transform || !target->transform) return 0.0f;
    Vec3 d = target->transform->Position() - looker->transform->Position();
    const float len = d.Magnitude();
    if (len < 1e-5f) return 0.0f;
    d = d * (1.0f / len);
    const Vec3 f = looker->transform->Forward();
    float c = f.x * d.x + f.y * d.y + f.z * d.z;
    c = std::min(std::max(c, -1.0f), 1.0f);
    return std::acos(c) * 57.2957795f;
}}
inline Rigidbody3D* GlBody(VsNode* n, GameObject* o, NodeGraph& g, VsContext& ctx) {{
    if (!o) return nullptr;
    if (auto* rb = o->GetComponent<Rigidbody3D>()) return rb;
    g.Error(n, ctx, "its object has no Rigidbody (add one to give it a speed)");
    return nullptr;
}}
inline Inventory* GlInventory(VsNode* n, GameObject* o, NodeGraph& g, VsContext& ctx) {{
    if (!o) return nullptr;
    if (auto* inv = o->GetComponent<Inventory>()) return inv;
    g.Error(n, ctx, "its object has no Inventory");
    return nullptr;
}}
struct GameplayLibNode : VsNode {{
    int op = 0;
    float mem = 0, mem2 = 0, mem3 = 0;
    int count = 0;
    bool seen = false, flag = false;
    explicit GameplayLibNode(int o) : op(o) {{}}
    int ExecOutCount() const override {{
        switch (op) {{
{exec_count}
        default: return 0;
        }}
    }}
    VsValue Eval(int pin, NodeGraph& g, VsContext& ctx) override {{
        switch (op) {{
{chr(10).join(cases)}
        default: return {{}};
        }}
    }}
    int Exec(NodeGraph& g, VsContext& ctx) override {{
        switch (op) {{
{chr(10).join(exec_cases)}
        default: break;
        }}
        return 0;
    }}
}};
inline int GameplayLibOp(const std::string& type) {{
    {" ".join(op_of)}
    return -1;
}}
inline std::unique_ptr<VsNode> MakeGameplayLibNode(const std::string& type) {{
    const int op = GameplayLibOp(type);
    if (op < 0) return nullptr;
    return std::make_unique<GameplayLibNode>(op);
}}
}}
'''
    (ROOT / "engine/include/okay/VisualScript/NodesGameplayLib.hpp").write_text(header)

    rows, reg, names, helps, places, levels, pg = [], [], [], [], [], [], []
    for n in NODES:
        rows.append(f'        {{"{n["type"]}", {"true" if n["exec"] else "false"}, "{letters(n["ins"])}", "{letters(n["outs"])}"}},')
        eouts = ", ".join(f'"{p}"' for p in (n["exec_outs"] or ([""] if n["exec"] else [])))
        dins = ", ".join(f'"{s[0]}"' for s in n["ins"])
        douts = ", ".join(f'"{s[0]}"' for s in n["outs"])
        reg.append(f'        {{"{n["type"]}", "{n["title"]}", {n["reg"]}, {"true" if n["exec"] else "false"}, {{{eouts}}}, {{{dins}}}, {{{douts}}}, 0, nullptr}},')
        names.append(f'    {{"{n["type"]}", VsNameKind::{kind_of(n)}, "{n["title"]}", "{n["title"]}", "", nullptr, 0, nullptr, 0}},')
        helps.append(f'        {{"{n["type"]}", "{n["help"]}"}},')
        sub = f', "{n["sub"]}"' if n["sub"] else ""
        places.append(f'    if (std::strcmp(type, "{n["type"]}") == 0) return VsAt("{n["topic"]}"{sub});')
        levels.append(f'    {{"{n["type"]}", VsLevel::{n["level"]}}},')
        pg.append(f'        {{"{n["type"]}", "gl_{pgname(n)}"}},')
    (ROOT / "engine/include/okay/VisualScript/gplib_rows.inc").write_text("\n".join(rows) + "\n")
    gen = ROOT / "editor/generated"
    gen.mkdir(exist_ok=True)
    (gen / "gplib_reg.inc").write_text("\n".join(reg) + "\n")
    (gen / "gplib_names.inc").write_text("\n".join(names) + "\n")
    (gen / "gplib_help.inc").write_text("\n".join(helps) + "\n")
    (gen / "gplib_place.inc").write_text("\n".join(places) + "\n")
    (gen / "gplib_levels.inc").write_text("\n".join(levels) + "\n")
    (gen / "gplib_playground.inc").write_text("\n".join(pg) + "\n")
    demos = []
    for name in sorted({pgname(n) for n in NODES}):
        title, focus, blurb, _ = PG_INFO[name]
        demos.append(f'        {{"gl_{name}", "{title}", "gl_{name}.okayscene", "gl_{name}.okayvs", "{focus}",\n         "{blurb}"}},')
    (gen / "gplib_demos.inc").write_text("\n".join(demos) + "\n")
    write_tests()
    write_playgrounds()
    (ROOT / "tools/gameplay_library_count.txt").write_text(str(len(NODES)) + "\n")
    print("gameplay library nodes", len(NODES))

def pgname(n):
    if n["demo"]: return n["demo"]
    by_topic = {("Physics", "Queries"): "ai", ("Camera", ""): "camera", ("UI", ""): "ui", ("UI", "Fields"): "ui", ("Audio", ""): "audio",
                ("Physics", ""): "physics", ("Animation", "Procedural"): "animation", ("Scene", "Story"): "quests",
                ("Variables", "Saved"): "saving", ("Objects", "Groups"): "groups", ("Time", ""): "timing",
                ("Input", "Axes"): "input", ("Input", "Keys"): "input", ("Input", "Mouse"): "input", ("Objects", "Survival"): "health",
                ("Objects", "Items"): "inventory", ("Movement", ""): "movement", ("Movement", "Turn"): "movement", ("Movement", "Size"): "movement",
                ("Math", "Progression"): "progression"}
    return by_topic[(n["topic"], n["sub"])]

# One playground per topic: (menu title, focus object, what you see, C++ that adds the scene's objects).
PG_INFO = {
    "movement": ("Gameplay - Movement", "Hero",
        "The white cube drives forward, turns, and is kept inside a box. The rest of the movement nodes are in the graph.",
        "GameObject* hero = sc.CreateGameObject(\"Hero\"); hero->transform->SetPosition(Vec3(0, 0.5f, -3)); hero->AddComponent<MeshRenderer>()->color = Color(0.92f, 0.92f, 0.96f, 1); hero->AddComponent<VisualScriptComponent>()->SetPath(file);"),
    "health": ("Gameplay - Health", "Fighter",
        "The fighter loses health every second, and Get Health reads it into a variable. The other health nodes are in the graph.",
        "GameObject* hero = sc.CreateGameObject(\"Fighter\"); hero->transform->SetPosition(Vec3(0, 0.8f, 0)); hero->transform->SetScale(Vec3(0.8f, 1.6f, 0.8f)); hero->AddComponent<MeshRenderer>()->color = Color(0.3f, 0.55f, 0.95f, 1); hero->AddComponent<HealthStat>(); hero->AddComponent<VisualScriptComponent>()->SetPath(file);"),
    "inventory": ("Gameplay - Inventory", "Backpack",
        "The backpack is given five coins, and Count Item reads how many it holds. The other item nodes are in the graph.",
        "GameObject* hero = sc.CreateGameObject(\"Backpack\"); hero->transform->SetPosition(Vec3(0, 0.5f, 0)); hero->AddComponent<MeshRenderer>()->color = Color(0.85f, 0.62f, 0.25f, 1); hero->AddComponent<Inventory>(); hero->AddComponent<VisualScriptComponent>()->SetPath(file);"),
}

def _hero(name, pos, color, extra=""):
    return ("GameObject* hero = sc.CreateGameObject(\"" + name + "\"); hero->transform->SetPosition(Vec3" + pos + "); "
            "hero->AddComponent<MeshRenderer>()->color = Color" + color + "; " + extra + " hero->AddComponent<VisualScriptComponent>()->SetPath(file);")

PG_INFO.update({
    "ai": ("Gameplay - AI and Perception", "Watcher",
        "The watcher measures its angle to the red goal and counts tagged objects. The other perception nodes are in the graph.",
        _hero("Watcher", "(0, 0.5f, 0)", "(0.3f, 0.7f, 0.9f, 1)") + ' GameObject* gl = sc.CreateGameObject("Goal"); gl->transform->SetPosition(Vec3(3, 0.5f, 4)); gl->tag = "enemy"; gl->AddComponent<MeshRenderer>()->color = Color(0.9f, 0.25f, 0.25f, 1);'),
    "camera": ("Gameplay - Camera", "Main Camera",
        "The camera zooms in a little and gives one shake when you press Play. The other camera nodes are in the graph.",
        "cam->AddComponent<VisualScriptComponent>()->SetPath(file); GameObject* mark = sc.CreateGameObject(\"Landmark\"); mark->transform->SetPosition(Vec3(0, 0.5f, 0)); mark->AddComponent<MeshRenderer>()->color = Color(0.9f, 0.8f, 0.3f, 1);"),
    "ui": ("Gameplay - UI Text", "Hud",
        "The HUD text shows Score: 42. The other text nodes are in the graph.",
        "GameObject* hero = sc.CreateGameObject(\"Hud\"); auto* hud = hero->AddComponent<TextRenderer>(); hud->text = \"Score\"; hud->screenSpace = true; hud->screenPos = {24, 200}; hud->pixelSize = 3.2f; hud->size = {600, 80}; hud->color = Color{0.45f, 1.0f, 0.55f, 1.0f}; hero->AddComponent<VisualScriptComponent>()->SetPath(file);"),
    "audio": ("Gameplay - Audio", "Speaker",
        "The speaker's volume is set to half. The other audio nodes are in the graph.",
        _hero("Speaker", "(0, 0.5f, 0)", "(0.9f, 0.5f, 0.2f, 1)", "hero->AddComponent<AudioSource>();")),
    "physics": ("Gameplay - Physics", "Ball",
        "The ball falls slowly and bounces. The other physics nodes are in the graph.",
        _hero("Ball", "(0, 4, 0)", "(0.95f, 0.85f, 0.3f, 1)", "hero->AddComponent<Rigidbody3D>();")),
    "animation": ("Gameplay - Procedural Animation", "Pulser",
        "The cube swells and shrinks and sways. The other animation nodes are in the graph.",
        _hero("Pulser", "(0, 0.7f, 0)", "(0.6f, 0.9f, 0.5f, 1)", "hero->AddComponent<SpriteRenderer>();")),
    "quests": ("Gameplay - Quests", "Guide",
        "A quest log with one quest, and a guide whose graph holds the quest nodes.",
        _hero("Guide", "(0, 0.5f, 0)", "(0.7f, 0.5f, 0.9f, 1)", "GameObject* qo = sc.CreateGameObject(\"Quests\"); auto* ql = qo->AddComponent<QuestLog>(); ql->drawUI = false; QuestDef qd; qd.id = \"q1\"; qd.title = \"First quest\"; ql->quests.push_back(qd);")),
    "saving": ("Gameplay - Saving", "Courier",
        "The courier's graph holds the save and load position nodes.",
        _hero("Courier", "(2, 0.5f, 1)", "(0.9f, 0.9f, 0.9f, 1)")),
    "groups": ("Gameplay - Groups of Objects", "Boss",
        "Three crates are hidden when you press Play. The other group nodes are in the graph.",
        _hero("Boss", "(0, 0.5f, -3)", "(0.8f, 0.3f, 0.3f, 1)") + ' for (int i = 0; i < 3; ++i) { GameObject* cr = sc.CreateGameObject("Crate" + std::to_string(i)); cr->tag = "crate"; cr->transform->SetPosition(Vec3(-2.0f + 2.0f * i, 0.5f, 1)); cr->AddComponent<MeshRenderer>()->color = Color(0.7f, 0.5f, 0.3f, 1); }'),
    "progression": ("Gameplay - Levels and Prices", "Scorekeeper",
        "The scorekeeper works out a level from experience. The other progression nodes are in the graph.",
        _hero("Scorekeeper", "(0, 0.5f, 0)", "(0.95f, 0.8f, 0.4f, 1)")),
    "timing": ("Gameplay - Time", "Clock",
        "The clock's graph holds the slow motion and game speed nodes.",
        _hero("Clock", "(0, 0.5f, 0)", "(0.5f, 0.8f, 0.9f, 1)")),
    "input": ("Gameplay - Input", "Player",
        "The player's graph holds the horizontal and vertical input, sprint and crouch nodes.",
        _hero("Player", "(0, 0.5f, 0)", "(0.4f, 0.9f, 0.6f, 1)")),
})

PLAYGROUNDS = {}

def write_playgrounds():
    by = {}
    for n in NODES:
        by.setdefault(pgname(n), []).append(n)
    for name, nodes in by.items():
        lines = [f"# Gameplay library: {name}. Every node below is declared here; the first chain runs in Play."]
        lines += PLAYGROUND_DEMO.get(name, [])
        nid = 100
        col = row = 0
        for n in nodes:
            x = 40 + (col % 6) * 260
            y = 520 + row * 190
            lines.append(f"node {nid} {n['type']}")
            lines.append(f"pos {nid} {x} {y}")
            for i, d in enumerate(n["defaults"] or []):
                if d is not None: lines.append(f"default {nid} {i} {d}")
            nid += 1
            col += 1
            if col % 6 == 0: row += 1
        (ROOT / PG / f"gl_{name}.okayvs").write_text("\n".join(lines) + "\n")

PLAYGROUND_DEMO = {
    "progression": [
        "node 1 OnStart", "pos 1 40 40", "entry OnStart 1",
        "node 2 GlLevelFromExperience", "pos 2 300 240", "default 2 0 450", "default 2 1 100",
        "node 3 SetVar level", "pos 3 300 40", "exec 1 0 3", "data 3 0 2 0",
    ],
    "animation": [
        "node 1 OnUpdate", "pos 1 40 40", "entry OnUpdate 1",
        "node 2 GlPulseSize", "pos 2 300 40", "default 2 0 2", "default 2 1 0.25", "exec 1 0 2",
        "node 3 GlSwayTurn", "pos 3 560 40", "default 3 0 1.5", "default 3 1 25", "exec 2 0 3",
    ],
    "camera": [
        "node 1 OnStart", "pos 1 40 40", "entry OnStart 1",
        "node 2 GlSetCameraZoom", "pos 2 300 40", "default 2 0 1.3", "exec 1 0 2",
        "node 3 GlShakeCamera", "pos 3 560 40", "default 3 0 0.3", "exec 2 0 3",
    ],
    "ui": [
        "node 1 OnStart", "pos 1 40 40", "entry OnStart 1",
        "node 2 GlShowNumberInText", "pos 2 300 40", "default 2 0 \"Score: \"", "default 2 1 42", "default 2 2 0", "exec 1 0 2",
    ],
    "audio": [
        "node 1 OnStart", "pos 1 40 40", "entry OnStart 1",
        "node 2 GlSetVolume", "pos 2 300 40", "default 2 0 0.5", "exec 1 0 2",
    ],
    "physics": [
        "node 1 OnStart", "pos 1 40 40", "entry OnStart 1",
        "node 2 GlSetGravityStrength", "pos 2 300 40", "default 2 0 0.3", "exec 1 0 2",
        "node 3 GlSetBounciness", "pos 3 560 40", "default 3 0 0.7", "exec 2 0 3",
    ],
    "ai": [
        "node 1 OnUpdate", "pos 1 40 40", "entry OnUpdate 1",
        "node 2 GlGetAngleToObject", "pos 2 300 240", "default 2 0 Goal",
        "node 3 SetVar angle", "pos 3 300 40", "exec 1 0 3", "data 3 0 2 0",
    ],
    "groups": [
        "node 1 OnStart", "pos 1 40 40", "entry OnStart 1",
        "node 2 GlHideAllWithTag", "pos 2 300 40", "default 2 0 crate", "exec 1 0 2",
    ],
    "movement": [
        "node 1 OnUpdate", "pos 1 40 40", "entry OnUpdate 1",
        "node 2 GlMoveForward", "pos 2 300 40", "default 2 0 2", "exec 1 0 2",
        "node 3 GlTurnBy", "pos 3 560 40", "default 3 0 60", "exec 2 0 3",
        "node 4 GlKeepInsideBox", "pos 4 820 40", "default 4 0 -6", "default 4 1 6", "default 4 2 -6", "default 4 3 6", "exec 3 0 4",
    ],
    "health": [
        "node 1 OnUpdate", "pos 1 40 40", "entry OnUpdate 1",
        "node 2 GlDamageOverTime", "pos 2 300 40", "default 2 0 12", "exec 1 0 2",
        "node 3 GlGetHealth", "pos 3 300 240",
        "node 4 SetVar health", "pos 4 560 40", "exec 2 0 4", "data 4 0 3 0",
        "node 5 OnStart", "pos 5 40 340", "entry OnStart 5",
        "node 6 GlSetHealth", "pos 6 300 340", "default 6 0 100", "exec 5 0 6",
    ],
    "inventory": [
        "node 1 OnStart", "pos 1 40 40", "entry OnStart 1",
        "node 2 AddItem", "pos 2 300 40", "default 2 0 coin", "default 2 1 5", "exec 1 0 2",
        "node 3 GlCountItem", "pos 3 300 240", "default 3 0 coin",
        "node 4 SetVar coins", "pos 4 560 40", "exec 2 0 4", "data 4 0 3 0",
    ],
}

def write_tests():
    prelude = '''// Generated by tools/gen_gameplay_library.py. Every gameplay library node runs in a scene.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include "okay/Components/Inventory.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/AudioSource.hpp"
#include "okay/Components/SpriteRenderer.hpp"
#include "okay/Components/QuestLog.hpp"
#include "okay/Core/Prefs.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include <cmath>
#include <fstream>
#include <set>
#include <string>
using namespace okay;
static std::set<std::string> g_seen;
static std::vector<std::string> g_errs;
static VisualScriptComponent* Attach(GameObject* go, const std::string& text) {
    auto* vc = go->AddComponent<VisualScriptComponent>();
    std::string err;
    CHECK(vc->LoadFromText(text, &err));
    if (!err.empty()) std::printf("  load %s: %s\\n", go->name.c_str(), err.c_str());
    return vc;
}
static vs::VsValue Var(VisualScriptComponent* v, const char* name) { return v->GetVariable(name); }
static void OnErr(const vs::VsRuntimeError& e) { g_errs.push_back(e.nodeType + ": " + e.message); }
'''
    blocks = []
    for n in NODES:
        if n.get("move_item"): continue
        entry = "OnUpdate" if n["update"] else "OnStart"
        lines = [f"node 1 {entry}", f"entry {entry} 1", f'node 2 {n["type"]}']
        for i, d in enumerate(n["defaults"] or []):
            if d is not None: lines.append(f"default 2 {i} {d}")
        if n["exec"]:
            lines.append("exec 1 0 2")
        else:
            lines += ["node 3 SetVar out", "exec 1 0 3", "data 3 0 2 0"]
        text = ("\\n".join(lines) + "\\n").replace('"', '\\"')
        if n["check"] is not None:
            check = n["check"]
        elif n["etype"] == "f":
            check = f'CHECK_NEAR(Var(v, "out").AsFloat(), {float(n["expect"])}f, 0.02f);'
        elif n["etype"] == "b":
            check = f'CHECK(Var(v, "out").AsBool() == {"true" if n["expect"] else "false"});'
        else:
            check = f'CHECK(Var(v, "out").AsString() == "{n["expect"]}");'
        blocks.append(f'''    {{
        Scene s; s.physicsEnabled = false;
        GameObject* me = s.CreateGameObject("Me");
        {n["setup"]}
        auto* v = Attach(me, "{text}");
        g_errs.clear();
        s.Start();
        {n["post"]}
        for (int i = 0; i < {n["steps"]}; ++i) s.Update({n["dt"]}f);
        {check}
        if (!g_errs.empty()) {{ std::printf("  {n["type"]} reported: %s\\n", g_errs[0].c_str()); CHECK(g_errs.empty()); }}
        Input::FeedKeys({{}});
        g_seen.insert("{n["type"]}");
    }}''')
    move = r'''    {   // Move Item To Object: the Moved branch, then Not Enough, then Too Full.
        Scene s; s.physicsEnabled = false;
        GameObject* giver = s.CreateGameObject("Giver");
        GameObject* taker = s.CreateGameObject("Taker");
        giver->AddComponent<Inventory>()->Add("coin", 5);
        taker->AddComponent<Inventory>();
        auto* v = Attach(giver,
            "node 1 OnStart\nentry OnStart 1\nnode 2 GlMoveItem\ndefault 2 0 \"coin\"\ndefault 2 1 3\ndefault 2 2 Taker\n"
            "node 3 SetVar branch\ndefault 3 0 moved\nnode 4 SetVar branch\ndefault 4 0 short\nnode 5 SetVar branch\ndefault 5 0 full\n"
            "exec 1 0 2\nexec 2 0 3\nexec 2 1 4\nexec 2 2 5\n");
        g_errs.clear();
        s.Start();
        CHECK(giver->GetComponent<Inventory>()->Count("coin") == 2);
        CHECK(taker->GetComponent<Inventory>()->Count("coin") == 3);
        CHECK(Var(v, "branch").AsString() == "moved");
        // Not Enough: ask for more than is left.
        auto* v2 = Attach(giver,
            "node 1 OnStart\nentry OnStart 1\nnode 2 GlMoveItem\ndefault 2 0 \"coin\"\ndefault 2 1 10\ndefault 2 2 Taker\n"
            "node 3 SetVar branch\ndefault 3 0 moved\nnode 4 SetVar branch\ndefault 4 0 short\nnode 5 SetVar branch\ndefault 5 0 full\n"
            "exec 1 0 2\nexec 2 0 3\nexec 2 1 4\nexec 2 2 5\n");
        v2->Start();
        CHECK(Var(v2, "branch").AsString() == "short");
        CHECK(giver->GetComponent<Inventory>()->Count("coin") == 2);
        // No Room: the taker has no free slot.
        GameObject* tiny = s.CreateGameObject("Tiny");
        tiny->AddComponent<Inventory>()->capacity = 1;
        tiny->GetComponent<Inventory>()->Add("key", 1);
        auto* v3 = Attach(giver,
            "node 1 OnStart\nentry OnStart 1\nnode 2 GlMoveItem\ndefault 2 0 \"coin\"\ndefault 2 1 1\ndefault 2 2 Tiny\n"
            "node 3 SetVar branch\ndefault 3 0 moved\nnode 4 SetVar branch\ndefault 4 0 short\nnode 5 SetVar branch\ndefault 5 0 full\n"
            "exec 1 0 2\nexec 2 0 3\nexec 2 1 4\nexec 2 2 5\n");
        v3->Start();
        CHECK(Var(v3, "branch").AsString() == "full");
        CHECK(g_errs.empty());
        g_seen.insert("GlMoveItem");
    }'''
    names = sorted({pgname(n) for n in NODES})
    builders = []
    for name in names:
        title, focus, blurb, cpp = PG_INFO[name]
        builders.append(f'    if (name == "{name}") {{ {cpp} }}')
    pg_types = []
    for name in names:
        pg_types.append("        {\"" + name + "\", {" + ", ".join(f'"{n["type"]}"' for n in NODES if pgname(n) == name) + "}},")
    types = ", ".join(f'"{n["type"]}"' for n in NODES)
    playground = f'''
static const char* kDir = "{PG}";
static void BuildPlayground(Scene& sc, const std::string& name, const std::string& file) {{
    GameObject* cam = sc.CreateGameObject("Main Camera");
    cam->transform->SetPosition(Vec3(0, 7, -11));
    cam->AddComponent<Camera>();
    cam->transform->LookAt(Vec3(0, 0, 0));
    GameObject* help = sc.CreateGameObject("Instructions");
    auto* ht = help->AddComponent<TextRenderer>();
    ht->text = "Press Play. " + name;
    ht->screenSpace = true; ht->screenPos = {{24, 28}}; ht->pixelSize = 2.2f;
    ht->size = {{900, 120}}; ht->wrap = true; ht->color = Color::White;
    GameObject* floor = sc.CreateGameObject("Floor");
    floor->transform->SetPosition(Vec3(0, -0.15f, 0));
    floor->transform->SetScale(Vec3(16, 0.3f, 16));
    floor->AddComponent<MeshRenderer>()->color = Color(0.18f, 0.2f, 0.24f, 1);
{chr(10).join(builders)}
}}
static std::string ReadFile(const std::string& path) {{
    std::ifstream in(path);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}}
static void CheckPlaygrounds() {{
    struct Row {{ const char* name; std::vector<const char*> nodes; }};
    const Row rows[] = {{
{chr(10).join(pg_types)}
    }};
    const std::string dir = std::string(OKAY_SOURCE_DIR) + "/" + kDir + "/";
    for (const Row& r : rows) {{
        const std::string base = std::string("gl_") + r.name;
        if (const char* w = std::getenv("OKAY_WRITE_GL_PLAYGROUND"); w && w[0] == '1') {{
            Scene writing; BuildPlayground(writing, r.name, base + ".okayvs");
            CHECK(SceneSerializer::SaveToFile(writing, dir + base + ".okayscene"));
        }}
        const std::string graph = ReadFile(dir + base + ".okayvs");
        CHECK(!graph.empty());
        for (const char* t : r.nodes) {{
            const bool listed = graph.find(std::string(" ") + t + "\\n") != std::string::npos;
            if (!listed) std::printf("  playground %s is missing %s\\n", r.name, t);
            CHECK(listed);
        }}
        Scene scene; std::string err;
        CHECK(SceneSerializer::LoadFromFile(scene, dir + base + ".okayscene", &err));
        if (!err.empty()) std::printf("  load %s: %s\\n", r.name, err.c_str());
        bool told = false, script = false;
        for (const auto& up : scene.Objects()) {{
            if (!up) continue;
            if (auto* t = up->GetComponent<TextRenderer>()) if (t->text.find("Play") != std::string::npos) told = true;
            for (auto* vc : up->GetComponents<VisualScriptComponent>()) if (vc->Graph() && vc->Graph()->NodeCount() > 0) script = true;
        }}
        CHECK(told); CHECK(script);
        g_errs.clear();
        scene.Start();
        for (int i = 0; i < 180; ++i) scene.Update(1.0f / 60.0f);
        if (!g_errs.empty()) for (const auto& e : g_errs) std::printf("  %s: %s\\n", r.name, e.c_str());
        CHECK(g_errs.empty());
        g_seen.insert(std::string("playground:") + r.name);
    }}
}}
'''
    test = prelude + playground + f'''int main() {{
    RUN_SUITE("vs_gameplay_library");
    vs::SetRuntimeErrorHook(&OnErr);
    const char* types[] = {{ {types} }};
{chr(10).join(blocks)}
{move}
    CheckPlaygrounds();
    for (const char* t : types) {{
        if (!g_seen.count(t)) std::printf("  gameplay library node not covered: %s\\n", t);
        CHECK(g_seen.count(t) == 1);
    }}
    std::printf("  gameplay library nodes tested: %d\\n", (int)g_seen.size());
    TEST_MAIN_RESULT();
}}
'''
    (ROOT / "tests/test_vs_gameplay_library.cpp").write_text(test)

if __name__ == "__main__":
    main()
