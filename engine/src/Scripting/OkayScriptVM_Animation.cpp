#include "OkayScriptVMInternal.hpp"

namespace okay {

void OkayScriptVM::Impl::RegisterCharacterAnimBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        auto selfObj = c.selfObj;
        auto charSelf = c.charSelf;
        auto modelSelf = c.modelSelf;
        // ---- Multiplayer (NetworkManager) -----------------------------
        // Start hosting / join from a script; both wire the host object as the
        // broadcast avatar and mirror remote peers as sprites.
        // ---- Character / imported-model animation --------------------------
        // The Character and the model's ModelAnimator are found the same way as in
        // visual scripts (AnimControl.hpp): this object, then its children (a
        // player whose model sits under it), then its parents (a script on a part).
        // play_clip holds the clip (locomotion and the humanoid retarget wait) until stop_clip.
        auto warnedNoAnimator = std::make_shared<bool>(false);
        b["play_clip"] = [selfObj, warnedNoAnimator](std::vector<Value>& a) {
            if (a.empty()) return Value{0.0f};
            return Value{PlayClipFrom(selfObj(), a[0].AsString(), *warnedNoAnimator) ? 1.0f : 0.0f};
        };
        b["stop_clip"] = [selfObj](std::vector<Value>&) { StopClipFrom(selfObj()); return Value{}; };
        b["playing_clip"] = [charSelf, modelSelf](std::vector<Value>&) {
            Character* c = charSelf();
            if (c) return Value{c->PlayingClip()};
            ModelAnimator* m = modelSelf();
            return Value{m ? m->CurrentName() : std::string{}};
        };
        b["is_playing_clip"] = [charSelf](std::vector<Value>&) {
            Character* c = charSelf(); return Value{(c && c->IsPlayingClip()) ? 1.0f : 0.0f};
        };
        b["load_clips"] = [charSelf](std::vector<Value>& a) {
            Character* c = charSelf();
            return Value{(c && !a.empty()) ? (float)c->LoadClips(a[0].AsString()) : 0.0f};
        };
        // Partial-body layer: play_layer("wave", "arms"|"upper"|<bone>); stop_layer().
        b["play_layer"] = [charSelf](std::vector<Value>& a) {
            Character* c = charSelf();
            if (!c || a.empty()) return Value{0.0f};
            std::string m = a.size() > 1 ? a[1].AsString() : "arms";
            std::uint32_t mask = (m == "upper" || m == "upper_body") ? Character::UpperBodyMask()
                               : (m == "arms" || m.empty())          ? Character::ArmsMask()
                               : Character::BoneBit(Character::BoneIndex(m));
            if (mask == 0) mask = Character::ArmsMask();
            return Value{c->PlayLayer(a[0].AsString(), mask) ? 1.0f : 0.0f};
        };
        b["stop_layer"] = [charSelf](std::vector<Value>&) { if (Character* c = charSelf()) c->StopLayer(); return Value{}; };
        // 1D blend tree: blend_tree("idle",0, "walk",2, "run",5); blend_param(speed).
        b["blend_tree"] = [charSelf](std::vector<Value>& a) {
            Character* c = charSelf();
            if (!c) return Value{0.0f};
            std::vector<Character::BlendStop> stops;
            for (std::size_t i = 0; i + 1 < a.size(); i += 2)
                stops.push_back({a[i + 1].AsFloat(), a[i].AsString()});
            c->SetBlendTree(stops);
            return Value{(float)stops.size()};
        };
        b["blend_param"] = [charSelf](std::vector<Value>& a) {
            if (Character* c = charSelf(); c && !a.empty()) c->SetBlendParam(a[0].AsFloat());
            return Value{};
        };
        b["clear_blend_tree"] = [charSelf](std::vector<Value>&) { if (Character* c = charSelf()) c->ClearBlendTree(); return Value{}; };
        b["clip_speed"] = [selfObj](std::vector<Value>& a) {
            if (!a.empty()) SetClipSpeedFrom(selfObj(), a[0].AsFloat());
            return Value{};
        };
        // Set/get the built-in animation index (0 none,1 idle,2 walk,3 run,...).
        b["set_anim"] = [charSelf](std::vector<Value>& a) {
            if (Character* c = charSelf(); c && !a.empty()) c->anim = SafeInt(a[0].AsFloat());
            return Value{};
        };
        b["get_anim"] = [charSelf](std::vector<Value>&) {
            Character* c = charSelf(); return Value{c ? (float)c->anim : 0.0f};
        };
        // grok/view-toggle: set_view(0 first / 1 third) eases the camera; get_view()
        // is the view asked for (0 / 1), or -1 when this object has no player view.
        b["set_view"] = [selfObj](std::vector<Value>& a) {
            GameObject* g = selfObj();
            PlayerView* v = g ? PlayerView::FindForScript(g, true) : nullptr;
            if (!v) return Value{0.0f};
            v->SetView(a.empty() ? 1 : SafeInt(a[0].AsFloat()));
            return Value{1.0f};
        };
        b["get_view"] = [selfObj](std::vector<Value>&) {
            GameObject* g = selfObj();
            if (PlayerController* pc = PlayerController::Of(g)) return Value{(float)pc->CurrentView()};
            PlayerView* v = g ? PlayerView::FindForScript(g, false) : nullptr;
            return Value{v ? (float)v->GetView() : -1.0f};
        };
        b["SetView"] = b["set_view"];
        b["GetView"] = b["get_view"];
        // grok/ui-theme: apply_ui_theme() bakes the theme on this object (or the
        // first in the scene). apply_ui_theme(name) picks a theme by name.
        // apply_ui_theme(name, root) bakes it onto the named object instead.
        b["apply_ui_theme"] = [selfObj](std::vector<Value>& a) {
            GameObject* g = selfObj();
            if (!g || !g->scene()) return Value{0.0f};
            const std::string name = a.empty() ? std::string() : a[0].AsString();
            UITheme* theme = UITheme::Find(g->scene(), name);
            if (!theme && name.empty()) theme = g->GetComponent<UITheme>();
            if (!theme) return Value{0.0f};
            GameObject* root = theme->gameObject;
            if (a.size() >= 2) {
                if (GameObject* named = ResolveObject(g->scene(), a[1])) root = named;
            }
            theme->ApplyTo(root);
            return Value{1.0f};
        };
        b["ApplyUITheme"] = b["apply_ui_theme"];
        // set_ui_theme(0 custom, 1 dark, 2 light, 3 sci-fi, 4 fantasy) fills the
        // palette and bakes it onto this theme's object.
        b["set_ui_theme"] = [selfObj](std::vector<Value>& a) {
            GameObject* g = selfObj();
            if (!g || !g->scene()) return Value{0.0f};
            const std::string name = a.size() >= 2 ? a[1].AsString() : std::string();
            UITheme* theme = name.empty() ? g->GetComponent<UITheme>() : nullptr;
            if (!theme) theme = UITheme::Find(g->scene(), name);
            if (!theme) return Value{0.0f};
            theme->ApplyPreset(a.empty() ? 1 : SafeInt(a[0].AsFloat()));
            theme->Apply();
            return Value{1.0f};
        };
        b["SetUITheme"] = b["set_ui_theme"];
        // Clip queries: timing + existence, so scripts can react to where a clip is.
        b["clip_time"] = [charSelf, modelSelf](std::vector<Value>&) {
            Character* c = charSelf();
            if (c) return Value{c->ClipTime()};
            ModelAnimator* m = modelSelf();
            return Value{m ? m->Time() : 0.0f};
        };
        b["clip_normalized"] = [selfObj](std::vector<Value>&) { return Value{ClipNormalizedFrom(selfObj())}; };
        b["clip_finished"] = [charSelf, modelSelf](std::vector<Value>&) {
            Character* c = charSelf();
            if (c) return Value{c->ClipFinished() ? 1.0f : 0.0f};
            ModelAnimator* m = modelSelf();
            return Value{(m && m->ClipFinished()) ? 1.0f : 0.0f};
        };
        b["clip_duration"] = [charSelf, modelSelf](std::vector<Value>& a) {
            if (a.empty()) return Value{0.0f};
            Character* c = charSelf();
            if (c) return Value{c->ClipDuration(a[0].AsString())};
            ModelAnimator* m = modelSelf();
            return Value{m ? m->ClipLength(m->FindClip(a[0].AsString())) : 0.0f};
        };
        b["has_clip"] = [charSelf, modelSelf](std::vector<Value>& a) {
            if (a.empty()) return Value{0.0f};
            Character* c = charSelf();
            if (c) return Value{c->HasClip(a[0].AsString()) ? 1.0f : 0.0f};
            ModelAnimator* m = modelSelf();
            return Value{(m && m->FindClip(a[0].AsString()) >= 0) ? 1.0f : 0.0f};
        };
        // Play a clip ONCE (attack / jump / hit reaction) and automatically return
        // to the previous clip — imported models via ModelAnimator; a Character
        // falls back to PlayClip (its clips carry their own once/loop mode).
        b["play_clip_once"] = [charSelf, modelSelf](std::vector<Value>& a) {
            if (a.empty()) return Value{0.0f};
            ModelAnimator* m = modelSelf();
            if (m && m->PlayOnce(a[0].AsString())) return Value{1.0f};
            Character* c = charSelf();
            return Value{(c && c->PlayClip(a[0].AsString())) ? 1.0f : 0.0f};
        };
        // Pop the next fired animation event name ("" if none) — footsteps, hit windows.
}

