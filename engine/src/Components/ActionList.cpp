#include "okay/Components/ActionList.hpp"
#include "okay/Components/AudioSource.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/SpriteRenderer.hpp"
#include "okay/Components/ParticleSystem.hpp"
#include "okay/Components/SpriteAnimator.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Components/UIProgressBar.hpp"
#include "okay/Components/UIRadialProgress.hpp"
#include "okay/Components/UITextBind.hpp"        // Resolve {var}/{pref} tokens in text
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/NativeUIActions.hpp"   // InvokeNativeUIAction (survival verbs)
#include "okay/Components/Consumables.hpp"       // UseIndex
#include "okay/Physics/Rigidbody2D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Render/Lighting.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Scene/SceneManager.hpp"
#include "okay/Net/NetworkManager.hpp"
#include "okay/Platform/Steam/Steam.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Core/Prefs.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Math/Mathf.hpp"
#include "okay/Core/Random.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Core/Time.hpp"

#include <cctype>
#include <cmath>
#include <cstdlib>

namespace okay {

std::unordered_map<std::string, float>& ActionList::Vars() {
    static std::unordered_map<std::string, float> v;
    return v;
}
void ActionList::ResetVars() { Vars().clear(); }

namespace {
float Num(const ActionList::Item& it, std::size_t i) {
    return i < it.args.size() ? (float)std::atof(it.args[i].c_str()) : 0.0f;
}
std::string Str(const ActionList::Item& it, std::size_t i) {
    return i < it.args.size() ? it.args[i] : std::string{};
}
// Join args from index i onward (for free-text instructions like set_text/log).
std::string Rest(const ActionList::Item& it, std::size_t i) {
    std::string s;
    for (; i < it.args.size(); ++i) { if (!s.empty()) s += " "; s += it.args[i]; }
    return s;
}

// Resolve a raycast direction token into a world-space direction for `go`.
// Keywords are relative to the object's facing (forward/back/up/down/left/right);
// "toward:<name>" aims at a named object. Returns false if the token isn't a known
// keyword (so the caller can fall back to raw "x y z" numbers).
bool RayDirFromToken(GameObject* go, const std::string& tok, Vec3& dir) {
    if (!go || !go->transform) return false;
    Transform* t = go->transform;
    if (tok == "forward") { dir = t->Forward();           return true; }
    if (tok == "back")    { dir = t->Forward() * -1.0f;   return true; }
    if (tok == "up")      { dir = t->Up();                return true; }
    if (tok == "down")    { dir = t->Up() * -1.0f;        return true; }
    if (tok == "right")   { dir = t->Right();             return true; }
    if (tok == "left")    { dir = t->Right() * -1.0f;     return true; }
    if (tok.rfind("toward:", 0) == 0) {
        Scene* s = go->scene();
        GameObject* tgt = s ? s->Find(tok.substr(7)) : nullptr;
        if (!tgt || !tgt->transform) return false;
        Vec3 d = tgt->transform->Position() - go->transform->Position();
        if (d.SqrMagnitude() < 1e-8f) return false;
        dir = d.Normalized();
        return true;
    }
    return false;
}

// Cast a ray for an action item. args[base] is a direction token (keyword or the
// first of a raw "x y z" triple, kept for back-compat); the distance follows.
// Returns the nearest hit (the object's own colliders are ignored).
RaycastHit3D ActionRaycast(GameObject* go, const ActionList::Item& it, std::size_t base) {
    RaycastHit3D miss;
    Scene* s = go ? go->scene() : nullptr;
    if (!s || !go || !go->transform) return miss;
    Vec3 dir; float dist;
    if (RayDirFromToken(go, Str(it, base), dir)) {
        dist = it.args.size() > base + 1 ? Num(it, base + 1) : 100.0f;
    } else {
        dir = {Num(it, base + 0), Num(it, base + 1), Num(it, base + 2)};
        if (dir.SqrMagnitude() < 1e-8f) dir = go->transform->Forward();
        dist = it.args.size() > base + 3 ? Num(it, base + 3) : 100.0f;
    }
    if (dist <= 0.0f) dist = 1e9f;
    return s->physics3D().Raycast(*s, go->transform->Position(), dir.Normalized(), dist, go);
}
} // namespace

std::string ActionList::ToText() const {
    std::string out = "trigger " + std::to_string((int)trigger) + " " +
                      (triggerKey.empty() ? std::string("-") : triggerKey) + " " +
                      (once ? "1" : "0") + "\n";
    auto emit = [&](const char* tag, const std::vector<Item>& list) {
        for (const Item& it : list) {
            out += tag; out += ' '; out += it.op;
            for (const std::string& a : it.args) { out += ' '; out += a; }
            out += '\n';
        }
    };
    emit("c", conditions);
    emit("i", instructions);
    return out;
}

void ActionList::FromText(const std::string& text) {
    trigger = Trigger::OnStart; triggerKey = "e"; once = false;
    conditions.clear(); instructions.clear();
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t nl = text.find('\n', pos);
        std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = (nl == std::string::npos) ? text.size() : nl + 1;
        // tokenize on whitespace
        std::vector<std::string> tok;
        std::size_t i = 0;
        while (i < line.size()) {
            while (i < line.size() && std::isspace((unsigned char)line[i])) ++i;
            std::size_t b = i;
            while (i < line.size() && !std::isspace((unsigned char)line[i])) ++i;
            if (i > b) tok.push_back(line.substr(b, i - b));
        }
        if (tok.empty()) continue;
        if (tok[0] == "trigger") {
            if (tok.size() > 1) trigger = (Trigger)std::atoi(tok[1].c_str());
            if (tok.size() > 2) triggerKey = (tok[2] == "-") ? std::string{} : tok[2];
            if (tok.size() > 3) once = (tok[3] == "1");
        } else if (tok[0] == "c" || tok[0] == "i") {
            Item it;
            if (tok.size() > 1) it.op = tok[1];
            for (std::size_t k = 2; k < tok.size(); ++k) it.args.push_back(tok[k]);
            (tok[0] == "c" ? conditions : instructions).push_back(std::move(it));
        }
    }
}

