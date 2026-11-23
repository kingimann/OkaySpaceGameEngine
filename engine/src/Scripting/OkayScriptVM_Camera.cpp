#include "OkayScriptVMInternal.hpp"

namespace okay {

void OkayScriptVM::Impl::RegisterCameraControlBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        auto sceneOf = c.sceneOf;
        // Main-camera control from script (screen-follow, cutscenes, zoom).
        b["cam_x"] = [sceneOf](std::vector<Value>&) -> Value {
            if (Scene* s = sceneOf()) if (s->mainCamera) return Value{s->mainCamera->gameObject->transform->localPosition.x};
            return Value{0.0f};
        };
        b["cam_y"] = [sceneOf](std::vector<Value>&) -> Value {
            if (Scene* s = sceneOf()) if (s->mainCamera) return Value{s->mainCamera->gameObject->transform->localPosition.y};
            return Value{0.0f};
        };
        b["set_cam"] = [sceneOf](std::vector<Value>& a) {
            if (Scene* s = sceneOf()) if (s->mainCamera) {
                Transform* ct = s->mainCamera->gameObject->transform;
                ct->localPosition.x = a.size() > 0 ? a[0].AsFloat() : ct->localPosition.x;
                ct->localPosition.y = a.size() > 1 ? a[1].AsFloat() : ct->localPosition.y;
            }
            return Value{};
        };
        b["move_cam"] = [sceneOf](std::vector<Value>& a) {
            if (Scene* s = sceneOf()) if (s->mainCamera) {
                Transform* ct = s->mainCamera->gameObject->transform;
                ct->localPosition.x += a.size() > 0 ? a[0].AsFloat() : 0.0f;
                ct->localPosition.y += a.size() > 1 ? a[1].AsFloat() : 0.0f;
            }
            return Value{};
        };
        b["cam_zoom"] = [sceneOf](std::vector<Value>&) -> Value {
            if (Scene* s = sceneOf()) if (s->mainCamera) return Value{s->mainCamera->orthographicSize};
            return Value{0.0f};
        };
        b["set_cam_zoom"] = [sceneOf](std::vector<Value>& a) {
            if (Scene* s = sceneOf()) if (s->mainCamera && !a.empty())
                s->mainCamera->orthographicSize = Mathf::Max(0.01f, a[0].AsFloat());
            return Value{};
        };
        // Camera shake (0..1, stacks) and smooth zoom on the main camera.
        b["camera_shake"] = [sceneOf](std::vector<Value>& a) {
            if (Scene* s = sceneOf()) if (s->mainCamera) s->mainCamera->Shake(a.empty() ? 0.5f : a[0].AsFloat());
            return Value{};
        };
        b["camera_zoom_to"] = [sceneOf](std::vector<Value>& a) {
            if (Scene* s = sceneOf()) if (s->mainCamera && !a.empty())
                s->mainCamera->ZoomTo(a[0].AsFloat(), a.size() > 1 ? a[1].AsFloat() : 0.0f);
            return Value{};
        };
        b["camera_dutch"] = [sceneOf](std::vector<Value>& a) {
            if (Scene* s = sceneOf()) if (s->mainCamera) s->mainCamera->dutch = a.empty() ? 0.0f : a[0].AsFloat();
            return Value{};
        };
        b["camera_fov"] = [sceneOf](std::vector<Value>& a) {
            if (Scene* s = sceneOf()) if (s->mainCamera) {
                if (a.empty()) return Value{s->mainCamera->fieldOfView};
                s->mainCamera->fieldOfView = Mathf::Clamp(a[0].AsFloat(), 1.0f, 179.0f);
            }
            return Value{};
        };
        // camera_follow("name"[, smoothing]): the main camera eases after that object
        // (adds a CameraFollow if it has none). Empty name stops following.
        b["camera_follow"] = [sceneOf](std::vector<Value>& a) {
            if (Scene* s = sceneOf()) if (s->mainCamera && s->mainCamera->gameObject) {
                GameObject* cg = s->mainCamera->gameObject;
                auto* f = cg->GetComponent<CameraFollow>();
                if (!f) f = cg->AddComponent<CameraFollow>();
                f->targetName = a.empty() ? std::string{} : a[0].AsString();
                if (a.size() > 1) f->smoothing = a[1].AsFloat();
            }
            return Value{};
        };
}

