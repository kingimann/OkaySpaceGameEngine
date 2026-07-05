#include "okay/Components/ActionList.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include "okay/Components/AudioSource.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/SpriteRenderer.hpp"
#include "okay/Components/ParticleSystem.hpp"
#include "okay/Components/SpriteAnimator.hpp"
#include "okay/Components/Character.hpp"
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
#include "okay/Physics/Collider2D.hpp"
#include "okay/Physics/Collider3D.hpp"
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
#include "okay/Core/Game.hpp"          // win / lose / quit instructions
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
float ActionList::GetVar(const std::string& key) {
    auto& v = Vars();
    auto it = v.find(key);
    if (it != v.end()) return it->second;
    return Prefs::Has(key) ? Prefs::GetFloat(key, 0.0f) : 0.0f;   // fall back to a saved/published value
}

std::unordered_map<std::string, std::vector<float>>& ActionList::Arrays() {
    static std::unordered_map<std::string, std::vector<float>> a;
    return a;
}

std::unordered_map<std::string, std::unordered_map<std::string, float>>& ActionList::Maps() {
    static std::unordered_map<std::string, std::unordered_map<std::string, float>> m;
    return m;
}

std::unordered_map<std::string, std::string>& ActionList::StrVars() {
    static std::unordered_map<std::string, std::string> s;
    return s;
}

void ActionList::ResetVars() { Vars().clear(); Arrays().clear(); Maps().clear(); StrVars().clear(); }

bool& ActionList::DebugPaused() { static bool p = false; return p; }
int&  ActionList::StepBudget()  { static int  b = 0;     return b; }

// A goto/if_goto target: a plain line number, or the index of `label "<name>"`.
int ActionList::ResolveTarget(const std::string& t) const {
    const std::vector<Item>& ins = RunList();
    if (!t.empty() && (std::isdigit((unsigned char)t[0]) || t[0] == '-')) return std::atoi(t.c_str());
    for (std::size_t i = 0; i < ins.size(); ++i)
        if (ins[i].op == "label" && !ins[i].args.empty() && ins[i].args[0] == t)
            return (int)i;
    return -1;
}

// Find the matching `endOp` for a block that opens at `openIp`, honoring nesting.
int ActionList::MatchingEnd(std::size_t openIp, const char* openOp, const char* endOp) const {
    const std::vector<Item>& ins = RunList();
    int depth = 0;
    for (std::size_t i = openIp; i < ins.size(); ++i) {
        if (ins[i].op == openOp) ++depth;
        else if (ins[i].op == endOp) { if (--depth == 0) return (int)i; }
    }
    return -1;
}

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
// An object-name argument: a literal name, or "$textvar" to read the name from a
// text variable — so a For Each Tagged loop can act on "the current object".
std::string ObjName(const ActionList::Item& it, std::size_t i) {
    std::string s = Str(it, i);
    if (s.size() > 1 && s[0] == '$') {
        auto& sv = ActionList::StrVars(); auto v = sv.find(s.substr(1));
        return v != sv.end() ? v->second : std::string();
    }
    return s;
}

