#include "OkayScriptVMInternal.hpp"

namespace okay {

void OkayScriptVM::Impl::RegisterDebugBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        // ---- Debugging -------------------------------------------------
        // All of these surface in the editor's Console (it installs a log sink).
        auto joinArgs = [](std::vector<Value>& a, const char* sep = " ") {
            std::string s;
            for (std::size_t i = 0; i < a.size(); ++i) { if (i) s += sep; s += a[i].AsString(); }
            return s;
        };
        b["print"]     = [joinArgs](std::vector<Value>& a) { Log::Info("[script] ", joinArgs(a)); return Value{}; };
        b["debug_log"] = [joinArgs](std::vector<Value>& a) { Log::Info("[script] ", joinArgs(a)); return Value{}; };
        b["log_info"]  = [joinArgs](std::vector<Value>& a) { Log::Info("[script] ", joinArgs(a)); return Value{}; };
        b["log_warn"]  = [joinArgs](std::vector<Value>& a) { Log::Warning("[script] ", joinArgs(a)); return Value{}; };
        b["log_error"] = [joinArgs](std::vector<Value>& a) { Log::Error("[script] ", joinArgs(a)); return Value{}; };
        b["trace"]     = [joinArgs](std::vector<Value>& a) { Log::Trace("[script] ", joinArgs(a)); return Value{}; };
        // watch("hp", hp) -> logs "hp = 42" — quick variable inspection.
        b["watch"] = [](std::vector<Value>& a) {
            if (a.size() >= 2) Log::Info("[watch] ", a[0].AsString(), " = ", a[1].AsString());
            else if (!a.empty()) Log::Info("[watch] ", a[0].AsString());
            return Value{};
        };
        // assert(cond [, "message"]) -> logs an error when cond is falsey.
        b["assert"] = [](std::vector<Value>& a) {
            bool ok = !a.empty() && a[0].AsFloat() != 0.0f;
            if (!ok) Log::Error("[assert] failed", a.size() > 1 ? (": " + a[1].AsString()) : std::string{});
            return Value{ok ? 1.0f : 0.0f};
        };
        // format("hp={} of {}", hp, max) -> fills each {} with the next argument.
        b["format"] = [](std::vector<Value>& a) {
            if (a.empty()) return Value{std::string{}};
            std::string fmt = a[0].AsString(), out;
            std::size_t arg = 1;
            for (std::size_t i = 0; i < fmt.size(); ++i) {
                if (i + 1 < fmt.size() && fmt[i] == '{' && fmt[i + 1] == '}') {
                    out += (arg < a.size()) ? a[arg++].AsString() : "";
                    ++i;
                } else out += fmt[i];
            }
            return Value{out};
        };
        b["concat"] = [joinArgs](std::vector<Value>& a) { return Value{joinArgs(a, "")}; };

}

void OkayScriptVM::Impl::RegisterMotionHelperBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        auto tf = c.tf;
        // ---- High-level one-liners: dt-scaled INTERNALLY so beginners never write
        // "* dt". walk(dx,dy,speed) heads in a direction; spin(degPerSec) rotates
        // smoothly. (WASD control is the existing on_key_move(speed).) Much less code.
        b["walk"] = [this, tf](std::vector<Value>& a) {
            float dx = a.size() > 0 ? a[0].AsFloat() : 0.0f, dy = a.size() > 1 ? a[1].AsFloat() : 0.0f;
            float sp = a.size() > 2 ? a[2].AsFloat() : 1.0f;
            float dt = rt.host ? rt.host->deltaTime : 0.0f;
            float len = Mathf::Sqrt(dx * dx + dy * dy); if (len > 0.0001f) { dx /= len; dy /= len; }
            if (Transform* t = tf()) t->Translate({dx * sp * dt, dy * sp * dt, 0.0f});
            return Value{};
        };
        b["spin"] = [this, tf](std::vector<Value>& a) {
            float dps = a.empty() ? 90.0f : a[0].AsFloat();
            float dt = rt.host ? rt.host->deltaTime : 0.0f;
            if (Transform* t = tf()) t->Rotate({0, 0, dps * dt});
            return Value{};
        };
        // move_to(x, y, speed): walk toward a world POINT, stopping when it arrives.
        // dt-scaled (no "* dt"); great for "go to a spot" without object lookups.
        b["move_to"] = [this, tf](std::vector<Value>& a) {
            Transform* t = tf(); if (!t || !rt.host || a.size() < 2) return Value{};
            float tx = a[0].AsFloat(), ty = a[1].AsFloat(), sp = a.size() > 2 ? a[2].AsFloat() : 3.0f;
            Vec3 me = t->Position();
            float dx = tx - me.x, dy = ty - me.y, len = Mathf::Sqrt(dx * dx + dy * dy);
            float step = sp * rt.host->deltaTime;
            if (len <= step || len < 1e-5f) { t->Translate({dx, dy, 0.0f}); return Value{}; }
            t->Translate({dx / len * step, dy / len * step, 0.0f});
            return Value{};
        };
        // bob(amount, speed): hover up/down around the start height (juice, one line).
        b["bob"] = [this, tf](std::vector<Value>& a) {
            Transform* t = tf(); if (!t || !rt.host) return Value{};
            float amt = a.size() > 0 ? a[0].AsFloat() : 0.5f, sp = a.size() > 1 ? a[1].AsFloat() : 2.0f;
            auto& g = rt.host->globals;
            auto it = g.find("__bob_y");
            float baseY = it != g.end() ? it->second.AsFloat() : t->localPosition.y;
            if (it == g.end()) g["__bob_y"] = Value{baseY};
            t->localPosition.y = baseY + Mathf::Sin(Time::ElapsedTime() * sp) * amt;
            return Value{};
        };
        // pulse(amount, speed): gently grow/shrink around normal size (juice).
        b["pulse"] = [tf](std::vector<Value>& a) {
            Transform* t = tf(); if (!t) return Value{};
            float amt = a.size() > 0 ? a[0].AsFloat() : 0.2f, sp = a.size() > 1 ? a[1].AsFloat() : 3.0f;
            float s = 1.0f + Mathf::Sin(Time::ElapsedTime() * sp) * amt;
            t->localScale = {s, s, s};
            return Value{};
        };
        // wander(speed): roam in a random direction, picking a new one every ~1s.
        b["wander"] = [this, tf](std::vector<Value>& a) {
            Transform* t = tf(); if (!t || !rt.host) return Value{};
            float sp = a.empty() ? 2.0f : a[0].AsFloat();
            auto& g = rt.host->globals;
            float dt = rt.host->deltaTime;
            float tmr = g.count("__wander_t")  ? g["__wander_t"].AsFloat()  : 999.0f;
            float dx  = g.count("__wander_dx") ? g["__wander_dx"].AsFloat() : 0.0f;
            float dy  = g.count("__wander_dy") ? g["__wander_dy"].AsFloat() : 0.0f;
            tmr += dt;
            if (tmr > 1.0f) { tmr = 0.0f; float ang = Random::Shared().Range(0.0f, 6.2831853f); dx = Mathf::Cos(ang); dy = Mathf::Sin(ang); }
            g["__wander_t"] = Value{tmr}; g["__wander_dx"] = Value{dx}; g["__wander_dy"] = Value{dy};
            t->Translate({dx * sp * dt, dy * sp * dt, 0.0f});
            return Value{};
        };
        b["pos_x"] = [tf](std::vector<Value>&) { Transform* t = tf(); return Value{t ? t->localPosition.x : 0.0f}; };
        b["pos_y"] = [tf](std::vector<Value>&) { Transform* t = tf(); return Value{t ? t->localPosition.y : 0.0f}; };
        // time() and frame_count() are in the shared table (Scripting/ScriptApiCore.cpp).
        b["set_timescale"] = [](std::vector<Value>& a) { Time::SetTimeScale(a.empty() ? 1.0f : a[0].AsFloat()); return Value{}; };
        b["timescale"]     = [](std::vector<Value>&) { return Value{Time::TimeScale()}; };
        // has_var("score"): is a script variable defined (in any scope)?
        b["has_var"] = [this](std::vector<Value>& a) { return Value{!a.empty() && rt.Has(a[0].AsString())}; };
        // Time since this Play began: time() and frame_count() keep counting across Plays, these start from 0.
        b["session_time"]          = [](std::vector<Value>&) { return Value{(float)Time::SessionTime()}; };
        b["session_unscaled_time"] = [](std::vector<Value>&) { return Value{(float)Time::SessionUnscaledTime()}; };
        b["session_frame_count"]   = [](std::vector<Value>&) { return Value{(float)Time::SessionFrameCount()}; };
        b["unscaled_dt"]   = [](std::vector<Value>&) { return Value{Time::UnscaledDeltaTime()}; };   // real frame time: a pause, slow motion and hit stops do not change it
        b["is_paused"]     = [](std::vector<Value>&) { return Value{Game::Paused()}; };
        // Pause with a reason of your own (push_pause("dialogue") ... pop_pause("dialogue")): the game stays paused until every
        // reason is let go, so two scripts that pause never resume each other (pause_game(true/false) is the one "manual" reason).
        b["push_pause"]    = [](std::vector<Value>& a) { Game::PushPause(a.empty() ? std::string() : a[0].AsString()); return Value{}; };
        b["pop_pause"]     = [](std::vector<Value>& a) { return Value{Game::PopPause(a.empty() ? std::string() : a[0].AsString())}; };
        b["is_paused_by"]  = [](std::vector<Value>& a) { return Value{Game::IsPausedBy(a.empty() ? std::string() : a[0].AsString())}; };
        b["hitch_count"]   = [](std::vector<Value>&) { return Value{(float)(GameLoop::Current() ? GameLoop::Current()->HitchCount() : 0)}; };
        // hit_stop(seconds [, scale]): a freeze frame (scale 0, the default) or a brief slow-down for an impact; it runs on
        // real time, ends by itself and composes with set_timescale. slow_motion(scale, seconds) is the same effect with
        // the arguments the other way round, for bullet time: slow_motion(0.2, 2) runs at a fifth speed for two seconds.
        b["hit_stop"]      = [](std::vector<Value>& a) {
            if (!a.empty()) Time::HitStop(a[0].AsFloat(), a.size() > 1 ? a[1].AsFloat() : 0.0f);
            return Value{};
        };
        b["slow_motion"]   = [](std::vector<Value>& a) {
            if (a.size() >= 2) Time::HitStop(a[1].AsFloat(), a[0].AsFloat());
            return Value{};
        };
        // ramp_timescale(target, seconds): ease the time scale to target over that many real seconds (a straight line).
        b["ramp_timescale"] = [](std::vector<Value>& a) {
            if (a.size() >= 2) Time::RampTimeScale(a[0].AsFloat(), a[1].AsFloat());
            return Value{};
        };
        b["mouse_wheel"]   = [](std::vector<Value>&) { return Value{Input::MouseWheel()}; };
        b["dt"]    = [this](std::vector<Value>&) { return Value{rt.host ? rt.host->deltaTime : 0.0f}; };
        // Scheduled callbacks: after(seconds, "fn") fires once; every(seconds,
        // "fn") repeats; cancel_timers() clears them. fn is a function in this
        // script. Great for spawn waves, respawns, cooldowns, blinking text.
        b["after"] = [this](std::vector<Value>& a) {
            if (a.size() >= 2) {
                float wait = a[0].AsFloat(); if (!(wait > 0.0f)) wait = 0.0f;   // negative and NaN fire on the next tick
                rt.timers.push_back({wait, 0.0f, a[1].AsString(), false});
            }
            return Value{};
        };
        b["every"] = [this](std::vector<Value>& a) {
            if (a.size() >= 2) {
                float iv = a[0].AsFloat(); if (!(iv > 0.0001f)) iv = 0.0001f;   // zero, negative and NaN: the shortest interval
                rt.timers.push_back({iv, iv, a[1].AsString(), false});
            }
            return Value{};
        };
        b["cancel_timers"] = [this](std::vector<Value>&) { rt.timers.clear(); return Value{}; };
        // timer("name", seconds): returns true about once every `seconds` — kills the
        // manual `t = t + dt; if (t >= N)` boilerplate. e.g.
        //   if (timer("spawn", 2)) { spawn("enemy.okayprefab", 0, 5) }
        b["timer"] = [this](std::vector<Value>& a) -> Value {
            if (!rt.host || a.size() < 2) return Value{false};
            float period = a[1].AsFloat(); if (period <= 0.0f) return Value{true};
            std::string key = "__timer_" + a[0].AsString();
            float acc = rt.host->globals.count(key) ? rt.host->globals[key].AsFloat() : 0.0f;
            acc += rt.host->deltaTime;
            if (acc >= period) { rt.host->globals[key] = Value{acc - period}; return Value{true}; }
            rt.host->globals[key] = Value{acc};
            return Value{false};
        };
        b["axis_x"] = [](std::vector<Value>&) { return Value{Input::AxisWASD().x}; };
        b["axis_y"] = [](std::vector<Value>&) { return Value{Input::AxisWASD().y}; };
}

