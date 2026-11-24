#include "okay/Components/ActionList.hpp"
#include "ActionListInternal.hpp"
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

namespace {

// One scene's action-list memory. The process-wide pool is the fallback used
// when no scene is current (a test that only calls Vars(), the editor before Play).
struct ActionPools {
    std::unordered_map<std::string, float> vars;
    std::unordered_map<std::string, std::vector<float>> arrays;
    std::unordered_map<std::string, std::unordered_map<std::string, float>> maps;
    std::unordered_map<std::string, std::string> strs;
    // `cooldown` condition state: the next time each condition item may pass again.
    // Keyed by the item's address — runtime-only, cleared with the variable pools.
    std::unordered_map<const void*, float> cooldowns;
};

struct PoolStore {
    ActionPools global;
    std::unordered_map<const Scene*, ActionPools> scenes;
    const Scene* current = nullptr;
};

PoolStore& Store() {
    static PoolStore s;
    return s;
}

ActionPools& Active() {
    PoolStore& st = Store();
    if (!st.current) return st.global;
    auto it = st.scenes.find(st.current);
    return it != st.scenes.end() ? it->second : st.global;
}

} // namespace

std::unordered_map<std::string, float>& ActionList::Vars() { return Active().vars; }
float ActionList::GetVar(const std::string& key) {
    auto& v = Vars();
    auto it = v.find(key);
    if (it != v.end()) return it->second;
    return Prefs::Has(key) ? Prefs::GetFloat(key, 0.0f) : 0.0f;   // fall back to a saved/published value
}

std::unordered_map<std::string, std::vector<float>>& ActionList::Arrays() { return Active().arrays; }

std::unordered_map<std::string, std::unordered_map<std::string, float>>& ActionList::Maps() {
    return Active().maps;
}

std::unordered_map<std::string, std::string>& ActionList::StrVars() { return Active().strs; }

void ActionList::UseScene(const Scene* scene) {
    PoolStore& st = Store();
    if (!scene) { st.current = nullptr; return; }
    if (!st.scenes.count(scene)) {
        // Start from the pools already in use, so a value written before this
        // scene runs (or left by the scene that was just current) is visible.
        // The copy is independent: later writes do not cross back.
        ActionPools fresh;
        if (st.current && st.current != scene) {
            auto src = st.scenes.find(st.current);
            fresh = (src != st.scenes.end()) ? src->second : st.global;
        } else if (st.current != scene) {
            fresh = st.global;
        }
        fresh.cooldowns.clear();
        st.scenes.emplace(scene, std::move(fresh));
    }
    st.current = scene;
}

void ActionList::ForgetScene(const Scene* scene) {
    if (!scene) return;
    PoolStore& st = Store();
    st.scenes.erase(scene);
    if (st.current == scene) st.current = nullptr;
}

void ActionList::ResetVars() {
    PoolStore& st = Store();
    st.global = ActionPools{};
    st.scenes.clear();
    st.current = nullptr;
}

namespace {

void BindOwner(GameObject* go) {
    if (!go) return;
    if (Scene* s = go->scene()) ActionList::UseScene(s);
}

} // namespace

bool& ActionList::DebugPaused() { static bool p = false; return p; }
int&  ActionList::StepBudget()  { static int  b = 0;     return b; }
std::vector<std::pair<const void*, int>>& ActionList::Breakpoints() {
    static std::vector<std::pair<const void*, int>> s; return s;
}

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