// Compare two numbers by a textual operator. Accepts both the word tokens the editor
// writes (eq/neq/gt/lt/ge/le) AND the symbols a user is likely to type by hand
// (==, =, !=, <>, >, <, >=, <=) so `while x < 4` works whichever form they use.
bool CmpPass(const std::string& op, float lhs, float rhs) {
    if (op == "eq"  || op == "==" || op == "=")  return Mathf::Approximately(lhs, rhs);
    if (op == "neq" || op == "!=" || op == "<>") return !Mathf::Approximately(lhs, rhs);
    if (op == "gt"  || op == ">")  return lhs > rhs;
    if (op == "lt"  || op == "<")  return lhs < rhs;
    if (op == "ge"  || op == ">=") return lhs >= rhs;
    if (op == "le"  || op == "<=") return lhs <= rhs;
    return false;
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

// Is this arg one of the optional trailing modifier tokens ("h:<n>" / "ig:<tag>")
// rather than a positional value?
bool IsRayMod(const std::string& a) { return a.rfind("h:", 0) == 0 || a.rfind("ig:", 0) == 0; }

// Cast a ray for an action item. args[base] is a direction token (keyword or the
// first of a raw "x y z" triple, kept for back-compat); the distance follows.
// Optional trailing modifiers (any order): "h:<offset>" raises/lowers the start point
// along world-up (so it can leave the feet); "ig:<tag>" ignores objects with that tag.
// Returns the nearest hit (the caster's own colliders are always ignored).
RaycastHit3D ActionRaycast(GameObject* go, const ActionList::Item& it, std::size_t base) {
    RaycastHit3D miss;
    Scene* s = go ? go->scene() : nullptr;
    if (!s || !go || !go->transform) return miss;
    float height = 0.0f; std::string ignoreTag, onlyTag;
    for (const std::string& a : it.args) {
        if (a.rfind("h:", 0) == 0)  height = (float)std::atof(a.c_str() + 2);
        else if (a.rfind("ig:", 0) == 0) ignoreTag = a.substr(3);
        else if (a.rfind("only:", 0) == 0) onlyTag = a.substr(5);
    }
    Vec3 dir; float dist;
    if (RayDirFromToken(go, Str(it, base), dir)) {
        dist = (it.args.size() > base + 1 && !IsRayMod(Str(it, base + 1))) ? Num(it, base + 1) : 100.0f;
    } else {
        dir = {Num(it, base + 0), Num(it, base + 1), Num(it, base + 2)};
        if (dir.SqrMagnitude() < 1e-8f) dir = go->transform->Forward();
        dist = (it.args.size() > base + 3 && !IsRayMod(Str(it, base + 3))) ? Num(it, base + 3) : 100.0f;
    }
    if (dist <= 0.0f) dist = 1e9f;
    Vec3 origin = go->transform->Position() + Vec3{0.0f, height, 0.0f};
    Vec3 nd = dir.Normalized();

    // Temporarily disable every collider the ray must NOT hit: the caster itself, any
    // object with the ignore tag, and — when an "only" tag is set (a layer-mask-style
    // include filter) — everything that isn't that tag. Both physics engines skip
    // disabled colliders, so this filters in 2D and 3D alike; flags are restored after.
    std::vector<Collider2D*> off2; std::vector<bool> was2;
    std::vector<Collider3D*> off3; std::vector<bool> was3;
    auto muteObj = [&](GameObject* g) {
        if (!g) return;
        for (Collider2D* c : g->GetComponents<Collider2D>()) { off2.push_back(c); was2.push_back(c->enabled); c->enabled = false; }
        for (Collider3D* c : g->GetComponents<Collider3D>()) { off3.push_back(c); was3.push_back(c->enabled); c->enabled = false; }
    };
    for (const auto& up : s->Objects()) {
        GameObject* g = up.get(); if (!g) continue;
        bool mute = (g == go)
                 || (!ignoreTag.empty() && g->tag == ignoreTag)
                 || (!onlyTag.empty()   && g->tag != onlyTag);
        if (mute) muteObj(g);
    }

    RaycastHit3D h3 = s->physics3D().Raycast(*s, origin, nd, dist, go);
    RaycastHit3D result = h3;
    // Also cast in 2D against Collider2D, so a raycast in a top-down / side-scroller game
    // hits sprites too — but only when the ray has a real XY direction (a pure ±Z ray,
    // e.g. 3D "forward", has no 2D meaning). Keep whichever hit is nearer.
    Vec2 d2{nd.x, nd.y};
    if (d2.SqrMagnitude() > 1e-6f) {
        d2 = d2.Normalized();
        RaycastHit2D h2 = s->physics().Raycast(*s, Vec2{origin.x, origin.y}, d2, dist);
        if (h2.hit && (!h3.hit || h2.distance < h3.distance)) {
            result.hit = true; result.gameObject = h2.gameObject;
            result.point = {h2.point.x, h2.point.y, origin.z};
            result.normal = {h2.normal.x, h2.normal.y, 0.0f};
            result.distance = h2.distance;
        }
    }

    for (std::size_t k = 0; k < off2.size(); ++k) off2[k]->enabled = was2[k];
    for (std::size_t k = 0; k < off3.size(); ++k) off3[k]->enabled = was3[k];
    return result;
}
} // namespace