void OkayScriptVM::Impl::RegisterScoreSpawnBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        auto go = c.go;
        // score()/add_score(n): the shared "score" value in one call (less than
        // set("score", get("score") + n)). set_score(n) overwrites it.
        b["score"] = [this](std::vector<Value>&) -> Value {
            if (!rt.host) return Value{0.0f};
            auto it = rt.host->globals.find("score");
            return it != rt.host->globals.end() ? it->second : Value{0.0f};
        };
        b["add_score"] = [this](std::vector<Value>& a) {
            if (!rt.host) return Value{};
            float n = a.empty() ? 1.0f : a[0].AsFloat();
            auto it = rt.host->globals.find("score");
            float cur = it != rt.host->globals.end() ? it->second.AsFloat() : 0.0f;
            rt.host->globals["score"] = Value{cur + n};
            return Value{cur + n};
        };
        b["set_score"] = [this](std::vector<Value>& a) {
            if (rt.host) rt.host->globals["score"] = Value{a.empty() ? 0.0f : a[0].AsFloat()};
            return Value{};
        };
        // Spawn a prefab file at (x, y); returns true on success. New objects
        // are adopted next frame, so this is safe to call from update().
        b["spawn"] = [this](std::vector<Value>& a) {
            if (a.empty() || !rt.host || !rt.host->gameObject) return Value{false};
            Scene* sc = rt.host->gameObject->scene();
            if (!sc) return Value{false};
            if (!ScriptAssetPathOk(a[0].AsString(), "spawn")) return Value{false};
            GameObject* go = SceneSerializer::InstantiateFromFile(*sc, a[0].AsString(), nullptr);
            if (!go) return Value{false};
            if (go->transform)
                go->transform->localPosition = {a.size() > 1 ? a[1].AsFloat() : 0.0f,
                                                 a.size() > 2 ? a[2].AsFloat() : 0.0f, 0.0f};
            return ObjectRef(go);   // a reference to the new copy (true in an if; prints as its name)
        };
        // spawn3(prefab, x, y, z): like spawn but places the new object in 3D.
        b["spawn3"] = [this](std::vector<Value>& a) {
            if (a.empty() || !rt.host || !rt.host->gameObject) return Value{false};
            Scene* sc = rt.host->gameObject->scene();
            if (!sc) return Value{false};
            if (!ScriptAssetPathOk(a[0].AsString(), "spawn3")) return Value{false};
            GameObject* go = SceneSerializer::InstantiateFromFile(*sc, a[0].AsString(), nullptr);
            if (!go) return Value{false};
            if (go->transform)
                go->transform->localPosition = {a.size() > 1 ? a[1].AsFloat() : 0.0f,
                                                 a.size() > 2 ? a[2].AsFloat() : 0.0f,
                                                 a.size() > 3 ? a[3].AsFloat() : 0.0f};
            return ObjectRef(go);
        };
}

