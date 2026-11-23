#include "OkayScriptVMInternal.hpp"

namespace okay {

void OkayScriptVM::Impl::RegisterCoroutineBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        auto alias = c.alias;
        // ---- Coroutines: StartCoroutine(Routine()) + yield return new WaitForSeconds(t) ----
        b["WaitForSeconds"] = [](std::vector<Value>& a) {
            Value v = Value::MakeMap(); (*v.AsMap())["__wait"] = Value{a.empty() ? 0.0f : a[0].AsFloat()}; return v;
        };
        b["WaitForSecondsRealtime"] = [](std::vector<Value>& a) {   // ignores Time.timeScale
            Value v = Value::MakeMap(); (*v.AsMap())["__wait"] = Value{a.empty() ? 0.0f : a[0].AsFloat()}; (*v.AsMap())["__real"] = Value{true}; return v;
        };
        // yield return new WaitUntil(() => cond) / WaitWhile(() => cond)
        b["WaitUntil"] = [](std::vector<Value>& a) {
            Value v = Value::MakeMap(); (*v.AsMap())["__until"] = a.empty() ? Value{} : a[0]; return v;
        };
        b["WaitWhile"] = [](std::vector<Value>& a) {
            Value v = Value::MakeMap(); (*v.AsMap())["__while"] = a.empty() ? Value{} : a[0]; return v;
        };
        b["WaitForEndOfFrame"] = [](std::vector<Value>&) { return Value{}; };
        alias("WaitForFixedUpdate", "WaitForEndOfFrame");
        b["StartCoroutine"] = [this](std::vector<Value>& a) {
            if (a.empty()) return Value{};
            std::vector<Value> args(a.begin() + 1, a.end());
            std::vector<std::string> names;
            if (rt.activeArgNames && rt.activeArgNames->size() > 1)
                names.assign(rt.activeArgNames->begin() + 1, rt.activeArgNames->end());
            int id = rt.StartCoroutine(a[0].AsString(), std::move(args), std::move(names));
            Value v = Value::MakeMap(); (*v.AsMap())["__co"] = Value{(float)id}; return v;
        };
        b["StopCoroutine"] = [this](std::vector<Value>& a) {
            if (a.empty()) return Value{};
            if (auto m = a[0].AsMap()) {
                if (auto it = m->find("__co"); it != m->end())
                    if (auto* co = rt.FindCo(SafeInt(it->second.AsFloat()))) { rt.StopCoroutine(co); rt.ReapCoroutines(); }
                return Value{};
            }
            rt.StopCoroutines(a[0].AsString().empty() ? std::string("\x01") : a[0].AsString());
            return Value{};
        };
        b["StopAllCoroutines"] = [this](std::vector<Value>&) { rt.StopCoroutines(""); return Value{}; };

}

