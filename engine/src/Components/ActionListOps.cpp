// ActionList::RunMoreOps: the second half of the instruction chain (arrays, maps, strings, scene, objects, audio,
// raycasts, math, game flow...), moved verbatim out of ActionList::Update (docs/engine/modularization_plan.md, S7).
#include "ActionListInternal.hpp"
#include "okay/Components/ActionList.hpp"
#include "okay/Core/ScriptPaths.hpp"
#include "okay/Core/AccessibilitySettings.hpp"
#include "okay/Core/DisplaySettings.hpp"
#include "okay/Components/LogicLinks.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include "okay/Components/AudioSource.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/SpriteRenderer.hpp"
#include "okay/Components/ParticleSystem.hpp"
#include "okay/Components/SpriteAnimator.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Animation/HumanoidActions.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Components/UIProgressBar.hpp"
#include "okay/Components/UIRadialProgress.hpp"
#include "okay/Components/UITextBind.hpp"        // Resolve {var}/{pref} tokens in text
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/VisualScriptComponent.hpp"   // messages also reach node graphs
#include "okay/Components/NativeUIActions.hpp"   // InvokeNativeUIAction (survival verbs)
#include "okay/Components/Consumables.hpp"       // UseIndex
#include "okay/Components/ItemPickup.hpp"        // InventoryOf (give / take / has_item)
#include "okay/Physics/Rigidbody2D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Physics/Collider2D.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Render/Lighting.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Scene/SceneManager.hpp"
#include "okay/Scene/Layers.hpp"           // raycast "lm:" layer lists
#include "okay/Net/NetworkManager.hpp"
#include "okay/Net/ServerBrowser.hpp"
#include "okay/Net/MatchResults.hpp"
#include "okay/Net/Matchmaker.hpp"
#include "okay/Platform/Steam/Steam.hpp"
#include "okay/Platform/PlayFab/PlayFab.hpp"
#include "okay/Platform/Achievements.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Core/Prefs.hpp"
#include "okay/Core/SaveData.hpp"
#include "okay/Core/GameSaves.hpp"
#include "okay/Core/Game.hpp"          // win / lose / quit instructions
#include "okay/Input/Input.hpp"
#include "okay/Math/Mathf.hpp"
#include "okay/Math/Easing.hpp"
#include "okay/Core/Random.hpp"
#include "okay/Core/Scheduler.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Core/Time.hpp"
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace okay {

using namespace actionlist_ops;

int ActionList::RunMoreOps(const Item& it, const std::string& op, Scene* scene, Transform* t) {
        // ---- Arrays (float lists) ----
        if (op == "array_clear") { Arrays()[Str(it, 0)].clear(); }
        else if (op == "array_push")  { Arrays()[Str(it, 0)].push_back(Num(it, 1)); }
        else if (op == "array_set")   {
            auto& a = Arrays()[Str(it, 0)]; int i2 = (int)Num(it, 1);
            if (i2 >= 0) { if ((int)a.size() <= i2) a.resize(i2 + 1, 0.0f); a[i2] = Num(it, 2); }
        }
        else if (op == "array_get")   {
            auto& a = Arrays()[Str(it, 0)]; int i2 = (int)Num(it, 1);
            Vars()[Str(it, 2)] = (i2 >= 0 && i2 < (int)a.size()) ? a[i2] : 0.0f;
        }
        else if (op == "array_len")   { Vars()[Str(it, 1)] = (float)Arrays()[Str(it, 0)].size(); }
        else if (op == "array_pop")   {
            auto& a = Arrays()[Str(it, 0)];
            float v = a.empty() ? 0.0f : a.back(); if (!a.empty()) a.pop_back();
            if (it.args.size() > 1) Vars()[Str(it, 1)] = v;
        }
        // ---- Maps / dictionaries (string key -> number) ----
        else if (op == "map_set")   { Maps()[Str(it, 0)][Str(it, 1)] = Num(it, 2); }
        else if (op == "map_get")   {
            auto& m = Maps()[Str(it, 0)]; auto mit = m.find(Str(it, 1));
            Vars()[Str(it, 2)] = (mit != m.end()) ? mit->second : (it.args.size() > 3 ? Num(it, 3) : 0.0f);
        }
        else if (op == "map_del")   { Maps()[Str(it, 0)].erase(Str(it, 1)); }
        else if (op == "map_clear") { Maps()[Str(it, 0)].clear(); }
        else if (op == "map_size")  { Vars()[Str(it, 1)] = (float)Maps()[Str(it, 0)].size(); }
        // ---- Text (string) variables ----
        else if (op == "str_set")    { StrVars()[Str(it, 0)] = Rest(it, 1); }               // literal text
        else if (op == "str_copy")   { StrVars()[Str(it, 0)] = StrVars()[Str(it, 1)]; }
        else if (op == "str_append") { StrVars()[Str(it, 0)] += Rest(it, 1); }
        else if (op == "str_concat") { StrVars()[Str(it, 0)] = StrVars()[Str(it, 1)] + StrVars()[Str(it, 2)]; }
        else if (op == "str_from_num"){                                                     // number -> text
            float v = GetVar(Str(it, 1));
            StrVars()[Str(it, 0)] = UITextBind::Resolve("{" + Str(it, 1) + "}");             // pretty-formats like the HUD
            if (StrVars()[Str(it, 0)].empty()) { char b[32]; std::snprintf(b, sizeof(b), "%g", v); StrVars()[Str(it, 0)] = b; }
        }
        else if (op == "str_to_num") {                                                      // text -> number
            try { Vars()[Str(it, 0)] = std::stof(StrVars()[Str(it, 1)]); } catch (...) { Vars()[Str(it, 0)] = 0.0f; }
        }
        else if (op == "str_set_text") {                                                    // put a string var on a Text object
            if (scene) if (GameObject* g = scene->Find(ObjName(it, 0)))
                if (auto* tr = g->GetComponent<TextRenderer>()) tr->text = StrVars()[Str(it, 1)];
        }
        else if (op == "str_upper") { auto& s = StrVars()[Str(it, 0)]; for (char& ch : s) ch = (char)std::toupper((unsigned char)ch); }
        else if (op == "str_lower") { auto& s = StrVars()[Str(it, 0)]; for (char& ch : s) ch = (char)std::tolower((unsigned char)ch); }
        else if (op == "str_len")   { Vars()[Str(it, 0)] = (float)StrVars()[Str(it, 1)].size(); }   // length -> number var
        else if (op == "str_sub")   {                                                       // dest = src[start .. start+count)
            const std::string& src = StrVars()[Str(it, 1)];
            int start = (int)Num(it, 2); if (start < 0) start = 0;
            int count = it.args.size() > 3 ? (int)Num(it, 3) : (int)src.size();
            StrVars()[Str(it, 0)] = (start < (int)src.size() && count > 0) ? src.substr(start, count) : std::string();
        }
        else if (op == "str_trim")  {
            auto& s = StrVars()[Str(it, 0)];
            std::size_t b = s.find_first_not_of(" \t\r\n"), e = s.find_last_not_of(" \t\r\n");
            s = (b == std::string::npos) ? std::string() : s.substr(b, e - b + 1);
        }
        else if (op == "str_replace") {                                                     // replace all `find` with `repl`
            auto& s = StrVars()[Str(it, 0)]; std::string find = Str(it, 1), repl = Str(it, 2);
            if (!find.empty()) for (std::size_t p = s.find(find); p != std::string::npos; p = s.find(find, p + repl.size()))
                s.replace(p, find.size(), repl);
        }
        else if (op == "spawn3") {
            if (scene && ScriptAssetPathOk(Str(it, 0), "spawn3")) {
                GameObject* g = SceneSerializer::InstantiateFromFile(*scene, Str(it, 0), nullptr);
                if (g && g->transform)
                    g->transform->localPosition = {Num(it, 1), Num(it, 2), Num(it, 3)};
            }
        }
        else if (op == "set_timescale") { Time::SetTimeScale(Num(it, 0)); }
        else if (op == "send") {
            if (scene) for (ActionList* a : scene->FindObjectsOfType<ActionList>())
                a->ReceiveMessage(Str(it, 0));
            if (scene) for (VisualScriptComponent* v : scene->FindObjectsOfType<VisualScriptComponent>())
                v->SendEvent(Str(it, 0), {}, gameObject);
        }
        // Events with a payload: the receiver reads `event_value` (number) and
        // `event_text` (text), set just before the message fires.
        // fire_output name [value]: raise a named output on this object's Logic Source, so
        // its I/O links (target, input, delay, once) run. The value replaces each link's parameter.
        else if (op == "fire_output") {
            if (gameObject) for (LogicSource* ls : gameObject->GetComponents<LogicSource>())
                ls->FireOutput(Str(it, 0), it.args.size() > 1 ? Str(it, 1) : std::string());
        }
        else if (op == "send_value") {
            Vars()["event_value"] = Num(it, 1); StrVars()["event_text"].clear();
            if (scene) for (ActionList* a : scene->FindObjectsOfType<ActionList>()) a->ReceiveMessage(Str(it, 0));
            if (scene) for (VisualScriptComponent* v : scene->FindObjectsOfType<VisualScriptComponent>())
                v->SendEvent(Str(it, 0), Num(it, 1), gameObject);
        }
        else if (op == "send_text") {
            StrVars()["event_text"] = Rest(it, 1); Vars()["event_value"] = 0.0f;
            if (scene) for (ActionList* a : scene->FindObjectsOfType<ActionList>()) a->ReceiveMessage(Str(it, 0));
            if (scene) for (VisualScriptComponent* v : scene->FindObjectsOfType<VisualScriptComponent>())
                v->SendEvent(Str(it, 0), Rest(it, 1), gameObject);
        }
        else if (op == "move") { if (t) t->Translate({Num(it, 0), Num(it, 1), Num(it, 2)}); }
        else if (op == "set_pos") { if (t) t->localPosition = {Num(it, 0), Num(it, 1), Num(it, 2)}; }
        else if (op == "rotate") {
            // "rotate 45" (one value) spins in 2D around Z; "rotate x y z" uses the
            // documented straight order. (Older builds cyclically shifted the args.)
            if (t) { if (it.args.size() <= 1) t->Rotate({0, 0, Num(it, 0)});
                     else                     t->Rotate({Num(it, 0), Num(it, 1), Num(it, 2)}); }
        }
        else if (op == "set_scale") { if (t) { float v = Num(it, 0); t->localScale = {v, v, v}; } }
        else if (op == "set_scale3") { if (t) t->localScale = {Num(it, 0), Num(it, 1), Num(it, 2)}; }
        else if (op == "move_toward") {
            if (t) {
                Vec3 p = t->localPosition, tg{Num(it, 0), Num(it, 1), Num(it, 2)};
                t->localPosition = Vec3::MoveTowards(p, tg, Num(it, 3));
            }
        }
        else if (op == "move_to") {   // frame-rate-independent: `speed` is units/second
            if (t) {
                Vec3 p = t->localPosition, tg{Num(it, 0), Num(it, 1), Num(it, 2)};
                t->localPosition = Vec3::MoveTowards(p, tg, Num(it, 3) * Time::DeltaTime());
            }
        }
        else if (op == "rotate_to") {  // rotate toward euler angles at `speed` deg/second
            if (t) {
                Vec3 e = t->localRotation.ToEuler();
                float md = Num(it, 3) * Time::DeltaTime();
                e.x = Mathf::MoveTowardsAngle(e.x, Num(it, 0), md);
                e.y = Mathf::MoveTowardsAngle(e.y, Num(it, 1), md);
                e.z = Mathf::MoveTowardsAngle(e.z, Num(it, 2), md);
                t->localRotation = Quat::Euler(e);
            }
        }
        else if (op == "scale_to") {   // scale toward a target at `speed` units/second
            if (t) {
                Vec3 sc = t->localScale, tg{Num(it, 0), Num(it, 1), Num(it, 2)};
                t->localScale = Vec3::MoveTowards(sc, tg, Num(it, 3) * Time::DeltaTime());
            }
        }
        else if (op == "look_at") {
            Scene* sc2 = GetScene();
            GameObject* g = sc2 ? sc2->Find(ObjName(it, 0)) : nullptr;
            if (g && t && gameObject) {
                Vec3 me = gameObject->transform->Position(), ot = g->transform->Position();
                float deg = std::atan2(ot.y - me.y, ot.x - me.x) * 57.2957795f;
                t->localRotation = Quat::Euler({0, 0, deg});
            }
        }
        else if (op == "set_var") { Vars()[Str(it, 0)] = Num(it, 1); }
        else if (op == "add_var") { Vars()[Str(it, 0)] += Num(it, 1); }
        else if (op == "toggle_var") { float& v = Vars()[Str(it, 0)]; v = (v != 0.0f) ? 0.0f : 1.0f; }  // flip a bool flag
        else if (op == "set_active") {
            // set_active <1|0> [object] — no name => this object; a name => that one.
            bool on = Num(it, 0) != 0.0f;
            std::string name = ObjName(it, 1);   // literal name or "$var"
            if (!name.empty()) { if (scene) if (GameObject* g = scene->Find(name)) g->active = on; }
            else if (gameObject) gameObject->active = on;
        }
        else if (op == "set_color") {
            Color col{Num(it, 0), Num(it, 1), Num(it, 2), it.args.size() > 3 ? Num(it, 3) : 1.0f};
            if (gameObject) {
                if (auto* sr = gameObject->GetComponent<SpriteRenderer>()) sr->color = col;
                if (auto* tr = gameObject->GetComponent<TextRenderer>()) tr->color = col;
                if (auto* mr = gameObject->GetComponent<MeshRenderer>()) mr->color = col;
            }
        }
        else if (op == "set_color_var") {
            // Tint this object from a Color variable (its "<var>.r/.g/.b/.a" components).
            const std::string& cv = Str(it, 0);
            Color col{GetVar(cv + ".r"), GetVar(cv + ".g"), GetVar(cv + ".b"), GetVar(cv + ".a")};
            if (gameObject) {
                if (auto* sr = gameObject->GetComponent<SpriteRenderer>()) sr->color = col;
                if (auto* tr = gameObject->GetComponent<TextRenderer>()) tr->color = col;
                if (auto* mr = gameObject->GetComponent<MeshRenderer>()) mr->color = col;
            }
        }
        else if (op == "velocity") {
            if (gameObject) if (auto* rb = gameObject->GetComponent<Rigidbody2D>())
                rb->velocity = {Num(it, 0), Num(it, 1)};
        }
        else if (op == "impulse") {
            if (gameObject) if (auto* rb = gameObject->GetComponent<Rigidbody2D>())
                rb->AddImpulse({Num(it, 0), Num(it, 1)});
        }
        else if (op == "emit") {
            if (gameObject) if (auto* ps = gameObject->GetComponent<ParticleSystem>())
                ps->Emit(it.args.empty() ? 8 : (int)Num(it, 0));
        }
        else if (op == "play_anim") {
            if (gameObject) if (auto* an = gameObject->GetComponent<SpriteAnimator>()) an->Restart();
        }
        // ---- Character animation (visual scripting / flow-graph nodes) ----
        else if (op == "set_anim" || op == "play_clip" || op == "stop_clip" ||
                 op == "play_layer" || op == "stop_layer" || op == "clip_speed") {
            // The Character may be on this object or a descendant (e.g. the player root
            // with the script on a child). Find the nearest one.
            std::function<Character*(GameObject*)> findCh = [&](GameObject* g) -> Character* {
                if (!g) return nullptr;
                if (auto* c = g->GetComponent<Character>()) return c;
                if (g->transform)
                    for (Transform* ch : g->transform->Children())
                        if (ch) if (Character* c = findCh(ch->gameObject)) return c;
                return nullptr;
            };
            Character* ch = findCh(gameObject);
            if (ch) {
                if (op == "set_anim")        ch->anim = (int)Num(it, 0);
                else if (op == "play_clip")  ch->PlayClip(Str(it, 0));
                else if (op == "stop_clip")  ch->StopClip();
                else if (op == "clip_speed") ch->animSpeed = Num(it, 0);
                else if (op == "stop_layer") ch->StopLayer();
                else if (op == "play_layer") {
                    std::string m = Str(it, 1);
                    std::uint32_t mask = (m == "upper" || m == "upper_body") ? Character::UpperBodyMask()
                                       : (m == "arms" || m.empty())          ? Character::ArmsMask()
                                       : Character::BoneBit(Character::BoneIndex(m));
                    if (mask == 0) mask = Character::ArmsMask();
                    ch->PlayLayer(Str(it, 0), mask);
                }
            }
        }
        else if (op == "play_action") {
            HumanoidActions::Play(gameObject, Str(it, 0), it.args.size() > 1 ? Num(it, 1) : 0.2f);
        }
        else if (op == "die") {
            int dir = 0; Vec3 from{}; const Vec3* hit = nullptr;
            if (it.args.size() >= 3) { from = {Num(it, 0), Num(it, 1), Num(it, 2)}; hit = &from; }
            else if (!it.args.empty()) dir = HumanoidActions::DirFromName(Str(it, 0));
            HumanoidActions::RequestDeath(gameObject, dir, hit);
        }
        else if (op == "revive") HumanoidActions::RequestRevive(gameObject);
        else if (op == "dive") HumanoidActions::RequestDive(gameObject);
        else if (op == "sit") {
            GameObject* seat = nullptr;
            int type = 1;
            if (!it.args.empty()) {
                const std::string s = Str(it, 0);
                const bool num = !s.empty() && (std::isdigit((unsigned char)s[0]) || s[0] == '-' || s[0] == '.');
                if (num) type = (int)Num(it, 0);
                else if (gameObject && gameObject->scene()) {
                    seat = gameObject->scene()->Find(s);
                    if (!seat) {
                        std::string k = s;
                        for (char& c : k) c = (char)std::tolower((unsigned char)c);
                        if (k == "chair" || k == "bench") type = 0;
                        else if (k == "knees") type = 2;
                    }
                }
            }
            HumanoidActions::RequestSit(gameObject, seat, type);
        }
        else if (op == "stand_up") HumanoidActions::RequestStandUp(gameObject);
        else if (op == "interact") HumanoidActions::Interact(gameObject);
        else if (op == "hit_react") {
            int dir = 0; bool heavy = false;
            if (it.args.size() >= 3) {
                dir = HumanoidActions::HitDirFromWorld(gameObject, Vec3{Num(it, 0), Num(it, 1), Num(it, 2)});
                if (it.args.size() >= 4) heavy = Num(it, 3) != 0.0f;
            } else if (!it.args.empty()) {
                dir = HumanoidActions::DirFromName(Str(it, 0));
                if (it.args.size() >= 2) heavy = Num(it, 1) != 0.0f;
            }
            HumanoidActions::HitReact(gameObject, dir, heavy);
        }
        else if (op == "roll")
            HumanoidActions::RequestRoll(gameObject, it.args.empty() ? 0 : HumanoidActions::DirFromName(Str(it, 0)));
        else if (op == "dodge")
            HumanoidActions::RequestDodge(gameObject, it.args.empty() ? 0 : HumanoidActions::DirFromName(Str(it, 0)));
        else if (op == "vault") HumanoidActions::RequestVault(gameObject);
        else if (op == "mantle") HumanoidActions::RequestMantle(gameObject);
        else if (op == "ledge_grab") HumanoidActions::RequestLedgeGrab(gameObject);
        else if (op == "climb_up") HumanoidActions::RequestClimbUp(gameObject);
        else if (op == "let_go") HumanoidActions::RequestLetGo(gameObject);
        else if (op == "shimmy") HumanoidActions::SetShimmy(gameObject, Num(it, 0));
        else if (op == "set_cam") {
            if (scene && scene->mainCamera) {
                Transform* ct = scene->mainCamera->gameObject->transform;
                ct->localPosition.x = Num(it, 0); ct->localPosition.y = Num(it, 1);
                // Optional third value = zoom (2D orthographic size), per the hint.
                if (it.args.size() > 2) { float z = Num(it, 2); if (z > 0.0f) scene->mainCamera->orthographicSize = z; }
            }
        }
        else if (op == "cam_center_on") {   // snap the camera's X/Y onto a named object
            if (scene && scene->mainCamera)
                if (GameObject* g = scene->Find(ObjName(it, 0))) {
                    Vec3 p = g->transform->Position();
                    Transform* ct = scene->mainCamera->gameObject->transform;
                    ct->localPosition.x = p.x; ct->localPosition.y = p.y;
                }
        }
        else if (op == "camera_shake") {   // strength (0..1)
            if (scene && scene->mainCamera) scene->mainCamera->Shake(it.args.empty() ? 0.5f : Num(it, 0));
        }
        else if (op == "camera_zoom_to") {   // zoom [seconds]
            if (scene && scene->mainCamera && !it.args.empty())
                scene->mainCamera->ZoomTo(Num(it, 0), it.args.size() > 1 ? Num(it, 1) : 0.0f);
        }
        else if (op == "set_bg") {
            if (scene && scene->mainCamera)
                scene->mainCamera->backgroundColor = {Num(it, 0), Num(it, 1), Num(it, 2), 1.0f};
        }
        else if (op == "set_light") {
            SceneLight::SetDirection({Num(it, 0), Num(it, 1), Num(it, 2)});
        }
        else return RunMoreOps2(it, op, scene, t);
    return 1;
}

// The chain goes on here: MSVC stops at 128 nested blocks (C1061), and every `else if` is one block deeper.
int ActionList::RunMoreOps2(const Item& it, const std::string& op, Scene* scene, Transform* t) {
        if (op == "set_ambient") { SceneLight::SetAmbient(Num(it, 0)); }
        else if (op == "set_text") {
            // Interpolate {var}/{pref} tokens so you can show a variable's live value:
            //   Set Text  "Score: {score}"   ->  "Score: 42"
            if (gameObject) if (auto* tr = gameObject->GetComponent<TextRenderer>())
                tr->text = UITextBind::Resolve(Rest(it, 0));
        }
        else if (op == "set_text_on") {
            // Set a named object's text label, with {var}/{pref} interpolation.
            if (scene) if (GameObject* g = scene->Find(ObjName(it, 0)))
                if (auto* tr = g->GetComponent<TextRenderer>())
                    tr->text = UITextBind::Resolve(Rest(it, 1));
        }
        else if (op == "set_bar") {
            // Fill a named progress bar from a variable: set_bar <object> <var> [max].
            // value = var / max (clamped 0..1). When [max] is left blank, auto-use the
            // stat's published cap "<var>Max" (e.g. healthMax) so a health bar just works
            // without the designer knowing the number; falls back to 1 if there's none.
            if (scene) if (GameObject* g = scene->Find(ObjName(it, 0))) {
                std::string var = Str(it, 1);
                float mx;
                bool explicitMax = it.args.size() > 2 && !Str(it, 2).empty();
                if (explicitMax) mx = Num(it, 2);
                else { auto& vs = Vars(); auto m = vs.find(var + "Max"); mx = (m != vs.end()) ? m->second : 1.0f; }
                if (mx == 0.0f) mx = 1.0f;
                float frac = Mathf::Clamp01(GetVar(var) / mx);
                if (auto* pb = g->GetComponent<UIProgressBar>())    pb->SetValue(frac);
                if (auto* rp = g->GetComponent<UIRadialProgress>()) rp->SetValue(frac);
            }
        }
        else if (op == "play_sound") {
            if (gameObject) if (auto* au = gameObject->GetComponent<AudioSource>()) au->Play();
        }
        else if (op == "destroy") { if (scene && gameObject) { scene->Destroy(gameObject); return 2; } }
        else if (op == "destroy_obj") {
            if (scene) if (GameObject* g = scene->Find(ObjName(it, 0))) scene->Destroy(g);
        }
        else if (op == "activate")   { if (scene) if (GameObject* g = scene->Find(ObjName(it, 0))) g->active = true; }
        else if (op == "deactivate") { if (scene) if (GameObject* g = scene->Find(ObjName(it, 0))) g->active = false; }
        else if (op == "set_name")   { if (gameObject) gameObject->name = Str(it, 0); }
        else if (op == "destroy_all_tag") {   // clear a whole tag: every bullet, every coin
            if (scene) { std::string tg = Str(it, 0);
                for (auto& up : scene->Objects()) if (up && up->active && up->tag == tg) scene->Destroy(up.get()); }
        }
        else if (op == "activate_all_tag" || op == "deactivate_all_tag") {
            bool on = (op == "activate_all_tag");
            if (scene) { std::string tg = Str(it, 0);
                for (auto& up : scene->Objects()) if (up && up->tag == tg) up->active = on; }
        }
        else if (op == "spawn") {
            if (scene && ScriptAssetPathOk(Str(it, 0), "spawn")) {
                GameObject* g = SceneSerializer::InstantiateFromFile(*scene, Str(it, 0), nullptr);
                if (g && g->transform) g->transform->localPosition = {Num(it, 1), Num(it, 2), Num(it, 3)};
            }
        }
        else if (op == "net_browser_open") ServerBrowser::Shared().windowOpen = true;   // the built-in "Find a server" window
        else if (op == "net_queue_join") {   // Find Match: "mode players" (both optional)
            MatchRequest r; r.mode = Str(it, 0);
            const int n = (int)Num(it, 1); if (n >= 2) r.players = n;
            Matchmaker::Shared().Start(r);
        }
        else if (op == "net_queue_cancel") Matchmaker::Shared().Cancel();
        else if (op == "net_match_start" || op == "net_match_end") {   // a match: scores to 0 / report results
            NetworkManager* n = scene ? scene->FindObjectOfType<NetworkManager>() : nullptr;
            if (n && n->IsServer()) {
                if (op == "net_match_start") n->BeginMatch();
                else {
                    std::vector<std::uint32_t> winners;
                    const std::string w = Str(it, 0);   // optional winner ids: "1 4"
                    std::size_t i = 0;
                    while (i < w.size()) {
                        while (i < w.size() && (w[i] == ' ' || w[i] == ',')) ++i;
                        std::size_t j = i; while (j < w.size() && std::isdigit((unsigned char)w[j])) ++j;
                        if (j > i) winners.push_back((std::uint32_t)std::strtoul(w.substr(i, j - i).c_str(), nullptr, 10));
                        if (j == i) ++j;
                        i = j;
                    }
                    EndMatchAndReport(*n, winners, std::string(), scene->Name());
                }
            }
        }
        else if (op == "net_host" || op == "net_join" || op == "net_send" || op == "net_disconnect" ||
                 op == "net_host_relay" || op == "net_join_relay") {
            if (scene) {
                NetworkManager* n = scene->FindObjectOfType<NetworkManager>();
                if (!n && op != "net_disconnect") {
                    GameObject* netObj = scene->CreateGameObject("__Network");
                    n = netObj->AddComponent<NetworkManager>();
                    if (gameObject) n->SetLocalAvatar(gameObject->transform, '@');
                    Scene* sc = scene;
                    n->SetRemoteFactory([sc](std::uint32_t id, char) {
                        GameObject* g = sc->CreateGameObject("Peer" + std::to_string(id));
                        g->AddComponent<SpriteRenderer>()->color = Color::FromBytes(230, 120, 90);
                        return g;
                    });
                }
                if (n) {
                    if (op == "net_host") n->StartServer((std::uint16_t)Num(it, 0));
                    else if (op == "net_join") n->StartClient(Str(it, 0).empty() ? "127.0.0.1" : Str(it, 0), (std::uint16_t)Num(it, 1));
                    else if (op == "net_send") n->Send(Str(it, 0), Rest(it, 1));
                    else if (op == "net_disconnect") n->Stop();
                    // Relay (NAT traversal): host/join through a shared relay + code, so
                    // players behind routers connect with no port-forwarding.
                    else if (op == "net_host_relay") n->HostViaRelay(Str(it, 0), (std::uint16_t)Num(it, 1), Rest(it, 2));
                    else if (op == "net_join_relay") n->JoinViaRelay(Str(it, 0), (std::uint16_t)Num(it, 1), Rest(it, 2));
                }
            }
        }
        else if (op == "net_set") {
            if (scene) if (NetworkManager* n = scene->FindObjectOfType<NetworkManager>()) n->SetVar(Str(it, 0), Rest(it, 1));
        }
        else if (op == "net_spawn") {
            if (scene) if (NetworkManager* n = scene->FindObjectOfType<NetworkManager>())
                n->Spawn(Str(it, 0), {Num(it, 1), Num(it, 2), Num(it, 3)});
        }
        else if (op == "net_ready") {
            if (scene) if (NetworkManager* n = scene->FindObjectOfType<NetworkManager>())
                n->SetReady(it.args.empty() ? true : Num(it, 0) != 0.0f);
        }
        else if (op == "net_start_match") {
            if (scene) if (NetworkManager* n = scene->FindObjectOfType<NetworkManager>()) n->StartMatch();
        }
        else if (op == "net_send_reliable") {
            if (scene) if (NetworkManager* n = scene->FindObjectOfType<NetworkManager>()) n->SendReliable(Str(it, 0), Rest(it, 1));
        }
        else if (op == "net_kick") {
            if (scene) if (NetworkManager* n = scene->FindObjectOfType<NetworkManager>()) n->Kick((std::uint32_t)Num(it, 0), Rest(it, 1));
        }
        else if (op == "net_spawn_owned") {
            if (scene) if (NetworkManager* n = scene->FindObjectOfType<NetworkManager>())
                n->SpawnOwned(Str(it, 0), {Num(it, 1), Num(it, 2), Num(it, 3)});
        }
        else if (op == "net_despawn") {
            if (scene) if (NetworkManager* n = scene->FindObjectOfType<NetworkManager>()) n->Despawn(Str(it, 0));
        }
        else if (op == "net_chat") {
            if (scene) if (NetworkManager* n = scene->FindObjectOfType<NetworkManager>()) n->Send("chat", Rest(it, 0));
        }
        else if (op == "net_rpc") {
            if (scene) if (NetworkManager* n = scene->FindObjectOfType<NetworkManager>()) n->Rpc(Str(it, 0), Rest(it, 1));
        }
        else if (op == "steam_unlock")   { Steam::Get().UnlockAchievement(Str(it, 0)); Steam::Get().StoreStats(); }
        else if (op == "achievement")     {   // cross-game achievement (local + optional cloud), id [label...]
            Achievements::Unlock(Str(it, 0), Rest(it, 1));
            Steam::Get().UnlockAchievement(Str(it, 0)); Steam::Get().StoreStats();   // also fire Steam if present
        }
        else if (op == "steam_set_stat") { Steam::Get().SetStat(Str(it, 0), Num(it, 1)); Steam::Get().StoreStats(); }
        else if (op == "steam_inc_stat") { Steam::Get().IncrementStat(Str(it, 0), Num(it, 1)); Steam::Get().StoreStats(); }
        // ---- PlayFab (Azure PlayFab) — cloud login, leaderboards, saves ----
        // Blocking HTTP (WinHTTP / our own client, NativeHttp.hpp): run these from one-shot triggers
        // (On Start / On Message), not every frame.
        else if (op == "playfab_login") {   // titleId customId (customId can be $textvar)
            PlayFab& pf = PlayFab::Get();
            pf.Configure(Str(it, 0));
            bool ok = pf.LoginWithCustomID(ObjName(it, 1));
            Vars()["playfab_ok"] = ok ? 1.0f : 0.0f;
            StrVars()["playfab_id"] = pf.PlayFabId();
            if (!ok) { StrVars()["playfab_error"] = pf.LastError(); Log::Warning("[actions] ", pf.LastError()); }
        }
        else if (op == "playfab_name")     { PlayFab::Get().SetDisplayName(Rest(it, 0)); }
        else if (op == "playfab_set_stat") {
            // Value can be a number, or "$var" to publish a game variable (e.g. $score).
            std::string vs = Str(it, 1);
            float v = (vs.size() > 1 && vs[0] == '$') ? GetVar(vs.substr(1)) : Num(it, 1);
            PlayFab::Get().SetStat(Str(it, 0), (int)v);
        }
        else if (op == "playfab_leaderboard") {
            // stat [count] -> pf_count + pf_<rank>_name / pf_<rank>_score variables
            // (show them with UI Text Bind {pf_1_name} {pf_1_score} ...).
            std::vector<PlayFab::Entry> rows;
            if (PlayFab::Get().GetLeaderboard(Str(it, 0), it.args.size() > 1 ? (int)Num(it, 1) : 10, rows)) {
                Vars()["pf_count"] = (float)rows.size();
                for (std::size_t ri = 0; ri < rows.size(); ++ri) {
                    std::string pref = "pf_" + std::to_string(ri + 1);
                    StrVars()[pref + "_name"] = rows[ri].name;
                    Vars()[pref + "_score"]   = (float)rows[ri].value;
                }
            } else Log::Warning("[actions] ", PlayFab::Get().LastError());
        }
        else if (op == "playfab_set_data") { PlayFab::Get().SetUserData(Str(it, 0), Rest(it, 1)); }
        else if (op == "playfab_get_data") {   // key [intoTextVar] (defaults to the key)
            std::string v;
            PlayFab::Get().GetUserData(Str(it, 0), v);
            StrVars()[Str(it, 1).empty() ? Str(it, 0) : Str(it, 1)] = v;
        }
        else if (op == "load_scene") {
            // Action lists are game content: a raw scene path must stay inside the game.
            const std::string path = Str(it, 0);
            if (SceneManager::IndexOf(path) < 0 && !ScriptPathOk(path, "load_scene")) return 2;
            if (scene) scene->RequestLoad(path);
            return 2;
        }
        else if (op == "load_scene_index") { if (scene) SceneManager::LoadScene(*scene, (int)Num(it, 0)); return 2; }
        else if (op == "load_next_scene")  { if (scene) SceneManager::LoadNextScene(*scene); return 2; }
        else if (op == "mul_var") { Vars()[Str(it, 0)] *= Num(it, 1); }
        else if (op == "div_var") { float d = Num(it, 1); if (d != 0.0f) Vars()[Str(it, 0)] /= d; }
        else if (op == "copy_var") { Vars()[Str(it, 0)] = Vars()[Str(it, 1)]; }
        else if (op == "rand_var") { Vars()[Str(it, 0)] = Random::Shared().Range(Num(it, 1), Num(it, 2)); }
        else if (op == "set_prefs") { Prefs::SetFloat(Str(it, 0), Num(it, 1)); }
        else if (op == "add_prefs") { Prefs::SetFloat(Str(it, 0), Prefs::GetFloat(Str(it, 0), 0.0f) + Num(it, 1)); }
        // Read a saved value (e.g. the Health component's "health") into a variable,
        // so a visual script can grab a stat published by another component.
        else if (op == "get_prefs") { Vars()[Str(it, 0)] = Prefs::GetFloat(Str(it, 1), Num(it, 2)); }
        else if (op == "save_prefs") {   // like the scripts' prefs_save: into the game's save folder
            const std::string name = Str(it, 0).empty() ? "game.okayprefs" : Str(it, 0);
            if (ScriptPathOk(name, "save_prefs")) Prefs::Save(Save::Resolve(name));
        }
        // Save slots (Saveable components mark what is stored; problems go to the Console).
        else if (op == "game_save")        { if (scene) GameSaves::Save(*scene, Str(it, 0).empty() ? "slot_1" : Str(it, 0), Rest(it, 1)); }
        else if (op == "game_load")        { if (scene) GameSaves::Load(*scene, Str(it, 0).empty() ? "slot_1" : Str(it, 0)); }
        else if (op == "game_continue")    { if (scene) GameSaves::Continue(*scene); }
        else if (op == "game_autosave")    { if (scene) GameSaves::AutoSave(*scene); }
        else if (op == "game_delete_slot") { GameSaves::Delete(Str(it, 0).empty() ? "slot_1" : Str(it, 0)); }
        else if (op == "game_slot_exists") { Vars()[Str(it, 1).empty() ? "slot_exists" : Str(it, 1)] = GameSaves::Exists(Str(it, 0).empty() ? "slot_1" : Str(it, 0)) ? 1.0f : 0.0f; }
        else if (op == "game_can_continue") { Vars()[Str(it, 0).empty() ? "can_continue" : Str(it, 0)] = GameSaves::CanContinue() ? 1.0f : 0.0f; }
        else if (op == "game_slot_age")    { Vars()[Str(it, 1).empty() ? "slot_age" : Str(it, 1)] = (float)GameSaves::SecondsSinceSave(Str(it, 0).empty() ? "slot_1" : Str(it, 0)); }
        else if (op == "game_slot_text")   { StrVars()[Str(it, 1).empty() ? "slot_text" : Str(it, 1)] = GameSaves::SavedText(Str(it, 0).empty() ? "slot_1" : Str(it, 0)); }
        else if (op == "set_display_mode") {
            WindowMode mode = WindowMode::Windowed;
            if (DisplaySettings::ParseMode(Str(it, 0), mode)) DisplaySettings::SetMode(mode);
        }
        else if (op == "set_display_resolution") DisplaySettings::SetResolution((int)Num(it, 0), (int)Num(it, 1));
        else if (op == "set_refresh_rate") DisplaySettings::SetRefreshHz((int)Num(it, 0));
        else if (op == "set_monitor") {
            const std::string name = Str(it, 0);
            bool digits = !name.empty();
            for (char c : name) if (c < '0' || c > '9') digits = false;
            if (digits) DisplaySettings::SetMonitorIndex(std::atoi(name.c_str()));
            else DisplaySettings::SetMonitorName(name);
        }
        else if (op == "begin_display_confirm") DisplaySettings::BeginTrial(10.f);
        else if (op == "keep_display_settings") DisplaySettings::KeepTrial();
        else if (op == "revert_display_settings") DisplaySettings::RevertTrial();
        else if (op == "get_display_mode") StrVars()[Str(it, 0).empty() ? "display_mode" : Str(it, 0)] = DisplaySettings::ModeName(DisplaySettings::Mode());
        else if (op == "get_monitor") StrVars()[Str(it, 0).empty() ? "monitor" : Str(it, 0)] = DisplaySettings::MonitorName();
        else if (op == "get_refresh_rate") Vars()[Str(it, 0).empty() ? "refresh_rate" : Str(it, 0)] = (float)DisplaySettings::RefreshHz();
        else if (op == "set_tag") { if (gameObject) gameObject->tag = Str(it, 0); }
        else if (op == "set_timescale_var") { Vars()[Str(it, 0)] = Time::TimeScale(); }
        else if (op == "log") { Log::Info("[actions] ", Rest(it, 0)); }
        else if (op == "set_rotation")  { if (t) t->localRotation = Quat::Euler({0, 0, Num(it, 0)}); }
        else if (op == "set_rotation3") { if (t) t->localRotation = Quat::Euler({Num(it, 0), Num(it, 1), Num(it, 2)}); }
        else return RunMoreOps3(it, op, scene, t);
    return 1;
}

// The chain goes on here: MSVC stops at 128 nested blocks (C1061), and every `else if` is one block deeper.
int ActionList::RunMoreOps3(const Item& it, const std::string& op, Scene* scene, Transform* t) {
    const std::vector<Item>& ins = RunList();   // the running handler's instructions (as in Update)
        if (op == "velocity3") {
            if (gameObject) if (auto* rb = gameObject->GetComponent<Rigidbody3D>())
                rb->velocity = {Num(it, 0), Num(it, 1), Num(it, 2)};
        }
        else if (op == "impulse3") {
            if (gameObject) if (auto* rb = gameObject->GetComponent<Rigidbody3D>())
                rb->AddImpulse({Num(it, 0), Num(it, 1), Num(it, 2)});
        }
        else if (op == "force3") {
            if (gameObject) if (auto* rb = gameObject->GetComponent<Rigidbody3D>())
                rb->AddForce({Num(it, 0), Num(it, 1), Num(it, 2)});
        }
        else if (op == "set_sprite") {
            if (gameObject) if (auto* sr = gameObject->GetComponent<SpriteRenderer>()) sr->texture = Str(it, 0);
        }
        else if (op == "set_visible") {
            bool vis = it.args.empty() ? true : Num(it, 0) != 0.0f;
            if (gameObject) {
                if (auto* sr = gameObject->GetComponent<SpriteRenderer>()) sr->enabled = vis;
                if (auto* mr = gameObject->GetComponent<MeshRenderer>())   mr->enabled = vis;
                if (auto* tr = gameObject->GetComponent<TextRenderer>())   tr->enabled = vis;
            }
        }
        else if (op == "call") {                 // call a script event on this object's ScriptComponent
            if (gameObject) if (auto* scc = gameObject->GetComponent<ScriptComponent>())
                if (scc->VM()) scc->VM()->CallEvent(Str(it, 0));
        }
        else if (op == "send_to") {              // message one named object's action lists
            if (scene) if (GameObject* g = scene->Find(ObjName(it, 0)))
{
                for (ActionList* a : g->GetComponents<ActionList>()) a->ReceiveMessage(Str(it, 1));
                for (VisualScriptComponent* v : g->GetComponents<VisualScriptComponent>()) v->SendEvent(Str(it, 1), {}, gameObject);
            }
        }
        else if (op == "raycast") {              // cast a ray, store the result in variables
            // Args: <direction> [distance] [prefix] [h:offset] [ig:tag]. Writes number
            // vars <prefix>_hit (1/0), <prefix>_dist, <prefix>_x/_y/_z (the hit point)
            // and text vars <prefix>_name / <prefix>_tag (what it hit) so later
            // instructions can branch on them (if_goto) or compare the object (str_eq).
            if (scene && gameObject) {
                RaycastHit3D h = ActionRaycast(gameObject, it, 0);
                std::string pre;                       // first non-modifier arg after distance
                for (std::size_t k = 2; k < it.args.size(); ++k)
                    if (!IsRayMod(Str(it, k))) { pre = Str(it, k); break; }
                if (pre.empty()) pre = "ray";
                Vars()[pre + "_hit"]  = h.hit ? 1.0f : 0.0f;
                Vars()[pre + "_dist"] = h.distance;
                Vars()[pre + "_x"] = h.point.x;
                Vars()[pre + "_y"] = h.point.y;
                Vars()[pre + "_z"] = h.point.z;
                Vars()[pre + "_nx"] = h.normal.x;   // surface normal
                Vars()[pre + "_ny"] = h.normal.y;
                Vars()[pre + "_nz"] = h.normal.z;
                StrVars()[pre + "_name"] = h.gameObject ? h.gameObject->name : std::string();
                StrVars()[pre + "_tag"]  = h.gameObject ? h.gameObject->tag  : std::string();
            }
        }
        else if (op == "if_goto") {              // conditional jump: var <op> value -> line or label
            int line = ResolveTarget(Str(it, 3));
            bool pass = CmpPass(Str(it, 1), GetVar(Str(it, 0)), Num(it, 2));
            if (pass && line >= 0 && line < (int)ins.size()) m_ip = (std::size_t)line;
        }
        // ---- More general instructions ----
        else if (op == "toggle_active") { if (gameObject) gameObject->active = !gameObject->active; }
        else if (op == "clamp_var") { float& v = Vars()[Str(it, 0)]; v = Mathf::Clamp(v, Num(it, 1), Num(it, 2)); }
        else if (op == "lerp_var") {                  // step a variable toward a target each run
            float& v = Vars()[Str(it, 0)]; float tg = Num(it, 1), st = Num(it, 2);
            v = (v < tg) ? Mathf::Min(tg, v + st) : Mathf::Max(tg, v - st);
        }
        else if (op == "add_var_var") { Vars()[Str(it, 0)] += Vars()[Str(it, 1)]; }
        // ---- Float math on a variable (PlayMaker "Float Operator" staples) ----
        else if (op == "floor_var")  { Vars()[Str(it, 0)] = std::floor(GetVar(Str(it, 0))); }
        else if (op == "round_var")  { Vars()[Str(it, 0)] = std::round(GetVar(Str(it, 0))); }
        else if (op == "abs_var")    { Vars()[Str(it, 0)] = std::fabs(GetVar(Str(it, 0))); }
        else if (op == "negate_var") { Vars()[Str(it, 0)] = -GetVar(Str(it, 0)); }
        else if (op == "sign_var")   { float v = GetVar(Str(it, 0)); Vars()[Str(it, 0)] = (v > 0.0f) ? 1.0f : ((v < 0.0f) ? -1.0f : 0.0f); }
        else if (op == "sqrt_var")   { float v = GetVar(Str(it, 0)); Vars()[Str(it, 0)] = v > 0.0f ? std::sqrt(v) : 0.0f; }
        else if (op == "pow_var")    { Vars()[Str(it, 0)] = std::pow(GetVar(Str(it, 0)), Num(it, 1)); }
        else if (op == "min_var")    { Vars()[Str(it, 0)] = std::fmin(GetVar(Str(it, 0)), Num(it, 1)); }
        else if (op == "max_var")    { Vars()[Str(it, 0)] = std::fmax(GetVar(Str(it, 0)), Num(it, 1)); }
        else if (op == "mod_var")    { float d = Num(it, 1); if (d != 0.0f) Vars()[Str(it, 0)] = std::fmod(GetVar(Str(it, 0)), d); }
        else if (op == "swap_var")   { float t2 = GetVar(Str(it, 0)); Vars()[Str(it, 0)] = GetVar(Str(it, 1)); Vars()[Str(it, 1)] = t2; }
        else if (op == "clamp01_var"){ float& v = Vars()[Str(it, 0)]; v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
        else if (op == "round_to_var") { float s = Num(it, 1); if (s > 0.0f) Vars()[Str(it, 0)] = std::round(GetVar(Str(it, 0)) / s) * s; }
        else if (op == "percent_var"){ float b = Num(it, 2); Vars()[Str(it, 0)] = (b != 0.0f) ? Num(it, 1) / b * 100.0f : 0.0f; }
        else if (op == "approach_var") {   // linear step toward a target (unlike lerp's ease)
            float& v = Vars()[Str(it, 0)]; float tgt = Num(it, 1), st = std::fabs(Num(it, 2));
            if (std::fabs(tgt - v) <= st) v = tgt; else v += (tgt > v ? st : -st);
        }
        else if (op == "wrap_var") {   // wrap a value into [min, max) — angles, cycling indices
            float lo = Num(it, 1), hi = Num(it, 2), range = hi - lo; float& v = Vars()[Str(it, 0)];
            if (range > 0.0f) v = lo + std::fmod(std::fmod(v - lo, range) + range, range);
        }
        else if (op == "map_range_var") {   // remap value from one range to another (slider -> anything)
            float v = GetVar(Str(it, 0)), a0 = Num(it, 1), a1 = Num(it, 2), b0 = Num(it, 3), b1 = Num(it, 4);
            float tt = (a1 - a0) != 0.0f ? (v - a0) / (a1 - a0) : 0.0f;
            Vars()[Str(it, 0)] = b0 + tt * (b1 - b0);
        }
        else if (op == "dice_var") {   // random whole number 1..sides
            int sides = (int)Num(it, 1); if (sides < 1) sides = 1;
            int roll = 1 + (int)Random::Shared().Range(0.0f, (float)sides);
            if (roll > sides) roll = sides;
            Vars()[Str(it, 0)] = (float)roll;
        }
        else if (op == "random_sign_var") { Vars()[Str(it, 0)] = (Random::Shared().Range(0.0f, 1.0f) < 0.5f) ? -1.0f : 1.0f; }
        else if (op == "pick_var") {   // set to one of the listed values at random
            int n = (int)it.args.size() - 1;
            if (n > 0) { int k = (int)Random::Shared().Range(0.0f, (float)n); if (k >= n) k = n - 1;
                         Vars()[Str(it, 0)] = Num(it, (std::size_t)(1 + k)); }
        }
        else if (op == "spawn_at") {                   // spawn a prefab at a named object's position
            if (scene && ScriptAssetPathOk(Str(it, 0), "spawn_at")) {
                GameObject* g = SceneSerializer::InstantiateFromFile(*scene, Str(it, 0), nullptr);
                if (g && g->transform) if (GameObject* at = scene->Find(ObjName(it, 1)))
                    if (at->transform) g->transform->SetPosition(at->transform->Position());
            }
        }
        else if (op == "set_parent") {                 // parent this object under a named object
            if (t && scene) if (GameObject* g = scene->Find(ObjName(it, 0)))
                if (g->transform) t->SetParent(g->transform);
        }
        else if (op == "unparent") { if (t) t->SetParent(nullptr); }
        else if (op == "pause")  { Time::SetTimeScale(0.0f); }
        else if (op == "resume") { Time::SetTimeScale(1.0f); }
        else if (op == "toggle_pause") { Time::SetTimeScale(Time::TimeScale() <= 0.0001f ? 1.0f : 0.0f); }
        // ---- Survival kit: drive the native survival components ----
        else if (op == "heal")   { InvokeNativeUIAction(gameObject, "Heal",   Num(it, 0)); }
        else if (op == "hurt")   { InvokeNativeUIAction(gameObject, "Damage", Num(it, 0)); }
        else if (op == "eat")    { InvokeNativeUIAction(gameObject, "Eat",    Num(it, 0)); }
        else if (op == "drink")  { InvokeNativeUIAction(gameObject, "Drink",  Num(it, 0)); }
        else if (op == "craft")  { InvokeNativeUIAction(gameObject, "Craft",  Num(it, 0)); }
        else if (op == "survival") { InvokeNativeUIAction(gameObject, Str(it, 0), Num(it, 1)); }
        else if (op == "survival_on") {
            if (scene) if (GameObject* g = scene->Find(ObjName(it, 0)))
                InvokeNativeUIAction(g, Str(it, 1), Num(it, 2));
        }
        else if (op == "use_item") {
            if (gameObject) if (auto* cons = gameObject->GetComponent<Consumables>())
                cons->UseIndex((int)Num(it, 0));
        }
        else if (op == "give") {   // item [count] [object]: as many as fit (on_inventory_full reports the rest)
            if (Inventory* inv = ItemInv(it, gameObject, GetScene())) inv->AddPartial(ItemName(it), ItemCount(it));
        }
        else if (op == "take") {   // item [count] [object]: all or nothing
            Inventory* inv = ItemInv(it, gameObject, GetScene());
            const std::string item = ItemName(it);
            if (inv && inv->Has(item, ItemCount(it))) inv->Remove(item, ItemCount(it));
        }
        // ---- Added ops: single-axis position, game flow, facing, chase/spawn ----
        else if (op == "set_x") { if (t) t->localPosition.x = Num(it, 0); }
        else if (op == "set_y") { if (t) t->localPosition.y = Num(it, 0); }
        else if (op == "set_z") { if (t) t->localPosition.z = Num(it, 0); }
        else if (op == "flip_x") { if (t) t->localScale.x = -t->localScale.x; }   // face the other way (2D)
        else if (op == "flip_y") { if (t) t->localScale.y = -t->localScale.y; }   // flip upside-down (2D)
        else if (op == "add_x") { if (t) t->localPosition.x += Num(it, 0); }
        else if (op == "add_y") { if (t) t->localPosition.y += Num(it, 0); }
        else if (op == "add_z") { if (t) t->localPosition.z += Num(it, 0); }
        else if (op == "reset_rotation") { if (t) t->localRotation = Quat::Euler({0, 0, 0}); }
        else if (op == "reset_scale")    { if (t) t->localScale = {1, 1, 1}; }
        else if (op == "add_score") {                                            // add to a score-like var
            std::string v = Str(it, 0).empty() ? std::string("score") : Str(it, 0);
            Vars()[v] += (it.args.size() > 1 ? Num(it, 1) : 1.0f);
        }
        else if (op == "win")  { Vars()["won"]  = 1.0f; Game::SetPaused(true); }
        else if (op == "lose") { Vars()["lost"] = 1.0f; Game::SetPaused(true); }
        else if (op == "quit") { Game::RequestQuit(); }
        else if (op == "follow" || op == "flee") {          // move toward/away from a named object (use under On Update)
            GameObject* g = scene ? scene->Find(ObjName(it, 0)) : nullptr;
            if (g && g->transform && t && gameObject) {
                Vec3 me = gameObject->transform->Position(), ot = g->transform->Position();
                float dx = ot.x - me.x, dy = ot.y - me.y;
                if (op == "flee") { dx = -dx; dy = -dy; }
                float d = Mathf::Sqrt(dx * dx + dy * dy);
                float speed = Num(it, 1), dtF = Time::DeltaTime();
                if (d > 1e-5f && speed > 0.0f) t->Translate({dx / d * speed * dtF, dy / d * speed * dtF, 0.0f});
            }
        }
        else if (op == "spawn_wave") {                       // ring of prefabs around self
            if (scene && t) {
                int count = (int)Num(it, 1); if (count < 1) count = 1;
                float radius = it.args.size() > 2 ? Num(it, 2) : 3.0f;
                Vec3 c = t->Position();
                for (int i = 0; i < count; ++i) {
                    if (!ScriptAssetPathOk(Str(it, 0), "spawn_wave")) break;
                    GameObject* g = SceneSerializer::InstantiateFromFile(*scene, Str(it, 0), nullptr);
                    if (!g || !g->transform) continue;
                    float ang = (6.2831853f * i) / count;
                    g->transform->localPosition = {c.x + Mathf::Cos(ang) * radius, c.y + Mathf::Sin(ang) * radius, c.z};
                }
            }
        }
        else return 0;
    return 1;
}

} // namespace okay