void OkayScriptVM::Impl::RegisterCameraRigBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        auto go = c.go;
        auto sceneOf = c.sceneOf;
        // ---- Camera rigs and effects (docs/gameplay/cameras.md): Camera.* ----
        // The live shot: the virtual camera the brain is showing, else the first enabled one.
        auto liveVcam = [sceneOf]() -> VirtualCamera* {
            Scene* s = sceneOf();
            if (!s) return nullptr;
            for (CinemachineBrain* br : s->FindObjectsOfType<CinemachineBrain>()) {
                if (br->enabled && br->Forced()) return br->Forced();
                if (br->enabled && br->LiveCamera()) return br->LiveCamera();
            }
            for (VirtualCamera* v : s->FindObjectsOfType<VirtualCamera>())
                if (v->enabled && v->gameObject && v->gameObject->active) return v;
            return nullptr;
        };
        // Camera.Shake(strength)            the classic trauma shake (hits ~0.3, explosions ~0.6)
        // Camera.Shake("impact", strength)  a preset: "impact", "explosion", "footstep" or "handheld"
        b["Camera.Shake"] = [sceneOf](std::vector<Value>& a) {
            Scene* s = sceneOf();
            if (!s || !s->mainCamera) return Value{};
            if (!a.empty() && a[0].IsString()) {
                std::string n = a[0].AsString();
                for (char& ch : n) ch = (char)std::tolower((unsigned char)ch);
                cam::ShakePreset p = cam::ShakePreset::Impact;
                if (n == "explosion") p = cam::ShakePreset::Explosion;
                else if (n == "footstep" || n == "footsteps") p = cam::ShakePreset::Footstep;
                else if (n == "handheld") p = cam::ShakePreset::Handheld;
                else if (n != "impact") return Value{};   // an unknown name does nothing
                s->mainCamera->ShakePreset(p, a.size() > 1 ? a[1].AsFloat() : 1.0f);
            } else {
                s->mainCamera->Shake(a.empty() ? 0.5f : a[0].AsFloat());
            }
            return Value{};
        };
        // Camera.BlendTo("Shot", seconds): cut or blend to that virtual camera and keep it live.
        // seconds < 0 (or left out) = the shot's own blend time. An empty name goes back to choosing by priority.
        b["Camera.BlendTo"] = [sceneOf](std::vector<Value>& a) {
            Scene* s = sceneOf();
            if (!s || !s->mainCamera || !s->mainCamera->gameObject) return Value{};
            const std::string name = a.empty() ? std::string{} : a[0].AsString();
            GameObject* cg = s->mainCamera->gameObject;
            auto* br = cg->GetComponent<CinemachineBrain>();
            if (name.empty()) { if (br) br->ReleaseForced(); return Value{}; }
            GameObject* g = s->Find(name);
            VirtualCamera* v = g ? g->GetComponent<VirtualCamera>() : nullptr;
            if (!v) return Value{};
            if (!br) br = cg->AddComponent<CinemachineBrain>();
            br->ForceLive(v, a.size() > 1 ? a[1].AsFloat() : -1.0f);
            return Value{};
        };
        // Camera.SetTarget("Name"): the live shot follows and looks at that object.
        b["Camera.SetTarget"] = [sceneOf, liveVcam](std::vector<Value>& a) {
            const std::string name = a.empty() ? std::string{} : a[0].AsString();
            if (VirtualCamera* v = liveVcam()) {
                if (v->lookAt.empty() || v->lookAt == v->follow) v->lookAt = name;
                v->follow = name;
                return Value{};
            }
            if (Scene* s = sceneOf()) if (s->mainCamera && s->mainCamera->gameObject) {   // no rig: the plain chase camera
                GameObject* cg = s->mainCamera->gameObject;
                auto* f = cg->GetComponent<CameraFollow>();
                if (!f) f = cg->AddComponent<CameraFollow>();
                f->targetName = name;
            }
            return Value{};
        };
        b["Camera.AddToGroup"] = [liveVcam](std::vector<Value>& a) {
            if (VirtualCamera* v = liveVcam()) if (!a.empty()) v->AddGroupTarget(a[0].AsString());
            return Value{};
        };
        b["Camera.RemoveFromGroup"] = [liveVcam](std::vector<Value>& a) {
            if (VirtualCamera* v = liveVcam()) { if (a.empty()) v->ClearGroup(); else v->RemoveGroupTarget(a[0].AsString()); }
            return Value{};
        };
        // Camera.FovKick(degrees, seconds): a quick punch of the field of view that settles back.
        b["Camera.FovKick"] = [sceneOf](std::vector<Value>& a) {
            if (Scene* s = sceneOf()) if (s->mainCamera)
                s->mainCamera->KickFov(a.empty() ? 6.0f : a[0].AsFloat(), a.size() > 1 ? a[1].AsFloat() : 0.4f);
            return Value{};
        };
        // Camera.SetFovOffset(degrees, seconds): a held change, for sprinting or aiming. 0 eases back.
        b["Camera.SetFovOffset"] = [sceneOf](std::vector<Value>& a) {
            if (Scene* s = sceneOf()) if (s->mainCamera)
                s->mainCamera->SetFovOffset(a.empty() ? 0.0f : a[0].AsFloat(), a.size() > 1 ? a[1].AsFloat() : 0.25f);
            return Value{};
        };
        // Camera.HitPulse(strength): flash the screen edges dark for a moment (a hit).
        b["Camera.HitPulse"] = [sceneOf](std::vector<Value>& a) {
            if (Scene* s = sceneOf()) if (s->mainCamera) s->mainCamera->HitPulse(a.empty() ? 0.6f : a[0].AsFloat());
            return Value{};
        };
        // Camera.SetHandheld(amount): a steady slight wobble, 0 to 1 (0 = off).
        b["Camera.SetHandheld"] = [sceneOf](std::vector<Value>& a) {
            if (Scene* s = sceneOf()) if (s->mainCamera) s->mainCamera->SetHandheld(a.empty() ? 0.0f : a[0].AsFloat());
            return Value{};
        };
        // Set the main camera's clear/background color (flash, day-night, fades).
        b["set_bg"] = [sceneOf](std::vector<Value>& a) {
            if (Scene* s = sceneOf()) if (s->mainCamera)
                s->mainCamera->backgroundColor = {a.size() > 0 ? a[0].AsFloat() : 0.0f,
                                                  a.size() > 1 ? a[1].AsFloat() : 0.0f,
                                                  a.size() > 2 ? a[2].AsFloat() : 0.0f,
                                                  a.size() > 3 ? a[3].AsFloat() : 1.0f};
            return Value{};
        };
        // 3D directional light: set its direction or ambient floor (day-night,
        // mood, hit flashes) — applies to the player and editor shading alike.
        b["set_light"] = [](std::vector<Value>& a) {
            SceneLight::SetDirection({a.size() > 0 ? a[0].AsFloat() : 0.0f,
                                      a.size() > 1 ? a[1].AsFloat() : -1.0f,
                                      a.size() > 2 ? a[2].AsFloat() : 0.0f});
            return Value{};
        };
        b["set_ambient"] = [](std::vector<Value>& a) {
            SceneLight::SetAmbient(a.empty() ? 0.25f : a[0].AsFloat());
            return Value{};
        };
        b["ambient"] = [](std::vector<Value>&) { return Value{SceneLight::Ambient()}; };
        // Light component on a named object: set_light_intensity("Lamp", 2),
        // set_light_range, set_light_angle (spot cone, degrees), set_light_color(name, r, g, b).
        auto lightOf = [this](std::vector<Value>& a, size_t need) -> Light* {
            if (a.size() < need || !rt.host || !rt.host->gameObject || !rt.host->gameObject->scene()) return nullptr;
            GameObject* g = ResolveObject(rt.host->gameObject->scene(), a[0]);
            return g ? g->GetComponent<Light>() : nullptr;
        };
        b["set_light_intensity"] = [lightOf](std::vector<Value>& a) { if (auto* l = lightOf(a, 2)) l->intensity = a[1].AsFloat(); return Value{}; };
        b["set_light_range"]     = [lightOf](std::vector<Value>& a) { if (auto* l = lightOf(a, 2)) l->range = a[1].AsFloat(); return Value{}; };
        b["set_light_angle"]     = [lightOf](std::vector<Value>& a) { if (auto* l = lightOf(a, 2)) l->spotAngle = a[1].AsFloat(); return Value{}; };
        b["set_light_color"]     = [lightOf](std::vector<Value>& a) {
            if (auto* l = lightOf(a, 4)) l->color = Color(a[1].AsFloat(), a[2].AsFloat(), a[3].AsFloat());
            return Value{};
        };

        // Screen size in pixels (HUD layout, clamping, spawn-at-edge).
        b["screen_w"] = [](std::vector<Value>&) { return Value{UICanvas::Width()}; };
        b["screen_h"] = [](std::vector<Value>&) { return Value{UICanvas::Height()}; };
        // Tag queries: count active objects with a tag, or find the nearest one
        // to this object (returns its name, "" if none) — enemy targeting, "all
        // coins collected?", proximity doors.
        b["count_tag"] = [sceneOf](std::vector<Value>& a) -> Value {
            if (a.empty()) return Value{0.0f};
            int n = 0;
            if (Scene* s = sceneOf())
                for (const auto& o : s->Objects())
                    if (o->active && o->tag == a[0].AsString()) ++n;
            return Value{(float)n};
        };
        b["nearest_tag"] = [this, sceneOf](std::vector<Value>& a) -> Value {
            if (a.empty() || !rt.host || !rt.host->gameObject) return Value{std::string{}};
            Scene* s = sceneOf(); if (!s) return Value{std::string{}};
            Vec3 me = rt.host->gameObject->transform->Position();
            std::string best; float bestD = 0.0f;
            for (const auto& o : s->Objects()) {
                if (!o->active || o->tag != a[0].AsString()) continue;
                if (o.get() == rt.host->gameObject) continue;       // skip self
                Vec3 p = o->transform->Position();
                float dx = p.x - me.x, dy = p.y - me.y, d = dx * dx + dy * dy;
                if (best.empty() || d < bestD) { best = o->name; bestD = d; }
            }
            return Value{best};
        };
        // Drive sibling components on this GameObject.
        b["set_text"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go())
                if (auto* tr = g->GetComponent<TextRenderer>())
                    tr->text = a.empty() ? std::string{} : a[0].AsString();
            return Value{};
        };
        b["set_color"] = [go](std::vector<Value>& a) {
            // Accept either set_color(r, g, b[, a]) or a single Color/Vector3
            // value: set_color(Color.red) / set_color(myColor [, alpha]).
            Color c;
            if (!a.empty() && a[0].IsVec3()) {
                Vec3 v = a[0].AsVec3();
                c = Color{v.x, v.y, v.z, a.size() > 1 ? a[1].AsFloat() : 1.0f};
            } else {
                c = Color{a.size() > 0 ? a[0].AsFloat() : 1.0f, a.size() > 1 ? a[1].AsFloat() : 1.0f,
                          a.size() > 2 ? a[2].AsFloat() : 1.0f, a.size() > 3 ? a[3].AsFloat() : 1.0f};
            }
            if (GameObject* g = go()) {
                if (auto* sr = g->GetComponent<SpriteRenderer>()) sr->color = c;
                if (auto* tr = g->GetComponent<TextRenderer>()) tr->color = c;
                if (auto* mr = g->GetComponent<MeshRenderer>()) mr->color = c;
            }
            return Value{};
        };
}

} // namespace okay
