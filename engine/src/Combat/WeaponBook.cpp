#include "okay/Combat/WeaponDefinition.hpp"
#include "okay/Components/Equipment.hpp"
#include "okay/Components/NPCController.hpp"
#include "okay/Components/Stats.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include "okay/Components/SurvivalStats.hpp"
#include "okay/Core/DataAsset.hpp"
#include "okay/Inventory/ItemDatabase.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Scene/GameObject.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>

namespace okay {

namespace {

std::string Quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + '"';
}

std::string ReadQuoted(std::istream& in) {
    in >> std::ws;
    if (in.peek() != '"') {
        std::string t;
        in >> t;
        return t;
    }
    in.get();
    std::string out;
    char c;
    while (in.get(c)) {
        if (c == '\\') { if (in.get(c)) out += c; }
        else if (c == '"') break;
        else out += c;
    }
    return out;
}

float Finite(float v, float fallback) { return std::isfinite(v) ? v : fallback; }

void Sanitize(WeaponDefinition& w) {
    w.damage = std::max(0.0f, Finite(w.damage, 0.0f));
    w.range = std::max(0.0f, Finite(w.range, 0.0f));
    w.knockback = std::max(0.0f, Finite(w.knockback, 0.0f));
    w.swingTime = std::max(0.0f, Finite(w.swingTime, 0.0f));
    w.arc = std::max(0.0f, std::min(360.0f, Finite(w.arc, 120.0f)));
    w.hitStart = std::max(0.0f, Finite(w.hitStart, 0.0f));
    w.hitEnd = std::max(0.0f, Finite(w.hitEnd, 0.0f));
    w.combo = std::max(1, std::min(w.combo, 20));
    w.comboWindow = std::max(0.0f, Finite(w.comboWindow, 0.0f));
    w.comboScale = Finite(w.comboScale, 0.0f);
    w.hitboxWidth = std::max(0.0f, Finite(w.hitboxWidth, 0.0f));
    w.hitboxHeight = std::max(0.0f, Finite(w.hitboxHeight, 0.0f));
    w.hitboxDepth = std::max(0.0f, Finite(w.hitboxDepth, 0.0f));
    w.fireRate = std::max(0.0f, Finite(w.fireRate, 0.0f));
    w.magazine = std::max(0, std::min(w.magazine, 100000));
    if (w.ammo < -1) w.ammo = -1;
    w.ammo = std::min(w.ammo, 1000000);
    w.reload = std::max(0.0f, Finite(w.reload, 0.0f));
    w.spread = std::max(0.0f, Finite(w.spread, 0.0f));
    w.projectileSpeed = std::max(0.0f, Finite(w.projectileSpeed, 0.0f));
}

std::map<std::string, WeaponBook>& Cache() {
    static std::map<std::string, WeaponBook> cache;
    return cache;
}

} // namespace

WeaponBook& WeaponBook::Get() {
    static WeaponBook book;
    return book;
}

const WeaponBook& WeaponBook::Cached(const std::string& path) {
    if (path.empty()) return Get();
    auto& cache = Cache();
    auto it = cache.find(path);
    if (it != cache.end()) return it->second;
    WeaponBook& book = cache[path];
    book.Load(path);
    return book;
}

void WeaponBook::Forget(const std::string& path) { Cache().erase(path); }

void WeaponBook::Clear() { m_weapons.clear(); m_path.clear(); }