std::string ActionList::ToText() const {
    std::string out;
    if (!name.empty()) out += "name " + name + "\n";     // optional label (may contain spaces)
    out += "trigger " + std::to_string((int)trigger) + " " +
           (triggerKey.empty() ? std::string("-") : triggerKey) + " " +
           (once ? "1" : "0") + "\n";
    // Quote an argument only when it needs it — it contains whitespace, is empty, or
    // holds a quote/backslash — so a value with a space (an object name like
    // "Main Camera") survives the round-trip instead of being split into two args.
    // Backward compatible: plain single-word args are still written bare.
    auto quoteArg = [](const std::string& a) -> std::string {
        bool need = a.empty();
        for (char c : a) if (std::isspace((unsigned char)c) || c == '"' || c == '\\') { need = true; break; }
        if (!need) return a;
        std::string q = "\"";
        for (char c : a) { if (c == '"' || c == '\\') q += '\\'; q += c; }
        q += '"';
        return q;
    };
    auto emit = [&](const char* tag, const std::vector<Item>& list) {
        for (const Item& it : list) {
            out += tag; out += ' '; out += it.op;
            for (const std::string& a : it.args) { out += ' '; out += quoteArg(a); }
            out += '\n';
        }
    };
    // Declared variables: "v <name> <type> <value...>" (value may contain spaces).
    for (const VarDecl& v : variables)
        out += "v " + quoteArg(v.name) + " " + std::to_string(v.type) + " " + quoteArg(v.value) + "\n";
    emit("c", conditions);
    emit("i", instructions);
    // Extra handlers: a "handler <type> <key> <once>" line then its own hc/hi lines.
    for (const Handler& h : extraHandlers) {
        out += "handler " + std::to_string((int)h.trigger) + " " +
               (h.triggerKey.empty() ? std::string("-") : quoteArg(h.triggerKey)) + " " +
               (h.once ? "1" : "0") + "\n";
        emit("hc", h.conditions);
        emit("hi", h.instructions);
    }
    return out;
}