void OkayScriptVM::Impl::RegisterObjectLifecycleBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        auto tf = c.tf;
        auto go = c.go;
        auto sceneOf = c.sceneOf;
        auto findNamed = c.findNamed;
        auto alias = c.alias;
        // ---- Objects: Destroy(obj[, seconds]), Instantiate(prefab, pos, rot), CompareTag ----
        b["Destroy"] = [this, findNamed](std::vector<Value>& a) {
            Value target = a.empty() ? Value{std::string()} : a[0];
            if (IsComponentRef(target)) return Value{};   // Destroy(component): ignore
            const float delay = a.size() > 1 ? a[1].AsFloat() : 0.0f;
            GameObject* me = rt.host ? rt.host->gameObject : nullptr;
            // A plain name equal to this object's own name means this object (as before);
            // a reference always means exactly the object it names.
            if (me && !target.IsObject() && target.AsString() == me->name) target = Value{std::string()};
            if (delay > 0.0f) { rt.pendingDestroy.push_back({delay, target}); return Value{}; }
            GameObject* g = findNamed(target);
            if (g && g->scene()) g->scene()->Destroy(g);
            return Value{};
        };
        alias("Object.Destroy", "Destroy");
        b["Instantiate"] = [this](std::vector<Value>& a) {
            if (a.empty()) return Value{};
            if (a.size() >= 2 && a[1].IsVec3()) {   // Instantiate(prefab, position, rotation)
                Vec3 p = a[1].AsVec3();
                std::vector<Value> b3{a[0], Value{p.x}, Value{p.y}, Value{p.z}};
                return rt.Call("spawn3", b3);
            }
            return rt.Call("spawn", a);
        };
        alias("Object.Instantiate", "Instantiate");
        // GameObject.Find("Enemy") -> a reference to the first ACTIVE object with that
        // name (or a "Parent/Child" path), or null. #888 F110: it used to return a
        // switched-off object too. FindIncludingInactive is the explicit "any" variant.
        b["GameObject.Find"] = [sceneOf](std::vector<Value>& a) -> Value {
            if (a.empty() || a[0].AsString().empty()) return Value{};
            Scene* s = sceneOf(); if (!s) return Value{};
            if (a[0].IsObject()) { GameObject* g = ResolveObject(s, a[0]); return g && g->ActiveInHierarchy() ? ObjectRef(g) : Value{}; }
            const std::string n = a[0].AsString();
            for (const auto& up : s->Objects()) if (up && up->name == n && up->ActiveInHierarchy()) return ObjectRef(up.get());
            if (n.find('/') != std::string::npos)
                if (GameObject* g = s->Find(n); g && g->ActiveInHierarchy()) return ObjectRef(g);
            return Value{};
        };
        b["GameObject.FindIncludingInactive"] = [sceneOf](std::vector<Value>& a) -> Value {
            if (a.empty() || a[0].AsString().empty()) return Value{};
            Scene* s = sceneOf();
            return ObjectRef(ResolveObject(s, a[0]));
        };
        b["GameObject.FindWithTag"] = [this](std::vector<Value>& a) -> Value {
            Scene* s = (rt.host && rt.host->gameObject) ? rt.host->gameObject->scene() : nullptr;
            if (s && !a.empty()) for (const auto& up : s->Objects()) if (up && up->ActiveInHierarchy() && up->tag == a[0].AsString()) return ObjectRef(up.get());
            return Value{};
        };
        alias("GameObject.FindGameObjectWithTag", "GameObject.FindWithTag");
        b["GameObject.FindGameObjectsWithTag"] = [this](std::vector<Value>& a) -> Value {
            Value out = Value::MakeArray();
            Scene* s = (rt.host && rt.host->gameObject) ? rt.host->gameObject->scene() : nullptr;
            if (s && !a.empty()) for (const auto& up : s->Objects()) if (up && up->ActiveInHierarchy() && up->tag == a[0].AsString()) out.AsArray()->push_back(ObjectRef(up.get()));
            return out;
        };
        b["CompareTag"] = [go](std::vector<Value>& a) {
            GameObject* g = go(); return Value{g && !a.empty() && g->tag == a[0].AsString()};
        };
        alias("gameObject.CompareTag", "CompareTag");
        b["__object_CompareTag"] = [findNamed](std::vector<Value>& a) {
            GameObject* g = a.empty() ? nullptr : findNamed(a[0]);
            return Value{g && a.size() > 1 && g->tag == a[1].AsString()};
        };
        b["__object_SetActive"] = [this](std::vector<Value>& a) {
            if (a.empty()) return Value{};
            if (a[0].IsObject()) {   // exactly the referenced object
                if (GameObject* g = ResolveObject(nullptr, a[0])) g->active = a.size() < 2 || a[1].AsBool();
                return Value{};
            }
            if (Scene* s = (rt.host && rt.host->gameObject) ? rt.host->gameObject->scene() : nullptr)
                for (const auto& up : s->Objects())
                    if (up && up->name == a[0].AsString()) { up->active = a.size() < 2 || a[1].AsBool(); break; }
            return Value{};
        };
        b["transform.LookAt"] = [this, tf](std::vector<Value>& a) {
            Transform* t = tf(); if (!t || a.empty()) return Value{};
            if (!a[0].IsVec3()) return rt.Call("look_at", a);
            Vec3 d = a[0].AsVec3() - t->Position();
            if (d.Magnitude() > 1e-6f) t->localRotation = Quat::LookRotation(d.Normalized());
            return Value{};
        };

}

