#include "okay/Inventory/InventoryPersistence.hpp"
#include "okay/Components/Equipment.hpp"
#include "okay/Components/GridInventory.hpp"
#include "okay/Components/Inventory.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include <fstream>
#include <map>
#include <sstream>

namespace okay {
namespace InventoryPersistence {

namespace {
struct Carried {
    bool hasInv = false, hasGrid = false, hasEquip = false;
    std::vector<Inventory::Slot> slots;
    std::vector<GridItem> items, overflow;
    std::vector<Inventory::Slot> worn;   // per Equipment slot (count 1 = worn)
};
// What `e` wears, one entry per slot, with worn bags' contents.
std::vector<Inventory::Slot> Worn(const Equipment& e) {
    std::vector<Inventory::Slot> w(e.slots.size());
    for (std::size_t i = 0; i < e.slots.size(); ++i)
        if (!e.slots[i].item.empty()) w[i] = Inventory::Slot{e.slots[i].item, 1, e.CurrentData((int)i)};
    return w;
}
void Wear(Equipment& e, const std::vector<Inventory::Slot>& w) {
    for (std::size_t i = 0; i < e.slots.size(); ++i) {
        const bool has = i < w.size() && !Inventory::Empty(w[i]);
        e.Restore((int)i, has ? w[i].item : std::string(), has ? w[i].data : ItemInstanceData{});
    }
}
std::map<std::string, Carried>& Pending() { static std::map<std::string, Carried> p; return p; }

std::string Quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) { if (c == '"' || c == '\\') out += '\\'; out += c; }
    return out + '"';
}
std::string ReadQuoted(std::istream& in) {
    in >> std::ws;
    if (in.peek() != '"') { std::string t; in >> t; return t; }
    in.get();
    std::string out; char c;
    while (in.get(c)) { if (c == '\\') { if (in.get(c)) out += c; } else if (c == '"') break; else out += c; }
    return out;
}
void WriteData(std::ostream& o, const ItemInstanceData& d) {
    o << " " << d.durability << " " << d.uid << " " << d.values.size();
    for (const auto& kv : d.values) o << " " << Quote(kv.first) << " " << Quote(kv.second);
}
void ReadData(std::istream& in, ItemInstanceData& d) {
    std::size_t n = 0;
    in >> d.durability >> d.uid >> n;
    for (std::size_t i = 0; i < n && i < 4096 && in; ++i) { std::string k = ReadQuoted(in); d.values.push_back({k, ReadQuoted(in)}); }
}
void WriteGridItem(std::ostream& o, const char* key, const GridItem& it) {
    o << key << " " << Quote(it.name) << " " << it.w << " " << it.h << " " << it.x << " " << it.y << " " << it.count << " " << it.weight;
    WriteData(o, it.data);
    o << "\n";
}
GridItem ReadGridItem(std::istream& in) {
    GridItem it;
    it.name = ItemDatabase::Get().Resolve(ReadQuoted(in));
    in >> it.w >> it.h >> it.x >> it.y >> it.count >> it.weight;
    ReadData(in, it.data);
    return it;
}
} // namespace

void BeforeSceneLoad(Scene& oldScene) {
    Pending().clear();
    for (const auto& up : oldScene.Objects()) {
        const GameObject* go = up.get();
        if (!go) continue;
        Carried c;
        if (auto* inv = go->GetComponent<Inventory>()) if (inv->keepOnSceneLoad) { c.hasInv = true; c.slots = inv->slots; }
        if (auto* g = go->GetComponent<GridInventory>()) if (g->keepOnSceneLoad) { c.hasGrid = true; c.items = g->items; c.overflow = g->overflow; }
        if (auto* e = go->GetComponent<Equipment>()) if (e->keepOnSceneLoad) { c.hasEquip = true; c.worn = Worn(*e); }
        if (c.hasInv || c.hasGrid || c.hasEquip) Pending()[go->name] = c;
    }
}