void OkayScriptVM::Impl::RegisterObjectPoolBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        auto go = c.go;
        auto sceneOf = c.sceneOf;
        // ---- Object pools: reuse spawned objects instead of creating and freeing each one ----
        // pool_get("Bullet.okayprefab", x, y [, z]) is spawn / spawn3 taken from this scene's pool for that prefab: an
        // idle object when there is one (reset to a fresh copy of the prefab, its scripts start again), else a new one.
        // Returns true on success. pool_release() hands THIS script's object back to its pool (destroyed when it did not
        // come from one), so a bullet ends with it. pool_prewarm(prefab, n) makes n idle objects ahead of time and
        // returns how many are idle; pool_idle(prefab) says how many are.
        b["pool_get"] = [this](std::vector<Value>& a) {
            if (a.empty() || !rt.host || !rt.host->gameObject) return Value{false};
            Scene* sc = rt.host->gameObject->scene();
            if (!sc) return Value{false};
            if (!ScriptAssetPathOk(a[0].AsString(), "pool_get")) return Value{false};
            ObjectPool* pool = ObjectPool::ForPrefab(*sc, a[0].AsString());
            GameObject* go = pool ? pool->Get() : nullptr;
            if (!go) return Value{false};
            if (go->transform)
                go->transform->localPosition = {a.size() > 1 ? a[1].AsFloat() : 0.0f,
                                                 a.size() > 2 ? a[2].AsFloat() : 0.0f,
                                                 a.size() > 3 ? a[3].AsFloat() : 0.0f};
            return ObjectRef(go);
        };
        b["pool_release"] = [this](std::vector<Value>&) {
            GameObject* go = rt.host ? rt.host->gameObject : nullptr;
            Scene* sc = go ? go->scene() : nullptr;
            return Value{sc ? sc->Despawn(go) : false};
        };
        b["pool_prewarm"] = [this](std::vector<Value>& a) {
            Scene* sc = (rt.host && rt.host->gameObject) ? rt.host->gameObject->scene() : nullptr;
            ObjectPool* pool = (sc && !a.empty() && ScriptAssetPathOk(a[0].AsString(), "pool_prewarm"))
                                   ? ObjectPool::ForPrefab(*sc, a[0].AsString()) : nullptr;
            if (!pool) return Value{0.0f};
            const float n = a.size() > 1 ? a[1].AsFloat() : 1.0f;
            pool->Prewarm(n > 0.0f && n < 100000.0f ? static_cast<int>(n) : 0);
            return Value{static_cast<float>(pool->IdleCount())};
        };
        b["pool_idle"] = [this](std::vector<Value>& a) {
            Scene* sc = (rt.host && rt.host->gameObject) ? rt.host->gameObject->scene() : nullptr;
            ObjectPool* pool = (sc && !a.empty() && ScriptAssetPathOk(a[0].AsString(), "pool_idle"))
                                   ? ObjectPool::ForPrefab(*sc, a[0].AsString()) : nullptr;
            return Value{pool ? static_cast<float>(pool->IdleCount()) : 0.0f};
        };
        // Destroy this script's own GameObject (deferred to end of frame).
        b["destroy"] = [this](std::vector<Value>&) {
            if (rt.host && rt.host->gameObject && rt.host->gameObject->scene())
                rt.host->gameObject->scene()->Destroy(rt.host->gameObject);
            return Value{};
        };
        // Control other objects by name (show/hide UI, toggle enemies, etc.).
        b["activate"] = [sceneOf](std::vector<Value>& a) {
            if (!a.empty()) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0])) g->active = true;
            return Value{};
        };
        b["deactivate"] = [sceneOf](std::vector<Value>& a) {
            if (!a.empty()) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0])) g->active = false;
            return Value{};
        };
        b["exists"] = [sceneOf](std::vector<Value>& a) {
            if (!a.empty()) if (Scene* s = sceneOf()) return Value{ResolveObject(s, a[0]) != nullptr};
            return Value{false};
        };
}

void OkayScriptVM::Impl::RegisterNamedObjectBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        auto sceneOf = c.sceneOf;
        auto findNamed = c.findNamed;
        // ---- Named-object helpers (also what visual-graph code export uses) ----
        // An object reference resolves by its id; a name ("" / "self" = this object)
        // by the first object with that name, as it always did.
        b["position_of"] = [findNamed](std::vector<Value>& a) {
            GameObject* g = findNamed(a.empty() ? Value{std::string()} : a[0]);
            return Value{(g && g->transform) ? g->transform->Position() : Vec3{0, 0, 0}};
        };
        b["tag_of"] = [findNamed](std::vector<Value>& a) {
            GameObject* g = findNamed(a.empty() ? Value{std::string()} : a[0]);
            return Value{g ? g->tag : std::string()};
        };
        b["set_active_of"] = [sceneOf](std::vector<Value>& a) {   // finds inactive objects too
            if (a.empty()) return Value{};
            Scene* s = sceneOf(); if (!s) return Value{};
            std::string n = a[0].AsString();
            for (const auto& up : s->Objects()) if (up && up->name == n) { up->active = a.size() < 2 || a[1].AsBool(); break; }
            return Value{};
        };
        b["set_text_of"] = [findNamed](std::vector<Value>& a) {
            if (GameObject* g = findNamed(a.empty() ? Value{std::string()} : a[0]))
                if (auto* tr = g->GetComponent<TextRenderer>()) tr->text = a.size() > 1 ? a[1].AsString() : std::string();
            return Value{};
        };
        b["move_toward_object"] = [this, findNamed](std::vector<Value>& a) {   // (name, speed) in 2D or 3D
            GameObject* me = rt.host ? rt.host->gameObject : nullptr;
            GameObject* g = findNamed(a.empty() ? Value{std::string()} : a[0]);
            if (!me || !g || g == me || !me->transform || !g->transform) return Value{};
            Vec3 d = g->transform->Position() - me->transform->Position();
            float dist = d.Magnitude(), step = (a.size() > 1 ? a[1].AsFloat() : 2.0f) * rt.host->deltaTime;
            if (dist > 1e-5f) me->transform->SetPosition(me->transform->Position() + d * ((step >= dist ? dist : step) / dist));
            return Value{};
        };
        b["face"] = [this, findNamed](std::vector<Value>& a) {   // turn toward a named object (3D yaw, else 2D Z)
            GameObject* me = rt.host ? rt.host->gameObject : nullptr;
            GameObject* g = findNamed(a.empty() ? Value{std::string()} : a[0]);
            if (!me || !g || g == me || !me->transform || !g->transform) return Value{};
            Vec3 d = g->transform->Position() - me->transform->Position();
            if (std::fabs(d.z) > 1e-4f) me->transform->localRotation = Quat::LookRotation(Vec3{-d.x, 0.0f, -d.z}.Normalized());
            else me->transform->localRotation = Quat::Euler(0, 0, std::atan2(d.y, d.x) * 57.2957795f);
            return Value{};
        };
        b["is_active"] = [sceneOf](std::vector<Value>& a) {
            if (!a.empty()) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0])) return Value{g->active};
            return Value{false};
        };
        // Parent this object under a named one (pick up items, mount turrets on a
        // moving platform); detach() returns it to the scene root. World position
        // is preserved across the change.
        b["set_parent"] = [this, sceneOf](std::vector<Value>& a) {
            if (a.empty() || !rt.host || !rt.host->gameObject) return Value{};
            if (Scene* s = sceneOf())
                if (GameObject* p = ResolveObject(s, a[0]))
                    rt.host->gameObject->transform->SetParent(p->transform, true);
            return Value{};
        };
        b["detach"] = [this](std::vector<Value>&) {
            if (rt.host && rt.host->gameObject)
                rt.host->gameObject->transform->SetParent(nullptr, true);
            return Value{};
        };
        b["has_parent"] = [this](std::vector<Value>&) -> Value {
            return Value{rt.host && rt.host->gameObject &&
                         rt.host->gameObject->transform->Parent() != nullptr};
        };
        // Hierarchy queries by object name: child_count("Ship"), get_child("Ship", 0)
        // (the child's name, "" if out of range) and parent_of("Gun") ("" if none).
        b["child_count"] = [sceneOf](std::vector<Value>& a) -> Value {
            if (!a.empty()) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                return Value{(float)g->transform->ChildCount()};
            return Value{0.0f};
        };
        b["get_child"] = [sceneOf](std::vector<Value>& a) -> Value {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0])) {
                int i = SafeInt(a[1].AsFloat());
                if (i >= 0 && i < g->transform->ChildCount() && g->transform->Children()[i]->gameObject)
                    return Value{g->transform->Children()[i]->gameObject->name};
            }
            return Value{std::string{}};
        };
        b["parent_of"] = [sceneOf](std::vector<Value>& a) -> Value {
            if (!a.empty()) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (Transform* p = g->transform->Parent(); p && p->gameObject) return Value{p->gameObject->name};
            return Value{std::string{}};
        };
        // RPG / survival stats on a named object: hurt("Orc", 12) (defense-mitigated,
        // fires on_death), heal, add_xp (fires on_level_up), get_hp/get_max_hp.
        // The order is the one scripts always had: a Stats component first, then SurvivalStats;
        // only an object with neither goes on to a Health Stat or an NPC (the one damage path).
        b["hurt"] = [sceneOf](std::vector<Value>& a) -> Value {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0])) {
                // hurt("Orc", 12) or hurt("Orc", 12, "Fire"): the optional third word is the damage kind.
                DamageType ty = DamageType::Generic;
                if (a.size() >= 3 && a[2].IsString()) DamageTypeFromName(a[2].AsString(), ty);
                const DamageEvent ev = DamageEvent::Make(a[1].AsFloat(), ty);
                if (auto* st = g->GetComponent<Stats>()) { st->TakeDamage(ev); return Value{true}; }
                if (auto* sv = g->GetComponent<SurvivalStats>()) { sv->Damage(ev); return Value{true}; }
                float hh = 0.0f, mm = 0.0f;
                if (HealthOf(g, hh, mm)) { DealDamage(g, ev); return Value{true}; }
            }
            return Value{false};
        };
        b["heal"] = [sceneOf](std::vector<Value>& a) -> Value {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0])) {
                if (auto* st = g->GetComponent<Stats>()) { st->Heal(a[1].AsFloat()); return Value{true}; }
                if (auto* sv = g->GetComponent<SurvivalStats>()) { sv->Heal(a[1].AsFloat()); return Value{true}; }
                float hh = 0.0f, mm = 0.0f;
                if (HealthOf(g, hh, mm)) { HealObject(g, a[1].AsFloat()); return Value{true}; }
            }
            return Value{false};
        };
        b["add_xp"] = [sceneOf](std::vector<Value>& a) -> Value {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* st = g->GetComponent<Stats>()) { st->AddXP(SafeInt(a[1].AsFloat())); return Value{true}; }
            return Value{false};
        };
        b["get_hp"] = [sceneOf](std::vector<Value>& a) -> Value {
            if (!a.empty()) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0])) {
                if (auto* st = g->GetComponent<Stats>()) return Value{st->health};
                if (auto* sv = g->GetComponent<SurvivalStats>()) return Value{sv->health};
                float hh = 0.0f, mm = 0.0f;
                if (HealthOf(g, hh, mm)) return Value{hh};
            }
            return Value{0.0f};
        };
        b["get_max_hp"] = [sceneOf](std::vector<Value>& a) -> Value {
            if (!a.empty()) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0])) {
                if (auto* st = g->GetComponent<Stats>()) return Value{st->maxHealth};
                if (auto* sv = g->GetComponent<SurvivalStats>()) return Value{sv->maxHealth};
                float hh = 0.0f, mm = 0.0f;
                if (HealthOf(g, hh, mm)) return Value{mm};
            }
            return Value{0.0f};
        };
        // Read another object's world position by name (enemy AI chasing the
        // player, doors tracking a key, cameras following a target…).
        b["obj_x"] = [sceneOf](std::vector<Value>& a) -> Value {
            if (!a.empty()) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                return Value{g->transform->Position().x};
            return Value{0.0f};
        };
        b["obj_y"] = [sceneOf](std::vector<Value>& a) -> Value {
            if (!a.empty()) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                return Value{g->transform->Position().y};
            return Value{0.0f};
        };
        b["obj_z"] = [sceneOf](std::vector<Value>& a) -> Value {
            if (!a.empty()) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                return Value{g->transform->Position().z};
            return Value{0.0f};
        };
}