void ActionList::FromText(const std::string& text) {
    trigger = Trigger::OnStart; triggerKey = "e"; once = false; name.clear();
    conditions.clear(); instructions.clear(); variables.clear(); extraHandlers.clear();
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t nl = text.find('\n', pos);
        std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = (nl == std::string::npos) ? text.size() : nl + 1;
        // tokenize on whitespace, honouring "quoted tokens" (with \" / \\ escapes) so
        // an arg containing spaces comes back as one token. Old unquoted saves parse
        // exactly as before.
        std::vector<std::string> tok;
        std::size_t i = 0;
        while (i < line.size()) {
            while (i < line.size() && std::isspace((unsigned char)line[i])) ++i;
            if (i >= line.size()) break;
            std::string t;
            if (line[i] == '"') {                       // quoted token
                ++i;
                while (i < line.size() && line[i] != '"') {
                    if (line[i] == '\\' && i + 1 < line.size()) ++i;   // unescape
                    t += line[i++];
                }
                if (i < line.size()) ++i;               // skip closing quote
                tok.push_back(t);
            } else {                                    // bare token
                std::size_t b = i;
                while (i < line.size() && !std::isspace((unsigned char)line[i])) ++i;
                tok.push_back(line.substr(b, i - b));
            }
        }
        if (tok.empty()) continue;
        if (tok[0] == "name") {
            // Everything after "name " is the label (joined tokens keep single spaces).
            name.clear();
            for (std::size_t k = 1; k < tok.size(); ++k) { if (k > 1) name += ' '; name += tok[k]; }
        } else if (tok[0] == "trigger") {
            if (tok.size() > 1) trigger = (Trigger)std::atoi(tok[1].c_str());
            if (tok.size() > 2) triggerKey = (tok[2] == "-") ? std::string{} : tok[2];
            if (tok.size() > 3) once = (tok[3] == "1");
        } else if (tok[0] == "handler") {           // extra handler: handler <type> <key> <once>
            Handler h;
            if (tok.size() > 1) h.trigger = (Trigger)std::atoi(tok[1].c_str());
            if (tok.size() > 2 && tok[2] != "-") h.triggerKey = tok[2];
            if (tok.size() > 3) h.once = (tok[3] == "1");
            extraHandlers.push_back(std::move(h));
        } else if ((tok[0] == "hc" || tok[0] == "hi") && !extraHandlers.empty()) {
            Item it;
            if (tok.size() > 1) it.op = tok[1];
            for (std::size_t k = 2; k < tok.size(); ++k) it.args.push_back(tok[k]);
            (tok[0] == "hc" ? extraHandlers.back().conditions : extraHandlers.back().instructions).push_back(std::move(it));
        } else if (tok[0] == "v") {                 // declared variable: v <name> <type> <value>
            VarDecl vd;
            if (tok.size() > 1) vd.name = tok[1];
            if (tok.size() > 2) vd.type = std::atoi(tok[2].c_str());
            if (tok.size() > 3) vd.value = tok[3];
            if (!vd.name.empty()) variables.push_back(std::move(vd));
        } else if (tok[0] == "c" || tok[0] == "i") {
            Item it;
            if (tok.size() > 1) it.op = tok[1];
            for (std::size_t k = 2; k < tok.size(); ++k) it.args.push_back(tok[k]);
            (tok[0] == "c" ? conditions : instructions).push_back(std::move(it));
        }
    }
}

void ActionList::Start() {
    // Seed declared variables into the shared pools before anything runs, so they
    // exist (with their starting values) for every list from frame one.
    for (const VarDecl& v : variables) {
        if (v.name.empty()) continue;
        switch (v.type) {
            case 1:  // Text
                StrVars()[v.name] = v.value;
                break;
            case 2:  // Bool -> 1/0 number (so var_eq/var_gt work)
                Vars()[v.name] = (v.value == "1" || v.value == "true" || v.value == "True") ? 1.0f : 0.0f;
                break;
            case 3:  // Vector2 -> two component numbers "<name>.x" / ".y"
            case 4: { // Vector3 -> three component numbers "<name>.x" / ".y" / ".z"
                float xyz[3] = {0.0f, 0.0f, 0.0f};
                std::sscanf(v.value.c_str(), "%f %f %f", &xyz[0], &xyz[1], &xyz[2]);
                Vars()[v.name + ".x"] = xyz[0];
                Vars()[v.name + ".y"] = xyz[1];
                if (v.type == 4) Vars()[v.name + ".z"] = xyz[2];
                break;
            }
            default: // Number
                Vars()[v.name] = (float)std::atof(v.value.c_str());
                break;
        }
    }
    if (HasTrigger(Trigger::OnStart)) Fire();
}