void OkayScriptVM::Impl::RegisterAnimStateMachineBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        auto go = c.go;
        auto selfObj = c.selfObj;
        auto smSelf = c.smSelf;
        // ---- Animation state machine (found like the animators above) ----
        b["anim_set_float"] = [smSelf](std::vector<Value>& a) {
            if (AnimStateMachine* m = smSelf(); m && a.size() >= 2) m->SetFloat(a[0].AsString(), a[1].AsFloat());
            return Value{};
        };
        b["anim_set_bool"] = [smSelf](std::vector<Value>& a) {
            if (AnimStateMachine* m = smSelf(); m && a.size() >= 2) m->SetBool(a[0].AsString(), a[1].AsFloat() != 0.0f);
            return Value{};
        };
        b["anim_trigger"] = [smSelf](std::vector<Value>& a) {
            if (AnimStateMachine* m = smSelf(); m && !a.empty()) m->SetTrigger(a[0].AsString());
            return Value{};
        };
        b["anim_get_float"] = [smSelf](std::vector<Value>& a) {
            if (a.empty()) return Value{0.0f};
            AnimStateMachine* m = smSelf();
            return Value{m ? m->GetFloat(a[0].AsString()) : 0.0f};
        };
        b["anim_get_bool"] = [smSelf](std::vector<Value>& a) {
            if (a.empty()) return Value{0.0f};
            AnimStateMachine* m = smSelf();
            return Value{(m && m->GetBool(a[0].AsString())) ? 1.0f : 0.0f};
        };
        b["anim_get_int"] = [smSelf](std::vector<Value>& a) {
            if (a.empty()) return Value{0.0f};
            AnimStateMachine* m = smSelf();
            return Value{m ? (float)m->GetInt(a[0].AsString()) : 0.0f};
        };
        auto layerOf = [selfObj](const Value& which) -> AnimStateMachine* {
            GameObject* g = selfObj();
            if (!g) return nullptr;
            if (which.IsString()) return FindAnimLayer(g, true, which.AsString(), 0);
            return FindAnimLayer(g, false, {}, (int)std::lround(which.AsFloat()));
        };
        b["anim_set_layer_weight"] = [layerOf](std::vector<Value>& a) {
            if (a.size() < 2) return Value{0.0f};
            AnimStateMachine* sm = layerOf(a[0]);
            if (!sm) return Value{0.0f};
            sm->layerWeight = a[1].AsFloat();
            return Value{1.0f};
        };
        b["anim_get_layer_weight"] = [layerOf](std::vector<Value>& a) {
            if (a.empty()) return Value{0.0f};
            AnimStateMachine* sm = layerOf(a[0]);
            return Value{sm ? sm->layerWeight : 0.0f};
        };
        b["set_root_motion"] = [selfObj](std::vector<Value>& a) {
            const bool on = a.empty() || a[0].AsFloat() != 0.0f;
            return Value{SetRootMotionOn(selfObj(), on) ? 1.0f : 0.0f};
        };
        b["set_character_model"] = [selfObj](std::vector<Value>& a) {
            if (a.empty()) return Value{0.0f};
            return Value{SetCharacterModelPath(selfObj(), a[0].AsString()) ? 1.0f : 0.0f};
        };
        // Actions (HumanoidActions): play_action goes to a state, or a blend-tree
        // motion of that clip name, else play_clip. die/revive drive Death.
        b["play_action"] = [selfObj](std::vector<Value>& a) {
            if (a.empty()) return Value{0.0f};
            const float blend = a.size() >= 2 ? a[1].AsFloat() : 0.2f;
            return Value{HumanoidActions::Play(selfObj(), a[0].AsString(), blend) ? 1.0f : 0.0f};
        };
        b["is_in_action"] = [selfObj](std::vector<Value>&) {
            return Value{HumanoidActions::IsInAction(selfObj()) ? 1.0f : 0.0f};
        };
        b["action_time"] = [selfObj](std::vector<Value>&) {
            return Value{HumanoidActions::ActionTime(selfObj())};
        };
        b["die"] = [selfObj](std::vector<Value>& a) {
            GameObject* go = selfObj();
            int dir = 0;
            const Vec3* from = nullptr;
            Vec3 world;
            if (a.size() >= 3 && a[0].IsNumber()) {
                world = {a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat()};
                from = &world;
            } else if (!a.empty() && a[0].IsVec3()) {
                world = a[0].AsVec3();
                from = &world;
            } else if (!a.empty() && a[0].IsString()) dir = HumanoidActions::DirFromName(a[0].AsString());
            else if (!a.empty() && a[0].IsNumber()) dir = SafeInt(a[0].AsFloat());
            HumanoidActions::RequestDeath(go, dir, from);
            return Value{};
        };
        b["revive"] = [selfObj](std::vector<Value>&) {
            HumanoidActions::RequestRevive(selfObj());
            return Value{};
        };
        // is_dead("Orc") also reads a named object's Stats / SurvivalStats.
        b["is_dead"] = [selfObj](std::vector<Value>& a) {
            if (!a.empty() && a[0].IsString()) {
                GameObject* me = selfObj();
                if (GameObject* g = me && me->scene() ? ResolveObject(me->scene(), a[0]) : nullptr) {
                    if (auto* st = g->GetComponent<Stats>()) return Value{st->IsDead() ? 1.0f : 0.0f};
                    if (auto* sv = g->GetComponent<SurvivalStats>()) return Value{sv->dead ? 1.0f : 0.0f};
                    return Value{IsObjectDead(g) ? 1.0f : 0.0f};
                }
                return Value{0.0f};
            }
            return Value{HumanoidActions::IsDead(selfObj()) ? 1.0f : 0.0f};
        };
        // Ragdoll: ragdoll() / ragdoll(0) goes limp / stands up; ragdoll_push(vx, vy, vz) gives the chest (or the part
        // nearest an optional px, py, pz) a push of that speed in m/s on the upper body (0.3 of the body's mass, about the
        // chest and arms: the old particle ragdoll pushed one part).
        b["ragdoll"] = [selfObj](std::vector<Value>& a) {
            auto* rd = selfObj() ? selfObj()->GetComponent<Ragdoll>() : nullptr;
            if (!rd) return Value{0.0f};
            if (!a.empty() && a[0].AsFloat() == 0.0f) { rd->Deactivate(); return Value{1.0f}; }
            return Value{rd->Activate() ? 1.0f : 0.0f};
        };
        b["ragdoll_push"] = [selfObj](std::vector<Value>& a) {
            auto* rd = selfObj() ? selfObj()->GetComponent<Ragdoll>() : nullptr;
            if (!rd || !rd->Ready() || a.size() < 3) return Value{0.0f};
            const Vec3 j = Vec3{a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat()} * (0.3f * rd->mass);
            if (a.size() >= 6) rd->PushAt(Vec3{a[3].AsFloat(), a[4].AsFloat(), a[5].AsFloat()}, j);
            else rd->Push(j);
            return Value{1.0f};
        };
        b["is_ragdoll"] = [selfObj](std::vector<Value>&) {
            auto* rd = selfObj() ? selfObj()->GetComponent<Ragdoll>() : nullptr;
            return Value{rd && rd->IsActive() ? 1.0f : 0.0f};
        };
        // The same Ragdoll, by its old ActiveRagdoll names: active_ragdoll("knockdown" | "getup" | "die" | "revive" |
        // "limp" | "powered" | "reactive" | "stumble"), active_ragdoll_push(ix, iy, iz[, px, py, pz]) in N s,
        // active_ragdoll_shoot(ox, oy, oz, dx, dy, dz[, power]) -> 1 on a hit, active_ragdoll_explode(cx, cy, cz,
        // force, radius), active_ragdoll_state() -> "Animated", "Balancing", "Falling", "Down", "GettingUp", ...
        b["active_ragdoll"] = [selfObj](std::vector<Value>& a) {
            auto* ar = selfObj() ? selfObj()->GetComponent<Ragdoll>() : nullptr;
            if (!ar || a.empty()) return Value{0.0f};
            return Value{ar->Trigger(a[0].AsString()) ? 1.0f : 0.0f};
        };
        b["active_ragdoll_push"] = [selfObj](std::vector<Value>& a) {
            auto* ar = selfObj() ? selfObj()->GetComponent<Ragdoll>() : nullptr;
            if (!ar || !ar->Ready() || a.size() < 3) return Value{0.0f};
            const Vec3 j{a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat()};
            if (a.size() >= 6) ar->PushAt(Vec3{a[3].AsFloat(), a[4].AsFloat(), a[5].AsFloat()}, j);
            else ar->Push(j);
            return Value{1.0f};
        };
        b["active_ragdoll_shoot"] = [selfObj](std::vector<Value>& a) {
            auto* ar = selfObj() ? selfObj()->GetComponent<Ragdoll>() : nullptr;
            if (!ar || a.size() < 6) return Value{0.0f};
            const float power = a.size() >= 7 ? a[6].AsFloat() : 25.0f;
            return Value{ar->Shoot(Vec3{a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat()},
                                   Vec3{a[3].AsFloat(), a[4].AsFloat(), a[5].AsFloat()}, power) ? 1.0f : 0.0f};
        };
        b["active_ragdoll_explode"] = [selfObj](std::vector<Value>& a) {
            auto* ar = selfObj() ? selfObj()->GetComponent<Ragdoll>() : nullptr;
            if (!ar || a.size() < 5) return Value{0.0f};
            ar->Explode(Vec3{a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat()}, a[3].AsFloat(), a[4].AsFloat());
            return Value{1.0f};
        };
        b["active_ragdoll_state"] = [selfObj](std::vector<Value>&) {
            auto* ar = selfObj() ? selfObj()->GetComponent<Ragdoll>() : nullptr;
            return Value{std::string(ar && ar->Ready() ? ar->StateName() : "")};
        };
        b["dive"] = [selfObj](std::vector<Value>&) {
            HumanoidActions::RequestDive(selfObj());
            return Value{};
        };
        b["can_dive"] = [selfObj](std::vector<Value>&) {
            return Value{HumanoidActions::CanDive(selfObj()) ? 1.0f : 0.0f};
        };
        // sit() ground, sit("Chair") a Seat by name, sit("knees") / sit(2) a kneel.
        // sit(0) is a chair pose with no Seat (the body stays where it is).
        b["sit"] = [selfObj](std::vector<Value>& a) {
            GameObject* go = selfObj();
            if (!go) return Value{0.0f};
            GameObject* seat = nullptr;
            int type = 1;
            if (!a.empty()) {
                if (a[0].IsNumber()) type = SafeInt(a[0].AsFloat());
                else if (go->scene()) {
                    const std::string name = a[0].AsString();
                    seat = go->scene()->Find(name);
                    if (!seat) {
                        std::string k = name;
                        for (char& c : k) c = (char)std::tolower((unsigned char)c);
                        if (k == "chair" || k == "bench") type = 0;
                        else if (k == "knees") type = 2;
                    }
                }
            }
            return Value{HumanoidActions::RequestSit(go, seat, type) ? 1.0f : 0.0f};
        };
        b["stand_up"] = [selfObj](std::vector<Value>&) {
            HumanoidActions::RequestStandUp(selfObj());
            return Value{};
        };
        b["is_sitting"] = [selfObj](std::vector<Value>&) {
            return Value{HumanoidActions::IsSitting(selfObj()) ? 1.0f : 0.0f};
        };
        b["interact"] = [selfObj](std::vector<Value>&) {
            HumanoidActions::Interact(selfObj());
            return Value{};
        };
        b["seat_prompt"] = [selfObj](std::vector<Value>&) {
            return Value{HumanoidActions::SeatPrompt(selfObj())};
        };
        // hit_react("back") light, hit_react("left", 1) heavy, or hit_react(x, y, z [, heavy]).
        b["hit_react"] = [selfObj](std::vector<Value>& a) {
            GameObject* go = selfObj();
            int dir = 0;
            bool heavy = false;
            if (a.size() >= 3 && a[0].IsNumber()) {
                dir = HumanoidActions::HitDirFromWorld(go, Vec3{a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat()});
                if (a.size() >= 4) heavy = a[3].AsFloat() != 0.0f;
            } else if (!a.empty() && a[0].IsVec3()) {
                dir = HumanoidActions::HitDirFromWorld(go, a[0].AsVec3());
                if (a.size() >= 2) heavy = a[1].AsFloat() != 0.0f;
            } else {
                if (!a.empty() && a[0].IsString()) dir = HumanoidActions::DirFromName(a[0].AsString());
                else if (!a.empty() && a[0].IsNumber()) dir = SafeInt(a[0].AsFloat());
                if (a.size() >= 2) heavy = a[1].AsFloat() != 0.0f;
            }
            return Value{HumanoidActions::HitReact(go, dir, heavy) ? 1.0f : 0.0f};
        };
        auto dirArg = [](const Value& v) {
            if (v.IsString()) return HumanoidActions::DirFromName(v.AsString());
            return SafeInt(v.AsFloat());
        };
        b["roll"] = [selfObj, dirArg](std::vector<Value>& a) {
            const int dir = a.empty() ? 0 : dirArg(a[0]);
            return Value{HumanoidActions::RequestRoll(selfObj(), dir) ? 1.0f : 0.0f};
        };
        // weapon_swing / weapon_fire / weapon_reload / weapon_ammo are registered by the weapons
        // module on the shared script table (engine/modules/weapons/register.cpp).
        b["weapon_set"] = [selfObj](std::vector<Value>& a) {
            GameObject* go = selfObj();
            if (!go || a.empty()) return Value{0.0f};
            const std::string id = a[0].AsString();
            bool any = false;
            if (auto* m = go->GetComponent<MeleeAttacker>()) { m->weapon = id; any = true; }
            if (auto* g = go->GetComponent<RangedWeapon>()) { g->weapon = id; any = true; }
            if (auto* s = go->GetComponent<TopDownShooterController>()) { s->weapon = id; any = true; }
            return Value{any ? 1.0f : 0.0f};
        };
        b["dodge"] = [selfObj, dirArg](std::vector<Value>& a) {
            const int dir = a.empty() ? 0 : dirArg(a[0]);
            return Value{HumanoidActions::RequestDodge(selfObj(), dir) ? 1.0f : 0.0f};
        };
        // Vault / mantle / ledge. These probe even when the controller flags are off.
        b["vault"] = [selfObj](std::vector<Value>&) {
            return Value{HumanoidActions::RequestVault(selfObj()) ? 1.0f : 0.0f};
        };
        b["mantle"] = [selfObj](std::vector<Value>&) {
            return Value{HumanoidActions::RequestMantle(selfObj()) ? 1.0f : 0.0f};
        };
        b["ledge_grab"] = [selfObj](std::vector<Value>&) {
            return Value{HumanoidActions::RequestLedgeGrab(selfObj()) ? 1.0f : 0.0f};
        };
        b["climb_up"] = [selfObj](std::vector<Value>&) {
            return Value{HumanoidActions::RequestClimbUp(selfObj()) ? 1.0f : 0.0f};
        };
        b["let_go"] = [selfObj](std::vector<Value>&) {
            HumanoidActions::RequestLetGo(selfObj());
            return Value{};
        };
        b["is_hanging"] = [selfObj](std::vector<Value>&) {
            return Value{HumanoidActions::IsHanging(selfObj()) ? 1.0f : 0.0f};
        };
        b["shimmy"] = [selfObj](std::vector<Value>& a) {
            HumanoidActions::SetShimmy(selfObj(), a.empty() ? 0.0f : a[0].AsFloat());
            return Value{};
        };
}