void OkayScriptVM::Impl::RegisterNpcBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        auto sceneOf = c.sceneOf;
        // ---- NPC commands: order an NPC Controller around from a script -----
        // npc_goto("Guard", x, y, z): send the named NPC to a world point (it
        // pathfinds there if pathfinding is on, then broadcasts npc_arrived).
        // Omit the name ("" or self name) to command a sibling NPCController.
        auto npcOf = [this, sceneOf](const std::string& name) -> NPCController* {
            if (name.empty())
                return (rt.host && rt.host->gameObject) ? rt.host->gameObject->GetComponent<NPCController>() : nullptr;
            Scene* s = sceneOf(); if (!s) return nullptr;
            GameObject* g = s->Find(name);
            return g ? g->GetComponent<NPCController>() : nullptr;
        };
        b["npc_goto"] = [npcOf](std::vector<Value>& a) -> Value {
            if (a.size() < 4) return Value{false};
            NPCController* n = npcOf(a[0].AsString()); if (!n) return Value{false};
            n->CommandGoTo({a[1].AsFloat(), a[2].AsFloat(), a[3].AsFloat()});
            return Value{true};
        };
        b["npc_stop"] = [npcOf](std::vector<Value>& a) -> Value {
            NPCController* n = npcOf(a.empty() ? std::string{} : a[0].AsString());
            if (!n) return Value{false};
            n->CancelCommand(); return Value{true};
        };
        // True while the NPC is still walking to its npc_goto point.
        b["npc_busy"] = [npcOf](std::vector<Value>& a) -> Value {
            NPCController* n = npcOf(a.empty() ? std::string{} : a[0].AsString());
            return Value{n && n->Commanded()};
        };
        // Current AI state name: "Idle","Wander","Patrol","Follow","Flee",
        // "Chase","Search","Return" ("" if the object has no NPC Controller).
        b["npc_state"] = [npcOf](std::vector<Value>& a) -> Value {
            NPCController* n = npcOf(a.empty() ? std::string{} : a[0].AsString());
            return Value{std::string{n ? n->StateName() : ""}};
        };
}

void OkayScriptVM::Impl::RegisterSpawnerBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        auto sceneOf = c.sceneOf;
        // ---- Spawner control: run enemy/pickup waves from a script ----------
        // Omit the name ("" ) to drive a sibling Spawner on this object.
        auto spawnerOf = [this, sceneOf](const std::string& name) -> Spawner* {
            if (name.empty())
                return (rt.host && rt.host->gameObject) ? rt.host->gameObject->GetComponent<Spawner>() : nullptr;
            Scene* s = sceneOf(); if (!s) return nullptr;
            GameObject* g = s->Find(name);
            return g ? g->GetComponent<Spawner>() : nullptr;
        };
        b["spawner_start"] = [spawnerOf](std::vector<Value>& a) -> Value {
            Spawner* s = spawnerOf(a.empty() ? std::string{} : a[0].AsString());
            if (!s) return Value{false};
            s->StartWaves(); return Value{true};
        };
        b["spawner_stop"] = [spawnerOf](std::vector<Value>& a) -> Value {
            Spawner* s = spawnerOf(a.empty() ? std::string{} : a[0].AsString());
            if (!s) return Value{false};
            s->StopWaves(); return Value{true};
        };
        // Live objects a spawner created (wave HUDs: "3 enemies left").
        b["spawner_alive"] = [spawnerOf](std::vector<Value>& a) -> Value {
            Spawner* s = spawnerOf(a.empty() ? std::string{} : a[0].AsString());
            return Value{(float)(s ? s->AliveCount() : 0)};
        };
}

