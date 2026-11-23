#include "OkayScriptVMInternal.hpp"

namespace okay {

void OkayScriptVM::Impl::RegisterTransformHelperBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        auto tf = c.tf;
        auto go = c.go;
        auto sceneOf = c.sceneOf;
        // Transform helpers
        b["set_x"] = [tf](std::vector<Value>& a) { if (Transform* t = tf()) t->localPosition.x = a.empty() ? 0 : a[0].AsFloat(); return Value{}; };
        b["set_y"] = [tf](std::vector<Value>& a) { if (Transform* t = tf()) t->localPosition.y = a.empty() ? 0 : a[0].AsFloat(); return Value{}; };
        b["set_z"] = [tf](std::vector<Value>& a) { if (Transform* t = tf()) t->localPosition.z = a.empty() ? 0 : a[0].AsFloat(); return Value{}; };
        b["pos_z"] = [tf](std::vector<Value>&) { Transform* t = tf(); return Value{t ? t->localPosition.z : 0.0f}; };
        // 3D movement / placement / rotation.
        b["move3"] = [tf](std::vector<Value>& a) {
            if (Transform* t = tf())
                t->Translate({a.size() > 0 ? a[0].AsFloat() : 0.0f, a.size() > 1 ? a[1].AsFloat() : 0.0f,
                              a.size() > 2 ? a[2].AsFloat() : 0.0f});
            return Value{};
        };
        b["set_pos3"] = [tf](std::vector<Value>& a) {
            if (Transform* t = tf())
                t->localPosition = {a.size() > 0 ? a[0].AsFloat() : 0.0f, a.size() > 1 ? a[1].AsFloat() : 0.0f,
                                    a.size() > 2 ? a[2].AsFloat() : 0.0f};
            return Value{};
        };
        b["rotate3"] = [tf](std::vector<Value>& a) {
            if (Transform* t = tf())
                t->Rotate({a.size() > 0 ? a[0].AsFloat() : 0.0f, a.size() > 1 ? a[1].AsFloat() : 0.0f,
                           a.size() > 2 ? a[2].AsFloat() : 0.0f});
            return Value{};
        };
        // Absolute 3D euler rotation (degrees) — face a direction outright.
        b["set_rot3"] = [tf](std::vector<Value>& a) {
            if (Transform* t = tf())
                t->localRotation = Quat::Euler({a.size() > 0 ? a[0].AsFloat() : 0.0f,
                                                a.size() > 1 ? a[1].AsFloat() : 0.0f,
                                                a.size() > 2 ? a[2].AsFloat() : 0.0f});
            return Value{};
        };
        // Current local rotation as Euler degrees (vec3).
        b["rotation_euler"] = [tf](std::vector<Value>&) {
            Transform* t = tf();
            return Value{t ? t->localRotation.ToEuler() : Vec3::Zero};
        };
        // 3D scale: set all axes, or read one.
        b["set_scale3"] = [tf](std::vector<Value>& a) {
            if (Transform* t = tf())
                t->localScale = {a.size() > 0 ? a[0].AsFloat() : 1.0f, a.size() > 1 ? a[1].AsFloat() : 1.0f,
                                 a.size() > 2 ? a[2].AsFloat() : 1.0f};
            return Value{};
        };
        b["set_scale"] = [tf](std::vector<Value>& a) {     // uniform scale
            if (Transform* t = tf()) { float s = a.empty() ? 1.0f : a[0].AsFloat(); t->localScale = {s, s, s}; }
            return Value{};
        };
        b["scale_x"] = [tf](std::vector<Value>&) { Transform* t = tf(); return Value{t ? t->localScale.x : 1.0f}; };
        b["scale_y"] = [tf](std::vector<Value>&) { Transform* t = tf(); return Value{t ? t->localScale.y : 1.0f}; };
        b["scale_z"] = [tf](std::vector<Value>&) { Transform* t = tf(); return Value{t ? t->localScale.z : 1.0f}; };
        // Full 3D distance to a named object (vs dist_to which ignores Z).
        b["dist3_to"] = [this, sceneOf](std::vector<Value>& a) -> Value {
            if (a.empty() || !rt.host || !rt.host->gameObject) return Value{0.0f};
            Scene* s = sceneOf(); if (!s) return Value{0.0f};
            GameObject* g = ResolveObject(s, a[0]); if (!g) return Value{0.0f};
            Vec3 d = g->transform->Position() - rt.host->gameObject->transform->Position();
            return Value{d.Magnitude()};
        };
        // Move toward a named object in 3D by speed*dt, without overshooting
        // (3D chase / homing). Returns the remaining distance.
        b["move_toward3"] = [this, tf, sceneOf](std::vector<Value>& a) -> Value {
            if (a.empty() || !rt.host || !rt.host->gameObject) return Value{0.0f};
            Transform* t = tf(); Scene* s = sceneOf();
            if (!t || !s) return Value{0.0f};
            GameObject* g = ResolveObject(s, a[0]); if (!g) return Value{0.0f};
            Vec3 me = rt.host->gameObject->transform->Position();
            Vec3 to = g->transform->Position() - me;
            float d = to.Magnitude();
            float step = (a.size() > 1 ? a[1].AsFloat() : 1.0f) * rt.host->deltaTime;
            if (d <= step || d < 1e-6f) t->localPosition += to;             // arrive
            else t->localPosition += to * (step / d);
            return Value{d};
        };
        // Rotate this object so its forward (+Z) faces a named object in 3D
        // (turrets/cameras tracking a target in a 3D scene).
        b["look_at3"] = [this, tf, sceneOf](std::vector<Value>& a) {
            if (a.empty()) return Value{};
            Transform* t = tf(); Scene* s = sceneOf();
            if (!t || !s) return Value{};
            GameObject* g = ResolveObject(s, a[0]); if (!g) return Value{};
            Vec3 dir = g->transform->Position() - rt.host->gameObject->transform->Position();
            float len = dir.Magnitude();
            if (len < 1e-6f) return Value{};
            float yaw = std::atan2(dir.x, dir.z) * 57.2957795f;
            float pitch = -std::asin(dir.y / len) * 57.2957795f;
            t->localRotation = Quat::Euler(pitch, yaw, 0.0f);
            return Value{};
        };
        // Swap a sibling MeshRenderer's primitive at runtime (morph, LOD, states).
        b["set_mesh"] = [this](std::vector<Value>& a) {
            if (!a.empty() && rt.host && rt.host->gameObject)
                if (auto* mr = rt.host->gameObject->GetComponent<MeshRenderer>())
                    mr->mesh = Mesh::FromName(a[0].AsString());
            return Value{};
        };

        // ===== Expanded API ============================================