void ActionList::Start() {
    if (trigger == Trigger::OnStart) Fire();
}

bool ActionList::EvalConditions() {
    for (const Item& c : conditions) {
        const std::string& op = c.op;
        bool ok = true;
        auto distTo = [&](const std::string& name, float& out) -> bool {
            Scene* s = GetScene();
            GameObject* g = s ? s->Find(name) : nullptr;
            if (!g || !gameObject) return false;
            Vec3 a = gameObject->transform->Position(), b = g->transform->Position();
            float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
            out = Mathf::Sqrt(dx * dx + dy * dy + dz * dz);
            return true;
        };
        if (op == "always" || op.empty()) ok = true;
        else if (op == "key")      ok = !Str(c, 0).empty() && Input::GetKey(Str(c, 0)[0]);
        else if (op == "key_down") ok = !Str(c, 0).empty() && Input::GetKeyDown(Str(c, 0)[0]);
        else if (op == "mouse")    ok = Input::GetMouseButton((int)Num(c, 0));
        else if (op == "chance")   ok = Random::Shared().Range(0.0f, 1.0f) < Num(c, 0);
        else if (op == "var_eq")   ok = Mathf::Approximately(Vars()[Str(c, 0)], Num(c, 1));
        else if (op == "var_neq")  ok = !Mathf::Approximately(Vars()[Str(c, 0)], Num(c, 1));
        else if (op == "var_gt")   ok = Vars()[Str(c, 0)] > Num(c, 1);
        else if (op == "var_lt")   ok = Vars()[Str(c, 0)] < Num(c, 1);
        else if (op == "var_ge")   ok = Vars()[Str(c, 0)] >= Num(c, 1);
        else if (op == "var_le")   ok = Vars()[Str(c, 0)] <= Num(c, 1);
        else if (op == "key_up")   ok = !Str(c, 0).empty() && Input::GetKeyUp(Str(c, 0)[0]);
        else if (op == "mouse_down") ok = Input::GetMouseButtonDown((int)Num(c, 0));
        else if (op == "prefs_eq") ok = Mathf::Approximately(Prefs::GetFloat(Str(c, 0), 0.0f), Num(c, 1));
        else if (op == "prefs_gt") ok = Prefs::GetFloat(Str(c, 0), 0.0f) > Num(c, 1);
        else if (op == "prefs_lt") ok = Prefs::GetFloat(Str(c, 0), 0.0f) < Num(c, 1);
        else if (op == "has_tag")  ok = gameObject && gameObject->tag == Str(c, 0);
        else if (op == "is_active")ok = gameObject && gameObject->active;
        else if (op == "dist_lt")  { float d; ok = distTo(Str(c, 0), d) && d < Num(c, 1); }
        else if (op == "dist_gt")  { float d; ok = distTo(Str(c, 0), d) && d > Num(c, 1); }
        else if (op == "exists")   { Scene* s = GetScene(); ok = s && s->Find(Str(c, 0)) != nullptr; }
        else if (op == "raycast") {
            // Cast a ray in a chosen direction; pass if it hits any collider.
            // Args: <direction> [distance].  direction = forward/back/up/down/
            // left/right (relative to facing) or toward:<object>.
            ok = ActionRaycast(gameObject, c, 0).hit;
        }
        else if (op == "raycast_tag") {
            // Like raycast, but only passes if the hit object has the given tag.
            // Args: <tag> <direction> [distance].
            RaycastHit3D h = ActionRaycast(gameObject, c, 1);
            ok = h.hit && h.gameObject && h.gameObject->tag == Str(c, 0);
        }
        else if (op == "raycast_name") {
            // Like raycast, but only passes if it hits the named object.
            // Args: <object> <direction> [distance].
            RaycastHit3D h = ActionRaycast(gameObject, c, 1);
            ok = h.hit && h.gameObject && h.gameObject->name == Str(c, 0);
        }
        if (!ok) return false;   // all conditions must pass (AND)
    }
    return true;
}

