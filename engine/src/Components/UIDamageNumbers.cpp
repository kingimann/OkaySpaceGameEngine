#include "okay/Components/UIDamageNumbers.hpp"
#include "okay/Components/UIElement.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace okay {

namespace {
constexpr float kBox = 220.0f;       // width / height of a number's text box
constexpr float kBoxH = 40.0f;

// A fixed pseudo-random value in [-1, 1] from a counter: the same drift every run.
float Hash11(unsigned n) {
    unsigned x = n * 747796405u + 2891336453u;
    x = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
    x = (x >> 22u) ^ x;
    return ((x & 0xFFFFu) / 32767.5f) - 1.0f;
}

// Is `g` still one of the numbers under `host`'s Canvas (or under host when there is none)? A number a
// user deleted by hand is gone from the child list, so a stale pointer to it is never followed.
bool StillThere(GameObject* host, GameObject* g) {
    if (!host || !g) return false;
    Canvas* cv = OwningCanvas(host);
    Transform* home = (cv && cv->gameObject ? cv->gameObject : host)->transform;
    if (!home) return false;
    for (Transform* c : home->Children()) if (c && c->gameObject == g) return true;
    return false;
}
} // namespace

UIDamageNumbers::Kind UIDamageNumbers::KindFromName(const std::string& name) {
    std::string n;
    for (char c : name) n.push_back((char)((c >= 'A' && c <= 'Z') ? c + 32 : c));
    if (n == "crit" || n == "critical") return Kind::Crit;
    if (n == "heal" || n == "healing")  return Kind::Heal;
    if (n == "miss")                    return Kind::Miss;
    if (n == "info" || n == "xp")       return Kind::Info;
    return Kind::Normal;
}

bool UIDamageNumbers::RunAction(const std::string& fn, float amount) {
    auto num = [&](const char* sign) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%s%d", sign, (int)(amount < 0 ? -amount : amount));
        return std::string(buf);
    };
    if (fn == "Hit")  { Spawn(num("-"), Kind::Normal, spawnAt); return true; }
    if (fn == "Crit") { Spawn("CRIT " + num("-"), Kind::Crit, spawnAt); return true; }
    if (fn == "Heal") { Spawn(num("+"), Kind::Heal, spawnAt); return true; }
    if (fn == "Miss") { Spawn("MISS", Kind::Miss, spawnAt); return true; }
    if (fn == "Info") { Spawn(num("+") + " XP", Kind::Info, spawnAt); return true; }
    return false;
}

Color UIDamageNumbers::ColorFor(Kind k) const {
    switch (k) {
        case Kind::Crit: return critColor;
        case Kind::Heal: return healColor;
        case Kind::Miss: return missColor;
        case Kind::Info: return infoColor;
        default:         return normalColor;
    }
}

GameObject* UIDamageNumbers::Make(const std::string& text, Kind kind, Live& l) {
    Scene* sc = gameObject ? gameObject->scene() : nullptr;
    if (!sc) return nullptr;
    while ((int)m_live.size() >= std::max(1, maxActive)) {      // the oldest one goes
        if (StillThere(gameObject, m_live.front().go)) sc->Destroy(m_live.front().go);
        m_live.erase(m_live.begin());
    }
    GameObject* go = sc->CreateGameObject("DamageNumber");
    // Numbers live on the Canvas (when there is one), so a position is in canvas pixels wherever this component sits.
    Canvas* cv = OwningCanvas(gameObject);
    go->transform->SetParent((cv && cv->gameObject ? cv->gameObject : gameObject)->transform, false);
    auto* tr = go->AddComponent<TextRenderer>();
    tr->text = text;
    tr->screenSpace = true;
    tr->anchor = UIAnchor::TopLeft;
    tr->size = {kBox, kBoxH};
    tr->align = 1;                       // centred on the spawn point
    tr->bold = true;
    tr->outline = true;
    tr->shadow = true;
    tr->color = ColorFor(kind);
    tr->pixelSize = textSize * (kind == Kind::Crit ? critScale : 1.0f);
    l.go = go;
    l.age = 0.0f;
    l.color = tr->color;
    l.drift = Hash11(m_counter++) * spread;
    l.scale = tr->pixelSize;
    return go;
}