        // --- 3D Rigidbody control (sibling Rigidbody3D) ---
        b["velocity_z"] = [go](std::vector<Value>&) -> Value {
            if (GameObject* g = go()) if (auto* rb = g->GetComponent<Rigidbody3D>()) return Value{rb->velocity.z};
            return Value{0.0f};
        };
        b["set_velocity3"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go()) if (auto* rb = g->GetComponent<Rigidbody3D>())
                rb->velocity = {a.size() > 0 ? a[0].AsFloat() : 0.0f,
                                a.size() > 1 ? a[1].AsFloat() : 0.0f,
                                a.size() > 2 ? a[2].AsFloat() : 0.0f};
            return Value{};
        };
        b["set_vz"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go()) if (auto* rb = g->GetComponent<Rigidbody3D>())
                rb->velocity.z = a.empty() ? 0.0f : a[0].AsFloat();
            return Value{};
        };
        b["add_force3"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go()) if (auto* rb = g->GetComponent<Rigidbody3D>())
                rb->AddForce({a.size() > 0 ? a[0].AsFloat() : 0.0f, a.size() > 1 ? a[1].AsFloat() : 0.0f,
                              a.size() > 2 ? a[2].AsFloat() : 0.0f});
            return Value{};
        };
        b["add_impulse3"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go()) if (auto* rb = g->GetComponent<Rigidbody3D>())
                rb->AddImpulse({a.size() > 0 ? a[0].AsFloat() : 0.0f, a.size() > 1 ? a[1].AsFloat() : 0.0f,
                                a.size() > 2 ? a[2].AsFloat() : 0.0f});
            return Value{};
        };
        // Jump helper: set vertical velocity straight up (2D uses y, 3D uses y too).
        b["jump"] = [go](std::vector<Value>& a) {
            float v = a.empty() ? 5.0f : a[0].AsFloat();
            if (GameObject* g = go()) {
                if (auto* rb = g->GetComponent<Rigidbody2D>()) rb->velocity.y = v;
                if (auto* rb3 = g->GetComponent<Rigidbody3D>()) rb3->velocity.y = v;
            }
            return Value{};
        };
        // stop(): zero this object's Rigidbody velocity (halt in place).
        b["stop"] = [go](std::vector<Value>&) {
            if (GameObject* g = go()) {
                if (auto* rb = g->GetComponent<Rigidbody2D>()) rb->velocity = {0.0f, 0.0f};
                if (auto* rb3 = g->GetComponent<Rigidbody3D>()) rb3->velocity = {0.0f, 0.0f, 0.0f};
            }
            return Value{};
        };
        // is_moving(): true when the Rigidbody2D is moving (for anim/state).
        b["is_moving"] = [go](std::vector<Value>&) -> Value {
            if (GameObject* g = go())
                if (auto* rb = g->GetComponent<Rigidbody2D>())
                    return Value{(rb->velocity.x * rb->velocity.x + rb->velocity.y * rb->velocity.y) > 1e-4f};
            return Value{false};
        };
        // face_velocity(): rotate (Z) to point the way the Rigidbody2D is moving.
        b["face_velocity"] = [go, tf](std::vector<Value>&) {
            GameObject* g = go(); Transform* t = tf(); if (!g || !t) return Value{};
            if (auto* rb = g->GetComponent<Rigidbody2D>()) {
                float vx = rb->velocity.x, vy = rb->velocity.y;
                if (vx * vx + vy * vy > 1e-6f)
                    t->localRotation = Quat::Euler({0, 0, std::atan2(vy, vx) * 57.2957795f});
            }
            return Value{};
        };
        // cooldown("name", secs): true (and starts the timer) only when at least
        // `secs` have passed since it last returned true — shooting, dashes, abilities.
        //   if (cooldown("shoot", 0.3)) spawn("bullet.okayprefab", pos_x(), pos_y())
        b["cooldown"] = [this](std::vector<Value>& a) -> Value {
            if (!rt.host || a.size() < 2) return Value{false};
            std::string key = "__cd_" + a[0].AsString();
            float now = Time::ElapsedTime(), secs = a[1].AsFloat();
            auto& g = rt.host->globals;
            float last = g.count(key) ? g[key].AsFloat() : -1e9f;
            if (now - last >= secs) { g[key] = Value{now}; return Value{true}; }
            return Value{false};
        };
        // once("name"): true exactly the first time it's reached, false ever after
        // (per name) — fire one-time events without a manual "did I already?" flag.
        b["once"] = [this](std::vector<Value>& a) -> Value {
            if (!rt.host || a.empty()) return Value{false};
            std::string key = "__once_" + a[0].AsString();
            auto& g = rt.host->globals;
            if (g.count(key)) return Value{false};
            g[key] = Value{1.0f};
            return Value{true};
        };

        // --- This object's identity / state ---
        b["name"] = [go](std::vector<Value>&) -> Value {
            GameObject* g = go(); return Value{g ? g->name : std::string{}};
        };
        b["set_name"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go()) g->name = a.empty() ? std::string{} : a[0].AsString();
            return Value{};
        };
        b["tag"] = [go](std::vector<Value>&) -> Value {
            GameObject* g = go(); return Value{g ? g->tag : std::string{}};
        };
        b["set_tag"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go()) g->tag = a.empty() ? std::string{} : a[0].AsString();
            return Value{};
        };
        b["has_tag"] = [go](std::vector<Value>& a) -> Value {
            GameObject* g = go(); return Value{g && !a.empty() && g->tag == a[0].AsString()};
        };
        b["set_active"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go()) g->active = a.empty() || a[0].AsBool();
            return Value{};
        };
        b["self_active"] = [go](std::vector<Value>&) -> Value {
            GameObject* g = go(); return Value{g ? g->active : false};
        };
        // Destroy a named object (e.g. a collected pickup, a defeated enemy).
        b["destroy_obj"] = [sceneOf](std::vector<Value>& a) {
            if (!a.empty()) if (Scene* s = sceneOf())
                if (GameObject* g = ResolveObject(s, a[0])) s->Destroy(g);
            return Value{};
        };

        // --- Facing vectors (from this object's rotation) ---
        b["forward_x"] = [tf](std::vector<Value>&) -> Value { Transform* t = tf(); return Value{t ? t->Forward().x : 0.0f}; };
        b["forward_y"] = [tf](std::vector<Value>&) -> Value { Transform* t = tf(); return Value{t ? t->Forward().y : 0.0f}; };
        b["forward_z"] = [tf](std::vector<Value>&) -> Value { Transform* t = tf(); return Value{t ? t->Forward().z : 1.0f}; };
        b["right_x"]   = [tf](std::vector<Value>&) -> Value { Transform* t = tf(); return Value{t ? t->Right().x : 1.0f}; };
        b["right_y"]   = [tf](std::vector<Value>&) -> Value { Transform* t = tf(); return Value{t ? t->Right().y : 0.0f}; };
        b["right_z"]   = [tf](std::vector<Value>&) -> Value { Transform* t = tf(); return Value{t ? t->Right().z : 0.0f}; };
        // Move along this object's own facing axes (forward / strafe), 3D.
        b["move_forward"] = [tf](std::vector<Value>& a) {
            if (Transform* t = tf()) t->localPosition = t->localPosition + t->Forward() * (a.empty() ? 0.0f : a[0].AsFloat());
            return Value{};
        };
        b["move_right"] = [tf](std::vector<Value>& a) {
            if (Transform* t = tf()) t->localPosition = t->localPosition + t->Right() * (a.empty() ? 0.0f : a[0].AsFloat());
            return Value{};
        };

        // --- More input ---
        b["mouse_up"] = [](std::vector<Value>& a) {
            return Value{Input::GetMouseButtonUp(a.empty() ? 0 : SafeInt(a[0].AsFloat()))};
        };

        // --- More math / utility ---
        b["clamp01"] = [](std::vector<Value>& a) { return Value{Mathf::Clamp01(a.empty() ? 0.0f : a[0].AsFloat())}; };
        b["dist3"] = [](std::vector<Value>& a) -> Value {
            if (a.size() < 6) return Value{0.0f};
            float dx = a[3].AsFloat() - a[0].AsFloat(), dy = a[4].AsFloat() - a[1].AsFloat(),
                  dz = a[5].AsFloat() - a[2].AsFloat();
            return Value{Mathf::Sqrt(dx * dx + dy * dy + dz * dz)};
        };
        // fps(): frames per second as measured, not 1 / this frame's scaled delta (which read 0 in a pause and doubled
        // at timeScale 0.5): see RealFps above.
        b["fps"] = [](std::vector<Value>&) -> Value { return Value{RealFps()}; };
}