bool WeaponBook::Load(const std::string& path, std::string* err) {
    std::ifstream f(path, std::ios::binary);
    if (!f && !DataAsset::BaseDir().empty()) f.open(DataAsset::BaseDir() + path, std::ios::binary);
    if (!f) {
        if (err) *err = "can't open " + path;
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    if (!LoadText(ss.str(), err)) return false;
    m_path = path;
    Cache()[path] = *this;
    return true;
}

bool WeaponBook::Save(const std::string& path) const {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << Serialize();
    if (!f) return false;
    Cache()[path] = *this;
    return true;
}

bool WeaponBook::LoadText(const std::string& text, std::string* err) {
    std::istringstream in(text);
    std::string word;
    if (!(in >> word) || word != "okayweapon") {
        if (err) *err = "not an .okayweapon file";
        return false;
    }
    int version = 0;
    in >> version;
    std::deque<WeaponDefinition> weapons;
    std::string line;
    WeaponDefinition* cur = nullptr;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string key;
        if (!(ls >> key) || key[0] == '#') continue;
        if (key == "weapon") {
            weapons.emplace_back();
            cur = &weapons.back();
            cur->id = ReadQuoted(ls);
            continue;
        }
        if (key == "end") { cur = nullptr; continue; }
        if (!cur) continue;
        WeaponDefinition& w = *cur;
        if (key == "name") w.name = ReadQuoted(ls);
        else if (key == "kind") {
            std::string k;
            ls >> k;
            w.kind = (k == "gun" || k == "ranged") ? WeaponKind::Gun : WeaponKind::Melee;
        } else if (key == "item") w.item = ReadQuoted(ls);
        else if (key == "damage") ls >> w.damage;
        else if (key == "range") ls >> w.range;
        else if (key == "knockback") ls >> w.knockback;
        else if (key == "swing") ls >> w.swingTime;
        else if (key == "arc") ls >> w.arc;
        else if (key == "hit") ls >> w.hitStart >> w.hitEnd;
        else if (key == "combo") ls >> w.combo >> w.comboWindow >> w.comboScale;
        else if (key == "hitbox") {
            int on = 0;
            ls >> on >> w.hitboxWidth >> w.hitboxHeight >> w.hitboxDepth;
            w.useHitbox = on != 0;
        } else if (key == "fire") ls >> w.fireRate;
        else if (key == "magazine") ls >> w.magazine;
        else if (key == "ammo") ls >> w.ammo;
        else if (key == "reload") ls >> w.reload;
        else if (key == "spread") ls >> w.spread;
        else if (key == "hitscan") {
            int on = 1;
            ls >> on;
            w.hitscan = on != 0;
        } else if (key == "projectile") w.projectile = ReadQuoted(ls);
        else if (key == "speed") ls >> w.projectileSpeed;
    }
    weapons.erase(std::remove_if(weapons.begin(), weapons.end(),
                                 [](const WeaponDefinition& w) { return w.id.empty(); }),
                  weapons.end());
    for (auto& w : weapons) Sanitize(w);
    m_weapons.swap(weapons);
    return true;
}

std::string WeaponBook::Serialize() const {
    std::ostringstream o;
    o << "okayweapon 1\n";
    for (const auto& w : m_weapons) {
        o << "weapon " << Quote(w.id) << "\n";
        if (!w.name.empty()) o << "  name " << Quote(w.name) << "\n";
        o << "  kind " << (w.kind == WeaponKind::Gun ? "gun" : "melee") << "\n";
        if (!w.item.empty()) o << "  item " << Quote(w.item) << "\n";
        o << "  damage " << w.damage << "\n";
        o << "  range " << w.range << "\n";
        o << "  knockback " << w.knockback << "\n";
        o << "  swing " << w.swingTime << "\n";
        o << "  arc " << w.arc << "\n";
        o << "  hit " << w.hitStart << " " << w.hitEnd << "\n";
        o << "  combo " << w.combo << " " << w.comboWindow << " " << w.comboScale << "\n";
        o << "  hitbox " << (w.useHitbox ? 1 : 0) << " " << w.hitboxWidth << " " << w.hitboxHeight << " "
          << w.hitboxDepth << "\n";
        o << "  fire " << w.fireRate << "\n";
        o << "  magazine " << w.magazine << "\n";
        o << "  ammo " << w.ammo << "\n";
        o << "  reload " << w.reload << "\n";
        o << "  spread " << w.spread << "\n";
        o << "  hitscan " << (w.hitscan ? 1 : 0) << "\n";
        if (!w.projectile.empty()) o << "  projectile " << Quote(w.projectile) << "\n";
        o << "  speed " << w.projectileSpeed << "\n";
        o << "end\n";
    }
    return o.str();
}

const WeaponDefinition* WeaponBook::Find(const std::string& id) const {
    if (id.empty()) return nullptr;
    for (const auto& w : m_weapons) if (w.id == id) return &w;
    return nullptr;
}

WeaponDefinition* WeaponBook::FindMutable(const std::string& id) {
    if (id.empty()) return nullptr;
    for (auto& w : m_weapons) if (w.id == id) return &w;
    return nullptr;
}