void OkayScriptVM::Impl::RegisterBuildModeBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        auto tf = c.tf;
        auto sceneOf = c.sceneOf;
        // ---- In-game build mode + logic links (docs/gameplay/build_mode.md) -----------
        // They drive the scene's Build Mode component (the player's).
        auto buildOf = [sceneOf]() -> BuildMode* {
            Scene* s = sceneOf();
            return s ? s->FindObjectOfType<BuildMode>() : nullptr;
        };
        auto argStr = [](std::vector<Value>& a, std::size_t i) { return a.size() > i ? a[i].AsString() : std::string{}; };
        b["build_enter"] = [buildOf](std::vector<Value>&) -> Value {
            BuildMode* m = buildOf(); if (!m) return Value{false};
            m->SetActive(true); return Value{true};
        };
        b["build_exit"] = [buildOf](std::vector<Value>&) -> Value {
            BuildMode* m = buildOf(); if (!m) return Value{false};
            m->SetActive(false); return Value{true};
        };
        b["build_toggle"] = [buildOf](std::vector<Value>&) -> Value {
            BuildMode* m = buildOf(); if (!m) return Value{false};
            m->SetActive(!m->Active()); return Value{m->Active()};
        };
        b["build_active"] = [buildOf](std::vector<Value>&) -> Value {
            BuildMode* m = buildOf(); return Value{m && m->Active()};
        };
        // build_link_mode(on): link mode on / off (no argument = toggle).
        b["build_link_mode"] = [buildOf](std::vector<Value>& a) -> Value {
            BuildMode* m = buildOf(); if (!m) return Value{false};
            m->SetLinkMode(a.empty() ? !m->LinkMode() : a[0].AsBool()); return Value{m->LinkMode()};
        };
        b["build_palette_clear"] = [buildOf](std::vector<Value>&) -> Value {
            BuildMode* m = buildOf(); if (!m) return Value{false};
            m->ClearPalette(); return Value{true};
        };
        // build_palette_add(name, prefab, icon): returns the new slot (0-based), -1 without Build Mode.
        b["build_palette_add"] = [buildOf, argStr](std::vector<Value>& a) -> Value {
            BuildMode* m = buildOf(); if (!m) return Value{-1.0f};
            return Value{(float)m->AddToPalette(argStr(a, 0), argStr(a, 1), argStr(a, 2))};
        };
        b["build_palette_select"] = [buildOf](std::vector<Value>& a) -> Value {
            BuildMode* m = buildOf(); if (!m || a.empty()) return Value{false};
            m->SelectSlot(SafeInt(a[0].AsFloat())); return Value{true};
        };
        b["build_selected"] = [buildOf](std::vector<Value>&) -> Value {
            BuildMode* m = buildOf(); return Value{(float)(m ? m->SelectedSlot() : -1)};
        };
        // Creations: save / load what players placed (a name in the save folder's
        // "creations" folder, or a path). Work without Build Mode too.
        b["build_save"] = [buildOf, sceneOf, argStr](std::vector<Value>& a) -> Value {
            std::string name = argStr(a, 0);
            if (!name.empty() && !ScriptPathOk(name, "build_save")) return Value{false};
            if (BuildMode* m = buildOf()) return Value{m->SaveCreation(name)};
            Scene* s = sceneOf(); if (!s) return Value{false};
            return Value{BuildCreation::Save(*s, BuildMode::CreationPath(name))};
        };
        b["build_load"] = [buildOf, sceneOf, argStr](std::vector<Value>& a) -> Value {
            std::string name = argStr(a, 0);
            if (!name.empty() && !ScriptPathOk(name, "build_load")) return Value{false};
            if (BuildMode* m = buildOf()) return Value{m->LoadCreation(name)};
            Scene* s = sceneOf(); if (!s) return Value{false};
            return Value{BuildCreation::Load(*s, BuildMode::CreationPath(name))};
        };
        b["build_exists"] = [buildOf, argStr](std::vector<Value>& a) -> Value {
            if (!argStr(a, 0).empty() && !ScriptPathOk(argStr(a, 0), "build_exists")) return Value{false};
            BuildMode* m = buildOf();
            std::ifstream f(BuildMode::CreationPath(argStr(a, 0), m ? m->creationFolder : "creations"));
            return Value{(bool)f};
        };
        b["build_clear"] = [buildOf, sceneOf](std::vector<Value>&) -> Value {
            if (BuildMode* m = buildOf()) { m->ClearCreation(); return Value{true}; }
            Scene* s = sceneOf(); if (!s) return Value{false};
            BuildCreation::Clear(*s); return Value{true};
        };
        b["build_undo"] = [buildOf](std::vector<Value>&) -> Value { BuildMode* m = buildOf(); return Value{m && m->Undo()}; };
        b["build_redo"] = [buildOf](std::vector<Value>&) -> Value { BuildMode* m = buildOf(); return Value{m && m->Redo()}; };
        // Objects players have placed.
        b["build_count"] = [sceneOf](std::vector<Value>&) -> Value {
            Scene* s = sceneOf(); return Value{(float)(s ? BuildCreation::PlacedRoots(*s).size() : 0)};
        };
        // logic_press(name): press a Logic Source button (any source: flip its output).
        b["logic_press"] = [this, sceneOf, argStr](std::vector<Value>& a) -> Value {
            Scene* s = sceneOf(); if (!s) return Value{false};
            std::string n = argStr(a, 0);
            GameObject* g = n.empty() ? (rt.host ? rt.host->gameObject : nullptr) : s->Find(n);
            LogicSource* ls = g ? g->GetComponent<LogicSource>() : nullptr;
            if (!ls) return Value{false};
            if (ls->kind == LogicSource::Kind::Button) ls->Press(); else ls->SetOutput(!ls->Output());
            return Value{true};
        };
        // logic_state(name): a source's output, or a target's on / off state.
        b["logic_state"] = [this, sceneOf, argStr](std::vector<Value>& a) -> Value {
            Scene* s = sceneOf(); if (!s) return Value{false};
            std::string n = argStr(a, 0);
            GameObject* g = n.empty() ? (rt.host ? rt.host->gameObject : nullptr) : s->Find(n);
            if (!g) return Value{false};
            if (auto* lt = g->GetComponent<LogicTarget>()) return Value{lt->State()};
            if (auto* ls = g->GetComponent<LogicSource>()) return Value{ls->Output()};
            return Value{false};
        };
        // Distance from this object to a named object (0 if missing).
        b["dist_to"] = [this, sceneOf](std::vector<Value>& a) -> Value {
            if (a.empty() || !rt.host || !rt.host->gameObject) return Value{0.0f};
            Scene* s = sceneOf(); if (!s) return Value{0.0f};
            GameObject* g = ResolveObject(s, a[0]); if (!g) return Value{0.0f};
            Vec3 me = rt.host->gameObject->transform->Position();
            Vec3 ot = g->transform->Position();
            float dx = ot.x - me.x, dy = ot.y - me.y;
            return Value{Mathf::Sqrt(dx * dx + dy * dy)};
        };
        // Set this object's Rigidbody2D velocity to head toward a named object at
        // `speed` (homing projectiles, flying/seeking enemies).
        b["vel_toward"] = [this, sceneOf](std::vector<Value>& a) {
            if (a.empty() || !rt.host || !rt.host->gameObject) return Value{};
            Scene* s = sceneOf(); if (!s) return Value{};
            GameObject* g = ResolveObject(s, a[0]); if (!g) return Value{};
            auto* rb = rt.host->gameObject->GetComponent<Rigidbody2D>(); if (!rb) return Value{};
            Vec3 me = rt.host->gameObject->transform->Position();
            Vec3 ot = g->transform->Position();
            float dx = ot.x - me.x, dy = ot.y - me.y;
            float d = Mathf::Sqrt(dx * dx + dy * dy);
            float speed = a.size() > 1 ? a[1].AsFloat() : 1.0f;
            if (d > 1e-6f) rb->velocity = {dx / d * speed, dy / d * speed};
            return Value{};
        };
        // Rotate this object (about Z) to face a named object — turrets, enemies
        // aiming at the player, signposts.
        b["look_at"] = [this, tf, sceneOf](std::vector<Value>& a) {
            if (a.empty()) return Value{};
            Transform* t = tf(); Scene* s = sceneOf();
            if (!t || !s) return Value{};
            GameObject* g = ResolveObject(s, a[0]); if (!g) return Value{};
            Vec3 me = rt.host->gameObject->transform->Position();
            Vec3 ot = g->transform->Position();
            float deg = std::atan2(ot.y - me.y, ot.x - me.x) * 57.2957795f;
            t->localRotation = Quat::Euler({0, 0, deg});
            return Value{};
        };
        // aim(x, y): rotate (Z) to face a world POINT — turrets, arrows, "look where
        // I'm going". Like look_at but toward coordinates instead of a named object.
        b["aim"] = [this, tf](std::vector<Value>& a) {
            if (a.size() < 2) return Value{};
            Transform* t = tf(); if (!t || !rt.host || !rt.host->gameObject) return Value{};
            Vec3 me = rt.host->gameObject->transform->Position();
            float deg = std::atan2(a[1].AsFloat() - me.y, a[0].AsFloat() - me.x) * 57.2957795f;
            t->localRotation = Quat::Euler({0, 0, deg});
            return Value{};
        };
        // grid_snap(size): snap this object's position to the nearest multiple of
        // `size` on X and Y — tile placement, building games, chunky movement.
        b["grid_snap"] = [tf](std::vector<Value>& a) {
            Transform* t = tf(); if (!t) return Value{};
            float g = a.empty() ? 1.0f : a[0].AsFloat();
            if (g <= 1e-6f) return Value{};
            t->localPosition.x = std::round(t->localPosition.x / g) * g;
            t->localPosition.y = std::round(t->localPosition.y / g) * g;
            return Value{};
        };

}