void OkayScriptVM::Impl::RegisterPhysicsQueryBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        // 3D physics: ground/line-of-sight check via a 3D ray, and gravity.
        b["raycast_hit3"] = [this](std::vector<Value>& a) -> Value {
            if (a.size() < 6 || !rt.host || !rt.host->gameObject) return Value{false};
            Scene* sc = rt.host->gameObject->scene(); if (!sc) return Value{false};
            Vec3 o{a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat()};
            Vec3 d{a[3].AsFloat(), a[4].AsFloat(), a[5].AsFloat()};
            float maxDist = a.size() > 6 ? a[6].AsFloat() : 1e9f;
            return Value{sc->physics3D().Raycast(*sc, o, d, maxDist).hit};
        };
        // raycast3(ox,oy,oz, dx,dy,dz [, maxDist]) returns the hit object's name
        // ("" = miss); details via ray3_hit/ray3_object/ray3_x/y/z/dist/nx/ny/nz.
        // Optional trailing query filters shared by the 3D/2D casts below:
        // layers = a layer mask number, or a list "0,3" / "Default,Enemy"
        // (see layer_mask), and ignore_triggers = true to pass through triggers.
        auto maskArg = [](const std::vector<Value>& a, std::size_t i) -> std::uint32_t {
            if (a.size() <= i) return Physics3D::kAllLayers;
            if (a[i].IsString()) return Layers::MaskFromList(a[i].AsString());
            float f = a[i].AsFloat();
            return f <= 0.0f ? Physics3D::kAllLayers : (std::uint32_t)f;
        };
        auto trigArg = [](const std::vector<Value>& a, std::size_t i, bool dflt) {
            return a.size() > i ? !a[i].AsBool() : dflt;   // arg is "ignore triggers"
        };
        auto toHit = [](const RaycastHit3D& h) {
            Runtime::LastHit l;
            if (!h.hit) return l;
            l.hit = true;
            l.object = h.gameObject ? h.gameObject->name : std::string{};
            l.px = h.point.x;  l.py = h.point.y;  l.pz = h.point.z;
            l.nx = h.normal.x; l.ny = h.normal.y; l.nz = h.normal.z;
            l.dist = h.distance;
            return l;
        };
        // raycast3(ox,oy,oz, dx,dy,dz [, maxDist [, layers [, ignore_triggers]]])
        b["raycast3"] = [this, maskArg, trigArg, toHit](std::vector<Value>& a) -> Value {
            rt.lastHit3D = Runtime::LastHit{};
            if (a.size() < 6 || !rt.host || !rt.host->gameObject) return Value{std::string{}};
            Scene* sc = rt.host->gameObject->scene(); if (!sc) return Value{std::string{}};
            Vec3 o{a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat()};
            Vec3 d{a[3].AsFloat(), a[4].AsFloat(), a[5].AsFloat()};
            float maxDist = a.size() > 6 ? a[6].AsFloat() : 1e9f;
            RaycastHit3D h = sc->physics3D().Raycast(*sc, o, d, maxDist, nullptr, maskArg(a, 7), trigArg(a, 8, true));
            sc->physics3D().RecordQueryRay(o, d, maxDist, h);
            rt.lastHit3D = toHit(h);
            return Value{rt.lastHit3D.object};
        };
        // raycast_all3(ox,oy,oz, dx,dy,dz [, maxDist [, layers [, ignore_triggers]]]) ->
        // the names of EVERYTHING the ray passes through, nearest first. Details per
        // hit: ray_all_count(), ray_all_object(i), ray_all_dist(i), ray_all_point(i),
        // ray_all_normal(i).
        b["raycast_all3"] = [this, maskArg, trigArg, toHit](std::vector<Value>& a) -> Value {
            rt.lastHitAll.clear();
            Value out = Value::MakeArray();
            if (a.size() < 6 || !rt.host || !rt.host->gameObject) return out;
            Scene* sc = rt.host->gameObject->scene(); if (!sc) return out;
            Vec3 o{a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat()};
            Vec3 d{a[3].AsFloat(), a[4].AsFloat(), a[5].AsFloat()};
            float maxDist = a.size() > 6 ? a[6].AsFloat() : 1e9f;
            auto hits = sc->physics3D().RaycastAll(*sc, o, d, maxDist, nullptr, maskArg(a, 7), trigArg(a, 8, true));
            sc->physics3D().RecordQueryRay(o, d, maxDist, hits.empty() ? RaycastHit3D{} : hits.front());
            for (const RaycastHit3D& h : hits) {
                rt.lastHitAll.push_back(toHit(h));
                out.AsArray()->push_back(Value{rt.lastHitAll.back().object});
            }
            return out;
        };
        auto allAt = [this](std::vector<Value>& a) -> const Runtime::LastHit* {
            int i = a.empty() ? 0 : SafeInt(a[0].AsFloat());
            return (i >= 0 && i < (int)rt.lastHitAll.size()) ? &rt.lastHitAll[(std::size_t)i] : nullptr;
        };
        b["ray_all_count"]  = [this](std::vector<Value>&) -> Value { return Value{(float)rt.lastHitAll.size()}; };
        b["ray_all_object"] = [allAt](std::vector<Value>& a) -> Value { auto* h = allAt(a); return Value{h ? h->object : std::string{}}; };
        b["ray_all_dist"]   = [allAt](std::vector<Value>& a) -> Value { auto* h = allAt(a); return Value{h ? h->dist : 0.0f}; };
        b["ray_all_point"]  = [allAt](std::vector<Value>& a) -> Value { auto* h = allAt(a); return Value{h ? Vec3{h->px, h->py, h->pz} : Vec3::Zero}; };
        b["ray_all_normal"] = [allAt](std::vector<Value>& a) -> Value { auto* h = allAt(a); return Value{h ? Vec3{h->nx, h->ny, h->nz} : Vec3::Zero}; };
        // Thick casts (fill the same ray3_* details as raycast3; skip triggers unless
        // the last arg is false): sphere_cast3(ox,oy,oz, dx,dy,dz, radius [, maxDist [, layers [, ignore_triggers]]])
        b["sphere_cast3"] = [this, maskArg, trigArg, toHit](std::vector<Value>& a) -> Value {
            rt.lastHit3D = Runtime::LastHit{};
            if (a.size() < 7 || !rt.host || !rt.host->gameObject) return Value{std::string{}};
            Scene* sc = rt.host->gameObject->scene(); if (!sc) return Value{std::string{}};
            Vec3 o{a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat()};
            Vec3 d{a[3].AsFloat(), a[4].AsFloat(), a[5].AsFloat()};
            float r = a[6].AsFloat(), maxDist = a.size() > 7 ? a[7].AsFloat() : 1e9f;
            RaycastHit3D h = sc->physics3D().SphereCast(*sc, o, d, r, maxDist, nullptr, maskArg(a, 8), trigArg(a, 9, false));
            sc->physics3D().RecordQueryRay(o, d, maxDist, h, r);
            rt.lastHit3D = toHit(h);
            return Value{rt.lastHit3D.object};
        };
        // box_cast3(cx,cy,cz, hx,hy,hz, dx,dy,dz [, maxDist [, layers [, ignore_triggers]]]) — h = half size
        b["box_cast3"] = [this, maskArg, trigArg, toHit](std::vector<Value>& a) -> Value {
            rt.lastHit3D = Runtime::LastHit{};
            if (a.size() < 9 || !rt.host || !rt.host->gameObject) return Value{std::string{}};
            Scene* sc = rt.host->gameObject->scene(); if (!sc) return Value{std::string{}};
            Vec3 c{a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat()};
            Vec3 h3{a[3].AsFloat(), a[4].AsFloat(), a[5].AsFloat()};
            Vec3 d{a[6].AsFloat(), a[7].AsFloat(), a[8].AsFloat()};
            float maxDist = a.size() > 9 ? a[9].AsFloat() : 1e9f;
            RaycastHit3D h = sc->physics3D().BoxCast(*sc, c, h3, d, Quat::Identity, maxDist, nullptr,
                                                     maskArg(a, 10), trigArg(a, 11, false));
            sc->physics3D().RecordQueryRay(c, d, maxDist, h, Mathf::Min(h3.x, Mathf::Min(h3.y, h3.z)));
            rt.lastHit3D = toHit(h);
            return Value{rt.lastHit3D.object};
        };
        // capsule_cast3(ax,ay,az, bx,by,bz, radius, dx,dy,dz [, maxDist [, layers [, ignore_triggers]]])
        b["capsule_cast3"] = [this, maskArg, trigArg, toHit](std::vector<Value>& a) -> Value {
            rt.lastHit3D = Runtime::LastHit{};
            if (a.size() < 10 || !rt.host || !rt.host->gameObject) return Value{std::string{}};
            Scene* sc = rt.host->gameObject->scene(); if (!sc) return Value{std::string{}};
            Vec3 p0{a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat()};
            Vec3 p1{a[3].AsFloat(), a[4].AsFloat(), a[5].AsFloat()};
            float r = a[6].AsFloat();
            Vec3 d{a[7].AsFloat(), a[8].AsFloat(), a[9].AsFloat()};
            float maxDist = a.size() > 10 ? a[10].AsFloat() : 1e9f;
            RaycastHit3D h = sc->physics3D().CapsuleCast(*sc, p0, p1, r, d, maxDist, nullptr,
                                                         maskArg(a, 11), trigArg(a, 12, false));
            sc->physics3D().RecordQueryRay((p0 + p1) * 0.5f, d, maxDist, h, r);
            rt.lastHit3D = toHit(h);
            return Value{rt.lastHit3D.object};
        };
        // 2D shape casts fill the same ray_* details as raycast() (not ray3_*).
        // A leading word ("hit", "point", "normal", "distance", "object") picks one
        // detail; without it the call returns the hit object's name.
        // box_cast([what,] cx, cy, hx, hy, dx, dy [, maxDist [, layers [, ignore_triggers [, angle_deg]]]])
        auto toHit2 = [](const RaycastHit2D& h) {
            Runtime::LastHit l;
            if (!h.hit) return l;
            l.hit = true;
            l.object = h.gameObject ? h.gameObject->name : std::string{};
            l.px = h.point.x; l.py = h.point.y;
            l.nx = h.normal.x; l.ny = h.normal.y;
            l.dist = h.distance;
            return l;
        };
        auto detail2 = [](const std::string& what, const RaycastHit2D& h) -> Value {
            if (what == "hit") return Value{h.hit};
            if (what == "point") return Value{Vec3{h.point.x, h.point.y, 0.0f}};
            if (what == "normal") return Value{Vec3{h.normal.x, h.normal.y, 0.0f}};
            if (what == "distance") return Value{h.distance};
            return Value{h.gameObject ? h.gameObject->name : std::string{}};
        };
        auto miss2 = [](const std::string& what) -> Value {
            if (what == "hit") return Value{false};
            if (what == "point" || what == "normal") return Value{Vec3::Zero};
            if (what == "distance") return Value{0.0f};
            return Value{std::string{}};
        };
        b["box_cast"] = [this, maskArg, trigArg, toHit2, detail2, miss2](std::vector<Value>& a) -> Value {
            rt.lastHit2D = Runtime::LastHit{};
            std::size_t b = 0;
            std::string what = "object";
            if (!a.empty() && a[0].IsString()) { what = a[0].AsString(); b = 1; }
            if (a.size() < b + 6 || !rt.host || !rt.host->gameObject) return miss2(what);
            Scene* sc = rt.host->gameObject->scene(); if (!sc) return miss2(what);
            Vec2 c{a[b].AsFloat(), a[b + 1].AsFloat()};
            Vec2 h2{a[b + 2].AsFloat(), a[b + 3].AsFloat()};
            Vec2 d{a[b + 4].AsFloat(), a[b + 5].AsFloat()};
            float maxDist = a.size() > b + 6 ? a[b + 6].AsFloat() : 1e9f;
            float angle = (a.size() > b + 9 ? a[b + 9].AsFloat() : 0.0f) * Mathf::Deg2Rad;
            RaycastHit2D h = sc->physics().BoxCast(*sc, c, h2, d, angle, maxDist, nullptr,
                                                   maskArg(a, b + 7), trigArg(a, b + 8, false));
            sc->physics().RecordQueryRay(c, d, maxDist, h, Mathf::Min(Mathf::Abs(h2.x), Mathf::Abs(h2.y)));
            rt.lastHit2D = toHit2(h);
            return detail2(what, h);
        };
        // capsule_cast([what,] ax, ay, bx, by, radius, dx, dy [, maxDist [, layers [, ignore_triggers]]])
        b["capsule_cast"] = [this, maskArg, trigArg, toHit2, detail2, miss2](std::vector<Value>& a) -> Value {
            rt.lastHit2D = Runtime::LastHit{};
            std::size_t b = 0;
            std::string what = "object";
            if (!a.empty() && a[0].IsString()) { what = a[0].AsString(); b = 1; }
            if (a.size() < b + 7 || !rt.host || !rt.host->gameObject) return miss2(what);
            Scene* sc = rt.host->gameObject->scene(); if (!sc) return miss2(what);
            Vec2 p0{a[b].AsFloat(), a[b + 1].AsFloat()};
            Vec2 p1{a[b + 2].AsFloat(), a[b + 3].AsFloat()};
            float r = a[b + 4].AsFloat();
            Vec2 d{a[b + 5].AsFloat(), a[b + 6].AsFloat()};
            float maxDist = a.size() > b + 7 ? a[b + 7].AsFloat() : 1e9f;
            RaycastHit2D h = sc->physics().CapsuleCast(*sc, p0, p1, r, d, maxDist, nullptr,
                                                       maskArg(a, b + 8), trigArg(a, b + 9, false));
            sc->physics().RecordQueryRay((p0 + p1) * 0.5f, d, maxDist, h, r);
            rt.lastHit2D = toHit2(h);
            return detail2(what, h);
        };
        auto names2 = [](const std::vector<Collider2D*>& cols) {
            Value out = Value::MakeArray();
            for (Collider2D* c : cols)
                out.AsArray()->push_back(Value{c->gameObject ? c->gameObject->name : std::string{}});
            return out;
        };
        // overlap_box(cx, cy, hx, hy [, layers [, ignore_triggers [, angle_deg]]])
        b["overlap_box"] = [this, maskArg, trigArg, names2](std::vector<Value>& a) -> Value {
            if (a.size() < 4 || !rt.host || !rt.host->gameObject) return Value::MakeArray();
            Scene* sc = rt.host->gameObject->scene(); if (!sc) return Value::MakeArray();
            float angle = (a.size() > 6 ? a[6].AsFloat() : 0.0f) * Mathf::Deg2Rad;
            return names2(sc->physics().OverlapBox(*sc, {a[0].AsFloat(), a[1].AsFloat()},
                                                   {a[2].AsFloat(), a[3].AsFloat()},
                                                   maskArg(a, 4), trigArg(a, 5, true), nullptr, angle));
        };
        // overlap_capsule(ax, ay, bx, by, radius [, layers [, ignore_triggers]])
        b["overlap_capsule"] = [this, maskArg, trigArg, names2](std::vector<Value>& a) -> Value {
            if (a.size() < 5 || !rt.host || !rt.host->gameObject) return Value::MakeArray();
            Scene* sc = rt.host->gameObject->scene(); if (!sc) return Value::MakeArray();
            return names2(sc->physics().OverlapCapsule(*sc, {a[0].AsFloat(), a[1].AsFloat()},
                                                       {a[2].AsFloat(), a[3].AsFloat()}, a[4].AsFloat(),
                                                       maskArg(a, 5), trigArg(a, 6, true)));
        };
        // Overlaps -> the names of every collider's object inside the shape.
        auto names3 = [](const std::vector<Collider3D*>& cols) {
            Value out = Value::MakeArray();
            for (Collider3D* c : cols) out.AsArray()->push_back(Value{c->gameObject ? c->gameObject->name : std::string{}});
            return out;
        };
        // overlap_sphere3(cx,cy,cz, radius [, layers [, ignore_triggers]])
        b["overlap_sphere3"] = [this, maskArg, trigArg, names3](std::vector<Value>& a) -> Value {
            if (a.size() < 4 || !rt.host || !rt.host->gameObject) return Value::MakeArray();
            Scene* sc = rt.host->gameObject->scene(); if (!sc) return Value::MakeArray();
            return names3(sc->physics3D().OverlapSphere(*sc, {a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat()},
                                                        a[3].AsFloat(), maskArg(a, 4), trigArg(a, 5, true)));
        };
        // overlap_box3(cx,cy,cz, hx,hy,hz [, layers [, ignore_triggers]]) — h = half size
        b["overlap_box3"] = [this, maskArg, trigArg, names3](std::vector<Value>& a) -> Value {
            if (a.size() < 6 || !rt.host || !rt.host->gameObject) return Value::MakeArray();
            Scene* sc = rt.host->gameObject->scene(); if (!sc) return Value::MakeArray();
            return names3(sc->physics3D().OverlapBox(*sc, {a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat()},
                                                     {a[3].AsFloat(), a[4].AsFloat(), a[5].AsFloat()}, Quat::Identity,
                                                     maskArg(a, 6), trigArg(a, 7, true)));
        };
        // overlap_capsule3(ax,ay,az, bx,by,bz, radius [, layers [, ignore_triggers]])
        b["overlap_capsule3"] = [this, maskArg, trigArg, names3](std::vector<Value>& a) -> Value {
            if (a.size() < 7 || !rt.host || !rt.host->gameObject) return Value::MakeArray();
            Scene* sc = rt.host->gameObject->scene(); if (!sc) return Value::MakeArray();
            return names3(sc->physics3D().OverlapCapsule(*sc, {a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat()},
                                                         {a[3].AsFloat(), a[4].AsFloat(), a[5].AsFloat()},
                                                         a[6].AsFloat(), maskArg(a, 7), trigArg(a, 8, true)));
        };
        // layer_mask("Default", "Enemy", 3, ...) -> a mask number for the layers arg.
        b["layer_mask"] = [](std::vector<Value>& a) -> Value {
            std::uint32_t m = 0;
            for (const Value& v : a)
                m |= v.IsString() ? Layers::MaskFromList(v.AsString()) : (1u << (SafeInt(v.AsFloat()) & 31));
            return Value{(float)(a.empty() ? Physics3D::kAllLayers : m)};
        };
        // ray3_cast(what, origin, direction [, maxDist [, layers [, ignore_triggers [, radius]]]])
        // — one value from a cast that skips this object: what = "hit" (true/false),
        // "point", "normal" (vectors), "distance" or "object" (name). Used by
        // visual-script Raycast 3D / Sphere Cast nodes turned into code.
        b["ray3_cast"] = [this, maskArg, trigArg](std::vector<Value>& a) -> Value {
            const std::string what = a.empty() ? std::string("hit") : a[0].AsString();
            RaycastHit3D h;
            Scene* sc = rt.host && rt.host->gameObject ? rt.host->gameObject->scene() : nullptr;
            if (what == "all") {   // names of everything along the ray, nearest first
                Value out = Value::MakeArray();
                if (!sc || a.size() < 3) return out;
                for (const RaycastHit3D& x : sc->physics3D().RaycastAll(*sc, a[1].AsVec3(), a[2].AsVec3(),
                        a.size() > 3 ? a[3].AsFloat() : 1e9f, rt.host->gameObject, maskArg(a, 4), trigArg(a, 5, true)))
                    out.AsArray()->push_back(Value{x.gameObject ? x.gameObject->name : std::string{}});
                return out;
            }
            if (sc && a.size() >= 3) {
                Vec3 o = a[1].AsVec3(), d = a[2].AsVec3();
                float maxDist = a.size() > 3 ? a[3].AsFloat() : 1e9f;
                float r = a.size() > 6 ? a[6].AsFloat() : 0.0f;
                bool trig = trigArg(a, 5, r <= 0.0f);
                h = r > 0.0f
                    ? sc->physics3D().SphereCast(*sc, o, d, r, maxDist, rt.host->gameObject, maskArg(a, 4), trig)
                    : sc->physics3D().Raycast(*sc, o, d, maxDist, rt.host->gameObject, maskArg(a, 4), trig);
                sc->physics3D().RecordQueryRay(o, d, maxDist, h, r);
            }
            if (what == "point")    return Value{h.point};
            if (what == "normal")   return Value{h.normal};
            if (what == "distance") return Value{h.distance};
            if (what == "object")   return Value{h.gameObject ? h.gameObject->name : std::string{}};
            return Value{h.hit};
        };
        // overlap3("sphere" | "box", center, radius_or_half_size [, layers]) -> names of
        // the objects inside, leaving out this object (the Overlap nodes as code).
        b["overlap3"] = [this, maskArg, names3](std::vector<Value>& a) -> Value {
            Scene* sc = rt.host && rt.host->gameObject ? rt.host->gameObject->scene() : nullptr;
            if (!sc || a.size() < 3) return Value::MakeArray();
            GameObject* self = rt.host->gameObject;
            std::vector<Collider3D*> cols = a[0].AsString() == "box"
                ? sc->physics3D().OverlapBox(*sc, a[1].AsVec3(), a[2].AsVec3(), Quat::Identity, maskArg(a, 3), true, self)
                : sc->physics3D().OverlapSphere(*sc, a[1].AsVec3(), a[2].AsFloat(), maskArg(a, 3), true, self);
            Value out = Value::MakeArray();   // one entry per object
            std::vector<GameObject*> seen;
            for (Collider3D* c : cols) {
                if (std::find(seen.begin(), seen.end(), c->gameObject) != seen.end()) continue;
                seen.push_back(c->gameObject);
                out.AsArray()->push_back(Value{c->gameObject ? c->gameObject->name : std::string{}});
            }
            return out;
        };
        b["ray3_point"]  = [this](std::vector<Value>&) -> Value { return Value{Vec3{rt.lastHit3D.px, rt.lastHit3D.py, rt.lastHit3D.pz}}; };
        b["ray3_normal"] = [this](std::vector<Value>&) -> Value { return Value{Vec3{rt.lastHit3D.nx, rt.lastHit3D.ny, rt.lastHit3D.nz}}; };
        b["ray3_hit"]    = [this](std::vector<Value>&) -> Value { return Value{rt.lastHit3D.hit}; };
        b["ray3_object"] = [this](std::vector<Value>&) -> Value { return Value{rt.lastHit3D.object}; };
        b["ray3_x"]      = [this](std::vector<Value>&) -> Value { return Value{rt.lastHit3D.px}; };
        b["ray3_y"]      = [this](std::vector<Value>&) -> Value { return Value{rt.lastHit3D.py}; };
        b["ray3_z"]      = [this](std::vector<Value>&) -> Value { return Value{rt.lastHit3D.pz}; };
        b["ray3_nx"]     = [this](std::vector<Value>&) -> Value { return Value{rt.lastHit3D.nx}; };
        b["ray3_ny"]     = [this](std::vector<Value>&) -> Value { return Value{rt.lastHit3D.ny}; };
        b["ray3_nz"]     = [this](std::vector<Value>&) -> Value { return Value{rt.lastHit3D.nz}; };
        b["ray3_dist"]   = [this](std::vector<Value>&) -> Value { return Value{rt.lastHit3D.dist}; };
        b["physics_steps"] = [this](std::vector<Value>&) {
            Scene* sc = rt.host && rt.host->gameObject ? rt.host->gameObject->scene() : nullptr;
            return Value{sc ? sc->PhysicsStepsPerSecond() : 0.0f};
        };
        b["set_physics_steps"] = [this](std::vector<Value>& a) {
            if (Scene* sc = rt.host && rt.host->gameObject ? rt.host->gameObject->scene() : nullptr)
                sc->SetPhysicsStepsPerSecond(a.empty() ? 50.0f : a[0].AsFloat());
            return Value{};
        };
        b["physics_smooth"] = [this](std::vector<Value>&) {
            Scene* sc = rt.host && rt.host->gameObject ? rt.host->gameObject->scene() : nullptr;
            return Value{sc ? sc->SmoothPhysics() : false};
        };
        b["set_physics_smooth"] = [this](std::vector<Value>& a) {
            if (Scene* sc = rt.host && rt.host->gameObject ? rt.host->gameObject->scene() : nullptr)
                sc->SetSmoothPhysics(a.empty() ? true : a[0].AsBool());
            return Value{};
        };
        b["set_gravity3"] = [this](std::vector<Value>& a) {
            if (rt.host && rt.host->gameObject && rt.host->gameObject->scene())
                rt.host->gameObject->scene()->physics3D().gravity =
                    Vec3{a.size() > 0 ? a[0].AsFloat() : 0.0f,
                         a.size() > 1 ? a[1].AsFloat() : -9.81f,
                         a.size() > 2 ? a[2].AsFloat() : 0.0f};
            return Value{};
        };

}

