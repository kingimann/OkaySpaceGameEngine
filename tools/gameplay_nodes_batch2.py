"""Gameplay library, batch 2: perception, camera, UI, audio, physics, animation,
quests, saving, groups, time and input. Called by gen_gameplay_library.py."""


def define(add, N, B, T, O, R):
    # -----------------------------------------------------------------------
    # AI and perception
    # -----------------------------------------------------------------------
    Q = ("Physics", "Queries")
    goal = 'GameObject* goal = s.CreateGameObject("Goal");'
    add("GlIsFacingObject", "Is Facing Object", *Q, "Intermediate", "True when this object looks toward Object, within Angle degrees either side.",
        [R("Object"), N("Angle", "45.0f"), O()], [B("Result")],
        "out = VsValue(o && o2 && GlAngleTo(o2, o) <= n0);",
        reg="CAT_LOGIC", defaults=["Goal", "45", None],
        setup=goal + " goal->transform->SetPosition(me->transform->Forward() * 5.0f);", expect=True, etype="b", demo="ai")
    add("GlGetAngleToObject", "Get Angle to Object", *Q, "Intermediate", "How far, in degrees, this object would have to turn to look straight at Object. 0 is dead ahead, 180 is behind.",
        [R("Object"), O()], [N("Degrees")],
        "out = VsValue(o && o2 ? GlAngleTo(o2, o) : 0.0f);",
        defaults=["Goal", None], setup=goal + " goal->transform->SetPosition(me->transform->Right() * 4.0f);", expect=90.0)
    add("GlIsObjectBehind", "Is Object Behind", *Q, "Intermediate", "True when Object is behind this object, more than 90 degrees from where it looks.",
        [R("Object"), O()], [B("Result")], "out = VsValue(o && o2 && GlAngleTo(o2, o) > 90.0f);",
        reg="CAT_LOGIC", defaults=["Goal", None], setup=goal + " goal->transform->SetPosition(me->transform->Forward() * -3.0f);", expect=True, etype="b")
    add("GlIsObjectHigher", "Is Object Higher", *Q, "Intermediate", "True when Object is above this object by more than Margin.",
        [R("Object"), N("Margin", "0.1f"), O()], [B("Result")],
        "out = VsValue(o && o2 && o->transform && o2->transform && o->transform->Position().y > o2->transform->Position().y + n0);",
        reg="CAT_LOGIC", defaults=["Goal", "0.5", None], setup=goal + " goal->transform->SetPosition(Vec3(0, 2, 0));", expect=True, etype="b")
    add("GlGetDirectionToObject", "Get Direction to Object", *Q, "Intermediate", "A length-1 arrow from this object toward Object, along the world Right, Up and Forward axes.",
        [R("Object"), O()], [N("Right"), N("Up"), N("Forward")],
        "Vec3 d = (o && o2 && o->transform && o2->transform) ? o->transform->Position() - o2->transform->Position() : Vec3::Zero;"
        " float len = d.Magnitude(); if (len > 1e-5f) d = d * (1.0f / len); out = VsValue(pin == 0 ? d.x : (pin == 1 ? d.y : d.z));",
        defaults=["Goal", None], setup=goal + " goal->transform->SetPosition(Vec3(4, 0, 0));", expect=1.0)
    add("GlCountObjectsWithTag", "Count Objects With Tag", *Q, "Basic", "How many active objects carry this tag.",
        [T("Tag")], [N("Count")],
        "float c = 0; if (ctx.gameObject && ctx.gameObject->scene()) for (const auto& up : ctx.gameObject->scene()->Objects()) if (up && up->active && up->tag == t0) c += 1.0f; out = VsValue(c);",
        defaults=['"coin"'],
        setup='for (int i = 0; i < 3; ++i) s.CreateGameObject("C")->tag = "coin"; s.CreateGameObject("Off")->tag = "coin"; s.Find("Off")->active = false;',
        expect=3.0, demo="ai")
    add("GlIsAnyTaggedInRange", "Is Any Tagged Object in Range", *Q, "Intermediate", "True when an active object with this tag is within Range of this object.",
        [T("Tag"), N("Range", "5.0f"), O()], [B("Result")],
        "bool hit = false; if (o && o->transform && o->scene()) for (const auto& up : o->scene()->Objects()) { if (!up || !up->active || up.get() == o || up->tag != t0) continue;"
        " if ((up->transform->Position() - o->transform->Position()).Magnitude() <= n0) { hit = true; break; } } out = VsValue(hit);",
        reg="CAT_LOGIC", defaults=['"enemy"', "4", None],
        setup=goal + ' goal->tag = "enemy"; goal->transform->SetPosition(Vec3(3, 0, 0));', expect=True, etype="b")
    add("GlGetAlertLevel", "Get Alert Level", "Physics", "Queries", "Advanced",
        "A 0 to 1 meter that fills while Sees is true and drains while it is false. Read it once each frame.",
        [B("Sees"), N("Rise Per Second", "1.0f"), N("Fall Per Second", "0.5f")], [N("Level")],
        "mem += (b0 ? std::max(0.0f, n0) : -std::max(0.0f, n1)) * ctx.deltaTime; mem = std::min(std::max(mem, 0.0f), 1.0f); out = VsValue(mem);",
        defaults=["true", "1", "0.5"], update=True, dt=0.25, steps=2, expect=0.5)
    add("GlPickRandomTagged", "Pick Random Tagged Object", *Q, "Intermediate", "An active object chosen at random from those with this tag, or nothing when there is none.",
        [T("Tag")], [("Object", "O", None)],
        "std::vector<GameObject*> all; if (ctx.gameObject && ctx.gameObject->scene()) for (const auto& up : ctx.gameObject->scene()->Objects())"
        " if (up && up->active && up->tag == t0) all.push_back(up.get());"
        " if (!all.empty()) { static std::uint32_t seed = 2463534242u; seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;"
        " out = VsValue::Object(all[seed % all.size()]->InstanceId()); }",
        defaults=['"pick"'], setup='s.CreateGameObject("OnlyOne")->tag = "pick";',
        check='CHECK(Var(v, "out").IsObject()); CHECK(Var(v, "out").AsObjectId() == s.Find("OnlyOne")->InstanceId());')

    # -----------------------------------------------------------------------
    # Camera
    # -----------------------------------------------------------------------
    C = ("Camera", "")
    cam = "me->AddComponent<Camera>();"
    add("GlSetCameraZoom", "Set Camera Zoom", *C, "Basic", "2 shows things twice as close, 0.5 twice as far. Needs a Camera on the object.",
        [N("Zoom", "1.0f"), O()], [], "if (Camera* c = GlCamera(this, o, g, ctx)) c->zoom = std::max(0.05f, n0);",
        reg="CAT_ACTION", exec_=True, defaults=["2", None], setup=cam, check="CHECK_NEAR(me->GetComponent<Camera>()->zoom, 2.0f, 0.001f);", demo="camera")
    add("GlGetCameraZoom", "Get Camera Zoom", *C, "Basic", "How zoomed in the camera is. 1 is normal.",
        [O()], [N("Zoom")], "Camera* c = GlCamera(this, o, g, ctx); out = VsValue(c ? c->zoom : 1.0f);",
        setup=cam + " me->GetComponent<Camera>()->zoom = 1.5f;", expect=1.5)
    add("GlSetFieldOfView", "Set Field of View", *C, "Intermediate", "How wide a perspective camera sees, in degrees from 10 to 170.",
        [N("Degrees", "60.0f"), O()], [], "if (Camera* c = GlCamera(this, o, g, ctx)) c->fieldOfView = std::min(std::max(n0, 10.0f), 170.0f);",
        reg="CAT_ACTION", exec_=True, defaults=["90", None], setup=cam, check="CHECK_NEAR(me->GetComponent<Camera>()->fieldOfView, 90.0f, 0.001f);")
    add("GlGetFieldOfView", "Get Field of View", *C, "Intermediate", "How wide the camera sees, in degrees.",
        [O()], [N("Degrees")], "Camera* c = GlCamera(this, o, g, ctx); out = VsValue(c ? c->fieldOfView : 60.0f);",
        setup=cam + " me->GetComponent<Camera>()->fieldOfView = 75.0f;", expect=75.0)
    add("GlShakeCamera", "Shake Camera", *C, "Basic", "Rattles the camera. 0.1 is a footstep, 0.3 a hit, 0.6 an explosion.",
        [N("Strength", "0.5f"), O()], [], "if (Camera* c = GlCamera(this, o, g, ctx)) c->Shake(std::min(std::max(n0, 0.0f), 1.0f));",
        reg="CAT_ACTION", exec_=True, defaults=["0.4", None], setup=cam, check="CHECK(me->GetComponent<Camera>()->ShakeTrauma() > 0.2f);", demo="camera")
    add("GlTiltCamera", "Tilt Camera", *C, "Advanced", "Rolls the camera sideways by Degrees, like a dutch angle.",
        [N("Degrees"), O()], [], "if (Camera* c = GlCamera(this, o, g, ctx)) c->dutch = n0;",
        reg="CAT_ACTION", exec_=True, defaults=["12", None], setup=cam, check="CHECK_NEAR(me->GetComponent<Camera>()->dutch, 12.0f, 0.001f);")

    # -----------------------------------------------------------------------
    # UI text
    # -----------------------------------------------------------------------
    U = ("UI", "Fields")
    txt = "me->AddComponent<TextRenderer>();"
    add("GlShowNumberInText", "Show Number in Text", "UI", "", "Basic", "Writes a label and a number into a Text object, like Score: 12.",
        [T("Label"), N("Value"), N("Decimals"), O()], [],
        "if (TextRenderer* tr = GlText(this, o, g, ctx)) tr->text = t0 + GlNumberText(n0, (int)std::lround(n1));",
        reg="CAT_ACTION", exec_=True, defaults=['"Score: "', "12.5", "1", None], setup=txt,
        check='CHECK(me->GetComponent<TextRenderer>()->text == "Score: 12.5");', demo="ui")
    add("GlFormatNumber", "Format Number", "UI", "", "Basic", "Turns a number into text with a fixed count of decimals.",
        [N("Value"), N("Decimals")], [T("Text")], "out = VsValue(GlNumberText(n0, (int)std::lround(n1)));",
        defaults=["3.14159", "2"], expect="3.14", etype="s")
    add("GlFormatClock", "Format Clock", "UI", "", "Basic", "Turns seconds into a clock reading like 02:05.",
        [N("Seconds")], [T("Text")],
        "int total = std::max(0, (int)std::floor(n0)); char b[32]; std::snprintf(b, sizeof b, \"%02d:%02d\", total / 60, total % 60); out = VsValue(std::string(b));",
        defaults=["125"], expect="02:05", etype="s", demo="ui")
    add("GlFormatPercent", "Format Percent", "UI", "", "Basic", "Turns a 0 to 1 share into text like 75%.",
        [N("Portion")], [T("Text")],
        "char b[32]; std::snprintf(b, sizeof b, \"%d%%\", (int)std::lround(n0 * 100.0f)); out = VsValue(std::string(b));",
        defaults=["0.75"], expect="75%", etype="s")
    add("GlSetTextColor", "Set Text Color", "UI", "", "Basic", "Colors the text. Red, Green, Blue and Opacity run from 0 to 1.",
        [N("Red", "1.0f"), N("Green", "1.0f"), N("Blue", "1.0f"), N("Opacity", "1.0f"), O()], [],
        "if (TextRenderer* tr = GlText(this, o, g, ctx)) tr->color = Color(n0, n1, n2, n3);",
        reg="CAT_ACTION", exec_=True, defaults=["0.2", "0.4", "0.6", "0.8", None], setup=txt,
        check="{ Color c = me->GetComponent<TextRenderer>()->color; CHECK_NEAR(c.r, 0.2f, 0.001f); CHECK_NEAR(c.b, 0.6f, 0.001f); CHECK_NEAR(c.a, 0.8f, 0.001f); }")
    add("GlGetText", "Get Text", "UI", "", "Basic", "The words a Text object is showing.",
        [O()], [T("Text")], "TextRenderer* tr = GlText(this, o, g, ctx); out = VsValue(tr ? tr->text : std::string());",
        setup=txt + ' me->GetComponent<TextRenderer>()->text = "hello";', expect="hello", etype="s")
    add("GlAddLineToText", "Add Line to Text", "UI", "", "Intermediate", "Adds a new line of words under what a Text object already shows.",
        [T("Line"), O()], [], "if (TextRenderer* tr = GlText(this, o, g, ctx)) tr->text += (tr->text.empty() ? std::string() : std::string(\"\\n\")) + t0;",
        reg="CAT_ACTION", exec_=True, defaults=['"two"', None], setup=txt + ' me->GetComponent<TextRenderer>()->text = "one";',
        check='CHECK(me->GetComponent<TextRenderer>()->text == "one\\ntwo");')
    add("GlSetTextSize", "Set Text Size", "UI", "", "Intermediate", "Scales the text. Bigger numbers draw bigger words.",
        [N("Size", "1.0f"), O()], [], "if (TextRenderer* tr = GlText(this, o, g, ctx)) tr->pixelSize = std::max(0.01f, n0);",
        reg="CAT_ACTION", exec_=True, defaults=["3", None], setup=txt, check="CHECK_NEAR(me->GetComponent<TextRenderer>()->pixelSize, 3.0f, 0.001f);")

    # -----------------------------------------------------------------------
    # Audio
    # -----------------------------------------------------------------------
    A = ("Audio", "")
    au = "me->AddComponent<AudioSource>();"
    add("GlSetVolume", "Set Volume", *A, "Basic", "How loud a sound is: 0 is silent, 1 is full.",
        [N("Volume", "1.0f"), O()], [], "if (AudioSource* a = GlAudio(this, o, g, ctx)) a->volume = std::min(std::max(n0, 0.0f), 4.0f);",
        reg="CAT_ACTION", exec_=True, defaults=["0.4", None], setup=au, check="CHECK_NEAR(me->GetComponent<AudioSource>()->volume, 0.4f, 0.001f);", demo="audio")
    add("GlGetVolume", "Get Volume", *A, "Basic", "How loud a sound is.",
        [O()], [N("Volume")], "AudioSource* a = GlAudio(this, o, g, ctx); out = VsValue(a ? a->volume : 0.0f);",
        setup=au + " me->GetComponent<AudioSource>()->volume = 0.7f;", expect=0.7)
    add("GlSetPitch", "Set Pitch", *A, "Intermediate", "Plays a sound higher (above 1) or lower (below 1), between 0.1 and 3.",
        [N("Pitch", "1.0f"), O()], [], "if (AudioSource* a = GlAudio(this, o, g, ctx)) a->pitch = std::min(std::max(n0, 0.1f), 3.0f);",
        reg="CAT_ACTION", exec_=True, defaults=["1.5", None], setup=au, check="CHECK_NEAR(me->GetComponent<AudioSource>()->pitch, 1.5f, 0.001f);")
    add("GlIsSoundPlaying", "Is Sound Playing", *A, "Basic", "True while the object's sound is playing.",
        [O()], [B("Result")], "AudioSource* a = GlAudio(this, o, g, ctx); out = VsValue(a && a->IsPlaying());",
        reg="CAT_LOGIC", setup=au, post="me->GetComponent<AudioSource>()->Play();", update=True, expect=True, etype="b", demo="audio")
    add("GlPauseSound", "Pause Sound", *A, "Intermediate", "Pauses the object's sound.",
        [O()], [], "if (AudioSource* a = GlAudio(this, o, g, ctx)) a->Pause();",
        reg="CAT_ACTION", exec_=True, setup=au, post="me->GetComponent<AudioSource>()->Play();", update=True, check="CHECK(!me->GetComponent<AudioSource>()->IsPlaying());")
    add("GlSetSoundLooping", "Set Sound Looping", *A, "Intermediate", "A looping sound starts again each time it ends.",
        [B("Loop", "true"), O()], [], "if (AudioSource* a = GlAudio(this, o, g, ctx)) a->loop = b0;",
        reg="CAT_ACTION", exec_=True, defaults=["true", None], setup=au, check="CHECK(me->GetComponent<AudioSource>()->loop);")

    # -----------------------------------------------------------------------
    # Physics
    # -----------------------------------------------------------------------
    P = ("Physics", "")
    rb = "me->AddComponent<Rigidbody3D>();"
    add("GlGetMass", "Get Mass", *P, "Intermediate", "How heavy the Rigidbody is.",
        [O()], [N("Mass")], "Rigidbody3D* rb = GlBody(this, o, g, ctx); out = VsValue(rb ? rb->mass : 0.0f);",
        setup=rb + " me->GetComponent<Rigidbody3D>()->mass = 4.0f;", expect=4.0)
    add("GlSetMass", "Set Mass", *P, "Intermediate", "How heavy the Rigidbody is. A heavy body is shoved less.",
        [N("Mass", "1.0f"), O()], [], "if (Rigidbody3D* rb = GlBody(this, o, g, ctx)) rb->mass = std::max(0.01f, n0);",
        reg="CAT_ACTION", exec_=True, defaults=["6", None], setup=rb, check="CHECK_NEAR(me->GetComponent<Rigidbody3D>()->mass, 6.0f, 0.001f);", demo="physics")
    add("GlSetGravityStrength", "Set Gravity Strength", *P, "Basic", "How much gravity pulls on the Rigidbody: 1 is normal, 0 floats, below 0 rises.",
        [N("Strength", "1.0f"), O()], [], "if (Rigidbody3D* rb = GlBody(this, o, g, ctx)) rb->gravityScale = n0;",
        reg="CAT_ACTION", exec_=True, defaults=["0.2", None], setup=rb, check="CHECK_NEAR(me->GetComponent<Rigidbody3D>()->gravityScale, 0.2f, 0.001f);", demo="physics")
    add("GlSetBounciness", "Set Bounciness", *P, "Intermediate", "How much the Rigidbody bounces back: 0 stops dead, 1 keeps all its speed.",
        [N("Bounce", "0.5f"), O()], [], "if (Rigidbody3D* rb = GlBody(this, o, g, ctx)) rb->bounciness = std::min(std::max(n0, 0.0f), 1.0f);",
        reg="CAT_ACTION", exec_=True, defaults=["0.8", None], setup=rb, check="CHECK_NEAR(me->GetComponent<Rigidbody3D>()->bounciness, 0.8f, 0.001f);")
    add("GlSetAirDrag", "Set Air Drag", *P, "Intermediate", "How quickly the Rigidbody loses speed on its own. 0 keeps going.",
        [N("Drag"), O()], [], "if (Rigidbody3D* rb = GlBody(this, o, g, ctx)) rb->drag = std::max(0.0f, n0);",
        reg="CAT_ACTION", exec_=True, defaults=["2", None], setup=rb, check="CHECK_NEAR(me->GetComponent<Rigidbody3D>()->drag, 2.0f, 0.001f);")
    add("GlLockRotation", "Lock Rotation", *P, "Intermediate", "On keeps the Rigidbody from tipping over or spinning. Off lets hits turn it.",
        [B("Locked", "true"), O()], [], "if (Rigidbody3D* rb = GlBody(this, o, g, ctx)) rb->freezeRotation = b0;",
        reg="CAT_ACTION", exec_=True, defaults=["false", None], setup=rb, check="CHECK(!me->GetComponent<Rigidbody3D>()->freezeRotation);")
    add("GlSpinAtSpeed", "Spin at Speed", *P, "Advanced", "Turns the Rigidbody around its up axis, Degrees each second. Needs Lock Rotation off.",
        [N("Degrees"), O()], [],
        "if (Rigidbody3D* rb = GlBody(this, o, g, ctx)) { rb->freezeRotation = false; rb->angularVelocity = Vec3(0.0f, n0 * 0.0174532925f, 0.0f); rb->WakeUp(); }",
        reg="CAT_ACTION", exec_=True, defaults=["180", None], setup=rb, check="CHECK_NEAR(me->GetComponent<Rigidbody3D>()->angularVelocity.y, 3.14159f, 0.01f);")
    add("GlGetSpinSpeed", "Get Spin Speed", *P, "Advanced", "How fast the Rigidbody is turning, in degrees each second.",
        [O()], [N("Degrees")], "Rigidbody3D* rb = GlBody(this, o, g, ctx); out = VsValue(rb ? rb->angularVelocity.Magnitude() * 57.2957795f : 0.0f);",
        setup=rb + " me->GetComponent<Rigidbody3D>()->angularVelocity = Vec3(0, 1.0f, 0);", expect=57.2958)

    # -----------------------------------------------------------------------
    # Procedural animation
    # -----------------------------------------------------------------------
    AN = ("Animation", "Procedural")
    add("GlPulseSize", "Pulse Size", *AN, "Intermediate", "Makes this object swell and shrink around the size it had. Speed is radians a second, Amount the share it grows.",
        [N("Speed", "3.0f"), N("Amount", "0.2f"), O()], [],
        "if (o && o->transform) { if (!seen) { seen = true; mem = o->transform->localScale.x; } mem2 += ctx.deltaTime * n0;"
        " float k = mem * (1.0f + n1 * std::sin(mem2)); o->transform->localScale = Vec3(k, k, k); }",
        reg="CAT_XFORM", exec_=True, defaults=["1.5707963", "0.2", None], update=True, dt=1.0,
        check="CHECK_NEAR(me->transform->localScale.x, 1.2f, 0.01f);", demo="animation")
    add("GlSwayTurn", "Sway Turn", *AN, "Intermediate", "Swings this object left and right around the way it faced. Speed is radians a second, Degrees how far.",
        [N("Speed", "2.0f"), N("Degrees", "20.0f"), O()], [],
        "if (o && o->transform) { if (!seen) { seen = true; mem = o->transform->EulerAngles().y; } mem2 += ctx.deltaTime * n0;"
        " o->transform->SetEulerAngles(Vec3(0.0f, mem + n1 * std::sin(mem2), 0.0f)); }",
        reg="CAT_XFORM", exec_=True, defaults=["1.5707963", "30", None], update=True, dt=1.0,
        check="CHECK_NEAR(std::fabs(me->transform->EulerAngles().y), 30.0f, 0.5f);", demo="animation")
    add("GlFadeSpriteTo", "Fade Sprite to", *AN, "Intermediate", "Moves a sprite's opacity toward Opacity, Per Second each second. Put it under On Every Frame.",
        [N("Opacity"), N("Per Second", "1.0f"), O()], [],
        "if (SpriteRenderer* sr = GlSprite(this, o, g, ctx)) { float step = std::max(0.0f, n1) * ctx.deltaTime; float a = sr->color.a;"
        " sr->color.a = a < n0 ? std::min(a + step, n0) : std::max(a - step, n0); }",
        reg="CAT_ACTION", exec_=True, defaults=["0", "1", None], update=True, dt=0.5, setup="me->AddComponent<SpriteRenderer>();",
        check="CHECK_NEAR(me->GetComponent<SpriteRenderer>()->color.a, 0.5f, 0.001f);", demo="animation")
    add("GlGetSpriteOpacity", "Get Sprite Opacity", *AN, "Intermediate", "How see-through a sprite is: 1 solid, 0 gone.",
        [O()], [N("Opacity")], "SpriteRenderer* sr = GlSprite(this, o, g, ctx); out = VsValue(sr ? sr->color.a : 1.0f);",
        setup="me->AddComponent<SpriteRenderer>()->color.a = 0.25f;", expect=0.25)

    # -----------------------------------------------------------------------
    # Quests
    # -----------------------------------------------------------------------
    S = ("Scene", "Story")
    ql = ('GameObject* qo = s.CreateGameObject("Quests"); auto* log = qo->AddComponent<QuestLog>(); log->drawUI = false; log->showNotifications = false;'
          ' QuestDef qd; qd.id = "q1"; qd.title = "Test"; QuestObjective ob; ob.kind = QuestObjective::Kind::Custom; ob.count = 3; qd.objectives.push_back(ob); log->quests.push_back(qd);')
    qstart = " log->StartQuest(\"q1\");"
    add("GlIsQuestActive", "Is Quest Active", *S, "Basic", "True while the quest is started and not yet finished.",
        [T("Quest")], [B("Result")], "QuestLog* q = GlQuest(ctx); out = VsValue(q && q->IsActive(t0));",
        reg="CAT_LOGIC", defaults=['"q1"'], setup=ql + qstart, expect=True, etype="b", demo="quests")
    add("GlIsQuestCompleted", "Is Quest Completed", *S, "Basic", "True once the quest has been completed.",
        [T("Quest")], [B("Result")], "QuestLog* q = GlQuest(ctx); out = VsValue(q && q->IsCompleted(t0));",
        reg="CAT_LOGIC", defaults=['"q1"'], setup=ql + qstart + ' log->CompleteQuest("q1");', expect=True, etype="b")
    add("GlCompleteQuest", "Complete Quest", *S, "Basic", "Finishes the quest and hands out its rewards.",
        [T("Quest")], [], "if (QuestLog* q = GlQuest(ctx)) q->CompleteQuest(t0);",
        reg="CAT_ACTION", exec_=True, defaults=['"q1"'], setup=ql + qstart, check='CHECK(s.Find("Quests")->GetComponent<QuestLog>()->IsCompleted("q1"));', demo="quests")
    add("GlFailQuest", "Fail Quest", *S, "Intermediate", "Marks the quest as failed.",
        [T("Quest")], [], "if (QuestLog* q = GlQuest(ctx)) q->FailQuest(t0);",
        reg="CAT_ACTION", exec_=True, defaults=['"q1"'], setup=ql + qstart,
        check='CHECK(s.Find("Quests")->GetComponent<QuestLog>()->Status("q1") == QuestStatus::Failed);')
    add("GlResetQuest", "Reset Quest", *S, "Intermediate", "Puts the quest back to not started, with no progress.",
        [T("Quest")], [], "if (QuestLog* q = GlQuest(ctx)) q->ResetQuest(t0);",
        reg="CAT_ACTION", exec_=True, defaults=['"q1"'], setup=ql + qstart,
        check='CHECK(s.Find("Quests")->GetComponent<QuestLog>()->Status("q1") == QuestStatus::Inactive);')
    add("GlAddQuestProgress", "Add Quest Progress", *S, "Intermediate", "Counts Amount more toward a quest objective. Objectives are counted from 0.",
        [T("Quest"), N("Objective"), N("Amount", "1.0f")], [],
        "if (QuestLog* q = GlQuest(ctx)) q->AddProgress(t0, (int)std::lround(n0), std::max(1, (int)std::lround(n1)));",
        reg="CAT_ACTION", exec_=True, defaults=['"q1"', "0", "2"], setup=ql + qstart,
        check='CHECK(s.Find("Quests")->GetComponent<QuestLog>()->Progress("q1", 0) == 2);')
    add("GlGetQuestProgress", "Get Quest Progress", *S, "Intermediate", "How far along a quest objective is. Objectives are counted from 0.",
        [T("Quest"), N("Objective")], [N("Progress")], "QuestLog* q = GlQuest(ctx); out = VsValue(q ? (float)q->Progress(t0, (int)std::lround(n0)) : 0.0f);",
        defaults=['"q1"', "0"], setup=ql + qstart + ' log->AddProgress("q1", 0, 2);', expect=2.0)
    add("GlIsObjectiveDone", "Is Objective Done", *S, "Intermediate", "True when that quest objective has been reached. Objectives are counted from 0.",
        [T("Quest"), N("Objective")], [B("Result")], "QuestLog* q = GlQuest(ctx); out = VsValue(q && q->ObjectiveDone(t0, (int)std::lround(n0)));",
        reg="CAT_LOGIC", defaults=['"q1"', "0"], setup=ql + qstart + ' log->AddProgress("q1", 0, 3);', expect=True, etype="b")
    add("GlCountActiveQuests", "Count Active Quests", *S, "Basic", "How many quests are started and not finished.",
        [], [N("Count")], "QuestLog* q = GlQuest(ctx); out = VsValue(q ? (float)q->ActiveIds().size() : 0.0f);",
        setup=ql + qstart, expect=1.0)

    # -----------------------------------------------------------------------
    # Saving
    # -----------------------------------------------------------------------
    V = ("Variables", "Saved")
    add("GlSavePosition", "Save Position", *V, "Intermediate", "Remembers where this object is, under a name, so Load Position can bring it back.",
        [T("Name"), O()], [], "if (o && o->transform) Prefs::SetVec3(\"vs.position.\" + t0, o->transform->Position());",
        reg="CAT_ACTION", exec_=True, defaults=['"spot"', None], setup="me->transform->SetPosition(Vec3(4, 1, -2));",
        check='{ Vec3 p = Prefs::GetVec3("vs.position.spot"); CHECK_NEAR(p.x, 4.0f, 0.001f); CHECK_NEAR(p.z, -2.0f, 0.001f); Prefs::Delete("vs.position.spot"); }', demo="saving")
    add("GlLoadPosition", "Load Position", *V, "Intermediate", "Moves this object to a position saved under that name. Runs Loaded, or Nothing Saved when there is none.",
        [T("Name"), O()], [],
        "if (o && o->transform && Prefs::Has(\"vs.position.\" + t0)) { o->transform->SetPosition(Prefs::GetVec3(\"vs.position.\" + t0)); flow = 0; } else flow = 1;",
        reg="CAT_ACTION", exec_=True, exec_outs=["Loaded", "Nothing Saved"], defaults=['"back"', None],
        setup='Prefs::SetVec3("vs.position.back", Vec3(7, 0, 7));',
        check='CHECK_NEAR(me->transform->Position().x, 7.0f, 0.001f); Prefs::Delete("vs.position.back");')
    add("GlHasSavedPosition", "Has Saved Position", *V, "Intermediate", "True when a position was saved under that name.",
        [T("Name")], [B("Result")], "out = VsValue(Prefs::Has(\"vs.position.\" + t0));",
        reg="CAT_LOGIC", defaults=['"known"'], setup='Prefs::SetVec3("vs.position.known", Vec3(1, 2, 3));', alt='Prefs::Delete("vs.position.known");', expect=True, etype="b")
    add("GlForgetSavedPosition", "Forget Saved Position", *V, "Intermediate", "Throws away the position saved under that name.",
        [T("Name")], [], "Prefs::Delete(\"vs.position.\" + t0);",
        reg="CAT_ACTION", exec_=True, defaults=['"gone"'], setup='Prefs::SetVec3("vs.position.gone", Vec3(1, 2, 3));',
        check='CHECK(!Prefs::Has("vs.position.gone"));')

    # -----------------------------------------------------------------------
    # Groups of objects
    # -----------------------------------------------------------------------
    G = ("Objects", "Groups")
    three = 'for (int i = 0; i < 3; ++i) s.CreateGameObject("Crate" + std::to_string(i))->tag = "crate";'
    add("GlDestroyAllWithTag", "Destroy All With Tag", *G, "Intermediate", "Removes every object that carries this tag.",
        [T("Tag")], [],
        "if (ctx.gameObject && ctx.gameObject->scene()) { Scene* sc = ctx.gameObject->scene(); std::vector<GameObject*> hit;"
        " for (const auto& up : sc->Objects()) if (up && up.get() != ctx.gameObject && up->tag == t0) hit.push_back(up.get()); for (GameObject* x : hit) sc->Destroy(x); }",
        reg="CAT_ACTION", exec_=True, defaults=['"crate"'], setup=three,
        post="s.Update(0.0f);", check='CHECK(s.FindGameObjectsWithTag("crate").empty());', demo="groups")
    add("GlHideAllWithTag", "Hide All With Tag", *G, "Intermediate", "Switches off every object that carries this tag.",
        [T("Tag")], [],
        "if (ctx.gameObject && ctx.gameObject->scene()) for (const auto& up : ctx.gameObject->scene()->Objects()) if (up && up.get() != ctx.gameObject && up->tag == t0) up->active = false;",
        reg="CAT_ACTION", exec_=True, defaults=['"crate"'], setup=three,
        check='CHECK(!s.Find("Crate0")->active); CHECK(!s.Find("Crate2")->active);', demo="groups")
    add("GlShowAllWithTag", "Show All With Tag", *G, "Intermediate", "Switches on every object that carries this tag.",
        [T("Tag")], [],
        "if (ctx.gameObject && ctx.gameObject->scene()) for (const auto& up : ctx.gameObject->scene()->Objects()) if (up && up->tag == t0) up->active = true;",
        reg="CAT_ACTION", exec_=True, defaults=['"crate"'], setup=three + ' s.Find("Crate1")->active = false;',
        check='CHECK(s.Find("Crate1")->active);')
    add("GlSetTagOnObject", "Set Tag on Object", *G, "Intermediate", "Gives this object a tag, so other nodes can find it with Count Objects With Tag and the like.",
        [T("Tag"), O()], [], "if (o) o->SetTag(t0);",
        reg="CAT_ACTION", exec_=True, defaults=['"loot"', None], check='CHECK(std::string(me->tag) == "loot");')

    # -----------------------------------------------------------------------
    # Time
    # -----------------------------------------------------------------------
    TM = ("Time", "")
    add("GlSlowMotion", "Slow Motion for Seconds", *TM, "Advanced", "Holds the game at Scale speed for Seconds of real time, then returns to normal. 0 freezes it.",
        [N("Scale", "0.3f"), N("Seconds", "1.0f")], [], "Time::HitStop(std::max(0.0f, n1), std::max(0.0f, n0));",
        reg="CAT_ACTION", exec_=True, defaults=["0.25", "2"], check="CHECK(Time::HitStopActive()); Time::CancelHitStop();", demo="timing")
    add("GlResetGameSpeed", "Reset Game Speed", *TM, "Basic", "Puts the game back to normal speed and ends any slow motion.",
        [], [], "Time::CancelHitStop(); Time::SetTimeScale(1.0f);",
        reg="CAT_ACTION", exec_=True, setup="Time::SetTimeScale(0.5f);", check="CHECK_NEAR(Time::BaseTimeScale(), 1.0f, 0.001f);")
    add("GlGetSessionTime", "Get Session Time", *TM, "Basic", "Seconds of play since the game started.",
        [], [N("Seconds")], "out = VsValue((float)Time::SessionTime());", setup="Time::SetTimeScale(1.0f); Time::CancelHitStop(); Time::MarkSessionStart(); Time::Step(3.0f);", alt="Time::MarkSessionStart();", check="CHECK_NEAR(Var(v, \"out\").AsFloat(), 3.0f, 0.1f);")
    add("GlIsEveryOtherInterval", "Is Every Other Interval", *TM, "Intermediate", "Flips between true and false each time Seconds pass. Handy for a blink.",
        [N("Seconds", "1.0f")], [B("Result")],
        "float len = std::max(0.01f, n0); out = VsValue((((long long)std::floor(Time::ElapsedTime() / len)) % 2) == 0);",
        reg="CAT_LOGIC", defaults=["1"], setup="Time::SetTimeScale(1.0f); Time::CancelHitStop(); Time::Step(1.5f);", alt="Time::SetTimeScale(1.0f); Time::CancelHitStop(); Time::Step(2.5f);",
        check="CHECK(Var(v, \"out\").AsBool() == ((((long long)std::floor(Time::ElapsedTime() / 1.0f)) % 2) == 0));")

    # -----------------------------------------------------------------------
    # Input
    # -----------------------------------------------------------------------
    IN = ("Input", "Axes")
    add("GlGetHorizontalInput", "Get Horizontal Input", *IN, "Basic", "-1 while left (A or the left arrow) is held, 1 while right (D) is held, 0 otherwise.",
        [], [N("Amount")], "out = VsValue(Input::AxisWASD().x);", setup="Input::FeedKeys({'d'});", update=True, expect=1.0, demo="input")
    add("GlGetVerticalInput", "Get Vertical Input", *IN, "Basic", "-1 while down (S) is held, 1 while up (W) is held, 0 otherwise.",
        [], [N("Amount")], "out = VsValue(Input::AxisWASD().y);", setup="Input::FeedKeys({'s'});", update=True, expect=-1.0)
    add("GlIsSprinting", "Is Sprinting", "Input", "Keys", "Basic", "True while the Shift key is held.",
        [], [B("Result")], "out = VsValue(Input::GetKey(Input::KeyShift));", reg="CAT_LOGIC",
        setup="Input::FeedKeys({Input::KeyShift});", update=True, expect=True, etype="b", demo="input")
    add("GlIsCrouching", "Is Crouching", "Input", "Keys", "Basic", "True while the Control key is held.",
        [], [B("Result")], "out = VsValue(Input::GetKey(Input::KeyCtrl));", reg="CAT_LOGIC",
        setup="Input::FeedKeys({Input::KeyCtrl});", update=True, expect=True, etype="b")
    add("GlIsAnyKeyHeld", "Is Any Key Held", "Input", "Keys", "Basic", "True while any key is held down.",
        [], [B("Result")], "out = VsValue(Input::AnyKey());", reg="CAT_LOGIC",
        setup="Input::FeedKeys({'q'});", update=True, expect=True, etype="b")
