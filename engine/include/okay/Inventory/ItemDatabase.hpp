#pragma once
#include "okay/Render/Color.hpp"
#include <cstdint>
#include <deque>
#include <string>
#include <utility>
#include <vector>

namespace okay {

/// Per-item-instance state carried in a slot or grid item: durability, a unique id for
/// one-of-a-kind items, and free key/value data (ammo in a mag, a custom name...).
/// Stacks only merge when this is equal on both sides (empty merges with empty).
struct ItemInstanceData {
    float durability = -1.0f;      ///< < 0 = not tracked
    std::uint64_t uid = 0;         ///< 0 = none; an item with a uid never stacks
    std::vector<std::pair<std::string, std::string>> values;

    bool Empty() const { return durability < 0.0f && uid == 0 && values.empty(); }
    bool operator==(const ItemInstanceData& o) const { return durability == o.durability && uid == o.uid && values == o.values; }
    bool operator!=(const ItemInstanceData& o) const { return !(*this == o); }
    /// Can two stacks with this data merge?
    bool StacksWith(const ItemInstanceData& o) const { return uid == 0 && o.uid == 0 && *this == o; }
    std::string Get(const std::string& key, const std::string& def = "") const {
        for (const auto& kv : values) if (kv.first == key) return kv.second;
        return def;
    }
    void Set(const std::string& key, const std::string& value) {
        for (auto& kv : values) if (kv.first == key) { kv.second = value; return; }
        values.push_back({key, value});
    }
};

/// Where an item can be worn or held.
enum class EquipSlot { None, Head, Chest, Legs, Feet, Hands, Back, MainHand, OffHand };
const char* EquipSlotName(EquipSlot s);
EquipSlot EquipSlotFromName(const std::string& name);

/// One kind of item, defined once and used by every inventory, chest, recipe and
/// builder. Inventories store the `id`; everything else (name shown, icon, stack size,
/// weight, grid size, rarity, what using it does) comes from here.
struct ItemDefinition {
    std::string id;                 ///< stable key stored in inventories; never shown
    std::string name;               ///< display name (rename freely; scenes keep working)
    std::string description;
    std::string icon;               ///< image path (empty = the UI's <iconFolder><id>.png)
    int   maxStack = 0;             ///< per-stack limit (0 = only the container's limit)
    float weight = 0.0f;            ///< per item
    int   gridW = 1, gridH = 1;     ///< footprint in a grid bag
    int   rarity = 0;               ///< tier (0 = common); higher sorts as rarer
    Color rarityColor = Color::FromBytes(255, 255, 255, 255);
    bool  hasRarityColor = false;   ///< tint borders with rarityColor
    std::string category;           ///< e.g. "Food", "Weapon", "Material"
    std::vector<std::string> tags;
    EquipSlot equip = EquipSlot::None;
    std::string useAction;          ///< e.g. "Eat", "Drink", "Heal" (empty = not usable)
    float useAmount = 0.0f;
    std::string worldPrefab;        ///< what dropping it spawns (.okayprefab)
    std::vector<std::pair<std::string, std::string>> custom;   ///< game-specific fields
    std::vector<std::string> legacyNames;   ///< old item names that load as this id
    bool loose = false;             ///< made automatically for an unknown name (not saved)

    const std::string& DisplayName() const { return name.empty() ? id : name; }
    std::string Custom(const std::string& key, const std::string& def = "") const;
    /// A field by name, as text, for scripts: name, description, icon, category, weight,
    /// stack, rarity, equip, use, prefab, width, height, or else a custom field.
    std::string FieldText(const std::string& field) const;
    /// The fields FieldText returns as numbers.
    static bool NumericField(const std::string& field);
    bool HasTag(const std::string& tag) const;
};

/// The project's item definitions (an `.okayitems` file). Every inventory looks items
/// up here by id. Loading a scene maps old item names to ids through Resolve(), and a
/// name nobody defined becomes a "loose" definition so old scenes keep working.
class ItemDatabase {
public:
    static ItemDatabase& Get();

    bool Load(const std::string& path, std::string* err = nullptr);
    bool Save(const std::string& path) const;
    bool LoadText(const std::string& text, std::string* err = nullptr);
    std::string Serialize() const;
    const std::string& Path() const { return m_path; }
    void Clear();

    /// The definition with this id, or null.
    const ItemDefinition* Find(const std::string& id) const;
    ItemDefinition* FindMutable(const std::string& id);
    /// The id an item string means: an id as-is, else a legacy name or display name of
    /// a definition, else (registering a loose definition) the string itself.
    std::string Resolve(const std::string& nameOrId);
    /// Resolve() then return its definition (never null for a non-empty string).
    const ItemDefinition* Def(const std::string& nameOrId);

    /// Add (or replace, by id) a definition. Returns the stored one.
    ItemDefinition& Put(const ItemDefinition& def);
    bool Remove(const std::string& id);
    /// All definitions. A deque, so pointers from Find()/Def() stay valid when more are added.
    const std::deque<ItemDefinition>& Items() const { return m_items; }
    std::deque<ItemDefinition>& Items() { return m_items; }
    /// Ids used by more than one definition (shown as a warning in the editor).
    std::vector<std::string> DuplicateIds() const;

    /// The per-stack limit for `item` in a container whose own limit is `container`
    /// (0 = unlimited): the smaller of the two non-zero limits.
    int StackLimit(const std::string& item, int container) const;

private:
    std::deque<ItemDefinition> m_items;
    std::string m_path;
};

/// The icon to draw for `item`: its definition's icon, else <folder><id>.png, else ""
/// (no folder and no icon = draw the name instead).
std::string ItemIconPath(const std::string& folder, const std::string& item);

} // namespace okay