void ActionList::Fire() {
    if (m_running) return;
    if (once && m_fired) return;
    if (!EvalConditions()) return;
    m_running = true; m_ip = 0; m_wait = 0.0f;
}

void ActionList::Update(float dt) {
    // ---- Triggers ----
    if (!m_running) {
        if (trigger == Trigger::OnUpdate) Fire();
        else if (trigger == Trigger::OnKey && !triggerKey.empty() && Input::GetKeyDown(triggerKey[0])) Fire();
        else if (trigger == Trigger::OnKeyUp && !triggerKey.empty() && Input::GetKeyUp(triggerKey[0])) Fire();
        else if (trigger == Trigger::OnClick) {
            // UI buttons report their own click; world objects arrive via m_pending
            // (OnMouseClick). Either path fires the list.
            if (auto* b = gameObject ? gameObject->GetComponent<UIButton>() : nullptr)
                if (b->WasClicked()) Fire();
        }
        // Collision / trigger / mouse triggers are latched by the event callbacks.
        if (!m_running && m_pending) { m_pending = false; Fire(); }
    }
    if (!m_running) return;

    // ---- Run instructions until a Wait or the end of the list ----
    if (m_wait > 0.0f) { m_wait -= dt; if (m_wait > 0.0f) return; }

    Scene* scene = GetScene();
    Transform* t = transform;
    int guard = 0;   // cap steps per frame so a goto-loop without a wait can't hang
    while (m_ip < instructions.size()) {
        if (++guard > 10000) break;
        const Item& it = instructions[m_ip];
        const std::string& op = it.op;
        ++m_ip;

        if (op == "wait") { m_wait = Num(it, 0); if (m_wait > 0.0f) return; }
        else if (op == "stop") { m_ip = instructions.size(); }
        else if (op == "goto") {
            int target = (int)Num(it, 0);
            if (target >= 0 && target < (int)instructions.size()) m_ip = (std::size_t)target;
        }
        else if (op == "spawn3") {
            if (scene) {
                GameObject* g = SceneSerializer::InstantiateFromFile(*scene, Str(it, 0), nullptr);
                if (g && g->transform)
                    g->transform->localPosition = {Num(it, 1), Num(it, 2), Num(it, 3)};
            }
        }
        else if (op == "set_timescale") { Time::SetTimeScale(Num(it, 0)); }
        else if (op == "send") {
            if (scene) for (ActionList* a : scene->FindObjectsOfType<ActionList>())
                a->ReceiveMessage(Str(it, 0));
        }
        else if (op == "move") { if (t) t->Translate({Num(it, 0), Num(it, 1), Num(it, 2)}); }
        else if (op == "set_pos") { if (t) t->localPosition = {Num(it, 0), Num(it, 1), Num(it, 2)}; }
        else if (op == "rotate") { if (t) t->Rotate({Num(it, 1), Num(it, 2), Num(it, 0)}); }
        else if (op == "set_scale") { if (t) { float v = Num(it, 0); t->localScale = {v, v, v}; } }
        else if (op == "set_scale3") { if (t) t->localScale = {Num(it, 0), Num(it, 1), Num(it, 2)}; }
        else if (op == "move_toward") {
            if (t) {
                Vec3 p = t->localPosition, tg{Num(it, 0), Num(it, 1), Num(it, 2)};
                t->localPosition = Vec3::MoveTowards(p, tg, Num(it, 3));
            }
        }
        else if (op == "look_at") {
            Scene* sc2 = GetScene();
            GameObject* g = sc2 ? sc2->Find(Str(it, 0)) : nullptr;
            if (g && t && gameObject) {
                Vec3 me = gameObject->transform->Position(), ot = g->transform->Position();
                float deg = std::atan2(ot.y - me.y, ot.x - me.x) * 57.2957795f;
                t->localRotation = Quat::Euler({0, 0, deg});
            }
        }
        else if (op == "set_var") { Vars()[Str(it, 0)] = Num(it, 1); }
        else if (op == "add_var") { Vars()[Str(it, 0)] += Num(it, 1); }
        else if (op == "set_active") {
            // set_active <1|0> [object] — no name => this object; a name => that one.
            bool on = Num(it, 0) != 0.0f;
            const std::string& name = Str(it, 1);
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
        else if (op == "set_cam") {
            if (scene && scene->mainCamera) {
                Transform* ct = scene->mainCamera->gameObject->transform;
                ct->localPosition.x = Num(it, 0); ct->localPosition.y = Num(it, 1);
            }
        }
        else if (op == "set_bg") {
            if (scene && scene->mainCamera)
                scene->mainCamera->backgroundColor = {Num(it, 0), Num(it, 1), Num(it, 2), 1.0f};
        }
        else if (op == "set_light") {
            SceneLight::SetDirection({Num(it, 0), Num(it, 1), Num(it, 2)});
        }
        else if (op == "set_ambient") { SceneLight::SetAmbient(Num(it, 0)); }
        else if (op == "set_text") {
            // Interpolate {var}/{pref} tokens so you can show a variable's live value:
            //   Set Text  "Score: {score}"   ->  "Score: 42"
            if (gameObject) if (auto* tr = gameObject->GetComponent<TextRenderer>())
                tr->text = UITextBind::Resolve(Rest(it, 0));
        }
        else if (op == "set_text_on") {
            // Set a named object's text label, with {var}/{pref} interpolation.
            if (scene) if (GameObject* g = scene->Find(Str(it, 0)))
                if (auto* tr = g->GetComponent<TextRenderer>())
                    tr->text = UITextBind::Resolve(Rest(it, 1));
        }
        else if (op == "set_bar") {
            // Fill a named progress bar from a variable: set_bar <object> <var> [max].
            // value = var / max (clamped 0..1); max defaults to 1.
            if (scene) if (GameObject* g = scene->Find(Str(it, 0))) {
                float mx = Num(it, 2); if (mx == 0.0f) mx = 1.0f;
                float frac = Mathf::Clamp01(Vars()[Str(it, 1)] / mx);
                if (auto* pb = g->GetComponent<UIProgressBar>())    pb->SetValue(frac);
                if (auto* rp = g->GetComponent<UIRadialProgress>()) rp->SetValue(frac);
            }
        }
        else if (op == "play_sound") {
            if (gameObject) if (auto* au = gameObject->GetComponent<AudioSource>()) au->Play();
        }
        else if (op == "destroy") { if (scene && gameObject) { scene->Destroy(gameObject); return; } }
        else if (op == "destroy_obj") {
            if (scene) if (GameObject* g = scene->Find(Str(it, 0))) scene->Destroy(g);
        }
        else if (op == "activate")   { if (scene) if (GameObject* g = scene->Find(Str(it, 0))) g->active = true; }
        else if (op == "deactivate") { if (scene) if (GameObject* g = scene->Find(Str(it, 0))) g->active = false; }
        else if (op == "spawn") {
            if (scene) {
                GameObject* g = SceneSerializer::InstantiateFromFile(*scene, Str(it, 0), nullptr);
                if (g && g->transform) g->transform->localPosition = {Num(it, 1), Num(it, 2), Num(it, 3)};
            }
        }
        else if (op == "net_host" || op == "net_join" || op == "net_send" || op == "net_disconnect") {
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
        else if (op == "steam_unlock")   { Steam::Get().UnlockAchievement(Str(it, 0)); Steam::Get().StoreStats(); }
        else if (op == "steam_set_stat") { Steam::Get().SetStat(Str(it, 0), Num(it, 1)); Steam::Get().StoreStats(); }
        else if (op == "steam_inc_stat") { Steam::Get().IncrementStat(Str(it, 0), Num(it, 1)); Steam::Get().StoreStats(); }
        else if (op == "load_scene") { if (scene) scene->RequestLoad(Str(it, 0)); return; }
        else if (op == "load_scene_index") { if (scene) SceneManager::LoadScene(*scene, (int)Num(it, 0)); return; }
        else if (op == "load_next_scene")  { if (scene) SceneManager::LoadNextScene(*scene); return; }
        else if (op == "mul_var") { Vars()[Str(it, 0)] *= Num(it, 1); }
        else if (op == "div_var") { float d = Num(it, 1); if (d != 0.0f) Vars()[Str(it, 0)] /= d; }
        else if (op == "copy_var") { Vars()[Str(it, 0)] = Vars()[Str(it, 1)]; }
        else if (op == "rand_var") { Vars()[Str(it, 0)] = Random::Shared().Range(Num(it, 1), Num(it, 2)); }
        else if (op == "set_prefs") { Prefs::SetFloat(Str(it, 0), Num(it, 1)); }
        else if (op == "add_prefs") { Prefs::SetFloat(Str(it, 0), Prefs::GetFloat(Str(it, 0), 0.0f) + Num(it, 1)); }
        // Read a saved value (e.g. the Health component's "health") into a variable,
        // so a visual script can grab a stat published by another component.
        else if (op == "get_prefs") { Vars()[Str(it, 0)] = Prefs::GetFloat(Str(it, 1), Num(it, 2)); }
        else if (op == "save_prefs") { Prefs::Save(Str(it, 0).empty() ? "game.okayprefs" : Str(it, 0)); }
        else if (op == "set_tag") { if (gameObject) gameObject->tag = Str(it, 0); }
        else if (op == "set_timescale_var") { Vars()[Str(it, 0)] = Time::TimeScale(); }
        else if (op == "log") { Log::Info("[actions] ", Rest(it, 0)); }
        else if (op == "set_rotation")  { if (t) t->localRotation = Quat::Euler({0, 0, Num(it, 0)}); }
        else if (op == "set_rotation3") { if (t) t->localRotation = Quat::Euler({Num(it, 0), Num(it, 1), Num(it, 2)}); }
        else if (op == "velocity3") {
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
            if (scene) if (GameObject* g = scene->Find(Str(it, 0)))
                for (ActionList* a : g->GetComponents<ActionList>()) a->ReceiveMessage(Str(it, 1));
        }
        else if (op == "raycast") {              // cast a ray, store the result in variables
            // Args: <direction> [distance] [prefix].  direction = forward/back/up/
            // down/left/right or toward:<object>. Writes <prefix>_hit (1/0),
            // <prefix>_dist and <prefix>_x/_y/_z (the hit point) so later
            // instructions can branch on them (if_goto) or use the position.
            if (scene && gameObject) {
                RaycastHit3D h = ActionRaycast(gameObject, it, 0);
                std::string pre = it.args.size() > 2 ? Str(it, 2) : std::string("ray");
                if (pre.empty()) pre = "ray";
                Vars()[pre + "_hit"]  = h.hit ? 1.0f : 0.0f;
                Vars()[pre + "_dist"] = h.distance;
                Vars()[pre + "_x"] = h.point.x;
                Vars()[pre + "_y"] = h.point.y;
                Vars()[pre + "_z"] = h.point.z;
            }
        }
        else if (op == "if_goto") {              // conditional jump: var <op> value -> instruction line
            const std::string& cmp = Str(it, 1);
            float lhs = Vars()[Str(it, 0)], rhs = Num(it, 2); int line = (int)Num(it, 3);
            bool pass = (cmp == "eq")  ? Mathf::Approximately(lhs, rhs)
                      : (cmp == "neq") ? !Mathf::Approximately(lhs, rhs)
                      : (cmp == "gt")  ? (lhs > rhs)
                      : (cmp == "lt")  ? (lhs < rhs)
                      : (cmp == "ge")  ? (lhs >= rhs)
                      : (cmp == "le")  ? (lhs <= rhs) : false;
            if (pass && line >= 0 && line < (int)instructions.size()) m_ip = (std::size_t)line;
        }
        // ---- More general instructions ----
        else if (op == "toggle_active") { if (gameObject) gameObject->active = !gameObject->active; }
        else if (op == "clamp_var") { float& v = Vars()[Str(it, 0)]; v = Mathf::Clamp(v, Num(it, 1), Num(it, 2)); }
        else if (op == "lerp_var") {                  // step a variable toward a target each run
            float& v = Vars()[Str(it, 0)]; float tg = Num(it, 1), st = Num(it, 2);
            v = (v < tg) ? Mathf::Min(tg, v + st) : Mathf::Max(tg, v - st);
        }
        else if (op == "add_var_var") { Vars()[Str(it, 0)] += Vars()[Str(it, 1)]; }
        else if (op == "spawn_at") {                   // spawn a prefab at a named object's position
            if (scene) {
                GameObject* g = SceneSerializer::InstantiateFromFile(*scene, Str(it, 0), nullptr);
                if (g && g->transform) if (GameObject* at = scene->Find(Str(it, 1)))
                    if (at->transform) g->transform->SetPosition(at->transform->Position());
            }
        }
        else if (op == "set_parent") {                 // parent this object under a named object
            if (t && scene) if (GameObject* g = scene->Find(Str(it, 0)))
                if (g->transform) t->SetParent(g->transform);
        }
        else if (op == "unparent") { if (t) t->SetParent(nullptr); }
        else if (op == "pause")  { Time::SetTimeScale(0.0f); }
        else if (op == "resume") { Time::SetTimeScale(1.0f); }
        // ---- Survival kit: drive the native survival components ----
        else if (op == "heal")   { InvokeNativeUIAction(gameObject, "Heal",   Num(it, 0)); }
        else if (op == "hurt")   { InvokeNativeUIAction(gameObject, "Damage", Num(it, 0)); }
        else if (op == "eat")    { InvokeNativeUIAction(gameObject, "Eat",    Num(it, 0)); }
        else if (op == "drink")  { InvokeNativeUIAction(gameObject, "Drink",  Num(it, 0)); }
        else if (op == "craft")  { InvokeNativeUIAction(gameObject, "Craft",  Num(it, 0)); }
        else if (op == "survival") { InvokeNativeUIAction(gameObject, Str(it, 0), Num(it, 1)); }
        else if (op == "survival_on") {
            if (scene) if (GameObject* g = scene->Find(Str(it, 0)))
                InvokeNativeUIAction(g, Str(it, 1), Num(it, 2));
        }
        else if (op == "use_item") {
            if (gameObject) if (auto* cons = gameObject->GetComponent<Consumables>())
                cons->UseIndex((int)Num(it, 0));
        }
        // unknown ops are ignored, so files stay forward-compatible
    }

    // Reached the end of the list.
    m_running = false;
    m_fired = true;
}

} // namespace okay