void OkayScriptVM::Impl::RegisterMovementHelperBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        auto tf = c.tf;
        auto go = c.go;
        auto sceneOf = c.sceneOf;
        // ---- High-level one-liners -----------------------------------------
        // Each is a whole behaviour in a single call, meant to be invoked every
        // frame from update(): the "write less, do more" layer over the low-level
        // move/spawn/input builtins.
        b["follow"] = [this, tf, sceneOf](std::vector<Value>& a) {
            // follow("name", speed[, stopDist]): chase a named object on the XY plane.
            if (a.empty() || !rt.host) return Value{};
            Transform* t = tf(); Scene* s = sceneOf();
            if (!t || !s) return Value{};
            GameObject* g = ResolveObject(s, a[0]); if (!g || !g->transform) return Value{};
            float speed = a.size() > 1 ? a[1].AsFloat() : 3.0f;
            float stop  = a.size() > 2 ? a[2].AsFloat() : 0.0f;
            Vec3 me = t->Position(), ot = g->transform->Position();
            float dx = ot.x - me.x, dy = ot.y - me.y;
            float dist = Mathf::Sqrt(dx * dx + dy * dy);
            if (dist > stop && dist > 1e-5f) {
                float step = Mathf::Min(speed * rt.host->deltaTime, dist - stop);
                t->Translate({dx / dist * step, dy / dist * step, 0.0f});
            }
            return Value{};
        };
        b["follow3"] = [this, tf, sceneOf](std::vector<Value>& a) {
            // follow3("name", speed[, stopDist]): chase a named object in full 3D.
            if (a.empty() || !rt.host) return Value{};
            Transform* t = tf(); Scene* s = sceneOf();
            if (!t || !s) return Value{};
            GameObject* g = ResolveObject(s, a[0]); if (!g || !g->transform) return Value{};
            float speed = a.size() > 1 ? a[1].AsFloat() : 3.0f;
            float stop  = a.size() > 2 ? a[2].AsFloat() : 0.0f;
            Vec3 d = g->transform->Position() - t->Position();
            float dist = d.Magnitude();
            if (dist > stop && dist > 1e-5f) {
                float step = Mathf::Min(speed * rt.host->deltaTime, dist - stop);
                t->Translate(d * (step / dist));
            }
            return Value{};
        };
        b["flee"] = [this, tf, sceneOf](std::vector<Value>& a) {
            // flee("name", speed): run directly away from a named object (XY plane).
            if (a.empty() || !rt.host) return Value{};
            Transform* t = tf(); Scene* s = sceneOf();
            if (!t || !s) return Value{};
            GameObject* g = ResolveObject(s, a[0]); if (!g || !g->transform) return Value{};
            float speed = a.size() > 1 ? a[1].AsFloat() : 3.0f;
            Vec3 me = t->Position(), ot = g->transform->Position();
            float dx = me.x - ot.x, dy = me.y - ot.y;
            float dist = Mathf::Sqrt(dx * dx + dy * dy);
            if (dist > 1e-5f) {
                float k = speed * rt.host->deltaTime / dist;
                t->Translate({dx * k, dy * k, 0.0f});
            }
            return Value{};
        };
        // platformer(speed[, jump]): a whole 2D side-scroller controller in one line.
        // A/D or Left/Right set horizontal velocity; W / Up jumps (edge-triggered).
        // Drives a Rigidbody2D if present (so gravity/ground work), else slides the
        // Transform horizontally. dt-scaled — no `* dt` needed.
        b["platformer"] = [this, tf](std::vector<Value>& a) {
            if (!rt.host || !rt.host->gameObject) return Value{};
            float speed = a.size() > 0 ? a[0].AsFloat() : 5.0f;
            float jump  = a.size() > 1 ? a[1].AsFloat() : 9.0f;
            Vec2 ax = Input::AxisWASD();
            bool upNow = ax.y > 0.5f;
            auto& g = rt.host->globals;
            bool upWas = g.count("__plat_up") && g["__plat_up"].AsFloat() != 0.0f;
            g["__plat_up"] = Value{upNow ? 1.0f : 0.0f};
            if (auto* rb = rt.host->gameObject->GetComponent<Rigidbody2D>()) {
                rb->velocity.x = ax.x * speed;
                if (upNow && !upWas) rb->velocity.y = jump;      // rising edge = one jump
            } else if (Transform* t = tf()) {
                t->Translate({ax.x * speed * rt.host->deltaTime, 0.0f, 0.0f});
            }
            return Value{};
        };
        // on_key("key", "fn"): call this script's function `fn` once, the frame `key`
        // is pressed. Event-style input with no if/edge bookkeeping.
        b["on_key"] = [this](std::vector<Value>& a) {
            if (a.size() < 2) return Value{};
            char c = KeyCharFromName(a[0].AsString());
            if (c != 0 && Input::GetKeyDown(c)) {
                std::string fn = a[1].AsString();
                if (rt.functions.count(fn) || rt.builtins.count(fn)) { std::vector<Value> none; rt.Call(fn, none); }
            }
            return Value{};
        };
        // smooth_follow("name", speed): chase a named object, but ease in (exponential
        // smoothing) so it glides instead of tracking rigidly. dt-scaled.
        b["smooth_follow"] = [this, tf, sceneOf](std::vector<Value>& a) {
            if (a.empty() || !rt.host) return Value{};
            Transform* t = tf(); Scene* s = sceneOf();
            if (!t || !s) return Value{};
            GameObject* g = ResolveObject(s, a[0]); if (!g || !g->transform) return Value{};
            float speed = a.size() > 1 ? a[1].AsFloat() : 4.0f;
            float k = 1.0f - std::exp(-speed * rt.host->deltaTime);   // 0..1 ease factor
            Vec3 me = t->Position(), ot = g->transform->Position();
            t->Translate({(ot.x - me.x) * k, (ot.y - me.y) * k, 0.0f});
            return Value{};
        };
        // spring_to(x, y[, speed]): ease toward a world POINT with the same smoothing,
        // slowing as it arrives. Great for cameras, cursors, snapping. dt-scaled.
        b["spring_to"] = [this, tf](std::vector<Value>& a) {
            if (a.size() < 2 || !rt.host) return Value{};
            Transform* t = tf(); if (!t) return Value{};
            float tx = a[0].AsFloat(), ty = a[1].AsFloat(), sp = a.size() > 2 ? a[2].AsFloat() : 6.0f;
            float k = 1.0f - std::exp(-sp * rt.host->deltaTime);
            Vec3 me = t->Position();
            t->Translate({(tx - me.x) * k, (ty - me.y) * k, 0.0f});
            return Value{};
        };
        b["patrol"] = [this, tf](std::vector<Value>& a) {
            // patrol(x1, y1, x2, y2, speed): walk back and forth between two points.
            // Uses a per-script time accumulator so it advances with update()'s dt.
            if (a.size() < 5 || !rt.host) return Value{};
            Transform* t = tf(); if (!t) return Value{};
            float x1 = a[0].AsFloat(), y1 = a[1].AsFloat(), x2 = a[2].AsFloat(), y2 = a[3].AsFloat(), speed = a[4].AsFloat();
            float dx = x2 - x1, dy = y2 - y1; float len = Mathf::Sqrt(dx * dx + dy * dy);
            if (len < 1e-4f || speed <= 0.0f) return Value{};
            auto& g = rt.host->globals;
            float acc = 0.0f;
            auto it = g.find("__patrol_t"); if (it != g.end()) acc = it->second.AsFloat();
            acc += rt.host->deltaTime;
            g["__patrol_t"] = Value{acc};
            float period = 2.0f * len / speed;                             // full round trip
            float ph = std::fmod(acc, period) / (period * 0.5f);           // 0..2
            float f = ph < 1.0f ? ph : 2.0f - ph;                          // triangle 0..1..0
            Vec3 me = t->Position();
            t->SetPosition({x1 + dx * f, y1 + dy * f, me.z});
            return Value{};
        };
        b["orbit"] = [this, tf, sceneOf](std::vector<Value>& a) {
            // orbit("name", radius, degPerSec): circle a target each frame.
            if (a.empty() || !rt.host) return Value{};
            Transform* t = tf(); Scene* s = sceneOf();
            if (!t || !s) return Value{};
            GameObject* g = ResolveObject(s, a[0]); if (!g || !g->transform) return Value{};
            float radius = a.size() > 1 ? a[1].AsFloat() : 2.0f;
            float spd    = a.size() > 2 ? a[2].AsFloat() : 90.0f;
            Vec3 c = g->transform->Position(), me = t->Position();
            float ang = std::atan2(me.y - c.y, me.x - c.x) + spd * 0.01745329f * rt.host->deltaTime;
            t->SetPosition({c.x + Mathf::Cos(ang) * radius, c.y + Mathf::Sin(ang) * radius, me.z});
            return Value{};
        };
        b["on_key_move"] = [this, tf](std::vector<Value>& a) {
            // on_key_move(speed): WASD/arrow keys move this object on the XY plane
            // (drives a Rigidbody2D's velocity if present, else the Transform).
            Transform* t = tf(); if (!t || !rt.host) return Value{};
            float speed = a.empty() ? 5.0f : a[0].AsFloat();
            Vec2 ax = Input::AxisWASD();
            float len = Mathf::Sqrt(ax.x * ax.x + ax.y * ax.y);
            if (len > 1.0f) { ax.x /= len; ax.y /= len; }
            GameObject* go = rt.host->gameObject;
            if (auto* rb = go ? go->GetComponent<Rigidbody2D>() : nullptr)
                rb->velocity = {ax.x * speed, ax.y * speed};
            else
                t->Translate({ax.x * speed * rt.host->deltaTime, ax.y * speed * rt.host->deltaTime, 0.0f});
            return Value{};
        };
        b["on_key_move3"] = [this, tf](std::vector<Value>& a) {
            // on_key_move3(speed): WASD/arrow keys move this object on the XZ ground
            // plane (drives a Rigidbody3D's velocity if present, else the Transform).
            Transform* t = tf(); if (!t || !rt.host) return Value{};
            float speed = a.empty() ? 5.0f : a[0].AsFloat();
            Vec2 ax = Input::AxisWASD();
            float len = Mathf::Sqrt(ax.x * ax.x + ax.y * ax.y);
            if (len > 1.0f) { ax.x /= len; ax.y /= len; }
            GameObject* go = rt.host->gameObject;
            if (auto* rb = go ? go->GetComponent<Rigidbody3D>() : nullptr)
                { rb->velocity.x = ax.x * speed; rb->velocity.z = ax.y * speed; }
            else
                t->Translate({ax.x * speed * rt.host->deltaTime, 0.0f, ax.y * speed * rt.host->deltaTime});
            return Value{};
        };
        b["shoot_at"] = [this, tf, sceneOf](std::vector<Value>& a) {
            // shoot_at("target", "prefab", speed): spawn a projectile at self flying
            // toward the target (sets the new object's Rigidbody velocity).
            if (a.size() < 2 || !rt.host || !rt.host->gameObject) return Value{false};
            Scene* s = sceneOf(); if (!s) return Value{false};
            GameObject* tgt = ResolveObject(s, a[0]); if (!tgt || !tgt->transform) return Value{false};
            Vec3 me = rt.host->gameObject->transform->Position();
            if (!ScriptAssetPathOk(a[1].AsString(), "shoot_at")) return Value{false};
            GameObject* go = SceneSerializer::InstantiateFromFile(*s, a[1].AsString(), nullptr);
            if (!go || !go->transform) return Value{false};
            go->transform->localPosition = me;
            float speed = a.size() > 2 ? a[2].AsFloat() : 8.0f;
            Vec3 d = tgt->transform->Position() - me; float dist = d.Magnitude();
            if (dist > 1e-5f) {
                Vec3 v = d * (speed / dist);
                if (auto* rb = go->GetComponent<Rigidbody2D>()) rb->velocity = {v.x, v.y};
                else if (auto* rb3 = go->GetComponent<Rigidbody3D>()) rb3->velocity = v;
            }
            return Value{true};
        };
        b["spawn_wave"] = [this, sceneOf](std::vector<Value>& a) {
            // spawn_wave("prefab", count[, radius]): ring of prefabs around self.
            if (a.empty() || !rt.host || !rt.host->gameObject) return Value{0.0f};
            Scene* s = sceneOf(); if (!s) return Value{0.0f};
            int count = a.size() > 1 ? SafeInt(a[1].AsFloat()) : 8;
            if (count < 1) count = 1;
            float radius = a.size() > 2 ? a[2].AsFloat() : 3.0f;
            Vec3 c = rt.host->gameObject->transform->Position();
            int made = 0;
            if (!ScriptAssetPathOk(a[0].AsString(), "spawn_wave")) return Value{0.0f};
            for (int i = 0; i < count; ++i) {
                GameObject* go = SceneSerializer::InstantiateFromFile(*s, a[0].AsString(), nullptr);
                if (!go || !go->transform) continue;
                float ang = (6.2831853f * i) / count;
                go->transform->localPosition = {c.x + Mathf::Cos(ang) * radius, c.y + Mathf::Sin(ang) * radius, c.z};
                ++made;
            }
            return Value{(float)made};
        };
}

