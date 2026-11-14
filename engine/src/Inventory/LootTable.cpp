#include "okay/Inventory/LootTable.hpp"
#include "okay/Core/DataAsset.hpp"
#include "okay/Inventory/ItemDatabase.hpp"
#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>

namespace okay {

namespace {
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
} // namespace

bool LootTable::LoadText(const std::string& text, std::string* err) {
    std::istringstream in(text);
    std::string word;
    if (!(in >> word) || word != "okayloot") { if (err) *err = "not an .okayloot file"; return false; }
    int version = 0; in >> version;
    LootTable t;
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string key;
        if (!(ls >> key) || key[0] == '#') continue;
        if (key == "rolls") ls >> t.rollsMin >> t.rollsMax;
        else if (key == "entry") {
            Entry e; e.item = ItemDatabase::Get().Resolve(ReadQuoted(ls));
            ls >> e.weight >> e.min >> e.max >> e.chance;
            if (e.item.empty() || !(e.weight > 0.0f)) continue;
            e.min = std::max(0, e.min); e.max = std::max(e.min, e.max);
            e.chance = std::max(0.0f, std::min(1.0f, e.chance));
            t.entries.push_back(e);
        }
    }
    t.rollsMin = std::max(0, std::min(t.rollsMin, 1000));
    t.rollsMax = std::max(t.rollsMin, std::min(t.rollsMax, 1000));
    *this = t;
    return true;
}

std::string LootTable::Serialize() const {
    std::ostringstream o;
    o << "okayloot 1\nrolls " << rollsMin << " " << rollsMax << "\n";
    for (const auto& e : entries)
        o << "entry " << Quote(e.item) << " " << e.weight << " " << e.min << " " << e.max << " " << e.chance << "\n";
    return o.str();
}

bool LootTable::Save(const std::string& path) const {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << Serialize();
    return (bool)f;
}

bool LootTable::Load(const std::string& path, std::string* err) {
    std::ifstream f(path, std::ios::binary);
    if (!f && !DataAsset::BaseDir().empty()) f.open(DataAsset::BaseDir() + path, std::ios::binary);
    if (!f) { if (err) *err = "can't open " + path; return false; }
    std::stringstream ss; ss << f.rdbuf();
    return LoadText(ss.str(), err);
}

namespace {
std::map<std::string, LootTable>& LootCache() {
    static std::map<std::string, LootTable> cache;
    return cache;
}
}

const LootTable& LootTable::Cached(const std::string& path) {
    auto& cache = LootCache();
    auto it = cache.find(path);
    if (it != cache.end()) return it->second;
    LootTable& t = cache[path];
    t.Load(path);
    return t;
}

void LootTable::Forget(const std::string& path) { LootCache().erase(path); }

std::vector<Inventory::Stack> LootTable::Roll(Random& rng) const {
    std::vector<Inventory::Stack> out;
    float total = 0.0f;
    for (const auto& e : entries) total += e.weight;
    if (total <= 0.0f) return out;
    const int rolls = rng.Range(rollsMin, rollsMax + 1);
    for (int r = 0; r < rolls; ++r) {
        float pick = rng.Range(0.0f, total);
        const Entry* e = &entries.back();
        for (const auto& c : entries) { if (pick < c.weight) { e = &c; break; } pick -= c.weight; }
        if (rng.Value() >= e->chance) continue;
        const int n = rng.Range(e->min, e->max + 1);
        if (n <= 0) continue;
        auto it = std::find_if(out.begin(), out.end(), [&](const Inventory::Stack& s) { return s.item == e->item; });
        if (it != out.end()) it->count += n; else out.push_back({e->item, n});
    }
    return out;
}

std::vector<LootTable::Share> LootTable::Shares() const {
    std::vector<Share> out;
    float total = 0.0f;
    for (const auto& e : entries) if (e.weight > 0.0f) total += e.weight;
    out.reserve(entries.size());
    for (int i = 0; i < (int)entries.size(); ++i) {
        Share s;
        s.item = entries[i].item;
        s.index = i;
        const float w = entries[i].weight > 0.0f ? entries[i].weight : 0.0f;
        s.weightShare = total > 0.0f ? w / total : 0.0f;
        float chance = entries[i].chance;
        if (chance < 0.0f) chance = 0.0f;
        if (chance > 1.0f) chance = 1.0f;
        s.dropShare = s.weightShare * chance;
        out.push_back(s);
    }
    return out;
}

std::vector<std::string> LootTable::Issues(const ItemDatabase& items) const {
    std::vector<std::string> out;
    if (entries.empty())
        out.push_back("This loot table has no entries, so a roll drops nothing.");
    if (rollsMax <= 0)
        out.push_back("This loot table rolls 0 times, so it drops nothing.");
    for (int i = 0; i < (int)entries.size(); ++i) {
        const Entry& e = entries[i];
        const std::string where = "Entry " + std::to_string(i + 1);
        const std::string named = e.item.empty() ? where : where + " (\"" + e.item + "\")";
        if (e.item.empty())
            out.push_back(where + " has no item chosen.");
        else if (!items.IsDefined(e.item))
            out.push_back(named + " isn't in the Item Database, so there is nothing to drop.");
        if (!(e.weight > 0.0f))
            out.push_back(named + " has no weight, so it never gets picked.");
        if (e.chance <= 0.0f)
            out.push_back(named + " has a 0% chance, so it never drops.");
        if (e.max <= 0)
            out.push_back(named + " drops a count of 0, so it never adds an item.");
        else if (e.min > e.max)
            out.push_back(named + " asks for between " + std::to_string(e.min) + " and " + std::to_string(e.max) + ". The higher number is used when it rolls.");
    }
    return out;
}

} // namespace okay
