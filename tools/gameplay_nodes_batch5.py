"""Gameplay library, batch 5: readings that pair with the settings from batches 1 to 4.
Called by gen_gameplay_library.py."""


def define(add, N, B, T, O, R):
    def getter(typ, title, topic, sub, level, help_, out, ctype, fn, expr, fallback, setup, expect, etype="f", question=False):
        add(typ, title, topic, sub, level, help_, [O()], [(out, "B" if question else "N", None)],
            f"{ctype}* c = {fn}(this, o, g, ctx); out = VsValue(c ? ({expr}) : {fallback});",
            reg="CAT_LOGIC" if question else "CAT_DATA", setup=setup, expect=expect, etype=etype)

    H = ("Objects", "Survival")
    hp = "me->AddComponent<HealthStat>(); "
    hs = "me->GetComponent<HealthStat>()->"
    getter("GlGetLives", "Get Lives", *H, "Intermediate", "How many times this object may come back after dying. 0 means as many as it likes.", "Lives", "HealthStat", "GpHealth",
           "(float)c->lives", "0.0f", hp + hs + "lives = 4;", 4.0)
    getter("GlIsRespawnOn", "Does Respawn", *H, "Intermediate", "True when this object comes back after dying.", "Result", "HealthStat", "GpHealth",
           "c->respawn", "false", hp + hs + "respawn = true;", True, "b", True)
    getter("GlGetOverhealLimit", "Get Overheal Limit", *H, "Advanced", "How far above the maximum healing may push health.", "Extra", "HealthStat", "GpHealth",
           "c->overhealMax", "0.0f", hp + hs + "overhealMax = 30;", 30.0)
    getter("GlGetHitProtectionTime", "Get Hit Protection Time", *H, "Intermediate", "How many seconds damage is ignored after each hit.", "Seconds", "HealthStat", "GpHealth",
           "c->invincibleTime", "0.0f", hp + hs + "invincibleTime = 1.5f;", 1.5)
    P = ("Physics", "")
    rb = "me->AddComponent<Rigidbody3D>(); "
    rs = "me->GetComponent<Rigidbody3D>()->"
    getter("GlGetGravityStrength", "Get Gravity Strength", *P, "Basic", "How much gravity pulls on the Rigidbody: 1 is normal.", "Strength", "Rigidbody3D", "GlBody",
           "c->gravityScale", "1.0f", rb + rs + "gravityScale = 0.4f;", 0.4)
    getter("GlGetBounciness", "Get Bounciness", *P, "Intermediate", "How much the Rigidbody bounces back: 0 stops dead, 1 keeps all its speed.", "Bounce", "Rigidbody3D", "GlBody",
           "c->bounciness", "0.0f", rb + rs + "bounciness = 0.6f;", 0.6)
    getter("GlGetAirDrag", "Get Air Drag", *P, "Intermediate", "How quickly the Rigidbody loses speed on its own.", "Drag", "Rigidbody3D", "GlBody",
           "c->drag", "0.0f", rb + rs + "drag = 1.25f;", 1.25)
    getter("GlGetSurfaceFriction", "Get Surface Friction", *P, "Intermediate", "How much the Rigidbody grips what it slides on.", "Friction", "Rigidbody3D", "GlBody",
           "c->friction", "0.0f", rb + rs + "friction = 0.7f;", 0.7)
    getter("GlIsRotationLocked", "Is Rotation Locked", *P, "Intermediate", "True when the Rigidbody cannot tip over or spin.", "Result", "Rigidbody3D", "GlBody",
           "c->freezeRotation", "false", rb + rs + "freezeRotation = true;", True, "b", True)
    getter("GlIsSoundPositional", "Is Sound Positional", "Audio", "", "Advanced", "True when the sound gets quieter the farther away it is.", "Result", "AudioSource", "GlAudio",
           "c->spatial", "false", "me->AddComponent<AudioSource>()->spatial = true;", True, "b", True)
    getter("GlGetCameraTilt", "Get Camera Tilt", "Camera", "", "Advanced", "How far the camera is rolled sideways, in degrees.", "Degrees", "Camera", "GlCamera",
           "c->dutch", "0.0f", "me->AddComponent<Camera>()->dutch = 15.0f;", 15.0)
    getter("GlGetTextOpacity", "Get Text Opacity", "UI", "", "Basic", "How see-through the text is: 1 solid, 0 gone.", "Opacity", "TextRenderer", "GlText",
           "c->color.a", "1.0f", "me->AddComponent<TextRenderer>()->color.a = 0.6f;", 0.6)
    getter("GlGetTextSize", "Get Text Size", "UI", "", "Intermediate", "How big the text is drawn.", "Size", "TextRenderer", "GlText",
           "c->pixelSize", "1.0f", "me->AddComponent<TextRenderer>()->pixelSize = 2.5f;", 2.5)
    I = ("Objects", "Items")
    getter("GlGetStackLimit", "Get Stack Limit", *I, "Intermediate", "The most of one item a single slot can hold. 0 means the item's own limit.", "Count", "Inventory", "GlInventory",
           "(float)c->maxStack", "0.0f", "me->AddComponent<Inventory>()->maxStack = 9;", 9.0)
    getter("GlGetInventorySize", "Get Inventory Size", *I, "Basic", "How many slots the inventory has.", "Slots", "Inventory", "GlInventory",
           "(float)c->capacity", "0.0f", "me->AddComponent<Inventory>()->capacity = 12;", 12.0)