void OkayScriptVM::Impl::RegisterBlendShapeBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        auto smSelf = c.smSelf;
        // ---- Blend shapes (morph targets) on this object and everything under it ----
        //   blend_shape("Smile", 0.8)   set a shape's weight 0..1 on every mesh that has it
        //   blend_shape_get("Smile")    read it back (0 if no mesh has it)
        auto bsEach = [this](auto fn) {
            GameObject* g = rt.host ? rt.host->gameObject : nullptr;
            if (!g || !g->scene()) return;
            for (const auto& up : g->scene()->Objects())
                if (up && up->IsSelfOrDescendantOf(g))
                    if (auto* bs = up->GetComponent<BlendShapes>()) fn(bs);
        };
        b["blend_shape"] = [bsEach](std::vector<Value>& a) {
            if (a.size() < 2) return Value{0.0f};
            bool any = false;
            const std::string nm = a[0].AsString(); const float w = a[1].AsFloat();
            bsEach([&](BlendShapes* bs) { any = bs->SetWeight(nm, w) || any; });
            return Value{any ? 1.0f : 0.0f};
        };
        b["blend_shape_get"] = [bsEach](std::vector<Value>& a) {
            if (a.empty()) return Value{0.0f};
            float w = 0.0f; const std::string nm = a[0].AsString();
            bsEach([&](BlendShapes* bs) { if (bs->Find(nm) >= 0) w = bs->Weight(nm); });
            return Value{w};
        };
        b["anim_state"] = [smSelf](std::vector<Value>&) {
            AnimStateMachine* m = smSelf(); return Value{m ? m->Current() : std::string{}};
        };
        // Previous state, seconds in the current state, and normalized clip time
        // (0..1 per loop; keeps counting past 1 on loops) — for gameplay timing.
}

