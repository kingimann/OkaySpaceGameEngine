"""Gameplay library, batch 4: settings and readings on components, more movement,
inventory, camera, input and quest nodes. Called by gen_gameplay_library.py."""


def define(add, N, B, T, O, R):
    def setter(typ, title, topic, sub, level, help_, ins, ctype, fn, stmt, setup, defaults, check, **kw):
        add(typ, title, topic, sub, level, help_, ins + [O()], [],
            f"if ({ctype}* c = {fn}(this, o, g, ctx)) {{ {stmt} }}",
            reg="CAT_ACTION", exec_=True, defaults=defaults + [None], setup=setup, check=check, **kw)

    def getter(typ, title, topic, sub, level, help_, out, ctype, fn, expr, fallback, setup, expect, etype="f", question=False, **kw):
        add(typ, title, topic, sub, level, help_, [O()], [(out, "B" if question else "N", None)],
            f"{ctype}* c = {fn}(this, o, g, ctx); out = VsValue(c ? ({expr}) : {fallback});",
            reg="CAT_LOGIC" if question else "CAT_DATA", setup=setup, expect=expect, etype=etype, **kw)

    goal = 'GameObject* goal = s.CreateGameObject("Goal");'
    hp = "me->AddComponent<HealthStat>();"
    HC = "HealthStat"
    H = ("Objects", "Survival")

    # -----------------------------------------------------------------------
    # Health settings and readings
    # -----------------------------------------------------------------------
    setter("GlSetInvincibleTime", "Set Hit Protection Time", *H, "Intermediate", "After each hit, damage is ignored for this many seconds.",
           [N("Seconds", "0.5f")], HC, "GpHealth", "c->invincibleTime = std::max(0.0f, n0);", hp, ["0.75"],
           "CHECK_NEAR(me->GetComponent<HealthStat>()->invincibleTime, 0.75f, 0.001f);")
    setter("GlSetLives", "Set Lives", *H, "Intermediate", "How many times this object may come back after dying. 0 means as many as it likes.",
           [N("Lives", "3.0f")], HC, "GpHealth", "c->lives = std::max(0, (int)std::lround(n0));", hp, ["3"],
           "CHECK(me->GetComponent<HealthStat>()->lives == 3);")
    setter("GlSetRespawnTime", "Set Respawn Time", *H, "Intermediate", "On makes this object come back after Seconds when it dies.",
           [B("Respawn", "true"), N("Seconds", "3.0f")], HC, "GpHealth", "c->respawn = b0; c->respawnDelay = std::max(0.0f, n0);", hp, ["true", "4"],
           "CHECK(me->GetComponent<HealthStat>()->respawn); CHECK_NEAR(me->GetComponent<HealthStat>()->respawnDelay, 4.0f, 0.001f);")
    setter("GlSetResistance", "Set Damage Resistance", *H, "Intermediate", "The share of each hit that is ignored after armor: 0.5 halves it.",
           [N("Share", "0.25f")], HC, "GpHealth", "c->resistance = std::min(std::max(n0, 0.0f), 1.0f);", hp, ["0.5"],
           "me->GetComponent<HealthStat>()->Damage(40); CHECK_NEAR(me->GetComponent<HealthStat>()->health, 80.0f, 0.01f);")
    setter("GlSetSmallestHit", "Set Smallest Hit", *H, "Advanced", "However strong the armor, a hit always takes at least this much.",
           [N("Amount", "1.0f")], HC, "GpHealth", "c->minDamage = std::max(0.0f, n0);", hp, ["2"],
           "me->GetComponent<HealthStat>()->armor = 50; me->GetComponent<HealthStat>()->Damage(10); CHECK_NEAR(me->GetComponent<HealthStat>()->health, 98.0f, 0.01f);")
    setter("GlSetOverhealLimit", "Set Overheal Limit", *H, "Advanced", "How far above the maximum healing may push health. It drains back at Decay Per Second.",
           [N("Extra", "20.0f"), N("Decay Per Second", "2.0f")], HC, "GpHealth", "c->overhealMax = std::max(0.0f, n0); c->overhealDecay = std::max(0.0f, n1);", hp, ["25", "3"],
           "CHECK_NEAR(me->GetComponent<HealthStat>()->overhealMax, 25.0f, 0.001f); CHECK_NEAR(me->GetComponent<HealthStat>()->overhealDecay, 3.0f, 0.001f);")
    setter("GlSetRemoveOnDeath", "Set Remove on Death", *H, "Intermediate", "On removes this object from the scene when it dies instead of only switching it off.",
           [B("Remove", "true")], HC, "GpHealth", "c->destroyOnDeath = b0;", hp, ["true"],
           "CHECK(me->GetComponent<HealthStat>()->destroyOnDeath);")
    getter("GlGetMissingHealth", "Get Missing Health", *H, "Basic", "How much health is missing from the maximum.", "Missing", HC, "GpHealth",
           "c->maxHealth - c->health", "0.0f", hp, 0.0, demo="health")
    getter("GlIsHealthAbove", "Is Health Above", *H, "Basic", "True when health is above 50 percent of the maximum.", "Result", HC, "GpHealth",
           "c->Fraction() > 0.5f", "false", hp, True, "b", question=True)
    getter("GlGetResistance", "Get Damage Resistance", *H, "Intermediate", "The share of each hit that is ignored after armor.", "Share", HC, "GpHealth",
           "c->resistance", "0.0f", hp + " me->GetComponent<HealthStat>()->resistance = 0.4f;", 0.4)

    # -----------------------------------------------------------------------
    # Inventory settings and readings
    # -----------------------------------------------------------------------
    I = ("Objects", "Items")
    inv = "me->AddComponent<Inventory>(); "
    IC = "Inventory"
    setter("GlSetCarryLimit", "Set Carry Limit", *I, "Intermediate", "The most weight that can be carried. 0 means no limit.",
           [N("Weight", "50.0f")], IC, "GlInventory", "c->weightLimit = std::max(0.0f, n0);", inv, ["30"],
           "CHECK_NEAR(me->GetComponent<Inventory>()->weightLimit, 30.0f, 0.001f);")
    setter("GlSetStackLimit", "Set Stack Limit", *I, "Intermediate", "The most of one item a single slot can hold. 0 means the item's own limit.",
           [N("Count", "0.0f")], IC, "GlInventory", "c->maxStack = std::max(0, (int)std::lround(n0));", inv, ["8"],
           "CHECK(me->GetComponent<Inventory>()->maxStack == 8);")
    getter("GlGetCarryLimit", "Get Carry Limit", *I, "Intermediate", "The most weight that can be carried. 0 means no limit.", "Weight", IC, "GlInventory",
           "c->weightLimit", "0.0f", inv + "me->GetComponent<Inventory>()->weightLimit = 12.0f;", 12.0)
    getter("GlIsOverweight", "Is Overweight", *I, "Intermediate", "True when more is carried than the carry limit allows.", "Result", IC, "GlInventory",
           "c->OverWeight()", "false", inv, False, "b", question=True)
    getter("GlGetSlowdownFromWeight", "Get Slowdown From Weight", *I, "Advanced", "The speed factor weight causes: 1 is none, lower is slower.", "Factor", IC, "GlInventory",
           "c->SpeedFactor()", "1.0f", inv, 1.0)
    add("GlCountAllItems", "Count All Items", *I, "Basic", "How many items are carried in all, adding every stack together.",
        [O()], [N("Count")],
        "Inventory* inv = GlInventory(this, o, g, ctx); float total = 0; if (inv) for (const auto& sl : inv->slots) if (!Inventory::Empty(sl)) total += (float)sl.count; out = VsValue(total);",
        setup=inv + 'me->GetComponent<Inventory>()->Add("coin", 4); me->GetComponent<Inventory>()->Add("key", 2);', expect=6.0, demo="inventory")

    # -----------------------------------------------------------------------
    # Physics settings
    # -----------------------------------------------------------------------
    P = ("Physics", "")
    rb = "me->AddComponent<Rigidbody3D>();"
    RC = "Rigidbody3D"
    add("GlSetBodyType", "Set Body Type", *P, "Intermediate", "Dynamic moves and is pushed, Kinematic moves only when told to, Static never moves.",
        [T("Type"), O()], [],
        "if (Rigidbody3D* rb = GlBody(this, o, g, ctx)) { std::string k; for (char ch : t0) k += (char)std::tolower((unsigned char)ch);"
        " if (k == \"static\") rb->bodyType = Rigidbody3D::BodyType::Static; else if (k == \"kinematic\") rb->bodyType = Rigidbody3D::BodyType::Kinematic; else if (k == \"dynamic\") rb->bodyType = Rigidbody3D::BodyType::Dynamic;"
        " else g.Error(this, ctx, \"the Type must be Dynamic, Kinematic or Static\"); }",
        reg="CAT_ACTION", exec_=True, defaults=['"static"', None], setup=rb,
        check="CHECK(me->GetComponent<Rigidbody3D>()->bodyType == Rigidbody3D::BodyType::Static);")
    setter("GlSetFallSpeedLimit", "Set Fall Speed Limit", *P, "Advanced", "A falling Rigidbody never gets faster than this. 0 means no limit.",
           [N("Speed", "20.0f")], RC, "GlBody", "c->maxFallSpeed = std::max(0.0f, n0);", rb, ["15"],
           "CHECK_NEAR(me->GetComponent<Rigidbody3D>()->maxFallSpeed, 15.0f, 0.001f);")
    setter("GlSetSurfaceFriction", "Set Surface Friction", *P, "Intermediate", "How much a Rigidbody grips what it slides on: 0 is ice, 1 is rubber.",
           [N("Friction", "0.5f")], RC, "GlBody", "c->friction = std::min(std::max(n0, 0.0f), 2.0f);", rb, ["0.9"],
           "CHECK_NEAR(me->GetComponent<Rigidbody3D>()->friction, 0.9f, 0.001f);")
    add("GlFreezeAxis", "Freeze Axis", *P, "Advanced", "Stops a Rigidbody moving along one world axis. Axis is Right, Up or Forward.",
        [T("Axis"), B("Frozen", "true"), O()], [],
        "if (Rigidbody3D* rb = GlBody(this, o, g, ctx)) { std::string k; for (char ch : t0) k += (char)std::tolower((unsigned char)ch);"
        " if (k == \"right\") rb->freezeX = b0; else if (k == \"up\") rb->freezeY = b0; else if (k == \"forward\") rb->freezeZ = b0;"
        " else g.Error(this, ctx, \"the Axis must be Right, Up or Forward\"); }",
        reg="CAT_ACTION", exec_=True, defaults=['"up"', "true", None], setup=rb,
        check="CHECK(me->GetComponent<Rigidbody3D>()->freezeY); CHECK(!me->GetComponent<Rigidbody3D>()->freezeX);")
    add("GlWakeBody", "Wake Body", *P, "Advanced", "Wakes a Rigidbody that fell asleep, so it reacts again.",
        [O()], [], "if (Rigidbody3D* rb = GlBody(this, o, g, ctx)) rb->WakeUp();",
        reg="CAT_ACTION", exec_=True, setup=rb, post="me->GetComponent<Rigidbody3D>()->sleeping = true;", update=True,
        check="CHECK(!me->GetComponent<Rigidbody3D>()->sleeping);")
    add("GlPushAwayFrom", "Push Away From Object", *P, "Intermediate", "Shoves this Rigidbody directly away from another object. Negative Force pulls it closer.",
        [R("Object"), N("Force", "5.0f"), O()], [],
        "if (o && o2 && o->transform && o2->transform) if (Rigidbody3D* rb = GlBody(this, o2, g, ctx)) { Vec3 d = o2->transform->Position() - o->transform->Position(); float len = d.Magnitude();"
        " if (len > 1e-4f) rb->AddImpulse(d * (1.0f / len) * n0); }",
        reg="CAT_XFORM", exec_=True, defaults=["Goal", "4", None], setup=rb + " " + goal + " goal->transform->SetPosition(Vec3(-3, 0, 0));",
        check="CHECK_NEAR(me->GetComponent<Rigidbody3D>()->velocity.x, 4.0f, 0.01f);")
    getter("GlIsBodyMovable", "Is Body Movable", *P, "Intermediate", "True when the Rigidbody can be pushed around (not Static).", "Result", RC, "GlBody",
           "c->bodyType != Rigidbody3D::BodyType::Static", "false", rb, True, "b", question=True)

    # -----------------------------------------------------------------------
    # Audio
    # -----------------------------------------------------------------------
    A = ("Audio", "")
    au = "me->AddComponent<AudioSource>();"
    AC = "AudioSource"
    add("GlPlaySoundNow", "Play Sound Now", *A, "Basic", "Starts the object's sound from the beginning.",
        [O()], [], "if (AudioSource* a = GlAudio(this, o, g, ctx)) a->Play();",
        reg="CAT_ACTION", exec_=True, setup=au, check="CHECK(me->GetComponent<AudioSource>()->IsPlaying());", demo="audio")
    setter("GlSetSound3D", "Set Sound Position Effect", *A, "Advanced", "On makes the sound louder up close and quieter far away. Off plays it at the same volume everywhere.",
           [B("On", "true")], AC, "GlAudio", "c->spatial = b0;", au, ["true"], "CHECK(me->GetComponent<AudioSource>()->spatial);")
    getter("GlGetPitch", "Get Pitch", *A, "Intermediate", "How high or low the sound plays. 1 is normal.", "Pitch", AC, "GlAudio",
           "c->pitch", "1.0f", au + " me->GetComponent<AudioSource>()->pitch = 1.4f;", 1.4)
    getter("GlIsSoundLooping", "Is Sound Looping", *A, "Intermediate", "True when the sound starts again each time it ends.", "Result", AC, "GlAudio",
           "c->loop", "false", au + " me->GetComponent<AudioSource>()->loop = true;", True, "b", question=True)

    # -----------------------------------------------------------------------
    # Camera
    # -----------------------------------------------------------------------
    C = ("Camera", "")
    cam = "me->AddComponent<Camera>();"
    CC = "Camera"
    setter("GlSetFlatCameraSize", "Set Flat Camera Size", *C, "Basic", "How much a flat (2D) camera shows: half the height, in world units. Bigger sees more.",
           [N("Size", "5.0f")], CC, "GlCamera", "c->orthographicSize = std::max(0.1f, n0);", cam, ["8"],
           "CHECK_NEAR(me->GetComponent<Camera>()->orthographicSize, 8.0f, 0.001f);")
    getter("GlGetFlatCameraSize", "Get Flat Camera Size", *C, "Basic", "How much a flat (2D) camera shows: half the height, in world units.", "Size", CC, "GlCamera",
           "c->orthographicSize", "0.0f", cam + " me->GetComponent<Camera>()->orthographicSize = 6.5f;", 6.5)
    add("GlResetCamera", "Reset Camera", *C, "Basic", "Puts zoom and tilt back to normal.",
        [O()], [], "if (Camera* c = GlCamera(this, o, g, ctx)) { c->zoom = 1.0f; c->dutch = 0.0f; }",
        reg="CAT_ACTION", exec_=True, setup=cam + " me->GetComponent<Camera>()->zoom = 3.0f; me->GetComponent<Camera>()->dutch = 20.0f;",
        check="CHECK_NEAR(me->GetComponent<Camera>()->zoom, 1.0f, 0.001f); CHECK_NEAR(me->GetComponent<Camera>()->dutch, 0.0f, 0.001f);")
    getter("GlIsCameraZoomedIn", "Is Camera Zoomed", *C, "Basic", "True when the camera is zoomed in closer than normal.", "Result", CC, "GlCamera",
           "c->zoom > 1.01f", "false", cam + " me->GetComponent<Camera>()->zoom = 2.0f;", True, "b", question=True)

    # -----------------------------------------------------------------------
    # Quests
    # -----------------------------------------------------------------------
    S = ("Scene", "Story")
    ql = ('GameObject* qo = s.CreateGameObject("Quests"); auto* log = qo->AddComponent<QuestLog>(); log->drawUI = false; log->showNotifications = false;'
          ' for (int qi = 1; qi <= 2; ++qi) { QuestDef qd; qd.id = "q" + std::to_string(qi); qd.title = "Test"; QuestObjective ob; ob.kind = QuestObjective::Kind::Custom; ob.count = 3; qd.objectives.push_back(ob); log->quests.push_back(qd); }')
    add("GlCountCompletedQuests", "Count Completed Quests", *S, "Basic", "How many quests have been completed.",
        [], [N("Count")],
        "QuestLog* q = GlQuest(ctx); float c = 0; if (q) for (const std::string& id : q->Ids()) if (q->IsCompleted(id)) c += 1.0f; out = VsValue(c);",
        setup=ql + ' log->StartQuest("q1"); log->CompleteQuest("q1");', expect=1.0, demo="quests")
    add("GlIsAnyQuestActive", "Is Any Quest Active", *S, "Basic", "True while at least one quest is started and not finished.",
        [], [B("Result")], "QuestLog* q = GlQuest(ctx); out = VsValue(q && !q->ActiveIds().empty());", reg="CAT_LOGIC",
        setup=ql + ' log->StartQuest("q2");', expect=True, etype="b")
    add("GlFailActiveQuests", "Fail All Active Quests", *S, "Intermediate", "Marks every quest that is running as failed.",
        [], [], "if (QuestLog* q = GlQuest(ctx)) { std::vector<std::string> ids = q->ActiveIds(); for (const std::string& id : ids) q->FailQuest(id); }",
        reg="CAT_ACTION", exec_=True, setup=ql + ' log->StartQuest("q1"); log->StartQuest("q2");',
        check='CHECK(s.Find("Quests")->GetComponent<QuestLog>()->ActiveIds().empty()); CHECK(s.Find("Quests")->GetComponent<QuestLog>()->Status("q2") == QuestStatus::Failed);')
    add("GlFinishObjectives", "Finish All Objectives of Quest", *S, "Advanced", "Counts every objective of the quest as reached, which completes it.",
        [T("Quest")], [],
        "if (QuestLog* q = GlQuest(ctx)) if (const QuestDef* d = q->Def(t0)) for (int i = 0; i < (int)d->objectives.size(); ++i) q->CompleteObjective(t0, i);",
        reg="CAT_ACTION", exec_=True, defaults=['"q1"'], setup=ql + ' log->StartQuest("q1");',
        check='CHECK(s.Find("Quests")->GetComponent<QuestLog>()->IsCompleted("q1"));')
    add("GlGetObjectiveCount", "Get Objective Count", *S, "Intermediate", "How many objectives a quest has.",
        [T("Quest")], [N("Count")], "QuestLog* q = GlQuest(ctx); const QuestDef* d = q ? q->Def(t0) : nullptr; out = VsValue(d ? (float)d->objectives.size() : 0.0f);",
        defaults=['"q1"'], setup=ql, expect=1.0)

    # -----------------------------------------------------------------------
    # UI text
    # -----------------------------------------------------------------------
    txt = "me->AddComponent<TextRenderer>();"
    TC = "TextRenderer"
    setter("GlSetTextOpacity", "Set Text Opacity", "UI", "", "Basic", "How see-through the text is: 1 solid, 0 gone.",
           [N("Opacity", "1.0f")], TC, "GlText", "c->color.a = std::min(std::max(n0, 0.0f), 1.0f);", txt, ["0.3"],
           "CHECK_NEAR(me->GetComponent<TextRenderer>()->color.a, 0.3f, 0.001f);")
    setter("GlFadeTextTo", "Fade Text to", "UI", "", "Intermediate", "Moves the text's opacity toward Opacity, Per Second each second. Put it under On Every Frame.",
           [N("Opacity"), N("Per Second", "1.0f")], TC, "GlText",
           "float step = std::max(0.0f, n1) * ctx.deltaTime; float a = c->color.a; c->color.a = a < n0 ? std::min(a + step, n0) : std::max(a - step, n0);", txt, ["0", "1"],
           "CHECK_NEAR(me->GetComponent<TextRenderer>()->color.a, 0.5f, 0.001f);", update=True, dt=0.5)
    add("GlShowTwoNumbersInText", "Show Two Numbers in Text", "UI", "", "Intermediate", "Writes a label and two numbers with a word between, like Coins: 3 of 10.",
        [T("Label"), N("First"), T("Between"), N("Second"), O()], [],
        "if (TextRenderer* tr = GlText(this, o, g, ctx)) tr->text = t0 + GlNumberText(n0, 0) + t1 + GlNumberText(n1, 0);",
        reg="CAT_ACTION", exec_=True, defaults=['"Coins: "', "3", '" of "', "10", None], setup=txt,
        check='CHECK(me->GetComponent<TextRenderer>()->text == "Coins: 3 of 10");', demo="ui")

    # -----------------------------------------------------------------------
    # More movement
    # -----------------------------------------------------------------------
    M = ("Movement", "")
    add("GlRiseAtSpeed", "Rise at Speed", *M, "Basic", "Moves this object straight up, Speed units each second. A negative Speed sinks it.",
        [N("Speed", "1.0f"), O()], [],
        "if (o && o->transform) { Vec3 p = o->transform->Position(); p.y += n0 * ctx.deltaTime; o->transform->SetPosition(p); }",
        reg="CAT_XFORM", exec_=True, defaults=["2", None], update=True, dt=0.5, check="CHECK_NEAR(me->transform->Position().y, 1.0f, 0.001f);")
    add("GlMoveInCircle", "Move in Circle", *M, "Intermediate", "Carries this object around a circle on the ground that starts at its own spot. Speed is radians a second.",
        [N("Radius", "2.0f"), N("Speed", "1.0f"), O()], [],
        "if (o && o->transform) { Vec3 p = o->transform->Position(); if (!seen) { seen = true; mem = p.x; mem2 = p.z; } mem3 += ctx.deltaTime * n1;"
        " p.x = mem + std::sin(mem3) * n0; p.z = mem2 + (1.0f - std::cos(mem3)) * n0; o->transform->SetPosition(p); }",
        reg="CAT_XFORM", exec_=True, defaults=["2", "1.5707963", None], update=True, dt=1.0,
        check="CHECK_NEAR(me->transform->Position().x, 2.0f, 0.01f); CHECK_NEAR(me->transform->Position().z, 2.0f, 0.01f);", demo="movement")
    add("GlChangeSizeOverTime", "Change Size Over Time", "Movement", "Size", "Intermediate", "Grows this object (or shrinks it, with a negative number) by Per Second each second.",
        [N("Per Second", "0.5f"), O()], [],
        "if (o && o->transform) { Vec3 sc = o->transform->localScale; float d = n0 * ctx.deltaTime; o->transform->localScale = Vec3(std::max(0.01f, sc.x + d), std::max(0.01f, sc.y + d), std::max(0.01f, sc.z + d)); }",
        reg="CAT_XFORM", exec_=True, defaults=["1", None], update=True, dt=0.5, check="CHECK_NEAR(me->transform->localScale.x, 1.5f, 0.001f);")
    add("GlMultiplySize", "Multiply Size", "Movement", "Size", "Basic", "Makes this object Factor times bigger. 0.5 halves it, 2 doubles it.",
        [N("Factor", "1.0f"), O()], [], "if (o && o->transform) o->transform->localScale = o->transform->localScale * std::max(0.01f, n0);",
        reg="CAT_XFORM", exec_=True, defaults=["3", None], check="CHECK_NEAR(me->transform->localScale.z, 3.0f, 0.001f);")
    add("GlFaceAwayFrom", "Face Away From Object", "Movement", "Turn", "Basic", "Turns this object flat to look away from another object.",
        [R("Object"), O()], [],
        "if (o && o2 && o->transform && o2->transform) o2->transform->LookAtYaw(o2->transform->Position() + (o2->transform->Position() - o->transform->Position()));",
        reg="CAT_XFORM", exec_=True, defaults=["Goal", None], setup=goal + " goal->transform->SetPosition(Vec3(6, 0, 0));",
        check="CHECK(me->transform->Forward().x < -0.95f);")
    add("GlGetFacingAngle", "Get Facing Angle", "Movement", "Turn", "Basic", "The way this object faces, in degrees around its up axis.",
        [O()], [N("Degrees")], "out = VsValue((o && o->transform) ? o->transform->EulerAngles().y : 0.0f);",
        setup="me->transform->SetEulerAngles(Vec3(0, 40, 0));", expect=40.0)
    add("GlSetFacingAngle", "Set Facing Angle", "Movement", "Turn", "Basic", "Turns this object to face this many degrees around its up axis.",
        [N("Degrees"), O()], [], "if (o && o->transform) o->transform->SetEulerAngles(Vec3(0.0f, n0, 0.0f));",
        reg="CAT_XFORM", exec_=True, defaults=["75", None], check="CHECK_NEAR(me->transform->EulerAngles().y, 75.0f, 0.01f);")
    add("GlSetHeight", "Set Height", *M, "Basic", "Puts this object at a height, leaving where it is on the ground alone.",
        [N("Height"), O()], [], "if (o && o->transform) { Vec3 p = o->transform->Position(); p.y = n0; o->transform->SetPosition(p); }",
        reg="CAT_XFORM", exec_=True, defaults=["3.5", None], setup="me->transform->SetPosition(Vec3(2, 9, 4));",
        check="CHECK_NEAR(me->transform->Position().y, 3.5f, 0.001f); CHECK_NEAR(me->transform->Position().x, 2.0f, 0.001f);")
    add("GlIsAboveHeight", "Is Above Height", *M, "Basic", "True when this object is higher than this.",
        [N("Height"), O()], [B("Result")], "out = VsValue(o && o->transform && o->transform->Position().y > n0);",
        reg="CAT_LOGIC", defaults=["1", None], setup="me->transform->SetPosition(Vec3(0, 2, 0));", expect=True, etype="b")
    add("GlIsBelowHeight", "Is Below Height", *M, "Basic", "True when this object is lower than this.",
        [N("Height"), O()], [B("Result")], "out = VsValue(o && o->transform && o->transform->Position().y < n0);",
        reg="CAT_LOGIC", defaults=["1", None], setup="me->transform->SetPosition(Vec3(0, -2, 0));", expect=True, etype="b")

    # -----------------------------------------------------------------------
    # Input
    # -----------------------------------------------------------------------
    add("GlIsFireHeld", "Is Fire Held", "Input", "Mouse", "Basic", "True while the left mouse button is held down.",
        [], [B("Result")], "out = VsValue(Input::GetMouseButton(0));", reg="CAT_LOGIC", expect=False, etype="b", update=True, demo="input")
    add("GlIsAimHeld", "Is Aim Held", "Input", "Mouse", "Basic", "True while the right mouse button is held down.",
        [], [B("Result")], "out = VsValue(Input::GetMouseButton(1));", reg="CAT_LOGIC", expect=False, etype="b", update=True)
    add("GlIsInteractHeld", "Is Interact Held", "Input", "Keys", "Basic", "True while the E key is held down.",
        [], [B("Result")], "out = VsValue(Input::GetKey('e'));", reg="CAT_LOGIC", setup="Input::FeedKeys({'e'});", update=True, expect=True, etype="b")
    add("GlGetMoveStrength", "Get Move Strength", "Input", "Axes", "Intermediate", "How hard the movement keys are pushed: 0 when none are held, up to 1.",
        [], [N("Strength")], "Vec2 a = Input::AxisWASD(); out = VsValue(std::min(1.0f, a.Magnitude()));", setup="Input::FeedKeys({'w'});", update=True, expect=1.0)