void OkayScriptVM::Impl::RegisterVehicleBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        auto sceneOf = c.sceneOf;
        auto alias = c.alias;
        // ---- Vehicles (docs/gameplay/vehicles.md): Vehicle.* ----
        // The first argument is the vehicle (an object or its name); leave it as "" to mean the object this script is on.
        auto carOf = [sceneOf, this](std::vector<Value>& a) -> VehicleController* {
            Scene* s = sceneOf(); if (!s) return nullptr;
            // One-argument forms such as Vehicle.Boost(1) act on this script's own object: the vehicle is then left out.
            if (a.empty() || !(a[0].IsObject() || a[0].IsString())) a.insert(a.begin(), Value{std::string{}});
            GameObject* g = (a[0].IsObject() || !a[0].AsString().empty()) ? ResolveObject(s, a[0]) : (rt.host ? rt.host->gameObject : nullptr);
            return g ? g->GetComponent<VehicleController>() : nullptr;
        };
        b["Vehicle.SetSetting"] = [carOf](std::vector<Value>& a) -> Value {   // Vehicle.SetSetting(car, "maxSpeed", 30): 1 = done, 0 = unknown name
            VehicleController* v = carOf(a); if (!v || a.size() < 3) return Value{0.0f};
            return Value{v->SetSetting(a[1].AsString(), a[2].AsFloat()) ? 1.0f : 0.0f};
        };
        b["Vehicle.GetSetting"] = [carOf](std::vector<Value>& a) -> Value {
            VehicleController* v = carOf(a); double d = 0; if (!v || a.size() < 2 || !v->GetSetting(a[1].AsString(), d)) return Value{0.0f};
            return Value{(float)d};
        };
        b["Vehicle.ApplyPreset"] = [carOf](std::vector<Value>& a) -> Value {   // "Sports Car", "Pickup", ...
            VehicleController* v = carOf(a); return Value{(v && a.size() > 1 && v->ApplyPreset(a[1].AsString())) ? 1.0f : 0.0f};
        };
        b["Vehicle.SetThrottle"] = [carOf](std::vector<Value>& a) -> Value { if (auto* v = carOf(a)) if (a.size() > 1) v->SetThrottle(a[1].AsFloat()); return Value{}; };
        b["Vehicle.SetBrake"]    = [carOf](std::vector<Value>& a) -> Value { if (auto* v = carOf(a)) if (a.size() > 1) v->SetBrake(a[1].AsFloat()); return Value{}; };
        b["Vehicle.SetSteer"]    = [carOf](std::vector<Value>& a) -> Value { if (auto* v = carOf(a)) if (a.size() > 1) v->SetSteer(a[1].AsFloat()); return Value{}; };
        b["Vehicle.SetHandbrake"] = [carOf](std::vector<Value>& a) -> Value { if (auto* v = carOf(a)) if (a.size() > 1) v->SetHandbrake(a[1].AsFloat() != 0.0f); return Value{}; };
        b["Vehicle.Boost"]  = [carOf](std::vector<Value>& a) -> Value { if (auto* v = carOf(a)) v->Boost(a.size() < 2 || a[1].AsFloat() != 0.0f); return Value{}; };
        b["Vehicle.Horn"]   = [carOf](std::vector<Value>& a) -> Value { if (auto* v = carOf(a)) v->Horn(a.size() < 2 || a[1].AsFloat() != 0.0f); return Value{}; };
        b["Vehicle.Lights"] = [carOf](std::vector<Value>& a) -> Value { if (auto* v = carOf(a)) v->SetLights(a.size() < 2 || a[1].AsFloat() != 0.0f); return Value{}; };
        b["Vehicle.ClearInput"] = [carOf](std::vector<Value>& a) -> Value { if (auto* v = carOf(a)) v->ClearInput(); return Value{}; };   // hand the keys back at once
        b["Vehicle.Recover"] = [carOf](std::vector<Value>& a) -> Value { if (auto* v = carOf(a)) v->Recover(); return Value{}; };
        b["Vehicle.Eject"]  = [carOf](std::vector<Value>& a) -> Value { return Value{VehicleSeat::EjectDriver(carOf(a)) ? 1.0f : 0.0f}; };
        b["Vehicle.Enter"]  = [carOf, sceneOf](std::vector<Value>& a) -> Value {   // Vehicle.Enter(car, person): the person takes the driver seat
            VehicleController* v = carOf(a); Scene* s = sceneOf();
            GameObject* who = (s && a.size() > 1) ? ResolveObject(s, a[1]) : nullptr;
            if (!v || !who || !v->gameObject) return Value{0.0f};
            for (VehicleSeat* seat : v->gameObject->GetComponentsInChildren<VehicleSeat>(true)) if (seat->driver) return Value{seat->Enter(who) ? 1.0f : 0.0f};
            return Value{0.0f};
        };
        b["Vehicle.Speed"] = [carOf](std::vector<Value>& a) -> Value { auto* v = carOf(a); return Value{v ? v->Speed() : 0.0f}; };
        b["Vehicle.Gear"]  = [carOf](std::vector<Value>& a) -> Value { auto* v = carOf(a); return Value{v ? (float)v->Gear() : 0.0f}; };
        b["Vehicle.Rpm"]   = [carOf](std::vector<Value>& a) -> Value { auto* v = carOf(a); return Value{v ? v->Rpm() : 0.0f}; };
        b["Quaternion.Euler"] = [](std::vector<Value>& a) -> Value {
            return Value{Vec3{a.size() > 0 ? a[0].AsFloat() : 0.0f,
                              a.size() > 1 ? a[1].AsFloat() : 0.0f,
                              a.size() > 2 ? a[2].AsFloat() : 0.0f}};
        };
        // AddComponent<T>() / AddComponent("T") — add a known component at runtime.
        b["AddComponent"] = [this](std::vector<Value>& a) -> Value {
            if (!rt.host || !rt.host->gameObject || a.empty()) return Value{false};
            const std::string& c = a[0].AsString();
            GameObject* g = rt.host->gameObject;
            if (c == "SpriteRenderer") { g->AddComponent<SpriteRenderer>(); return Value{true}; }
            if (c == "Rigidbody2D")    { g->AddComponent<Rigidbody2D>();    return Value{true}; }
            if (c == "TextRenderer")   { g->AddComponent<TextRenderer>();   return Value{true}; }
            if (c == "BoxCollider2D")  { g->AddComponent<BoxCollider2D>();  return Value{true}; }
            if (c == "NetworkObject") {
                auto* o = g->AddComponent<NetworkObject>();
                if (a.size() > 1) o->netId = a[1].AsString();
                return Value{true};
            }
            if (c == "NetworkTransform") { g->AddComponent<NetworkTransform>(); return Value{true}; }
            if (c == "SyncVar") {
                auto* sv = g->AddComponent<SyncVar>();
                if (a.size() > 1) sv->name = a[1].AsString();
                return Value{true};
            }
            if (c == "FirstPersonHands") { g->AddComponent<FirstPersonHands>(); return Value{true}; }
            return Value{false};
        };
        // Input.GetAxis("Horizontal"/"Vertical") -> WASD/stick axis.
        b["Input.GetAxis"] = [](std::vector<Value>& a) -> Value {
            std::string ax = a.empty() ? std::string{} : a[0].AsString();
            return Value{ax == "Vertical" ? Input::AxisWASD().y : Input::AxisWASD().x};
        };
        // gameObject.SetActive(bool) toggles this object.
        b["gameObject.SetActive"] = [this](std::vector<Value>& a) -> Value {
            if (rt.host && rt.host->gameObject)
                rt.host->gameObject->active = a.empty() ? true : a[0].AsBool();
            return Value{};
        };
        b["GetComponent"] = [this](std::vector<Value>& a) -> Value {
            // Lightweight: true if this object has the named component (so scripts
            // can guard with `if (GetComponent("Rigidbody2D")) ...`).
            if (!rt.host || !rt.host->gameObject || a.empty()) return Value{false};
            const std::string& c = a[0].AsString();
            GameObject* g = rt.host->gameObject;
            bool has = (c == "SpriteRenderer" && g->GetComponent<SpriteRenderer>())
                    || (c == "Rigidbody2D"   && g->GetComponent<Rigidbody2D>())
                    || (c == "TextRenderer"  && g->GetComponent<TextRenderer>());
            return Value{has};
        };
        // A few more math helpers scripts reach for (clamp01 / lerp_angle
        // already exist; these are new and avoid clobbering string repeat()).
        b["math_repeat"] = [](std::vector<Value>& a) -> Value {   // Mathf.Repeat: wrap into [0, len)
            if (a.size() < 2) return Value{0.0f};
            float t = a[0].AsFloat(), len = a[1].AsFloat();
            if (len == 0.0f) return Value{0.0f};
            float m = std::fmod(t, len); if (m < 0.0f) m += len;
            return Value{m};
        };
        b["inverse_lerp"] = [](std::vector<Value>& a) -> Value {
            if (a.size() < 3) return Value{0.0f};
            float lo = a[0].AsFloat(), hi = a[1].AsFloat(), v = a[2].AsFloat();
            if (hi == lo) return Value{0.0f};
            float t = (v - lo) / (hi - lo);
            return Value{t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t)};
        };
        b["approximately"] = [](std::vector<Value>& a) -> Value {
            if (a.size() < 2) return Value{false};
            return Value{Mathf::Abs(a[0].AsFloat() - a[1].AsFloat()) < 1e-4f};
        };
        b["delta_angle"] = [](std::vector<Value>& a) -> Value {   // shortest signed diff
            if (a.size() < 2) return Value{0.0f};
            float d = std::fmod(a[1].AsFloat() - a[0].AsFloat(), 360.0f);
            if (d > 180.0f) d -= 360.0f; if (d < -180.0f) d += 360.0f;
            return Value{d};
        };
        alias("Mathf.Clamp01", "clamp01");
        alias("Mathf.Repeat", "math_repeat");
        alias("Mathf.InverseLerp", "inverse_lerp");
        alias("Mathf.Approximately", "approximately");
        alias("Mathf.DeltaAngle", "delta_angle");
        alias("Mathf.LerpAngle", "lerp_angle");
        alias("inverse_lerp_value", "inverse_lerp");

        // Method-name aliases mapping C#-style calls onto existing builtins.
        alias("Input.GetKey", "key");
        alias("Input.GetKeyDown", "key_down");
        alias("Input.GetKeyUp", "key_up");
        alias("Input.GetMouseButton", "mouse");
        alias("Input.GetMouseButtonDown", "mouse_down");
        alias("Input.GetMouseButtonUp", "mouse_up");
        alias("Debug.Log", "print");
        alias("Debug.LogWarning", "print");
        alias("Debug.LogError", "print");
        alias("Mathf.Sin", "sin");   alias("Mathf.Cos", "cos");   alias("Mathf.Tan", "tan");
        alias("Mathf.Sqrt", "sqrt"); alias("Mathf.Abs", "abs");   alias("Mathf.Sign", "sign");
        alias("Mathf.Floor", "floor"); alias("Mathf.Ceil", "ceil"); alias("Mathf.Round", "round");
        alias("Mathf.Min", "min");   alias("Mathf.Max", "max");   alias("Mathf.Pow", "pow");
        alias("Mathf.Clamp", "clamp"); alias("Mathf.Lerp", "lerp"); alias("Mathf.Atan2", "atan2");
        alias("Mathf.PingPong", "ping_pong"); alias("Mathf.SmoothStep", "smoothstep");
        alias("Mathf.MoveTowards", "move_toward");
        alias("Random.Range", "rand");
        alias("Vector3.Distance", "vec_distance"); alias("Vector2.Distance", "vec_distance");
        alias("transform.Translate", "move"); alias("transform.Rotate", "rotate");
        alias("transform.LookAt", "look_at3");
        alias("Instantiate", "spawn"); alias("Object.Instantiate", "spawn");
        alias("Destroy", "destroy"); alias("Object.Destroy", "destroy");
        alias("Physics2D.Raycast", "raycast"); alias("Physics.Raycast", "raycast3");
        alias("Physics2D.BoxCast", "box_cast"); alias("Physics2D.CapsuleCast", "capsule_cast");
        alias("Physics2D.OverlapBox", "overlap_box"); alias("Physics2D.OverlapCapsule", "overlap_capsule");
        alias("Rigidbody2D.AddExplosionForce", "add_explosion_force");
        alias("SceneManager.LoadScene", "load_scene_name");
        alias("Application.Quit", "quit");
        alias("GameObject.Find", "exists");

}

} // namespace okay