void OkayScriptVM::Impl::RegisterIkBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        auto selfObj = c.selfObj;
        // ---- IK control on this object (any of LimbIK / ChainIK / LookAtIK / AimIK) ----
        //   ik_enable(on)            switch IK on/off (eases over each component's fadeTime)
        //   ik_set_weight(w)         0..1 on every IK component on this object
        //   ik_set_weight(kind, w)   one of foot, look, aim, hand_l, hand_r, full
        //                            (also foot_l, foot_r). Looks on this object and its children.
        //   ik_set_target(x, y, z)   world point on Limb / Chain / Look / Aim
        //   ik_set_target(limb, "Name") or ik_set_target(limb, x, y, z)
        //   ik_clear_target(limb)    drop that goal
        //   ik_set_pole(x, y, z)     bend direction point (LimbIK pole / ChainIK pole)
        auto ikEach = [this](auto fn) {
            GameObject* g = rt.host ? rt.host->gameObject : nullptr;
            if (!g) return;
            if (auto* c = g->GetComponent<LimbIK>())   fn(c);
            if (auto* c = g->GetComponent<ChainIK>())  fn(c);
            if (auto* c = g->GetComponent<LookAtIK>()) fn(c);
            if (auto* c = g->GetComponent<AimIK>())    fn(c);
        };
        auto ikToggle = [this](auto fn) {   // ik_enable / ik_set_weight also reach Full Body IK
            GameObject* g = rt.host ? rt.host->gameObject : nullptr;
            if (g) if (auto* c = g->GetComponent<FullBodyIK>()) fn(c);
        };
        b["ik_enable"] = [ikEach, ikToggle](std::vector<Value>& a) {
            bool on = a.empty() || a[0].AsFloat() != 0.0f;
            ikEach([&](auto* c) { c->ikActive = on; });
            ikToggle([&](auto* c) { c->ikActive = on; });
            return Value{};
        };
        b["ik_set_weight"] = [selfObj, ikEach, ikToggle](std::vector<Value>& a) {
            if (!a.empty() && a[0].IsString()) {
                const float w = a.size() > 1 ? a[1].AsFloat() : 1.0f;
                return Value{SetIkKindWeight(selfObj(), a[0].AsString(), w) ? 1.0f : 0.0f};
            }
            float w = a.empty() ? 1.0f : a[0].AsFloat();
            ikEach([&](auto* c) { c->weight = w; });
            ikToggle([&](auto* c) { c->weight = w; });
            return Value{};
        };
        b["ik_set_target"] = [selfObj, ikEach](std::vector<Value>& a) {
            if (!a.empty() && a[0].IsString()) {
                GameObject* g = selfObj();
                if (a.size() >= 2 && a[1].IsString()) {
                    GameObject* named = (g && g->scene()) ? ResolveObject(g->scene(), a[1]) : nullptr;
                    if (!named) return Value{0.0f};
                    return Value{SetIkLimbTarget(g, a[0].AsString(), named, nullptr) ? 1.0f : 0.0f};
                }
                Vec3 t{a.size() > 1 ? a[1].AsFloat() : 0.0f, a.size() > 2 ? a[2].AsFloat() : 0.0f,
                       a.size() > 3 ? a[3].AsFloat() : 0.0f};
                return Value{SetIkLimbTarget(g, a[0].AsString(), nullptr, &t) ? 1.0f : 0.0f};
            }
            Vec3 t{a.size() > 0 ? a[0].AsFloat() : 0.0f, a.size() > 1 ? a[1].AsFloat() : 0.0f, a.size() > 2 ? a[2].AsFloat() : 0.0f};
            ikEach([&](auto* c) { c->targetObject = nullptr; c->target = t; });
            return Value{};
        };
        b["ik_clear_target"] = [selfObj](std::vector<Value>& a) {
            if (a.empty()) return Value{0.0f};
            return Value{ClearIkLimbTarget(selfObj(), a[0].AsString()) ? 1.0f : 0.0f};
        };
        // Full Body IK goals on this object:
        //   ik_hand("left"|"right", x, y, z [, weight])   reach a point (weight 0 releases)
        //   ik_foot("left"|"right", x, y, z [, weight])
        //   ik_look(x, y, z [, weight])                   turn head + chest toward a point
        //   ik_release()                                  drop every goal
        auto fbik = [this]() -> FullBodyIK* {
            GameObject* g = rt.host ? rt.host->gameObject : nullptr;
            return g ? g->GetComponent<FullBodyIK>() : nullptr;
        };
        auto setGoal = [fbik](std::vector<Value>& a, bool hand) {
            FullBodyIK* f = fbik();
            if (!f || a.empty()) return Value{};
            std::string side = a[0].AsString();
            bool left = !side.empty() && (side[0] == 'l' || side[0] == 'L');
            int l = hand ? (left ? FullBodyIK::LeftHand : FullBodyIK::RightHand) : (left ? FullBodyIK::LeftFoot : FullBodyIK::RightFoot);
            FullBodyIK::Effector& e = f->effectors[l];
            e.target = nullptr;
            e.position = {a.size() > 1 ? a[1].AsFloat() : 0.0f, a.size() > 2 ? a[2].AsFloat() : 0.0f, a.size() > 3 ? a[3].AsFloat() : 0.0f};
            float w = a.size() > 4 ? a[4].AsFloat() : 1.0f;
            e.positionWeight = w; e.enabled = w > 0.0f;
            return Value{};
        };
        b["ik_hand"] = [setGoal](std::vector<Value>& a) { return setGoal(a, true); };
        b["ik_foot"] = [setGoal](std::vector<Value>& a) { return setGoal(a, false); };
        // ik_look_at("Guard") aims this object's Look-At IK at that object.
        // set_stance(0 stand, 1 crouch, 2 prone); crouch / prone / stand are the same.
        // (look_at is the transform turn; this one is the head IK.)
        b["ik_look_at"] = [selfObj](std::vector<Value>& a) {
            GameObject* g = selfObj();
            if (!g || !g->scene() || a.empty()) return Value{0.0f};
            GameObject* tgt = ResolveObject(g->scene(), a[0]);
            if (!tgt) return Value{0.0f};
            GameObject* host = g;
            for (Transform* t = g->transform; t; t = t->Parent()) {
                if (!t->gameObject) continue;
                if (t->gameObject->GetComponent<LookAtIK>() || t->gameObject->GetComponent<Character>())
                    host = t->gameObject;
            }
            LookAtIK* look = host->GetComponent<LookAtIK>();
            if (!look) look = host->AddComponent<LookAtIK>();
            look->targetObject = tgt->transform;
            look->targetName = tgt->name;
            look->ikActive = true;
            if (a.size() > 1) look->weight = a[1].AsFloat();
            return Value{1.0f};
        };
        // recoil(strength) kicks an Aim Offset on this object or under it.
        // capture_pose() saves one frame; capture_pose(seconds) records that long.
        b["recoil"] = [selfObj](std::vector<Value>& a) {
            GameObject* g = selfObj();
            if (!g) return Value{0.0f};
            const float strength = a.empty() ? 1.0f : a[0].AsFloat();
            auto fire = [&](AimOffset* o) { if (o) { o->Recoil(strength); return true; } return false; };
            if (fire(g->GetComponent<AimOffset>())) return Value{1.0f};
            if (!g->scene()) return Value{0.0f};
            for (const auto& up : g->scene()->Objects())
                if (up && up->IsSelfOrDescendantOf(g) && fire(up->GetComponent<AimOffset>())) return Value{1.0f};
            return Value{0.0f};
        };
        b["capture_pose"] = [selfObj](std::vector<Value>& a) {
            GameObject* g = selfObj();
            if (!g) return Value{0.0f};
            PoseCapture* pc = g->GetComponent<PoseCapture>();
            if (!pc && g->scene())
                for (const auto& up : g->scene()->Objects())
                    if (up && up->IsSelfOrDescendantOf(g) && (pc = up->GetComponent<PoseCapture>())) break;
            if (!pc) pc = g->AddComponent<PoseCapture>();
            if (!a.empty() && a[0].AsFloat() > 0.0f) { pc->StartRecord(a[0].AsFloat()); return Value{1.0f}; }
            if (pc->IsRecording()) return Value{pc->StopRecord() >= 0 ? 1.0f : 0.0f};
            return Value{pc->CaptureFrame() >= 0 ? 1.0f : 0.0f};
        };
        auto setStance = [selfObj](int s) {
            if (s < 0) s = 0;
            if (s > 2) s = 2;
            GameObject* g = selfObj();
            for (Transform* t = g ? g->transform : nullptr; t; t = t->Parent()) {
                GameObject* p = t->gameObject;
                if (!p) continue;
                if (auto* c = p->GetComponent<ThirdPersonController>()) {
                    c->SetStance((ThirdPersonController::Stance)s); return true;
                }
                if (auto* c = p->GetComponent<FirstPersonController>()) {
                    c->SetStance((FirstPersonController::Stance)s); return true;
                }
            }
            return false;
        };
        b["set_stance"] = [setStance](std::vector<Value>& a) {
            return Value{setStance(a.empty() ? 0 : SafeInt(a[0].AsFloat())) ? 1.0f : 0.0f};
        };
        b["crouch"] = [setStance](std::vector<Value>&) { return Value{setStance(1) ? 1.0f : 0.0f}; };
        b["prone"]  = [setStance](std::vector<Value>&) { return Value{setStance(2) ? 1.0f : 0.0f}; };
        b["stand"]  = [setStance](std::vector<Value>&) { return Value{setStance(0) ? 1.0f : 0.0f}; };
        b["ik_look"] = [fbik, selfObj](std::vector<Value>& a) {
            if (FullBodyIK* f = fbik()) {
                f->lookTarget = nullptr;
                f->lookPosition = {a.size() > 0 ? a[0].AsFloat() : 0.0f, a.size() > 1 ? a[1].AsFloat() : 0.0f, a.size() > 2 ? a[2].AsFloat() : 0.0f};
                f->lookWeight = a.size() > 3 ? a[3].AsFloat() : 1.0f;
                f->lookEnabled = f->lookWeight > 0.0f;
            }
            GameObject* g = selfObj();
            if (g) if (auto* look = g->GetComponent<LookAtIK>()) {
                look->targetObject = nullptr;
                look->aimAtPoint = true;
                look->target = {a.size() > 0 ? a[0].AsFloat() : 0.0f, a.size() > 1 ? a[1].AsFloat() : 0.0f,
                                a.size() > 2 ? a[2].AsFloat() : 0.0f};
                if (a.size() > 3) look->weight = a[3].AsFloat();
                look->ikActive = true;
            }
            return Value{};
        };
        // ik_reached("left_hand" | "right_hand" | "left_foot" | "right_foot" [, tolerance]) -> 1 when there.
        b["ik_reached"] = [fbik](std::vector<Value>& a) {
            FullBodyIK* f = fbik();
            if (!f || a.empty()) return Value{0.0f};
            std::string n = a[0].AsString();
            for (auto& ch : n) ch = (char)std::tolower((unsigned char)ch);
            const bool left = n.find("left") != std::string::npos || (!n.empty() && n[0] == 'l');
            const bool foot = n.find("foot") != std::string::npos;
            int l = foot ? (left ? FullBodyIK::LeftFoot : FullBodyIK::RightFoot) : (left ? FullBodyIK::LeftHand : FullBodyIK::RightHand);
            return Value{f->Reached(l, a.size() > 1 ? a[1].AsFloat() : 0.05f) ? 1.0f : 0.0f};
        };
        b["ik_release"] = [fbik](std::vector<Value>&) {
            if (FullBodyIK* f = fbik()) { for (auto& e : f->effectors) e.enabled = false; f->lookEnabled = false; }
            return Value{};
        };
        // Procedural Object Holding on this object:
        //   hold_pickup(name) / hold_nearest([range])   take an object into the hands -> 1 if held
        //   hold_drop()                                 let go of it (keeps the holder's velocity)
        //   hold_throw([charge])                        launch it along the camera aim; charge is 0..1
        //                                               (omit it to use the charge built by holding the key)
        //   hold_charge()                               that charge right now, 0..1
        //   hold_object()                               name of what's held ("" = nothing)
        auto holder = [this]() -> ProceduralObjectHolding* {
            GameObject* g = rt.host ? rt.host->gameObject : nullptr;
            return g ? g->GetComponent<ProceduralObjectHolding>() : nullptr;
        };
        b["hold_pickup"] = [this, holder](std::vector<Value>& a) {
            ProceduralObjectHolding* h = holder();
            Scene* sc = rt.host && rt.host->gameObject ? rt.host->gameObject->scene() : nullptr;
            GameObject* obj = (sc && !a.empty()) ? ResolveObject(sc, a[0]) : nullptr;
            return Value{h && h->Pickup(obj) ? 1.0f : 0.0f};
        };
        b["hold_nearest"] = [holder](std::vector<Value>& a) {
            ProceduralObjectHolding* h = holder();
            return Value{h && h->PickupNearest(a.empty() ? 2.0f : a[0].AsFloat()) ? 1.0f : 0.0f};
        };
        b["hold_drop"] = [holder](std::vector<Value>&) {
            if (ProceduralObjectHolding* h = holder()) h->Drop();
            return Value{};
        };
        b["hold_throw"] = [holder](std::vector<Value>& a) {
            ProceduralObjectHolding* h = holder();
            const float charge = a.empty() ? -1.0f : a[0].AsFloat();
            return Value{h && h->Throw(charge) ? 1.0f : 0.0f};
        };
        b["hold_charge"] = [holder](std::vector<Value>&) {
            ProceduralObjectHolding* h = holder();
            return Value{h ? h->ThrowCharge() : 0.0f};
        };
        b["hold_object"] = [holder](std::vector<Value>&) {
            ProceduralObjectHolding* h = holder();
            return Value{h && h->Held() ? h->Held()->name : std::string()};
        };
}

} // namespace okay
