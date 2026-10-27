#include "okay/Inventory/ItemDatabase.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace okay {

namespace {
const char* kSlotNames[] = {"none", "head", "chest", "legs", "feet", "hands", "back", "mainhand", "offhand"};

std::string Quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) { if (c == '"' || c == '\\') out += '\\'; out += c; }
    return out + '"';
}
std::string ReadQuoted(std::istream& in) {
    in >> std::ws;
    if (in.peek() != '"') { std::string tok; in >> tok; return tok; }
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
} // namespace

const char* EquipSlotName(EquipSlot s) { return kSlotNames[(int)s]; }
EquipSlot EquipSlotFromName(const std::string& name) {
    for (int i = 0; i < (int)(sizeof(kSlotNames) / sizeof(kSlotNames[0])); ++i)
        if (name == kSlotNames[i]) return (EquipSlot)i;
    return EquipSlot::None;
}

std::string ItemDefinition::Custom(const std::string& key, const std::string& def) const {
    for (const auto& kv : custom) if (kv.first == key) return kv.second;
    return def;
}
bool ItemDefinition::HasTag(const std::string& tag) const {
    return std::find(tags.begin(), tags.end(), tag) != tags.end();
}

std::string ItemIconPath(const std::string& folder, const std::string& item) {
    if (const ItemDefinition* d = ItemDatabase::Get().Find(item))
        if (!d->icon.empty()) return d->icon;
    if (folder.empty() || item.empty() || item == "." || item == ".." || item.find("..") != std::string::npos)
        return {};
    for (unsigned char c : item)
        if (c < 32 || c == '/' || c == '\\' || c == ':' || c == '?' || c == '*' || c == '"' || c == '<' || c == '>' || c == '|')
            return {};
    return folder + item + ".png";
}

ItemDatabase& ItemDatabase::Get() {
    static ItemDatabase db;
    return db;
}

void ItemDatabase::Clear() { m_items.clear(); m_path.clear(); }

bool ItemDatabase::Load(const std::string& path, std::string* err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { if (err) *err = "can't open " + path; return false; }
    std::stringstream ss; ss << f.rdbuf();
    if (!LoadText(ss.str(), err)) return false;
    m_path = path;
    return true;
}

bool ItemDatabase::Save(const std::string& path) const {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << Serialize();
    return (bool)f;
}

// Format:
//   okayitems 1
//   item "wood"
//     name "Wood"
//     stack 64
//     ...
//   end
// Unknown keys are skipped, so newer files load in older builds.
bool ItemDatabase::LoadText(const std::string& text, std::string* err) {
    std::istringstream in(text);
    std::string word;
    if (!(in >> word) || word != "okayitems") { if (err) *err = "not an .okayitems file"; return false; }
    int version = 0; in >> version;
    std::deque<ItemDefinition> items;
    std::string line;
    ItemDefinition* cur = nullptr;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string key;
        if (!(ls >> key) || key[0] == '#') continue;
        if (key == "item") { items.emplace_back(); cur = &items.back(); cur->id = ReadQuoted(ls); continue; }
        if (key == "end") { cur = nullptr; continue; }
        if (!cur) continue;
        ItemDefinition& d = *cur;
        if      (key == "name")        d.name = ReadQuoted(ls);
        else if (key == "description") d.description = ReadQuoted(ls);
        else if (key == "icon")        d.icon = ReadQuoted(ls);
        else if (key == "stack")       ls >> d.maxStack;
        else if (key == "weight")      ls >> d.weight;
        else if (key == "size")        ls >> d.gridW >> d.gridH;
        else if (key == "rarity") {
            ls >> d.rarity;
            Color c;
            if (ls >> c.r >> c.g >> c.b >> c.a) { d.rarityColor = c; d.hasRarityColor = true; }
        }
        else if (key == "category")    d.category = ReadQuoted(ls);
        else if (key == "tags")        { for (std::string t = ReadQuoted(ls); !t.empty(); t = ReadQuoted(ls)) d.tags.push_back(t); }
        else if (key == "equip")       d.equip = EquipSlotFromName(ReadQuoted(ls));
        else if (key == "use")         { d.useAction = ReadQuoted(ls); ls >> d.useAmount; }
        else if (key == "prefab")      d.worldPrefab = ReadQuoted(ls);
        else if (key == "custom")      { std::string k = ReadQuoted(ls); d.custom.push_back({k, ReadQuoted(ls)}); }
        else if (key == "legacy")      d.legacyNames.push_back(ReadQuoted(ls));
    }
    for (auto& d : items) {   // sane values whatever the file says
        if (d.maxStack < 0) d.maxStack = 0;
        if (!(d.weight >= 0.0f)) d.weight = 0.0f;
        d.gridW = std::max(1, std::min(d.gridW, 64));
        d.gridH = std::max(1, std::min(d.gridH, 64));
    }
    items.erase(std::remove_if(items.begin(), items.end(), [](const ItemDefinition& d) { return d.id.empty(); }), items.end());
    m_items.swap(items);
    return true;
}