const WeaponDefinition* WeaponBook::ForItem(const std::string& itemId) const {
    if (itemId.empty()) return nullptr;
    for (const auto& w : m_weapons) if (w.item == itemId) return &w;
    return nullptr;
}

WeaponDefinition& WeaponBook::Put(const WeaponDefinition& def) {
    if (WeaponDefinition* have = FindMutable(def.id)) {
        *have = def;
        Sanitize(*have);
        return *have;
    }
    m_weapons.push_back(def);
    Sanitize(m_weapons.back());
    return m_weapons.back();
}

bool WeaponBook::Remove(const std::string& id) {
    const auto n = m_weapons.size();
    m_weapons.erase(std::remove_if(m_weapons.begin(), m_weapons.end(),
                                   [&](const WeaponDefinition& w) { return w.id == id; }),
                    m_weapons.end());
    return m_weapons.size() != n;
}

std::vector<std::string> WeaponBook::Issues() const {
    std::vector<std::string> out;
    std::map<std::string, int> seen;
    for (const auto& w : m_weapons) ++seen[w.id];
    for (const auto& kv : seen)
        if (kv.second > 1) out.push_back("\"" + kv.first + "\" is used by more than one weapon. Each weapon needs its own id.");
    const bool itemsKnown = !ItemDatabase::Get().Items().empty();
    for (const auto& w : m_weapons) {
        const std::string who = w.DisplayName();
        if (w.kind == WeaponKind::Melee && w.swingTime > 0.0f && w.hitEnd <= w.hitStart)
            out.push_back(who + ": the hit window closes before it opens, so this swing can never land.");
        if (w.kind == WeaponKind::Gun && !(w.fireRate > 0.0f))
            out.push_back(who + ": shots per second is 0, so this gun cannot fire.");
        if (w.kind == WeaponKind::Gun && w.magazine > 0 && !(w.reload > 0.0f) && w.ammo == 0)
            out.push_back(who + ": it has no spare rounds and a reload time of 0.");
        if (!w.item.empty() && itemsKnown && !ItemDatabase::Get().Find(w.item))
            out.push_back(who + ": item \"" + w.item + "\" is not in the item list.");
    }
    return out;
}

const WeaponDefinition* FindWeapon(const std::string& id, const std::string& file) {
    if (!file.empty()) {
        const WeaponBook& book = WeaponBook::Cached(file);
        if (id.empty()) {
            if (book.Weapons().size() == 1) return &book.Weapons().front();
        } else if (const WeaponDefinition* w = book.Find(id)) {
            return w;
        }
    }
    if (!id.empty()) return WeaponBook::Get().Find(id);
    return nullptr;
}

const WeaponDefinition* WeaponForHolder(GameObject* go) {
    Equipment* eq = Equipment::Of(go);
    if (!eq) return nullptr;
    const int slot = eq->Find("Main Hand");
    if (slot < 0) return nullptr;
    const std::string& item = eq->ItemIn(slot);
    if (item.empty()) return nullptr;
    if (const ItemDefinition* def = ItemDatabase::Get().Find(item)) {
        const std::string named = def->Custom("weapon");
        if (!named.empty())
            if (const WeaponDefinition* w = WeaponBook::Get().Find(named)) return w;
    }
    return WeaponBook::Get().ForItem(item);
}

bool ApplyCombatDamage(GameObject* target, float amount, const Vec3& from, const Vec3& impulse) {
    if (!target || !(amount > 0.0f)) return false;
    bool hurt = false;
    if (auto* hp = target->GetComponent<HealthStat>()) {
        if (!hp->IsDead()) { hp->Damage(amount, from); hurt = true; }
    } else if (auto* surv = target->GetComponent<SurvivalStats>()) {
        if (!surv->dead) { surv->Damage(amount); hurt = true; }
    } else if (auto* stats = target->GetComponent<Stats>()) {
        if (!stats->IsDead()) { stats->TakeDamage(amount); hurt = true; }
    } else if (auto* npc = target->GetComponent<NPCController>()) {
        if (!npc->IsDead()) { npc->Damage(amount); hurt = true; }
    }
    if (hurt && (impulse.x != 0.0f || impulse.y != 0.0f || impulse.z != 0.0f))
        if (auto* body = target->GetComponent<Rigidbody3D>()) body->AddImpulse(impulse);
    return hurt;
}

} // namespace okay
