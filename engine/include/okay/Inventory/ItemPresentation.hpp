#pragma once
// Shared item presentation for the player and the editor Play view: the tooltip
// (definition, stats, compare-to-equipped), the colour-tile fallback when an icon
// file is missing, and a warning when two inventory screens bind the same key.
#include "okay/Components/ChestInventory.hpp"
#include "okay/Components/CraftingMenu.hpp"
#include "okay/Components/CraftingStation.hpp"
#include "okay/Components/Equipment.hpp"
#include "okay/Components/GridInventory.hpp"
#include "okay/Components/GridInventoryUI.hpp"
#include "okay/Components/InventoryUI.hpp"
#include "okay/Inventory/ItemDatabase.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Scene/Scene.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace okay {

/// One line of an item tooltip. `useTint` draws `tint` (the rarity colour on the
/// name); otherwise the screen's tooltip text colour is used.
struct TipLine {
    std::string text;
    bool name = false;
    Color tint{};
    bool useTint = false;
};
struct ItemTip { std::vector<TipLine> lines; };

/// A stand-in icon: a coloured tile and one or two initials, used when the item
/// has no image (or the file is missing).
struct ItemGlyph {
    std::string letters;
    Color bg{0.25f, 0.28f, 0.36f, 1.0f};
    Color fg{0.96f, 0.96f, 0.98f, 1.0f};
};

namespace detail {
inline Color HueColor(unsigned hash, float S, float V) {
    float H = (float)(hash % 360);
    float C = V * S, X = C * (1.0f - std::fabs(std::fmod(H / 60.0f, 2.0f) - 1.0f)), m = V - C;
    float r = 0, g = 0, b = 0;
    if (H < 60)       { r = C; g = X; }
    else if (H < 120) { r = X; g = C; }
    else if (H < 180) { g = C; b = X; }
    else if (H < 240) { g = X; b = C; }
    else if (H < 300) { r = X; b = C; }
    else              { r = C; b = X; }
    return Color{r + m, g + m, b + m, 1.0f};
}
inline bool LooksNumeric(const std::string& v, float& out) {
    if (v.empty()) return false;
    char* end = nullptr;
    out = std::strtof(v.c_str(), &end);
    return end && end != v.c_str() && *end == '\0';
}
} // namespace detail

inline ItemGlyph GlyphFor(const std::string& item) {
    ItemGlyph g;
    const ItemDefinition* d = ItemDatabase::Get().Find(item);
    const std::string& src = (d && !d->name.empty()) ? d->name : item;
    bool take = true;
    for (unsigned char c : src) {
        if (c == ' ' || c == '_' || c == '-') { take = true; continue; }
        if (take && std::isalnum(c)) {
            g.letters.push_back((char)std::toupper(c));
            take = false;
            if (g.letters.size() == 2) break;
        }
    }
    if (g.letters.empty() && !src.empty())
        g.letters.push_back((char)std::toupper((unsigned char)src[0]));
    unsigned h = 2166136261u;
    for (unsigned char c : item) { h ^= c; h *= 16777619u; }
    g.bg = (d && d->hasRarityColor) ? d->rarityColor : detail::HueColor(h, 0.42f, 0.48f);
    return g;
}