void OkayScriptVM::Impl::RegisterComponentAccessBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        auto go = c.go;
        auto findNamed = c.findNamed;
        auto alias = c.alias;
        // ---- Components: GetComponent<T>() returns a reference ("@T") whose
        // fields / methods map onto the engine component. ----
        auto compOf = [](GameObject* g, const std::string& t) -> bool {
            if (!g) return false;
            if (t == "Transform" || t == "GameObject") return true;
            if (t == "SpriteRenderer") return g->GetComponent<SpriteRenderer>();
            if (t == "MeshRenderer" || t == "Renderer" || t == "SkinnedMeshRenderer")
                return g->GetComponent<MeshRenderer>() || g->GetComponent<SpriteRenderer>();
            if (t == "TextRenderer" || t == "Text" || t == "TMP_Text" || t == "TextMeshPro" || t == "TextMeshProUGUI")
                return g->GetComponent<TextRenderer>();
            if (t == "Rigidbody2D") return g->GetComponent<Rigidbody2D>();
            if (t == "Rigidbody")   return g->GetComponent<Rigidbody3D>();
            if (t == "AudioSource") return g->GetComponent<AudioSource>();
            if (t == "NetworkObject") return g->GetComponent<NetworkObject>();
            if (t == "NetworkTransform") return g->GetComponent<NetworkTransform>();
            if (t == "SyncVar") return g->GetComponent<SyncVar>();
            if (t == "FirstPersonHands") return g->GetComponent<FirstPersonHands>() != nullptr;
            if (t == "Animator" || t == "Animation") return true;   // resolved by the anim_* builtins
            if (t == "IK") return true;                              // resolved by the ik_* builtins
            return false;
        };
        // The engine component a C# type name refers to (for .enabled).
        auto compPtr = [](GameObject* g, const std::string& t) -> Component* {
            if (!g) return nullptr;
            if (t == "SpriteRenderer") return g->GetComponent<SpriteRenderer>();
            if (t == "MeshRenderer" || t == "SkinnedMeshRenderer") return g->GetComponent<MeshRenderer>();
            if (t == "Renderer") { if (auto* m = g->GetComponent<MeshRenderer>()) return m; return g->GetComponent<SpriteRenderer>(); }
            if (t == "TextRenderer" || t == "Text" || t == "TMP_Text" || t == "TextMeshPro" || t == "TextMeshProUGUI")
                return g->GetComponent<TextRenderer>();
            if (t == "Rigidbody2D") return g->GetComponent<Rigidbody2D>();
            if (t == "Rigidbody")   return g->GetComponent<Rigidbody3D>();
            if (t == "AudioSource") return g->GetComponent<AudioSource>();
            if (t == "NetworkObject") return g->GetComponent<NetworkObject>();
            if (t == "NetworkTransform") return g->GetComponent<NetworkTransform>();
            if (t == "SyncVar") return g->GetComponent<SyncVar>();
            if (t == "FirstPersonHands") return g->GetComponent<FirstPersonHands>();
            return nullptr;
        };
        b["GetComponent"] = [this, compOf](std::vector<Value>& a) -> Value {
            if (!rt.host || !rt.host->gameObject || a.empty()) return Value{};
            const std::string c = a[0].AsString();
            return compOf(rt.host->gameObject, c) ? Value{"@" + c} : Value{};
        };
        alias("gameObject.GetComponent", "GetComponent");
        alias("TryGetComponent", "GetComponent");
        // other.GetComponent<T>(): "@T#<id>" names that object's component, so
        // what is done with it lands on `other` (#888 F111: it used to be "@T",
        // which every handler below read as this script's own component).
        b["__object_GetComponent"] = [findNamed, compOf, go](std::vector<Value>& a) -> Value {
            GameObject* g = a.empty() ? nullptr : findNamed(a[0]);
            if (!g || a.size() < 2 || !compOf(g, a[1].AsString())) return Value{};
            if (g == go()) return Value{"@" + a[1].AsString()};
            return Value{"@" + a[1].AsString() + "#" + std::to_string(g->InstanceId())};
        };
        // The object and type a component reference names: "T" is this object's,
        // "T#<id>" another object's (nullptr once that object is gone).
        auto compTarget = [go](const Value& ref, std::string& type) -> GameObject* {
            const std::string body = ref.AsString();
            const auto hash = body.find('#');
            if (hash == std::string::npos) { type = body; return go(); }
            type = body.substr(0, hash);
            return GameObject::FromInstanceId(std::strtoull(body.c_str() + hash + 1, nullptr, 10));
        };
        // Rigidbody / Rigidbody2D members a script used that don't exist: said once each.
        auto unknownMember = [](const std::string& type, const std::string& member) {
            static std::unordered_set<std::string> told;
            if (told.insert(type + "." + member).second)
                Log::Warning("[script] ", type, " has no property '", member, "' (ignored)");
        };
        b["__get_component"] = [this, compTarget, compPtr, unknownMember](std::vector<Value>& a) -> Value {
            if (a.size() < 2) return Value{};
            std::string t;
            GameObject* g = compTarget(a[0], t); if (!g) return Value{};
            ActAsObject as(rt.host, g);   // Transform / Animator members read the referenced object
            const std::string p = a[1].AsString();
            auto rgb = [](const Color& c) { return Value{Vec3{c.r, c.g, c.b}}; };
            if (t == "Transform") { bool ok = false; Value v = rt.GetDotted("transform." + p, ok); return v; }
            if (p == "enabled") { Component* c = compPtr(g, t); return Value{c ? c->enabled : true}; }
            if (auto* sr = g->GetComponent<SpriteRenderer>(); sr && (t == "SpriteRenderer" || t == "Renderer")) {
                if (p == "color" || p == "material.color") return rgb(sr->color);
                if (p == "flipX") return Value{sr->flipX};
            }
            if (auto* mr = g->GetComponent<MeshRenderer>(); mr && (t == "MeshRenderer" || t == "Renderer" || t == "SkinnedMeshRenderer"))
                if (p == "color" || p == "material.color") return rgb(mr->color);
            if (auto* tr = g->GetComponent<TextRenderer>()) {
                if (p == "text") return Value{tr->text};
                if (p == "color") return rgb(tr->color);
            }
            if (auto* rb = g->GetComponent<Rigidbody2D>(); rb && t == "Rigidbody2D") {
                if (p == "velocity" || p == "linearVelocity") return Value{Vec3{rb->velocity.x, rb->velocity.y, 0.0f}};
                if (p == "gravityScale") return Value{rb->gravityScale};
                if (p == "angularVelocity") return Value{rb->angularVelocity};   // degrees per second
                if (p == "mass") return Value{rb->mass};
                if (p == "drag" || p == "linearDamping") return Value{rb->drag};
                if (p == "angularDrag" || p == "angularDamping") return Value{rb->angularDrag};
                if (p == "isKinematic") return Value{rb->bodyType == Rigidbody2D::BodyType::Kinematic};
                if (p == "bodyType") return Value{(float)(int)rb->bodyType};
                if (p == "freezeRotation") return Value{rb->freezeRotation};
                if (p == "bounciness") return Value{rb->bounciness};
                if (p == "friction") return Value{rb->friction};
                if (p == "position") return Value{g->transform ? g->transform->Position() : Vec3::Zero};
                if (p == "sleeping" || p == "isSleeping") return Value{rb->sleeping};
                unknownMember("Rigidbody2D", p);
                return Value{};
            }
            if (auto* rb = g->GetComponent<Rigidbody3D>(); rb && t == "Rigidbody") {
                if (p == "velocity" || p == "linearVelocity") return Value{rb->velocity};
                if (p == "angularVelocity") return Value{rb->angularVelocity};   // radians per second
                if (p == "mass") return Value{rb->mass};
                if (p == "drag" || p == "linearDamping") return Value{rb->drag};
                if (p == "angularDrag" || p == "angularDamping") return Value{rb->angularDrag};
                if (p == "gravityScale") return Value{rb->gravityScale};
                if (p == "useGravity") return Value{rb->gravityScale != 0.0f};
                if (p == "isKinematic") return Value{rb->bodyType == Rigidbody3D::BodyType::Kinematic};
                if (p == "freezeRotation") return Value{rb->freezeRotation};
                if (p == "bounciness") return Value{rb->bounciness};
                if (p == "friction") return Value{rb->friction};
                if (p == "maxFallSpeed") return Value{rb->maxFallSpeed};
                if (p == "position") return Value{g->transform ? g->transform->Position() : Vec3::Zero};
                if (p == "sleeping" || p == "isSleeping") return Value{rb->sleeping};
                unknownMember("Rigidbody", p);
                return Value{};
            }
            if (auto* au = g->GetComponent<AudioSource>(); au && t == "AudioSource") {
                if (p == "volume") return Value{au->volume};
                if (p == "pitch") return Value{au->pitch};
                if (p == "pan") return Value{au->pan};
                if (p == "lowPass") return Value{au->lowPass};
                if (p == "doppler") return Value{au->doppler};
                if (p == "dopplerScale") return Value{au->dopplerScale};
                if (p == "occlude") return Value{au->occlude};
                if (p == "occlusionVolume") return Value{au->occlusionVolume};
                if (p == "occlusionLowPass") return Value{au->occlusionLowPass};
                if (p == "distanceLowPass") return Value{au->distanceLowPass};
                if (p == "distanceLowPassHz") return Value{au->distanceLowPassHz};
                if (p == "isPlaying") return Value{au->IsPlaying()};
            }
            if (auto* o = g->GetComponent<NetworkObject>(); o && t == "NetworkObject") {
                if (p == "netId") return Value{o->netId};
                if (p == "owned" || p == "isOwned") return Value{o->IsLocallyOwned()};
                if (p == "authority") return Value{(float)o->authority};
            }
            if (auto* sv = g->GetComponent<SyncVar>(); sv && t == "SyncVar") {
                if (p == "name") return Value{sv->name};
                if (p == "value") return Value{sv->Get()};
            }
            if (t == "FirstPersonHands") return FpHandsGet(g->GetComponent<FirstPersonHands>(), p);
            return Value{};
        };
        b["__set_component"] = [this, compTarget, compPtr, unknownMember](std::vector<Value>& a) -> Value {
            if (a.size() < 3) return Value{};
            std::string t;
            GameObject* g = compTarget(a[0], t); if (!g) return Value{};
            ActAsObject as(rt.host, g);   // Transform members write the referenced object
            const std::string p = a[1].AsString();
            const Value& v = a[2];
            auto col = [&](const Color& old) {
                if (v.IsVec3()) { Vec3 c = v.AsVec3(); return Color{c.x, c.y, c.z, old.a}; }
                return old;
            };
            if (t == "Transform") { rt.SetDotted("transform." + p, v); return Value{}; }
            if (p == "enabled") { if (Component* c = compPtr(g, t)) c->enabled = v.AsBool(); return Value{}; }
            if (p == "color" || p == "material.color") {
                if (auto* sr = g->GetComponent<SpriteRenderer>(); sr && (t == "SpriteRenderer" || t == "Renderer")) sr->color = col(sr->color);
                if (auto* mr = g->GetComponent<MeshRenderer>(); mr && t != "SpriteRenderer") mr->color = col(mr->color);
                if (auto* tr = g->GetComponent<TextRenderer>(); tr && (t == "Text" || t == "TextRenderer" || t.rfind("TMP", 0) == 0 || t.rfind("TextMesh", 0) == 0)) tr->color = col(tr->color);
                return Value{};
            }
            if (p == "flipX") if (auto* sr = g->GetComponent<SpriteRenderer>()) sr->flipX = v.AsBool();
            if (p == "text") if (auto* tr = g->GetComponent<TextRenderer>()) tr->text = v.AsString();
            if (t == "Rigidbody2D") if (auto* rb = g->GetComponent<Rigidbody2D>()) {
                if (p == "velocity" || p == "linearVelocity") { Vec3 q = v.AsVec3(); rb->velocity = {q.x, q.y}; rb->WakeUp(); }
                else if (p == "gravityScale") { rb->gravityScale = v.AsFloat(); rb->WakeUp(); }
                else if (p == "angularVelocity") { rb->angularVelocity = v.AsFloat(); rb->WakeUp(); }
                else if (p == "mass") {
                    if (!(v.AsFloat() > 0.0f)) throw ScriptError("Rigidbody2D mass must be more than 0");
                    rb->mass = v.AsFloat();
                }
                else if (p == "drag" || p == "linearDamping") rb->drag = std::max(0.0f, v.AsFloat());
                else if (p == "angularDrag" || p == "angularDamping") rb->angularDrag = std::max(0.0f, v.AsFloat());
                else if (p == "isKinematic") { rb->bodyType = v.AsBool() ? Rigidbody2D::BodyType::Kinematic : Rigidbody2D::BodyType::Dynamic; rb->WakeUp(); }
                else if (p == "bodyType") {
                    const int k = SafeInt(v.AsFloat());
                    rb->bodyType = k == 1 ? Rigidbody2D::BodyType::Kinematic : k == 2 ? Rigidbody2D::BodyType::Static : Rigidbody2D::BodyType::Dynamic;
                    rb->WakeUp();
                }
                else if (p == "freezeRotation") rb->freezeRotation = v.AsBool();
                else if (p == "bounciness") rb->bounciness = v.AsFloat();
                else if (p == "friction") rb->friction = v.AsFloat();
                else if (p == "position") { if (g->transform) g->transform->SetPosition(v.AsVec3()); rb->WakeUp(); }
                else if (p != "enabled") unknownMember("Rigidbody2D", p);
                return Value{};
            }
            if (t == "Rigidbody") if (auto* rb = g->GetComponent<Rigidbody3D>()) {
                if (p == "velocity" || p == "linearVelocity") { rb->velocity = v.AsVec3(); rb->WakeUp(); }
                else if (p == "angularVelocity") { rb->angularVelocity = v.AsVec3(); rb->WakeUp(); }
                else if (p == "mass") {
                    if (!(v.AsFloat() > 0.0f)) throw ScriptError("Rigidbody mass must be more than 0");
                    rb->mass = v.AsFloat();
                }
                else if (p == "drag" || p == "linearDamping") rb->drag = std::max(0.0f, v.AsFloat());
                else if (p == "angularDrag" || p == "angularDamping") rb->angularDrag = std::max(0.0f, v.AsFloat());
                else if (p == "gravityScale") { rb->gravityScale = v.AsFloat(); rb->WakeUp(); }
                else if (p == "useGravity") {   // off = gravityScale 0; on = 1 unless a scale is already set
                    if (!v.AsBool()) rb->gravityScale = 0.0f;
                    else if (rb->gravityScale == 0.0f) rb->gravityScale = 1.0f;
                    rb->WakeUp();
                }
                else if (p == "isKinematic") { rb->bodyType = v.AsBool() ? Rigidbody3D::BodyType::Kinematic : Rigidbody3D::BodyType::Dynamic; rb->WakeUp(); }
                else if (p == "freezeRotation") rb->freezeRotation = v.AsBool();
                else if (p == "bounciness") rb->bounciness = v.AsFloat();
                else if (p == "friction") rb->friction = v.AsFloat();
                else if (p == "maxFallSpeed") rb->maxFallSpeed = std::max(0.0f, v.AsFloat());
                else if (p == "position") { if (g->transform) g->transform->SetPosition(v.AsVec3()); rb->WakeUp(); }
                else if (p != "enabled") unknownMember("Rigidbody", p);
                return Value{};
            }
            if (t == "AudioSource") if (auto* au = g->GetComponent<AudioSource>()) {
                if (p == "volume") au->volume = v.AsFloat();
                if (p == "pitch") au->pitch = v.AsFloat();
                if (p == "pan") au->pan = v.AsFloat();
                if (p == "lowPass") au->lowPass = v.AsFloat();
                if (p == "doppler") au->doppler = v.AsBool();
                if (p == "dopplerScale") au->dopplerScale = v.AsFloat();
                if (p == "occlude") au->occlude = v.AsBool();
                if (p == "occlusionVolume") au->occlusionVolume = v.AsFloat();
                if (p == "occlusionLowPass") au->occlusionLowPass = v.AsFloat();
                if (p == "distanceLowPass") au->distanceLowPass = v.AsBool();
                if (p == "distanceLowPassHz") au->distanceLowPassHz = v.AsFloat();
            }
            if (t == "NetworkObject") if (auto* o = g->GetComponent<NetworkObject>()) {
                if (p == "netId") o->netId = v.AsString();
                if (p == "owned") o->SetOwned(v.AsBool());
                if (p == "authority") o->authority = (NetworkObject::Authority)SafeInt(v.AsFloat());
            }
            if (t == "SyncVar") if (auto* sv = g->GetComponent<SyncVar>()) {
                if (p == "name") sv->name = v.AsString();
                if (p == "value") sv->Set(v.AsString());
            }
            if (t == "FirstPersonHands") FpHandsSet(g->GetComponent<FirstPersonHands>(), p, v);
            return Value{};
        };
        b["__call_component"] = [this, compTarget](std::vector<Value>& a) -> Value {
            if (a.size() < 2) return Value{};
            std::string t;
            GameObject* g = compTarget(a[0], t); if (!g) return Value{};
            ActAsObject as(rt.host, g);   // Animator / IK / Transform calls run on the referenced object
            const std::string m = a[1].AsString();
            std::vector<Value> rest(a.begin() + 2, a.end());
            auto arg = [&](std::size_t i) { return i < rest.size() ? rest[i] : Value{}; };
            if (t == "Animator" || t == "Animation") {
                if (m == "SetTrigger") { std::vector<Value> x{arg(0)}; return rt.Call("anim_trigger", x); }
                if (m == "SetBool")    { std::vector<Value> x{arg(0), Value{arg(1).AsBool() ? 1.0f : 0.0f}}; return rt.Call("anim_set_bool", x); }
                if (m == "SetFloat" || m == "SetInteger") { std::vector<Value> x{arg(0), arg(1)}; return rt.Call("anim_set_float", x); }
                if (m == "GetFloat") return rt.Call("anim_get_float", rest);
                if (m == "GetBool") return rt.Call("anim_get_bool", rest);
                if (m == "GetInt" || m == "GetInteger") return rt.Call("anim_get_int", rest);
                if (m == "SetLayerWeight") return rt.Call("anim_set_layer_weight", rest);
                if (m == "GetLayerWeight") return rt.Call("anim_get_layer_weight", rest);
                if (m == "SetRootMotion") return rt.Call("set_root_motion", rest);
                if (m == "SetCharacterModel") return rt.Call("set_character_model", rest);
                if (m == "Play" || m == "CrossFade" || m == "CrossFadeInFixedTime") {
                    std::vector<Value> x{arg(0)};
                    if (m != "Play" && rest.size() >= 2) x.push_back(arg(1));
                    return rt.Call("play_action", x);
                }
                return Value{};
            }
            if (t == "IK") {
                if (m == "SetKindWeight") return rt.Call("ik_set_weight", rest);
                if (m == "SetLimbTarget") return rt.Call("ik_set_target", rest);
                if (m == "ClearLimbTarget") return rt.Call("ik_clear_target", rest);
                if (m == "LookAt") return rt.Call("ik_look_at", rest);
                if (m == "SetWeight") return rt.Call("ik_set_weight", rest);
                return Value{};
            }
            if (t == "Rigidbody2D") if (auto* rb = g->GetComponent<Rigidbody2D>()) {
                // A force is a vector or two numbers, then an optional mode: ForceMode2D.Force (0) /
                // ForceMode2D.Impulse (1); ForceMode.Impulse (2) and VelocityChange (3) are taken too.
                const bool vecFirst = arg(0).IsVec3();
                Vec3 f = vecFirst ? arg(0).AsVec3() : Vec3{arg(0).AsFloat(), arg(1).AsFloat(), 0.0f};
                const std::size_t modeAt = vecFirst ? 1 : 2;
                const int mode = rest.size() > modeAt ? SafeInt(arg(modeAt).AsFloat()) : 0;
                const ForceMode fm = mode == 3 ? ForceMode::VelocityChange : (mode == 1 || mode == 2) ? ForceMode::Impulse : ForceMode::Force;
                if (m == "AddForce") { rb->AddForce(Vec2{f.x, f.y}, fm); return Value{}; }
                if (m == "AddRelativeForce") {   // in the body's own axes
                    Vec3 w = g->transform ? g->transform->Rotation() * Vec3{f.x, f.y, 0.0f} : f;
                    rb->AddForce(Vec2{w.x, w.y}, fm); return Value{};
                }
                if (m == "AddImpulse") { rb->AddImpulse(Vec2{f.x, f.y}); return Value{}; }
                if (m == "AddTorque") {   // (torque [, mode]); an impulse is spread over one physics step
                    const float tq = arg(0).AsFloat();
                    const int md = rest.size() > 1 ? SafeInt(arg(1).AsFloat()) : 0;
                    const float step = Time::FixedDeltaTime() > 0.0f ? Time::FixedDeltaTime() : 1.0f / 60.0f;
                    rb->AddTorque(md == 0 ? tq : tq / step);
                    return Value{};
                }
                if (m == "AddForceAtPosition") {
                    Vec3 pt = vecFirst ? arg(1).AsVec3() : Vec3{arg(2).AsFloat(), arg(3).AsFloat(), 0.0f};
                    rb->AddForceAtPosition(Vec2{f.x, f.y}, Vec2{pt.x, pt.y});
                    return Value{};
                }
                if (m == "MoveRotation") { if (g->transform) g->transform->localRotation = Quat::Euler({0, 0, arg(0).AsFloat()}); return Value{}; }
                if (m == "WakeUp") { rb->WakeUp(); return Value{}; }
                if (m == "Sleep") { rb->sleeping = true; return Value{}; }
                if (m == "IsSleeping") return Value{rb->sleeping};
                if (m == "IsAwake") return Value{!rb->sleeping};
                if (m != "AddExplosionForce" && m != "MovePosition")
                    throw ScriptError("Rigidbody2D has no method '" + m + "'");
                if (m == "AddExplosionForce") {
                    float up = 0.0f;
                    Vec2 c; float force = 0.0f, radius = 0.0f;
                    if (arg(0).IsVec3()) {
                        Vec3 v = arg(0).AsVec3();
                        c = {v.x, v.y};
                        force = rest.size() > 1 ? arg(1).AsFloat() : 0.0f;
                        radius = rest.size() > 2 ? arg(2).AsFloat() : 0.0f;
                        up = rest.size() > 3 ? arg(3).AsFloat() : 0.0f;
                    } else {
                        c = {arg(0).AsFloat(), rest.size() > 1 ? arg(1).AsFloat() : 0.0f};
                        force = rest.size() > 2 ? arg(2).AsFloat() : 0.0f;
                        radius = rest.size() > 3 ? arg(3).AsFloat() : 0.0f;
                        up = rest.size() > 4 ? arg(4).AsFloat() : 0.0f;
                    }
                    rb->AddExplosionForce(force, c, radius, up);
                }
                if (m == "MovePosition" && g->transform) g->transform->localPosition = {f.x, f.y, g->transform->localPosition.z};
                return Value{};
            }
            if (t == "Rigidbody") if (auto* rb = g->GetComponent<Rigidbody3D>()) {
                // #888 F109: only AddForce (without its ForceMode) and MovePosition worked; the rest
                // returned null without a word. A force is a vector or three numbers, then an optional
                // ForceMode (Force 0, Acceleration 1, Impulse 2, VelocityChange 3).
                const bool vecFirst = arg(0).IsVec3();
                Vec3 f = vecFirst ? arg(0).AsVec3() : Vec3{arg(0).AsFloat(), arg(1).AsFloat(), arg(2).AsFloat()};
                const std::size_t next = vecFirst ? 1 : 3;   // the argument after the force
                auto modeAt = [&](std::size_t i) {
                    const int md = rest.size() > i ? SafeInt(arg(i).AsFloat()) : 0;
                    return md == 1 ? ForceMode::Acceleration : md == 2 ? ForceMode::Impulse
                         : md == 3 ? ForceMode::VelocityChange : ForceMode::Force;
                };
                auto local = [&](const Vec3& v) { return g->transform ? g->transform->Rotation() * v : v; };
                // Torque: Force adds to the step's torque; an Impulse is spread over one physics
                // step; Acceleration and VelocityChange change the spin directly (radians per second).
                auto torque = [&](const Vec3& tq, ForceMode md) {
                    const float step = Time::FixedDeltaTime() > 0.0f ? Time::FixedDeltaTime() : 1.0f / 60.0f;
                    if (md == ForceMode::Force) rb->AddTorque(tq);
                    else if (md == ForceMode::Impulse) rb->AddTorque(tq * (1.0f / step));
                    else if (md == ForceMode::Acceleration) { rb->angularVelocity = rb->angularVelocity + tq * step; rb->WakeUp(); }
                    else { rb->angularVelocity = rb->angularVelocity + tq; rb->WakeUp(); }
                };
                if (m == "AddForce") { rb->AddForce(f, modeAt(next)); return Value{}; }
                if (m == "AddRelativeForce") { rb->AddForce(local(f), modeAt(next)); return Value{}; }
                if (m == "AddImpulse") { rb->AddImpulse(f); return Value{}; }
                if (m == "AddTorque") { torque(f, modeAt(next)); return Value{}; }
                if (m == "AddRelativeTorque") { torque(local(f), modeAt(next)); return Value{}; }
                if (m == "AddForceAtPosition") {   // (force, point [, mode]); Impulse / VelocityChange kick at once
                    const Vec3 pt = vecFirst ? arg(1).AsVec3() : Vec3{arg(3).AsFloat(), arg(4).AsFloat(), arg(5).AsFloat()};
                    const ForceMode md = modeAt(vecFirst ? 2 : 6);
                    const float step = Time::FixedDeltaTime() > 0.0f ? Time::FixedDeltaTime() : 1.0f / 60.0f;
                    const float scale = (md == ForceMode::Impulse || md == ForceMode::VelocityChange) ? 1.0f / step : 1.0f;
                    rb->AddForceAtPosition(f * scale, pt);
                    return Value{};
                }
                if (m == "AddExplosionForce") {   // (force, center, radius [, upwards])
                    const Vec3 c = arg(1).AsVec3();
                    rb->AddExplosionForce(arg(0).AsFloat(), c, arg(2).AsFloat(), rest.size() > 3 ? arg(3).AsFloat() : 0.0f);
                    return Value{};
                }
                if (m == "MovePosition") { if (g->transform) g->transform->localPosition = f; return Value{}; }
                if (m == "MoveRotation") { if (g->transform) g->transform->localRotation = Quat::Euler(arg(0).AsVec3()); return Value{}; }
                if (m == "WakeUp") { rb->WakeUp(); return Value{}; }
                if (m == "Sleep") { rb->sleeping = true; return Value{}; }
                if (m == "IsSleeping") return Value{rb->sleeping};
                if (m == "IsAwake") return Value{!rb->sleeping};
                throw ScriptError("Rigidbody has no method '" + m + "'");
            }
            if (t == "AudioSource") if (auto* au = g->GetComponent<AudioSource>()) {
                if (m == "Play") au->Play();
                if (m == "Stop") au->Stop();
                if (m == "Pause") au->Pause();
                if (m == "Resume") au->Resume();
                if (m == "PlayOneShot") au->PlayOneShot(rest.empty() ? 1.0f : arg(0).AsFloat());
                if (m == "FadeIn") au->FadeIn(rest.empty() ? 1.0f : arg(0).AsFloat());
                if (m == "FadeOut") au->FadeOut(rest.empty() ? 1.0f : arg(0).AsFloat());
                return Value{};
            }
            if (t == "NetworkObject") if (auto* o = g->GetComponent<NetworkObject>()) {
                if (m == "Rpc" && !rest.empty()) o->Rpc(arg(0).AsString(), rest.size() > 1 ? arg(1).AsString() : std::string{});
                if (m == "RpcTo" && rest.size() >= 2) o->RpcTo(SafeUInt32(arg(0).AsFloat()), arg(1).AsString(), rest.size() > 2 ? arg(2).AsString() : std::string{});
                if (m == "RpcReliable" && !rest.empty()) o->RpcReliable(arg(0).AsString(), rest.size() > 1 ? arg(1).AsString() : std::string{});
                return Value{};
            }
            if (t == "SyncVar") if (auto* sv = g->GetComponent<SyncVar>()) {
                if (m == "Set") sv->Set(arg(0).AsString());
                if (m == "Get") return Value{sv->Get()};
                return Value{};
            }
            if (t == "FirstPersonHands") { FpHandsCall(g->GetComponent<FirstPersonHands>(), m); return Value{}; }
            if (t == "Transform") {
                std::vector<Value> x = rest;
                return rt.Call("transform." + m, x);
            }
            return Value{};
        };
        b["ForceMode2D.Impulse"] = [](std::vector<Value>&) { return Value{1.0f}; };
}

} // namespace okay