namespace actionlist_ops {
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
// give / take / has_item: an item id or name, a count (default 1), and an optional
// object whose Inventory it is (default this object's, else its root's).
int ItemCount(const ActionList::Item& it) { return it.args.size() > 1 ? (int)std::atof(it.args[1].c_str()) : 1; }
std::string ObjName(const ActionList::Item& it, std::size_t i);
Inventory* ItemInv(const ActionList::Item& it, GameObject* self, Scene* s) {
    GameObject* g = it.args.size() > 2 ? (s ? s->Find(ObjName(it, 2)) : nullptr) : self;
    return ItemPickup::InventoryOf(g);
}
std::string ItemName(const ActionList::Item& it) { return ItemDatabase::Get().Resolve(Str(it, 0)); }
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

// Ease-name → curve mapping shared by every tween-style op (tween_move/scale/rotate,
// tween_color, fade, tween_var). Unknown/empty names fall back to Linear.
Ease EaseByName(const std::string& s) {
    if (s == "in")          return Ease::CubicIn;
    if (s == "out")         return Ease::CubicOut;
    if (s == "in_out")      return Ease::CubicInOut;
    if (s == "out_back")    return Ease::BackOut;
    if (s == "out_bounce")  return Ease::BounceOut;
    if (s == "out_elastic") return Ease::ElasticOut;
    return Ease::Linear;
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
bool IsRayMod(const std::string& a) {
    return a.rfind("h:", 0) == 0 || a.rfind("ig:", 0) == 0 || a.rfind("only:", 0) == 0 ||
           a.rfind("r:", 0) == 0 || a.rfind("lm:", 0) == 0 || a.rfind("nt:", 0) == 0;
}

// Cast a ray for an action item. args[base] is a direction token (keyword or the
// first of a raw "x y z" triple, kept for back-compat); the distance follows.
// Optional trailing modifiers (any order): "h:<offset>" raises/lowers the start point
// along world-up (so it can leave the feet); "ig:<tag>" ignores objects with that tag.
// Returns the nearest hit (the caster's own colliders are always ignored).
RaycastHit3D ActionRaycast(GameObject* go, const ActionList::Item& it, std::size_t base) {
    RaycastHit3D miss;
    Scene* s = go ? go->scene() : nullptr;
    if (!s || !go || !go->transform) return miss;
    // Further modifiers: "r:<radius>" makes it a thick sphere / circle cast,
    // "lm:<layers>" limits it to collider layers ("0,3" or names), "nt:1" skips
    // trigger colliders.
    float height = 0.0f, radius = 0.0f; std::string ignoreTag, onlyTag;
    std::uint32_t layerMask = Physics3D::kAllLayers; bool hitTriggers = true;
    for (const std::string& a : it.args) {
        if (a.rfind("h:", 0) == 0)  height = (float)std::atof(a.c_str() + 2);
        else if (a.rfind("ig:", 0) == 0) ignoreTag = a.substr(3);
        else if (a.rfind("only:", 0) == 0) onlyTag = a.substr(5);
        else if (a.rfind("r:", 0) == 0) radius = (float)std::atof(a.c_str() + 2);
        else if (a.rfind("lm:", 0) == 0) layerMask = Layers::MaskFromList(a.substr(3));
        else if (a.rfind("nt:", 0) == 0) hitTriggers = std::atoi(a.c_str() + 3) == 0;
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

    RaycastHit3D h3 = radius > 0.0f
        ? s->physics3D().SphereCast(*s, origin, nd, radius, dist, go, layerMask, hitTriggers)
        : s->physics3D().Raycast(*s, origin, nd, dist, go, layerMask, hitTriggers);
    s->physics3D().RecordQueryRay(origin, nd, dist, h3, radius);
    RaycastHit3D result = h3;
    // Also cast in 2D against Collider2D, so a raycast in a top-down / side-scroller game
    // hits sprites too — but only when the ray has a real XY direction (a pure ±Z ray,
    // e.g. 3D "forward", has no 2D meaning). Keep whichever hit is nearer.
    Vec2 d2{nd.x, nd.y};
    if (d2.SqrMagnitude() > 1e-6f) {
        d2 = d2.Normalized();
        Vec2 o2{origin.x, origin.y};
        RaycastHit2D h2 = radius > 0.0f
            ? s->physics().CircleCast(*s, o2, d2, radius, dist, go, layerMask, hitTriggers)
            : s->physics().Raycast(*s, o2, d2, dist, go, layerMask, hitTriggers);
        s->physics().RecordQueryRay(o2, d2, dist, h2, radius);
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
} // namespace actionlist_ops

namespace {
// %-escape the characters an .okayvs line can't hold inside one token.
std::string PctEncode(const std::string& a) {
    if (a.empty()) return "%-";
    static const char* kHex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : a) {
        if (c <= ' ' || c == '%' || c == '"' || c == '#' || c == ';' || c == '\'' || c == '\\' || c >= 0x7F) {
            o += '%'; o += kHex[c >> 4]; o += kHex[c & 15];
        } else o += (char)c;
    }
    return o;
}
std::string PctDecode(const std::string& a) {
    if (a == "%-") return {};
    auto hex = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
    std::string o;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i] == '%' && i + 2 < a.size() && hex(a[i + 1]) >= 0 && hex(a[i + 2]) >= 0) {
            o += (char)(hex(a[i + 1]) * 16 + hex(a[i + 2])); i += 2;
        } else o += a[i];
    }
    return o;
}
std::vector<std::string> SplitWs(const std::string& s) {
    std::vector<std::string> out; std::string cur;
    for (char c : s) {
        if (std::isspace((unsigned char)c)) { if (!cur.empty()) { out.push_back(cur); cur.clear(); } }
        else cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}
} // namespace

std::string ActionList::EncodeItem(const Item& it) {
    std::string o = PctEncode(it.op);
    for (const std::string& a : it.args) { o += ' '; o += PctEncode(a); }
    return o;
}
ActionList::Item ActionList::DecodeItem(const std::string& line) {
    Item it;
    std::vector<std::string> t = SplitWs(line);
    if (t.empty()) return it;
    it.op = PctDecode(t[0]);
    for (std::size_t i = 1; i < t.size(); ++i) it.args.push_back(PctDecode(t[i]));
    return it;
}
std::string ActionList::EncodeItems(const std::vector<Item>& items) {
    std::string o;
    for (std::size_t i = 0; i < items.size(); ++i) { if (i) o += " ; "; o += EncodeItem(items[i]); }
    return o;
}
std::vector<ActionList::Item> ActionList::DecodeItems(const std::string& text) {
    std::vector<Item> out;
    std::string cur;
    auto flush = [&]() { Item it = DecodeItem(cur); if (!it.op.empty()) out.push_back(std::move(it)); cur.clear(); };
    for (char c : text) { if (c == ';') flush(); else cur += c; }
    flush();
    return out;
}

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
    // Saved Flow-Graph layout: "pos <key> <x> <y>" (editor-only; runtime ignores it).
    for (const auto& kv : nodeLayout)
        out += "pos " + quoteArg(kv.first) + " " +
               std::to_string(kv.second.first) + " " + std::to_string(kv.second.second) + "\n";
    return out;
}

