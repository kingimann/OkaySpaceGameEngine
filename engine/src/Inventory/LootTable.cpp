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

bool LootTable::Load(const std::string& path, std::string* err) {
    std::ifstream f(path, std::ios::binary);
    if (!f && !DataAsset::BaseDir().empty()) f.open(DataAsset::BaseDir() + path, std::ios::binary);
    if (!f) { if (err) *err = "can't open " + path; return false; }
    std::stringstream ss; ss << f.rdbuf();
    return LoadText(ss.str(), err);
}

const LootTable& LootTable::Cached(const std::string& path) {
    static std::map<std::string, LootTable> cache;
    auto it = cache.find(path);
    if (it != cache.end()) return it->second;
    LootTable& t = cache[path];
    t.Load(path);
    return t;
}

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

} // namespace okay