GameObject* UIDamageNumbers::Spawn(const std::string& text, Kind kind, Vec2 at) {
    Live l;
    if (!Make(text, kind, l)) return nullptr;
    l.start = at;
    l.inWorld = false;
    m_live.push_back(l);
    Place(m_live.back());
    return l.go;
}

GameObject* UIDamageNumbers::SpawnAtWorld(const std::string& text, Kind kind, Vec3 at) {
    Live l;
    if (!Make(text, kind, l)) return nullptr;
    l.world = at;
    l.inWorld = true;
    m_live.push_back(l);
    Place(m_live.back());
    return l.go;
}

void UIDamageNumbers::Clear() {
    Scene* sc = gameObject ? gameObject->scene() : nullptr;
    if (sc) for (auto& l : m_live) if (StillThere(gameObject, l.go)) sc->Destroy(l.go);
    m_live.clear();
}

void UIDamageNumbers::Place(Live& l) {
    if (!l.go) return;
    auto* tr = l.go->GetComponent<TextRenderer>();
    if (!tr) return;
    const float t = lifetime > 1e-4f ? std::min(1.0f, l.age / lifetime) : 1.0f;
    Vec2 p = l.start;
    bool visible = true;
    if (l.inWorld) {
        Vec2 sp; float depth = 0.0f;
        if (UIWorld().project && UIWorld().project(l.world, sp, depth) && depth > 0.0f) {
            // screen pixels -> this object's own (canvas-scaled) pixels
            const float sw = UIWorld().screenW > 1.0f ? UIWorld().screenW : UICanvas::Width();
            const float sh = UIWorld().screenH > 1.0f ? UIWorld().screenH : UICanvas::Height();
            float k = UIScaleFor(l.go, sw, sh);
            if (k < 1e-3f) k = 1.0f;
            p = {sp.x / k, sp.y / k};
        } else {
            visible = false;
        }
    }
    const float rise = riseSpeed * l.age - 0.5f * gravity * l.age * l.age;
    p.x += l.drift * t;
    p.y -= rise;
    tr->screenPos = {p.x - kBox * 0.5f, p.y - kBoxH * 0.5f};
    // Pop in a little bigger, then settle; fade over the last 40% of the life.
    const float pop = 1.0f + (popScale - 1.0f) * std::max(0.0f, 1.0f - t * 6.0f);
    tr->pixelSize = l.scale * pop;
    float a = t < 0.6f ? 1.0f : std::max(0.0f, 1.0f - (t - 0.6f) / 0.4f);
    Color c = l.color; c.a *= visible ? a : 0.0f;
    tr->color = c;
    Color o = tr->outlineColor; o.a = 0.9f * c.a; tr->outlineColor = o;
    Color s = tr->shadowColor; s.a = 0.8f * c.a; tr->shadowColor = s;
}

void UIDamageNumbers::Update(float dt) {
    Scene* sc = gameObject ? gameObject->scene() : nullptr;
    if (!sc) return;
    // A number someone deleted by hand (the Hierarchy, a script) is forgotten, never touched again.
    for (std::size_t i = 0; i < m_live.size();) {
        if (!StillThere(gameObject, m_live[i].go)) { m_live.erase(m_live.begin() + (std::ptrdiff_t)i); continue; }
        Live& l = m_live[i];
        l.age += dt;
        if (l.age >= lifetime) {
            if (l.go) sc->Destroy(l.go);
            m_live.erase(m_live.begin() + (std::ptrdiff_t)i);
            continue;
        }
        Place(l);
        ++i;
    }
}

} // namespace okay
