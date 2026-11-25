#include "OkayScriptVMInternal.hpp"

namespace okay {

void OkayScriptVM::Impl::RegisterInputBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        // key / key_down / key_up and the game actions action / action_down / action_up / action_value are in
        // the shared table (Scripting/ScriptApiCore.cpp). Here: action_x / action_y for Move / Look,
        // rumble(low, high, s), rebind("interact", "key:f").
        auto actName = [](std::vector<Value>& a) { return a.empty() ? std::string() : a[0].AsString(); };
        // A second argument names the controller (0..7) for an action marked per player.
        auto playerOf = [](std::vector<Value>& a) { return a.size() > 1 ? SafeInt(a[1].AsFloat()) : -1; };
        b["action_x"]     = [actName, playerOf](std::vector<Value>& a) { const int p = playerOf(a); return Value{(p < 0 ? InputActions::Vector(actName(a)) : InputActions::Vector(actName(a), p)).x}; };
        b["action_y"]     = [actName, playerOf](std::vector<Value>& a) { const int p = playerOf(a); return Value{(p < 0 ? InputActions::Vector(actName(a)) : InputActions::Vector(actName(a), p)).y}; };
        // Timing: action_buffered("jump", 0.15) is true for a moment after a press until action_consume("jump")
        // uses it; action_double_tap, action_held (seconds held), action_hold("x", 1.5) (0..1 progress).
        b["action_buffered"]   = [actName](std::vector<Value>& a) { return Value{InputActions::Buffered(actName(a), a.size() > 1 ? a[1].AsFloat() : 0.15f)}; };
        b["action_consume"]    = [actName](std::vector<Value>& a) { return Value{InputActions::Consume(actName(a))}; };
        b["action_double_tap"] = [actName](std::vector<Value>& a) { return Value{InputActions::DoubleTap(actName(a), a.size() > 1 ? a[1].AsFloat() : 0.3f)}; };
        b["action_held"]       = [actName](std::vector<Value>& a) { return Value{InputActions::HeldFor(actName(a))}; };
        b["action_hold"]       = [actName](std::vector<Value>& a) { return Value{InputActions::HoldProgress(actName(a), a.size() > 1 ? a[1].AsFloat() : 1.0f)}; };
        b["action_hold_done"]  = [actName](std::vector<Value>& a) { return Value{InputActions::HoldReached(actName(a), a.size() > 1 ? a[1].AsFloat() : 1.0f)}; };
        // Which device the player used last, and the action's button as that device names it
        // ("Cross" on a PlayStation pad, "E" on the keyboard).
        b["input_device"] = [](std::vector<Value>&) {
            switch (Input::LastDevice()) {
                case Input::Device::Gamepad: return Value{std::string("gamepad")};
                case Input::Device::Mouse:   return Value{std::string("mouse")};
                case Input::Device::Touch:   return Value{std::string("touch")};
                default:                     return Value{std::string("keyboard")};
            }
        };
        b["action_label"] = [actName](std::vector<Value>& a) {
            const InputActions::Action* act = InputActions::Find(actName(a));
            if (!act) return Value{std::string()};
            const bool pad = Input::LastDevice() == Input::Device::Gamepad;
            const InputBinding* bnd = InputActions::Current(act->name, pad);
            if (!bnd) bnd = InputActions::Current(act->name, !pad);
            return Value{bnd ? bnd->Label(Input::GamepadFamily(Input::LastGamepad())) : std::string()};
        };
        b["action_conflicts"] = [](std::vector<Value>& a) {
            std::string out;
            if (a.size() >= 2) for (const RemapConflict& c : RemapConflicts(a[0].AsString(), a[1].AsString())) { if (!out.empty()) out += ", "; out += c.action; }
            return Value{out};
        };
        b["rebind_swap"] = [](std::vector<Value>& a) { return Value{a.size() >= 2 && RemapBind(a[0].AsString(), a[1].AsString(), RemapResolve::Swap).ok}; };
        b["reset_bindings"] = [](std::vector<Value>&) { InputActions::Reset(); InputActions::Save(); return Value{}; };
        b["input_preset"] = [](std::vector<Value>& a) { return Value{!a.empty() && InputActions::ApplyPreset(a[0].AsString())}; };
        b["set_response"] = [](std::vector<Value>& a) {   // (action, deadzone, curve, sensitivity [, invertX, invertY])
            if (a.empty()) return Value{false};
            InputActions::Response r = InputActions::GetResponse(a[0].AsString());
            if (a.size() > 1) r.deadzone = a[1].AsFloat();
            if (a.size() > 2) r.curve = a[2].AsFloat();
            if (a.size() > 3) r.sensitivity = a[3].AsFloat();
            if (a.size() > 4) r.invertX = a[4].AsFloat() != 0.0f;
            if (a.size() > 5) r.invertY = a[5].AsFloat() != 0.0f;
            return Value{InputActions::SetResponse(a[0].AsString(), r)};
        };
        b["response_curve"] = [](std::vector<Value>& a) {   // (action, t 0..1) -> shaped value, to draw the curve
            return Value{a.size() < 2 ? 0.0f : InputActions::ShapeValue(a[1].AsFloat(), InputActions::GetResponse(a[0].AsString()))};
        };
        b["mouse_wheel"] = [](std::vector<Value>&) { return Value{Input::MouseWheel()}; };
        // haptic("heartbeat", 0): a rumble pattern (tap, buzz, pulse, heartbeat, ramp, "ramp down") on a controller;
        // haptic_strength(0.5) scales every pattern (0 = vibration off), kept for the player.
        b["haptic"] = [](std::vector<Value>& a) { return Value{!a.empty() && InputHaptics::Play(a[0].AsString(), a.size() > 1 ? SafeInt(a[1].AsFloat()) : 0)}; };
        b["haptic_strength"] = [](std::vector<Value>& a) { if (!a.empty()) InputHaptics::SetStrength(a[0].AsFloat()); return Value{InputHaptics::Strength()}; };
        // key_positions(true): read letters by key position (US places, so AZERTY players keep W A S D under the same fingers); returns the setting.
        b["key_positions"] = [](std::vector<Value>& a) { if (!a.empty()) Input::SetKeysByPosition(a[0].AsBool()); return Value{Input::KeysByPosition()}; };
        // Controllers: who is plugged in and how it is mapped. The slot is 0-based (0 = player 1).
        //   pad_name, pad_family ("DualSense"), pad_ids ("0x054c:0x0ce6"), pad_mapping ("built-in" | "generic" | "user"),
        //   pad_info (several lines), pad_raw (live axes / buttons / hat), pad_battery, pad_button_name("a", slot),
        //   input_button_name("jump") (the action's button as the controller in use names it: "Cross", "A", "ZR"),
        //   pad_event() (the newest connect / disconnect text, once), pad_setup_*, pad_lightbar, pad_swap, pad_touch, pad_gyro.
        auto padSlot = [](std::vector<Value>& a, size_t i = 0) { return a.size() > i ? SafeInt(a[i].AsFloat()) : 0; };
        b["pad_name"]    = [padSlot](std::vector<Value>& a) { return Value{PadProfiles::Get(padSlot(a)).id.name}; };
        b["pad_family"]  = [padSlot](std::vector<Value>& a) { const PadProfile& p = PadProfiles::Get(padSlot(a)); return Value{p.present ? p.info.familyName : std::string()}; };
        b["pad_ids"]     = [padSlot](std::vector<Value>& a) { const PadProfile& p = PadProfiles::Get(padSlot(a)); return Value{p.present ? FormatUsbId(p.id.vendor, p.id.product) : std::string()}; };
        b["pad_mapping"] = [padSlot](std::vector<Value>& a) { const PadProfile& p = PadProfiles::Get(padSlot(a)); return Value{p.present ? std::string(MapSourceName(p.mapSource)) : std::string()}; };
        b["pad_info"]    = [padSlot](std::vector<Value>& a) { return Value{PadProfiles::Describe(padSlot(a))}; };
        b["pad_raw"]     = [padSlot](std::vector<Value>& a) { return Value{PadProfiles::RawText(padSlot(a))}; };
        b["pad_battery"] = [padSlot](std::vector<Value>& a) { return Value{PadProfiles::BatteryText(padSlot(a))}; };
        b["pad_button_name"] = [padSlot](std::vector<Value>& a) {
            const int btn = a.empty() ? -1 : Input::GamepadButtonFromName(a[0].AsString());
            return Value{PadProfiles::ButtonName(padSlot(a, 1), btn)};
        };
        b["input_button_name"] = [actName](std::vector<Value>& a) {
            const InputActions::Action* act = InputActions::Find(actName(a));
            if (!act) return Value{std::string()};
            const bool pad = Input::LastDevice() == Input::Device::Gamepad;
            const InputBinding* bnd = InputActions::Current(act->name, pad);
            if (!bnd) bnd = InputActions::Current(act->name, !pad);
            if (!bnd) return Value{std::string()};
            if (bnd->kind == InputBinding::Kind::PadButton) {
                const int slot = a.size() > 1 ? SafeInt(a[1].AsFloat()) : (bnd->player > 0 ? bnd->player : Input::LastGamepad());
                return Value{PadProfiles::ButtonName(slot, bnd->code) + (bnd->player > 0 ? " (Pad " + std::to_string(bnd->player + 1) + ")" : std::string())};
            }
            return Value{bnd->Label(Input::GamepadFamily(Input::LastGamepad()))};
        };
        b["pad_event"] = [](std::vector<Value>&) { PadEvent e; return Value{PadProfiles::PopEvent(e) ? e.text : std::string()}; };
        // Controller Setup: pad_setup_start(slot) asks for each control in turn; call pad_setup_prompt every frame to show
        // the question; pad_setup_skip / pad_setup_back / pad_setup_cancel steer it; pad_setup_save keeps the result for this model.
        b["pad_setup_start"]  = [padSlot](std::vector<Value>& a) { return Value{PadProfiles::BeginSetup(padSlot(a))}; };
        b["pad_setup_active"] = [padSlot](std::vector<Value>& a) { ControllerSetup* s = PadProfiles::Setup(padSlot(a)); return Value{s && s->Active()}; };
        b["pad_setup_prompt"] = [padSlot](std::vector<Value>& a) {
            ControllerSetup* s = PadProfiles::Setup(padSlot(a));
            if (!s) return Value{std::string()};
            if (s->Finished()) return Value{std::string("Done. All controls set.")};
            if (!s->Active()) return Value{std::string()};
            return Value{"Step " + std::to_string(s->StepIndex() + 1) + " of " + std::to_string(s->StepCount()) + ": " + s->Prompt() +
                         (s->LastCapture().empty() ? std::string() : "   (last: " + s->LastCapture() + ")")};
        };
        b["pad_setup_skip"]   = [padSlot](std::vector<Value>& a) { if (ControllerSetup* s = PadProfiles::Setup(padSlot(a))) s->Skip(); return Value{}; };
        b["pad_setup_back"]   = [padSlot](std::vector<Value>& a) { if (ControllerSetup* s = PadProfiles::Setup(padSlot(a))) s->Back(); return Value{}; };
        b["pad_setup_cancel"] = [padSlot](std::vector<Value>& a) { PadProfiles::CancelSetup(padSlot(a)); return Value{}; };
        b["pad_setup_save"]   = [padSlot](std::vector<Value>& a) { return Value{PadProfiles::SaveSetup(padSlot(a))}; };
        b["pad_setup_clear"]  = [padSlot](std::vector<Value>& a) { PadProfiles::ClearSaved(padSlot(a)); return Value{}; };
        b["pad_lightbar"] = [padSlot](std::vector<Value>& a) {   // (slot, r, g, b) 0..255; only pads whose host exposes a lightbar (DualShock 4 / DualSense)
            PadProfiles::RequestLightbar(padSlot(a), a.size() > 1 ? SafeInt(a[1].AsFloat()) : 0, a.size() > 2 ? SafeInt(a[2].AsFloat()) : 0, a.size() > 3 ? SafeInt(a[3].AsFloat()) : 0);
            return Value{};
        };
        b["pad_trigger_rumble"] = [padSlot](std::vector<Value>& a) {   // (slot, left, right, seconds) Xbox One / Series trigger motors
            PadProfiles::RequestTriggerRumble(padSlot(a), a.size() > 1 ? a[1].AsFloat() : 0.5f, a.size() > 2 ? a[2].AsFloat() : 0.5f, a.size() > 3 ? a[3].AsFloat() : 0.2f);
            return Value{};
        };
        b["pad_swap"] = [padSlot](std::vector<Value>& a) { PadProfiles::RequestSwap(padSlot(a, 0), padSlot(a, 1)); return Value{}; };
        b["pad_touch"] = [padSlot](std::vector<Value>& a) {   // "x,y" of the first finger on the pad's touchpad (0..1), "" when none / not touching
            const PadProfile& p = PadProfiles::Get(padSlot(a));
            if (!p.present || p.touchpads == 0 || !p.finger[0].down) return Value{std::string()};
            char buf[40]; std::snprintf(buf, sizeof buf, "%.2f,%.2f", p.finger[0].x, p.finger[0].y);
            return Value{std::string(buf)};
        };
        b["pad_gyro"] = [padSlot](std::vector<Value>& a) {   // "x,y,z" rad/s, "" when the pad / host has no gyroscope; ask every frame you want it
            const int slot = padSlot(a);
            PadProfiles::Edit(slot).sensorsWanted = true;
            const PadProfile& p = PadProfiles::Get(slot);
            if (!p.present || !p.hostGyro) return Value{std::string()};
            char buf[64]; std::snprintf(buf, sizeof buf, "%.2f,%.2f,%.2f", p.gyro[0], p.gyro[1], p.gyro[2]);
            return Value{std::string(buf)};
        };
        // Typed actions and local players (the .okayinput system): ia_use_default() / ia_load("Assets/my.okayinput"), ia_join("keyboard" |
        // "pad0".."pad7" | "any"), then read by name: ia_x / ia_y / ia_z / ia_value, ia_down, ia_pressed, ia_released, and the phases
        // ia_started / ia_performed / ia_canceled, ia_progress ("hold to confirm" 0..1), ia_phase, ia_event (one "Jump:performed" per call).
        // The last argument of each is the player (0 = first, default). ia_map("UI", true) switches an action map, ia_scheme() names
        // the active control scheme, ia_rebind(action, binding, part, "key:j") changes one input (ia_conflicts lists clashes), ia_save() keeps it.
        auto iaPlayer = [](std::vector<Value>& a, size_t i) { return a.size() > i ? SafeInt(a[i].AsFloat()) : 0; };
        auto iaName = [](std::vector<Value>& a) { return a.empty() ? std::string() : a[0].AsString(); };
        b["ia_use_default"] = [](std::vector<Value>&) { InputRuntime::UseDefault(); return Value{}; };
        b["ia_load"] = [](std::vector<Value>& a) { return Value{!a.empty() && InputRuntime::LoadAsset(a[0].AsString())}; };
        b["ia_join"] = [](std::vector<Value>& a) { return Value{(float)InputRuntime::JoinNamed(a.empty() ? std::string("any") : a[0].AsString())}; };
        b["ia_poll_join"] = [](std::vector<Value>&) { InputRuntime::Set(); return Value{(float)InputRuntime::Players().PollJoin()}; };
        b["ia_leave"] = [iaPlayer](std::vector<Value>& a) { InputRuntime::Players().Leave(iaPlayer(a, 0)); return Value{}; };
        auto iaGo = [this]() -> GameObject* { return rt.host ? rt.host->gameObject : nullptr; };
        b["ia_my_player"] = [iaGo](std::vector<Value>&) {   // the player index of a Player Input component on this object, -1 when there is none
            GameObject* g = iaGo();
            auto* lp = g ? g->GetComponent<LocalPlayerInput>() : nullptr;
            return Value{(float)(lp ? lp->GetPlayerIndex() : -1)};
        };
        b["ia_players"] = [](std::vector<Value>&) { int n = 0; for (int i = 0; i < InputRuntime::Players().Count(); ++i) n += InputRuntime::Players().Active(i); return Value{(float)n}; };
        auto iaVal = [iaName, iaPlayer](std::vector<Value>& a) { ActionSet* s = InputRuntime::Set(iaPlayer(a, 1)); return s ? s->Value(iaName(a)) : InputValue{}; };
        b["ia_value"] = [iaVal](std::vector<Value>& a) { return Value{iaVal(a).x}; };
        b["ia_x"] = [iaVal](std::vector<Value>& a) { return Value{iaVal(a).x}; };
        b["ia_y"] = [iaVal](std::vector<Value>& a) { return Value{iaVal(a).y}; };
        b["ia_z"] = [iaVal](std::vector<Value>& a) { return Value{iaVal(a).z}; };
        auto iaFlag = [iaName, iaPlayer](bool (ActionSet::*fn)(const std::string&) const) {
            return [=](std::vector<Value>& a) { ActionSet* s = InputRuntime::Set(iaPlayer(a, 1)); return Value{s && (s->*fn)(iaName(a))}; };
        };
        b["ia_down"] = iaFlag(&ActionSet::Held);
        b["ia_pressed"] = iaFlag(&ActionSet::Pressed);
        b["ia_released"] = iaFlag(&ActionSet::Released);
        b["ia_started"] = iaFlag(&ActionSet::Started);
        b["ia_performed"] = iaFlag(&ActionSet::Performed);
        b["ia_canceled"] = iaFlag(&ActionSet::Canceled);
        b["ia_progress"] = [iaName, iaPlayer](std::vector<Value>& a) { ActionSet* s = InputRuntime::Set(iaPlayer(a, 1)); return Value{s ? s->Progress(iaName(a)) : 0.0f}; };
        b["ia_phase"] = [iaName, iaPlayer](std::vector<Value>& a) { ActionSet* s = InputRuntime::Set(iaPlayer(a, 1)); return Value{std::string(PhaseName(s ? s->PhaseOf(iaName(a)) : Phase::Waiting))}; };
        b["ia_event"] = [iaPlayer](std::vector<Value>& a) { InputRuntime::Set(iaPlayer(a, 0)); std::string e; return Value{InputRuntime::PopEvent(iaPlayer(a, 0), e) ? e : std::string()}; };
        b["ia_map"] = [iaName, iaPlayer](std::vector<Value>& a) { ActionSet* s = InputRuntime::Set(iaPlayer(a, 2)); return Value{s && a.size() > 1 && s->SetMapEnabled(iaName(a), a[1].AsFloat() != 0.0f)}; };
        b["ia_map_enabled"] = [iaName, iaPlayer](std::vector<Value>& a) { ActionSet* s = InputRuntime::Set(iaPlayer(a, 1)); return Value{s && s->MapEnabled(iaName(a))}; };
        b["ia_scheme"] = [iaPlayer](std::vector<Value>& a) { ActionSet* s = InputRuntime::Set(iaPlayer(a, 0)); return Value{s ? s->Scheme() : std::string()}; };
        b["ia_set_scheme"] = [iaName, iaPlayer](std::vector<Value>& a) { ActionSet* s = InputRuntime::Set(iaPlayer(a, 1)); return Value{s && s->SetScheme(iaName(a))}; };
        b["ia_rebind"] = [iaPlayer](std::vector<Value>& a) {
            InputRuntime::Set(iaPlayer(a, 4));   // as before: the first read joins a player even when the call is short of arguments
            return Value{a.size() > 3 && InputRuntime::Rebind(iaPlayer(a, 4), a[0].AsString(), SafeInt(a[1].AsFloat()), SafeInt(a[2].AsFloat()), a[3].AsString())};
        };
        b["ia_conflicts"] = [iaPlayer](std::vector<Value>& a) {
            ActionSet* s = InputRuntime::Set(iaPlayer(a, 4));
            std::string out;
            if (s && a.size() > 3) for (const std::string& c : s->Conflicts(a[0].AsString(), SafeInt(a[1].AsFloat()), SafeInt(a[2].AsFloat()), a[3].AsString())) { if (!out.empty()) out += ", "; out += c; }
            return Value{out};
        };
        b["ia_reset_bindings"] = [iaPlayer](std::vector<Value>& a) { InputRuntime::ResetBinding(iaPlayer(a, 0)); return Value{}; };
        b["ia_save"] = [iaPlayer](std::vector<Value>& a) { InputRuntime::SaveOverrides(iaPlayer(a, 0)); return Value{}; };
        b["ia_label"] = [iaName, iaPlayer](std::vector<Value>& a) {   // the first active binding of the action as the player reads it ("Cross", "Space", "W A S D")
            ActionSet* s = InputRuntime::Set(iaPlayer(a, 1));
            const ActionDef* d = s ? s->Find(iaName(a)) : nullptr;
            if (!d) return Value{std::string()};
            for (const BindingSpec& bs : d->bindings) {
                if (!s->BindingActive(bs) || bs.parts.empty()) continue;
                std::string out;
                for (std::size_t i = 0; i < bs.parts.size() && i < 6; ++i) { const InputBinding ib = InputBinding::Parse(bs.parts[i]); out += (i ? (bs.kind == BindingSpec::Kind::Chord ? " + " : " ") : "") + PadProfiles::BindingLabel(ib); if (bs.kind == BindingSpec::Kind::Single) break; }
                return Value{out};
            }
            return Value{std::string()};
        };
        b["gamepad_count"] = [](std::vector<Value>&) { return Value{(float)Input::GamepadCount()}; };
        b["gamepad_connected"] = [](std::vector<Value>& a) { return Value{Input::GamepadConnected(a.empty() ? 0 : SafeInt(a[0].AsFloat()))}; };
        b["set_per_player"] = [](std::vector<Value>& a) { if (!a.empty()) InputActions::SetPerPlayer(a[0].AsString(), a.size() < 2 || a[1].AsFloat() != 0.0f); return Value{}; };
        // Input recording: input_record("start") ... input_record("stop"), then input_record("play")
        // replays it. "status" says what it is doing. The recording is kept as text in the variable
        // returned by input_record("text") (a bug report can paste it).
        b["input_record"] = [](std::vector<Value>& a) {
            const std::string cmd = a.empty() ? std::string("status") : a[0].AsString();
            if (cmd == "start") { InputRecorder::StartRecording(); return Value{std::string("recording")}; }
            if (cmd == "stop") { InputRecorder::StopRecording(); InputRecorder::StopPlayback(); return Value{std::to_string(InputRecorder::FrameCount()) + " frames"}; }
            if (cmd == "play") {
                const std::string text = InputRecorder::Text();
                return Value{InputRecorder::StartPlayback(text) ? std::string("playing") : std::string("nothing recorded")};
            }
            if (cmd == "text") return Value{InputRecorder::Text()};
            if (InputRecorder::Recording()) return Value{"recording, " + std::to_string(InputRecorder::FrameCount()) + " frames"};
            if (InputRecorder::Playing()) return Value{"playing, frame " + std::to_string(InputRecorder::PlaybackFrame()) + " of " + std::to_string(InputRecorder::FrameCount())};
            return Value{InputRecorder::FrameCount() ? std::to_string(InputRecorder::FrameCount()) + " frames kept" : std::string("idle")};
        };
        // Touch gestures. gesture("tap"), gesture("double tap"), gesture("long press"),
        // gesture("swipe"), gesture("swipe up"|"down"|"left"|"right"), gesture("pinch"),
        // gesture("rotate"). True on the frame it happens.
        b["gesture"] = [](std::vector<Value>& a) {
            return Value{Gestures::Happened(a.empty() ? std::string() : a[0].AsString())};
        };
        b["swipe_direction"] = [](std::vector<Value>&) { return Value{Gestures::SwipeDirection()}; };
        b["pinch_scale"] = [](std::vector<Value>&) { return Value{Gestures::PinchScale()}; };
        b["twist_degrees"] = [](std::vector<Value>&) { return Value{Gestures::TwistDegrees()}; };
        b["is_touch_screen"] = [](std::vector<Value>&) { return Value{Input::TouchScreen()}; };
        b["rumble"] = [](std::vector<Value>& a) {
            Input::Rumble(a.size() > 0 ? a[0].AsFloat() : 0.5f, a.size() > 1 ? a[1].AsFloat() : 0.5f, a.size() > 2 ? a[2].AsFloat() : 0.2f);
            return Value{};
        };
        // define_action("dash", "key:q", "pad:rs") adds an action (or changes its defaults);
        // define_direction("steer", "keys:i,k,j,l", "stick:right") a direction one.
        auto define = [](std::vector<Value>& a, bool dir) {
            if (a.empty()) return Value{false};
            std::vector<std::string> binds;
            for (std::size_t i = 1; i < a.size(); ++i) binds.push_back(a[i].AsString());
            InputActions::Define(a[0].AsString(), binds, dir);
            return Value{true};
        };
        b["define_action"]    = [define](std::vector<Value>& a) { return define(a, false); };
        b["define_direction"] = [define](std::vector<Value>& a) { return define(a, true); };
        b["reset_controls"] = [](std::vector<Value>&) { InputActions::Reset(); return Value{}; };   // every action back to its defaults (the Controls screen's Reset button)
        b["rebind"] = [](std::vector<Value>& a) {
            return Value{a.size() >= 2 && InputActions::Rebind(a[0].AsString(), a[1].AsString())};
        };
        b["mouse_x"] = [](std::vector<Value>&) { return Value{Input::MousePosition().x}; };
        b["mouse_y"] = [](std::vector<Value>&) { return Value{Input::MousePosition().y}; };
        b["mouse"] = [](std::vector<Value>& a) {
            return Value{Input::GetMouseButton(a.empty() ? 0 : SafeInt(a[0].AsFloat()))};
        };
        b["mouse_held"] = [](std::vector<Value>& a) {
            return Value{Input::GetMouseButton(a.empty() ? 0 : SafeInt(a[0].AsFloat()))};
        };
        b["move_towards"] = [](std::vector<Value>& a) {   // (current, target, maxDelta)
            float c = a.size() > 0 ? a[0].AsFloat() : 0.0f, t = a.size() > 1 ? a[1].AsFloat() : 0.0f, m = a.size() > 2 ? a[2].AsFloat() : 0.0f;
            return Value{std::fabs(t - c) <= m ? t : c + (t > c ? m : -m)};
        };
        b["dot"] = [](std::vector<Value>& a) {
            if (a.size() < 2) return Value{0.0f};
            return Value{Vec3::Dot(a[0].AsVec3(), a[1].AsVec3())};
        };
        b["to_fixed"] = [](std::vector<Value>& a) {   // (value, decimals) -> text
            int d = a.size() > 1 ? SafeInt(a[1].AsFloat()) : 2; d = d < 0 ? 0 : (d > 6 ? 6 : d);
            char buf[64]; std::snprintf(buf, sizeof(buf), "%.*f", d, a.empty() ? 0.0f : a[0].AsFloat());
            return Value{std::string(buf)};
        };
        b["mouse_down"] = [](std::vector<Value>& a) {
            return Value{Input::GetMouseButtonDown(a.empty() ? 0 : SafeInt(a[0].AsFloat()))};
        };
        b["gamepad_x"] = [](std::vector<Value>&) { return Value{Input::GamepadAxis().x}; };
        b["gamepad_y"] = [](std::vector<Value>&) { return Value{Input::GamepadAxis().y}; };
        b["gamepad"] = [](std::vector<Value>& a) {
            return Value{Input::GetGamepadButton(a.empty() ? 0 : SafeInt(a[0].AsFloat()))};
        };
        b["gamepad_down"] = [](std::vector<Value>& a) {
            return Value{Input::GetGamepadButtonDown(a.empty() ? 0 : SafeInt(a[0].AsFloat()))};
        };
        // rand, randi, chance, choose, shuffle and rand_seed: the language core's (CoreBuiltins.hpp), drawing from
        // the engine's shared generator.
        okayscript::InstallRandomBuiltins<Value>(b, okayscript::RandomSource{
            [](float lo, float hi) { return Random::Shared().Range(lo, hi); },
            [](int lo, int hi) { return Random::Shared().Range(lo, hi); },
            [](std::uint64_t s) { Random::Shared().Seed(s); }});
        b["dist"] = [](std::vector<Value>& a) {
            if (a.size() < 4) return Value{0.0f};
            float dx = a[2].AsFloat() - a[0].AsFloat(), dy = a[3].AsFloat() - a[1].AsFloat();
            return Value{Mathf::Sqrt(dx * dx + dy * dy)};
        };
        b["get"] = [this](std::vector<Value>& a) {
            if (a.empty() || !rt.host) return Value{};
            auto it = rt.host->globals.find(a[0].AsString());
            return it != rt.host->globals.end() ? it->second : Value{};
        };
        b["set"] = [this](std::vector<Value>& a) {
            if (a.size() >= 2 && rt.host) rt.host->globals[a[0].AsString()] = a[1];
            return Value{};
        };
}

} // namespace okay
