#include "okay/Inventory/Recipe.hpp"
#include "okay/Components/ActionList.hpp"
#include "okay/Components/ItemPickup.hpp"
#include "okay/Core/DataAsset.hpp"
#include "okay/Inventory/InventoryOps.hpp"
#include "okay/Inventory/ItemDatabase.hpp"
#include "okay/Scene/Scene.hpp"
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

std::vector<Inventory::Stack> Recipe::Outputs() const {
    std::vector<Inventory::Stack> out;
    if (!output.empty()) out.push_back({output, outputCount < 1 ? 1 : outputCount});
    for (const auto& b : byproducts) if (!b.item.empty() && b.count > 0) out.push_back({b.item, b.count});
    return out;
}

bool Recipe::Unlocked() const {
    if (unlockFlag.empty()) return true;
    auto& vars = ActionList::Vars();
    auto it = vars.find(unlockFlag);
    return it != vars.end() && it->second != 0.0f;
}

// ---- RecipeBook ----

bool RecipeBook::LoadText(const std::string& text, std::string* err) {
    std::istringstream in(text);
    std::string word;
    if (!(in >> word) || word != "okayrecipes") { if (err) *err = "not an .okayrecipes file"; return false; }
    int version = 0; in >> version;
    std::vector<Recipe> out;
    Recipe* cur = nullptr;
    std::string line;
    ItemDatabase& db = ItemDatabase::Get();
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string key;
        if (!(ls >> key) || key[0] == '#') continue;
        if (key == "recipe") {
            out.emplace_back();
            cur = &out.back();
            cur->output = db.Resolve(ReadQuoted(ls));
            ls >> cur->outputCount;
            continue;
        }
        if (key == "end") { cur = nullptr; continue; }
        if (!cur) continue;
        if (key == "in" || key == "out") {
            Ingredient g; g.item = db.Resolve(ReadQuoted(ls)); ls >> g.count;
            if (g.item.empty() || g.count <= 0) continue;
            (key == "in" ? cur->inputs : cur->byproducts).push_back(g);
        }
        else if (key == "time")    { ls >> cur->craftTime; if (!(cur->craftTime >= 0.0f)) cur->craftTime = 0.0f; }
        else if (key == "station") cur->station = ReadQuoted(ls);
        else if (key == "unlock")  cur->unlockFlag = ReadQuoted(ls);
    }
    out.erase(std::remove_if(out.begin(), out.end(), [](const Recipe& r) { return r.output.empty() || r.inputs.empty(); }), out.end());
    for (auto& r : out) if (r.outputCount < 1) r.outputCount = 1;
    recipes.swap(out);
    return true;
}

std::string RecipeBook::Serialize() const {
    std::ostringstream o;
    o << "okayrecipes 1\n";
    for (const auto& r : recipes) {
        o << "recipe " << Quote(r.output) << " " << r.outputCount << "\n";
        for (const auto& g : r.inputs) o << "  in " << Quote(g.item) << " " << g.count << "\n";
        for (const auto& g : r.byproducts) o << "  out " << Quote(g.item) << " " << g.count << "\n";
        if (r.craftTime > 0.0f) o << "  time " << r.craftTime << "\n";
        if (!r.station.empty()) o << "  station " << Quote(r.station) << "\n";
        if (!r.unlockFlag.empty()) o << "  unlock " << Quote(r.unlockFlag) << "\n";
        o << "end\n";
    }
    return o.str();
}

bool RecipeBook::Load(const std::string& path, std::string* err) {
    std::ifstream f(path, std::ios::binary);
    if (!f && !DataAsset::BaseDir().empty()) f.open(DataAsset::BaseDir() + path, std::ios::binary);
    if (!f) { if (err) *err = "can't open " + path; return false; }
    std::stringstream ss; ss << f.rdbuf();
    return LoadText(ss.str(), err);
}

const RecipeBook& RecipeBook::Cached(const std::string& path) {
    static std::map<std::string, RecipeBook> cache;
    auto it = cache.find(path);
    if (it != cache.end()) return it->second;
    RecipeBook& b = cache[path];
    b.Load(path);
    return b;
}

// ---- CraftQueue ----

int CraftQueue::Affordable(const Inventory& inv, const Recipe& r) {
    const auto need = Inventory::SumStacks(r.inputs);
    if (need.empty() || r.output.empty()) return 0;
    long long n = 1 << 30;
    for (const auto& s : need) n = std::min<long long>(n, inv.Count(s.item) / s.count);
    return (int)n;
}

void CraftQueue::Give(Inventory& inv, const std::vector<Inventory::Stack>& stacks, Scene* scene, const Vec3& dropPos) {
    for (const auto& s : stacks) {
        const int left = inv.AddPartial(s.item, s.count);
        if (left > 0 && scene) ItemPickup::Spawn(*scene, s.item, left, ItemInstanceData{}, dropPos);
    }
}

int CraftQueue::Craft(Inventory& inv, const Recipe& r, int times) {
    if (!r.Unlocked() || r.output.empty()) return 0;
    const auto take = Inventory::SumStacks(r.inputs);
    int done = 0;
    for (; done < times && done < 9999; ++done) {
        if (r.craftTime <= 0.0f) {   // instant, atomic (inputs only go if the outputs fit)
            if (!InventoryOps::Craft(&inv, take, r.Outputs()).Ok()) break;
        } else {                     // queued: take the inputs now
            if (Affordable(inv, r) < 1 || !inv.Exchange(take, {})) break;
            m_jobs.push_back({r, r.craftTime});
        }
    }
    return done;
}

void CraftQueue::Update(float dt, Inventory& inv, Scene* scene, const Vec3& dropPos) {
    while (!m_jobs.empty() && dt > 0.0f) {
        Job& j = m_jobs.front();
        const float step = std::min(dt, j.left);
        j.left -= step; dt -= step;
        if (j.left > 0.0f) break;
        Give(inv, j.recipe.Outputs(), scene, dropPos);
        m_jobs.pop_front();
    }
}

void CraftQueue::Cancel(Inventory& inv, Scene* scene, const Vec3& dropPos) {
    for (const auto& j : m_jobs) Give(inv, Inventory::SumStacks(j.recipe.inputs), scene, dropPos);
    m_jobs.clear();
}

float CraftQueue::Progress() const {
    if (m_jobs.empty()) return 0.0f;
    const Job& j = m_jobs.front();
    return j.recipe.craftTime > 0.0f ? 1.0f - j.left / j.recipe.craftTime : 1.0f;
}

} // namespace okay