void OkayScriptVM::Impl::RegisterMaterialEventBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        auto go = c.go;
        // --- Material control on a sibling MeshRenderer ---
        b["set_emissive"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go()) if (auto* mr = g->GetComponent<MeshRenderer>())
                mr->emissive = {a.size() > 0 ? a[0].AsFloat() : 0.0f,
                                a.size() > 1 ? a[1].AsFloat() : 0.0f,
                                a.size() > 2 ? a[2].AsFloat() : 0.0f, 1.0f};
            return Value{};
        };
        b["set_unlit"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go()) if (auto* mr = g->GetComponent<MeshRenderer>())
                mr->unlit = a.empty() ? true : a[0].AsBool();
            return Value{};
        };
        b["set_specular"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go()) if (auto* mr = g->GetComponent<MeshRenderer>())
                mr->specular = a.empty() ? 0.0f : a[0].AsFloat();
            return Value{};
        };
        b["set_mesh_texture"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go()) if (auto* mr = g->GetComponent<MeshRenderer>())
                mr->texture = a.empty() ? std::string{} : a[0].AsString();
            return Value{};
        };
        b["set_tiling"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go()) if (auto* mr = g->GetComponent<MeshRenderer>())
                mr->tiling = {a.size() > 0 ? a[0].AsFloat() : 1.0f, a.size() > 1 ? a[1].AsFloat() : 1.0f};
            return Value{};
        };
        // --- Bridge to the visual-script (ActionList) shared variables, so
        //     OkayScript and the no-code Actions can read/write the same state. ---
        b["gc_set"] = [](std::vector<Value>& a) {
            if (a.size() >= 2) ActionList::Vars()[a[0].AsString()] = a[1].AsFloat();
            return Value{};
        };
        b["gc_get"] = [](std::vector<Value>& a) -> Value {
            if (a.empty()) return Value{0.0f};
            return Value{ActionList::GetVar(a[0].AsString())};   // shares stats/prefs too
        };
        // Broadcast a named signal to every Actions (OnMessage) list in the scene.
        b["send_message"] = [this](std::vector<Value>& a) {
            if (!a.empty() && rt.host && rt.host->gameObject && rt.host->gameObject->scene())
                for (ActionList* al : rt.host->gameObject->scene()->FindObjectsOfType<ActionList>())
                    al->ReceiveMessage(a[0].AsString());
            return Value{};
        };

        // fire_output("OnOpened", param): raise a named output on this object's Logic Source,
        // running its I/O links. Returns how many links were delivered or queued.
        b["fire_output"] = [this](std::vector<Value>& a) -> Value {
            int n = 0;
            if (!a.empty() && rt.host && rt.host->gameObject)
                for (LogicSource* ls : rt.host->gameObject->GetComponents<LogicSource>())
                    n += ls->FireOutput(a[0].AsString(), a.size() > 1 ? a[1].AsString() : std::string());
            return Value{(float)n};
        };

        // --- Custom events + object variables shared with visual-script graphs ---
        // send_event("Target", "Hit", value): runs the target's On Custom Event "Hit"
        // graph nodes and its scripts' OnEvent_Hit(value). "" / "self" = this object.
        auto evTarget = [this](const Value& ref) -> GameObject* {
            GameObject* me = rt.host ? rt.host->gameObject : nullptr;
            if (!me) return nullptr;
            if (ref.IsObject()) return ResolveObject(nullptr, ref);   // send_event(other, ...) hits that copy
            const std::string name = ref.AsString();
            if (name.empty() || name == "self") return me;
            return me->scene() ? me->scene()->Find(name) : nullptr;
        };
        auto deliverEvent = [this](GameObject* o, const std::string& ev, const Value& v) {
            if (!o) return;
            GameObject* me = rt.host ? rt.host->gameObject : nullptr;
            for (auto* vc : o->GetComponents<VisualScriptComponent>()) vc->SendEvent(ev, v, me);
            for (auto* sc : o->GetComponents<ScriptComponent>())
                if (sc->VM()) sc->VM()->CallEventWith("OnEvent_" + ev, v);
        };
        b["send_event"] = [evTarget, deliverEvent](std::vector<Value>& a) {
            if (a.size() < 2) return Value{};
            deliverEvent(evTarget(a[0]), a[1].AsString(), a.size() > 2 ? a[2] : Value{});
            return Value{};
        };
        // raise("OnHurt", 3) runs this script's += listeners for that name, and a
        // function of the same name when there is one.
        b["raise"] = [this](std::vector<Value>& a) {
            if (a.empty()) return Value{};
            std::vector<Value> args;
            if (a.size() > 1) args.push_back(a[1]);
            const std::string ev = a[0].AsString();
            try {
                if (rt.functions.count(ev)) rt.Call(ev, args);
                rt.FireSubs(ev, args);
            } catch (const std::exception& e) { Log::Error("OkayScript raise: ", e.what()); }
            return Value{};
        };
        // run_action("move 1 0 0") / check_action("var_gt hp 0"): one step of the
        // no-code Actions library, run on this object (what a graph's Action /
        // Condition node exports to).
        b["run_action"] = [this](std::vector<Value>& a) {
            GameObject* me = rt.host ? rt.host->gameObject : nullptr;
            if (a.empty() || !me) return Value{};
            ActionList runner;
            runner.gameObject = me; runner.transform = me->transform;
            runner.instructions = ActionList::DecodeItems(a[0].AsString());
            if (runner.BeginRun()) runner.Update(rt.host ? rt.host->deltaTime : 0.0f);
            return Value{};
        };
        // actions_load(text): the Actions export. Adds the list described by `text`
        // (ActionList::ToText) to this object. The scene starts it and ticks it, so
        // every step, wait, branch, and trigger behaves as it does on an Actions component.
        b["actions_load"] = [this](std::vector<Value>& a) {
            GameObject* me = rt.host ? rt.host->gameObject : nullptr;
            if (a.empty() || !me) return Value{};
            const std::string text = a[0].AsString();
            for (ActionList* have : me->GetComponents<ActionList>())
                if (have->ToText() == text) return Value{};
            ActionList* al = me->AddComponent<ActionList>();
            al->FromText(text);
            return Value{};
        };
        b["check_action"] = [this](std::vector<Value>& a) {
            GameObject* me = rt.host ? rt.host->gameObject : nullptr;
            if (a.empty() || !me) return Value{false};
            ActionList tester;
            tester.gameObject = me; tester.transform = me->transform;
            return Value{tester.TestConditions({ActionList::DecodeItem(a[0].AsString())})};
        };
        b["broadcast_event"] = [this, deliverEvent](std::vector<Value>& a) {
            GameObject* me = rt.host ? rt.host->gameObject : nullptr;
            if (a.empty() || !me || !me->scene()) return Value{};
            std::vector<GameObject*> all;
            for (const auto& up : me->scene()->Objects()) if (up && up->active) all.push_back(up.get());
            for (GameObject* o : all) deliverEvent(o, a[0].AsString(), a.size() > 1 ? a[1] : Value{});
            return Value{};
        };
        // set_object_var("Enemy", "hp", 10) / get_object_var("Enemy", "hp"): a graph's
        // blackboard variable or a script's global on another object.
        b["set_object_var"] = [evTarget](std::vector<Value>& a) {
            if (a.size() < 3) return Value{};
            if (GameObject* o = evTarget(a[0])) {
                for (auto* vc : o->GetComponents<VisualScriptComponent>()) vc->SetVariable(a[1].AsString(), a[2]);
                for (auto* sc : o->GetComponents<ScriptComponent>()) if (sc->VM()) sc->VM()->SetGlobal(a[1].AsString(), a[2]);
            }
            return Value{};
        };
        b["get_object_var"] = [evTarget](std::vector<Value>& a) -> Value {
            if (a.size() < 2) return Value{};
            if (GameObject* o = evTarget(a[0])) {
                if (auto* vc = o->GetComponent<VisualScriptComponent>()) return vc->GetVariable(a[1].AsString());
                if (auto* sc = o->GetComponent<ScriptComponent>()) if (sc->VM()) return sc->VM()->GetGlobal(a[1].AsString());
            }
            return Value{};
        };
        // set_object_position("Door", x, y, z): a non-number axis keeps its value.
        b["set_object_position"] = [evTarget](std::vector<Value>& a) {
            if (a.empty()) return Value{};
            GameObject* o = evTarget(a[0]);
            if (!o || !o->transform) return Value{};
            Vec3 p = o->transform->Position();
            if (a.size() > 1 && !a[1].IsString()) p.x = a[1].AsFloat();
            if (a.size() > 2 && !a[2].IsString()) p.y = a[2].AsFloat();
            if (a.size() > 3 && !a[3].IsString()) p.z = a[3].AsFloat();
            o->transform->SetPosition(p);
            return Value{};
        };
        // UI Animation: ui_show("Menu") activates it and plays its intro, ui_hide("Menu")
        // plays its outro then hides it (instantly when it has no UI Animation),
        // ui_play("Menu") replays the intro.
        b["ui_show"] = [evTarget](std::vector<Value>& a) {
            if (GameObject* o = evTarget(a.empty() ? Value{std::string()} : a[0])) {
                o->active = true;
                if (auto* ua = o->GetComponent<UIAnimation>()) ua->Play();
            }
            return Value{};
        };
        b["ui_hide"] = [evTarget](std::vector<Value>& a) {
            if (GameObject* o = evTarget(a.empty() ? Value{std::string()} : a[0])) {
                if (auto* ua = o->GetComponent<UIAnimation>(); ua && o->active) ua->PlayOut();
                else o->active = false;
            }
            return Value{};
        };
        b["ui_play"] = [evTarget](std::vector<Value>& a) {
            if (GameObject* o = evTarget(a.empty() ? Value{std::string()} : a[0]))
                if (auto* ua = o->GetComponent<UIAnimation>()) ua->Play();
            return Value{};
        };
        b["pause_game"] = [](std::vector<Value>& a) { Game::SetPaused(a.empty() || a[0].AsBool()); return Value{}; };
        if (!b.count("quit")) b["quit"] = [](std::vector<Value>&) { Game::RequestQuit(); return Value{}; };   // Application.Quit()
        b["show_cursor"] = [](std::vector<Value>& a) { Cursor::Capture(!(a.empty() || a[0].AsBool())); return Value{}; };
        // cursor_lock("locked" | "confined" | "none"). Locked hides the pointer,
        // Confined keeps it inside the window, None lets it roam.
        b["cursor_lock"] = [](std::vector<Value>& a) {
            std::string m = a.empty() ? std::string("locked") : a[0].AsString();
            for (char& c : m) c = (char)std::tolower((unsigned char)c);
            if (m == "none" || m == "free" || m == "0") {
                Cursor::SetLock(Cursor::LockMode::None);
                Cursor::SetVisible(true);
            } else if (m == "confined" || m == "2") {
                Cursor::SetLock(Cursor::LockMode::Confined);
                Cursor::SetVisible(true);
            } else {
                Cursor::Capture(true);
            }
            return Value{};
        };
        b["cursor_lock_mode"] = [](std::vector<Value>&) {
            if (Cursor::IsConfined()) return Value{std::string("confined")};
            if (Cursor::IsLocked()) return Value{std::string("locked")};
            return Value{std::string("none")};
        };

        // --- Sprite glyph/size on a sibling SpriteRenderer ---
        b["set_glyph"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go()) if (auto* sr = g->GetComponent<SpriteRenderer>()) {
                std::string s = a.empty() ? std::string{} : a[0].AsString();
                if (!s.empty()) sr->glyph = s[0];
            }
            return Value{};
        };
        b["sprite_size"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go()) if (auto* sr = g->GetComponent<SpriteRenderer>())
                sr->size = {a.size() > 0 ? a[0].AsFloat() : 1.0f, a.size() > 1 ? a[1].AsFloat() : 1.0f};
            return Value{};
        };
        // --- Call one of this script's own functions by name (dynamic dispatch:
        //     state machines, command tables, "call the handler for this state"). ---
        // call("fn", args...) invokes a function (user or builtin) by name, passing
        // through any extra arguments and returning its result.
        b["call"] = [this](std::vector<Value>& a) -> Value {
            if (a.empty()) return Value{};
            std::string fn = a[0].AsString();
            std::vector<Value> callArgs(a.begin() + 1, a.end());
            if (rt.functions.count(fn) || rt.builtins.count(fn)) return rt.Call(fn, callArgs);
            return Value{};
        };

}

} // namespace okay
