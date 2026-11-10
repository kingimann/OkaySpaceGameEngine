"""Gameplay library, batch 3: more movement, physics, health, items, progression,
text, audio, camera, perception, input, saving and time. Called by gen_gameplay_library.py."""


def define(add, N, B, T, O, R):
    goal = 'GameObject* goal = s.CreateGameObject("Goal");'
    # -----------------------------------------------------------------------
    # Movement
    # -----------------------------------------------------------------------
    M = ("Movement", "")
    add("GlMoveToPosition", "Move to Position", *M, "Basic", "Moves this object toward a point in the world, Speed units each second, and stops when it gets there.",
        [N("Right"), N("Up"), N("Forward"), N("Speed", "3.0f"), O()], [],
        "if (o && o->transform) GpStep(o->transform, Vec3(n0, n1, n2), n3, ctx.deltaTime);",
        reg="CAT_XFORM", exec_=True, defaults=["10", "0", "0", "2", None], update=True, dt=1.0,
        check="CHECK_NEAR(me->transform->Position().x, 2.0f, 0.01f);", demo="movement")
    add("GlLookAtPosition", "Look at Position", "Movement", "Turn", "Basic", "Turns this object to face a point in the world.",
        [N("Right"), N("Up"), N("Forward"), O()], [],
        "if (o && o->transform) o->transform->LookAt(Vec3(n0, n1, n2));",
        reg="CAT_XFORM", exec_=True, defaults=["10", "0", "0", None], check="CHECK(me->transform->Forward().x > 0.95f);")
    add("GlGetDistanceToPosition", "Get Distance to Position", *M, "Basic", "How far this object is from a point in the world.",
        [N("Right"), N("Up"), N("Forward"), O()], [N("Distance")],
        "out = VsValue((o && o->transform) ? (o->transform->Position() - Vec3(n0, n1, n2)).Magnitude() : 0.0f);",
        defaults=["3", "4", "0", None], expect=5.0)
    add("GlIsNearPosition", "Is Near Position", *M, "Basic", "True when this object is within Range of a point in the world.",
        [N("Right"), N("Up"), N("Forward"), N("Range", "1.0f"), O()], [B("Result")],
        "out = VsValue(o && o->transform && (o->transform->Position() - Vec3(n0, n1, n2)).Magnitude() <= n3);",
        reg="CAT_LOGIC", defaults=["1", "0", "0", "2", None], expect=True, etype="b")
    add("GlHop", "Hop", *M, "Intermediate", "Bounces this object up and down like a hopping ball. Speed is radians a second, Height the top of each hop.",
        [N("Height", "1.0f"), N("Speed", "3.0f"), O()], [],
        "if (o && o->transform) { Vec3 p = o->transform->Position(); if (!seen) { seen = true; mem = p.y; } mem2 += ctx.deltaTime * n1;"
        " p.y = mem + std::fabs(std::sin(mem2)) * n0; o->transform->SetPosition(p); }",
        reg="CAT_XFORM", exec_=True, defaults=["1", "1.5707963", None], update=True, dt=1.0, setup="me->transform->SetPosition(Vec3(0, 0.5f, 0));",
        check="CHECK_NEAR(me->transform->Position().y, 1.5f, 0.01f);")
    add("GlMoveBackAndForth", "Move Back and Forth", *M, "Intermediate", "Swings this object left and right of where it started. Speed is radians a second, Distance how far each way.",
        [N("Distance", "2.0f"), N("Speed", "2.0f"), O()], [],
        "if (o && o->transform) { Vec3 p = o->transform->Position(); if (!seen) { seen = true; mem = p.x; } mem2 += ctx.deltaTime * n1;"
        " p.x = mem + std::sin(mem2) * n0; o->transform->SetPosition(p); }",
        reg="CAT_XFORM", exec_=True, defaults=["3", "1.5707963", None], update=True, dt=1.0,
        check="CHECK_NEAR(me->transform->Position().x, 3.0f, 0.01f);", demo="movement")
    add("GlShakePosition", "Shake Position", *M, "Advanced", "Jitters this object around where it was, up to Strength units each way. Stops when the node stops running.",
        [N("Strength", "0.1f"), O()], [],
        "if (o && o->transform) { if (!seen) { seen = true; mem = o->transform->Position().x; mem2 = o->transform->Position().z; }"
        " Vec3 p = o->transform->Position(); p.x = mem + (GlRandom01() * 2.0f - 1.0f) * n0; p.z = mem2 + (GlRandom01() * 2.0f - 1.0f) * n0; o->transform->SetPosition(p); }",
        reg="CAT_XFORM", exec_=True, defaults=["0.5", None], update=True, steps=3, dt=0.1,
        check="CHECK(std::fabs(me->transform->Position().x) <= 0.5f + 1e-4f); CHECK(std::fabs(me->transform->Position().x) + std::fabs(me->transform->Position().z) > 0.0f);")
    add("GlKeepAboveHeight", "Keep Above Height", *M, "Intermediate", "Lifts this object up to a height whenever it drops below it.",
        [N("Lowest")], [], "if (ctx.transform) { Vec3 p = ctx.transform->Position(); if (p.y < n0) { p.y = n0; ctx.transform->SetPosition(p); } }",
        reg="CAT_XFORM", exec_=True, defaults=["0"], setup="me->transform->SetPosition(Vec3(1, -5, 2));",
        check="CHECK_NEAR(me->transform->Position().y, 0.0f, 0.001f); CHECK_NEAR(me->transform->Position().x, 1.0f, 0.001f);")
    add("GlLimitHeight", "Limit Height", *M, "Intermediate", "Keeps this object between a lowest and a highest height.",
        [N("Lowest"), N("Highest", "10.0f"), O()], [],
        "if (o && o->transform) { Vec3 p = o->transform->Position(); p.y = std::min(std::max(p.y, std::min(n0, n1)), std::max(n0, n1)); o->transform->SetPosition(p); }",
        reg="CAT_XFORM", exec_=True, defaults=["0", "4", None], setup="me->transform->SetPosition(Vec3(0, 9, 0));",
        check="CHECK_NEAR(me->transform->Position().y, 4.0f, 0.001f);")
    add("GlResetRotation", "Reset Rotation", "Movement", "Turn", "Basic", "Turns this object back to facing straight ahead, with no tilt.",
        [O()], [], "if (o && o->transform) o->transform->SetEulerAngles(Vec3(0.0f, 0.0f, 0.0f));",
        reg="CAT_XFORM", exec_=True, setup="me->transform->SetEulerAngles(Vec3(20, 70, 10));",
        check="CHECK_NEAR(me->transform->EulerAngles().y, 0.0f, 0.01f); CHECK_NEAR(me->transform->EulerAngles().x, 0.0f, 0.01f);")
    add("GlSetSizeEvenly", "Set Size Evenly", "Movement", "Size", "Basic", "Makes this object the same size in every direction.",
        [N("Size", "1.0f"), O()], [], "if (o && o->transform) o->transform->localScale = Vec3(n0, n0, n0);",
        reg="CAT_XFORM", exec_=True, defaults=["2.5", None], check="CHECK_NEAR(me->transform->localScale.y, 2.5f, 0.001f);")
    add("GlFlipSide", "Flip Side", "Movement", "Size", "Intermediate", "Mirrors this object left to right, like a character turning around in a side view.",
        [O()], [], "if (o && o->transform) { Vec3 s2 = o->transform->localScale; s2.x = -s2.x; o->transform->localScale = s2; }",
        reg="CAT_XFORM", exec_=True, check="CHECK_NEAR(me->transform->localScale.x, -1.0f, 0.001f);")
    add("GlCopyPositionFrom", "Copy Position From", *M, "Basic", "Moves this object to where another object is.",
        [R("Object"), O()], [], "if (o && o2 && o->transform && o2->transform) o2->transform->SetPosition(o->transform->Position());",
        reg="CAT_XFORM", exec_=True, defaults=["Goal", None], setup=goal + " goal->transform->SetPosition(Vec3(1, 2, 3));",
        check="CHECK_NEAR(me->transform->Position().y, 2.0f, 0.001f); CHECK_NEAR(me->transform->Position().z, 3.0f, 0.001f);")
    add("GlStayAtDistanceFrom", "Stay at Distance From", *M, "Advanced", "Walks toward or away from Object until it is Distance away, Speed units each second.",
        [R("Object"), N("Distance", "3.0f"), N("Speed", "2.0f"), O()], [],
        "if (o && o2 && o->transform && o2->transform) { Vec3 d = o2->transform->Position() - o->transform->Position(); float len = d.Magnitude();"
        " if (len > 1e-4f) { Vec3 ideal = o->transform->Position() + d * (1.0f / len) * std::max(0.0f, n0); GpStep(o2->transform, ideal, n1, ctx.deltaTime); } }",
        reg="CAT_XFORM", exec_=True, defaults=["Goal", "3", "4", None], update=True, dt=1.0,
        setup=goal + " goal->transform->SetPosition(Vec3(0, 0, 10));",
        check="CHECK_NEAR(me->transform->Position().z, 4.0f, 0.05f);")
    add("GlFaceObject", "Face Object", "Movement", "Turn", "Basic", "Turns this object flat to look at another object.",
        [R("Object"), O()], [], "if (o && o2 && o->transform && o2->transform) o2->transform->LookAtYaw(o->transform->Position());",
        reg="CAT_XFORM", exec_=True, defaults=["Goal", None], setup=goal + " goal->transform->SetPosition(Vec3(6, 0, 0));",
        check="CHECK(me->transform->Forward().x > 0.95f);", demo="ai")
    add("GlTurnTowardObject", "Turn Toward Object", "Movement", "Turn", "Intermediate", "Turns this object a little each frame toward another object, at most Degrees Per Second.",
        [R("Object"), N("Degrees Per Second", "90.0f"), O()], [],
        "if (o && o2 && o->transform && o2->transform) { Vec3 d = o->transform->Position() - o2->transform->Position(); d.y = 0.0f;"
        " Vec3 f = o2->transform->Forward(); f.y = 0.0f; float dl = d.Magnitude(), fl = f.Magnitude();"
        " if (dl > 1e-4f && fl > 1e-4f) { d = d * (1.0f / dl); f = f * (1.0f / fl);"
        " float ang = std::atan2(f.x * d.z - f.z * d.x, f.x * d.x + f.z * d.z) * 57.2957795f;"
        " float lim = std::max(0.0f, n0) * ctx.deltaTime; float turn = std::min(std::max(ang, -lim), lim);"
        " o2->transform->Rotate(Vec3(0.0f, -turn, 0.0f)); } }",
        reg="CAT_XFORM", exec_=True, defaults=["Goal", "90", None], update=True, dt=0.5,
        setup=goal + " goal->transform->SetPosition(me->transform->Right() * 6.0f);",
        check="{ Vec3 dd = goal->transform->Position() - me->transform->Position(); dd = dd * (1.0f / dd.Magnitude()); Vec3 ff = me->transform->Forward();"
              " float c = ff.x * dd.x + ff.y * dd.y + ff.z * dd.z; CHECK_NEAR(std::acos(std::min(1.0f, std::max(-1.0f, c))) * 57.2957795f, 45.0f, 1.0f); }")

    # -----------------------------------------------------------------------
    # Physics
    # -----------------------------------------------------------------------
    P = ("Physics", "")
    rb = "me->AddComponent<Rigidbody3D>();"
    add("GlJump", "Jump", *P, "Basic", "Throws a Rigidbody upward at Strength units a second.",
        [N("Strength", "5.0f"), O()], [], "if (Rigidbody3D* rb = GlBody(this, o, g, ctx)) { rb->velocity.y = n0; rb->WakeUp(); }",
        reg="CAT_XFORM", exec_=True, defaults=["6", None], setup=rb, check="CHECK_NEAR(me->GetComponent<Rigidbody3D>()->velocity.y, 6.0f, 0.001f);", demo="physics")
    add("GlIsFalling", "Is Falling", *P, "Basic", "True while a Rigidbody is moving downward.",
        [O()], [B("Result")], "Rigidbody3D* rb = GlBody(this, o, g, ctx); out = VsValue(rb && rb->velocity.y < -0.1f);",
        reg="CAT_LOGIC", setup=rb + " me->GetComponent<Rigidbody3D>()->velocity = Vec3(0, -3, 0);", expect=True, etype="b")
    add("GlIsRising", "Is Rising", *P, "Basic", "True while a Rigidbody is moving upward.",
        [O()], [B("Result")], "Rigidbody3D* rb = GlBody(this, o, g, ctx); out = VsValue(rb && rb->velocity.y > 0.1f);",
        reg="CAT_LOGIC", setup=rb + " me->GetComponent<Rigidbody3D>()->velocity = Vec3(0, 3, 0);", expect=True, etype="b")
    add("GlGetVerticalSpeed", "Get Vertical Speed", *P, "Intermediate", "How fast a Rigidbody moves up (positive) or down (negative).",
        [O()], [N("Speed")], "Rigidbody3D* rb = GlBody(this, o, g, ctx); out = VsValue(rb ? rb->velocity.y : 0.0f);",
        setup=rb + " me->GetComponent<Rigidbody3D>()->velocity = Vec3(1, -2.5f, 1);", expect=-2.5)
    add("GlGetFlatSpeed", "Get Flat Speed", *P, "Intermediate", "How fast a Rigidbody moves along the ground, ignoring up and down.",
        [O()], [N("Speed")], "Rigidbody3D* rb = GlBody(this, o, g, ctx); out = VsValue(rb ? std::sqrt(rb->velocity.x * rb->velocity.x + rb->velocity.z * rb->velocity.z) : 0.0f);",
        setup=rb + " me->GetComponent<Rigidbody3D>()->velocity = Vec3(3, 9, 4);", expect=5.0)
    add("GlLimitSpeed", "Limit Speed", *P, "Intermediate", "Slows a Rigidbody down when it is faster than Top Speed. Slower ones are left alone.",
        [N("Top Speed", "5.0f"), O()], [],
        "if (Rigidbody3D* rb = GlBody(this, o, g, ctx)) { float sp = rb->velocity.Magnitude(); float cap = std::max(0.0f, n0); if (sp > cap && sp > 1e-5f) rb->velocity = rb->velocity * (cap / sp); }",
        reg="CAT_XFORM", exec_=True, defaults=["5", None], setup=rb + " me->GetComponent<Rigidbody3D>()->velocity = Vec3(6, 0, 8);",
        check="CHECK_NEAR(me->GetComponent<Rigidbody3D>()->velocity.Magnitude(), 5.0f, 0.01f);")
    add("GlSetFlatSpeed", "Set Flat Velocity", *P, "Intermediate", "Sets how a Rigidbody moves along the ground and leaves its up and down speed alone.",
        [N("Right"), N("Forward"), O()], [],
        "if (Rigidbody3D* rb = GlBody(this, o, g, ctx)) { rb->velocity.x = n0; rb->velocity.z = n1; rb->WakeUp(); }",
        reg="CAT_XFORM", exec_=True, defaults=["2", "3", None], setup=rb + " me->GetComponent<Rigidbody3D>()->velocity = Vec3(0, 7, 0);",
        check="CHECK_NEAR(me->GetComponent<Rigidbody3D>()->velocity.y, 7.0f, 0.001f); CHECK_NEAR(me->GetComponent<Rigidbody3D>()->velocity.z, 3.0f, 0.001f);")

    # -----------------------------------------------------------------------
    # Health
    # -----------------------------------------------------------------------
    H = ("Objects", "Survival")
    hp = "me->AddComponent<HealthStat>();"
    add("GlHealPortion", "Heal Portion", *H, "Basic", "Heals a share of the maximum: 0.25 restores a quarter.",
        [N("Portion", "0.25f"), O()], [], "if (HealthStat* h = GpHealth(this, o, g, ctx)) h->Heal(h->maxHealth * std::max(0.0f, n0));",
        reg="CAT_ACTION", exec_=True, defaults=["0.5", None], setup=hp, post="me->GetComponent<HealthStat>()->health = 10;", update=True,
        check="CHECK_NEAR(me->GetComponent<HealthStat>()->health, 60.0f, 0.01f);")
    add("GlDamagePortion", "Damage Portion", *H, "Basic", "Takes a share of the maximum: 0.3 removes almost a third.",
        [N("Portion", "0.25f"), O()], [], "if (HealthStat* h = GpHealth(this, o, g, ctx)) h->Damage(h->maxHealth * std::max(0.0f, n0));",
        reg="CAT_ACTION", exec_=True, defaults=["0.3", None], setup=hp, check="CHECK_NEAR(me->GetComponent<HealthStat>()->health, 70.0f, 0.01f);")
    add("GlSetHealthPortion", "Set Health Portion", *H, "Intermediate", "Sets health to a share of the maximum. 1 is full.",
        [N("Portion", "1.0f"), O()], [],
        "if (HealthStat* h = GpHealth(this, o, g, ctx)) h->health = std::min(std::max(n0, 0.0f), 1.0f) * h->maxHealth;",
        reg="CAT_ACTION", exec_=True, defaults=["0.4", None], setup=hp, check="CHECK_NEAR(me->GetComponent<HealthStat>()->health, 40.0f, 0.01f);")
    add("GlReviveWithHealth", "Revive With Health", *H, "Intermediate", "Brings a dead object back with this share of its maximum health. Run it from another object: a dead one is switched off.",
        [N("Portion", "0.5f"), O()], [],
        "if (HealthStat* h = GpHealth(this, o, g, ctx)) { h->Revive(); h->health = std::min(std::max(n0, 0.01f), 1.0f) * h->maxHealth; }",
        reg="CAT_ACTION", exec_=True, defaults=["0.5", "Goal"],
        setup=goal + " goal->AddComponent<HealthStat>()->playDeathAnim = false;", post="goal->GetComponent<HealthStat>()->Damage(1000.0f); CHECK(goal->GetComponent<HealthStat>()->IsDead());", update=True,
        check="CHECK(!goal->GetComponent<HealthStat>()->IsDead()); CHECK_NEAR(goal->GetComponent<HealthStat>()->health, 50.0f, 0.01f);")
    add("GlCriticalDamage", "Get Critical Damage", *H, "Intermediate", "The damage of a hit: multiplied when Is Critical is true, unchanged otherwise.",
        [N("Damage"), B("Is Critical"), N("Multiplier", "2.0f")], [N("Result")], "out = VsValue(b0 ? n0 * std::max(1.0f, n1) : n0);",
        reg="CAT_MATH", defaults=["10", "true", "2.5"], expect=25.0)
    add("GlDamageFalloff", "Get Damage by Distance", *H, "Advanced", "Full damage up close, fading in a straight line to none: Full Range keeps all of it, Zero Range none.",
        [N("Damage"), N("Distance"), N("Full Range", "5.0f"), N("Zero Range", "15.0f")], [N("Result")],
        "float span = n3 - n2; float k = span <= 1e-4f ? (n1 <= n2 ? 1.0f : 0.0f) : 1.0f - (n1 - n2) / span; out = VsValue(n0 * std::min(std::max(k, 0.0f), 1.0f));",
        reg="CAT_MATH", defaults=["100", "15", "10", "20"], expect=50.0)

    # -----------------------------------------------------------------------
    # Items
    # -----------------------------------------------------------------------
    I = ("Objects", "Items")
    inv = "me->AddComponent<Inventory>(); "
    add("GlIsInventoryEmpty", "Is Inventory Empty", *I, "Basic", "True when nothing is carried.",
        [O()], [B("Result")], "Inventory* inv = GlInventory(this, o, g, ctx); out = VsValue(inv && inv->SlotsUsed() == 0);",
        reg="CAT_LOGIC", setup=inv, expect=True, etype="b")
    add("GlStacksNeeded", "Get Stacks Needed", *I, "Intermediate", "How many stacks it takes to hold Total of an item when a stack holds Stack Size.",
        [N("Total"), N("Stack Size", "10.0f")], [N("Stacks")],
        "out = VsValue(n1 < 1.0f ? 0.0f : std::ceil(std::max(0.0f, n0) / std::floor(n1)));", reg="CAT_MATH", defaults=["25", "10"], expect=3.0)
    add("GlAddItemIfRoom", "Add Item When Room Allows", *I, "Intermediate", "Adds Count of an item only when all of it fits. Runs Added, or Full when it would not fit.",
        [T("Item"), N("Count", "1.0f"), O()], [],
        "if (Inventory* inv = GlInventory(this, o, g, ctx)) { int want = std::max(1, (int)std::lround(n0)); if (inv->CanAdd(t0, want) >= want && inv->Add(t0, want)) flow = 0; else flow = 1; } else flow = 1;",
        reg="CAT_ACTION", exec_=True, exec_outs=["Added", "Full"], defaults=['"coin"', "2", None],
        setup=inv, check='CHECK(me->GetComponent<Inventory>()->Count("coin") == 2);')
    add("GlTakeOneItem", "Take One Item", *I, "Basic", "Removes one of this item. Runs Taken, or None Left when there was none to take.",
        [T("Item"), O()], [],
        "if (Inventory* inv = GlInventory(this, o, g, ctx)) { flow = inv->Has(t0, 1) && inv->Remove(t0, 1) ? 0 : 1; } else flow = 1;",
        reg="CAT_ACTION", exec_=True, exec_outs=["Taken", "None Left"], defaults=['"coin"', None],
        setup=inv + 'me->GetComponent<Inventory>()->Add("coin", 3);', check='CHECK(me->GetComponent<Inventory>()->Count("coin") == 2);')

    # -----------------------------------------------------------------------
    # Progression
    # -----------------------------------------------------------------------
    PR = ("Math", "Progression")
    add("GlLevelFromExperience", "Get Level From Experience", *PR, "Intermediate", "The level reached with this much experience. Level 2 needs Per Level, level 3 needs four times that, and so on.",
        [N("Experience"), N("Per Level", "100.0f")], [N("Level")],
        "out = VsValue(n1 <= 0.0f ? 1.0f : std::floor(std::sqrt(std::max(0.0f, n0) / n1)) + 1.0f);", reg="CAT_MATH", defaults=["400", "100"], expect=3.0)
    add("GlExperienceForLevel", "Get Experience for Level", *PR, "Intermediate", "How much experience a level needs in all. Level 1 needs none.",
        [N("Level"), N("Per Level", "100.0f")], [N("Experience")],
        "float l = std::max(1.0f, std::floor(n0)) - 1.0f; out = VsValue(l * l * std::max(0.0f, n1));", reg="CAT_MATH", defaults=["3", "100"], expect=400.0)
    add("GlExperienceToNextLevel", "Get Experience to Next Level", *PR, "Advanced", "How much more experience is needed to reach the next level.",
        [N("Experience"), N("Per Level", "100.0f")], [N("Experience Needed")],
        "float b = std::max(0.0f, n1); float lv = b <= 0.0f ? 1.0f : std::floor(std::sqrt(std::max(0.0f, n0) / b)) + 1.0f; out = VsValue(lv * lv * b - std::max(0.0f, n0));",
        reg="CAT_MATH", defaults=["250", "100"], expect=150.0)
    add("GlGrowByLevel", "Get Stat by Level", *PR, "Advanced", "A stat that grows by a share each level: Base at level 1, then times 1 plus Growth for every level after.",
        [N("Base"), N("Growth", "0.1f"), N("Level", "1.0f")], [N("Result")],
        "out = VsValue(n0 * std::pow(1.0f + std::max(-0.99f, n1), std::max(1.0f, n2) - 1.0f));", reg="CAT_MATH", defaults=["10", "0.1", "3"], expect=12.1)
    add("GlPriceWithDiscount", "Get Price With Discount", *PR, "Basic", "A price after a discount: 0.25 takes a quarter off.",
        [N("Price"), N("Discount")], [N("Result")], "out = VsValue(n0 * (1.0f - std::min(std::max(n1, 0.0f), 1.0f)));", reg="CAT_MATH", defaults=["80", "0.25"], expect=60.0)
    add("GlCooldownPortion", "Get Cooldown Portion", *PR, "Basic", "How far through a cooldown you are: 0 just started, 1 ready.",
        [N("Seconds Left"), N("Total Seconds")], [N("Portion")],
        "out = VsValue(n1 <= 1e-4f ? 1.0f : std::min(std::max(1.0f - n0 / n1, 0.0f), 1.0f));", reg="CAT_MATH", defaults=["3", "12"], expect=0.75)
    add("GlRollChance", "Roll Chance", *PR, "Intermediate", "True with this chance each time it is read: 0 never, 1 always, 0.25 one time in four.",
        [N("Chance", "0.5f")], [B("Result")], "out = VsValue(n0 >= 1.0f || (n0 > 0.0f && GlRandom01() < n0));", reg="CAT_LOGIC", defaults=["1"], expect=True, etype="b")

    # -----------------------------------------------------------------------
    # UI text
    # -----------------------------------------------------------------------
    txt = "me->AddComponent<TextRenderer>();"
    add("GlTypewriterText", "Type Out Text", "UI", "", "Intermediate", "Shows Text a few letters at a time, Letters Per Second, like a typewriter. Put it under On Every Frame.",
        [T("Text"), N("Letters Per Second", "20.0f"), O()], [],
        "if (TextRenderer* tr = GlText(this, o, g, ctx)) { mem += std::max(0.0f, n0) * ctx.deltaTime; size_t shown = std::min(t0.size(), (size_t)std::max(0.0f, std::floor(mem))); tr->text = t0.substr(0, shown); }",
        reg="CAT_ACTION", exec_=True, defaults=['"Hello"', "4", None], update=True, dt=0.5, setup=txt,
        check='CHECK(me->GetComponent<TextRenderer>()->text == "He");')
    add("GlShowHealthInText", "Show Health in Text", "UI", "", "Intermediate", "Writes a label and another object's health into a Text object, like HP: 80.",
        [T("Label"), R("Object"), O()], [],
        "if (TextRenderer* tr = GlText(this, o2, g, ctx)) { HealthStat* h = GpHealth(this, o, g, ctx); if (h) tr->text = t0 + GlNumberText(h->health, 0); }",
        reg="CAT_ACTION", exec_=True, defaults=['"HP: "', "Goal", None],
        setup=txt + " " + goal + " goal->AddComponent<HealthStat>()->maxHealth = 80;", check='CHECK(me->GetComponent<TextRenderer>()->text == "HP: 80");')
    add("GlShowSessionTimeInText", "Show Session Time in Text", "UI", "", "Basic", "Writes a label and how long the game has run, like Time: 02:05, into a Text object.",
        [T("Label"), O()], [],
        "if (TextRenderer* tr = GlText(this, o, g, ctx)) { int total = std::max(0, (int)std::floor(Time::SessionTime())); char b[32]; std::snprintf(b, sizeof b, \"%02d:%02d\", total / 60, total % 60); tr->text = t0 + b; }",
        reg="CAT_ACTION", exec_=True, defaults=['"Time: "', None], setup=txt, check='CHECK(me->GetComponent<TextRenderer>()->text.rfind("Time: ", 0) == 0);')
    add("GlClearText", "Clear Text", "UI", "", "Basic", "Wipes the words from a Text object.",
        [O()], [], "if (TextRenderer* tr = GlText(this, o, g, ctx)) tr->text.clear();",
        reg="CAT_ACTION", exec_=True, setup=txt + ' me->GetComponent<TextRenderer>()->text = "gone";', check="CHECK(me->GetComponent<TextRenderer>()->text.empty());")

    # -----------------------------------------------------------------------
    # Audio
    # -----------------------------------------------------------------------
    au = "me->AddComponent<AudioSource>();"
    add("GlFadeVolumeTo", "Fade Volume to", "Audio", "", "Intermediate", "Moves a sound's volume toward Volume, Per Second each second. Put it under On Every Frame.",
        [N("Volume"), N("Per Second", "1.0f"), O()], [],
        "if (AudioSource* a = GlAudio(this, o, g, ctx)) { float step = std::max(0.0f, n1) * ctx.deltaTime; a->volume = a->volume < n0 ? std::min(a->volume + step, n0) : std::max(a->volume - step, n0); }",
        reg="CAT_ACTION", exec_=True, defaults=["0", "1", None], update=True, dt=0.5, setup=au,
        check="CHECK_NEAR(me->GetComponent<AudioSource>()->volume, 0.5f, 0.001f);")
    add("GlRandomizePitch", "Randomize Pitch", "Audio", "", "Intermediate", "Gives a sound a random pitch between Lowest and Highest, so repeated sounds do not feel the same.",
        [N("Lowest", "0.9f"), N("Highest", "1.1f"), O()], [],
        "if (AudioSource* a = GlAudio(this, o, g, ctx)) { float lo = std::min(n0, n1), hi = std::max(n0, n1); a->pitch = std::min(std::max(lo + (hi - lo) * GlRandom01(), 0.1f), 3.0f); }",
        reg="CAT_ACTION", exec_=True, defaults=["1.3", "1.3", None], setup=au, check="CHECK_NEAR(me->GetComponent<AudioSource>()->pitch, 1.3f, 0.001f);")
    add("GlMuteSound", "Mute Sound", "Audio", "", "Basic", "On silences a sound. Off brings back the volume it had.",
        [B("Muted", "true"), O()], [],
        "if (AudioSource* a = GlAudio(this, o, g, ctx)) { if (b0) { if (!flag) { flag = true; mem = a->volume; } a->volume = 0.0f; } else if (flag) { flag = false; a->volume = mem; } }",
        reg="CAT_ACTION", exec_=True, defaults=["true", None], setup=au, check="CHECK_NEAR(me->GetComponent<AudioSource>()->volume, 0.0f, 0.001f);")

    # -----------------------------------------------------------------------
    # Camera
    # -----------------------------------------------------------------------
    cam = "me->AddComponent<Camera>();"
    add("GlCameraFollowObject", "Follow Object With Camera", "Camera", "", "Intermediate", "Slides this object after another one, keeping the gap it had at first. Bigger Smoothness catches up faster.",
        [R("Object"), N("Smoothness", "5.0f"), O()], [],
        "if (o && o2 && o->transform && o2->transform) { if (!seen) { seen = true; Vec3 off = o2->transform->Position() - o->transform->Position(); mem = off.x; mem2 = off.y; mem3 = off.z; }"
        " Vec3 want = o->transform->Position() + Vec3(mem, mem2, mem3); float k = 1.0f - std::exp(-std::max(0.0f, n0) * ctx.deltaTime);"
        " o2->transform->SetPosition(o2->transform->Position() + (want - o2->transform->Position()) * k); }",
        reg="CAT_ACTION", exec_=True, defaults=["Goal", "100", None], update=True, dt=1.0,
        setup=goal + " me->transform->SetPosition(Vec3(0, 5, -8));",
        check="goal->transform->SetPosition(Vec3(10, 0, 0)); s.Update(1.0f); CHECK_NEAR(me->transform->Position().x, 10.0f, 0.05f); CHECK_NEAR(me->transform->Position().y, 5.0f, 0.05f);",
        demo="camera")
    add("GlCameraLookAtObject", "Look at Object With Camera", "Camera", "", "Basic", "Points this object, usually the camera, straight at another object.",
        [R("Object"), O()], [], "if (o && o2 && o->transform && o2->transform) o2->transform->LookAt(o->transform->Position());",
        reg="CAT_ACTION", exec_=True, defaults=["Goal", None], setup=goal + " goal->transform->SetPosition(Vec3(8, 0, 0));",
        check="CHECK(me->transform->Forward().x > 0.95f);", demo="camera")
    add("GlZoomSmoothlyTo", "Zoom Smoothly to", "Camera", "", "Intermediate", "Moves the camera's zoom toward Zoom, Per Second each second. Put it under On Every Frame.",
        [N("Zoom", "1.0f"), N("Per Second", "1.0f"), O()], [],
        "if (Camera* c = GlCamera(this, o, g, ctx)) { float step = std::max(0.0f, n1) * ctx.deltaTime; c->zoom = c->zoom < n0 ? std::min(c->zoom + step, n0) : std::max(c->zoom - step, std::max(0.05f, n0)); }",
        reg="CAT_ACTION", exec_=True, defaults=["2", "1", None], update=True, dt=0.5, setup=cam, check="CHECK_NEAR(me->GetComponent<Camera>()->zoom, 1.5f, 0.001f);")

    # -----------------------------------------------------------------------
    # Perception, input, saving, time
    # -----------------------------------------------------------------------
    Q = ("Physics", "Queries")
    add("GlCountTaggedInRange", "Count Tagged Objects in Range", *Q, "Intermediate", "How many active objects with this tag are within Range of this object.",
        [T("Tag"), N("Range", "5.0f"), O()], [N("Count")],
        "float c = 0; if (o && o->transform && o->scene()) for (const auto& up : o->scene()->Objects()) { if (!up || !up->active || up.get() == o || up->tag != t0) continue;"
        " if ((up->transform->Position() - o->transform->Position()).Magnitude() <= n0) c += 1.0f; } out = VsValue(c);",
        defaults=['"enemy"', "5", None],
        setup='for (int i = 0; i < 3; ++i) { GameObject* e = s.CreateGameObject("E" + std::to_string(i)); e->tag = "enemy"; e->transform->SetPosition(Vec3(2.0f * i + 1.5f, 0, 0)); }',
        expect=2.0)
    add("GlIsJumpHeld", "Is Jump Held", "Input", "Keys", "Basic", "True while the Space bar is held down.",
        [], [B("Result")], "out = VsValue(Input::GetKey(' '));", reg="CAT_LOGIC", setup="Input::FeedKeys({' '});", update=True, expect=True, etype="b", demo="input")
    V = ("Variables", "Saved")
    add("GlSaveHealth", "Save Health", *V, "Intermediate", "Remembers how much health an object has, under a name.",
        [T("Name"), O()], [], "if (HealthStat* h = GpHealth(this, o, g, ctx)) Prefs::SetFloat(\"vs.health.\" + t0, h->health);",
        reg="CAT_ACTION", exec_=True, defaults=['"hero"', None], setup=hp if False else "me->AddComponent<HealthStat>()->maxHealth = 70;",
        check='CHECK_NEAR(Prefs::GetFloat("vs.health.hero", -1.0f), 70.0f, 0.01f); Prefs::Delete("vs.health.hero");')
    add("GlLoadHealth", "Load Health", *V, "Intermediate", "Gives an object the health saved under a name. Runs Loaded, or Nothing Saved when there is none.",
        [T("Name"), O()], [],
        "HealthStat* h = GpHealth(this, o, g, ctx); if (h && Prefs::Has(\"vs.health.\" + t0)) { h->health = std::min(Prefs::GetFloat(\"vs.health.\" + t0, h->health), h->maxHealth); flow = 0; } else flow = 1;",
        reg="CAT_ACTION", exec_=True, exec_outs=["Loaded", "Nothing Saved"], defaults=['"back"', None],
        setup='me->AddComponent<HealthStat>(); Prefs::SetFloat("vs.health.back", 35.0f);',
        check='CHECK_NEAR(me->GetComponent<HealthStat>()->health, 35.0f, 0.01f); Prefs::Delete("vs.health.back");')
    TM = ("Time", "")
    add("GlHasTimePassed", "Has Time Passed", *TM, "Basic", "True once the game has been running for at least this many seconds.",
        [N("Seconds")], [B("Result")], "out = VsValue(Time::SessionTime() >= n0);", reg="CAT_LOGIC", defaults=["0"], expect=True, etype="b")
    add("GlGetGameSpeed", "Get Game Speed", *TM, "Basic", "How fast the game runs: 1 is normal, 0.5 half speed, 0 stopped.",
        [], [N("Speed")], "out = VsValue(Time::TimeScale());", setup="Time::SetTimeScale(1.0f);", expect=1.0)
    G = ("Objects", "Groups")
    add("GlRenameObject", "Rename Object", *G, "Intermediate", "Gives this object a new name.",
        [T("Name"), O()], [], "if (o && !t0.empty()) o->name = t0;", reg="CAT_ACTION", exec_=True, defaults=['"Hero"', None],
        check='CHECK(me->name == "Hero");')