std::string ItemDatabase::Serialize() const {
    std::ostringstream o;
    o << "okayitems 1\n";
    for (const auto& d : m_items) {
        if (d.loose) continue;
        o << "item " << Quote(d.id) << "\n";
        if (!d.name.empty())        o << "  name " << Quote(d.name) << "\n";
        if (!d.description.empty()) o << "  description " << Quote(d.description) << "\n";
        if (!d.icon.empty())        o << "  icon " << Quote(d.icon) << "\n";
        if (d.maxStack > 0)         o << "  stack " << d.maxStack << "\n";
        if (d.weight > 0.0f)        o << "  weight " << d.weight << "\n";
        if (d.gridW != 1 || d.gridH != 1) o << "  size " << d.gridW << " " << d.gridH << "\n";
        if (d.rarity != 0 || d.hasRarityColor) {
            o << "  rarity " << d.rarity;
            if (d.hasRarityColor) o << " " << d.rarityColor.r << " " << d.rarityColor.g << " " << d.rarityColor.b << " " << d.rarityColor.a;
            o << "\n";
        }
        if (!d.category.empty())    o << "  category " << Quote(d.category) << "\n";
        if (!d.tags.empty())        { o << "  tags"; for (const auto& t : d.tags) o << " " << Quote(t); o << "\n"; }
        if (d.equip != EquipSlot::None) o << "  equip " << EquipSlotName(d.equip) << "\n";
        if (!d.useAction.empty())   o << "  use " << Quote(d.useAction) << " " << d.useAmount << "\n";
        if (!d.worldPrefab.empty()) o << "  prefab " << Quote(d.worldPrefab) << "\n";
        for (const auto& kv : d.custom) o << "  custom " << Quote(kv.first) << " " << Quote(kv.second) << "\n";
        for (const auto& n : d.legacyNames) o << "  legacy " << Quote(n) << "\n";
        o << "end\n";
    }
    return o.str();
}

const ItemDefinition* ItemDatabase::Find(const std::string& id) const {
    for (const auto& d : m_items) if (d.id == id) return &d;
    return nullptr;
}
ItemDefinition* ItemDatabase::FindMutable(const std::string& id) {
    for (auto& d : m_items) if (d.id == id) return &d;
    return nullptr;
}

namespace {
std::string TrimCopy(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}
std::string Fold(const std::string& s) {
    std::string o = s;
    for (char& c : o) c = (char)std::tolower((unsigned char)c);
    return o;
}
bool FoldMatch(const ItemDefinition& d, const std::string& fold) {
    if (Fold(d.id) == fold || Fold(d.name) == fold) return true;
    for (const auto& n : d.legacyNames) if (Fold(n) == fold) return true;
    return false;
}
}

std::string ItemDatabase::Resolve(const std::string& nameOrId) {
    const std::string key = TrimCopy(nameOrId);
    if (key.empty() || Find(key)) return key;
    for (const auto& d : m_items)
        if (!d.loose && std::find(d.legacyNames.begin(), d.legacyNames.end(), key) != d.legacyNames.end()) return d.id;
    for (const auto& d : m_items)
        if (!d.loose && d.name == key) return d.id;
    const std::string fold = Fold(key);
    const ItemDefinition* hit = nullptr;
    int hits = 0;
    for (const auto& d : m_items) {
        if (d.loose || !FoldMatch(d, fold)) continue;
        hit = &d;
        if (++hits > 1) break;
    }
    if (hits == 1 && hit) return hit->id;   // "Wood" is the one item whose id is "wood"
    ItemDefinition loose;   // an item nobody defined: keep it working as-is
    loose.id = key;
    loose.loose = true;
    m_items.push_back(loose);
    return key;
}

const ItemDefinition* ItemDatabase::Def(const std::string& nameOrId) {
    if (nameOrId.empty()) return nullptr;
    return Find(Resolve(nameOrId));
}

ItemDefinition& ItemDatabase::Put(const ItemDefinition& def) {
    if (ItemDefinition* d = FindMutable(def.id)) { *d = def; return *d; }
    m_items.push_back(def);
    return m_items.back();
}

bool ItemDatabase::Remove(const std::string& id) {
    auto it = std::find_if(m_items.begin(), m_items.end(), [&](const ItemDefinition& d) { return d.id == id; });
    if (it == m_items.end()) return false;
    m_items.erase(it);
    return true;
}

std::vector<std::string> ItemDatabase::DuplicateIds() const {
    std::set<std::string> seen, dup;
    for (const auto& d : m_items) if (!seen.insert(d.id).second) dup.insert(d.id);
    return std::vector<std::string>(dup.begin(), dup.end());
}

std::vector<std::string> ItemDatabase::CaseConflicts() const {
    std::map<std::string, std::string> seen;
    std::vector<std::string> out;
    for (const auto& d : m_items) {
        if (d.loose || d.id.empty()) continue;
        const std::string f = Fold(d.id);
        auto it = seen.find(f);
        if (it == seen.end()) seen.emplace(f, d.id);
        else if (it->second != d.id) out.push_back(d.id + " and " + it->second);
    }
    return out;
}

int ItemDatabase::StackLimit(const std::string& item, int container) const {
    const ItemDefinition* d = Find(item);
    const int own = d ? d->maxStack : 0;
    if (own > 0 && container > 0) return std::min(own, container);
    return own > 0 ? own : container;
}

} // namespace okay
