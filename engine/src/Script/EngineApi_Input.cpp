// Typed script API, Input: the Bind<Topic> functions of the binding table (see EngineApiInternal.hpp).
#include "EngineApiInternal.hpp"

namespace okay::script::engine_api {

void Ctx::BindInput() {
    // ---- Input ----
    {
        ApiType in; in.name = "Input"; in.isStatic = true; in.doc = "Keyboard, mouse and gamepad.";
        // GetKey / GetKeyDown / GetKeyUp, GetButton / GetButtonDown / GetButtonUp and GetActionValue come from the
        // shared table (Scripting/ScriptApiCore.cpp).
        ApiMethod axis = method("GetAxis", {tString}, {"axis"}, tFloat,
            [](const Value&, std::vector<Value>& a, CallContext& c) {
                const std::string& n = S(a[0]);
                if (n == "Horizontal") return Value{Axes().x};
                if (n == "Vertical") return Value{Axes().y};
                throw RuntimeError("\"" + n + "\" isn't an axis. Use \"Horizontal\" or \"Vertical\"", c.loc);
            }, "\"Horizontal\" (A/D, left/right, stick) or \"Vertical\" (W/S, up/down, stick): -1 to 1.");
        axis.checkLiteral = CheckAxisLiteral;
        in.staticMethods.push_back(axis);
        auto mouse = [&](const char* n, bool (*fn)(int), const char* doc) {
            in.staticMethods.push_back(method(n, {tInt}, {"button"}, tBool,
                [fn](const Value&, std::vector<Value>& a, CallContext& c) {
                    int b = I(a[0]);
                    if (b < 0 || b > 2) throw RuntimeError("Mouse buttons are 0 (left), 1 (right) and 2 (middle)", c.loc);
                    return Value{fn(b)};
                }, doc));
        };
        mouse("GetMouseButton", &Input::GetMouseButton, "True while the mouse button is held (0 left, 1 right, 2 middle).");
        mouse("GetMouseButtonDown", &Input::GetMouseButtonDown, "True on the frame the mouse button is pressed.");
        mouse("GetMouseButtonUp", &Input::GetMouseButtonUp, "True on the frame the mouse button is released.");
        in.staticProps.push_back(ro("mousePosition", tVec,
            [](const Value&, CallContext&) { Vec2 m = Input::MousePosition(); return Value{Vec3{m.x, m.y, 0.0f}}; },
            "The cursor in screen pixels (x, y; z is 0)."));
        // Game actions (InputActions): what the player pressed, on any device, after
        // their own rebinding. Prefer these over raw keys.
        auto actionOf = [](const std::string& n, CallContext& c) {
            if (!InputActions::Has(n))
                throw RuntimeError("There's no input action called \"" + n + "\" (Move, Look, Jump, Sprint, Crouch, Fire, Aim, Interact, Reload, Inventory, Pause, or one the game defined)", c.loc);
            return n;
        };
        in.staticMethods.push_back(method("GetActionVector", {tString}, {"action"}, tVec,
            [actionOf](const Value&, std::vector<Value>& a, CallContext& c) {
                const Vec2 v = InputActions::Vector(actionOf(S(a[0]), c));
                return Value{Vec3{v.x, v.y, 0.0f}};
            }, "A direction action (\"Move\", \"Look\") as x, y in -1..1 (y up; z is 0)."));
        in.staticMethods.push_back(method("Rumble", {tFloat, tFloat, tFloat}, {"low", "high", "seconds"}, tVoid,
            [](const Value&, std::vector<Value>& a, CallContext&) { Input::Rumble(F(a[0]), F(a[1]), F(a[2])); return Value{}; },
            "Shake the controller: low (heavy) and high (light) motor strength 0 to 1, for some seconds."));
        in.staticMethods.push_back(method("RebindAction", {tString, tString}, {"action", "binding"}, tBool,
            [actionOf](const Value&, std::vector<Value>& a, CallContext& c) { return Value{InputActions::Rebind(actionOf(S(a[0]), c), S(a[1]))}; },
            "Bind an action to \"key:e\", \"mouse:right\", \"pad:rt\"... for this player (saved). False if the binding isn't understood."));
        in.staticMethods.push_back(method("DefineAction", {tString, tString, tString}, {"action", "binding", "binding2"}, tVoid,
            [](const Value&, std::vector<Value>& a, CallContext&) {
                std::vector<std::string> b;
                for (int i = 1; i < 3; ++i) if (!S(a[i]).empty()) b.push_back(S(a[i]));
                InputActions::Define(S(a[0]), b, false);
                return Value{};
            }, "Add a game action (or change its defaults), e.g. (\"Dash\", \"key:q\", \"pad:rs\"). Use \"\" for an unused binding."));
        in.staticMethods.push_back(method("DefineDirection", {tString, tString, tString}, {"action", "binding", "binding2"}, tVoid,
            [](const Value&, std::vector<Value>& a, CallContext&) {
                std::vector<std::string> b;
                for (int i = 1; i < 3; ++i) if (!S(a[i]).empty()) b.push_back(S(a[i]));
                InputActions::Define(S(a[0]), b, true);
                return Value{};
            }, "Add a direction action, e.g. (\"Steer\", \"keys:i,k,j,l\", \"stick:right\")."));
        // ---- Local players and typed actions (the .okayinput system): the player is the index a Player Input component reports (0 = first).
        auto setOf = [](const std::vector<Value>& a, std::size_t i) { return InputRuntime::Set(a.size() > i ? I(a[i]) : 0); };
        in.staticMethods.push_back(method("PlayerActionValue", {tString, tInt}, {"action", "player"}, tVec,
            [setOf](const Value&, std::vector<Value>& a, CallContext&) {
                ActionSet* s = setOf(a, 1);
                const InputValue v = s ? s->Value(S(a[0])) : InputValue{};
                return Value{Vec3{v.x, v.y, v.z}};
            }, "A local player's action as x, y, z: a button is x 0 to 1, an axis is x, a 2D vector is x and y (y up), a 3D vector uses z too."));
        auto flag = [&](const char* n, bool (ActionSet::*fn)(const std::string&) const, const char* doc) {
            in.staticMethods.push_back(method(n, {tString, tInt}, {"action", "player"}, tBool,
                [setOf, fn](const Value&, std::vector<Value>& a, CallContext&) { ActionSet* s = setOf(a, 1); return Value{s && (s->*fn)(S(a[0]))}; }, doc));
        };
        flag("PlayerActionHeld", &ActionSet::Held, "True while a local player's action is held (past its press point).");
        flag("PlayerActionPressed", &ActionSet::Pressed, "True on the frame a local player's action is pressed.");
        flag("PlayerActionReleased", &ActionSet::Released, "True on the frame a local player's action is released.");
        flag("PlayerActionStarted", &ActionSet::Started, "True on the frame a local player's action starts (the first press of a hold, tap or multi-tap).");
        flag("PlayerActionPerformed", &ActionSet::Performed, "True on the frame a local player's action is performed (its hold, tap, multi-tap or repeat completes).");
        flag("PlayerActionCanceled", &ActionSet::Canceled, "True on the frame a local player's action is canceled (let go too early, too slow).");
        in.staticMethods.push_back(method("PlayerActionProgress", {tString, tInt}, {"action", "player"}, tFloat,
            [setOf](const Value&, std::vector<Value>& a, CallContext&) { ActionSet* s = setOf(a, 1); return Value{s ? s->Progress(S(a[0])) : 0.0f}; },
            "How far through a hold the action is, 0 to 1 (a hold-to-confirm bar)."));
        in.staticMethods.push_back(method("PlayerActionPhase", {tString, tInt}, {"action", "player"}, tString,
            [setOf](const Value&, std::vector<Value>& a, CallContext&) { ActionSet* s = setOf(a, 1); return Value{std::string(PhaseName(s ? s->PhaseOf(S(a[0])) : Phase::Waiting))}; },
            "\"started\", \"performed\", \"canceled\" or \"waiting\": where a local player's action is."));
        in.staticMethods.push_back(method("SetActionMapEnabled", {tString, tBool, tInt}, {"map", "enabled", "player"}, tBool,
            [setOf](const Value&, std::vector<Value>& a, CallContext&) { ActionSet* s = setOf(a, 2); return Value{s && s->SetMapEnabled(S(a[0]), std::get_if<bool>(&a[1]) ? *std::get_if<bool>(&a[1]) : F(a[1]) != 0.0f)}; },
            "Switch a local player's action map on or off (Gameplay, UI ...). False for an unknown map."));
        in.staticMethods.push_back(method("PlayerRebind", {tString, tInt, tInt, tString, tInt}, {"action", "binding", "part", "input", "player"}, tBool,
            [setOf](const Value&, std::vector<Value>& a, CallContext&) { ActionSet* s = setOf(a, 4); return Value{s && InputRuntime::Rebind(a.size() > 4 ? I(a[4]) : 0, S(a[0]), I(a[1]), I(a[2]), S(a[3]))}; },   // player 0's reaches InputActions too
            "Change one input of a local player's action (binding and part count from 0), e.g. (\"Jump\", 0, 0, \"key:j\", 0). False if it is not understood."));
        in.staticMethods.push_back(method("MyPlayer", {}, {}, tInt,
            [](const Value&, std::vector<Value>&, CallContext& c) {
                auto* lp = c.self ? c.self->GetComponent<LocalPlayerInput>() : nullptr;
                return Value{(std::int32_t)(lp ? lp->GetPlayerIndex() : -1)};
            }, "The local player index of the Player Input component on this object (use it as the player of the Player calls), or -1 when there is none or the game is not playing."));
        in.staticMethods.push_back(method("PlayerScheme", {tInt}, {"player"}, tString,
            [setOf](const Value&, std::vector<Value>& a, CallContext&) { ActionSet* s = setOf(a, 0); return Value{s ? s->Scheme() : std::string()}; },
            "The control scheme a local player is in now (Keyboard+Mouse, Gamepad ...); it follows the device used last when the player uses any device."));
        in.staticProps.push_back(ro("gamepadConnected", tBool,
            [](const Value&, CallContext&) { return Value{Input::GamepadConnected()}; }, "True while a controller is plugged in."));
        in.staticMethods.push_back(method("Gesture", {tString}, {"name"}, tBool,
            [](const Value&, std::vector<Value>& a, CallContext&) {
                return Value{Gestures::Happened(S(a[0]))};
            }, "True on the frame a touch gesture happens. name is \"tap\", \"double tap\", \"long press\", \"swipe\", \"swipe up\", \"swipe down\", \"swipe left\", \"swipe right\", \"pinch\" or \"rotate\"."));
        in.staticProps.push_back(ro("swipeDirection", tString,
            [](const Value&, CallContext&) { return Value{Gestures::SwipeDirection()}; },
            "\"up\", \"down\", \"left\" or \"right\" when a swipe happened this frame, otherwise empty."));
        in.staticProps.push_back(ro("pinchScale", tFloat,
            [](const Value&, CallContext&) { return Value{Gestures::PinchScale()}; },
            "How far apart two fingers are compared with where the pinch began. 1 means unchanged. Greater than 1 means they moved apart."));
        in.staticProps.push_back(ro("twistDegrees", tFloat,
            [](const Value&, CallContext&) { return Value{Gestures::TwistDegrees()}; },
            "How far two fingers have twisted, in degrees. Positive is counter-clockwise."));
        in.staticProps.push_back(ro("touchScreen", tBool,
            [](const Value&, CallContext&) { return Value{Input::TouchScreen()}; },
            "True once a touch screen has been used or reported."));
        api.types.push_back(std::move(in));
    }
}

} // namespace okay::script::engine_api