void ActionList::FromText(const std::string& text) {
    trigger = Trigger::OnStart; triggerKey = "e"; once = false; name.clear();
    conditions.clear(); instructions.clear(); variables.clear(); extraHandlers.clear();
    nodeLayout.clear();
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
        } else if (tok[0] == "pos" && tok.size() > 3) {   // saved Flow-Graph node position
            nodeLayout[tok[1]] = {(float)std::atof(tok[2].c_str()), (float)std::atof(tok[3].c_str())};
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

void ActionList::SeedVariable(const VarDecl& v) {
    {
        if (v.name.empty()) return;
        // Type indices are STABLE for serialization (never renumber; only append):
        // 0 Number, 1 Text, 2 Bool, 3 Vector2, 4 Vector3, 5 Int, 6 Float, 7 Double,
        // 8 GameObject (stores an object name in StrVars), 9 Color.
        switch (v.type) {
            case 1:  // Text
            case 8:  // GameObject reference (the object's name, usable as a text var)
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
            case 5:  // Int -> whole number
                Vars()[v.name] = std::round((float)std::atof(v.value.c_str()));
                break;
            case 9: { // Color -> four component numbers "<name>.r/.g/.b/.a" (0..1)
                float rgba[4] = {1.0f, 1.0f, 1.0f, 1.0f};
                std::sscanf(v.value.c_str(), "%f %f %f %f", &rgba[0], &rgba[1], &rgba[2], &rgba[3]);
                Vars()[v.name + ".r"] = rgba[0];
                Vars()[v.name + ".g"] = rgba[1];
                Vars()[v.name + ".b"] = rgba[2];
                Vars()[v.name + ".a"] = rgba[3];
                break;
            }
            default: // Number / Float / Double (all share the float variable pool)
                Vars()[v.name] = (float)std::atof(v.value.c_str());
                break;
        }
    }
}

void ActionList::Start() {
    BindOwner(gameObject);
    if (!ScriptsRun(gameObject)) { m_startDeferred = true; return; }
    m_startDeferred = false;
    // Seed declared variables into the shared pools before anything runs, so they
    // exist (with their starting values) for every list from frame one.
    for (const VarDecl& v : variables) SeedVariable(v);
    if (HasTrigger(Trigger::OnStart)) Fire();
}

bool ActionList::EvalConditions(const std::vector<Item>& conds) {
    BindOwner(gameObject);
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
        else if (op == "cooldown") {   // pass at most once every N seconds (attack rate, pickups)
            float now = Time::ElapsedTime();
            float& nxt = Active().cooldowns[(const void*)&c];
            if (now >= nxt) { nxt = now + Num(c, 0); ok = true; } else ok = false;
        }
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
        else if (op == "var_even")     { long n = std::lround(GetVar(Str(c, 0))); ok = (n % 2) == 0; }
        else if (op == "var_odd")      { long n = std::lround(GetVar(Str(c, 0))); ok = (n % 2) != 0; }
        else if (op == "var_positive") { ok = GetVar(Str(c, 0)) > 0.0f; }
        else if (op == "var_negative") { ok = GetVar(Str(c, 0)) < 0.0f; }
        else if (op == "var_zero")     { ok = Mathf::Approximately(GetVar(Str(c, 0)), 0.0f); }
        else if (op == "var_multiple_of") { float d = Num(c, 1); ok = (d != 0.0f) && Mathf::Approximately(std::fmod(GetVar(Str(c, 0)), d), 0.0f); }
        else if (op == "var_near")     { ok = std::fabs(GetVar(Str(c, 0)) - Num(c, 1)) <= Num(c, 2); }
        else if (op == "key_any") { ok = false; for (std::size_t i = 0; i < c.args.size(); ++i) { const std::string& k = Str(c, i); if (!k.empty() && Input::GetKey(k[0])) { ok = true; break; } } }  // any of these keys held
        else if (op == "net_relay_ready") {   // relay has paired this peer (safe to play)
            NetworkManager* n = GetScene() ? GetScene()->FindObjectOfType<NetworkManager>() : nullptr;
            ok = n && n->RelayReady();
        }
        else if (op == "net_relayed") {       // routing through a relay (host or client)
            NetworkManager* n = GetScene() ? GetScene()->FindObjectOfType<NetworkManager>() : nullptr;
            ok = n && n->Relayed();
        }
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
        else if (op == "is_in_action") ok = HumanoidActions::IsInAction(gameObject);
        else if (op == "is_dead") ok = HumanoidActions::IsDead(gameObject);
        else if (op == "can_dive") ok = HumanoidActions::CanDive(gameObject);
        else if (op == "is_sitting") ok = HumanoidActions::IsSitting(gameObject);
        else if (op == "is_hanging") ok = HumanoidActions::IsHanging(gameObject);
        else if (op == "is_active")ok = gameObject && gameObject->active;
        else if (op == "has_item") {      // item [count] [object]: the inventory holds at least count
            Inventory* inv = ItemInv(c, gameObject, GetScene());
            ok = inv && inv->Has(ItemName(c), ItemCount(c));
        }
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
        else if (op == "dist_lt")  { float d; ok = distTo(ObjName(c, 0), d) && d < Num(c, 1); }
        else if (op == "dist_gt")  { float d; ok = distTo(ObjName(c, 0), d) && d > Num(c, 1); }
        else if (op == "exists")   { Scene* s = GetScene(); ok = s && s->Find(ObjName(c, 0)) != nullptr; }
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
    BindOwner(gameObject);
    if (m_running) return false;
    if (onceFlag && fired) return false;
    if (!EvalConditions(conds)) return false;
    m_running = true; m_ip = 0; m_wait = 0.0f;
    m_loops.clear(); m_callStack.clear();
    m_run = &ins; m_runFired = &fired;
    return true;
}

void ActionList::Update(float dt) {
    BindOwner(gameObject);
    if (!ScriptsRun(gameObject)) return;
    if (m_startDeferred) Start();
    // ---- Triggers ----
    if (!m_running) {
        // Poll the primary handler and every extra handler; each has its OWN trigger,
        // gate and actions (like separate functions in one script). First match runs.
        auto pollH = [&](Trigger type, const std::string& key,
                         std::vector<Item>& conds, std::vector<Item>& ins, bool& fired, bool onceFlag, float& timer) {
            if (m_running) return;
            bool go = false;
            if (type == Trigger::OnUpdate) go = true;
            else if (type == Trigger::OnInterval) {          // fire every <key> seconds
                float iv = (float)std::atof(key.c_str());
                if (iv > 0.0f) { timer += dt; if (timer >= iv) { timer -= iv; go = true; } }
            }
            else if (type == Trigger::OnKey && !key.empty() && Input::GetKeyDown(key[0])) go = true;
            else if (type == Trigger::OnKeyUp && !key.empty() && Input::GetKeyUp(key[0])) go = true;
            else if (type == Trigger::OnClick) {
                if (auto* b = gameObject ? gameObject->GetComponent<UIButton>() : nullptr)
                    if (b->WasClicked()) go = true;
            }
            if (go) FireHandler(conds, ins, fired, onceFlag);
        };
        pollH(trigger, triggerKey, conditions, instructions, m_fired, once, m_timer);
        for (Handler& h : extraHandlers) pollH(h.trigger, h.triggerKey, h.conditions, h.instructions, h.m_fired, h.once, h.m_timer);
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
        // Breakpoint on the next instruction: flip into the paused/step mode the
        // editor's Flow Graph drives, exactly as if the user hit Pause here.
        if (!DebugPaused() && !Breakpoints().empty()) {
            for (const auto& bp : Breakpoints())
                if (bp.first == (const void*)&ins && bp.second == (int)m_ip) {
                    DebugPaused() = true; StepBudget() = 0; break;
                }
        }
        // Step debugging: when paused, run only as many instructions as the editor
        // has granted, then hold here (return keeps the list running so it resumes).
        if (DebugPaused()) { if (StepBudget() <= 0) return; --StepBudget(); }
        const Item& it = ins[m_ip];
        const std::string& op = it.op;
        ++m_ip;

        if (op == "wait") { m_wait = Num(it, 0); if (m_wait > 0.0f) return; }
        // wait_until <var> <cmp> <value>: hold ON this instruction, re-testing every
        // frame, until the comparison passes — then flow continues past it.
        else if (op == "wait_until") {
            if (!CmpPass(Str(it, 1), GetVar(Str(it, 0)), Num(it, 2))) { --m_ip; return; }
        }
        else if (op == "stop") { m_ip = ins.size(); }
        else if (op == "label") { /* jump target marker — no-op */ }
        // declare_var <name> <type> <value>: a Variables-panel declaration (graphs
        // converted from Actions seed their variables with it on start).
        else if (op == "declare_var") {
            VarDecl vd; vd.name = Str(it, 0); vd.type = (int)Num(it, 1); vd.value = Rest(it, 2);
            SeedVariable(vd);
        }
        else if (op == "comment") { /* designer note — no-op at runtime */ }
        // ---- Juice: scheduler-driven tweens (mirror the script tween_* builtins).
        // They start the effect and continue immediately; add a `wait` to hold.
        else if (op == "tween_move" || op == "tween_scale" || op == "shake" ||
                 op == "punch_scale" || op == "tween_rotate") {
            Transform* tt = transform;
            if (scene && tt) {
                // These effects write to this object's components, so they end with the object
                // (destroyed or its scene unloaded) instead of running on freed memory.
                Scheduler& schedulerRef = scene->scheduler();
                Scheduler::Options bound; bound.object = gameObject;
                auto tween = [&](float dur, std::function<void(float)> update, std::function<void()> done = {}) {
                    schedulerRef.Tween(dur, std::move(update), std::move(done), bound);
                };
                const auto easeOf = EaseByName;   // shared ease-name mapping
                if (op == "tween_move") {           // x y seconds [ease]
                    Vec3 start = tt->localPosition, target{Num(it, 0), Num(it, 1), start.z};
                    Ease e = easeOf(Str(it, 3));
                    tween(Num(it, 2), [tt, start, target, e](float u) {
                        float k = Easing::Evaluate(e, u);
                        tt->localPosition = start + (target - start) * k;
                    });
                } else if (op == "tween_scale") {   // scale seconds [ease]
                    Vec3 start = tt->localScale; float v = Num(it, 0); Vec3 target{v, v, v};
                    Ease e = easeOf(Str(it, 2));
                    tween(Num(it, 1), [tt, start, target, e](float u) {
                        float k = Easing::Evaluate(e, u);
                        tt->localScale = start + (target - start) * k;
                    });
                } else if (op == "shake") {         // intensity seconds
                    Vec3 start = tt->localPosition; float inten = Num(it, 0);
                    tween(Num(it, 1), [tt, start, inten](float u) {
                        float decay = (1.0f - u) * inten;
                        tt->localPosition = start + Vec3{Random::Shared().Range(-1.0f, 1.0f) * decay,
                                                         Random::Shared().Range(-1.0f, 1.0f) * decay, 0.0f};
                    }, [tt, start]() { tt->localPosition = start; });
                } else if (op == "punch_scale") {    // amount seconds
                    Vec3 start = tt->localScale; float amount = Num(it, 0);
                    tween(Num(it, 1), [tt, start, amount](float u) {
                        float osc = Mathf::Sin(u * Mathf::PI * 6.0f) * amount * (1.0f - u);
                        tt->localScale = start + Vec3{osc, osc, osc};
                    }, [tt, start]() { tt->localScale = start; });
                } else {                             // tween_rotate: degrees seconds [ease]
                    Quat start = tt->localRotation; float deg = Num(it, 0);
                    Ease e = easeOf(Str(it, 2));
                    tween(Num(it, 1), [tt, start, deg, e](float u) {
                        float k = Easing::Evaluate(e, u);
                        tt->localRotation = start * Quat::Euler({0, 0, deg * k});
                    });
                }
            }
        }
        // ---- Juice: colour effects on this object's renderers (sprite/text/mesh).
        // Like the transform tweens they start the effect and continue immediately.
        else if (op == "tween_color" || op == "fade" || op == "flash") {
            auto* srr = gameObject ? gameObject->GetComponent<SpriteRenderer>() : nullptr;
            auto* trr = gameObject ? gameObject->GetComponent<TextRenderer>()   : nullptr;
            auto* mrr = gameObject ? gameObject->GetComponent<MeshRenderer>()   : nullptr;
            if (scene && (srr || trr || mrr)) {
                Color start = srr ? srr->color : trr ? trr->color : mrr->color;
                auto apply = [srr, trr, mrr](const Color& c) {
                    if (srr) srr->color = c;
                    if (trr) trr->color = c;
                    if (mrr) mrr->color = c;
                };
                auto mix = [](const Color& a, const Color& b, float k) {
                    return Color{a.r + (b.r - a.r) * k, a.g + (b.g - a.g) * k,
                                 a.b + (b.b - a.b) * k, a.a + (b.a - a.a) * k};
                };
                // These effects write to this object's components, so they end with the object
                // (destroyed or its scene unloaded) instead of running on freed memory.
                Scheduler& schedulerRef = scene->scheduler();
                Scheduler::Options bound; bound.object = gameObject;
                auto tween = [&](float dur, std::function<void(float)> update, std::function<void()> done = {}) {
                    schedulerRef.Tween(dur, std::move(update), std::move(done), bound);
                };
                if (op == "tween_color") {          // r g b seconds [ease]
                    Color to{Num(it, 0), Num(it, 1), Num(it, 2), start.a};
                    float dur = it.args.size() > 3 ? Num(it, 3) : 0.3f;
                    Ease e = EaseByName(Str(it, 4));
                    tween(dur, [apply, mix, start, to, e](float u) {
                        apply(mix(start, to, Easing::Evaluate(e, u)));
                    });
                } else if (op == "fade") {          // alpha seconds [ease]
                    Color to = start; to.a = Num(it, 0);
                    float dur = it.args.size() > 1 ? Num(it, 1) : 0.3f;
                    Ease e = EaseByName(Str(it, 2));
                    tween(dur, [apply, mix, start, to, e](float u) {
                        apply(mix(start, to, Easing::Evaluate(e, u)));
                    });
                } else if (Accessibility::FlashScale() > 0.0f) {   // flash [seconds] [r g b] — blink, then restore (skipped with reduced flashing)
                    float dur = Num(it, 0);
                    if (dur <= 0.0f) dur = 0.2f;
                    Color fc = it.args.size() > 3 ? Color{Num(it, 1), Num(it, 2), Num(it, 3), start.a}
                                                  : Color{1.0f, 1.0f, 1.0f, start.a};
                    tween(dur, [apply, mix, start, fc](float u) {
                        apply(mix(start, fc, 1.0f - std::fabs(2.0f * u - 1.0f)));   // up, then back
                    }, [apply, start]() { apply(start); });
                }
            }
        }
        // tween_var <var> <to> <seconds> [ease]: animate a shared variable over time
        // (HP bars, countdowns, smooth difficulty ramps — pairs with UI Text Bind).
        else if (op == "tween_var") {
            if (scene) {
                std::string v = Str(it, 0);
                float from = GetVar(v), to = Num(it, 1);
                float dur = it.args.size() > 2 ? Num(it, 2) : 0.3f;
                Ease e = EaseByName(Str(it, 3));
                scene->scheduler().Tween(dur, [v, from, to, e](float u) {
                    Vars()[v] = from + (to - from) * Easing::Evaluate(e, u);
                });
            }
        }
        // move_dir <direction> <speed>: facing-relative movement (forward/back/up/
        // down/left/right or toward:<Object>), units/second — run under On Update.
        // Uses this Update's dt (not Time::DeltaTime) so it also works when the
        // scene is stepped outside the main app loop (tests, tools).
        else if (op == "move_dir") {
            Vec3 mdir;
            if (t && RayDirFromToken(gameObject, Str(it, 0), mdir))
                t->Translate(mdir * (Num(it, 1) * dt));
        }
        // orbit <object> <radius> <deg_per_sec>: circle a named object (moons,
        // shields, patrol rings) — run under On Update. Arg order matches the
        // script builtin orbit(name, radius, degPerSec); with only two args the
        // radius is the current distance, so the orbit starts where the object is.
        else if (op == "orbit") {
            GameObject* g = scene ? scene->Find(ObjName(it, 0)) : nullptr;
            if (g && g->transform && t && gameObject) {
                Vec3 c = g->transform->Position(), me = gameObject->transform->Position();
                float dx = me.x - c.x, dy = me.y - c.y;
                float rad, degPerSec;
                if (it.args.size() >= 3) { rad = Num(it, 1); degPerSec = Num(it, 2); }
                else { rad = Mathf::Sqrt(dx * dx + dy * dy); degPerSec = Num(it, 1); }
                if (rad < 1e-4f) rad = 1.0f;
                float ang = std::atan2(dy, dx) + degPerSec * 0.0174532925f * dt;
                Vec3 np{c.x + Mathf::Cos(ang) * rad, c.y + Mathf::Sin(ang) * rad, me.z};
                t->Translate({np.x - me.x, np.y - me.y, 0.0f});
            }
        }
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
        // ---- Branching block: if <var> <cmp> <value> ... [else ...] end_if ----
        // Structured version of If Go To — no line numbers or labels to maintain.
        else if (op == "if") {
            bool pass = CmpPass(Str(it, 1), GetVar(Str(it, 0)), Num(it, 2));
            int end = MatchingEnd(m_ip - 1, "if", "end_if");
            if (end < 0) { /* unmatched: ignore */ }
            else if (!pass) {
                // False: jump past our `else` (into its body) if one exists at
                // this nesting depth, otherwise past the whole block.
                std::size_t target = (std::size_t)end + 1;
                int depth = 0;
                for (std::size_t i = m_ip; i < (std::size_t)end; ++i) {
                    const std::string& o2 = ins[i].op;
                    if (o2 == "if") ++depth;
                    else if (o2 == "end_if") --depth;
                    else if (o2 == "else" && depth == 0) { target = i + 1; break; }
                }
                m_ip = target;
            }
            // True: fall through into the then-body.
        }
        else if (op == "else") {
            // Only reached by falling out of a passing then-body: skip the
            // else-body by jumping past the matching end_if.
            int depth = 0;
            for (std::size_t i = m_ip; i < ins.size(); ++i) {
                if (ins[i].op == "if") ++depth;
                else if (ins[i].op == "end_if") {
                    if (depth == 0) { m_ip = i + 1; break; }
                    --depth;
                }
            }
        }
        else if (op == "end_if") { /* block marker — no-op */ }
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
        else {
            // Every remaining op family lives in ActionListOps.cpp; 2 means the instruction ended this Update.
            if (RunMoreOps(it, op, scene, t) == 2) return;
        }
        // unknown ops are ignored, so files stay forward-compatible
    }

    // Reached the end of the running handler.
    m_running = false;
    if (m_runFired) *m_runFired = true;   // mark THIS handler fired (for `once`)
    m_run = nullptr; m_runFired = nullptr;
}

void ActionList::LateUpdate(float /*dt*/) {
    BindOwner(gameObject);
    // OnLateUpdate handlers start here (after every object's Update); their instructions
    // then run on the next Update tick like any other started handler.
    if (m_running) return;
    if (trigger == Trigger::OnLateUpdate) FireHandler(conditions, instructions, m_fired, once);
    for (Handler& h : extraHandlers)
        if (!m_running && h.trigger == Trigger::OnLateUpdate)
            FireHandler(h.conditions, h.instructions, h.m_fired, h.once);
}

} // namespace okay
