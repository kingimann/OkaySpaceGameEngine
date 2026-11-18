// UI library slice 2, game HUD parts: the parts of okay/Components/UIGameHud.hpp that need the rest of the engine.
#include "okay/Components/UIGameHud.hpp"
#include "okay/Components/UIVars.hpp"
#include "okay/Components/QuestLog.hpp"
#include "okay/Components/RangedWeapon.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/Equipment.hpp"
#include "okay/Inventory/ItemPresentation.hpp"
#include "okay/Components/Character.hpp"
#include <filesystem>
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include <cmath>

namespace okay {

namespace {

std::vector<std::string> Split(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) { if (c == '|') { if (!cur.empty()) out.push_back(cur); cur.clear(); } else cur += c; }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

GameObject* FindPlayer(Scene* s, const std::string& tag) {
    if (!s) return nullptr;
    if (GameObject* p = s->FindWithTag(tag.empty() ? std::string("Player") : tag)) return p;
    return s->Find("Player");
}

} // namespace

// ---- Kill feed ------------------------------------------------------------------------------------------------------

void UIKillFeed::Push(const std::string& killer, const std::string& victim, const std::string& weapon) {
    std::map<std::string, std::string> f;
    f["killer"] = killer; f["victim"] = victim; f["weapon"] = weapon;
    const std::string text = uivars::FillTemplate(weapon.empty() ? format : weaponFormat, f);
    const bool mine = !playerName.empty() && (killer == playerName || victim == playerName);
    PushText(text, mine);
}

int UIKillFeed::Announce(Scene& scene, const std::string& killer, const std::string& victim, const std::string& weapon) {
    int n = 0;
    for (UIKillFeed* k : scene.FindObjectsOfType<UIKillFeed>()) { k->Push(killer, victim, weapon); ++n; }
    return n;
}

// ---- Ammo counter ---------------------------------------------------------------------------------------------------

UIAmmoCounter::State UIAmmoCounter::Read() const {
    State s;
    s.magazine = magazine; s.current = current; s.reserve = reserve; s.reloading = reloading;
    float v = 0.0f;
    if (!currentVar.empty() && uivars::ReadNumber(gameObject, currentVar, v)) {
        s.current = (int)std::lround(v);
        if (!reserveVar.empty() && uivars::ReadNumber(gameObject, reserveVar, v)) s.reserve = (int)std::lround(v);
        return s;
    }
    Scene* sc = gameObject ? gameObject->scene() : nullptr;
    GameObject* wo = nullptr;
    if (sc) {
        if (!weapon.empty()) wo = sc->Find(weapon);
        if (!wo) if (GameObject* p = FindPlayer(sc, "Player")) wo = p;
    }
    RangedWeapon* rw = wo ? wo->GetComponentInChildren<RangedWeapon>(true) : nullptr;
    if (!rw && sc) { auto all = sc->FindObjectsOfType<RangedWeapon>(); if (!all.empty()) rw = all.front(); }
    if (rw) {
        s.fromWeapon = true;
        s.current = rw->Rounds();
        s.reserve = rw->Spare();
        s.reloading = rw->IsReloading();
        s.magazine = rw->magazine > 0 ? rw->magazine : magazine;
    }
    return s;
}

// ---- Interaction prompt ---------------------------------------------------------------------------------------------

float UIInteractionPrompt::Distance() const {
    Scene* sc = gameObject ? gameObject->scene() : nullptr;
    GameObject* p = FindPlayer(sc, playerTag);
    GameObject* t = (sc && !target.empty()) ? sc->Find(target) : nullptr;
    if (!p || !t || !p->transform || !t->transform) return 1.0e9f;
    const Vec3 a = p->transform->Position(), b = t->transform->Position();
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

bool UIInteractionPrompt::ShouldShow() const {
    if (alwaysShow) return true;
    float v = 0.0f;
    if (!showVar.empty() && uivars::ReadNumber(gameObject, showVar, v) && v != 0.0f) return true;
    return !target.empty() && Distance() <= range;
}

void UIInteractionPrompt::Update(float dt) {
    const float goal = ShouldShow() ? 1.0f : 0.0f;
    const float k = (fadeSpeed < 0.1f ? 0.1f : fadeSpeed) * dt;
    if (m_alpha < goal) { m_alpha += k; if (m_alpha > goal) m_alpha = goal; }
    else if (m_alpha > goal) { m_alpha -= k; if (m_alpha < goal) m_alpha = goal; }
    if (goal > 0.5f && !key.empty() && Input::GetKeyDown((char)std::tolower((unsigned char)key[0]))) FireUIEvent(gameObject, "on_interact");
}

// ---- Objective tracker ----------------------------------------------------------------------------------------------

std::vector<UIObjectiveTracker::Line> UIObjectiveTracker::Lines() const {
    std::vector<Line> out;
    QuestLog* log = gameObject ? QuestLog::Get(gameObject->scene(), false) : nullptr;
    if (!log) return out;
    std::vector<std::string> ids;
    if (!questId.empty()) { if (log->IsActive(questId)) ids.push_back(questId); }
    else if (!log->tracked.empty() && log->IsActive(log->tracked)) ids.push_back(log->tracked);
    else ids = log->ActiveIds();
    if (maxQuests > 0 && (int)ids.size() > maxQuests) ids.resize((std::size_t)maxQuests);
    for (const std::string& id : ids) {
        const QuestDef* q = log->Def(id);
        if (!q) continue;
        out.push_back(Line{Line::Quest, QuestTitle(*q), 0, 0});
        if (showDescription && !QuestDescription(*q).empty()) out.push_back(Line{Line::Note, QuestDescription(*q), 0, 0});
        for (int i = 0; i < (int)q->objectives.size(); ++i) {
            const QuestObjective& o = q->objectives[(std::size_t)i];
            std::string text = QuestObjectiveText(*q, i);
            if (text.empty()) text = o.description.empty() ? o.target : o.description;
            Line l;
            l.kind = log->ObjectiveDone(id, i) ? Line::Done : Line::Objective;
            l.text = text + (o.optional ? " (optional)" : "");
            l.need = o.count;
            l.have = log->Progress(id, i);
            out.push_back(std::move(l));
        }
    }
    return out;
}

// ---- Compass --------------------------------------------------------------------------------------------------------

float UICompass::Heading(float* fromX, float* fromZ, bool* hasPos) const {
    if (hasPos) *hasPos = false;
    float hv = 0.0f;
    if (!headingVar.empty() && uivars::ReadNumber(gameObject, headingVar, hv)) return std::fmod(std::fmod(hv, 360.0f) + 360.0f, 360.0f);
    Scene* sc = gameObject ? gameObject->scene() : nullptr;
    Transform* t = nullptr;
    if (sc) {
        if (!source.empty()) { GameObject* o = sc->Find(source); if (o) t = o->transform; }
        else if (sc->mainCamera && sc->mainCamera->gameObject) t = sc->mainCamera->gameObject->transform;
    }
    if (!t) return heading;
    const Vec3 f = t->Forward(), p = t->Position();
    if (fromX) *fromX = p.x;
    if (fromZ) *fromZ = p.z;
    if (hasPos) *hasPos = true;
    if (std::fabs(f.x) < 1e-5f && std::fabs(f.z) < 1e-5f) return heading;   // looking straight up or down
    return BearingOf(f.x, f.z);
}

std::vector<UICompass::Marker> UICompass::Markers() const {
    std::vector<Marker> out;
    Scene* sc = gameObject ? gameObject->scene() : nullptr;
    if (!sc) return out;
    float fx = 0.0f, fz = 0.0f; bool has = false;
    Heading(&fx, &fz, &has);
    for (const std::string& n : Split(markers)) {
        GameObject* o = sc->Find(n);
        if (!o || !o->transform) continue;
        const Vec3 p = o->transform->Position();
        Marker m;
        m.name = n;
        const float dx = p.x - fx, dz = p.z - fz;
        m.bearing = BearingOf(dx, dz);
        m.distance = has ? std::sqrt(dx * dx + dz * dz) : 0.0f;
        out.push_back(std::move(m));
    }
    return out;
}

// ---- Item tooltip card ----------------------------------------------------------------------------------------------

std::string UIItemTooltipCard::CurrentItem() const {
    if (!itemVar.empty()) {
        const uivars::Value v = uivars::Read(gameObject, itemVar);
        if (v.found && !v.text.empty() && v.text != "0") return v.text;
    }
    return item;
}

bool UIItemTooltipCard::LoadItems() const {
    if (itemsFile.empty()) return true;
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path p(itemsFile);
    if (!p.is_absolute() && !fs::exists(p, ec) && !Character::AssetBaseDir().empty()) p = fs::path(Character::AssetBaseDir()) / p;
    m_loadedFile = itemsFile;
    ItemDatabase extra;
    if (!extra.Load(p.string(), nullptr)) return false;
    for (const ItemDefinition& d : extra.Items()) ItemDatabase::Get().Put(d);   // added next to the project's items
    return true;
}

std::vector<UIItemTooltipCard::Line> UIItemTooltipCard::Lines() const {
    std::vector<Line> out;
    if (!itemsFile.empty() && m_loadedFile != itemsFile) LoadItems();
    const std::string id = CurrentItem();
    if (id.empty()) return out;
    const Equipment* eq = nullptr;
    if (!equipment.empty() && gameObject && gameObject->scene())
        if (GameObject* e = gameObject->scene()->Find(equipment)) eq = e->GetComponent<Equipment>();
    const ItemTip tip = DescribeItem(ItemDatabase::Get().Resolve(id), count, nullptr, eq);
    for (const TipLine& t : tip.lines) {
        Line l; l.text = t.text; l.name = t.name; l.useTint = t.useTint; l.tint = t.tint;
        out.push_back(std::move(l));
    }
    return out;
}

} // namespace okay
