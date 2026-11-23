"""Gameplay library, batch 7: friendly blocks for jobs a beginner otherwise reaches only through raw
text-script (Fn.*) or Engine API nodes: camera-relative directions, character speed, sprint and crouch,
vehicles, menus and sliders, rebinding and rumble, audio buses, colliders, save slots, lights, a
countdown, splitting text and debug drawing. Each one calls the engine system that already does the
job (helpers in engine/include/okay/VisualScript/NodesFriendlyHelpers.hpp). Called by
gen_gameplay_library.py."""


def define(add, N, B, T, O, R, V, L, A):
    sc = "ctx.gameObject ? ctx.gameObject->scene() : nullptr"

    # ---- Camera-relative movement -----------------------------------------------------------
    cam = ("GameObject* cg = s.CreateGameObject(\"Cam\"); cg->AddComponent<Camera>(); "
           "cg->transform->SetRotation(Quat::AngleAxis(90.0f, Vec3(0, 1, 0)) * Quat::AngleAxis(-30.0f, Vec3(1, 0, 0)));")
    add("GlGetCameraForward", "Get Camera Forward", "Camera", "", "Basic",
        "The way the camera looks, flat along the ground and one unit long. Multiply it by the up and down input to walk where the camera faces.",
        [], [V("Direction")],
        f"Camera* c = FrViewCamera({sc}); if (!c) g.Error(this, ctx, \"there is no camera in the scene\"); out = VsValue(FrCameraFlat(c, false));",
        reg="CAT_XFORM", setup=cam, demo="camera",
        check="{ Vec3 d = Var(v, \"out\").AsVec3(); CHECK_NEAR(d.x, -1.0f, 0.01f); CHECK_NEAR(d.y, 0.0f, 0.001f); CHECK_NEAR(d.z, 0.0f, 0.01f); }")
    add("GlGetCameraRight", "Get Camera Right", "Camera", "", "Basic",
        "The camera's right, flat along the ground and one unit long. Multiply it by the left and right input to strafe the way the camera sees it.",
        [], [V("Direction")],
        f"Camera* c = FrViewCamera({sc}); if (!c) g.Error(this, ctx, \"there is no camera in the scene\"); out = VsValue(FrCameraFlat(c, true));",
        reg="CAT_XFORM", setup=cam, demo="camera",
        check="{ Vec3 d = Var(v, \"out\").AsVec3(); CHECK_NEAR(d.x, 0.0f, 0.01f); CHECK_NEAR(d.y, 0.0f, 0.001f); CHECK_NEAR(d.z, -1.0f, 0.01f); }")

    # ---- Character: speed, sprint, crouch --------------------------------------------------
    tp = "auto* tp = me->AddComponent<ThirdPersonController>(); tp->walkSpeed = 2.0f; tp->runSpeed = 4.0f; "
    nomove = "\"its object has no player controller (first person, third person, character or top-down)\""
    add("GlSetMoveSpeed", "Set Move Speed", "Movement", "", "Basic",
        "Sets how fast a player walks, in units a second. Running stays as many times faster as it was.",
        [N("Speed", "5.0f"), O()], [],
        f"if (o && !FrSetMoveSpeed(o, n0)) g.Error(this, ctx, {nomove});",
        reg="CAT_ACTION", exec_=True, defaults=["3", None], setup=tp, demo="movement",
        check="CHECK_NEAR(me->GetComponent<ThirdPersonController>()->walkSpeed, 3.0f, 0.001f); CHECK_NEAR(me->GetComponent<ThirdPersonController>()->runSpeed, 6.0f, 0.001f);")
    add("GlGetMoveSpeed", "Get Move Speed", "Movement", "", "Basic",
        "How fast a player walks, in units a second (its controller's walk speed).",
        [O()], [N("Speed")],
        f"FrSpeeds sp = FrMoverSpeeds(o); if (o && sp.walk.empty()) g.Error(this, ctx, {nomove}); out = VsValue(sp.walk.empty() ? 0.0f : *sp.walk[0]);",
        setup=tp, expect=2.0, demo="movement")
    stance = "\"its object has no first- or third-person player controller\""
    add("GlSetSprinting", "Set Sprinting", "Movement", "", "Basic",
        "Makes the player run (on) or walk (off) as if the sprint key were held. Off gives the sprint key back.",
        [B("Sprint", "true"), O()], [],
        f"if (o && !FrWithStanceController(o, [&](auto* c) {{ c->sprintByScript = b0; }})) g.Error(this, ctx, {stance});",
        reg="CAT_ACTION", exec_=True, defaults=["true", None], setup=tp, demo="movement",
        check="CHECK(me->GetComponent<ThirdPersonController>()->sprintByScript);")
    add("GlSetCrouching", "Set Crouching", "Movement", "", "Basic",
        "Makes the player crouch (on) or stand up (off). Standing waits until there is room above.",
        [B("Crouch", "true"), O()], [],
        f"if (o && !FrWithStanceController(o, [&](auto* c) {{ using C = std::remove_pointer_t<decltype(c)>; c->SetStance(b0 ? C::Stance::Crouch : C::Stance::Stand); }})) g.Error(this, ctx, {stance});",
        reg="CAT_ACTION", exec_=True, defaults=["true", None], setup=tp, update=True, steps=3, demo="movement",
        check="CHECK(me->GetComponent<ThirdPersonController>()->stance() == ThirdPersonController::Stance::Crouch);")

    # ---- Vehicles ----------------------------------------------------------------------------
    car = ("GameObject* car = s.CreateGameObject(\"Car\"); car->AddComponent<VehicleController>(); "
           "GameObject* st = s.CreateGameObject(\"Seat\"); st->transform->SetParent(car->transform); st->AddComponent<VehicleSeat>(); ")
    add("GlEnterVehicle", "Enter Vehicle", "Movement", "", "Basic",
        "Puts the driver (this object when empty) in the vehicle's free driver seat, else a free passenger seat.",
        [R("Vehicle"), O("Driver")], [],
        "if (o) { if (FrEnterVehicle(o, o2)) flow = 0; else flow = 1; }",
        reg="CAT_ACTION", exec_=True, exec_outs=["Entered", "Seats Full"], defaults=["Car", None], setup=car, demo="movement",
        check="CHECK(s.Find(\"Seat\")->GetComponent<VehicleSeat>()->Occupant() == me);")
    add("GlExitVehicle", "Exit Vehicle", "Movement", "", "Basic",
        "Gets the driver (this object when empty) out of the vehicle seat it sits in, beside the door.",
        [O("Driver")], [],
        "if (o) if (VehicleSeat* seat = FrSeatOf(o)) seat->Exit();",
        reg="CAT_ACTION", exec_=True, setup=car + "s.Find(\"Seat\")->GetComponent<VehicleSeat>()->Enter(me);", demo="movement",
        check="CHECK(s.Find(\"Seat\")->GetComponent<VehicleSeat>()->Occupant() == nullptr);")
    add("GlIsDriving", "Is Driving", "Movement", "", "Basic",
        "True while this object (or Target) sits in a vehicle's driver seat.",
        [O()], [B("Result")], "out = VsValue(FrIsDriving(o));",
        reg="CAT_LOGIC", setup=car + "s.Find(\"Seat\")->GetComponent<VehicleSeat>()->Enter(me);", expect=True, etype="b", demo="movement")

    # ---- UI: sliders and menus ---------------------------------------------------------------
    add("GlGetSliderValue", "Get Slider Value", "UI", "", "Basic", "The number a UI slider is set to now.",
        [O()], [N("Value")],
        "UISlider* sl = o ? o->GetComponent<UISlider>() : nullptr; if (o && !sl) g.Error(this, ctx, \"its object has no UI Slider\"); out = VsValue(sl ? sl->value : 0.0f);",
        setup="auto* sl = me->AddComponent<UISlider>(); sl->minValue = 0; sl->maxValue = 10; sl->value = 7;", expect=7.0, demo="ui")
    menu = "\"its object has no menu (UI Modal or Pause Menu); it is switched on or off instead\""
    add("GlOpenMenu", "Open Menu", "UI", "", "Basic",
        "Shows a menu: opens a UI Modal, pauses with a Pause Menu, or switches the object on.",
        [O("Menu")], [],
        "if (o) { if (auto* m = o->GetComponent<UIModal>()) m->Open(); else if (auto* p = o->GetComponent<PauseMenu>()) p->Pause(); else o->active = true; }",
        reg="CAT_ACTION", exec_=True, setup="GameObject* mn = s.CreateGameObject(\"Options\"); mn->active = false;", defaults=["Options"], demo="ui",
        check="CHECK(s.Find(\"Options\") && s.Find(\"Options\")->active);")
    add("GlCloseMenu", "Close Menu", "UI", "", "Basic",
        "Hides a menu: closes a UI Modal, resumes from a Pause Menu, or switches the object off.",
        [O("Menu")], [],
        "if (o) { if (auto* m = o->GetComponent<UIModal>()) m->Close(); else if (auto* p = o->GetComponent<PauseMenu>()) p->Resume(); else o->active = false; }",
        reg="CAT_ACTION", exec_=True, setup="s.CreateGameObject(\"Options\");", defaults=["Options"], demo="ui",
        check="CHECK(s.Find(\"Options\") == nullptr || !s.Find(\"Options\")->active);")

    # ---- Input: rebind and rumble ------------------------------------------------------------
    add("GlRebindAction", "Rebind Action", "Input", "Keys", "Intermediate",
        "Changes the key or button of a named action (Jump, Fire...), like key:j or pad:rt. Saved for the player.",
        [T("Action"), T("Binding")], [],
        "if (InputActions::Rebind(t0, t1)) flow = 0; else flow = 1;",
        reg="CAT_ACTION", exec_=True, exec_outs=["Done", "Not Understood"], defaults=["\"Jump\"", "\"key:j\""], demo="input",
        check="CHECK(InputActions::ExportBindings().find(\"key:j\") != std::string::npos); InputActions::Reset();")
    add("GlRumble", "Rumble Controller", "Input", "Keys", "Basic",
        "Shakes the gamepad. Strength runs from 0 to 1, for Seconds.",
        [N("Strength", "0.5f"), N("Seconds", "0.3f")], [],
        "const float k = std::min(std::max(n0, 0.0f), 1.0f); Input::Rumble(k, k, std::max(0.0f, n1));",
        reg="CAT_ACTION", exec_=True, defaults=["0.8", "0.5"], demo="input",
        check="(void)0;   // the snapshot reads the rumble request (Input::TakeRumble); test_vs_node_values checks the numbers")

    # ---- Audio buses -------------------------------------------------------------------------
    bus = ("AudioGroup grp; if (!AudioMixer::GroupFromName(t0.c_str(), grp)) { g.Error(this, ctx, \"there is no sound bus called '\" + t0 + \"' (use Music, SFX, UI or Voice)\"); return 0; } ")
    add("GlSetBusVolume", "Set Bus Volume", "Audio", "", "Basic",
        "Sets the volume of one sound bus (Music, SFX, UI or Voice) from 0 to 1, like a settings menu does.",
        [T("Bus"), N("Volume", "1.0f")], [],
        bus + "AudioMixer::groupVolume[(int)grp] = std::min(std::max(n0, 0.0f), 1.0f);",
        reg="CAT_ACTION", exec_=True, defaults=["\"Music\"", "0.4"], demo="audio",
        check="{ AudioGroup m; CHECK(AudioMixer::GroupFromName(\"Music\", m)); CHECK_NEAR(AudioMixer::groupVolume[(int)m], 0.4f, 0.001f); AudioMixer::groupVolume[(int)m] = 1.0f; }")
    add("GlMuteBus", "Mute Bus", "Audio", "", "Basic",
        "Silences one sound bus (Music, SFX, UI or Voice), or turns it back on when Mute is off.",
        [T("Bus"), B("Mute", "true")], [],
        bus + "AudioMixer::groupMuted[(int)grp] = b0;",
        reg="CAT_ACTION", exec_=True, defaults=["\"SFX\"", "true"], demo="audio",
        check="{ AudioGroup m; CHECK(AudioMixer::GroupFromName(\"SFX\", m)); CHECK(AudioMixer::groupMuted[(int)m]); AudioMixer::groupMuted[(int)m] = false; }")

    # ---- Colliders ---------------------------------------------------------------------------
    nocol = "\"its object has no collider\""
    add("GlSetColliderSize", "Set Collider Size", "Physics", "", "Intermediate",
        "Resizes this object's colliders: a box to Width, Height and Depth, a sphere or circle to Width across, a capsule to Width and Height.",
        [N("Width", "1.0f"), N("Height", "1.0f"), N("Depth", "1.0f"), O()], [],
        f"if (o && FrSetColliderSize(o, n0, n1, n2) == 0) g.Error(this, ctx, {nocol});",
        reg="CAT_ACTION", exec_=True, defaults=["2", "3", "4", None], setup="me->AddComponent<BoxCollider3D>();", demo="physics",
        check="{ Vec3 z = me->GetComponent<BoxCollider3D>()->size; CHECK_NEAR(z.x, 2.0f, 0.001f); CHECK_NEAR(z.y, 3.0f, 0.001f); CHECK_NEAR(z.z, 4.0f, 0.001f); }")
    add("GlEnableCollider", "Enable Collider", "Physics", "", "Basic",
        "Switches this object's colliders on, so it bumps into things and triggers again.",
        [O()], [], f"if (o && FrSetCollidersEnabled(o, true) == 0) g.Error(this, ctx, {nocol});",
        reg="CAT_ACTION", exec_=True, setup="me->AddComponent<BoxCollider3D>()->enabled = false;", demo="physics",
        check="CHECK(me->GetComponent<BoxCollider3D>()->enabled);")
    add("GlDisableCollider", "Disable Collider", "Physics", "", "Basic",
        "Switches this object's colliders off, so things pass through it and its triggers stop.",
        [O()], [], f"if (o && FrSetCollidersEnabled(o, false) == 0) g.Error(this, ctx, {nocol});",
        reg="CAT_ACTION", exec_=True, setup="me->AddComponent<BoxCollider3D>();", demo="physics",
        check="CHECK(!me->GetComponent<BoxCollider3D>()->enabled);")
    add("GlIsGrounded", "Is Grounded", "Physics", "", "Basic",
        "True while this object stands on something: its player controller says so, or the ground is just under its collider.",
        [O()], [B("Result")], "out = VsValue(FrIsGrounded(o));",
        reg="CAT_LOGIC", setup="me->AddComponent<BoxCollider3D>(); GameObject* fl = s.CreateGameObject(\"Floor\"); fl->transform->SetPosition(Vec3(0, -1.0f, 0)); fl->AddComponent<BoxCollider3D>()->size = Vec3(10, 1, 10);",
        expect=True, etype="b", demo="physics")

    # ---- Save slots --------------------------------------------------------------------------
    add("GlSaveGame", "Save Game", "Variables", "Saved", "Basic",
        "Saves the whole game (every object, the player, items and variables) into a named slot.",
        [T("Slot")], [],
        f"Scene* scn = {sc}; std::string slot = t0.empty() ? std::string(\"1\") : t0; if (scn && SaveGame::Save(*scn, slot, slot)) flow = 0; else flow = 1;",
        reg="CAT_ACTION", exec_=True, exec_outs=["Saved", "Failed"], defaults=["\"vstest\""], demo="saving",
        check="CHECK(SaveGame::Exists(\"vstest\")); SaveGame::Delete(\"vstest\");")
    add("GlLoadGame", "Load Game", "Variables", "Saved", "Basic",
        "Loads the game saved in a slot, at the end of this frame. Nothing Saved runs when the slot is empty.",
        [T("Slot")], [],
        f"Scene* scn = {sc}; std::string slot = t0.empty() ? std::string(\"1\") : t0; if (scn && SaveGame::Exists(slot) && SaveGame::Load(*scn, slot)) flow = 0; else flow = 1;",
        reg="CAT_ACTION", exec_=True, exec_outs=["Loading", "Nothing Saved"], defaults=["\"vsnothing\""], demo="saving",
        graph='node 4 SetVar branch\ndefault 4 0 "none"\nexec 2 1 4',
        check="CHECK(Var(v, \"branch\").AsString() == \"none\");")
    add("GlHasSave", "Has Save", "Variables", "Saved", "Basic", "True when a slot has a saved game in it.",
        [T("Slot")], [B("Result")], "out = VsValue(SaveGame::Exists(t0.empty() ? std::string(\"1\") : t0));",
        reg="CAT_LOGIC", defaults=["\"vsnothing\""], expect=False, etype="b", demo="saving")

    # ---- Lights ------------------------------------------------------------------------------
    nolight = "\"its object has no Light\""
    add("GlSetLightColor", "Set Light Color", "Scene", "Lights", "Basic",
        "Colors a light. Red, Green and Blue run from 0 to 1.",
        [N("Red", "1.0f"), N("Green", "1.0f"), N("Blue", "1.0f"), O()], [],
        f"Light* lt = o ? o->GetComponent<Light>() : nullptr; if (o && !lt) g.Error(this, ctx, {nolight}); if (lt) lt->color = Color(n0, n1, n2, 1.0f);",
        reg="CAT_ACTION", exec_=True, defaults=["1", "0.5", "0.25", None], setup="me->AddComponent<Light>();", demo="timing",
        check="{ Color c = me->GetComponent<Light>()->color; CHECK_NEAR(c.r, 1.0f, 0.001f); CHECK_NEAR(c.g, 0.5f, 0.001f); CHECK_NEAR(c.b, 0.25f, 0.001f); }")
    add("GlSetLightIntensity", "Set Light Intensity", "Scene", "Lights", "Basic",
        "How bright a light is. 1 is normal, 0 is off, 2 twice as bright.",
        [N("Intensity", "1.0f"), O()], [],
        f"Light* lt = o ? o->GetComponent<Light>() : nullptr; if (o && !lt) g.Error(this, ctx, {nolight}); if (lt) lt->intensity = std::max(0.0f, n0);",
        reg="CAT_ACTION", exec_=True, defaults=["2.5", None], setup="me->AddComponent<Light>();", demo="timing",
        check="CHECK_NEAR(me->GetComponent<Light>()->intensity, 2.5f, 0.001f);")

    # ---- Countdown and text ------------------------------------------------------------------
    add("GlCountdownTimer", "Countdown Timer", "Time", "", "Basic",
        "Put it after On Update. Counts down Seconds; Counting runs each frame until then, On Finished runs once at zero. Restart starts again.",
        [N("Seconds", "5.0f"), B("Restart")], [],
        "if (!seen || b0) { mem = std::max(0.0f, n0); seen = true; flag = false; } "
        "if (flag) return -1; "
        "mem -= std::max(0.0f, ctx.deltaTime); "
        "if (mem <= 1e-6f) { mem = 0.0f; flag = true; return 1; } "
        "return 0;",
        reg="CAT_FLOW", exec_=True, exec_outs=["Counting", "On Finished"], defaults=["0.5", None], update=True, steps=10, demo="timing",
        graph="node 4 AddVar done\ndefault 4 0 1\nexec 2 1 4\nnode 5 AddVar ticks\ndefault 5 0 1\nexec 2 0 5",
        check="CHECK_NEAR(Var(v, \"done\").AsFloat(), 1.0f, 0.001f); CHECK_NEAR(Var(v, \"ticks\").AsFloat(), 4.0f, 0.001f);")
    add("GlSplitText", "Split Text", "Variables", "", "Basic",
        "Cuts a text into a list of pieces at every Separator, like a,b,c at a comma. An empty Separator gives one letter each.",
        [T("Text"), T("Separator")], [L("Pieces")],
        "VsValue arr = VsValue::MakeArray(); auto list = arr.AsArray(); "
        "if (t1.empty()) { for (char c : t0) list->push_back(VsValue(std::string(1, c))); } "
        "else { std::size_t at = 0, nx; while ((nx = t0.find(t1, at)) != std::string::npos) { list->push_back(VsValue(t0.substr(at, nx - at))); at = nx + t1.size(); } list->push_back(VsValue(t0.substr(at))); } "
        "out = arr;",
        defaults=["\"red,green,blue\"", "\",\""], demo="progression",
        check="CHECK(Var(v, \"out\").AsString() == \"[red, green, blue]\");")

    # ---- Debug drawing -----------------------------------------------------------------------
    add("GlDrawDebugSphere", "Draw Debug Sphere", "Scene", "Debug", "Intermediate",
        "Draws a wire sphere (three circles) at Center while playing, with this object's debug line. Handy to see a range.",
        [V("Center"), N("Radius", "0.5f")], [],
        "if (!ctx.gameObject) { g.Error(this, ctx, \"this node needs an object\"); return 0; } FrDebugLine(ctx.gameObject)->SetPositions(FrSpherePoints(v0, n0));",
        reg="CAT_ACTION", exec_=True, defaults=[None, "2"], demo="timing",
        check="{ auto& p = me->GetComponent<LineRenderer>()->positions; CHECK(p.size() == 79); for (const Vec3& q : p) CHECK_NEAR(q.Magnitude(), 2.0f, 0.001f); }")
    add("GlDrawDebugBox", "Draw Debug Box", "Scene", "Debug", "Intermediate",
        "Draws a wire box at Center with this Size while playing, with this object's debug line. Handy to see an area.",
        [V("Center"), V("Size", "Vec3{1.0f, 1.0f, 1.0f}")], [],
        "if (!ctx.gameObject) { g.Error(this, ctx, \"this node needs an object\"); return 0; } FrDebugLine(ctx.gameObject)->SetPositions(FrBoxPoints(v0, v1));",
        reg="CAT_ACTION", exec_=True, demo="timing",
        check="{ auto& p = me->GetComponent<LineRenderer>()->positions; CHECK(p.size() == 16); for (const Vec3& q : p) { CHECK_NEAR(std::fabs(q.x), 0.5f, 0.001f); CHECK_NEAR(std::fabs(q.y), 0.5f, 0.001f); CHECK_NEAR(std::fabs(q.z), 0.5f, 0.001f); } }")
    add("GlWatchValue", "Watch Value", "Scene", "Debug", "Basic",
        "Shows Name: Value on the screen while playing, one line per name, updated each time it runs. Put it after On Update.",
        [T("Name"), A("Value")], [],
        f"FrWatch({sc}, t0, a0.AsString());",
        reg="CAT_ACTION", exec_=True, defaults=["\"speed\"", "12.5"], demo="timing",
        check="CHECK(s.Find(\"Watch Values\") && s.Find(\"Watch Values\")->GetComponent<TextRenderer>()->text == \"speed: 12.5\");")