void AfterSceneLoad(Scene& newScene) {
    for (auto& kv : Pending()) {
        GameObject* go = newScene.Find(kv.first);
        if (!go) continue;   // no such object in the new scene: nothing to carry into
        if (kv.second.hasInv) if (auto* inv = go->GetComponent<Inventory>()) { inv->slots = kv.second.slots; inv->NotifyChanged(); }
        if (kv.second.hasGrid) if (auto* g = go->GetComponent<GridInventory>()) {
            g->items = kv.second.items; g->overflow = kv.second.overflow; g->Validate();
        }
        if (kv.second.hasEquip) if (auto* e = go->GetComponent<Equipment>()) Wear(*e, kv.second.worn);
    }
    Pending().clear();
}

std::string SerializeObject(const GameObject& go) {
    std::ostringstream o;
    o << "okayinv 1\n";
    if (auto* inv = go.GetComponent<Inventory>()) {
        o << "inventory " << inv->slots.size() << "\n";
        for (const auto& s : inv->slots) {
            o << "slot " << Quote(s.item) << " " << s.count;
            WriteData(o, s.data);
            o << "\n";
        }
    }
    if (auto* g = go.GetComponent<GridInventory>()) {
        o << "grid\n";
        for (const auto& it : g->items) WriteGridItem(o, "item", it);
        for (const auto& it : g->overflow) WriteGridItem(o, "overflow", it);
    }
    if (auto* e = go.GetComponent<Equipment>()) {
        o << "equipment " << e->slots.size() << "\n";
        for (const auto& s : Worn(*e)) {
            o << "worn " << Quote(s.item) << " " << s.count;
            WriteData(o, s.data);
            o << "\n";
        }
    }
    return o.str();
}

bool LoadObjectText(GameObject& go, const std::string& text) {
    std::istringstream in(text);
    std::string word;
    if (!(in >> word) || word != "okayinv") return false;
    int version = 0; in >> version;
    Inventory* inv = go.GetComponent<Inventory>();
    GridInventory* g = go.GetComponent<GridInventory>();
    std::vector<Inventory::Slot> slots;
    Equipment* e = go.GetComponent<Equipment>();
    std::vector<GridItem> items, overflow;
    std::vector<Inventory::Slot> worn;
    bool sawInv = false, sawGrid = false, sawEquip = false;
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string key;
        if (!(ls >> key)) continue;
        if (key == "inventory") sawInv = true;
        else if (key == "grid") sawGrid = true;
        else if (key == "slot") {
            Inventory::Slot s;
            s.item = ItemDatabase::Get().Resolve(ReadQuoted(ls));
            ls >> s.count;
            ReadData(ls, s.data);
            if (s.count <= 0) s = Inventory::Slot{};
            slots.push_back(s);
        }
        else if (key == "equipment") sawEquip = true;
        else if (key == "worn") {
            Inventory::Slot s;
            s.item = ItemDatabase::Get().Resolve(ReadQuoted(ls));
            ls >> s.count;
            ReadData(ls, s.data);
            if (s.count <= 0 || s.item.empty()) s = Inventory::Slot{};
            worn.push_back(s);
        }
        else if (key == "item") items.push_back(ReadGridItem(ls));
        else if (key == "overflow") overflow.push_back(ReadGridItem(ls));
    }
    if (sawInv && inv) { inv->slots = slots; inv->NotifyChanged(); }
    if (sawGrid && g) { g->items = items; g->overflow = overflow; g->Validate(); }
    if (sawEquip && e) Wear(*e, worn);
    return true;
}

bool SaveObject(const GameObject& go, const std::string& path) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << SerializeObject(go);
    return (bool)f;
}

bool LoadObject(GameObject& go, const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss; ss << f.rdbuf();
    return LoadObjectText(go, ss.str());
}

} // namespace InventoryPersistence
} // namespace okay