bool ActionList::EvalConditions(const std::vector<Item>& conds) {
    for (const Item& c : conds) {
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
        else if (op == "var_eq")   ok = Mathf::Approximately(GetVar(Str(c, 0)), Num(c, 1));
        else if (op == "var_neq")  ok = !Mathf::Approximately(GetVar(Str(c, 0)), Num(c, 1));
        else if (op == "var_gt")   ok = GetVar(Str(c, 0)) > Num(c, 1);
        else if (op == "var_lt")   ok = GetVar(Str(c, 0)) < Num(c, 1);
        else if (op == "var_ge")   ok = GetVar(Str(c, 0)) >= Num(c, 1);
        else if (op == "var_le")   ok = GetVar(Str(c, 0)) <= Num(c, 1);
        else if (op == "key_up")   ok = !Str(c, 0).empty() && Input::GetKeyUp(Str(c, 0)[0]);
        else if (op == "mouse_down") ok = Input::GetMouseButtonDown((int)Num(c, 0));
        else if (op == "prefs_eq") ok = Mathf::Approximately(Prefs::GetFloat(Str(c, 0), 0.0f), Num(c, 1));
        else if (op == "prefs_gt") ok = Prefs::GetFloat(Str(c, 0), 0.0f) > Num(c, 1);
        else if (op == "prefs_lt") ok = Prefs::GetFloat(Str(c, 0), 0.0f) < Num(c, 1);
        else if (op == "prefs_neq")ok = !Mathf::Approximately(Prefs::GetFloat(Str(c, 0), 0.0f), Num(c, 1));
        else if (op == "var_between") { float v = GetVar(Str(c, 0)); ok = v >= Num(c, 1) && v <= Num(c, 2); }
        else if (op == "vars_cmp") ok = CmpPass(Str(c, 1), GetVar(Str(c, 0)), GetVar(Str(c, 2)));  // var <op> var
        else if (op == "is_moving") {   // any Rigidbody velocity above the threshold (default 0.01)
            float thr = c.args.size() > 0 ? Num(c, 0) : 0.01f, sp = 0.0f;
            if (auto* rb = gameObject ? gameObject->GetComponent<Rigidbody2D>() : nullptr)
                sp = Mathf::Sqrt(rb->velocity.x * rb->velocity.x + rb->velocity.y * rb->velocity.y);
            else if (auto* rb3 = gameObject ? gameObject->GetComponent<Rigidbody3D>() : nullptr)
                sp = rb3->velocity.Magnitude();
            ok = sp > thr;
        }
        else if (op == "tag_count_lt" || op == "tag_count_gt") {   // # of objects with a tag (wave clears, spawns)
            int n = 0; Scene* s = GetScene();
            if (s) for (const auto& up : s->Objects()) if (up && up->active && up->tag == Str(c, 0)) ++n;
            ok = (op == "tag_count_lt") ? (n < (int)Num(c, 1)) : (n > (int)Num(c, 1));
        }
        else if (op == "array_has") {   // does the array contain the value?
            auto& a = Arrays()[Str(c, 0)]; float v = Num(c, 1);
            ok = std::find_if(a.begin(), a.end(), [&](float x){ return Mathf::Approximately(x, v); }) != a.end();
        }
        else if (op == "array_len_gt") ok = (int)Arrays()[Str(c, 0)].size() > (int)Num(c, 1);
        else if (op == "array_len_lt") ok = (int)Arrays()[Str(c, 0)].size() < (int)Num(c, 1);
        else if (op == "map_has")      { auto& m = Maps()[Str(c, 0)]; ok = m.find(Str(c, 1)) != m.end(); }
        else if (op == "str_eq")       ok = StrVars()[Str(c, 0)] == Rest(c, 1);
        else if (op == "str_neq")      ok = StrVars()[Str(c, 0)] != Rest(c, 1);
        else if (op == "str_contains") ok = StrVars()[Str(c, 0)].find(Rest(c, 1)) != std::string::npos;
        else if (op == "str_empty")    ok = StrVars()[Str(c, 0)].empty();
        else if (op == "str_starts")   { const std::string& s = StrVars()[Str(c, 0)]; std::string p = Rest(c, 1); ok = s.rfind(p, 0) == 0; }
        else if (op == "str_ends")     { const std::string& s = StrVars()[Str(c, 0)]; std::string p = Rest(c, 1); ok = s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0; }
        else if (op == "has_tag")  ok = gameObject && gameObject->tag == Str(c, 0);
        else if (op == "is_active")ok = gameObject && gameObject->active;
        else if (op == "obj_has_tag") {   // does a NAMED object have this tag?
            Scene* s = GetScene(); GameObject* g = s ? s->Find(ObjName(c, 0)) : nullptr;
            ok = g && g->tag == Str(c, 1);
        }
        else if (op == "obj_active") {    // is a named object active?
            Scene* s = GetScene(); GameObject* g = s ? s->Find(ObjName(c, 0)) : nullptr;
            ok = g && g->active;
        }
        else if (op == "any_with_tag") {  // does ANY active object have this tag?
            Scene* s = GetScene(); ok = false;
            if (s) for (const auto& up : s->Objects()) if (up && up->active && up->tag == Str(c, 0)) { ok = true; break; }
        }
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

void ActionList::Fire() { FireHandler(conditions, instructions, m_fired, once); }

// Begin running one handler's instructions (if idle and its gate passes). Only one
// handler runs at a time; the others latch/poll again once it finishes.
bool ActionList::FireHandler(std::vector<Item>& conds, std::vector<Item>& ins, bool& fired, bool onceFlag) {
    if (m_running) return false;
    if (onceFlag && fired) return false;
    if (!EvalConditions(conds)) return false;
    m_running = true; m_ip = 0; m_wait = 0.0f;
    m_loops.clear(); m_callStack.clear();
    m_run = &ins; m_runFired = &fired;
    return true;
}

void ActionList::Update(float dt) {
    // ---- Triggers ----
    if (!m_running) {
        // Poll the primary handler and every extra handler; each has its OWN trigger,
        // gate and actions (like separate functions in one script). First match runs.
        auto pollH = [&](Trigger type, const std::string& key,
                         std::vector<Item>& conds, std::vector<Item>& ins, bool& fired, bool onceFlag) {
            if (m_running) return;
            bool go = false;
            if (type == Trigger::OnUpdate) go = true;
            else if (type == Trigger::OnKey && !key.empty() && Input::GetKeyDown(key[0])) go = true;
            else if (type == Trigger::OnKeyUp && !key.empty() && Input::GetKeyUp(key[0])) go = true;
            else if (type == Trigger::OnClick) {
                if (auto* b = gameObject ? gameObject->GetComponent<UIButton>() : nullptr)
                    if (b->WasClicked()) go = true;
            }
            if (go) FireHandler(conds, ins, fired, onceFlag);
        };
        pollH(trigger, triggerKey, conditions, instructions, m_fired, once);
        for (Handler& h : extraHandlers) pollH(h.trigger, h.triggerKey, h.conditions, h.instructions, h.m_fired, h.once);
        // Latched collision / trigger / mouse events fire the handler listening for them.
        if (!m_running && m_pending) {
            m_pending = false;
            auto matches = [&](Trigger ht) {
                return ht == m_pendingType ||
                       (m_pendingType == Trigger::OnTriggerEnter && ht == Trigger::OnCollision);  // legacy
            };
            if (matches(trigger)) FireHandler(conditions, instructions, m_fired, once);
            else for (Handler& h : extraHandlers) if (!m_running && matches(h.trigger)) FireHandler(h.conditions, h.instructions, h.m_fired, h.once);
        }
    }
    if (!m_running) return;

    // ---- Run instructions until a Wait or the end of the list ----
    if (m_wait > 0.0f) { m_wait -= dt; if (m_wait > 0.0f) return; }

    Scene* scene = GetScene();
    Transform* t = transform;
    int guard = 0;   // cap steps per frame so a goto-loop without a wait can't hang
    const std::vector<Item>& ins = RunList();   // the running handler's instructions
    while (m_ip < ins.size()) {
        if (++guard > 10000) break;
        // Step debugging: when paused, run only as many instructions as the editor
        // has granted, then hold here (return keeps the list running so it resumes).
        if (DebugPaused()) { if (StepBudget() <= 0) return; --StepBudget(); }
        const Item& it = ins[m_ip];
        const std::string& op = it.op;
        ++m_ip;

        if (op == "wait") { m_wait = Num(it, 0); if (m_wait > 0.0f) return; }
        else if (op == "stop") { m_ip = ins.size(); }
        else if (op == "label") { /* jump target marker — no-op */ }
        else if (op == "goto") {
            int target = ResolveTarget(Str(it, 0));   // a line number OR a label name
            if (target >= 0 && target < (int)ins.size()) m_ip = (std::size_t)target;
        }
        // ---- Counted loops: repeat <count> ... end_repeat, with break/continue ----
        else if (op == "repeat") {
            int count = (int)Num(it, 0);
            int end = MatchingEnd(m_ip - 1, "repeat", "end_repeat");
            if (end < 0) { /* unmatched: ignore */ }
            else if (count <= 0) m_ip = (std::size_t)end + 1;      // zero iterations: skip the body
            else { LoopFrame f; f.kind = 0; f.headIp = m_ip - 1; f.bodyStart = m_ip; f.endIp = (std::size_t)end; f.remaining = count - 1; m_loops.push_back(f); }
        }
        else if (op == "end_repeat") {
            if (!m_loops.empty() && m_loops.back().kind == 0) {
                auto& f = m_loops.back();
                if (f.remaining > 0) { --f.remaining; m_ip = f.bodyStart; }
                else m_loops.pop_back();
            }
        }
        // ---- While loop: while <var> <op> <value> ... end_while ----
        else if (op == "while") {
            bool pass = CmpPass(Str(it, 1), GetVar(Str(it, 0)), Num(it, 2));
            int end = MatchingEnd(m_ip - 1, "while", "end_while");
            if (end < 0) { /* unmatched */ }
            else if (!pass) m_ip = (std::size_t)end + 1;           // condition false: exit
            else { LoopFrame f; f.kind = 1; f.headIp = m_ip - 1; f.bodyStart = m_ip; f.endIp = (std::size_t)end; m_loops.push_back(f); }
        }
        else if (op == "end_while") {
            if (!m_loops.empty() && m_loops.back().kind == 1) {
                std::size_t head = m_loops.back().headIp;
                m_loops.pop_back();                                 // the while op re-evaluates and re-pushes
                m_ip = head;
            }
        }
        // ---- For each over an array: for_each <array> <idxVar> <valVar> ... end_for ----
        else if (op == "for_each") {
            int end = MatchingEnd(m_ip - 1, "for_each", "end_for");
            auto& a = Arrays()[Str(it, 0)];
            if (end < 0) { /* unmatched */ }
            else if (a.empty()) m_ip = (std::size_t)end + 1;
            else {
                LoopFrame f; f.kind = 2; f.headIp = m_ip - 1; f.bodyStart = m_ip; f.endIp = (std::size_t)end;
                f.arr = Str(it, 0); f.idxVar = Str(it, 1); f.valVar = Str(it, 2); f.idx = 0;
                if (!f.idxVar.empty()) Vars()[f.idxVar] = 0.0f;
                if (!f.valVar.empty()) Vars()[f.valVar] = a[0];
                m_loops.push_back(f);
            }
        }
        else if (op == "end_for") {
            if (!m_loops.empty() && m_loops.back().kind == 2) {
                auto& f = m_loops.back();
                auto& a = Arrays()[f.arr];
                ++f.idx;
                if (f.idx < a.size()) {
                    if (!f.idxVar.empty()) Vars()[f.idxVar] = (float)f.idx;
                    if (!f.valVar.empty()) Vars()[f.valVar] = a[f.idx];
                    m_ip = f.bodyStart;
                } else m_loops.pop_back();
            }
        }
        // ---- For each object with a tag: for_each_tag <tag> <name-var> ... end_for_tag ----
        else if (op == "for_each_tag") {
            int end = MatchingEnd(m_ip - 1, "for_each_tag", "end_for_tag");
            std::vector<std::string> names;
            if (scene) for (const auto& up : scene->Objects())
                if (up && up->active && up->tag == Str(it, 0)) names.push_back(up->name);
            if (end < 0) { /* unmatched */ }
            else if (names.empty()) m_ip = (std::size_t)end + 1;
            else {
                LoopFrame f; f.kind = 3; f.headIp = m_ip - 1; f.bodyStart = m_ip; f.endIp = (std::size_t)end;
                f.valVar = Str(it, 1); f.names = std::move(names); f.idx = 0;
                if (!f.valVar.empty()) StrVars()[f.valVar] = f.names[0];   // the current object's name
                m_loops.push_back(std::move(f));
            }
        }
        else if (op == "end_for_tag") {
            if (!m_loops.empty() && m_loops.back().kind == 3) {
                auto& f = m_loops.back();
                ++f.idx;
                if (f.idx < f.names.size()) {
                    if (!f.valVar.empty()) StrVars()[f.valVar] = f.names[f.idx];
                    m_ip = f.bodyStart;
                } else m_loops.pop_back();
            }
        }
        else if (op == "break") {
            if (!m_loops.empty()) { m_ip = m_loops.back().endIp + 1; m_loops.pop_back(); }
        }
        else if (op == "continue") {
            if (!m_loops.empty()) m_ip = m_loops.back().endIp;      // jump to the end_* -> next iteration
        }
        // ---- Subroutines (delegate-style): gosub <label> ... return_sub ----
        else if (op == "gosub") {
            int target = ResolveTarget(Str(it, 0));
            if (target >= 0 && target < (int)ins.size()) { m_callStack.push_back(m_ip); m_ip = (std::size_t)target; }
        }
        else if (op == "return_sub") {
            if (!m_callStack.empty()) { m_ip = m_callStack.back(); m_callStack.pop_back(); }
            else m_ip = ins.size();
        }
        // ---- Arrays (float lists) ----
        else if (op == "array_clear") { Arrays()[Str(it, 0)].clear(); }
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
        // Events with a payload: the receiver reads `event_value` (number) and
        // `event_text` (text), set just before the message fires.
        else if (op == "send_value") {
            Vars()["event_value"] = Num(it, 1); StrVars()["event_text"].clear();
            if (scene) for (ActionList* a : scene->FindObjectsOfType<ActionList>()) a->ReceiveMessage(Str(it, 0));
        }
        else if (op == "send_text") {
            StrVars()["event_text"] = Rest(it, 1); Vars()["event_value"] = 0.0f;
            if (scene) for (ActionList* a : scene->FindObjectsOfType<ActionList>()) a->ReceiveMessage(Str(it, 0));
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
        else if (op == "destroy") { if (scene && gameObject) { scene->Destroy(gameObject); return; } }
        else if (op == "destroy_obj") {
            if (scene) if (GameObject* g = scene->Find(ObjName(it, 0))) scene->Destroy(g);
        }
        else if (op == "activate")   { if (scene) if (GameObject* g = scene->Find(ObjName(it, 0))) g->active = true; }
        else if (op == "deactivate") { if (scene) if (GameObject* g = scene->Find(ObjName(it, 0))) g->active = false; }
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
            if (scene) if (GameObject* g = scene->Find(ObjName(it, 0)))
                for (ActionList* a : g->GetComponents<ActionList>()) a->ReceiveMessage(Str(it, 1));
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
                Vars()[pre + "_nx"] = h.normal.x;   // surface normal (Unity's hit.normal)
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
        else if (op == "spawn_at") {                   // spawn a prefab at a named object's position
            if (scene) {
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
        // ---- Added ops: single-axis position, game flow, facing, chase/spawn ----
        else if (op == "set_x") { if (t) t->localPosition.x = Num(it, 0); }
        else if (op == "set_y") { if (t) t->localPosition.y = Num(it, 0); }
        else if (op == "set_z") { if (t) t->localPosition.z = Num(it, 0); }
        else if (op == "flip_x") { if (t) t->localScale.x = -t->localScale.x; }   // face the other way (2D)
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
                    GameObject* g = SceneSerializer::InstantiateFromFile(*scene, Str(it, 0), nullptr);
                    if (!g || !g->transform) continue;
                    float ang = (6.2831853f * i) / count;
                    g->transform->localPosition = {c.x + Mathf::Cos(ang) * radius, c.y + Mathf::Sin(ang) * radius, c.z};
                }
            }
        }
        // unknown ops are ignored, so files stay forward-compatible
    }

    // Reached the end of the running handler.
    m_running = false;
    if (m_runFired) *m_runFired = true;   // mark THIS handler fired (for `once`)
    m_run = nullptr; m_runFired = nullptr;
}

} // namespace okay