/// Tooltip for `id` × `count`. `weightEach` overrides the definition weight (grid
/// items store their own). `gw`/`gh` add a size line. When `equip` is set and the
/// item has an equip slot, a compare block is appended against whatever is worn there.
inline ItemTip DescribeItem(const std::string& id, int count, const ItemInstanceData* data,
                            const Equipment* equip, float weightEach = -1.0f, int gw = 0, int gh = 0) {
    ItemTip tip;
    if (id.empty()) return tip;
    const ItemDefinition* d = ItemDatabase::Get().Find(id);
    auto add = [&](std::string text, bool name = false, const Color* tint = nullptr) {
        if (text.empty()) return;
        TipLine ln;
        ln.text = std::move(text);
        ln.name = name;
        if (tint) { ln.useTint = true; ln.tint = *tint; }
        tip.lines.push_back(std::move(ln));
    };
    add(d ? d->DisplayName() : id, true, (d && d->hasRarityColor) ? &d->rarityColor : nullptr);
    if (d) {
        std::string sub = d->category;
        if (d->equip != EquipSlot::None) {
            if (!sub.empty()) sub += "  ·  ";
            sub += EquipSlotName(d->equip);
        }
        if (d->rarity > 0) {
            if (!sub.empty()) sub += "  ·  ";
            sub += "rarity " + std::to_string(d->rarity);
        }
        add(sub);
        add(d->description);
    }
    if (count > 0) {
        std::string st = "Stack " + std::to_string(count);
        if (d && d->maxStack > 0) st += " / " + std::to_string(d->maxStack);
        add(st);
    }
    const float each = weightEach >= 0.0f ? weightEach : (d ? d->weight : 0.0f);
    if (each > 0.0f && count > 0) {
        char b[64];
        std::snprintf(b, sizeof(b), "Weight %.2f kg", each * (float)count);
        add(b);
    }
    if (gw > 0 && gh > 0) add(std::to_string(gw) + " x " + std::to_string(gh));
    if (d && !d->useAction.empty()) {
        char b[96];
        std::snprintf(b, sizeof(b), "Use %s  %g", d->useAction.c_str(), d->useAmount);
        add(b);
    }
    if (data && data->durability >= 0.0f) {
        char b[64];
        std::snprintf(b, sizeof(b), "Durability %.0f", data->durability);
        add(b);
    }
    if (d) {
        for (const auto& kv : d->custom) {
            if (kv.first.empty() || kv.first == "equipPrefab") continue;
            add(kv.first + ": " + kv.second);
        }
    }
    if (equip && d && d->equip != EquipSlot::None) {
        const Equipment::Slot* worn = nullptr;
        for (const auto& s : equip->slots) {
            if (s.item.empty()) continue;
            const ItemDefinition* wd = ItemDatabase::Get().Find(s.item);
            const bool slotMatch = s.accepts == d->equip || (wd && wd->equip == d->equip);
            if (slotMatch) { worn = &s; break; }
        }
        if (worn && worn->item == id) add("Equipped");
        else if (worn) {
            const ItemDefinition* wd = ItemDatabase::Get().Find(worn->item);
            add(std::string("Compared to ") + (wd ? wd->DisplayName() : worn->item));
            if (wd) {
                std::vector<std::string> keys;
                auto consider = [&](const ItemDefinition& def) {
                    for (const auto& kv : def.custom) {
                        float dummy;
                        if (detail::LooksNumeric(kv.second, dummy) &&
                            std::find(keys.begin(), keys.end(), kv.first) == keys.end())
                            keys.push_back(kv.first);
                    }
                };
                consider(*d); consider(*wd);
                for (const auto& k : keys) {
                    float a = 0, b = 0;
                    detail::LooksNumeric(d->Custom(k, "0"), a);
                    detail::LooksNumeric(wd->Custom(k, "0"), b);
                    if (a == b) continue;
                    char buf[96];
                    std::snprintf(buf, sizeof(buf), "%s  %+.0f", k.c_str(), a - b);
                    add(buf);
                }
            }
        }
    }
    return tip;
}

/// One line per shared key among inventory screens (bag, grid, chest, benches).
/// Empty when every key is unique. Key 0 (unbound) is ignored.
inline std::string InventoryKeyWarning(const Scene* scene) {
    if (!scene) return {};
    struct Hit { char key; std::string who; };
    std::vector<Hit> hits;
    auto add = [&](char key, const std::string& who) {
        if (!key || key == ' ') return;
        unsigned char u = (unsigned char)key;
        if (u >= 'A' && u <= 'Z') u = (unsigned char)(u - 'A' + 'a');
        hits.push_back({(char)u, who});
    };
    auto nameOf = [](const GameObject* g, const char* what) {
        return (g && !g->name.empty() ? g->name : std::string("?")) + " " + what;
    };
    for (const auto& up : scene->Objects()) {
        GameObject* g = up.get();
        if (!g) continue;
        if (auto* ui = g->GetComponent<InventoryUI>()) {
            add(ui->toggleKey, nameOf(g, "bag"));
            add(ui->sortKey, nameOf(g, "sort"));
            add(ui->dropKey, nameOf(g, "drop"));
            add(ui->useKey, nameOf(g, "use"));
        }
        if (auto* gi = g->GetComponent<GridInventory>()) add(gi->toggleKey, nameOf(g, "grid"));
        if (auto* gu = g->GetComponent<GridInventoryUI>()) add(gu->rotateKey, nameOf(g, "rotate"));
        if (auto* ch = g->GetComponent<ChestInventory>()) add(ch->openKey, nameOf(g, "chest"));
        if (auto* cs = g->GetComponent<CraftingStation>()) add(cs->toggleKey, nameOf(g, "bench"));
        if (auto* cm = g->GetComponent<CraftingMenu>()) add(cm->toggleKey, nameOf(g, "craft menu"));
    }
    std::string out;
    for (std::size_t i = 0; i < hits.size(); ++i) {
        std::string who = hits[i].who;
        int n = 1;
        for (std::size_t j = i + 1; j < hits.size(); ++j)
            if (hits[j].key == hits[i].key) { who += ", " + hits[j].who; ++n; hits[j].key = 0; }
        if (n > 1 && hits[i].key) {
            if (!out.empty()) out += "\n";
            out += std::string("Key '") + hits[i].key + "' is shared by " + who;
        }
    }
    return out;
}

} // namespace okay
