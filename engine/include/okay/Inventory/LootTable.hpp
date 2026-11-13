#pragma once
#include "okay/Components/Inventory.hpp"
#include "okay/Core/Random.hpp"
#include <string>
#include <vector>

namespace okay { class ItemDatabase; }

namespace okay {

/// A loot table (`.okayloot`): each roll picks one entry by weight, which then drops
/// (if its chance passes) a random count between min and max.
///
///     okayloot 1
///     rolls 1 3                    # how many picks (min max)
///     entry "wood" 5 1 4 1.0       # item  weight  min  max  chance
///     entry "gold" 1 1 1 0.5
struct LootTable {
    struct Entry { std::string item; float weight = 1.0f; int min = 1, max = 1; float chance = 1.0f; };
    std::vector<Entry> entries;
    int rollsMin = 1, rollsMax = 1;

    /// One entry's share of a single pick. `weightShare` is its weight over the
    /// weights that are above zero; `dropShare` is that times its chance.
    struct Share {
        std::string item;
        int index = 0;
        float weightShare = 0.0f;
        float dropShare = 0.0f;
    };

    bool LoadText(const std::string& text, std::string* err = nullptr);
    std::string Serialize() const;
    /// Write Serialize() to `path`.
    bool Save(const std::string& path) const;
    /// Load from a file (relative paths also tried against DataAsset's base dir).
    bool Load(const std::string& path, std::string* err = nullptr);
    /// Loaded once per path and reused.
    static const LootTable& Cached(const std::string& path);
    /// Drop a cached table so the next Cached() reads the file again.
    static void Forget(const std::string& path);

    /// Roll the table: what drops (same item summed).
    std::vector<Inventory::Stack> Roll(Random& rng) const;
    /// Each entry's share of one pick (empty weights count as no share).
    std::vector<Share> Shares() const;
    /// Plain-English problems, including items that are not a defined item.
    std::vector<std::string> Issues(const ItemDatabase& items) const;
};

} // namespace okay
